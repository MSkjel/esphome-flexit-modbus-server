// NOAA SWPC's planetary Kp for each 3 hours, observed for the past week and forecast for the next three days, read
// as it comes in. Its answer is a list of {"time_tag": "2026-11-08T21:00:00", "kp": 4.67, ...}, oldest first, in UTC.
// Plain C++ without ESPHome, see met_scene.h.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "../panel/dates.h"
#include "../panel/json_scan.h"

namespace flexit::panel::met {

inline constexpr const char *KP_URL = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json";
inline constexpr uint32_t KP_BLOCK_S = 3 * 3600;

// The newest of them, from a day or two back to the end of the forecast
struct KpBlocks {
  static constexpr int MAX = 32;
  struct Block {
    uint32_t t;  // its start
    float kp;
  };
  Block b[MAX];
  int n = 0;

  // The Kp for the 3 hours t is in, NAN without one
  float at(uint32_t t) const {
    for (int i = 0; i < n; i++)
      if (t >= b[i].t && t - b[i].t < KP_BLOCK_S)
        return b[i].kp;
    return NAN;
  }
  void add(Block k) {
    if (n == MAX)
      memmove(b, b + 1, --n * sizeof *b);
    b[n++] = k;
  }
};

struct KpReader : JsonScan {
  KpBlocks blocks;
  bool feed(const char *p, size_t n) { return scan(p, n); }
  void value(int depth, const char *key, int, const char *v) override {
    if (depth != 2)
      return;
    if (!strcmp(key, "time_tag")) {
      next.t = utc_of(v);
    } else if (!strcmp(key, "kp")) {
      char *e;
      float k = strtof(v, &e);
      next.kp = e != v && k >= 0 && k <= 9 ? k : NAN;
    }
  }
  void end(int depth, const char *) override {
    if (depth != 2)
      return;
    if (next.t && !std::isnan(next.kp))
      blocks.add(next);
    next = {0, NAN};
  }

 private:
  KpBlocks::Block next = {0, NAN};
};

}  // namespace flexit::panel::met
