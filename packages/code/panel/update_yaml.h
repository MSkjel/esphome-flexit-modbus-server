// update.cpp for update.yaml: the component and settings.h behind its hooks, the panel's bus address from the link,
// and the check for a new version through fetch.h
#pragma once
#include <algorithm>
#include <cstring>
#include <functional>
#include <new>
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

// The start of the file, where its version is
struct Head : fetch::Request {
  uint8_t head[HEAD_LEN];
  size_t got = 0;
  bool feed(const char *p, size_t n) override {
    n = std::min(n, HEAD_LEN - got);
    memcpy(head + got, p, n);
    got += n;
    return got < HEAD_LEN;
  }
};

// The file at the Panel Firmware URL, looked at every check_interval and when Home Assistant asks
struct Check : fetch::Asker {
  const std::string *url = nullptr;  // the text's state
  fetch::Request *make(int) override {
    auto *r = new (std::nothrow) Head();
    if (r) {
      r->url = *url;
      r->header = "Range";
      r->value = "bytes=0-" + std::to_string(HEAD_LEN - 1);
      r->tx_buffer = 4096;  // GitHub's releases redirect to a URL about 1.7 KB long
    }
    return r;
  }
  bool got(fetch::Request &r) override {
    auto &h = static_cast<Head &>(r);
    return check_head(h.head, h.got);
  }
};
inline Check check;

// update.yaml's Panel Firmware entity. It has no state until both versions are known, so a stock panel's says unknown
// rather than offer an update. url is the Panel Firmware URL text, every its check_interval, and press the Flash Panel
// Firmware button, which holds up the loop through the download.
template<typename Entity, typename Text>
inline void attach(Entity *entity, Text *url, const char *every, std::function<void()> press) {
  check.what = "panel firmware version";
  check.server = "the Panel Firmware URL";
  check.url = &url->state;
  check.every_s = std::max<uint32_t>(picture::interval_ms(every) / 1000, 60);
  fetch::add(check);
  entity->on_check([]() {
    if (!check.asking)
      check.ask();
  });
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
