// rover.yaml: where Perseverance or Curiosity has driven, on the CTX map of its crater, see rover_maps.h. Its
// waypoints, the end of every drive since it landed, are read as they stream a few times a day and kept as map
// pixels, each a pixel or more from the last. The track goes into the map, the older drives warm and the latest cyan,
// and where the rover is now, where it landed, the years and how far it has driven go over it as marks.
// Waypoints: NASA/JPL-Caltech. Map: CTX mosaic, NASA/JPL-Caltech/MSSS, the Murray Lab at Caltech.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
#include "rover_maps.h"
#include "space.h"

namespace flexit::panel::space::rover {

inline constexpr double M_PER_DEG = 3396190 * M_PI / 180;

struct Rover {
  const char *id, *name, *where, *code;  // "perseverance", "Perseverance", "Jezero crater", "M20"
  Landing landed;
  int year;           // it landed
  double lon, lat;    // the map's middle, as scripts/rover_maps.py made it
  float mpp;          // metres a pixel
  int recent;         // sols drawn as the latest
  int years_every;    // a year's mark every this many
  const char *climb;  // the caption for how far up it has gone
  bool column;        // its text in a column at the left, else at the top and the bottom
};
inline constexpr Rover ROVERS[] = {
    {"perseverance",
     "Perseverance",
     "Jezero crater",
     "M20",
     {52304.4545, 77.45},
     2021,
     77.338,
     18.47,
     46,
     120,
     1,
     "Climbed",
     false},
    {"curiosity",
     "Curiosity",
     "Gale crater",
     "MSL",
     {49269.2455, 137.44},
     2012,
     137.35,
     -4.712,
     70,
     300,
     2,
     "Up Mount Sharp",
     true},
};

struct Point {
  int16_t x, y;  // map pixels, in 8ths
  uint16_t sol;
  float fx() const { return x / 8.0f; }
  float fy() const { return y / 8.0f; }
};

struct Waypoint {
  int sol = -1;
  double lon = 0, lat = 0;
  float dist = 0, elev = 0, yaw = 0;  // m driven since it landed, m above the datum, degrees east of north
};

// The waypoints as they come, a point kept for each drive a pixel or more from the last
struct List : Ask {
  const Rover *r = nullptr;
  Waypoint now, first, last;
  std::vector<Point> track;

  void add(const Waypoint &w);
  void value(int depth, const char *key, int, const char *v) override;
  void end(int depth, const char *key) override;
};

// A rover's picture, its waypoints asked for through fetch.h
struct Map : fetch::Asker {
  const Rover *r = nullptr;
  std::vector<Point> track;
  Waypoint first, last;

  fetch::Request *make(int) override {
    std::string code = r->code;
    auto *q =
        fetch::request<List>("https://mars.nasa.gov/mmgis-maps/" + code + "/Layers/json/" + code + "_waypoints.json");
    if (q)
      q->r = r;
    return q;
  }
  bool got(fetch::Request &q) override;
};
inline Map maps[std::size(ROVERS)];

inline Map *map_of(const char *rover) {
  for (size_t i = 0; i < std::size(ROVERS); i++)
    if (!strcmp(ROVERS[i].id, rover))
      return &maps[i];
  return nullptr;
}

inline Point point_of(const Rover &r, double lon, double lat, int sol) {
  double dlat = r.mpp / M_PER_DEG, dlon = dlat / cos(r.lat * M_PI / 180);
  double x = marks::W / 2.0 + (lon - r.lon) / dlon, y = marks::H / 2.0 - (lat - r.lat) / dlat;
  auto eighths = [](double v) { return (int16_t) std::clamp(lround(v * 8), -32000L, 32000L); };
  return {eighths(x), eighths(y), (uint16_t) std::max(sol, 0)};
}

inline void List::add(const Waypoint &w) {
  if (w.sol < 0)
    return;
  if (first.sol < 0)
    first = w;
  last = w;
  Point p = point_of(*r, w.lon, w.lat, w.sol);
  if (track.empty() || abs(p.x - track.back().x) >= 8 || abs(p.y - track.back().y) >= 8)
    track.push_back(p);
}

inline void List::value(int depth, const char *key, int, const char *v) {
  if (depth != 4 || strcmp(parent(), "properties"))
    return;
  Waypoint &w = now;
  if (!strcmp(key, "sol"))
    w.sol = atoi(v);
  else if (!strcmp(key, "lon"))
    w.lon = atof(v);
  else if (!strcmp(key, "lat"))
    w.lat = atof(v);
  else if (!strcmp(key, "dist_total_m"))
    w.dist = (float) atof(v);
  else if (!strcmp(key, "elev_geoid"))
    w.elev = (float) atof(v);
  else if (!strcmp(key, "yaw"))
    w.yaw = (float) atof(v);
}

inline void List::end(int depth, const char *key) {
  if (depth != 3 || strcmp(key, "features"))
    return;
  add(now);
  now = Waypoint();
}

// Earth's local time in the middle of the rover's sol
inline uint32_t sol_middle(const Rover &r, int sol) { return sol_start(sol, r.landed) + 44388; }

// ---------------------------------------------------------------- the marks

// Where it is now and which way it faces, or an arrow at the edge toward it when it's off the map. "Now" beside it.
inline void now_mark(marks::Layer &l, float x, float y, float yaw) {
  bool off = x < 8 || y < 8 || x > marks::W - 8 || y > marks::H - 8;
  if (off) {
    float cx = marks::W / 2.0f, cy = marks::H / 2.0f;
    yaw = atan2f(x - cx, cy - y) * 180 / (float) M_PI;
    marks::edge({11, 11, marks::W - 11.0f, marks::H - 11.0f}, cx, cy, x - cx, y - cy, x, y);  // the tip 3 px in
  }
  float a = yaw * (float) M_PI / 180;
  l.poly({x + 8 * sinf(a), y - 8 * cosf(a), x + 3.6f * sinf(a - 1.9f), y - 3.6f * cosf(a - 1.9f),
          x + 3.6f * sinf(a + 1.9f), y - 3.6f * cosf(a + 1.9f)},
         marks::CYAN);
  if (!off) {
    l.dot(x, y, 3, marks::CYAN, 1, 0.6f);
    l.dot(x, y, 1.3f, marks::INK);
  }
  float w = marks::width(marks::upper("Now"), marks::CAPS, 0.78f) + 9;
  struct {
    float x, y;
  } spots[] = {{x + 9, y}, {x - 9 - w, y}, {x - w / 2, y - 13}, {x - w / 2, y + 13}};
  auto at = spots[0];
  for (auto s : spots)
    if (l.free({s.x, s.y - 6, s.x + w, s.y + 6})) {
      at = s;
      break;
    }
  l.pill(at.x, at.y, "Now");
}

// A dot where it was as each year began, every so many, with the year beside it. Not by where it landed or is.
inline void years(marks::Layer &l, const Map &m, const Point &start, const Point &end) {
  struct Mark {
    float x, y;
    int year;
  };
  std::vector<Mark> got;
  int want = 0;
  for (const Point &p : m.track) {
    int y = ESPTime::from_epoch_local((time_t) sol_middle(*m.r, p.sol)).year;
    if (!want)
      want = y + 1;
    if (y < want)
      continue;
    want = y + m.r->years_every;
    float x = p.fx(), py = p.fy();
    if (x < 4 || py < 4 || x > marks::W - 4 || py > marks::H - 4 || hypotf(x - end.fx(), py - end.fy()) < 16 ||
        hypotf(x - start.fx(), py - start.fy()) < 16)
      continue;
    got.push_back({x, py, y});
  }
  for (const Mark &k : got) {
    l.dot(k.x, k.y, 1.6f, marks::WHITE);
    l.boxes.push_back({k.x - 2, k.y - 2, k.x + 2, k.y + 2});
  }
  for (const Mark &k : got)
    l.label(k.x, k.y, std::to_string(k.year), marks::LABEL, marks::WHITE, 0.85f);
}

// A bar metres long, its right end at x1 or its left at x1 when left
inline void scale(marks::Layer &l, float x1, float y, float mpp, float metres, const char *s, bool left) {
  float n = metres / mpp;
  if (left)
    x1 += n;
  l.line({x1 - n, y, x1, y}, 1, marks::WHITE, 0.8f);
  for (float x : {x1 - n, x1})
    l.line({x, y - 2.5f, x, y}, 1, marks::WHITE, 0.8f);
  l.caps(left ? x1 - n : x1, y - 4.5f, s, 0.8f, marks::WHITE, left ? 'l' : 'r');
  l.boxes.push_back({x1 - n - 2, y - 12, x1 + 2, y + 2});
}

// The latest's colour and what it means, right-aligned at x1 or left-aligned there when left
inline void legend(marks::Layer &l, float x1, float y, const std::string &s, bool left) {
  if (left) {
    l.line({x1, y, x1 + 11, y}, 1.8f, marks::CYAN, 1, 0.35f);
    l.caps(x1 + 16, y + 2.3f, s, 0.75f);
    return;
  }
  float w = l.caps(x1, y + 2.3f, s, 0.75f, marks::WHITE, 'r');
  l.line({x1 - w - 16, y, x1 - w - 5, y}, 1.8f, marks::CYAN, 1, 0.35f);
  l.boxes.push_back({x1 - w - 18, y - 6, x1 + 2, y + 5});
}

inline marks::Layer layer_of(const Map &m) {
  const Rover &r = *m.r;
  marks::Layer l;
  if (r.column) {
    l.scrim('l', 150, 0.9f);
    l.boxes.push_back({0, 0, 112, marks::H});
  } else {
    l.scrim('t', 46, 0.7f);
    l.scrim('b', 82, 0.88f);
    l.boxes.push_back({0, 0, 150, 36});
    l.boxes.push_back({0, marks::H - 48, marks::W, marks::H});
  }
  std::vector<float> all, latest;
  all.reserve(m.track.size() * 2);
  for (const Point &p : m.track) {
    all.insert(all.end(), {p.fx(), p.fy()});
    if (p.sol + r.recent >= m.last.sol)
      latest.insert(latest.end(), {p.fx(), p.fy()});
  }
  l.line(std::move(all), 1.3f, marks::WARM, 0.8f);
  if (latest.size() >= 4)
    l.line(std::move(latest), 1.8f, marks::CYAN, 1, 0.35f);
  const Point &start = m.track.front(), &end = m.track.back();
  now_mark(l, end.fx(), end.fy(), m.last.yaw);
  l.ring(start.fx(), start.fy(), 2.6f, marks::WARM, 1.2f);
  l.boxes.push_back({start.fx() - 3, start.fy() - 3, start.fx() + 3, start.fy() + 3});
  l.label(start.fx(), start.fy(), "Landed " + std::to_string(r.year), marks::LABEL, marks::WARM, 0.9f, 0, true);
  years(l, m, start, end);

  char km[16], up[16];
  snprintf(km, sizeof km, "%.2f", m.last.dist / 1000);
  snprintf(up, sizeof up, "%.0f", m.last.elev - m.first.elev);
  std::string driven = "Driven since " + std::to_string(r.year), last = day_of(sol_middle(r, m.last.sol)),
              sol = "Sol " + std::to_string(m.last.sol), recent = "Last " + std::to_string(r.recent) + " sols";
  if (r.column) {
    l.text(12, 22, r.name, marks::TITLE);
    l.text(12, 34, std::string(r.where) + ", Mars", marks::SMALL, marks::WHITE, 0.7f);
    l.stat(12, 82, km, "km", driven, marks::HUGE);
    l.stat(12, 128, up, "m", r.climb, marks::HUGE);
    l.stat(12, 174, last, "", "Last drive, " + sol, marks::BIG);
    legend(l, 12, 204, recent, true);
    scale(l, 12, 226, r.mpp, 2000, "2 km", true);
  } else {
    l.text(12, 20, r.name, marks::TITLE);
    l.text(12, 32, std::string(r.where) + ", Mars", marks::SMALL, marks::WHITE, 0.7f);
    legend(l, marks::W - 12, 15, recent, false);
    scale(l, marks::W - 12, 33, r.mpp, 2000, "2 km", false);
    l.stat(12, marks::H - 26, km, "km", driven, marks::HUGE);
    l.stat(170, marks::H - 26, up, "m", r.climb, marks::BIG);
    l.stat(marks::W - 12, marks::H - 26, last, "", sol, marks::BIG, marks::SMALL, marks::WHITE, 'r');
  }
  return l;
}

inline bool Map::got(fetch::Request &a) {
  List &q = static_cast<List &>(a);
  if (q.track.size() < 2)
    return false;
  Point end = point_of(*r, q.last.lon, q.last.lat, q.last.sol);
  if (end.x != q.track.back().x || end.y != q.track.back().y)
    q.track.push_back(end);  // where it is, though within a pixel of the last kept
  char s[64];
  snprintf(s, sizeof s, "Sol %d, %.2f km", q.last.sol, q.last.dist / 1000);
  say(s);
  if (q.last.sol == last.sol && q.track.size() == track.size())
    return true;
  ESP_LOGI("space", "%s: sol %d, %u points", r->name, q.last.sol, (unsigned) q.track.size());
  track = std::move(q.track);
  first = q.first;
  last = q.last;
  marks::set(r->id, layer_of(*this));
  return true;
}

// From rover.yaml's on_boot. rover: an id in ROVERS, also the picture's name, and map its map. every: how often to
// read its waypoints, status: its sensor. encoding and quality as picture.yaml has them.
template<typename S>
void setup(const char *rover, const Mosaic &map, const char *every, const char *encoding, const char *quality,
           S &&status) {
  Map *m = map_of(rover);
  if (!m) {
    ESP_LOGE("space", "No rover called '%s'", rover);
    return;
  }
  m->r = &ROVERS[m - maps];
  m->what = "track";
  m->server = "NASA";
  m->timeout_ms = 30000;
  ask_every(*m, every, status);
  picture::Source s;
  s.size = [](int &w, int &h) {
    w = marks::W;
    h = marks::H;
    return true;
  };
  s.pixel = [&map](int x, int y) {
    uint8_t two = map.px[(y * marks::W + x) / 2];
    return map.rgb[x & 1 ? two & 15 : two >> 4];
  };
  s.coding(encoding, quality);
  picture::add(rover, s);
  marks::on(rover);
}

}  // namespace flexit::panel::space::rover
