// probes.yaml: where the probes flying now are, on a chart of the inner solar system. JPL Horizons is asked where
// each was at midnight UTC today and tomorrow, one probe after the other, once a day; the planets are worked out here
// from their mean orbits, close enough for the chart. The Sun and the stars are the picture, the orbits, planets and
// probes are marks over it, the outer planets and the far probes arrows at its edge. The three farthest go at the
// left, with how long the farthest one's signal takes to reach us.
// Positions: NASA/JPL Horizons. Orbits: E. M. Standish, JPL, Keplerian elements for approximate positions of the
// major planets.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
#include "../panel/picture_online.h"
#include "space.h"

namespace flexit::panel::space::probes {

struct Probe {
  int id;  // Horizons'
  const char *name;
};
inline constexpr Probe PROBES[] = {
    {-31, "Voyager 1"},      {-32, "Voyager 2"}, {-98, "New Horizons"},    {-49, "Lucy"},
    {-255, "Psyche"},        {-91, "Hera"},      {-159, "Europa Clipper"}, {-28, "JUICE"},
    {-144, "Solar Orbiter"}, {-96, "Parker"},    {-121, "BepiColombo"}};
inline constexpr int N = (int) std::size(PROBES);
inline constexpr uint32_t PROBE = 0xD6F2FF;
inline constexpr double NEAR_AU = 2.5;  // farther ones are arrows at the edge

// Where a probe was, AU from the Sun in the ecliptic of J2000, at midnight UTC on day and the day after
struct Track {
  bool ok = false;
  uint32_t day = 0;  // Unix time
  double at[2][3] = {};
};
inline Track tracks[N];

// a, e, I, L, the longitudes of perihelion and of the node, AU and degrees at J2000, and how they go a century
struct Planet {
  const char *name;
  uint32_t rgb;
  double el[6], rate[6];
};
inline constexpr Planet PLANETS[] = {
    {"Mercury",
     0xAAA096,
     {0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593},
     {0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081}},
    {"Venus",
     0xECD296,
     {0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255},
     {0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418}},
    {"Earth",
     0x5AA0FF,
     {1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0},
     {0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0}},
    {"Mars",
     0xE66E46,
     {1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891},
     {0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343}},
    {"Jupiter",
     0xE6BE8C,
     {5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909},
     {-0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106}},
    {"Saturn",
     0xECD696,
     {9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448},
     {-0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794}},
    {"Uranus",
     0x96DCE6,
     {19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503},
     {-0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589}},
    {"Neptune",
     0x5A82FF,
     {30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574},
     {0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664}},
};
inline constexpr int INNER = 4;  // Mercury to Mars are drawn to scale

struct Vec {
  double x = 0, y = 0, z = 0;
};
inline double dist(const Vec &a, const Vec &b = {}) {
  return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Where planet p is at Unix time t
inline Vec planet_at(const Planet &p, uint32_t t) {
  const double r = M_PI / 180, T = (t / 86400.0 + 2440587.5 - 2451545.0) / 36525;
  double v[6];
  for (int i = 0; i < 6; i++)
    v[i] = p.el[i] + p.rate[i] * T;
  double e = v[1], I = v[2] * r, node = v[5] * r, peri = (v[4] - v[5]) * r;
  double M = fmod(v[3] - v[4], 360) * r, E = M;
  for (int i = 0; i < 8; i++)
    E -= (E - e * sin(E) - M) / (1 - e * cos(E));
  double px = v[0] * (cos(E) - e), py = v[0] * sqrt(1 - e * e) * sin(E);
  double cw = cos(peri), sw = sin(peri), cn = cos(node), sn = sin(node), ci = cos(I), si = sin(I);
  return {(cw * cn - sw * sn * ci) * px + (-sw * cn - cw * sn * ci) * py,
          (cw * sn + sw * cn * ci) * px + (-sw * sn + cw * cn * ci) * py, sw * si * px + cw * si * py};
}

// Where probe i is at Unix time t, from its two days, on as they go at most days after them
inline Vec probe_at(int i, uint32_t t, double most = 2) {
  const Track &k = tracks[i];
  double f = std::clamp(((double) t - k.day) / 86400, -1.0, most);
  return {k.at[0][0] + (k.at[1][0] - k.at[0][0]) * f, k.at[0][1] + (k.at[1][1] - k.at[0][1]) * f,
          k.at[0][2] + (k.at[1][2] - k.at[0][2]) * f};
}

// ---------------------------------------------------------------- asking JPL Horizons

// " X =-1.2E-01 Y = 9.6E-01 Z =-4.1E-05" as its three, false if line isn't one
inline bool xyz(const char *line, double *at) {
  while (*line == ' ')
    line++;
  if (*line != 'X')
    return false;
  for (int i = 0; i < 3; i++) {
    const char *p = strchr(line, "XYZ"[i]);
    if (!p || strncmp(p + 1, " =", 2))
      return false;
    char *end;
    at[i] = strtod(p + 3, &end);
    if (end == p + 3)
      return false;
    line = end;
  }
  return true;
}

// One probe's answer, read a line at a time: its two positions are the lines with X, Y and Z between $$SOE and $$EOE
struct Horizons : fetch::Request {
  int probe = 0;
  uint32_t day = 0;
  char line[120];
  int len = 0, rows = 0;
  bool in = false;
  double at[2][3];

  void take() {
    line[len] = 0;
    len = 0;
    if (!strncmp(line, "$$SOE", 5))
      in = true;
    else if (!strncmp(line, "$$EOE", 5))
      in = false;
    else if (in && rows < 2 && xyz(line, at[rows]))
      rows++;
  }
  bool feed(const char *p, size_t n) override {
    for (size_t i = 0; i < n; i++)
      if (p[i] == '\n')
        take();
      else if (len < (int) sizeof line - 1)
        line[len++] = p[i];
    return true;
  }
};

// JPL Horizons, a probe at a time, a round of them each day. Panel Probes is its status.
struct Positions : fetch::Asker {
  uint32_t day = 0;  // the round's, midnight UTC
  fetch::Request *make(int step) override;
  bool got(fetch::Request &r) override;
};
inline Positions horizons;
inline std::string name;

inline std::string url_of(int id, uint32_t day) {
  int y, m, d, y1, m1, d1;
  civil_from_days((int32_t) (day / 86400), &y, &m, &d);
  civil_from_days((int32_t) (day / 86400 + 1), &y1, &m1, &d1);
  char u[360];
  snprintf(u, sizeof u,
           "https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND=%%27%d%%27&OBJ_DATA=%%27NO%%27"
           "&MAKE_EPHEM=%%27YES%%27&EPHEM_TYPE=%%27VECTORS%%27&CENTER=%%27500@10%%27"
           "&START_TIME=%%27%04d-%02d-%02d%%27&STOP_TIME=%%27%04d-%02d-%02d%%27&STEP_SIZE=%%271d%%27"
           "&VEC_TABLE=%%271%%27&OUT_UNITS=%%27AU-D%%27&REF_PLANE=%%27ECLIPTIC%%27",
           id, y, m, d, y1, m1, d1);
  return u;
}

// Where probe step was on the round's day
inline fetch::Request *Positions::make(int step) {
  uint32_t now = fetch::now_s();
  if (!step)
    day = now - now % 86400;
  auto *h = fetch::request<Horizons>(url_of(PROBES[step].id, day));
  if (h) {
    h->probe = step;
    h->day = day;
  }
  return h;
}

// ---------------------------------------------------------------- the chart

inline constexpr int X0 = 92, Y0 = 6, X1 = 314, Y1 = 234;  // where it goes, the left is for the figures
inline float cx = 158, cy = 170, k = 88;                   // the Sun on it, px an AU

inline void fit(const std::vector<Vec> &near) {
  double x0 = 0, x1 = 0, y0 = 0, y1 = 0;  // the Sun is on it too
  for (const Vec &v : near) {
    x0 = std::min(x0, v.x);
    x1 = std::max(x1, v.x);
    y0 = std::min(y0, v.y);
    y1 = std::max(y1, v.y);
  }
  const float m = 14;  // for the labels
  float w = X1 - X0 - 2 * m, h = Y1 - Y0 - 2 * m;
  k = (float) std::min({88.0, w / std::max(x1 - x0, 0.1), h / std::max(y1 - y0, 0.1)});
  cx = X0 + m + (w - (float) (x1 - x0) * k) / 2 - (float) x0 * k;
  cy = Y0 + m + (h - (float) (y1 - y0) * k) / 2 + (float) y1 * k;
}

struct Mark {
  float x, y;
  std::string name, extra;
  uint32_t rgb;
  float alpha;
  marks::FaceId face;
  char side;
};

// An arrow at the chart's edge toward v, its label on the inside
inline Mark arrow(marks::Layer &l, const Vec &v, const char *name, uint32_t rgb, float alpha) {
  float dx = (float) v.x, dy = (float) -v.y, x, y;
  marks::edge({X0, Y0, X1, Y1}, cx, cy, dx, dy, x, y);
  float a = atan2f(dy, dx), ca = cosf(a), sa = sinf(a);
  x -= 3 * ca;
  y -= 3 * sa;
  l.poly({x + 3.2f * ca, y + 3.2f * sa, x - 2.4f * ca - 2.4f * sa, y - 2.4f * sa + 2.4f * ca, x - 2.4f * ca + 2.4f * sa,
          y - 2.4f * sa - 2.4f * ca},
         rgb, alpha);
  char au[16];
  snprintf(au, sizeof au, "%.0f AU", dist(v));
  char side = ca > 0.7f ? 'l' : ca < -0.7f ? 'r' : sa < 0 ? 'd' : 'u';
  return {x, y, name, au, rgb, alpha, marks::LABEL, side};
}

// Seconds probe i's signal takes to reach the Earth at t, from where it was when it left. A year on at most.
inline double signal_s(int i, uint32_t t) {
  const double AU_S = 499.005;
  Vec e = planet_at(PLANETS[2], t);
  double s = dist(probe_at(i, t, 366), e) * AU_S;
  return dist(probe_at(i, (uint32_t) (t - s), 366), e) * AU_S;
}

inline std::string hours(double s) {
  char b[24];
  snprintf(b, sizeof b, "%d h %d min", (int) (s / 3600), (int) fmod(s / 60, 60));
  return b;
}

inline marks::Layer layer_of(uint32_t t) {
  Vec planet[std::size(PLANETS)], probe[N];
  for (size_t i = 0; i < std::size(PLANETS); i++)
    planet[i] = planet_at(PLANETS[i], t);
  std::vector<int> far;  // the probes it knows, farthest first
  for (int i = 0; i < N; i++)
    if (tracks[i].ok) {
      probe[i] = probe_at(i, t);
      far.push_back(i);
    }
  std::sort(far.begin(), far.end(), [&](int a, int b) { return dist(probe[a]) > dist(probe[b]); });
  size_t listed = std::min<size_t>(3, far.size());
  std::vector<Vec> near(planet, planet + INNER);
  for (size_t j = listed; j < far.size(); j++)
    if (dist(probe[far[j]]) <= NEAR_AU)
      near.push_back(probe[far[j]]);
  fit(near);

  marks::Layer l;
  for (int i = 0; i < INNER; i++)
    l.ring(cx, cy, (float) PLANETS[i].el[0] * k, PLANETS[i].rgb, 0.8f, 0.42f);
  l.boxes.push_back({cx - 9, cy - 9, cx + 9, cy + 9});
  l.boxes.push_back({0, 0, X0 - 2, marks::H});
  std::vector<Mark> ms;
  // The planets' colours go in the picture, the text over it in two inks, so each has the opacities its edges need
  for (int i = 0; i < INNER; i++) {
    float x = cx + (float) planet[i].x * k, y = cy - (float) planet[i].y * k;
    l.rect({x - 3.4f, y - 3.4f, x + 3.4f, y + 3.4f}, 3.4f, PLANETS[i].rgb);
    l.ring(x, y, 5.6f, PLANETS[i].rgb, 1, 0.55f);
    ms.push_back({x, y, PLANETS[i].name, "", marks::WHITE, 0.9f, marks::LABEL, 0});
  }
  for (size_t j = listed; j < far.size(); j++) {
    const Vec &v = probe[far[j]];
    if (dist(v) > NEAR_AU)
      continue;
    float x = cx + (float) v.x * k, y = cy - (float) v.y * k;
    l.dot(x, y, 1.5f, marks::WHITE, 1, 0.45f);
    ms.push_back({x, y, PROBES[far[j]].name, "", PROBE, 0.95f, marks::LABEL, 0});
  }
  for (size_t i = INNER; i < std::size(PLANETS); i++)
    ms.push_back(arrow(l, planet[i], PLANETS[i].name, marks::WHITE, 0.62f));
  for (size_t j = listed; j < far.size(); j++)
    if (dist(probe[far[j]]) > NEAR_AU)
      ms.push_back(arrow(l, probe[far[j]], PROBES[far[j]].name, PROBE, 0.95f));
  for (const Mark &m : ms)
    l.boxes.push_back({m.x - 5, m.y - 5, m.x + 5, m.y + 5});
  for (const Mark &m : ms)
    l.label(m.x, m.y, m.name, m.face, m.rgb, m.alpha, m.side, true, m.extra);

  ESPTime now = ESPTime::from_epoch_local((time_t) t);
  l.text(12, 24, "Probes", marks::BIG);
  l.text(12, 35, marks::day(now.day_of_month, now.month) + " " + std::to_string(now.year), marks::SMALL, marks::WHITE,
         0.62f);
  l.caps(12, 58, "Farthest");
  float y = 74;
  for (size_t j = 0; j < listed; y += 31, j++) {
    char au[16];
    snprintf(au, sizeof au, "%.0f", dist(probe[far[j]]));
    l.text(12, y, PROBES[far[j]].name, marks::SMALL, marks::WHITE, 0.7f);
    float w = l.text(12, y + 15, au, marks::BIG);
    l.text(12 + w + 3, y + 15, "AU", marks::SMALL, marks::WHITE, 0.7f);
  }
  if (!far.empty()) {  // its signal: how long it takes, and when that reaches a day, within a year
    int f = far[0];
    double s = signal_s(f, t);
    l.caps(12, 176, std::string(PROBES[f].name) + "'s signal");
    l.text(12, 191, hours(s), marks::TITLE);
    std::string when = s >= 86400 ? "Over a light-day away" : "";
    for (int n = 1; n <= 366 && when.empty(); n++) {
      double s1 = signal_s(f, t + n * 86400u);
      if (s1 >= 86400) {
        ESPTime d = ESPTime::from_epoch_local((time_t) (t + (n - 1 + (86400 - s) / (s1 - s)) * 86400));
        when = "A light-day on " + marks::day(d.day_of_month, d.month);
      }
      s = s1;
    }
    if (!when.empty())
      l.text(12, 203, when, marks::LABEL, marks::WHITE, 0.62f);
  }
  l.line({12, 226, 12 + k / 2, 226}, 1, marks::WHITE, 0.4f);
  l.caps(12, 221, "½ AU", 0.45f);
  return l;
}

// A probe's answer: the next is asked for, and the chart's marks are made once the round is done. Without its
// positions Horizons has nothing for it now, like after its mission ended.
inline bool Positions::got(fetch::Request &r) {
  auto &h = static_cast<Horizons &>(r);
  Track &t = tracks[h.probe];
  t.ok = h.rows == 2;
  if (t.ok) {
    t.day = h.day;
    memcpy(t.at, h.at, sizeof t.at);
  }
  if (h.probe + 1 < N)
    return true;
  int known = 0;
  for (const Track &k : tracks)
    known += k.ok;
  say(std::to_string(known) + " probes, " + when(fetch::now_s()));
  marks::set(name, layer_of(fetch::now_s()));
  return true;
}

// ---------------------------------------------------------------- the picture

inline uint32_t hash(uint32_t v) {
  v ^= v >> 16;
  v *= 0x7FEB352D;
  v ^= v >> 15;
  v *= 0x846CA68B;
  return v ^ v >> 16;
}

// The night, its stars, and the Sun: a disc darker at its edge, with a tight glow
inline uint32_t pixel(int x, int y) {
  float r[3] = {3, 4, 10};
  float rr = hypotf(x + 0.5f - cx, y + 0.5f - cy) / 7.5f;
  if (rr < 1) {
    float disc = sqrtf(1 - rr * rr), m = 0.8f + 0.2f * disc, lit = (0.55f + 0.45f * disc) * 255;
    r[0] += lit;
    r[1] += lit * 0.86f * m;
    r[2] += lit * 0.55f * m * m * m;
  } else {
    float g = expf(-(rr - 1) * 2.4f) * 0.55f * 255;
    r[0] += g;
    r[1] += g * 0.62f;
    r[2] += g * 0.25f;
    uint32_t h = hash((uint32_t) (y * marks::W + x) * 2654435761u);
    if (rr > 3 && h % 640 == 0)
      for (int c = 0; c < 3; c++)
        r[c] += (h >> 12 & 1 ? 107 : 36) + c * (h >> 12 & 1 ? 8 : 6);
  }
  uint32_t out = 0;
  for (float v : r)
    out = out << 8 | (uint32_t) std::min(255.0f, v);
  return out;
}

// From probes.yaml's on_boot. every: how often to ask where they are, status: its sensor. encoding and quality as
// picture.yaml has them.
template<typename S>
void setup(const std::string &title, const char *every, const char *encoding, const char *quality, S &&status) {
  name = title;
  horizons.what = "positions";
  horizons.server = "JPL Horizons";
  horizons.steps = N;
  ask_every(horizons, every, status, 3600);
  picture::Source s;
  s.coding(encoding, quality);
  s.size = [](int &w, int &h) {
    if (!horizons.have) {
      if (picture::S.want == name)
        picture::S.waiting = "asking JPL Horizons where they are";
      return false;
    }
    w = marks::W;
    h = marks::H;
    return true;
  };
  s.pixel = pixel;
  picture::add(name, s);
  marks::on(name);
}

}  // namespace flexit::panel::space::probes
