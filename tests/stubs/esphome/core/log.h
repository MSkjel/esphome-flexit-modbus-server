#pragma once
#include <cstdarg>
#include <cstdio>

namespace esphome {

inline bool log_on;  // print the component's log

__attribute__((format(printf, 2, 3))) inline void log(const char *tag, const char *format, ...) {
  if (!log_on)
    return;
  va_list args;
  va_start(args, format);
  printf("    [%s] ", tag);
  vprintf(format, args);
  printf("\n");
  va_end(args);
}

}  // namespace esphome

#define ESP_LOGE(tag, ...) esphome::log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) esphome::log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) esphome::log(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) esphome::log(tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) esphome::log(tag, __VA_ARGS__)
