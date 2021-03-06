/* Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

/*
 * This file contains the definition of a simple Inference API for Paddle.
 *
 * ATTENTION: It requires some C++11 features, for lower version C++ or C, we
 * might release another API.
 */

#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace paddle {

enum PaddleDType {
  FLOAT32,
  INT64,
};

class PaddleBuf {
 public:
  PaddleBuf() = default;
  PaddleBuf(PaddleBuf&& other);
  // Copy only available when memory is managed externally.
  explicit PaddleBuf(const PaddleBuf&);
  PaddleBuf& operator=(const PaddleBuf&);
  PaddleBuf& operator=(PaddleBuf&&);
  // Do not own the memory.
  PaddleBuf(void* data, size_t length)
      : data_(data), length_(length), memory_owned_{false} {}
  // Own memory.
  explicit PaddleBuf(size_t length)
      : data_(new char[length]), length_(length), memory_owned_(true) {}
  // Resize to `length` bytes.
  void Resize(size_t length);
  // Reset to external memory.
  void Reset(void* data, size_t length);
  bool empty() const { return length_ == 0; }
  void* data() const { return data_; }
  size_t length() const { return length_; }

  ~PaddleBuf() { Free(); }

 private:
  void Free();
  void* data_{nullptr};  // pointer to the data memory.
  size_t length_{0};     // number of memory bytes.
  bool memory_owned_{true};
};

struct PaddleTensor {
  PaddleTensor() = default;
  std::string name;  // variable name.
  std::vector<int> shape;
  PaddleBuf data;  // blob of data.
  PaddleDType dtype;
  std::vector<std::vector<size_t>> lod;  // Tensor+LoD equals LoDTensor
};

enum class PaddleEngineKind {
  kNative = 0,         // Use the native Fluid facility.
  kAnakin,             // Use Anakin for inference.
  kAutoMixedTensorRT,  // Automatically mix Fluid with TensorRT.
  kAnalysis
  // TODO(Superjomn) support following engines latter.
  // kTensorRT,           // Use TensorRT for inference.
  // kAutoMixedAnakin,    // Automatically mix Fluid with Anakin.
};

/*
 * A simple Inference API for Paddle. Currently this API can be used by
 * non-sequence scenerios.
 */
class PaddlePredictor {
 public:
  struct Config;
  PaddlePredictor() = default;
  PaddlePredictor(const PaddlePredictor&) = delete;
  PaddlePredictor& operator=(const PaddlePredictor&) = delete;

  // Predict an record.
  // The caller should be responsible for allocating and releasing the memory of
  // `inputs`. `inputs` should be available until Run returns. Caller should be
  // responsible for the output tensor's buffer, either allocated or passed from
  // outside.
  virtual bool Run(const std::vector<PaddleTensor>& inputs,
                   std::vector<PaddleTensor>* output_data,
                   int batch_size = -1) = 0;

  // Clone a predictor that share the model weights, the Cloned predictor should
  // be thread-safe.
  virtual std::unique_ptr<PaddlePredictor> Clone() = 0;

  // Destroy the Predictor.
  virtual ~PaddlePredictor() = default;

  // The common configs for all the predictors.
  struct Config {
    std::string model_dir;  // path to the model directory.
  };
};

struct NativeConfig : public PaddlePredictor::Config {
  // GPU related fields.
  bool use_gpu{false};
  int device{0};
  float fraction_of_gpu_memory{-1.f};  // Negative to notify initialization.
  // NOTE: NOT use it, just for the internal test, will discard later
  bool _use_mkldnn{false};
  // Specify the variable's name of each input.
  bool specify_input_name{false};

  std::string prog_file;
  std::string param_file;
};

// Configurations for Anakin engine.
struct AnakinConfig : public PaddlePredictor::Config {
  enum TargetType { NVGPU = 0, X86 };
  int device;
  std::string model_file;
  int max_batch_size{-1};
  TargetType target_type;
};

struct TensorRTConfig : public NativeConfig {
  // Determine whether a subgraph will be executed by TRT.
  int min_subgraph_size{1};
  // While TensorRT allows an engine optimized for a given max batch size
  // to run at any smaller size, the performance for those smaller
  // sizes may not be as well-optimized. Therefore, Max batch is best
  // equivalent to the runtime batch size.
  int max_batch_size{1};
  // For workspace_size, refer it from here:
  // https://docs.nvidia.com/deeplearning/sdk/tensorrt-developer-guide/index.html#troubleshooting
  int workspace_size{1 << 30};
};

// NOTE WIP, not stable yet.
struct AnalysisConfig : public NativeConfig {
  //
  enum class IrPassMode {
    kSystem,   // Use system default passes, not customize.
    kInclude,  // Specify the passes in `ir_passes`.
    kExclude   // Specify the disabled passes in `ir_passes`.
  };

  bool enable_ir_optim = true;
  IrPassMode ir_mode{IrPassMode::kExclude};
  // attention lstm fuse works only on some specific models, disable as default.
  std::vector<std::string> ir_passes{"attention_lstm_fuse_pass"};
};

// A factory to help create different predictors.
//
// FOR EXTENSION DEVELOPER:
// Different predictors are designated by config type and engine kind. Similar
// configs can be merged, but there shouldn't be a huge config containing
// different fields for more than one kind of predictors.
//
// Similarly, each engine kind should map to a unique predictor implementation.
template <typename ConfigT, PaddleEngineKind engine = PaddleEngineKind::kNative>
std::unique_ptr<PaddlePredictor> CreatePaddlePredictor(const ConfigT& config);

int PaddleDtypeSize(PaddleDType dtype);

}  // namespace paddle
