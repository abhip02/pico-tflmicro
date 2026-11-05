#include <cmath>
#include <cstdio>

#include "output_handler.h"

void HandleOutput(float x, float y) {
  std::printf("%.6f,%.6f,%.6f\n", static_cast<double>(x),
              static_cast<double>(y), static_cast<double>(std::sin(x)));
}
