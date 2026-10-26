#pragma once
#include <cstdint>
#include <functional>
#include <algorithm>
#include <map>
#include "esphome/core/hal.h"

namespace esphome {

// Self-keyed timeouts. The test calls run() as its time passes.
class Scheduler {
 public:
  void set_timeout(const void *self, uint32_t timeout, std::function<void()> &&func) {
    timeouts_[self] = {millis() + timeout, std::move(func)};
  }
  bool cancel_timeout(const void *self) { return timeouts_.erase(self) != 0; }
  void run() {
    for (auto it = due_(); it != timeouts_.end(); it = due_()) {
      auto func = std::move(it->second.func);
      timeouts_.erase(it);
      func();
    }
  }
  void clear() { timeouts_.clear(); }

 protected:
  struct Timeout {
    uint32_t at;
    std::function<void()> func;
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
