#ifndef EXAMPLES_EMG_CLASSIFIER_EMG_FEATURES_H_
#define EXAMPLES_EMG_CLASSIFIER_EMG_FEATURES_H_

#include <cmath>
#include <cstdint>

namespace emg {
constexpr int kChannels = 8;
constexpr int kWindowSamples = 40;
constexpr int kStrideSamples = 10;
constexpr int kFeatureCount = 16;
constexpr int kClassCount = 5;
enum class Command { kStop = 0, kForward, kReverse, kLeft, kRight };
enum class Mode { kRaw, kFiltered, kPreprocessed };

inline int WindowSamples(Mode mode) {
  return mode == Mode::kPreprocessed ? 10 : 40;
}

// Time is the first dimension. Use int8_t for RAW/FILTERED or uint16_t for
// PREPROCESSED. Feature order and normalization MUST match data.py.
template <typename Sample>
inline void ExtractFeatures(const Sample samples[][kChannels],
                            float features[kFeatureCount], Mode mode = Mode::kRaw) {
  const int window_samples = WindowSamples(mode);
  for (int channel = 0; channel < kChannels; ++channel) {
    double sum_absolute = 0;
    double sum_squares = 0;
    for (int sample = 0; sample < window_samples; ++sample) {
      const double value = samples[sample][channel];
      sum_absolute += value < 0 ? -value : value;
      sum_squares += value * value;
    }
    const double mav = sum_absolute / window_samples;
    const double rms = std::sqrt(sum_squares / window_samples);
    if (mode == Mode::kPreprocessed) {
      // Match log1p(feature) / log(65536) in data.py without clipping uint16.
      features[channel] = static_cast<float>(std::log1p(mav) / std::log(65536.0));
      features[kChannels + channel] = static_cast<float>(std::log1p(rms) / std::log(65536.0));
    } else {
      features[channel] = static_cast<float>(mav / 128.0);
      features[kChannels + channel] = static_cast<float>(rms / 128.0);
    }
  }
}

// Read scale/zero_point from interpreter.input(0)->params. Raw Myo int8
// samples are not the model's quantized feature inputs.
inline bool QuantizeFeatures(const float features[kFeatureCount], float scale,
                             int zero_point, int8_t result[kFeatureCount]) {
  if (!std::isfinite(scale) || scale <= 0.0f) return false;
  for (int i = 0; i < kFeatureCount; ++i) {
    if (!std::isfinite(features[i])) return false;
    const float value = std::floor(features[i] / scale + 0.5f) + zero_point;
    result[i] = static_cast<int8_t>(value < -128 ? -128 : value > 127 ? 127 : value);
  }
  return true;
}
}  // namespace emg

#endif  // EXAMPLES_EMG_CLASSIFIER_EMG_FEATURES_H_
