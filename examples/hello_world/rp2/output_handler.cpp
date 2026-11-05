/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "output_handler.h"

#include <algorithm>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/pwm.h"

#include "constants.h"

namespace {

// Shared between the inference loop and the PWM interrupt handler.
volatile int g_led_brightness = 0;
constexpr int kUpdateDelayMs = 50;
constexpr unsigned int kLogEveryNInferences = 10;

// For details on what this code is doing, see
// https://github.com/raspberrypi/pico-examples/blob/master/pwm/led_fade
extern "C" void on_pwm_wrap() {
  // Clear the interrupt flag that brought us here
  pwm_clear_irq(pwm_gpio_to_slice_num(PICO_DEFAULT_LED_PIN));
  // Square the value to make the LED's brightness appear more linear
  // Note this range matches with the wrap value
  pwm_set_gpio_level(PICO_DEFAULT_LED_PIN, g_led_brightness * g_led_brightness);
}

void init_pwm_fade() {
  // Tell the LED pin that the PWM is in charge of its value.
  gpio_set_function(PICO_DEFAULT_LED_PIN, GPIO_FUNC_PWM);
  // Figure out which slice we just connected to the LED pin
  uint slice_num = pwm_gpio_to_slice_num(PICO_DEFAULT_LED_PIN);

  // Mask our slice's IRQ output into the PWM block's single interrupt line,
  // and register our interrupt handler
  pwm_clear_irq(slice_num);
  pwm_set_irq_enabled(slice_num, true);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
  irq_set_enabled(PWM_IRQ_WRAP, true);

  // Get some sensible defaults for the slice configuration. By default, the
  // counter is allowed to wrap over its maximum range (0 to 2**16-1)
  pwm_config config = pwm_get_default_config();
#ifdef POLOLU_3PI_2040_ROBOT
  // The robot's yellow LED is active-low.
  pwm_config_set_output_polarity(
      &config, pwm_gpio_to_channel(PICO_DEFAULT_LED_PIN) == PWM_CHAN_A,
      pwm_gpio_to_channel(PICO_DEFAULT_LED_PIN) == PWM_CHAN_B);
#endif
  // Set divider, reduces counter clock to sysclock/this value
  pwm_config_set_clkdiv(&config, 4.f);
  // Load the configuration into our PWM slice, and set it running.
  pwm_init(slice_num, &config, true);
}

}  // namespace

void HandleOutput(float x_value, float y_value) {
  // Do this only once
  static bool is_initialized = false;
  if (!is_initialized) {
    init_pwm_fade();
    is_initialized = true;
    MicroPrintf("TFLite Micro: int8 sine model running. %d predictions per "
                "cycle, %d ms between updates.",
                kInferencesPerCycle, kUpdateDelayMs);
  }

  // Calculate the brightness of the LED such that y=-1 is fully off
  // and y=1 is fully on. The LED's brightness can range from 0-255.
  g_led_brightness = std::max(0, std::min(255, (int)(127.5f * (y_value + 1))));

  // Log twice per second so a USB terminal clearly shows fresh inferences,
  // including the model's input and predicted output driving the LED.
  static unsigned int inference_number = 0;
  ++inference_number;
  if ((inference_number - 1) % kLogEveryNInferences == 0) {
    const unsigned int cycle = (inference_number - 1) / kInferencesPerCycle + 1;
    MicroPrintf("Model inference %u | cycle %u | x=%.3f | prediction=%.3f | "
                "LED=%d/255",
                inference_number, cycle, static_cast<double>(x_value),
                static_cast<double>(y_value), static_cast<int>(g_led_brightness));
  }

  // 120 model predictions x 50 ms gives an approximately six-second fade.
  sleep_ms(kUpdateDelayMs);
}
