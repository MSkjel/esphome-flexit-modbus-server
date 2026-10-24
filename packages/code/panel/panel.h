// Code for packages/panel.yaml. Icons, views, actions and settings go by their ids in plink_*.h, in lower case.
#pragma once
#include <bitset>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include "animation.h"
#include "items.h"

// True while update.h has the panel in its bootloader. Weak, the harness builds without update.h.
extern "C" bool flexit_panel_in_bootloader() __attribute__((weak));
#include "panel_link.h"
#include "picture.h"
#include "plink.h"
#include "plink_icons.h"

namespace flexit::panel {

// Action ids and view names from plink_keys.h, indexed by their number
#define ID_NAME(id, ...) #id,
static const char *const ACTION_IDS[] = {PLINK_ACTIONS(ID_NAME)};
#undef ID_NAME
#define VIEW_NAME(id, name) name,
static const char *const VIEW_NAMES[] = {PLINK_VIEWS(VIEW_NAME)};
#undef VIEW_NAME

// The PLINK_VALUES table from plink_values.h
struct Value {
  const char *id;
  uint16_t key;
  int32_t min, max;
  uint16_t flags, cx60, scale;
};
#define VALUE(id, key, field, lo, hi, fl, reg, sc) {#id, key, lo, hi, fl, reg, sc},
static const Value VALUES[] = {PLINK_VALUES(VALUE)};
#undef VALUE

// What to do with each key from the panel. update.yaml adds K_BUS_ADDR.
inline std::map<uint16_t, std::function<void(uint32_t)>> handlers;

// Set when the panel's HELLO has our link version. We ignore its other messages until then.
inline bool compatible;

// The last note, kept until the panel has it. A panel ignores what it gets before our hello, so it goes again if
// the panel restarts before taking it.
inline std::string note;
inline bool note_queued;     // it's in the link's queue
inline uint16_t note_psess;  // the panel's session at its last hello, the one the note is for

// Registers the CX60 has written since we started, and those still to go to the panel, see hello()
inline std::bitset<0x200> seen, resend;

inline void notify(const std::string &message, const std::string &icon_name, const std::string &level) {
  int lv = level == "alarm" ? NOTE_ALARM : level == "warning" ? NOTE_WARNING : NOTE_INFO;
  note = {(char) icon(icon_name.empty() ? "info" : icon_name), (char) lv};
  note += cut(message, PLINK_TEXT_MAX);
  note_queued = link::connected && link::send(K_NOTIFY, (const uint8_t *) note.data(), (int) note.size());
}

inline void show_view(const std::string &view) {
  int i = find(VIEW_NAMES, view);
  if (i >= 0) {
    link::send(K_SHOW_VIEW, (uint32_t) i);
  } else {
    ESP_LOGW("panel", "No view called '%s'", view.c_str());
  }
}

// panel_set, by the setting's lower-case id. The panel doesn't echo what we set, so it's handled here as if the
// panel had reported it. The LEDs only go by their selects, which hello() sends again.
inline void set(const std::string &setting, int value) {
  for (auto &v : VALUES)
    if ((v.flags & VF_LINK_W) && !(v.flags & (VF_CX60 | VF_ESP)) && v.key != K_LEDS && lower(v.id) == setting) {
      if (value < v.min || value > v.max) {
        ESP_LOGW("panel", "%s: %d to %d, not %d", setting.c_str(), (int) v.min, (int) v.max, value);
        return;
      }
      link::send(v.key, (uint32_t) value);
      auto it = handlers.find(v.key);
      if (it != handlers.end())
        it->second((uint32_t) value);
      return;
    }
  ESP_LOGW("panel", "No panel setting called '%s'", setting.c_str());
}

// Local time in seconds since 2000-01-01, as the panel and the CS60 count
inline void send_time() {
  ESPTime t = id(flexit_panel_time).now();
  if (t.is_valid())
    link::send(K_TIME, (uint32_t) (t.timestamp + ESPTime::timezone_offset() - 946684800));
}

// 2 bits per LED from the lowest: alarm, filter, OK. The selects' options are in LED_* order.
inline void send_leds() {
  link::send(K_LEDS, (uint32_t) (id(panel_led_alarm).active_index().value_or(0) |
                                 id(panel_led_filter).active_index().value_or(0) << 2 |
                                 id(panel_led_ok).active_index().value_or(0) << 4));
}

// After the panel's hello. A panel that started on its own (asked) may have missed what the CX60 sent while it
// was away, so every register it has written since we started goes again. The rest we only have from flash, and
// the panel's copy may be newer. Not when the panel only answered our hello: it has them.
inline void hello(bool asked) {
  if (asked)
    resend = seen;
  send_leds();
  send_time();
  items_hello();
  picture::restarted();
  anim::restarted();
}

// The panel started or answered our hello. Unsent values follow our HELLO, hello() sends the rest again.
inline void got_hello(uint32_t v) {
  unsigned version = v >> 16;
  link::forget();
  note_queued = false;
  note_psess = link::tx.psess;
  if (v & HELLO_ASK)
    link::send(K_HELLO, (uint32_t) PLINK_VERSION << 16);
  compatible = version == PLINK_VERSION;
  if (!compatible) {
    ESP_LOGE("panel", "The panel's firmware has link version %u, this ESP %u: update the older one", version,
             (unsigned) PLINK_VERSION);
    return;
  }
  ESP_LOGI("panel", "Panel says hello");
  hello(v & HELLO_ASK);
}

// The panel's language from before a restart, so the items' texts go out in it straight away
inline void restore_language() { language = (uint8_t) id(panel_language); }

// A few at a time, like the items. Not the status block, which comes every ~110 ms anyway, nor the clock, which
// send_time() covers.
inline void send_registers() {
  for (uint16_t r = 0; r < resend.size() && resend.any() && link::waiting() < link::BACKLOG; r++) {
    if (!resend[r] || (r >= 0x0BE && r <= 0x112) || r == 0x094 || r == 0x095) {
      resend.reset(r);
      continue;
    }
    uint16_t v = id(server).read_holding_register((flexit_modbus_server::HoldingRegisterIndex) r);
    if (!link::send(K_CX60_REG, (uint32_t) r << 16 | v))
      return;
    resend.reset(r);
  }
}

// panel_address: the bus address of the panel to link with
inline void setup(uint8_t panel_address) {
  link::setup(panel_address);
  restore_language();
  id(server).add_on_register_callback([](uint16_t reg, uint16_t) {
    if (reg < seen.size())
      seen.set(reg);
  });
  auto &h = handlers;
  h[K_ACTION] = [](uint32_t v) {  // an action picked in the panel's menu
    if (v > A_NONE && v <= A_REMOTE_LAST) {
      std::string name = lower(ACTION_IDS[v]);
      ESP_LOGI("panel", "Panel: %s", name.c_str());
      id(panel_action).trigger(name);
    }
  };
  h[K_PIC_ACK] = [](uint32_t v) {  // each picks out its own
    picture::ack(v);
    anim::ack(v);
  };
  h[K_ANIM_GONE] = anim::gone;
  for (int i = 0; i < N_ITEMS; i++)
    h[K_ITEM_VALUE + i] = [i](uint32_t v) { item_set(i, v); };
  h[K_ITEM_HELP] = item_help;
  h[K_LANGUAGE] = [](uint32_t v) {
    id(panel_language) = (int) v;
    set_language(v);
  };

  // The panel reports its settings when they change and after a hello.
  h[K_BRIGHTNESS] = [](uint32_t v) { id(panel_brightness).publish_state((float) v); };
  h[K_IDLE_BRIGHTNESS] = [](uint32_t v) { id(panel_idle_brightness).publish_state((float) v); };
  h[K_SCREENSAVER] = [](uint32_t v) {
    if (v < SAVER_COUNT)
      id(panel_screensaver).publish_state((size_t) v);
  };
  h[K_ROOM_TEMP] = [](uint32_t v) {
    int32_t t = (int32_t) v;
    id(panel_temperature).publish_state(t <= -32768 ? NAN : t / 10.0f);
  };
  h[K_FW_VERSION] = [](uint32_t v) {  // maj<<16 | min<<8 | patch
    char b[16];
    snprintf(b, sizeof b, "%u.%u.%u", (unsigned) (v >> 16 & 0xFF), (unsigned) (v >> 8 & 0xFF), (unsigned) (v & 0xFF));
    id(panel_firmware).publish_state(b);
  };
  h[K_NIGHT] = [](uint32_t v) { id(panel_night).publish_state(v != 0); };
  h[K_KEY_LOCK] = [](uint32_t v) { id(panel_key_lock).publish_state(v != 0); };

  // The CS60 only takes these from the lowest panel address, which is us, so the panel asks us to write them.
  for (auto &v : VALUES)
    if (v.flags & VF_MASTER) {
      uint16_t reg = v.cx60, scale = v.scale;
      h[v.key] = [reg, scale](uint32_t x) {
        ESP_LOGI("panel", "Panel sets 0x%03X to %d", reg, (int) (int16_t) x);
        id(server).send_cmd((flexit_modbus_server::HoldingRegisterIndex) reg, (uint16_t) (x * scale));
      };
    }

  // The panel answers with its hello and settings, and hello() sends ours.
  link::send(K_HELLO, (uint32_t) PLINK_VERSION << 16 | HELLO_ASK);
}

// Every 20 ms
inline void loop() {
  static int last_hour = -1;  // local hour when the time was last sent

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"  // update.h is always there on the ESP
  bool flashing = flexit_panel_in_bootloader && flexit_panel_in_bootloader();
#pragma GCC diagnostic pop
  bool connected = link::online() && compatible && !flashing;
  if (connected != link::connected) {
    link::connected = connected;
    if (connected) {
      ESP_LOGI("panel", "Panel connected");
    } else if (flashing) {
      ESP_LOGI("panel", "Panel is in its bootloader");
    } else {
      ESP_LOGW("panel", "Panel went quiet");
    }
  }

  auto in = std::move(link::in);
  link::in.clear();
  for (auto &m : in) {
    if (m.first == K_HELLO) {
      got_hello(m.second);
      continue;
    }
    if (!compatible)
      continue;
    auto it = handlers.find(m.first);
    if (it != handlers.end()) {
      it->second(m.second);
    } else {
      ESP_LOGV("panel", "Nothing here for 0x%03X = %u", m.first, (unsigned) m.second);
    }
  }

  // The note, once the panel's there, until it has it
  if (connected && !note.empty()) {
    if (!note_queued && link::room() >= 3 + note.size())
      note_queued = link::send(K_NOTIFY, (const uint8_t *) note.data(), (int) note.size());
    else if (note_queued && !link::queued(K_NOTIFY) && link::tx.psess == note_psess)
      note.clear();
  }

  // Each new local hour, so a DST change shows right away.
  ESPTime t = id(flexit_panel_time).now();
  if (connected && t.is_valid() && t.hour != last_hour) {
    last_hour = t.hour;
    send_time();
  }
  link::log_stats();
  if (connected) {
    send_registers();
    items_loop();
  }
  picture::loop();
  anim::loop();
}

}  // namespace flexit::panel
