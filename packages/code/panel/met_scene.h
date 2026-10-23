// The weather as a 320x240 picture in at most 16 flat colours, drawn a row at a time. Plain C++ without
// ESPHome, so sim/met_render.cpp in flexit-ci600-firmware can render it on a PC.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace flexit::panel::met {

static constexpr int W = 320, H = 240;
static constexpr int HORIZON = 156;                        // just above the panel's clock band, the bottom 74 rows
static constexpr int CABIN_X = 178;                        // in the band's gap between the clock and the temperatures
static constexpr int OV_H = 64;                            // rows of the forecast overlay, at the top
static constexpr int16_t BAND[5] = {0, 52, 92, 118, 138};  // first row of each sky band
static constexpr int RB_RX = 100, RB_RY = 96;              // the rainbow's outer radii
static constexpr int LH_X = 268;                           // the lighthouse, on the coast's point

// ---------------------------------------------------------------- the forecast

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

static constexpr int HOURS = 18;

struct Weather {
  bool ok = false;
  uint32_t time = 0;  // forecast hour, UTC seconds since 1970
  float temp = 10, cloud = 0, cloud_low = -1, cloud_mid = -1, cloud_high = -1, fog = 0;  // %, -1 = not given
  float rain = 0;                                                                        // mm in the coming hour
  float wind = 0, wind_from = 0, thunder = 0;                                            // m/s, degrees, %
  char symbol[40] = "";   // MET symbol for the coming hour, like "lightrainshowers_day"
  Hour hour[HOURS] = {};  // this hour onwards, as far as the response went
  int hours = 0;
};

// What the picture shows besides the weather
enum Ground : uint8_t { G_HILLS, G_COAST, G_MOUNTAINS, G_CITY };
enum Aurora : uint8_t { A_AUTO, A_OFF, A_ALWAYS };
struct Options {
  Ground ground = G_HILLS;
  Aurora aurora = A_AUTO;  // auto = some clear nights north of 55 degrees
  int heater_pct = 0;      // the unit's heater %, for the chimney smoke
  bool fireplace = false;  // more smoke
};

// MET symbol code to our Icon, see https://api.met.no/weatherapi/weathericon/2.0/documentation
inline Icon icon_of(const char *s) {
  bool night = strstr(s, "_night") != nullptr || strstr(s, "_polartwilight") != nullptr;
  if (strstr(s, "thunder"))
    return I_THUNDER;
  if (strstr(s, "sleet"))
    return I_SLEET;
  if (strstr(s, "snow"))
    return I_SNOW;
  if (strstr(s, "rain") || strstr(s, "drizzle"))
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

inline uint32_t days_from_civil(int y, int m, int d) {  // days since 1970-01-01
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return (uint32_t) (era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468);
}

inline void civil_from_days(int32_t z, int *y, int *m, int *d) {  // days since 1970-01-01 to a date
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  int doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yoe + era * 400 + (*m <= 2);
}

inline int easter_day_of_year(int y) {  // Gregorian Easter Sunday as day of the year, 1 = Jan 1
  int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451, month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
  return (int) (days_from_civil(y, month, day) - days_from_civil(y, 1, 1)) + 1;
}

// Reads a Locationforecast 2.0 answer as it comes in, an hour at a time, so it needs only a small buffer. The
// first hour is the weather now, it and the ones after it are for the overlay. "complete" has the cloud layers
// and fog, "compact" doesn't.
struct Reader {
  static constexpr size_t SIZE = 4096;  // an hour is about 1.2 KB
  char *buf;                            // SIZE + 1 bytes
  size_t n = 0;
  bool series = false;  // past "timeseries":[
  Weather w;            // w.ok once it has the first hour

  explicit Reader(char *b) : buf(b) { buf[0] = 0; }
  // Where the next bytes go, and how many fit
  char *space(size_t &room) {
    room = SIZE - n;
    return buf + n;
  }
  // n bytes came in at space(). False once it has HOURS hours, or what came can't be read.
  bool got(size_t k) {
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

 private:
  void drop(size_t k) {
    memmove(buf, buf + k, n - k + 1);
    n -= k;
  }
  // One hour, from its {"time" to the next one. False when no more are needed.
  bool hour(std::string_view e) {
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
      size_t q = e.find('"', s + 1);
      if (q != std::string_view::npos && q - s - 1 < size)
        memcpy(out, e.data() + s + 1, q - s - 1);
    };
    int y, mo, d, h;
    float temp;
    if (sscanf(e.data(), "{\"time\":\"%d-%d-%dT%d", &y, &mo, &d, &h) != 4 || !num("air_temperature", temp))
      return w.ok;  // a later hour is skipped, without the first there's no forecast
    uint32_t time = days_from_civil(y, mo, d) * 86400u + (uint32_t) h * 3600u;
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
      char sym[40] = "";
      symbol(n1, sym, sizeof sym);
      w.hour[w.hours++] = {time, temp, icon_of(sym)};
    }
    return w.hours < HOURS;
  }
};

// ---------------------------------------------------------------- positions of the sun, moon and stars

static constexpr double kPi = 3.14159265358979323846, kRad = kPi / 180;  // not PI, that's a macro in Arduino.h

struct Pos {
  float el, az;  // degrees above the horizon, degrees clockwise from north
};

inline double wrap360(double a) { return a - 360 * floor(a / 360); }

// Right ascension and declination in radians to elevation and azimuth, d days after J2000
inline Pos horizontal(double ra, double dec, double lat, double lon, double d) {
  double ha = wrap360(280.46061837 + 360.98564736629 * d + lon) * kRad - ra, la = lat * kRad;
  double el = asin(sin(la) * sin(dec) + cos(la) * cos(dec) * cos(ha));
  double az = atan2(sin(ha), cos(ha) * sin(la) - tan(dec) * cos(la)) / kRad + 180;  // from south + 180
  return {(float) (el / kRad), (float) wrap360(az)};
}

inline double j2000(uint32_t t) { return t / 86400.0 - 10957.5; }

inline double sun_longitude(double d) {  // ecliptic longitude in radians, good to 0.01 degrees
  double g = (357.529 + 0.98560028 * d) * kRad;
  return (280.459 + 0.98564736 * d + 1.915 * sin(g) + 0.020 * sin(2 * g)) * kRad;
}

inline Pos sun_at(double lat, double lon, uint32_t t) {
  double d = j2000(t), l = sun_longitude(d), e = (23.439 - 0.00000036 * d) * kRad;
  return horizontal(atan2(cos(e) * sin(l), cos(l)), asin(sin(e) * sin(l)), lat, lon, d);
}

// Moon position from Schlyter's elements and the largest perturbations, good to a degree or two.
// phase 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter
inline Pos moon_at(double lat, double lon, uint32_t t, float *phase) {
  double d = j2000(t) + 1.5;  // Schlyter's day 0 is 1999-12-31 0h
  double n = (125.1228 - 0.0529538083 * d) * kRad, i = 5.1454 * kRad, w = (318.0634 + 0.1643573223 * d) * kRad;
  double e = 0.0549, m = wrap360(115.3654 + 13.0649929509 * d) * kRad;
  double ea = m + e * sin(m) * (1 + e * cos(m));
  double x = cos(ea) - e, y = sqrt(1 - e * e) * sin(ea), v = atan2(y, x);
  double lon_e =
      atan2(sin(n) * cos(v + w) + cos(n) * sin(v + w) * cos(i), cos(n) * cos(v + w) - sin(n) * sin(v + w) * cos(i));
  double lat_e = asin(sin(v + w) * sin(i));
  double ms = (356.0470 + 0.9856002585 * d) * kRad, ls = ms + (282.9404 + 4.70935e-5 * d) * kRad;
  double lm = m + w + n, dd = lm - ls, f = lm - n;
  lon_e += (-1.274 * sin(m - 2 * dd) + 0.658 * sin(2 * dd) - 0.186 * sin(ms) - 0.059 * sin(2 * m - 2 * dd) -
            0.057 * sin(m - 2 * dd + ms) + 0.053 * sin(m + 2 * dd)) *
           kRad;
  lat_e += (-0.173 * sin(f - 2 * dd) - 0.055 * sin(m - f - 2 * dd) - 0.046 * sin(m + f - 2 * dd)) * kRad;
  if (phase)
    *phase = (float) (wrap360((lon_e - sun_longitude(j2000(t))) / kRad) / 360);
  double ob = (23.4393 - 3.563e-7 * d) * kRad;
  double xe = cos(lon_e) * cos(lat_e), ye = sin(lon_e) * cos(lat_e), ze = sin(lat_e);
  double yq = ye * cos(ob) - ze * sin(ob), zq = ye * sin(ob) + ze * cos(ob);
  return horizontal(atan2(yq, xe), atan2(zq, sqrt(xe * xe + yq * yq)), lat, lon, j2000(t));
}

// Bright stars. ra in hours, dec in degrees, bright 1 is drawn bigger
struct Star {
  float ra, dec;
  uint8_t bright;
};
// clang-format off: laid out by hand
static const Star STARS[] = {
    // 0 Polaris
    {2.530f, 89.26f, 0},
    // 1-7 the Big Dipper
    {11.062f, 61.75f, 1}, {11.031f, 56.38f, 0}, {11.897f, 53.69f, 0}, {12.257f, 57.03f, 0},
    {12.900f, 55.96f, 1}, {13.399f, 54.93f, 0}, {13.792f, 49.31f, 1},
    // 8-12 Cassiopeia
    {0.153f, 59.15f, 0}, {0.675f, 56.54f, 1}, {0.945f, 60.72f, 0}, {1.430f, 60.24f, 0}, {1.907f, 63.67f, 0},
    // 13 Vega, 14 Deneb, 15 Altair
    {18.616f, 38.78f, 1}, {20.690f, 45.28f, 1}, {19.846f, 8.87f, 1},
    // 16-19 Cygnus
    {20.370f, 40.26f, 0}, {19.512f, 27.96f, 0}, {20.770f, 33.97f, 0}, {19.750f, 45.13f, 0},
    // 20 Capella, 21 Aldebaran, 22 Pleiades
    {5.278f, 45.998f, 1}, {4.599f, 16.51f, 1}, {3.790f, 24.10f, 0},
    // 23-26 Orion: Betelgeuse, Rigel, Bellatrix, Saiph
    {5.919f, 7.41f, 1}, {5.242f, -8.20f, 1}, {5.419f, 6.35f, 0}, {5.796f, -9.67f, 0},
    // 27-29 Orion's belt
    {5.679f, -1.94f, 0}, {5.604f, -1.20f, 0}, {5.533f, -0.30f, 0},
    // 30 Sirius, 31 Procyon, 32 Castor, 33 Pollux
    {6.752f, -16.72f, 1}, {7.655f, 5.22f, 1}, {7.577f, 31.89f, 0}, {7.755f, 28.03f, 1},
    // 34 Regulus, 35 Denebola, 36 Arcturus, 37 Spica
    {10.139f, 11.97f, 1}, {11.818f, 14.57f, 0}, {14.261f, 19.18f, 1}, {13.420f, -11.16f, 1},
    // 38 Antares, 39 Mirfak, 40 Algol
    {16.490f, -26.43f, 1}, {3.405f, 49.86f, 0}, {3.136f, 40.96f, 0},
    // 41-43 Andromeda
    {0.140f, 29.09f, 0}, {1.162f, 35.62f, 0}, {2.065f, 42.33f, 0},
    // 44-46 Pegasus, with Alpheratz
    {23.063f, 28.08f, 0}, {23.079f, 15.21f, 0}, {0.220f, 15.18f, 0},
    // 47 Hamal, 48 Elnath, 49 Menkalinan, 50 Kochab
    {2.120f, 23.46f, 0}, {5.438f, 28.61f, 0}, {5.992f, 44.95f, 0}, {14.845f, 74.16f, 0},
};
// clang-format on
// ---------------------------------------------------------------- colours
// All colours are 4 bits per channel, so the panel's 16-colour palette holds them exactly.

// RGB rounded to 4 bits per channel, multiples of 17
inline uint32_t q(int r, int g, int b) {
  auto c = [](int v) { return (uint32_t) ((v < 0 ? 0 : v > 255 ? 255 : v) + 8) / 17 * 17; };
  return c(r) << 16 | c(g) << 8 | c(b);
}
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  auto ch = [&](int s) { return (int) ((a >> s & 255) + ((int) (b >> s & 255) - (int) (a >> s & 255)) * t + 0.5f); };
  return q(ch(16), ch(8), ch(0));
}
inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// ---------------------------------------------------------------- the scene

struct Ellipse {
  int16_t x, y, rx, ry;
};

struct Cloud {
  int16_t x, top, base, shade;  // x is the left edge in the wrapping field, rows below shade are darker
  Ellipse e[5];                 // x relative to the cloud's x
  uint8_t n;
};

// A one-colour bitmap. Layer 0 is drawn in the sky behind the ground, 1 in front of it, 2 on top of everything.
struct Sprite {
  int16_t x, y;
  uint8_t w, h, layer;  // at most 32 by 24
  uint32_t c;
  uint32_t rows[24];  // leftmost pixel in the highest of w bits
};
static constexpr int SPRITES = 56;

// Everything build() works out, so row() can draw any row on its own
struct Scene {
  uint32_t sky[5];  // sky bands from BAND, top to the horizon
  bool glow;        // sunrise or sunset light around the sun, low in the sky
  int16_t glow_x, glow_rx, glow_ry;
  uint32_t glow_c, glow_outer_c;
  bool sun, sun_rings, moon, moon_glow;
  int16_t sun_x, sun_y, moon_x, moon_y;
  float moon_phase;
  uint32_t sun_c, ring1_c, ring2_c, moon_c, moon_glow_c;
  int nstars;  // faint background stars at made-up positions
  int16_t star_x[90], star_y[90];
  uint32_t star_c, dim_c;  // catalogue stars, background stars
  int nbright;             // catalogue stars, at their real positions
  int16_t bright_x[64], bright_y[64];
  uint8_t bright_big[64];
  bool aurora;                            // a wide ribbon high up and a thin one lower
  int16_t aur_top[2][W], aur_bot[2][W];   // W = none in this column
  uint32_t aur_c, aur_top_c, aur_edge_c;  // main colour, top, light lower edge
  int nclouds, drift[3];                  // drift is each layer's offset in the wrapping field
  Cloud cloud[22];
  uint8_t cloud_layer[22];
  uint32_t cloud_lit[3], cloud_shade[3];  // high, middle, low
  bool rainbow;
  int16_t rb_x, rb_y;
  uint32_t rb_c[3];
  int nprec;
  int16_t prec_x[220], prec_y[220];
  uint8_t prec_kind[220];  // 0 rain streak, 1 snowflake, 2 big snowflake
  int16_t prec_top[W];     // where it starts falling in each column
  int8_t slant;            // rain's x per 8 rows
  uint32_t rain_c, snow_c;
  bool bolt;
  int16_t bolt_x[7], bolt_y[7];
  uint32_t bolt_c;
  int16_t fog_top[2][W];  // over the far and the middle hills, H = none
  uint32_t fog_c;
  Ground ground;
  int16_t hill[3][W];             // far, middle, near. Top row in each column
  uint32_t hill_c[3], tree_c[3];  // spruces are the hill's colour, but dark green on snow
  bool snow_trees;                // snow on the spruces' tiers
  uint32_t snowcap_c;             // mountains above the snow line
  int16_t snowline;
  int ntrees;
  int16_t tree_x[18], tree_top[18], tree_base[18];
  uint8_t tree_layer[18], tree_kind[18];  // kind 0 spruce, 1 birch, 2 bare birch
  uint32_t birch_c;
  bool sea;  // water from sea_top down to the middle hill
  int16_t sea_top;
  uint32_t sea_c, wave_c;
  int nwaves;
  int16_t wave_x[70], wave_y[70], wave_w[70];
  int16_t shine_x;  // the sun's or moon's reflection on the water, -1 = none
  int nbld;         // city buildings, a hazy far row and a near row with windows
  int16_t bld_x[28], bld_w[28], bld_top[28];
  uint8_t bld_back[28], bld_kind[28];  // kind 0 flat roof, 1 a box on it, 2 antenna, 3 spire
  uint32_t bld_c, bld_back_c, bld_edge_c;
  uint32_t win_seed;  // picks the lit windows
  bool ridge;         // mountains, a higher and hazier ridge behind the peaks
  int16_t ridge_y[W];
  uint32_t ridge_c;
  bool lighthouse;  // coast only, at LH_X
  int16_t lh_y;     // its base
  uint32_t lh_c, lh_band_c;
  bool cabin;
  int16_t cabin_x, cabin_y;  // centre, and floor on the near hill
  bool window_lit;
  uint32_t window_c, cabin_c;
  uint32_t white_c;                     // for small things and the cabin's trim, borrowed from a lit cloud or the sun
  int16_t pole_x, pole_top, pole_base;  // flagpole, by the cabin or in the city's park
  int16_t home_x;                       // where the holidays happen, the cabin or the pole
  // The two biggest parts are the caller's, kept apart so the scene needs no big block
  int nsprites;
  Sprite *sprite;        // SPRITES of them
  bool overlay;          // the forecast drawn over the picture
  uint8_t (*ov)[W / 4];  // OV_H rows, 2 bits per pixel. 0 shows the picture, 1 outline, 2 text
  uint32_t text_c, outline_c;
};

static constexpr int FIELD = W + 200;  // clouds drift through a field wider than the screen

struct Rng {  // xorshift32
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int in(int a, int b) { return a + (int) (next() % (uint32_t) (b - a + 1)); }
  float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
};

inline bool has(const Weather &w, const char *s) { return strstr(w.symbol, s) != nullptr; }

// ---------------------------------------------------------------- the forecast over the picture

// 5x7 font, leftmost pixel in the highest of w bits. '^' is the degree sign.
struct Glyph {
  char c;
  uint8_t w, row[7];
};
// clang-format off: laid out by hand
static const Glyph FONT[] = {
    {'0', 5, {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', 5, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', 5, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', 5, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', 5, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', 5, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', 5, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', 5, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', 5, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', 5, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', 4, {0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00}}, {'.', 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03}},
    {'^', 4, {0x06, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00}}, {'m', 5, {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}},
    {' ', 2, {0}}};
// clang-format on

// 9x9 icons, leftmost pixel in bit 8
static const uint16_t ICON_SUN[9] = {0x010, 0x082, 0x038, 0x07C, 0x17D, 0x07C, 0x038, 0x082, 0x010};
static const uint16_t ICON_MOON[9] = {0x03C, 0x078, 0x0F0, 0x0E0, 0x0E0, 0x0E0, 0x0F0, 0x078, 0x03C};
static const uint16_t ICON_CLOUD[9] = {0x000, 0x000, 0x038, 0x07C, 0x0FE, 0x1FF, 0x1FF, 0x0FE, 0x000};
static const uint16_t ICON_RAIN[9] = {0x038, 0x07C, 0x0FE, 0x1FF, 0x0FE, 0x000, 0x092, 0x124, 0x000};
static const uint16_t ICON_SNOW[9] = {0x038, 0x07C, 0x0FE, 0x1FF, 0x0FE, 0x000, 0x092, 0x000, 0x124};
static const uint16_t ICON_SLEET[9] = {0x038, 0x07C, 0x0FE, 0x1FF, 0x0FE, 0x000, 0x092, 0x124, 0x049};
static const uint16_t ICON_THUNDER[9] = {0x038, 0x07C, 0x0FE, 0x1FF, 0x0FE, 0x030, 0x060, 0x030, 0x060};
static const uint16_t ICON_FOG[9] = {0x000, 0x1FE, 0x000, 0x0FF, 0x000, 0x1FE, 0x000, 0x0FF, 0x000};

inline int ov_get(const Scene &s, int x, int y) { return s.ov[y][x / 4] >> (x % 4 * 2) & 3; }
inline void ov_put(Scene &s, int x, int y, int v) {
  if (x < 0 || x >= W || y < 0 || y >= OV_H)
    return;
  uint8_t &b = s.ov[y][x / 4];
  b = (uint8_t) ((b & ~(3 << (x % 4 * 2))) | v << (x % 4 * 2));
}

// A bitmap of w-bit rows scaled k times. Pass 1 draws its outline, pass 2 the bitmap.
inline void ov_bitmap(Scene &s, int x0, int y0, const uint16_t *rows, int w, int h, int k, int pass) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      if (!(rows[r] >> (w - 1 - c) & 1))
        continue;
      for (int dy = 0; dy < k; dy++)
        for (int dx = 0; dx < k; dx++) {
          int x = x0 + c * k + dx, y = y0 + r * k + dy;
          if (pass == 2)
            ov_put(s, x, y, 2);
          else
            for (int oy = -1; oy <= 1; oy++)
              for (int ox = -1; ox <= 1; ox++)
                ov_put(s, x + ox, y + oy, 1);
        }
    }
}

inline int ov_text_width(const char *t, int k) {
  int w = 0;
  for (; *t; t++)
    for (const Glyph &g : FONT)
      if (g.c == *t)
        w += (g.w + 1) * k;
  return w > 0 ? w - k : 0;
}

inline void ov_text(Scene &s, int x, int y, const char *t, int k) {
  for (int pass = 1; pass <= 2; pass++) {  // all outlines first, so they don't cut into the letters
    int cx = x;
    for (const char *p = t; *p; p++)
      for (const Glyph &g : FONT)
        if (g.c == *p) {
          uint16_t rows[7];
          for (int r = 0; r < 7; r++)
            rows[r] = g.row[r];
          ov_bitmap(s, cx, y, rows, g.w, 7, k, pass);
          cx += (g.w + 1) * k;
        }
  }
}

inline void ov_icon(Scene &s, int x, int y, Icon i, int k) {
  auto one = [&](const uint16_t *b, int dx, int dy) {
    for (int pass = 1; pass <= 2; pass++)
      ov_bitmap(s, x + dx * k, y + dy * k, b, 9, 9, k, pass);
  };
  switch (i) {
    case I_SUN:
      one(ICON_SUN, 0, 0);
      break;
    case I_MOON:
      one(ICON_MOON, 0, 0);
      break;
    case I_PARTLY_SUN:
      one(ICON_SUN, -1, -2);
      one(ICON_CLOUD, 2, 1);
      break;  // a cloud in front of it
    case I_PARTLY_MOON:
      one(ICON_MOON, -1, -2);
      one(ICON_CLOUD, 2, 1);
      break;
    case I_CLOUD:
      one(ICON_CLOUD, 0, 0);
      break;
    case I_RAIN:
      one(ICON_RAIN, 0, 0);
      break;
    case I_SNOW:
      one(ICON_SNOW, 0, 0);
      break;
    case I_SLEET:
      one(ICON_SLEET, 0, 0);
      break;
    case I_THUNDER:
      one(ICON_THUNDER, 0, 0);
      break;
    case I_FOG:
      one(ICON_FOG, 0, 0);
      break;
    default:
      break;
  }
}

inline int round_temp(float v) { return (int) (v < 0 ? v - 0.5f : v + 0.5f); }

// Now at top left, the next four 3-hour marks in local time at top right. utc_offset is local time minus UTC
// in seconds.
inline void forecast(Scene &s, const Weather &w, uint32_t t, int utc_offset) {
  char b[16];
  Icon now = w.hours ? w.hour[0].icon : icon_of(w.symbol);
  if (now == I_NONE)
    now = icon_of(w.symbol);
  ov_icon(s, 6, 7, now, 3);
  snprintf(b, sizeof b, "%d^", round_temp(w.temp));
  ov_text(s, 46, 6, b, 4);
  if (w.rain >= 0.1f) {
    snprintf(b, sizeof b, "%.1f mm", w.rain);
    ov_text(s, 8, 42, b, 2);
  }
  int col = 0;
  for (int i = 0; i < w.hours && col < 4; i++) {
    const Hour &h = w.hour[i];
    int local = (int) ((h.time + (uint32_t) (utc_offset + 86400)) % 86400 / 3600);
    if (h.time <= t || local % 3)
      continue;
    int cx = W - 6 - (4 - col) * 40 + 20;  // middle of the column
    snprintf(b, sizeof b, "%02d", local);
    ov_text(s, cx - ov_text_width(b, 2) / 2, 6, b, 2);
    ov_icon(s, cx - 9, 24, h.icon, 2);
    snprintf(b, sizeof b, "%d^", round_temp(h.temp));
    ov_text(s, cx - ov_text_width(b, 2) / 2 + 2, 46, b, 2);
    col++;
  }
}

// ---------------------------------------------------------------- sprites

inline Sprite *sprite(Scene &s, int x, int y, int w, int h, int layer, uint32_t c) {
  if (s.nsprites >= SPRITES)
    return nullptr;
  Sprite &p = s.sprite[s.nsprites++];
  p = Sprite{};
  p.x = (int16_t) x;
  p.y = (int16_t) y;
  p.w = (uint8_t) (w > 32 ? 32 : w);
  p.h = (uint8_t) (h > 24 ? 24 : h);
  p.layer = (uint8_t) layer;
  p.c = c;
  return &p;
}

inline void sprite_rows(Sprite *p, const uint32_t *rows) {
  if (p)
    for (int r = 0; r < p->h; r++)
      p->rows[r] = rows[r];
}

inline void sprite_set(Sprite *p, int x, int y) {
  if (p && x >= 0 && x < p->w && y >= 0 && y < p->h)
    p->rows[y] |= 1u << (p->w - 1 - x);
}

// a filled disc as a sprite
inline Sprite *blob(Scene &s, int cx, int cy, int rx, int ry, int layer, uint32_t c) {
  if (rx > 15 || ry > 11)
    return nullptr;
  Sprite *p = sprite(s, cx - rx, cy - ry, 2 * rx + 1, 2 * ry + 1, layer, c);
  if (!p)
    return nullptr;
  for (int dy = -ry; dy <= ry; dy++) {
    float v = 1 - (float) (dy * dy) / (float) (ry * ry + 0.01f);
    int h = (int) (rx * sqrtf(v < 0 ? 0 : v) + 0.5f);
    for (int dx = -h; dx <= h; dx++)
      sprite_set(p, dx + rx, dy + ry);
  }
  return p;
}

inline Sprite *box(Scene &s, int x, int y, int w, int h, int layer, uint32_t c) {
  Sprite *p = sprite(s, x, y, w, h, layer, c);
  if (p)
    for (int r = 0; r < p->h; r++)
      p->rows[r] = p->w == 32 ? 0xFFFFFFFFu : (1u << p->w) - 1;
  return p;
}

// a firework of rays around a bright middle
inline void burst(Scene &s, int cx, int cy, int r, uint32_t c, uint32_t heart) {
  Sprite *p = sprite(s, cx - r, cy - r, 2 * r + 1, 2 * r + 1, 0, c);
  for (int k = 0; k < 12; k++) {
    float a = k * 6.2832f / 12;
    for (int d = 3; d <= r; d++)
      if ((d + k) % 3 != 2)  // gaps so the rays look like they're burning out
        sprite_set(p, r + (int) lroundf(d * cosf(a)), r + (int) lroundf(d * sinf(a)));
  }
  Sprite *in = sprite(s, cx - 2, cy - 2, 5, 5, 0, heart);
  for (int d = -2; d <= 2; d++) {
    sprite_set(in, 2 + d, 2);
    sprite_set(in, 2, 2 + d);
  }
}

// The Norwegian flag, 22 by 16 in the official 6-1-2-1-12 by 6-1-2-1-6 proportions, on layer 2
inline void flag_norway(Scene &s, int x, int y, uint32_t red, uint32_t white, uint32_t blue) {
  Sprite *r = sprite(s, x, y, 22, 16, 2, red), *w = sprite(s, x, y, 22, 16, 2, white),
         *b = sprite(s, x, y, 22, 16, 2, blue);
  for (int row = 0; row < 16; row++)
    for (int col = 0; col < 22; col++) {
      bool vb = col == 7 || col == 8, vw = col == 6 || col == 9, hb = row == 7 || row == 8, hw = row == 6 || row == 9;
      sprite_set(vb || hb ? b : vw || hw ? w : r, col, row);
    }
}

// The vimpel, a red pennant 9 rows tall at the pole and len long, with a white-blue-white stripe. Points left
// when dir < 0.
inline void pennant(Scene &s, int x, int y, int len, int dir, uint32_t red, uint32_t white, uint32_t blue) {
  Sprite *r = sprite(s, x, y, len, 9, 2, red), *w = sprite(s, x, y, len, 9, 2, white),
         *b = sprite(s, x, y, len, 9, 2, blue);
  for (int col = 0; col < len; col++) {
    int hi = (int) (4.0f * (1 - (float) col / len) + 0.5f);  // half height at this column
    for (int row = 4 - hi; row <= 4 + hi; row++) {
      int d = abs(row - 4);  // the stripe while there's room for it, the tip all red
      sprite_set(d == 0 && hi >= 1 ? b : d == 1 && hi >= 2 ? w : r, dir < 0 ? len - 1 - col : col, row);
    }
  }
}

// Sprite bitmaps, one entry per row, leftmost pixel in the highest of w bits
// clang-format off: laid out by hand
static const uint32_t BIRD[3] = {0x41, 0x36, 0x8};                               // 7 wide
static const uint32_t BAT[4] = {0x183, 0x1EF, 0xFE, 0x28};                        // 9 wide
// 13 wide
static const uint32_t HEART[11] = {0x71C, 0xFBE, 0x1FFF, 0x1FFF, 0x1FFF, 0xFFE, 0x7FC, 0x3F8, 0x1F0, 0xE0, 0x40};
static const uint32_t FLAME[20] = {0x100, 0x100, 0x300, 0x380, 0x780, 0x7A0, 0xFA0, 0xFE0, 0x1FE0, 0x1FF0,   // 15 wide
                                   0x1FF0, 0x3FF8, 0x3FF8, 0x3FFC, 0x7FFC, 0x7FFE, 0x7FFF, 0x7FFF, 0x3FFE, 0x1FFC};
static const uint32_t FLAME_IN[20] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0x100, 0x300, 0x380,
                                      0x780, 0x780, 0x7C0, 0xFC0, 0xFC0, 0xFE0, 0x1FE0, 0x1FE0, 0xFE0, 0x7C0};
static const uint32_t STAR7[7] = {0x08, 0x1C, 0x7F, 0x3E, 0x3E, 0x63, 0x41};     // 7 wide
// the pumpkin's face, 11 wide
static const uint32_t FACE[7] = {0x104, 0x38E, 0x7FF, 0x0, 0x555, 0x6DB, 0x3FE};
// clang-format on

// ---------------------------------------------------------------- the sky

// Zenith, horizon and glow colours for a sun elevation. The dark ones keep red and green equal, or at 4 bits
// per channel they come out teal or purple.
inline void sky_colours(float el, uint32_t *top, uint32_t *bottom, uint32_t *glow) {
  // clang-format off: laid out by hand
  static const struct { float el; uint32_t top, bottom, glow; } K[] = {
      {-90, 0x000022, 0x222255, 0x222255}, {-18, 0x000022, 0x222255, 0x222255},
      {-12, 0x111133, 0x333366, 0x3C3668}, {-6, 0x16225A, 0x544680, 0xA85C7A},
      {-2, 0x22357A, 0x8E6488, 0xF0885A},  {2, 0x2A4994, 0xD88C6C, 0xFFAE60},
      {8, 0x3263B2, 0xEABC90, 0xFFD48E},   {16, 0x3277CC, 0xA6CEEC, 0xE6EEF0},
      {90, 0x2868C4, 0xB2D8F2, 0xFFFFFF}};
  // clang-format on
  int k = 0;
  while (k < 7 && el > K[k + 1].el)
    k++;
  float t = clamp01((el - K[k].el) / (K[k + 1].el - K[k].el));
  *top = mix(K[k].top, K[k + 1].top, t);
  *bottom = mix(K[k].bottom, K[k + 1].bottom, t);
  *glow = mix(K[k].glow, K[k + 1].glow, t);
}

inline int sky_x(float az) { return (int) (az / 360 * W) % W; }  // north at the edges, south in the middle
inline int sky_y(float el) { return HORIZON - (int) (el * (HORIZON - 26) / 55); }

// 0 winter with snow on the ground, 1 winter without, 2 spring, 3 summer, 4 autumn
inline int season_of(int month, int day, bool white) {
  if (white)
    return 0;
  int k = month * 100 + day;
  if (k < 320 || k >= 1201)
    return 1;
  if (k < 601)
    return 2;
  if (k < 901)
    return 3;
  return 4;
}

// The light, weather and date the parts of the scene go by. utc_offset is local time minus UTC in seconds.
struct Now {
  const Weather &w;
  const Options &o;
  double lat, lon;
  uint32_t t, local;  // UTC, and local time with a day added so it's never negative
  Pos sun, moon;
  float moon_phase, light, fog, cover, grey, glow;  // light 0 night .. 1 day, grey how overcast, glow see sky()
  bool snow, rain, thunder, dark, white;            // white: snow on the ground
  int year, hour, season, wdir;                     // wdir: which way the wind blows things
  bool xmas, newyear, easter_days, midsummer, halloween, valentine, flag_day, flag_up;

  Now(const Weather &weather, const Options &options, double la, double lo, uint32_t utc, int utc_offset)
      : w(weather), o(options), lat(la), lon(lo), t(utc), local(utc + (uint32_t) (utc_offset + 86400)) {
    sun = sun_at(lat, lon, t);
    moon = moon_at(lat, lon, t, &moon_phase);
    light = clamp01((sun.el + 6) / 18);
    snow = has(w, "snow") || has(w, "sleet");
    rain = has(w, "rain") || has(w, "sleet") || has(w, "drizzle");
    if (w.ok && w.rain > 0.05f && !snow && !rain)
      (w.temp > 1 ? rain : snow) = true;
    thunder = has(w, "thunder") || w.thunder >= 30;
    fog = std::max(w.fog, has(w, "fog") ? 85.0f : 0.0f);
    cover = w.ok ? w.cloud : 0;
    grey = clamp01((cover - 45) / 50) * 0.9f + (rain || snow ? 0.1f : 0) + (thunder ? 0.15f : 0);
    glow = (1 - fabsf(sun.el - 1) / 11) * (1 - grey);
    dark = sun.el < -6;
    wdir = w.wind_from > 0 && w.wind_from < 180 ? -1 : 1;
    int month, day;
    hour = (int) (local % 86400 / 3600);
    civil_from_days((int32_t) (local / 86400) - 1, &year, &month, &day);
    white =
        (w.ok && w.temp <= 0 && snow) || (w.ok && w.temp <= -3) || (w.ok && w.temp <= 1 && (month == 12 || month <= 2));
    season = season_of(month, day, white);
    int doy = (int) (days_from_civil(year, month, day) - days_from_civil(year, 1, 1)) + 1,
        easter = easter_day_of_year(year);
    xmas = month == 12 && day >= 24 && day <= 26;
    newyear = (month == 12 && day == 31 && hour >= 18) || (month == 1 && day == 1 && hour < 3);
    easter_days = doy >= easter - 3 && doy <= easter + 1;
    midsummer = month == 6 && day == 23 && hour >= 19;
    halloween = month == 10 && day == 31;
    valentine = month == 2 && day == 14;
    // Official flag days from forskrift om bruk av statsflagget § 4. New Year's Day, Princess Ingrid Alexandra,
    // the Sami people's day, the King, 1 May, Liberation Day, Constitution Day, the union's dissolution, the
    // Queen, the Crown Prince, Olsok, the Crown Princess, Christmas Day, Easter Sunday, Pentecost, and the
    // Storting election on the second Monday of September every fourth year
    int weekday = (int) ((local / 86400 - 1 + 4) % 7);  // 0 Sunday. local has a day added
    flag_day = (month == 5 && day == 17) || (month == 1 && day == 1) || (month == 1 && day == 21) ||
               (month == 2 && day == 6) || (month == 2 && day == 21) || (month == 5 && day == 1) ||
               (month == 5 && day == 8) || (month == 6 && day == 7) || (month == 7 && day == 4) ||
               (month == 7 && day == 20) || (month == 7 && day == 29) || (month == 8 && day == 19) ||
               (month == 12 && day == 25) || doy == easter || doy == easter + 49 ||
               (year % 4 == 1 && month == 9 && weekday == 1 && day >= 8 && day <= 14);
    // Up at 8 from March to October and at 9 in winter, down at sunset or by 21. From Nordland and north it's
    // 10 to 15 in winter, § 6.
    bool winter_months = month <= 2 || month >= 11;
    flag_up = flag_day && (lat >= 65 && winter_months ? hour >= 10 && hour < 15
                                                      : hour >= (winter_months ? 9 : 8) && hour < 21 && sun.el > -0.5f);
  }
};

// The sky's bands, the fog's colour and the glow around a low sun
inline void sky(Scene &s, const Now &n) {
  uint32_t top, bottom, glow;
  sky_colours(n.sun.el, &top, &bottom, &glow);
  uint32_t grey_top = mix(0x14171F, n.thunder ? 0x5C626C : 0x8A94A2, n.light),
           grey_bottom = mix(0x20242C, n.thunder ? 0x7A808A : 0xBEC4CC, n.light);
  top = mix(top, grey_top, n.grey);
  bottom = mix(bottom, grey_bottom, n.grey);
  float darken = std::min(0.4f, (n.w.ok ? n.w.rain : 0) / 12);
  top = mix(top, 0x2A2F38, darken);
  bottom = mix(bottom, 0x4A505A, darken);
  s.fog_c = mix(mix(0x262C38, 0xC6CCD4, n.light), bottom, 0.3f);
  if (n.fog >= 60) {
    top = mix(top, s.fog_c, (n.fog - 60) / 80);
    bottom = mix(bottom, s.fog_c, (n.fog - 60) / 60);
  }
  static const float AT[5] = {0, 0.3f, 0.55f, 0.78f, 1};
  for (int i = 0; i < 5; i++)
    s.sky[i] = mix(top, bottom, AT[i]);
  float g = n.glow;
  if (g > 0.15f) {
    s.glow = true;
    s.glow_x = (int16_t) sky_x(n.sun.az);
    s.glow_rx = (int16_t) (90 + 60 * g);
    s.glow_ry = (int16_t) (16 + 22 * g);
    s.glow_c = mix(s.sky[4], glow, 0.3f + 0.45f * g);
    s.glow_outer_c = mix(s.sky[3], glow, 0.15f + 0.25f * g);
  }
}

inline void sun_moon(Scene &s, const Now &n) {
  if (n.sun.el > -1) {
    s.sun = true;
    s.sun_x = (int16_t) sky_x(n.sun.az);
    s.sun_y = (int16_t) sky_y(n.sun.el);
    uint32_t behind = s.sky[s.sun_y < 52 ? 0 : s.sun_y < 92 ? 1 : s.sun_y < 118 ? 2 : 3];
    if (n.grey > 0.75f) {
      s.sun_c = mix(behind, 0xFFFFFF, 0.3f);  // a pale disc through the clouds
    } else {
      s.sun_c = n.sun.el < 6 ? mix(0xFF9A50, 0xFFE0A0, n.sun.el / 6) : 0xFFF6D2;
      s.sun_rings = true;
      s.ring1_c = mix(behind, s.sun_c, 0.42f);
      s.ring2_c = mix(behind, s.sun_c, 0.2f);
    }
  }
  if (n.moon.el > 1 && n.sun.el < 10) {
    s.moon = true;
    s.moon_x = (int16_t) sky_x(n.moon.az);
    s.moon_y = (int16_t) sky_y(n.moon.el);
    s.moon_phase = n.moon_phase;
    s.moon_c = mix(0xDCE6F2, 0xF4F6FA, 1 - n.light);
    if (n.sun.el < -6 && n.grey < 0.6f) {
      s.moon_glow = true;
      s.moon_glow_c = mix(s.sky[s.moon_y < 52 ? 0 : s.moon_y < 92 ? 1 : 2], s.moon_c, 0.16f);
    }
  }
}

// Northern lights, some clear nights up north
inline void aurora(Scene &s, const Now &n) {
  if (n.o.aurora == A_OFF || n.sun.el >= -9 || n.cover >= 40 || n.fog >= 30)
    return;
  Rng a{(uint32_t) ((n.local - 43200) / 86400) * 2246822519u + 3};  // from noon to noon, the same all night
  if (n.o.aurora != A_ALWAYS && (n.lat < 55 || a.f() >= (n.lat >= 65 ? 0.6f : 0.35f)))
    return;
  s.aurora = true;
  s.moon_glow = false;  // its colour slot goes to the ribbons' edge
  float p0 = a.f() * 6.283f, p1 = a.f() * 6.283f, p2 = a.f() * 6.283f, lift = (float) ((n.t / 3600) % 7) * 2;
  int x1 = a.in(40, 120), x2 = a.in(W - 60, W + 80);  // where the lower ribbon starts and ends
  for (int x = 0; x < W; x++) {                       // ribbons thinning out towards their ends
    float mid = 44 + lift + 16 * sinf(x / 58.0f + p0) + 5 * sinf(x / 23.0f + p1);
    float th = 20 * (0.75f + 0.25f * sinf(x / 41.0f + p2)) * std::min(1.0f, 0.3f + 1.4f * sinf(kPi * x / W));
    s.aur_top[0][x] = (int16_t) (mid - th / 2);
    s.aur_bot[0][x] = (int16_t) (mid + th / 2);
    float mid2 = 92 + lift + 10 * sinf(x / 47.0f + p1 + 2) + 3 * sinf(x / 17.0f + p2);
    float env = x > x1 && x < x2 ? sinf(kPi * (x - x1) / (x2 - x1)) : 0;
    float th2 = 9 * std::min(1.0f, 1.6f * env);
    s.aur_top[1][x] = (int16_t) (th2 >= 1 ? mid2 - th2 / 2 : W);
    s.aur_bot[1][x] = (int16_t) (th2 >= 1 ? mid2 + th2 / 2 : W);
  }
  // mostly green under violet, some nights teal under pink, now and then green under red
  static const uint32_t HUES[3][3] = {
      {0x40B878, 0x7858A8, 0x98E0B8}, {0x38A8A0, 0xB05890, 0x90D8D0}, {0x58B860, 0xB04848, 0xA0E0A0}};
  const uint32_t *hue = HUES[a.f() < 0.6f ? 0 : a.f() < 0.6f ? 1 : 2];
  s.aur_c = mix(s.sky[0], hue[0], 0.42f);
  s.aur_top_c = mix(s.sky[0], hue[1], 0.4f);
  s.aur_edge_c = mix(s.sky[0], hue[2], 0.5f);
}

// Faint random stars, and the catalogue ones where they really are
inline void stars(Scene &s, const Now &n) {
  if (n.sun.el >= -7 || n.grey >= 0.85f)
    return;
  Rng r{0x5EED5u};
  s.nstars = (int) ((n.sun.el < -12 ? 90 : 40) * (1 - n.cover / 100));
  for (int i = 0; i < s.nstars; i++) {
    s.star_x[i] = (int16_t) r.in(0, W - 1);
    s.star_y[i] = (int16_t) r.in(2, 132);
  }
  s.star_c = 0xFFFFFF;  // same white as the forecast text
  s.dim_c = s.aurora ? s.star_c : mix(s.sky[1], 0xE8EEFA, 0.4f);
  double d = j2000(n.t);
  for (unsigned i = 0; i < sizeof STARS / sizeof *STARS && n.cover < 70 && s.nbright < 64; i++) {
    Pos p = horizontal(STARS[i].ra * 15 * kRad, STARS[i].dec * kRad, n.lat, n.lon, d);
    if (p.el < 1 || p.el > 58)
      continue;
    s.bright_x[s.nbright] = (int16_t) sky_x(p.az);
    s.bright_y[s.nbright] = (int16_t) std::max(3, sky_y(p.el));
    s.bright_big[s.nbright++] = STARS[i].bright;
  }
}

// Clouds, seeded by the forecast hour so they don't jump about between redraws
inline void clouds(Scene &s, const Now &n) {
  const Weather &w = n.w;
  float layer[3] = {w.cloud_high, w.cloud_mid, w.cloud_low};
  if (layer[0] < 0 || layer[1] < 0 || layer[2] < 0) {
    layer[0] = n.cover * 0.25f;
    layer[1] = n.cover * 0.45f;
    layer[2] = n.cover * 0.6f;
  }
  bool wet = n.rain || n.snow;
  uint32_t lit = mix(0x353C56, wet ? 0xC4CAD2 : 0xF6F8FB, n.light),
           shade = mix(0x232839, wet ? 0x8E96A2 : 0xC6CEDA, n.light);
  if (n.thunder) {
    lit = mix(lit, 0x5E646E, 0.6f);
    shade = mix(shade, 0x3A3F48, 0.6f);
  }
  if (s.glow) {  // lit from below at sunrise and sunset
    lit = mix(lit, 0xFFD0A4, 0.75f * n.glow);
    shade = mix(shade, 0x86668A, 0.6f * n.glow);
  }
  for (int l = 0; l < 3; l++) {
    s.cloud_lit[l] = l == 0 ? mix(lit, s.sky[0], 0.25f) : lit;
    s.cloud_shade[l] = l == 2 && n.grey > 0.3f ? mix(shade, 0x404650, 0.25f * n.grey) : shade;
  }
  Rng r{w.time * 2654435761u + 7};
  uint32_t into_hour = w.ok && n.t > w.time ? n.t - w.time : n.t % 3600;
  for (int l = 0; l < 3; l++) {
    s.drift[l] = (int) (into_hour / 60.0f * (0.3f + w.wind * 0.12f) * (l + 1)) % FIELD;
    int count = (int) (layer[l] / 100 * (l == 0 ? 5 : 7) + 0.5f);
    for (int k = 0; k < count && s.nclouds < 22; k++) {
      Cloud &c = s.cloud[s.nclouds];
      s.cloud_layer[s.nclouds++] = (uint8_t) l;
      int width = l == 0 ? r.in(60, 120) : l == 1 ? r.in(48, 86) : r.in(70, 128);
      c.x = (int16_t) ((k * FIELD) / count + r.in(-20, 20) + FIELD) % FIELD;
      c.base = (int16_t) (l == 0 ? r.in(20, 64) : l == 1 ? r.in(62, 102) : r.in(100, 146));
      if (l == 0) {  // high ones are thin streaks
        c.n = (uint8_t) r.in(2, 3);
        for (int j = 0; j < c.n; j++)
          c.e[j] = {(int16_t) (width * (j + 1) / (c.n + 1) + r.in(-8, 8)), (int16_t) (c.base - r.in(0, 6)),
                    (int16_t) (width / (c.n + 1) + r.in(4, 14)), (int16_t) r.in(1, 3)};
        c.top = (int16_t) (c.base - 12);
        c.shade = 999;
        c.base += 4;
        continue;
      }
      c.n = (uint8_t) r.in(3, 5);
      int rmax = width / c.n + 4;
      c.top = c.base;
      for (int j = 0; j < c.n; j++) {
        int middle = 2 * j - (c.n - 1);  // bigger towards the middle
        int rad = rmax - abs(middle) * 3 + r.in(-3, 3);
        c.e[j] = {(int16_t) (width * (2 * j + 1) / (2 * c.n)), (int16_t) (c.base - rad * 6 / 10), (int16_t) rad,
                  (int16_t) rad};
        c.top = (int16_t) std::min<int>(c.top, c.e[j].y - rad);
      }
      c.shade = (int16_t) (c.base - (c.base - c.top) * 26 / 100);
    }
  }
}

// The rainbow, rain and snow from under the clouds, not through them, and the bolt from there
inline void precipitation(Scene &s, const Now &n) {
  const Weather &w = n.w;
  if (n.rain && !n.snow && n.sun.el > 1 && n.sun.el < 42 && n.grey < 0.8f) {  // with the sun out and low enough
    s.rainbow = true;
    int x = sky_x(n.sun.az + 180);  // opposite the sun, kept whole on the screen
    s.rb_x = (int16_t) (x < RB_RX ? RB_RX : x > W - 1 - RB_RX ? W - 1 - RB_RX : x);
    s.rb_y = (int16_t) (HORIZON + n.sun.el * 1.6f);
    uint32_t behind = s.sky[2];
    s.rb_c[0] = mix(behind, 0xE05050, 0.55f);
    s.rb_c[1] = mix(behind, 0xE8D850, 0.55f);
    s.rb_c[2] = mix(behind, 0x6090E0, 0.55f);
    s.sun_rings = false;
  }
  if (n.rain || n.snow || n.thunder) {
    int deck = 0;  // lowest base of the middle and low clouds, for the columns between them
    for (int x = 0; x < W; x++)
      s.prec_top[x] = -1;
    for (int i = 0; i < s.nclouds; i++) {  // under each cloud, from its own base
      const Cloud &c = s.cloud[i];
      if (s.cloud_layer[i] == 0)
        continue;  // not from the high thin ones
      deck = std::max<int>(deck, c.base);
      int x0 = (c.x + s.drift[s.cloud_layer[i]]) % FIELD - 100;
      for (int j = 0; j < c.n; j++)
        for (int k = -1; k <= 0; k++) {
          int cx = x0 + c.e[j].x + k * FIELD;
          for (int x = std::max(0, cx - c.e[j].rx); x <= std::min(W - 1, cx + c.e[j].rx); x++)
            s.prec_top[x] = (int16_t) std::max<int>(s.prec_top[x], c.base + 2);
        }
    }
    for (int x = 0; x < W; x++)
      if (s.prec_top[x] < 0)
        s.prec_top[x] = (int16_t) (deck ? deck : 110);
  }
  if (n.rain || n.snow) {
    Rng p{w.time + 99};
    float amount = std::max(w.rain, has(w, "heavy") ? 4.0f : has(w, "light") ? 0.3f : 1.0f);
    s.nprec = (int) std::min(220.0f, 30 + amount * 45);
    float across = -n.wdir * std::min(1.0f, w.wind / 9);
    s.slant = (int8_t) (across * 4);
    bool both = has(w, "sleet");
    for (int i = 0; i < s.nprec; i++) {
      int x = p.in(0, W - 1);
      s.prec_x[i] = (int16_t) x;
      s.prec_y[i] = (int16_t) p.in(s.prec_top[x], H - 1);
      s.prec_kind[i] = (uint8_t) (n.snow && (!both || i % 2) ? (p.in(0, 3) ? 1 : 2) : 0);
    }
    s.rain_c = mix(0x4A5C78, 0x9EBAD6, n.light);
    s.snow_c = mix(0xAAB4C6, 0xF6FAFF, n.light);
  }
  if (n.thunder) {
    Rng b{w.time + 5};
    s.bolt = true;
    int x = b.in(70, 250), top = s.prec_top[x] - 3,
        step = std::max(6, (HORIZON + 12 - top) / 6);  // out of the cloud, to the hills
    for (int i = 0; i < 7; i++) {
      s.bolt_x[i] = (int16_t) x;
      s.bolt_y[i] = (int16_t) (top + i * step);
      x += (i % 2 ? 1 : -1) * b.in(5, 13);
    }
    s.bolt_c = q(255, 248, 196);
  }
}

// Sea to a rocky shore, a headland on the left, a lighthouse on the point
inline void coast(Scene &s, const Now &n, Rng &h, const float *ph) {
  s.sea = true;
  s.sea_top = HORIZON + 1;
  for (int x = 0; x < W; x++) {
    s.hill[0][x] = (int16_t) (x < 120 ? HORIZON - 4 - (int) (14 * sinf((120 - x) / 76.0f)) : H);
    int point = x > 236 && x < 300 ? 22 - abs(x - 268) / 3 : 0;
    s.hill[1][x] = (int16_t) (184 - point - 2 * sinf(x / 13.0f + ph[4]));
    s.hill[2][x] = (int16_t) (206 - 5 * sinf(x / 53.0f + ph[5]) - 2 * sinf(x / 17.0f + ph[6]));
  }
  s.lighthouse = true;  // 32 tall
  s.lh_y = (int16_t) (s.hill[1][LH_X] + 2);
  s.lh_c = mix(s.sky[4], 0xFFFFFF, n.light * 0.5f + 0.3f);
  s.lh_band_c = mix(mix(0x2A1418, 0x9A3426, n.light), s.sky[4], n.fog / 200);
  if (n.dark && (n.t / 300) % 2 == 0)  // the lamp, lit every other redraw
    box(s, LH_X - 1, s.lh_y - 29, 3, 3, 2, q(255, 210, 122));
  for (int k = 0; k < 2; k++) {  // skerries
    int x = k ? h.in(140, 215) : h.in(20, 90);
    blob(s, x, h.in(170, 178), h.in(4, 7), h.in(1, 2), 1, s.hill_c[1]);
  }
}

// Snow-capped peaks over a fjord, a hazy ridge behind
inline void mountains(Scene &s, const Now &n, const float *ph) {
  s.sea = true;
  s.sea_top = 160;
  s.ridge = true;
  for (int x = 0; x < W; x++) {
    float tri = fabsf(fmodf(x / 58.0f + ph[0], 2.0f) - 1), tri2 = fabsf(fmodf(x / 23.0f + ph[1], 2.0f) - 1);
    float tri3 = fabsf(fmodf(x / 91.0f + ph[2], 2.0f) - 1), tri4 = fabsf(fmodf(x / 31.0f + ph[3], 2.0f) - 1);
    s.hill[0][x] = (int16_t) (HORIZON - 20 - (int) (62 * tri) - (int) (10 * tri2));
    s.ridge_y[x] = (int16_t) (HORIZON - 36 - (int) (60 * tri3) - (int) (8 * tri4));
    s.hill[1][x] = (int16_t) (186 - 2 * sinf(x / 11.0f + ph[4]));
    s.hill[2][x] = (int16_t) (206 - 6 * sinf(x / 71.0f + ph[5]) - 3 * sinf(x / 29.0f + ph[6]));
  }
  s.snowline = (int16_t) (HORIZON - (n.white ? 20 : n.season == 3 ? 62 : 48));
  s.snowcap_c = mix(mix(0x33445A, 0xF0F4F8, n.light), s.sky[3], 0.25f);
  s.hill_c[0] = mix(mix(0x1C2838, 0x5A6C7E, n.light), s.sky[4], 0.25f);
  s.ridge_c = mix(s.hill_c[0], s.sky[3], 0.55f);
}

// A skyline on the middle ground, a park in front
inline void city(Scene &s, const Now &n, Rng &h) {
  for (int x = 0; x < W; x++)
    s.hill[1][x] = 178;
  s.cabin = false;
  // a far row with gaps and a near row, where one becomes a tall antenna tower and one gets a spire
  for (int back = 1; back >= 0; back--) {
    int x = back ? h.in(10, 40) : 2;
    while (x < W - 8 && s.nbld < 28) {
      int i = s.nbld++;
      s.bld_back[i] = (uint8_t) back;
      s.bld_w[i] = (int16_t) (back ? h.in(20, 44) : h.in(16, 38));
      s.bld_top[i] = (int16_t) (back ? h.in(96, 136) : h.in(120, 160));
      s.bld_kind[i] = (uint8_t) (back ? 0 : h.in(0, 3) == 0 ? 1 : 0);
      s.bld_x[i] = (int16_t) x;
      x += s.bld_w[i] + (back ? h.in(8, 36) : h.in(2, 6));
    }
  }
  int tower = -1, church = -1;
  for (int i = 0; i < s.nbld; i++) {
    if (s.bld_back[i])
      continue;
    if (tower < 0 || abs(s.bld_x[i] + s.bld_w[i] / 2 - 170) < abs(s.bld_x[tower] + s.bld_w[tower] / 2 - 170))
      tower = i;
    if (church < 0 || abs(s.bld_x[i] - 50) < abs(s.bld_x[church] - 50))
      church = i;
  }
  if (tower >= 0) {
    s.bld_top[tower] = 86;
    s.bld_kind[tower] = 2;
  }
  if (church >= 0 && church != tower) {
    s.bld_top[church] = 146;
    s.bld_kind[church] = 3;
  }
  s.bld_c = mix(mix(0x1A1E2C, 0x5E6678, n.light), s.sky[4], 0.2f);
  s.bld_back_c = mix(s.bld_c, s.sky[4], 0.45f);
  s.bld_edge_c = mix(s.bld_c, 0x000000, 0.35f);
  s.win_seed = (uint32_t) (n.local / 1800) * 2654435761u;
  if (n.dark && (n.t / 300) % 2 == 0 && tower >= 0)  // red light on the antenna, every other redraw
    box(s, s.bld_x[tower] + s.bld_w[tower] / 2 - 1, 86 - 14, 3, 2, 2, q(255, 48, 48));
  if (n.dark) {  // a street lamp in the park
    Sprite *lamp = sprite(s, 60, 180, 5, 3, 2, q(255, 210, 122));
    if (lamp) {
      lamp->rows[0] = 0x0E;
      lamp->rows[1] = 0x1F;
      lamp->rows[2] = 0x0E;
    }
  }
}

// The sea's waves and the sun's or moon's reflection, and a sailboat off the coast on a fine day
inline void sea(Scene &s, const Now &n, Rng &h) {
  const Weather &w = n.w;
  s.sea_c = mix(mix(0x0C1C30, 0x2C5C8C, n.light), s.sky[4], 0.35f);
  s.wave_c = n.o.ground == G_MOUNTAINS ? s.snowcap_c : mix(s.sky[4], 0xFFFFFF, n.light * 0.5f + 0.3f);
  Rng wv{(uint32_t) (n.t / 3600) * 7919u + 11};  // new waves each hour
  s.nwaves = std::min(70, 6 + (int) (w.wind * 5));
  for (int i = 0; i < s.nwaves; i++) {
    s.wave_x[i] = (int16_t) wv.in(0, W - 1);
    s.wave_y[i] = (int16_t) wv.in(s.sea_top + 2, 182);
    s.wave_w[i] = (int16_t) wv.in(2, 3 + (int) w.wind);
  }
  if (s.moon && n.dark)
    s.shine_x = s.moon_x;
  else if (s.sun && n.sun.el < 20 && n.grey < 0.5f)
    s.shine_x = s.sun_x;
  if (n.o.ground == G_COAST && n.sun.el > 0 && w.wind < 10 && !n.rain && !n.snow && !n.thunder) {
    int bx = h.in(120, 225), by = h.in(168, 176);
    box(s, bx - 5, by - 2, 11, 3, 1, s.hill_c[1]);
    box(s, bx, by - 16, 1, 14, 1, s.hill_c[1]);
    Sprite *sail = sprite(s, bx + 1, by - 15, 8, 12, 1, s.wave_c);
    for (int r = 0; r < 12; r++)
      for (int c = 0; c < r * 8 / 12; c++)
        sprite_set(sail, c, r);
  }
}

// The hills, the landscape, the cabin or the park, the trees and the fog. Seeded by the location, so it's always
// the same landscape there.
inline void ground(Scene &s, const Now &n) {
  Rng h{(uint32_t) (int32_t) (n.lat * 1000) * 31u + (uint32_t) (int32_t) (n.lon * 1000) + 1};
  float ph[8];
  for (float &v : ph)
    v = h.f() * 6.283f;
  // far, middle, near hills for each season_of()
  static const uint32_t HILLS[5][3] = {{0xA4B8CE, 0xC8D6E4, 0xE0EAF2},
                                       {0x6C8CA8, 0x4C5C50, 0x303C34},
                                       {0x6C8CA8, 0x6E9A58, 0x3E7040},
                                       {0x6C8CA8, 0x486C60, 0x284434},
                                       {0x6C8CA8, 0x7A6236, 0x5A4420}};
  static const uint32_t BIRCH[5] = {0x8C9CAE, 0x8C9CAE, 0x9AD070, 0x6AA850, 0xE0B030};
  uint32_t night_c[3] = {0x223344u, 0x112233u, n.white ? 0x334455u : 0x111122u};
  for (int l = 0; l < 3; l++) {
    s.hill_c[l] = mix(night_c[l], HILLS[n.season][l], n.light);
    if (l == 0)
      s.hill_c[l] = mix(s.hill_c[l], s.sky[4], 0.3f);
    if (n.fog > 15)
      s.hill_c[l] = mix(s.hill_c[l], s.fog_c, n.fog / 100 * (0.5f - l * 0.15f));
    s.tree_c[l] = s.hill_c[l];
  }
  if (n.white) {  // dark spruces with snow on them, against the white
    s.snow_trees = true;
    s.tree_c[1] = s.tree_c[2] = mix(0x18302A, 0x2E5240, n.light);
  }
  s.birch_c = mix(mix(0x223344, BIRCH[n.season], n.light), s.hill_c[2], n.fog / 200);
  for (int x = 0; x < W; x++) {
    s.hill[0][x] = (int16_t) (HORIZON - 6 - 7 * sinf(x / 61.0f + ph[0]) - 5 * sinf(x / 23.0f + ph[1]) -
                              2 * sinf(x / 9.0f + ph[2]));
    s.hill[1][x] = (int16_t) (174 - 11 * sinf(x / 47.0f + ph[3]) - 6 * sinf(x / 19.0f + ph[4]));
    s.hill[2][x] = (int16_t) (204 - 8 * sinf(x / 71.0f + ph[5]) - 4 * sinf(x / 29.0f + ph[6]));
  }
  s.cabin = true;
  s.cabin_x = (int16_t) (CABIN_X + h.in(-8, 8));
  int ntrees = 18, nfront = 11;
  if (n.o.ground == G_COAST) {
    coast(s, n, h, ph);
    ntrees = nfront = 5;
  } else if (n.o.ground == G_MOUNTAINS) {
    mountains(s, n, ph);
    ntrees = 14;
    nfront = 9;
  } else if (n.o.ground == G_CITY) {
    city(s, n, h);
    ntrees = nfront = 6;
  }
  if (s.sea)
    sea(s, n, h);
  s.cabin_y = (int16_t) (s.hill[2][s.cabin_x] + 6);
  // flagpole by the cabin, or in the city's park on flag days
  int park = h.in(90, 130);
  s.pole_x = (int16_t) (s.cabin ? s.cabin_x + 24 : n.flag_day ? park : -1);  // the park's pole stands all day
  s.pole_base = (int16_t) (s.cabin ? s.cabin_y - 2 : s.hill[2][park] + 4);
  s.pole_top = (int16_t) (s.pole_base - 48);
  s.home_x = (int16_t) (s.cabin ? s.cabin_x : park - 24);
  for (int i = 0; i < ntrees; i++) {  // spruces and a few birches, big in front and small on the middle hills
    bool front = i < nfront;
    int x = h.in(0, W - 1), l = front ? 2 : 1;
    if (n.o.ground == G_COAST && !front)
      continue;
    if (front && x > s.home_x - 36 && x < s.home_x + 44)  // keep a clearing around home
      x = (x + W / 2) % W;
    s.tree_x[s.ntrees] = (int16_t) x;
    s.tree_base[s.ntrees] = (int16_t) (s.hill[l][x] + 3);
    s.tree_top[s.ntrees] = (int16_t) (s.tree_base[s.ntrees] - (front ? h.in(18, 40) : h.in(8, 16)));
    s.tree_layer[s.ntrees] = (uint8_t) l;
    bool birch = i % 5 == 3;
    s.tree_kind[s.ntrees++] = (uint8_t) (birch ? (n.season <= 1 ? 2 : 1) : 0);
  }
  s.window_lit = n.sun.el < -3 && ((n.hour >= 6 && n.hour < 23) || n.xmas || n.newyear);  // asleep after eleven
  s.window_c = n.halloween ? q(255, 140, 40) : q(255, 210, 122);
  s.cabin_c = mix(mix(0x2A1418, 0x9A3426, n.light), s.hill_c[2], n.fog / 200);  // falu red
  // fog over the far hills, and over the middle ones when it's thick
  for (int f = 0; f < 2; f++)
    for (int x = 0; x < W; x++) {
      bool on = f == 0 ? n.fog > 15 : n.fog > 45;
      int edge = f == 0 ? HORIZON - 4 - (int) (n.fog * 0.1f) : 178 - (int) (n.fog * 0.12f);
      s.fog_top[f][x] = (int16_t) (on ? edge + (int) (3 * sinf(x / 17.0f + ph[7] + f)) : H);
    }
}

// Birds, bats, the chimney's smoke and the flagpole
inline void life(Scene &s, const Now &n, Rng &rng) {
  const Weather &w = n.w;
  bool fair = n.sun.el > 3 && n.cover < 60 && !n.rain && !n.snow && !n.thunder && n.fog < 30 && w.wind < 12;
  if (fair && !n.halloween) {
    int count = rng.in(2, 5), bx = rng.in(20, W - 60), by = rng.in(40, 100);
    for (int i = 0; i < count; i++)
      sprite_rows(sprite(s, bx + i * 11 + rng.in(-2, 2), by + (i % 2 ? 4 : 0) + rng.in(-1, 1), 7, 3, 0, s.tree_c[1]),
                  BIRD);
  }
  if (n.halloween && n.dark) {  // bats, around the moon if it's up, else low where the sky is lighter
    int bx = s.moon ? s.moon_x - 40 : rng.in(40, W - 100), by = s.moon ? s.moon_y - 16 : rng.in(70, 110);
    for (int i = 0; i < 7; i++)
      sprite_rows(sprite(s, bx + rng.in(-10, 90), by + rng.in(-20, 36), 9, 4, 0, q(0, 0, 0)), BAT);
  }
  // borrowed so it doesn't cost one of the 16 colours
  bool mid_low = false, high = false;
  for (int i = 0; i < s.nclouds; i++)
    (s.cloud_layer[i] ? mid_low : high) = true;
  s.white_c = mid_low ? s.cloud_lit[1] : high ? s.cloud_lit[0] : s.sun ? s.sun_c : s.cloud_lit[1];
  // Chimney smoke while the heater's on, more in fireplace mode, and hearts on Valentine's day. Layer 1, since
  // the cabin stands on the near hill.
  int puffs = s.cabin ? n.o.fireplace ? 5 : n.o.heater_pct > 0 ? 2 + std::min(3, n.o.heater_pct / 30) : 0 : 0;
  if (n.valentine && !puffs)
    puffs = 3;
  if (n.midsummer)
    puffs = 0;
  int cx = s.cabin ? s.cabin_x + 9 : s.home_x, cy = s.cabin ? s.cabin_y - 32 : s.pole_base - 10;
  for (int i = 0; i < puffs; i++) {
    int r = 3 + i, px = cx + n.wdir * (i * i + 1) * (1 + (int) w.wind / 4) + (int) (rng.in(-1, 1)),
        py = cy - 6 - i * 10;
    if (n.valentine && (i >= puffs - 2 || !s.cabin))
      sprite_rows(sprite(s, px - 6, py - 5, 13, 11, 1, mix(0xE04070, 0xFF80B0, n.light)), HEART);
    else
      blob(s, px, py, r, std::max(2, r - 1), 1, s.white_c);
  }
  // The pennant, or the flag on flag days. Blue from the top of the sky and white_c, colours we have anyway.
  // Only the flag's red is extra.
  uint32_t blue = s.sky[0];
  int fx = s.pole_x, fy = s.pole_top;
  if (n.flag_up && s.pole_x >= 0) {
    flag_norway(s, n.wdir < 0 ? fx - 23 : fx + 2, fy, mix(0x601418, 0xEF2B2D, n.light), s.white_c, blue);
  } else if (s.cabin) {
    int len = std::min(30, 14 + 2 * (int) w.wind);
    pennant(s, n.wdir < 0 ? fx - len - 1 : fx + 2, fy, len, n.wdir, s.cabin_c, s.white_c, blue);
  }
}

inline void holidays(Scene &s, const Now &n, Rng &rng) {
  if (n.xmas && n.dark) {  // lights on the spruce nearest home, a star on top
    int best = -1;
    for (int i = 0; i < s.ntrees; i++)
      if (s.tree_layer[i] == 2 && s.tree_kind[i] == 0 &&
          (best < 0 || abs(s.tree_x[i] - s.home_x) < abs(s.tree_x[best] - s.home_x)))
        best = i;
    if (best >= 0) {
      uint32_t cols[3] = {q(255, 60, 60), s.window_c, q(60, 220, 100)};
      int top = s.tree_top[best], base = s.tree_base[best];
      Rng lt{77};
      for (int k = 0; k < 14; k++) {
        int d = 7 + (base - top - 9) * k / 14, hw = std::max(d / 4, d * 2 / 5 - (d % 6) / 2);
        box(s, s.tree_x[best] + lt.in(-hw, hw - 1), top + d, 2, 2, 2, cols[k % 3]);
      }
      sprite_rows(sprite(s, s.tree_x[best] - 3, top - 5, 7, 7, 2, s.window_c), STAR7);
    }
  }
  if (n.newyear && n.dark) {  // fireworks, somewhere new every redraw
    Rng fw{(uint32_t) (n.t / 300) * 40503u + 1};
    uint32_t cols[3] = {q(255, 100, 170), q(100, 220, 255), q(255, 230, 120)};
    for (int i = 0; i < 4; i++)
      burst(s, fw.in(30, W - 30), fw.in(24, 100), fw.in(9, 12), cols[(i + fw.in(0, 1)) % 3],
            s.nstars ? s.star_c : 0xFFFFFF);
  }
  if (n.easter_days && !n.white) {  // striped eggs in the grass in front of home
    uint32_t cols[3] = {s.window_c, q(230, 100, 170), q(100, 190, 230)};
    Rng eg{(uint32_t) n.year * 7u};
    for (int i = 0; i < 5; i++) {
      int x = (s.home_x - 84 + i * 19 + eg.in(-3, 3) + W) % W, y = s.hill[2][x] + 12 + eg.in(0, 8);
      blob(s, x, y, 4, 5, 1, cols[i % 3]);
      box(s, x - 3, y - 2, 7, 1, 1, s.white_c);
      box(s, x - 3, y + 1, 7, 1, 1, s.white_c);
    }
  }
  if (n.midsummer) {  // the bonfire, on the near hill away from home
    int bx = (s.home_x + W / 2) % W, by = s.hill[2][bx] + 14;
    box(s, bx - 9, by - 2, 19, 3, 2, s.tree_c[2]);  // the logs
    sprite_rows(sprite(s, bx - 7, by - 21, 15, 20, 2, q(255, 130, 30)), FLAME);
    sprite_rows(sprite(s, bx - 7, by - 21, 15, 20, 2, q(255, 220, 90)), FLAME_IN);
    for (int i = 0; i < 10; i++)
      box(s, bx + rng.in(-12, 12), by - 24 - rng.in(0, 26), 1, 1, 2, q(255, 220, 90));
    blob(s, bx + n.wdir * 10, by - 36, 6, 4, 1, s.hill_c[0]);  // smoke in the far hills' blue-grey
    blob(s, bx + n.wdir * 18, by - 46, 8, 5, 1, s.hill_c[0]);
  }
  if (n.halloween) {  // a pumpkin by the door, or by the pole in the park
    int px = s.cabin ? s.cabin_x - 22 : s.home_x + 36, py = (s.cabin ? s.cabin_y : s.pole_base) - 5;
    blob(s, px, py, 7, 6, 1, q(255, 130, 40));
    box(s, px - 1, py - 8, 3, 3, 1, s.tree_c[2]);  // its stalk
    if (n.dark)
      sprite_rows(sprite(s, px - 5, py - 3, 11, 7, 2, q(17, 17, 34)), FACE);
  }
}

// The forecast over the picture, in white with an outline
inline void overlay(Scene &s, const Now &n, int utc_offset) {
  s.overlay = true;
  memset(s.ov, 0, OV_H * sizeof *s.ov);
  s.text_c = q(255, 255, 255);
  // outline in the darkest colour already in the picture, if it's dark enough
  uint32_t cand[] = {s.sky[0], s.hill_c[0], s.hill_c[1], s.hill_c[2], s.cloud_shade[2]};
  auto luma = [](uint32_t c) { return (int) ((c >> 16) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10; };
  s.outline_c = cand[0];
  for (uint32_t c : cand)
    if (luma(c) < luma(s.outline_c))
      s.outline_c = c;
  if (luma(s.outline_c) > 70)
    s.outline_c = q(17, 17, 34);
  forecast(s, n.w, n.t, utc_offset);
}

// The whole scene at t, UTC. Without w.ok there's no weather in it. utc_offset is local time minus UTC in
// seconds.
inline void build(Scene &s, const Weather &w, double lat, double lon, uint32_t t, bool with_forecast, int utc_offset,
                  const Options &o) {
  Sprite *sprite = s.sprite;
  uint8_t (*ov)[W / 4] = s.ov;
  memset(&s, 0, sizeof s);
  s.sprite = sprite;
  s.ov = ov;
  s.ground = o.ground;
  s.shine_x = -1;
  Now n(w, o, lat, lon, t, utc_offset);
  sky(s, n);
  sun_moon(s, n);
  aurora(s, n);
  stars(s, n);
  clouds(s, n);
  precipitation(s, n);
  ground(s, n);
  Rng rng{(uint32_t) (n.local / 3600) * 1664525u + 1013904223u};  // for life() and holidays(), new each hour
  life(s, n, rng);
  holidays(s, n, rng);
  if (with_forecast && w.ok)
    overlay(s, n, utc_offset);
}

// ---------------------------------------------------------------- drawing a row

inline void span(uint32_t *row, int a, int b, uint32_t c) {
  if (a < 0)
    a = 0;
  if (b > W - 1)
    b = W - 1;
  for (int x = a; x <= b; x++)
    row[x] = c;
}

// Half width of an ellipse dy rows from its centre, -1 if outside
inline int half(int dy, int rx, int ry) {
  if (dy < -ry || dy > ry || ry <= 0)
    return -1;
  float v = 1 - (float) (dy * dy) / (float) (ry * ry);
  return (int) (rx * sqrtf(v) + 0.5f);
}

inline void disc(uint32_t *row, int y, int cx, int cy, int r, uint32_t c) {
  int h = half(y - cy, r, r);
  if (h >= 0)
    for (int k = -1; k <= 1; k++)  // north is at both edges, so draw it there too
      span(row, cx - h + k * W, cx + h + k * W, c);
}

inline void sprites(const Scene &s, int y, uint32_t *out, int layer) {
  for (int i = 0; i < s.nsprites; i++) {
    const Sprite &p = s.sprite[i];
    if (p.layer != layer || y < p.y || y >= p.y + p.h)
      continue;
    uint32_t bits = p.rows[y - p.y];
    for (int c = 0; c < p.w; c++)
      if (bits >> (p.w - 1 - c) & 1) {
        int x = p.x + c;
        if (x >= 0 && x < W)
          out[x] = p.c;
      }
  }
}

inline void tree(const Scene &s, int i, int y, uint32_t *out) {
  int l = s.tree_layer[i], d = y - s.tree_top[i];
  if (s.tree_kind[i] == 0) {  // spruce, a narrow cone with ragged tiers
    int hw = std::max(d / 4, d * 2 / 5 - (d % 6) / 2);
    bool snowy = s.snow_trees && d % 6 == 1 && d > 2;
    span(out, s.tree_x[i] - hw, s.tree_x[i] + hw, snowy ? s.hill_c[2] : s.tree_c[l]);
    return;
  }
  int height = s.tree_base[i] - s.tree_top[i], crown = height * 6 / 10;
  if (d < crown && s.tree_kind[i] == 1) {  // a round crown
    int h = half(d - crown / 2, std::max(3, height / 4), crown / 2);
    if (h >= 0)
      span(out, s.tree_x[i] - h, s.tree_x[i] + h, s.birch_c);
  } else if (d < crown) {  // bare, a few twigs
    if (d % 4 == 0)
      span(out, s.tree_x[i] - 1 - d / 5, s.tree_x[i] + 1 + d / 5, s.tree_c[l]);
  }
  if (d >= crown / 3)
    span(out, s.tree_x[i], s.tree_x[i] + (height > 24 ? 1 : 0), s.tree_c[l]);  // the trunk
}

// Draws row y of the scene into out, W pixels
inline void row(const Scene &s, int y, uint32_t *out) {
  int b = 4;
  while (b > 0 && y < BAND[b])
    b--;
  span(out, 0, W - 1, s.sky[b]);
  if (y <= HORIZON) {
    if (s.glow)
      for (int step = 0; step < 2; step++) {  // a wide faint one, a smaller strong one
        int rx = step ? s.glow_rx * 3 / 5 : s.glow_rx * 3 / 2, ry = step ? s.glow_ry * 3 / 5 : s.glow_ry;
        int h = half(y - HORIZON, rx, ry);
        if (h >= 0)
          for (int k = -1; k <= 1; k++)
            span(out, s.glow_x - h + k * W, s.glow_x + h + k * W, step ? s.glow_c : s.glow_outer_c);
      }
    if (s.aurora)  // top colour, main colour, light lower edge
      for (int r = 0; r < 2; r++)
        for (int x = 0; x < W; x++) {
          int top = s.aur_top[r][x], bot = s.aur_bot[r][x];
          if (y < top || y >= bot)
            continue;
          int th = bot - top;
          out[x] = y - top < th * 3 / 10 ? s.aur_top_c : bot - y <= std::max(2, th / 5) ? s.aur_edge_c : s.aur_c;
        }
    for (int i = 0; i < s.nstars; i++)
      if (s.star_y[i] == y)
        out[s.star_x[i]] = s.dim_c;
    for (int i = 0; i < s.nbright; i++) {  // catalogue stars are 2x2, the brightest a round 4x4
      int dy = y - s.bright_y[i], x = s.bright_x[i];
      if (dy == 0 || dy == 1)
        span(out, x - (s.bright_big[i] ? 1 : 0), x + 1 + (s.bright_big[i] ? 1 : 0), s.star_c);
      else if (s.bright_big[i] && (dy == -1 || dy == 2))
        span(out, x, x + 1, s.star_c);
    }
    if (s.moon) {
      if (s.moon_glow)
        disc(out, y, s.moon_x, s.moon_y, 15, s.moon_glow_c);
      int dy = y - s.moon_y, h = half(dy, 9, 9);
      if (h >= 0) {  // lit from the right while waxing, from the left after full
        float lim = cosf(s.moon_phase * 2 * (float) kPi) * 9 * sqrtf(1 - dy * dy / 81.0f);
        for (int dx = -h; dx <= h; dx++) {
          bool lit = s.moon_phase < 0.5f ? dx > lim : dx < -lim;
          int x = s.moon_x + dx;
          if (lit && x >= 0 && x < W)
            out[x] = s.moon_c;
        }
      }
    }
    if (s.sun) {
      if (s.sun_rings) {
        disc(out, y, s.sun_x, s.sun_y, 30, s.ring2_c);
        disc(out, y, s.sun_x, s.sun_y, 21, s.ring1_c);
      }
      disc(out, y, s.sun_x, s.sun_y, 13, s.sun_c);
    }
  }
  for (int i = 0; i < s.nclouds; i++) {
    const Cloud &c = s.cloud[i];
    if (y < c.top || y > c.base)
      continue;
    int l = s.cloud_layer[i];
    uint32_t col = y > c.shade ? s.cloud_shade[l] : s.cloud_lit[l];
    int x0 = (c.x + s.drift[l]) % FIELD - 100;
    for (int j = 0; j < c.n; j++) {
      int h = half(y - c.e[j].y, c.e[j].rx, c.e[j].ry);
      if (h < 0)
        continue;
      int cx = x0 + c.e[j].x;
      span(out, cx - h, cx + h, col);
      span(out, cx - h - FIELD, cx + h - FIELD, col);
    }
  }
  if (s.rainbow && y < HORIZON)
    for (int k = 0; k < 3; k++) {  // three bands, each 4 rows thick, red outermost
      int outer = half(y - s.rb_y, RB_RX - k * 4, RB_RY - k * 4),
          inner = half(y - s.rb_y, RB_RX - k * 4 - 4, RB_RY - k * 4 - 4);
      if (outer < 0)
        continue;
      if (inner < 0)
        inner = -1;
      span(out, s.rb_x - outer, s.rb_x - inner - 1, s.rb_c[k]);
      span(out, s.rb_x + inner + 1, s.rb_x + outer, s.rb_c[k]);
    }
  sprites(s, y, out, 0);
  if (s.ridge)
    for (int x = 0; x < W; x++)
      if (y >= s.ridge_y[x])
        out[x] = s.ridge_c;
  for (int l = 0; l < 3; l++) {
    for (int x = 0; x < W; x++)
      if (y >= s.hill[l][x])
        out[x] = l == 0 && s.ground == G_MOUNTAINS && y < s.snowline + (int) (4 * sinf(x / 9.0f)) ? s.snowcap_c
                                                                                                  : s.hill_c[l];
    if (l == 0 && s.sea && y >= s.sea_top) {  // the water, its waves and the reflection
      for (int x = 0; x < W; x++)
        if (y < s.hill[1][x] && (s.ground != G_COAST || y >= s.hill[0][x] || s.hill[0][x] == H))
          out[x] = s.sea_c;
      for (int i = 0; i < s.nwaves; i++)
        if (s.wave_y[i] == y && y < s.hill[1][s.wave_x[i]])
          span(out, s.wave_x[i], s.wave_x[i] + s.wave_w[i], s.wave_c);
      if (s.shine_x >= 0 && (y & 1) && y < s.hill[1][s.shine_x]) {
        int spread = 1 + (y - s.sea_top) / 6;
        span(out, s.shine_x - spread, s.shine_x + spread, s.wave_c);
      }
    }
    if (l == 1 && s.nbld && y < 178)  // the city, far row first
      for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < s.nbld; i++) {
          if ((s.bld_back[i] != 0) != (pass == 0))
            continue;
          int top = s.bld_top[i], x0 = s.bld_x[i], w = s.bld_w[i], cx = x0 + w / 2;
          uint32_t c = pass == 0 ? s.bld_back_c : s.bld_c;
          if (y < top) {  // what's on the roof
            if (s.bld_kind[i] == 1 && y >= top - 5)
              span(out, cx - w / 6, cx + w / 6, c);
            else if (s.bld_kind[i] == 2 && y >= top - 12)
              span(out, cx, cx, s.bld_edge_c);
            else if (s.bld_kind[i] == 3 && y >= top - 14)
              span(out, cx - (y - top + 14) * (w / 2) / 14, cx + (y - top + 14) * (w / 2) / 14, c);
            continue;
          }
          span(out, x0, x0 + w - 1, c);
          if (pass == 0)
            continue;
          out[std::min(W - 1, x0 + w - 1)] = s.bld_edge_c;
          int r = (y - top - 3) / 5;  // windows, some lit at night
          if (r >= 0 && (y - top - 3) % 5 < 2 && y < 174)
            for (int k = 0; k * 5 + 6 < w; k++) {
              uint32_t hsh = (s.win_seed ^ (uint32_t) (i * 7919 + r * 131 + k * 17)) * 2246822519u;
              bool lit = s.win_seed && s.window_lit && (hsh >> 13) % 3 == 0;
              span(out, x0 + 3 + k * 5, x0 + 4 + k * 5, lit ? s.window_c : s.bld_edge_c);
            }
        }
    if (l == 1 && s.lighthouse) {  // the lighthouse tower with its band, then the lamp room and its roof
      int cx = LH_X, by = s.lh_y;
      if (y > by - 25 && y <= by)
        span(out, cx - 3 - (y > by - 8 ? 1 : 0), cx + 3 + (y > by - 8 ? 1 : 0),
             y > by - 15 && y <= by - 10 ? s.lh_band_c : s.lh_c);
      else if (y > by - 30 && y <= by - 25)
        span(out, cx - 2, cx + 2, s.tree_c[2]);
      else if (y > by - 33 && y <= by - 30)
        span(out, cx - (y - (by - 33)), cx + (y - (by - 33)), s.tree_c[2]);
    }
    for (int i = 0; i < s.ntrees; i++)
      if (s.tree_layer[i] == l && y >= s.tree_top[i] && y <= s.tree_base[i])
        tree(s, i, y, out);
    // the cabin, falu red with white trim
    if (l == 2 && s.cabin) {
      int cx = s.cabin_x, r = s.cabin_y - y;  // r: rows above the floor
      if (r >= 0 && r < 16) {
        span(out, cx - 13, cx + 13, s.cabin_c);
        span(out, cx - 13, cx - 13, s.white_c);
        span(out, cx + 13, cx + 13, s.white_c);
      }
      if (r >= 16 && r < 26) {  // the roof in the near hill's colour, so it's turf or snow, with white barge boards
        int hw = 16 - (r - 16) * 16 / 10;
        span(out, cx - hw, cx + hw, s.hill_c[2]);
        span(out, cx - hw, cx - hw + 1, s.white_c);
        span(out, cx + hw - 1, cx + hw, s.white_c);
      }
      if (r >= 22 && r < 32)  // the chimney
        span(out, cx + 7, cx + 10, s.tree_c[2]);
      if (r >= 6 && r < 14)  // the window frame, then four panes, lit at night
        span(out, cx - 10, cx - 2, s.white_c);
      if (r >= 7 && r < 13 && r != 10) {
        uint32_t glass = s.window_lit ? s.window_c : s.tree_c[2];
        span(out, cx - 9, cx - 7, glass);
        span(out, cx - 5, cx - 3, glass);
      }
      if (r >= 0 && r < 12)  // the door and its frame
        span(out, cx + 2, cx + 8, s.white_c);
      if (r >= 0 && r < 11)
        span(out, cx + 3, cx + 7, s.tree_c[2]);
    }
    if (l == 2 && s.pole_x >= 0 && y >= s.pole_top && y <= s.pole_base)  // the flagpole
      span(out, s.pole_x, s.pole_x + 1, s.tree_c[2]);
    if (l < 2)
      for (int x = 0; x < W; x++)
        if (y >= s.fog_top[l][x])
          out[x] = s.fog_c;
  }
  sprites(s, y, out, 1);
  for (int i = 0; i < s.nprec; i++) {
    int dy = y - s.prec_y[i];
    if (dy < 0 || y < s.prec_top[s.prec_x[i]])
      continue;
    if (s.prec_kind[i] == 0) {  // a streak, 7 rows
      if (dy >= 0 && dy < 7) {
        int x = s.prec_x[i] + s.slant * dy / 8;
        if (x >= 0 && x < W)
          out[x] = s.rain_c;
      }
    } else if (dy >= 0 && dy < s.prec_kind[i]) {
      span(out, s.prec_x[i], s.prec_x[i] + s.prec_kind[i] - 1, s.snow_c);
    }
  }
  if (s.bolt)
    for (int i = 0; i < 6; i++) {
      int y0 = s.bolt_y[i], y1 = s.bolt_y[i + 1];
      if (y >= y0 && y < y1) {
        int x = s.bolt_x[i] + (s.bolt_x[i + 1] - s.bolt_x[i]) * (y - y0) / (y1 - y0);
        span(out, x, x + 1, s.bolt_c);
      }
    }
  sprites(s, y, out, 2);
  if (s.overlay && y < OV_H)
    for (int x = 0; x < W; x++) {
      int v = ov_get(s, x, y);
      if (v)
        out[x] = v == 2 ? s.text_c : s.outline_c;
    }
}

}  // namespace flexit::panel::met
