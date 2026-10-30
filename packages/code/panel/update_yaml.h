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
  handlers[K_FW_VERSION] = [firmware = handlers[K_FW_VERSION]](uint32_t v) {
    firmware(v);
    panel_version = (int32_t) v;
    if (on_change)
      on_change();
  };
  on_link_note = set_link_note;
  id(server).add_on_status_callback(pump);
  id(server).add_on_read_callback(pump);
  id(server).add_on_frame_callback(on_frame);
}

// update.yaml's Panel Firmware entity. It has no state until both versions are known, so a stock panel's says unknown
// rather than offer an update. press is the Flash Panel Firmware button, which holds up the loop through the download.
template<typename Entity> inline void attach(Entity *entity, std::function<void()> check, std::function<void()> press) {
  entity->on_check(std::move(check));
  entity->on_install([press = std::move(press)]() {
    installing = true;
    on_change();
    press();
    installing = false;
    on_change();
  });
  on_change = [entity]() {
    if (panel_version >= 0 && latest_version >= 0)
      entity->show(version_text(panel_version), version_text(latest_version), latest_version > panel_version,
                   installing || running(), running() ? last_pct : -1, link_note);
  };
}

}  // namespace flexit::panel::update
