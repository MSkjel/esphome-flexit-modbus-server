// MET Norway's Locationforecast 2.0, read as it comes in. Plain C++ without ESPHome, see met_scene.h.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../panel/json_scan.h"

namespace flexit::panel::met {

enum Icon : uint8_t {
  I_NONE,
  I_SUN,
  I_MOON,
  I_PARTLY_SUN,
  I_PARTLY_MOON,
  I_CLOUD,
  I_RAIN,
  I_SNOW,
  I_SLEET,
  I_THUNDER,
  I_FOG
};

struct Hour {     // one hour of the forecast, for the overlay
  uint32_t time;  // UTC
  float temp;
  Icon icon;
};

inline constexpr int HOURS = 18;
// MET symbol codes go up to 42 chars, lightssleetshowersandthunder_polartwilight (sic). Longer ones are cut.
inline constexpr size_t SYMBOL = 48;

struct Weather {
  bool ok = false;
  uint32_t time = 0;  // forecast hour, UTC seconds since 1970
  float temp = 10, cloud = 0, cloud_low = 0, cloud_mid = 0, cloud_high = 0, fog = 0;  // %
  float rain = 0;                                                                     // mm in the coming hour
  float wind = 0, gust = 0, wind_from = 0, thunder = 0;                               // m/s, m/s, degrees, %
  float wave = -1;           // significant wave height in m, from Oceanforecast (see Sea). -1 = none
  char symbol[SYMBOL] = "";  // MET symbol for the coming hour, like "lightrainshowers_day"
  Hour hour[HOURS] = {};     // this hour onwards, as far as the response went
  int hours = 0;
};

// MET symbol code to our Icon, see https://api.met.no/weatherapi/weathericon/2.0/documentation
Icon icon_of(const char *s);

// The forecast hour that covers t, the first hour's weather before the hours start
Hour now_of(const Weather &w, uint32_t t);

// When the forecast has run out, an hour after its last hour
inline uint32_t ends(const Weather &w) { return (w.hours ? w.hour[w.hours - 1].time : w.time) + 3600; }

// Reads a Locationforecast 2.0 answer as it comes in, an hour at a time, keeping only what it takes from it. The
// first hour is the weather now, it and the ones after it are for the overlay. "complete" has the cloud layers and
// fog, "compact" doesn't.
struct Forecast : JsonScan {
  Weather w;  // w.ok once it has the first hour
  // More of the answer. False once it has HOURS hours, or what came can't be read.
  bool feed(const char *p, size_t n) { return scan(p, n) && !stop; }
  void value(int depth, const char *key, int index, const char *v) override;
  void end(int depth, const char *key) override;

 private:
  struct Next {  // next_1_hours' or next_6_hours'
    bool seen = false;
    float rain = 0, thunder = 0;
    char symbol[SYMBOL] = "";
  };
  bool stop = false;
  // the hour being read
  uint32_t time = 0;
  float temp = 0;
  bool has_temp = false, has_cloud = false;
  Next n1, n6;
};

// The waves from Oceanforecast 2.0: the significant wave height, hourly from about now. MET Norway has them for
// points at sea, and points near the coast go to the nearest one; inland the timeseries is empty, and outside MET's
// area it answers 422.
inline constexpr int SEA_HOURS = 24;
struct Sea {
  bool ok = false;
  int hours = 0;
  uint32_t time[SEA_HOURS] = {};  // UTC
  float height[SEA_HOURS] = {};   // m, the mean of the highest third of the waves
};

// The wave height at t, UTC: the hour nearest it, if that's within three hours. -1 without one.
float wave_at(const Sea &sea, uint32_t t);

// Reads an Oceanforecast 2.0 answer as Forecast does a forecast, an hour at a time
struct Waves : JsonScan {
  Sea sea;
  // More of the answer. False once it has SEA_HOURS hours, or what came can't be read.
  bool feed(const char *p, size_t n) { return scan(p, n) && !stop; }
  void value(int depth, const char *key, int index, const char *v) override;
  void end(int depth, const char *key) override;

 private:
  bool stop = false, has = false;
  uint32_t time = 0;
  float height = 0;
};

}  // namespace flexit::panel::met
