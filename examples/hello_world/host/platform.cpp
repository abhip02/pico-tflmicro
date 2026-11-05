#include <chrono>
#include <cstdio>

#include "tensorflow/lite/micro/debug_log.h"
#include "tensorflow/lite/micro/micro_time.h"
#include "tensorflow/lite/micro/system_setup.h"

namespace tflite {
void InitializeTarget() {}

uint32_t ticks_per_second() { return 1000000; }

uint32_t GetCurrentTimeTicks() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}
}  // namespace tflite

extern "C" void DebugLog(const char* format, va_list args) {
  vfprintf(stderr, format, args);
}

extern "C" int DebugVsnprintf(char* buffer, size_t size, const char* format,
                             va_list args) {
  return vsnprintf(buffer, size, format, args);
}
