// Runs an exported model with this repository's TFLite Micro implementation.
// This is a host validation tool; it does not control the robot or its motors.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "emg_features.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
bool HasShape(const TfLiteTensor* tensor, int width) {
  return tensor && tensor->type == kTfLiteInt8 && tensor->dims->size == 2 &&
         tensor->dims->data[0] == 1 && tensor->dims->data[1] == width;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    std::fprintf(stderr, "Usage: %s emg_model_int8.tflite verification.csv [RAW|FILTERED|PREPROCESSED]\n", argv[0]);
    return 1;
  }
  const std::string mode_name = argc == 4 ? argv[3] : "RAW";
  emg::Mode mode;
  if (mode_name == "RAW") mode = emg::Mode::kRaw;
  else if (mode_name == "FILTERED") mode = emg::Mode::kFiltered;
  else if (mode_name == "PREPROCESSED") mode = emg::Mode::kPreprocessed;
  else {
    std::fprintf(stderr, "Unknown recording mode: %s\n", argv[3]);
    return 1;
  }
  std::ifstream model_file(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(model_file)), {});
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  if (bytes.empty() || !tflite::VerifyModelBuffer(verifier)) {
    std::fprintf(stderr, "Cannot read a valid TFLite model from %s\n", argv[1]);
    return 1;
  }
  const auto* model = tflite::GetModel(bytes.data());
  if (model->version() != TFLITE_SCHEMA_VERSION) return 1;
  tflite::MicroMutableOpResolver<2> resolver;
  if (resolver.AddFullyConnected() != kTfLiteOk || resolver.AddSoftmax() != kTfLiteOk)
    return 1;
  // Starting budget, not measured RP2040 usage. Host pointer sizes differ.
  alignas(16) static uint8_t arena[32 * 1024];
  tflite::MicroInterpreter interpreter(model, resolver, arena, sizeof(arena));
  if (interpreter.AllocateTensors() != kTfLiteOk) return 1;
  if (interpreter.inputs_size() != 1 || interpreter.outputs_size() != 1 ||
      !HasShape(interpreter.input(0), emg::kFeatureCount) ||
      !HasShape(interpreter.output(0), emg::kClassCount)) {
    std::fprintf(stderr, "Expected int8 [1,16] input and int8 [1,5] output\n");
    return 1;
  }

  std::ifstream vectors(argv[2]);
  std::string line;
  std::getline(vectors, line);  // Header from train.py.
  int rows = 0;
  int max_error = 0;
  while (std::getline(vectors, line)) {
    std::istringstream row(line);
    std::string cell;
    std::vector<int> values;
    while (std::getline(row, cell, ',')) {
      std::istringstream number(cell);
      int value;
      if (!(number >> value) || !(number >> std::ws).eof()) {
        std::fprintf(stderr, "Invalid integer in verification row %d\n", rows + 1);
        return 1;
      }
      values.push_back(value);
    }
    const int kRawCount = emg::WindowSamples(mode) * emg::kChannels;
    if (values.size() != kRawCount + emg::kClassCount) {
      std::fprintf(stderr, "Wrong number of columns in verification row %d\n", rows + 1);
      return 1;
    }
    // int32 holds both signed samples and PREPROCESSED uint16 without narrowing.
    int32_t samples[emg::kWindowSamples][emg::kChannels];
    for (int i = 0; i < kRawCount + emg::kClassCount; ++i) {
      const bool unsigned_sample = mode == emg::Mode::kPreprocessed && i < kRawCount;
      if (values[i] < (unsigned_sample ? 0 : -128) || values[i] > (unsigned_sample ? 65535 : 127)) {
        std::fprintf(stderr, "Out-of-range value in verification row %d\n", rows + 1);
        return 1;
      }
      if (i < kRawCount) samples[i / emg::kChannels][i % emg::kChannels] = values[i];
    }
    float features[emg::kFeatureCount];
    emg::ExtractFeatures(samples, features, mode);
    auto* input = interpreter.input(0);
    if (!emg::QuantizeFeatures(features, input->params.scale,
                               input->params.zero_point, input->data.int8)) return 1;
    if (interpreter.Invoke() != kTfLiteOk) return 1;
    for (int i = 0; i < emg::kClassCount; ++i) {
      const int difference = std::abs(
          static_cast<int>(interpreter.output(0)->data.int8[i]) - values[kRawCount + i]);
      max_error = std::max(max_error, difference);
    }
    ++rows;
  }
  // Allow two output quantization steps for different kernel rounding.
  if (rows == 0 || max_error > 2) {
    std::fprintf(stderr, "Verification failed: %d windows, maximum int8 error %d\n", rows, max_error);
    return 1;
  }
  std::printf("Verified %d %s windows through C++ features and TFLite Micro; "
              "maximum int8 output difference: %d; host arena used: %zu bytes\n",
              rows, mode_name.c_str(), max_error, interpreter.arena_used_bytes());
  return 0;
}
