// update.cpp for update.yaml: the component and settings.h behind its hooks, the panel's bus address from the link
#pragma once
#include <functional>
#include <string>
#include <utility>
#include "panel.h"
#include "settings.h"
#include "update.h"

namespace flexit::panel::update {

struct EspHooks : Hooks {
  void send_frame(uint8_t *f, size_t n) override { id(server).send_frame(f, n); }
  void expect_reply(uint8_t addr, uint8_t fc, size_t n) override { id(server).expect_reply(addr, fc, n); }
  bool is_own_frame() override { return id(server).is_own_frame(); }
  uint8_t server_address() override { return id(server).server_address(); }
  void set_updating(bool updating) override { id(server).set_updating(updating); }
  uint32_t quiet_min_us() override { return id(server).quiet_min_us(); }
  void save_settings() override { settings::save(); }
  bool settings_saved() override { return settings::saved(); }
  void settings_flashed() override { settings::flashed(); }
};

// update.yaml at boot, after panel.yaml's setup()
inline void setup(std::function<void(const std::string &)> status_fn, std::function<void(int)> progress_fn) {
  static EspHooks esp;
  hooks = &esp;
  on_status = std::move(status_fn);
  on_progress = std::move(progress_fn);
  handlers[K_BUS_ADDR] = [](uint32_t v) {  // the address the panel reports, if it's a Modbus one
    if (v >= 1 && v <= 247)
      panel_addr = (uint8_t) v;
  };
  id(server).add_on_status_callback(pump);
  id(server).add_on_read_callback(pump);
  id(server).add_on_frame_callback(on_frame);
}

}  // namespace flexit::panel::update
