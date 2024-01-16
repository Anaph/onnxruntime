// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/coreml/model/model.h"

namespace onnxruntime {
namespace coreml {

class Execution {};

Model::Model(const std::string& /*path*/, const logging::Logger& /*logger*/, uint32_t /*coreml_flags*/)
  : execution_(std::make_unique<Execution>()) {
}

Model::~Model() {}

Status Model::LoadModel() {
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Loading a CoreML model is not supported on this platform.");
}

Status Model::Predict(const std::unordered_map<std::string, OnnxTensorData>& /*inputs*/,
                      const std::unordered_map<std::string, OnnxTensorInfo>& /*outputs*/,
                      const GetOutputTensorMutableRawDataFn& /*get_output_tensor_mutable_raw_data_fn*/) {
  return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Executing a CoreML model is not supported on this platform.");
}

}  // namespace coreml
}  // namespace onnxruntime
