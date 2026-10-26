#pragma once
#include <cstdint>

namespace esphome {

// the test's clock
uint32_t micros();
uint32_t millis();
inline void delay(uint32_t) {}

}  // namespace esphome
