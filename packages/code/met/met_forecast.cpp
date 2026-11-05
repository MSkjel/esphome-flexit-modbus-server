// MET Norway's forecast, see met_forecast.h
#include "met_forecast.h"
#include <cstdlib>
#include <cstring>
#include "../panel/dates.h"

namespace flexit::panel::met {

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
    if (!strcmp(key, "air_temperature")) {
      temp = strtof(v, nullptr);
      has_temp = true;
    }
    if (w.ok)
      return;  // the rest is for the weather now
    static constexpr struct {
      const char *key;
      float Weather::*to;
    } NOW[] = {{"cloud_area_fraction", &Weather::cloud},
               {"cloud_area_fraction_low", &Weather::cloud_low},
               {"cloud_area_fraction_medium", &Weather::cloud_mid},
               {"cloud_area_fraction_high", &Weather::cloud_high},
               {"fog_area_fraction", &Weather::fog},
               {"wind_speed", &Weather::wind},
               {"wind_speed_of_gust", &Weather::gust},
               {"wind_from_direction", &Weather::wind_from}};
    for (const auto &n : NOW)
      if (!strcmp(key, n.key)) {
        w.*n.to = strtof(v, nullptr);
        has_cloud = has_cloud || n.to == &Weather::cloud;
      }
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
    n.rain = strtof(v, nullptr);
  } else if (!strcmp(key, "probability_of_thunder")) {
    n.thunder = strtof(v, nullptr);
  }
}

// An hour's end: a later one without its time or temperature is skipped, without the first there's no forecast
void Forecast::end(int depth, const char *key) {
  if (depth != 4 || strcmp(key, "timeseries"))
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

}  // namespace flexit::panel::met
