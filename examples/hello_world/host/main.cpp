#include <cstdio>

#include "constants.h"
#include "main_functions.h"

// Run one complete cycle so the laptop demo terminates on its own.
int main() {
  setup();
  std::puts("x,predicted_sin_x,sin_x");
  for (int i = 0; i < kInferencesPerCycle; ++i) {
    loop();
  }
  return 0;
}
