// The panel's own settings around a flash, which erases them. update.h has them saved before it flashes, and once
// the panel says hello without any they go back, see K_SETTINGS. Only in RAM.
#pragma once
#include <vector>
#include "panel_link.h"
#include "plink.h"

namespace flexit::panel::settings {

inline std::vector<uint32_t> records;  // as the panel sent them, its format first and how many last
inline bool asked, all, restore;       // all: the last one came with every record, restore: after our flash
inline size_t next;                    // records sent back so far

// update.h: before it flashes
inline void save() {
  records.clear();
  all = restore = false;
  next = 0;
  asked = link::connected;  // stock firmware has no link, and starts from defaults anyway
  if (asked)
    link::send(K_SETTINGS, 0);
}

// update.h: they're in, or there was no panel to ask
inline bool saved() { return !asked || all; }

// update.h: the flash went fine
inline void flashed() { restore = all; }

inline void got(uint32_t v) {
  if (!asked || all)
    return;
  if (!(v >> 16))
    records.clear();
  records.push_back(v);
  if (v >> 16 == SETTINGS_END) {
    unsigned n = v & 0xFFFF;
    all = records.size() == n + 2 && !(records[0] >> 16);
    if (all) {
      ESP_LOGI("panel", "Saved the panel's %u settings for after the flash", n);
    } else {
      ESP_LOGW("panel", "The panel's settings didn't all come, it starts from defaults after the flash");
    }
  }
}

// A panel that started without settings after our flash gets them back. Once it says hello with its own, ours go.
inline void hello(bool fresh) {
  next = 0;
  if (restore && fresh) {
    ESP_LOGI("panel", "The panel started without settings, sending its %u back", (unsigned) records.size() - 2);
  } else if (restore) {
    restore = false;
    records.clear();
    ESP_LOGI("panel", "The panel is back with its settings");
  }
}

inline void loop() {
  while (restore && next < records.size() && link::waiting() < link::BACKLOG && link::send(K_SETTINGS, records[next]))
    next++;
}

}  // namespace flexit::panel::settings
