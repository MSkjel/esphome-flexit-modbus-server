#pragma once
#include <cstdint>
#include <functional>
#include "esphome/core/application.h"

namespace esphome {

namespace setup_priority {
inline constexpr float BUS = 1000.0f;
}

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }
  virtual void on_safe_shutdown() {}

 protected:
  // One per component, App.scheduler runs it
  void set_interval(uint32_t interval, std::function<void()> &&f) {
    App.scheduler.set_interval(this, interval, std::move(f));
  }
};

}  // namespace esphome
