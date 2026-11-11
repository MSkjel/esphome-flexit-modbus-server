// MET Norway's forecast, see met_forecast.h
#include "met_forecast.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "../panel/dates.h"

namespace flexit::panel::met {

// v as a number from lo to hi into f. False for what isn't one, like null, and f is left as it was.
static bool number(const char *v, float &f, float lo, float hi) {
  char *end;
  float x = strtof(v, &end);
  if (end == v || *end || !std::isfinite(x))
    return false;
  f = std::clamp(x, lo, hi);
  return true;
}

Icon icon_of(const char *s) {
  bool night = strstr(s, "_night") != nullptr || strstr(s, "_polartwilight") != nullptr;
  if (strstr(s, "thunder"))
    return I_THUNDER;
  if (strstr(s, "sleet"))
    return I_SLEET;
  if (strstr(s, "snow"))
    return I_SNOW;
  if (strstr(s, "rain"))
    return I_RAIN;
  if (strstr(s, "fog"))
    return I_FOG;
  if (strstr(s, "partlycloudy") || strstr(s, "fair"))
    return night ? I_PARTLY_MOON : I_PARTLY_SUN;
  if (strstr(s, "clearsky"))
    return night ? I_MOON : I_SUN;
  if (strstr(s, "cloudy"))
    return I_CLOUD;
  return I_NONE;
}

Hour now_of(const Weather &w, uint32_t t) {
  Hour h = {w.time, w.temp, icon_of(w.symbol)};
  for (int i = 0; i < w.hours && w.hour[i].time <= t; i++)
    h = w.hour[i];
  return h;
}

void Forecast::value(int depth, const char *key, int, const char *v) {
  if (depth == 4 && !*parent() && !strcmp(key, "time")) {
    time = utc_of(v);
    return;
  }
  if (depth != 7)
    return;
  const char *in = parent(2);
  if (!strcmp(in, "instant")) {
    if (!strcmp(key, "air_temperature"))
      has_temp = number(v, temp, -100, 100) || has_temp;
    if (w.ok)
      return;  // the rest is for the weather now
    static constexpr struct {
      const char *key;
      float Weather::*to;
      float most;  // percent, m/s and degrees
    } NOW[] = {{"cloud_area_fraction", &Weather::cloud, 100},
               {"cloud_area_fraction_low", &Weather::cloud_low, 100},
               {"cloud_area_fraction_medium", &Weather::cloud_mid, 100},
               {"cloud_area_fraction_high", &Weather::cloud_high, 100},
               {"fog_area_fraction", &Weather::fog, 100},
               {"wind_speed", &Weather::wind, 100},
               {"wind_speed_of_gust", &Weather::gust, 100},
               {"wind_from_direction", &Weather::wind_from, 360}};
    for (const auto &n : NOW)
      if (!strcmp(key, n.key) && number(v, w.*n.to, 0, n.most))
        has_cloud = has_cloud || n.to == &Weather::cloud;
    return;
  }
  bool one = !strcmp(in, "next_1_hours");
  if (!one && strcmp(in, "next_6_hours"))
    return;
  Next &n = one ? n1 : n6;
  n.seen = true;
  if (!strcmp(key, "symbol_code")) {
    strncpy(n.symbol, v, SYMBOL - 1);
    n.symbol[SYMBOL - 1] = 0;
  } else if (!strcmp(key, "precipitation_amount")) {
    number(v, n.rain, 0, 1000);
  } else if (!strcmp(key, "probability_of_thunder")) {
    number(v, n.thunder, 0, 100);
  }
}

// An hour's end: a later one without its time or temperature is skipped, without the first there's no forecast.
// Nothing after it has all it takes: the rest of the piece it came in is still scanned.
void Forecast::end(int depth, const char *key) {
  if (stop || depth != 4 || strcmp(key, "timeseries"))
    return;
  if (time && has_temp && (w.ok || has_cloud)) {
    if (!w.ok) {
      const Next &n = n1.seen ? n1 : n6;
      w.time = time;
      w.temp = temp;
      w.rain = n.rain;
      w.thunder = n.thunder;
      memcpy(w.symbol, n.symbol, SYMBOL);
      w.ok = true;
    }
    if (n1.seen)
      w.hour[w.hours++] = {time, temp, icon_of(n1.symbol)};
  }
  stop = !w.ok || w.hours == HOURS;
  time = 0;
  has_temp = false;
  n1 = n6 = Next();
}

float wave_at(const Sea &sea, uint32_t t) {
  float best = -1;
  uint32_t gap = 3 * 3600 + 1800 + 1;
  for (int i = 0; i < sea.hours; i++) {
    uint32_t mid = sea.time[i] + 1800, d = t > mid ? t - mid : mid - t;
    if (d < gap) {
      gap = d;
      best = sea.height[i];
    }
  }
  return best;
}

void Waves::value(int depth, const char *key, int, const char *v) {
  if (depth == 4 && !*parent() && !strcmp(key, "time"))
    time = utc_of(v);
  else if (depth == 7 && !strcmp(parent(2), "instant") && !strcmp(key, "sea_surface_wave_height"))
    has = number(v, height, 0, 100) || has;
}

// An hour's end: one without its time or waves is skipped, and nothing after it has all it keeps
void Waves::end(int depth, const char *key) {
  if (stop || depth != 4 || strcmp(key, "timeseries"))
    return;
  if (time && has) {
    sea.time[sea.hours] = time;
    sea.height[sea.hours++] = height;
    sea.ok = true;
  }
  stop = sea.hours == SEA_HOURS;
  time = 0;
  has = false;
}

}  // namespace flexit::panel::met
