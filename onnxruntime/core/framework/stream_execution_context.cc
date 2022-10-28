// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
#include "core/framework/stream_execution_context.h"
#include "core/framework/execution_provider.h"
#include "core/framework/execution_frame.h"
#include "core/framework/bfc_arena.h"
#include "core/framework/session_state.h"
#include "core/common/spin_pause.h"

namespace onnxruntime {

StreamExecutionContext ::StreamExecutionContext(const SessionState& sess_state,
                                                int32_t num_streams,
                                                gsl::span<const size_t> notification_owners,
                                                gsl::span<const int> feed_mlvalue_idxs,
                                                gsl::span<const OrtValue> feeds, gsl::span<const int> fetch_mlvalue_idxs,
                                                std::vector<OrtValue>& fetches,
                                                const InlinedHashMap<size_t, IExecutor::CustomAllocator>& fetch_allocators,
                                                size_t num_barriers,
                                                const logging::Logger& sess_logger,
                                                const DeviceStreamCollection& device_stream_map,
                                                bool single_thread_mode) : session_state_(&sess_state),
                                                                           frame_(feed_mlvalue_idxs,
                                                                                  feeds,
                                                                                  fetch_mlvalue_idxs,
                                                                                  fetches,
                                                                                  fetch_allocators,
                                                                                  sess_state,
                                                                                  device_stream_map.GetStreams()),
                                                                           logger_(&sess_logger),
                                                                           device_stream_map_(device_stream_map),
                                                                           count_down_barriers_(num_barriers),
                                                                           single_thread_mode_(single_thread_mode) {
  auto device_streams = device_stream_map_.GetStreams();
  notifications_.reserve(notification_owners.size());
  for (size_t i = 0; i < notification_owners.size(); ++i) {
    auto& stream = device_streams[notification_owners[i]];
    if (stream)
      notifications_.emplace_back(stream->CreateNotification(/*TODO: calculate num of consumers*/ 0));
    else
      notifications_.push_back(nullptr);
  }
  std::atomic_int* p_release_plan_buffer = new std::atomic_int[sess_state.GetExecutionPlan()->release_actions.size()];
  release_plan_ = std::unique_ptr<std::atomic_int[]>(p_release_plan_buffer);

  // init barreris
  for (size_t i = 0; i < num_barriers; ++i) {
    count_down_barriers_[i].Set(2);
  }
  // init remain task to number of streams
  remain_tasks_.Set(num_streams);
  // generate release plan (the ref counts)
  auto& release_actions = sess_state.GetExecutionPlan()->release_actions;
  for (size_t i = 0; i < release_actions.size(); ++i) {
    release_plan_[i] = static_cast<int>(release_actions[i].ref_count);
  }
}

const SessionState& StreamExecutionContext ::GetSessionState() const { return *session_state_; }

const logging::Logger& StreamExecutionContext ::GetLogger() const { return *logger_; }

ExecutionFrame& StreamExecutionContext ::GetExecutionFrame() { return frame_; }

synchronize::Notification* StreamExecutionContext ::GetNotification(size_t idx) { return notifications_[idx].get(); }

bool StreamExecutionContext ::DecCountDownBarrier(size_t barrier_id) {
  return count_down_barriers_[barrier_id].Dec();
}

Stream* StreamExecutionContext ::GetDeviceStream(size_t idx) {
  ORT_ENFORCE(idx < device_stream_map_.NumStreams());
  return device_stream_map_.GetStreams()[idx];
}

const Status& StreamExecutionContext ::TaskStatus() const {
  return task_status_;
}

void StreamExecutionContext ::CompleteTask() {
  remain_tasks_.Dec();
}

void StreamExecutionContext ::AddTask() {
  remain_tasks_.Inc();
}

void StreamExecutionContext ::WaitAll() {
  while (remain_tasks_.Get())
    onnxruntime::concurrency::SpinPause();
}

void StreamExecutionContext ::SetStatus(Status& status) {
  // TODO: if multiple worker report non-ok status,
  // what is our strategy? currently we just keep
  // a random one. as long as it is not OK, the
  // execution will fail.
  if (task_status_.IsOK() && !status.IsOK())
    task_status_ = status;
}

StreamExecutionContext ::~StreamExecutionContext() {}

void StreamExecutionContext ::RecycleNodeInputs(onnxruntime::NodeIndex node_index) {
  auto* execution_plan = session_state_->GetExecutionPlan();
  for (auto idx : execution_plan->node_release_list[node_index]) {
    if (--release_plan_[idx] == 0) {
      ORT_ENFORCE(frame_.ReleaseMLValue(static_cast<int>(execution_plan->release_actions[idx].value_index)).IsOK());
      LOGS(*logger_, INFO) << "ort value " << execution_plan->release_actions[idx].value_index << " released";
    }
  }
}

void RunSince(size_t stream_idx, StreamExecutionContext& ctx, SessionScope& session_scope, const bool& terminate_flag, size_t since) {
  if (!ctx.TaskStatus().IsOK()) {
    // already in bad status, terminate it
    ctx.CompleteTask();
    return;
  }

  // get logic stream
  auto& execution_plan = ctx.GetSessionState().GetExecutionPlan()->execution_plan;
  auto& logic_stream = execution_plan[stream_idx];
  size_t end = logic_stream->steps_.size();
#ifdef ENABLE_TRAINING
  auto* range = ctx.GetCurrentRange();
  if (range)
    end = std::min(end, range->stream_pc_range[stream_idx].second);
#endif

  if (since > end && since < logic_stream->steps_.size()) {
#ifdef ENABLE_TRAINING
    // this is a special handle for training
    // with ORTModule, we are partially execute the graph with a shared context.
    // there is a case that in forward pass we want to trigger downstream which
    // not in current range. We need to execute one step to consume the Barrier
    // counter otherwise later in backward the downstream won't execute correctly.
    // this is ugly, hopefully we won't need to worry about if deprecate ORTModule
    // by Torch Dynamo.
    if (!ctx.TaskStatus().IsOK()) {
      ctx.CompleteTask();
      return;
    }
    if (terminate_flag) {
      Status status_made = ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Exiting due to terminate flag being set to true.");
      ctx.SetStatus(status_made);
      ctx.CompleteTask();
      return;
    }
    bool continue_flag = true;
    Status status;
    ORT_TRY {
      status = logic_stream->steps_[since]->Execute(&ctx, stream_idx, session_scope, terminate_flag, continue_flag);
    }
    ORT_CATCH(const std::exception& ex) {
      ORT_HANDLE_EXCEPTION([&]() {
        status = ORT_MAKE_STATUS(ONNXRUNTIME, RUNTIME_EXCEPTION, ex.what());
      });
    }
    if (!status.IsOK()) {
      // terminate it
      ctx.SetStatus(status);
      ctx.CompleteTask();
      return;
    }
    if (continue_flag) {
      ORT_THROW("Execute the barrier step in backward range passed! this is not expected.");
    }
    ctx.CompleteTask();
    return;
#else
    ORT_THROW("Trigger execution beyond current range is not expected in inference build");
#endif
  }

  while (since < end) {
    if (!ctx.TaskStatus().IsOK()) {
      ctx.CompleteTask();
      return;
    }
    if (terminate_flag) {
      Status status_made = ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Exiting due to terminate flag being set to true.");
      ctx.SetStatus(status_made);
      ctx.CompleteTask();
      return;
    }
    bool continue_flag = true;
    Status status;
    ORT_TRY {
      status = logic_stream->steps_[since]->Execute(&ctx, stream_idx, session_scope, terminate_flag, continue_flag);
    }
    ORT_CATCH(const std::exception& ex) {
      ORT_HANDLE_EXCEPTION([&]() {
        status = ORT_MAKE_STATUS(ONNXRUNTIME, RUNTIME_EXCEPTION, ex.what());
      });
    }
    if (!status.IsOK()) {
      // terminate it
      ctx.SetStatus(status);
      ctx.CompleteTask();
      return;
    }
    if (!continue_flag) {
      // break but not terminate
      ctx.CompleteTask();
      return;
    }
    since++;
  }
  ORT_ENFORCE(since == end);
  ctx.CompleteTask();
  return;
}

void ScheduleDownstream(StreamExecutionContext& ctx,
                        size_t trigger,
                        bool single_thread_mode,
                        const bool& terminate_flag,
                        SessionScope& session_scope) {
  auto* ctx_ptr = &ctx;
  auto* plan = ctx.GetSessionState().GetExecutionPlan();
  auto& downstream_map = plan->downstream_map;
  auto* tp = single_thread_mode ? nullptr : ctx.GetSessionState().GetInterOpThreadPool();
  auto it = downstream_map.find(trigger);
  if (it != downstream_map.end()) {
    for (auto downstream : it->second) {
      // increase the task count before schedule down-stream
      ctx.AddTask();
      concurrency::ThreadPool::Schedule(tp,
                                        [ctx_ptr, downstream, &terminate_flag, &session_scope]() {
                                          RunSince(downstream.first, *ctx_ptr, session_scope, terminate_flag, downstream.second);
                                        });
    }
  }
}

}  // namespace onnxruntime
