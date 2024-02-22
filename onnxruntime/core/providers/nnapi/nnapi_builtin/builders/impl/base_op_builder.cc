// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/graph/graph_viewer.h"
#include "core/providers/nnapi/nnapi_builtin/builders/impl/base_op_builder.h"
#include "core/providers/shared/utils/utils.h"

namespace onnxruntime {
namespace nnapi {

namespace {
bool HasExternalInitializer(const InitializedTensorSet& initializers, const NodeUnit& node_unit,
                            const logging::Logger& logger) {
  const auto is_ext_initializer =
      [&](const NodeArg& node_arg) {
        const auto& input_name(node_arg.Name());
        const auto initializer = initializers.find(input_name);
        if (initializer == initializers.end())
          return false;

        const auto& tensor = *initializer->second;
        if (tensor.has_data_location() &&
            tensor.data_location() == ONNX_NAMESPACE::TensorProto_DataLocation_EXTERNAL) {
          LOGS(logger, VERBOSE) << "Initializer [" << input_name
                                << "] with external data location are not currently supported";
          return true;
        }

        return false;
      };

  const auto& inputs = node_unit.Inputs();
  for (const auto& input : inputs) {
    if (is_ext_initializer(input.node_arg))
      return true;

    if (!input.quant_param)
      continue;

    if (is_ext_initializer(input.quant_param->scale))
      return true;

    if (input.quant_param->zero_point && is_ext_initializer(*input.quant_param->zero_point))
      return true;
  }

  return false;
}
}  // namespace

Status BaseOpBuilder::AddToModelBuilder(ModelBuilder& model_builder, const NodeUnit& node_unit) const {
  OpSupportCheckParams params{
      model_builder.GetEffectiveFeatureLevel(),
      model_builder.UseNCHW(),
  };

  // We checked supported in IExecutionProvider::GetCapability.
  // Checking again in AddToModelBuilder which is called in IExecutionProvider::Compile is redundant.
  // ORT_RETURN_IF_NOT(IsOpSupported(model_builder.GetGraphViewer(), node_unit, params),
  //                  "Unsupported operator ", node_unit.OpType());

#ifndef NDEBUG
  model_builder.SetDebugCurrentOnnxNodeIndex(node_unit.Index());
#endif
  ORT_RETURN_IF_ERROR(AddToModelBuilderImpl(model_builder, node_unit));
  LOGS(model_builder.GetLogger(), VERBOSE) << "Operator name: [" << node_unit.Name()
                                           << "] type: [" << node_unit.OpType() << "] was added";
  return Status::OK();
}

bool BaseOpBuilder::IsOpSupported(const GraphViewer& graph_viewer, const NodeUnit& node_unit,
                                  const OpSupportCheckParams& params,
                                  const logging::Logger& logger) const {
  int32_t required_feature_level = GetMinSupportedNNAPIFeatureLevel(node_unit, params, logger);
  if (required_feature_level > params.android_feature_level) {
    LOGS(logger, VERBOSE) << "Current Android API level [" << params.android_feature_level
                          << "], Operator [" << node_unit.OpType()
                          << "] is only supported on API >" << required_feature_level;
    return false;
  }

  if (!IsNodeUnitTypeSupported(node_unit, logger))
    return false;

  if (!HasSupportedInputOutputs(graph_viewer, node_unit, params, logger))
    return false;

  // We do not support external initializers for now
  if (HasExternalInitializer(graph_viewer.GetAllInitializedTensors(), node_unit, logger))
    return false;

  if (!HasSupportedOpSet(node_unit, logger))
    return false;

  return IsOpSupportedImpl(graph_viewer, node_unit, params, logger);
}

bool BaseOpBuilder::HasSupportedInputOutputs(const GraphViewer& graph_viewer, const NodeUnit& node_unit,
                                             const OpSupportCheckParams& params, const logging::Logger& logger) const {
  // We do not support unknown(null) input shape
  auto has_supported_shape = [&logger](const NodeArg& node_arg, const std::string& name, const std::string& op_type) {
    const auto* shape_proto = node_arg.Shape();
    if (!shape_proto) {
      LOGS(logger, VERBOSE) << "Node [" << name << "] type [" << op_type
                            << "] Input [" << node_arg.Name() << "] has no shape";
      return false;
    }

    // We do not support dynamic shape input for now
    for (const auto& dim : shape_proto->dim()) {
      if (!dim.has_dim_value()) {
        LOGS(logger, VERBOSE) << "Dynamic shape is not supported for now, for input:" << node_arg.Name();
        return false;
      }
    }
    return true;
  };

  for (const auto& input : node_unit.Inputs()) {
    if (!input.node_arg.Exists()) {
      continue;
    }
    if (!has_supported_shape(input.node_arg, node_unit.Name(), node_unit.OpType()))
      return false;

    if (input.quant_param.has_value()) {
      if (!has_supported_shape(input.quant_param->scale, node_unit.Name(), node_unit.OpType()))
        return false;

      // zero point is optional
      if (input.quant_param->zero_point &&
          !has_supported_shape(*input.quant_param->zero_point, node_unit.Name(), node_unit.OpType()))
        return false;
    }
  }

  return HasSupportedInputOutputsImpl(graph_viewer, node_unit, params, logger);
}

bool BaseOpBuilder::InputIsFloat(const NodeUnit& node_unit, int idx, const logging::Logger& logger) {
  const auto& input = node_unit.Inputs()[idx].node_arg;

  int32_t input_type;
  if (!GetType(input, input_type, logger))
    return false;

  if (input_type != ONNX_NAMESPACE::TensorProto_DataType_FLOAT) {
    LOGS(logger, VERBOSE) << "[" << node_unit.OpType()
                          << "] Input type: [" << input_type
                          << "] is not supported for now";
    return false;
  }

  return true;
}

bool BaseOpBuilder::HasSupportedOpSet(const NodeUnit& node_unit, const logging::Logger& logger) const {
  auto since_version = node_unit.SinceVersion();
  if (since_version < GetMinSupportedOpSet(node_unit) || since_version > GetMaxSupportedOpSet(node_unit)) {
    LOGS(logger, VERBOSE) << node_unit.OpType() << " opset [" << since_version
                          << "] is only supported for opset ["
                          << GetMinSupportedOpSet(node_unit) << ", "
                          << GetMaxSupportedOpSet(node_unit) << "]";
    return false;
  }

  return true;
}

bool BaseOpBuilder::IsNodeUnitTypeSupported(const NodeUnit& node_unit, const logging::Logger& logger) const {
  if (node_unit.UnitType() == NodeUnit::Type::QDQGroup) {
    LOGS(logger, VERBOSE) << "QDQ NodeUnit [" << node_unit.OpType()
                          << "] is not supported for now";

    return false;
  }

  return true;
}

}  // namespace nnapi
}  // namespace onnxruntime
