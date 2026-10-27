// MET Norway's forecast, see met_forecast.h
#include "met_forecast.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "met_sky_math.h"

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

bool Reader::got(size_t k) {
  n += k;
  buf[n] = 0;
  if (!series) {
    const char *p = strstr(buf, "\"timeseries\":[");
    if (!p) {
      drop(n - std::min<size_t>(n, 15));  // the key may be cut in two
      return true;
    }
    series = true;
    drop((size_t) (p - buf));
  }
  for (;;) {  // each hour once the next one starts, so the last one, maybe cut off, is never read
    const char *e = strstr(buf, "{\"time\"");
    const char *f = e ? strstr(e + 1, "{\"time\"") : nullptr;
    if (!f)
      return n < SIZE;
    if (!hour(std::string_view(e, (size_t) (f - e))))
      return false;
    drop((size_t) (f - buf));
  }
}

void Reader::drop(size_t k) {
  memmove(buf, buf + k, n - k + 1);
  n -= k;
}

bool Reader::hour(std::string_view e) {
  auto at = [&](const char *key, size_t from = 0) -> size_t {
    char k[40];
    snprintf(k, sizeof k, "\"%s\":", key);
    size_t p = e.find(k, from);
    return p == std::string_view::npos ? p : p + strlen(k);
  };
  auto num = [&](const char *key, float &out, size_t from = 0) {
    size_t p = at(key, from);
    if (p != std::string_view::npos)
      out = strtof(e.data() + p, nullptr);
    return p != std::string_view::npos;
  };
  auto symbol = [&](size_t from, char *out, size_t size) {  // out stays "" without one
    size_t s = at("symbol_code", from);
    if (s == std::string_view::npos || e[s] != '"')
      return;
    size_t end = e.find('"', s + 1);
    if (end != std::string_view::npos) {
      size_t n = std::min(end - s - 1, size - 1);
      memcpy(out, e.data() + s + 1, n);
      out[n] = 0;
    }
  };
  int y, mo, d, h;
  float temp;
  if (sscanf(e.data(), "{\"time\":\"%d-%d-%dT%d", &y, &mo, &d, &h) != 4 || !num("air_temperature", temp))
    return w.ok;  // a later hour is skipped, without the first there's no forecast
  uint32_t time = unix_of(y, mo, d, h);
  size_t n1 = at("next_1_hours");
  if (!w.ok) {
    if (!num("cloud_area_fraction", w.cloud))
      return false;
    w.time = time;
    w.temp = temp;
    num("cloud_area_fraction_low", w.cloud_low);
    num("cloud_area_fraction_medium", w.cloud_mid);
    num("cloud_area_fraction_high", w.cloud_high);
    num("fog_area_fraction", w.fog);
    num("wind_speed", w.wind);
    num("wind_speed_of_gust", w.gust);
    num("wind_from_direction", w.wind_from);
    size_t next = n1 != std::string_view::npos ? n1 : at("next_6_hours");
    if (next != std::string_view::npos) {
      num("precipitation_amount", w.rain, next);
      num("probability_of_thunder", w.thunder, next);
      symbol(next, w.symbol, sizeof w.symbol);
    }
    w.ok = true;
  }
  if (n1 != std::string_view::npos) {
    char sym[SYMBOL] = "";
    symbol(n1, sym, sizeof sym);
    w.hour[w.hours++] = {time, temp, icon_of(sym)};
  }
  return w.hours < HOURS;
}

}  // namespace flexit::panel::met
