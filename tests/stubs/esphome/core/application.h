#pragma once
#include <cstdint>
#include <functional>
#include <algorithm>
#include <map>
#include "esphome/core/hal.h"

namespace esphome {

// Timeouts and intervals, one per owner. The test calls run() as its clock moves.
class Scheduler {
 public:
  void set_timeout(const void *self, uint32_t timeout, std::function<void()> &&func) {
    timeouts_[self] = {millis() + timeout, std::move(func), 0};
  }
  void set_interval(const void *self, uint32_t interval, std::function<void()> &&func) {
    timeouts_[self] = {millis() + interval, std::move(func), interval};
  }
  bool cancel_timeout(const void *self) { return timeouts_.erase(self) != 0; }
  void run() {
    for (auto it = due_(); it != timeouts_.end(); it = due_()) {
      auto func = it->second.func;
      if (it->second.interval)
        it->second.at = millis() + it->second.interval;
      else
        timeouts_.erase(it);
      func();
    }
  }
  void clear() { timeouts_.clear(); }

 protected:
  struct Timeout {
    uint32_t at;
    std::function<void()> func;
    uint32_t interval;  // 0 = a timeout
  };
  std::map<const void *, Timeout> timeouts_;
  std::map<const void *, Timeout>::iterator due_() {
    return std::find_if(timeouts_.begin(), timeouts_.end(),
                        [](const auto &t) { return (int32_t) (millis() - t.second.at) >= 0; });
  }
};

struct Application {
  Scheduler scheduler;
  void feed_wdt() {}
};
inline Application App;

}  // namespace esphome
