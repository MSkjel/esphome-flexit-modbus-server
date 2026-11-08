// The weather picture's animations, one show at a time: what fits the weather, the light and the day, like gulls
// over the cabin, rain from under the clouds or the lights on the Christmas spruce. met_sprites.h draws them in
// their frames, and each copy runs a script on the panel, see overlay_script.h: how it moves, what frames it plays
// and when it goes. Now and then something comes by. The picture leaves out what a show draws itself. The shows are
// in met_live_*.h, met_live_shows.h picks one, met.h runs it, see the Panel Animation Weather switch in met.yaml.
#pragma once
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../panel/overlay.h"
#include "met_raster.h"
#include "met_sprites.h"

namespace flexit::panel::met::live {

// ms of a step: the panel moves things in them, so a wave jumps back to its start in one, and loop() runs once a
// step, so a timer goes off up to one late
inline constexpr int STEP = 50;

struct Timer {
  uint32_t at;                   // millis()
  std::function<uint32_t()> fn;  // the ms it takes, for sometimes()
  uint32_t mean;                 // sometimes()'s, 0 for once
  bool rare = false;             // the roll for a rare encounter, see met_live_rare.h
};

// The show that runs and what it made
struct Running {
  bool on;                           // the switch
  int show = -1;                     // met_live_shows.h's SHOWS, -1 none
  uint32_t where;                    // the place, the landscape and what the show is made of, see apply()
  bool flag;                         // the flag is up with it
  int8_t flown;                      // the way the wind blew it when it went up
  int16_t sun_x;                     // the sun's now, -1 down
  float light;                       // the scene's now, see in_light()
  std::map<std::string, float> lit;  // the sprites in it, and the least light each is in
  std::vector<Timer> timers;
  std::map<std::string, Frames> sprites;   // the show's, by name
  std::vector<std::string> names;          // the panel animations it made
  std::map<std::string, uint32_t> comers;  // the sprites of visits and encounters, and the millis() they go by
  Rng rng{1};
  std::vector<uint8_t> px;  // the frame frame_pixel() drew last, empty without the RAM for it
  Colours cols;             // its colours
  const Frames *drawn;
  int drawn_f;
  std::string rare;                    // the sprite of the rare encounter on, see met_live_rare.h
  std::function<uint32_t(int)> visit;  // the show's visitors, see visitors()
  int visits;
};
inline Running R;

// ---- what the shows use

// a or more, below b
inline float rnd(float a, float b) { return a + (b - a) * R.rng.f(); }
inline bool chance(float p) { return rnd(0, 1) < p; }
// a to b minutes at random, or m minutes, in ms
inline uint32_t minutes(float a, float b) { return (uint32_t) (rnd(a, b) * 60000); }
constexpr uint32_t minutes(float m) { return (uint32_t) (m * 60000); }

inline std::string key(const std::string &name) { return "w_" + name; }

inline void free_px() {
  std::vector<uint8_t>().swap(R.px);
  R.drawn = nullptr;
}

// Pixel x, y of sp's frame f as 0xAARRGGBB, for overlay.h. The frame is drawn whole and kept, a byte a pixel, up
// to 7 KB for the rain. Without the RAM it stays empty: a failed allocation would restart the ESP.
inline uint32_t frame_pixel(const Frames *sp, int f, int x, int y) {
  if (R.drawn != sp || R.drawn_f != f) {
    size_t n = (size_t) sp->w * sp->h;
    if (n > R.px.capacity())
      free_px();  // before the bigger one is taken
    R.drawn = sp;
    R.drawn_f = f;
    if (R.px.empty() && !picture::has_ram(n, n)) {
      if (!f) {
        ESP_LOGW("met", "No RAM to draw the frames of a %dx%d weather animation", sp->w, sp->h);
      }
      return 0;
    }
    R.px.assign(n, 0);
    R.cols = Colours();
    sp->draw(f, Canvas{sp->w, sp->h, R.px.data(), &R.cols});
  }
  uint8_t i = R.px.empty() ? 0 : R.px[(size_t) y * sp->w + x];
  return i ? 0xFF000000u | R.cols.rgb[i] : 0;
}

// A sprite the show made itself, like a cloud from the picture's. Before it's placed.
inline void add(const std::string &name, Frames sp) { R.sprites[name] = std::move(sp); }

// name's sprite, the show's own or one from met_sprites.h made the first time. Null if there's none.
inline const Frames *frames_of(const std::string &name) {
  auto it = R.sprites.find(name);
  if (it == R.sprites.end()) {
    Frames sp;
    if (!make_sprite(name, sp)) {
      ESP_LOGW("met", "No weather animation called '%s'", name.c_str());
      return nullptr;
    }
    it = R.sprites.emplace(name, std::move(sp)).first;
  }
  return &it->second;
}
// ms name's frames from to to - 1 take, to the last without to
inline int ms_of(const std::string &name, int from = 0, int to = INT_MAX) {
  const Frames *sp = frames_of(name);
  return sp ? sp->ms(from, to) : 0;
}
// ms of a strip's clip, see make_sprite()
inline int ms_of(const std::string &name, const char *clip) {
  const Frames *sp = frames_of(name);
  if (!sp || !sp->clips.count(clip))
    return 0;
  const Frames::Clip &c = sp->clips.at(clip);
  return sp->ms(c.first, c.last + 1);
}
// Where name, its top at y, comes out of the forest at e from and goes back into, see Edge::out()
inline int out_of(const Edge &e, const std::string &name, int y) {
  const Frames *sp = frames_of(name);
  return sp ? e.out(sp->w, sp->h, y + sp->h - 1) : -2;
}

// The colour the scene's goes toward as the light goes, see met::mix()
inline constexpr uint32_t NIGHT = 0x223344;

// name's sprite as a panel animation, a strip's clips for its scripts. Its light goes toward NIGHT, but for LAMP.
inline bool ready(const std::string &name) {
  std::string k = key(name);
  if (overlay::sources.count(k))
    return true;
  const Frames *sp = frames_of(name);
  if (!sp)
    return false;
  auto pixel = [sp](int f, int x, int y) { return frame_pixel(sp, f, x, y); };
  overlay::Source s{sp->w, sp->h, sp->count(), 100, 0, 0, 1, pixel};
  s.delays = sp->delays;
  s.fade_ms = 300;
  s.glows = {LAMP};
  s.shade = NIGHT;
  for (auto &[n, c] : sp->clips)
    s.clips[n] = {c.first, c.last, c.w, c.h};
  overlay::add(k, std::move(s));
  R.names.push_back(k);
  return true;
}

// Takes name's animation off the panel, to be made again
inline void unload(const std::string &name) {
  std::string k = key(name);
  overlay::clear(k);
  overlay::sources.erase(k);
  R.names.erase(std::remove(R.names.begin(), R.names.end(), k), R.names.end());
  R.sprites.erase(name);
  R.comers.erase(name);
  R.lit.erase(name);
  if (R.rare == name)
    R.rare.clear();
  R.drawn = nullptr;
}

// Sprite n's copies from now on in the light there is, 1 day to 0 night, the way the picture's colours go, but never
// darker than least, so it still shows: under the stars by default
inline void in_light(const std::string &n, float least = 0.35f) { R.lit[n] = least; }
// What a lit sprite's light is now, percent
inline int light_of(float least) { return (int) lroundf(std::max(R.light, least) * 100); }

// The lit sprites' copies on the panel glide to the light there is now
inline void relight() {
  for (auto &[n, least] : R.lit)
    if (overlay::Overlay *a = overlay::find(key(n)))
      for (size_t i = 0; i < a->copies.size(); i++)
        if (a->copies[i].used)
          overlay::glide(key(n), (int) i, "light", light_of(least), 3000, "both");
}

// How a copy is placed
struct How {
  bool left = false;                // mirrored, facing left
  int fade = -1;                    // ms it fades in and out, -1 the usual 300
  uint8_t depth = OVERLAY_NEAREST;  // a Depth, behind the scene's parts nearer than it
};
// Copy i of sprite n at x, y doing what script says, with numbers by name in k, see overlay_script.h, in the light
// there is if it's lit. With a frame of its own if the script plays frames. o.left mirrors it with its left at x all
// the same.
inline bool place(const std::string &n, int i, int x, int y, const std::string &script = "",
                  const overlay::Consts &k = {}, How o = {}) {
  if (!ready(n))
    return false;
  std::string error, first = o.left ? "flip x\n" : "";
  if (auto l = R.lit.find(n); l != R.lit.end())
    first += "glide light " + std::to_string(light_of(l->second)) + "\n";
  overlay::Program p = overlay::compile(first + script, error, overlay::sources[key(n)].clips, k);
  if (!error.empty()) {
    ESP_LOGW("met", "Weather animation %s's script, %s", n.c_str(), error.c_str());
    return false;
  }
  return overlay::place(key(n), i, x, y, std::move(p), false, o.fade, o.depth);
}
// Copy i of n, placed, runs script from now on
inline void script(const std::string &n, int i, const std::string &text, const overlay::Consts &k = {}) {
  if (overlay::copy_for(key(n), i).second)
    overlay::script(key(n), i, text, k);
}
inline void remove(const std::string &n, int i, int fade = 300) { overlay::remove(key(n), i, fade); }
// name's animation and its copies go, faded out
inline void clear(const std::string &n) { overlay::clear(key(n)); }

// ---- script text

inline std::string num(int v) { return std::to_string(v); }

// A leg of a path: to x, y in ms
struct Leg {
  int x, y, ms;
  const char *ease = "none";
};
// Moves along legs one after another, each waited for
inline std::string path(std::initializer_list<Leg> legs) {
  std::string t;
  for (const Leg &l : legs)
    t += "move " + num(l.x) + ", " + num(l.y) + " over " + num(l.ms) + " ease " + l.ease + "; wait " + num(l.ms) + "\n";
  return t;
}
inline int path_ms(std::initializer_list<Leg> legs) {
  int t = 0;
  for (const Leg &l : legs)
    t += l.ms;
  return t;
}

// The near hill's top every 4 px, for walks along it after the scene's gone
struct NearHill {
  uint8_t y[W / 4 + 1] = {};
  NearHill() = default;
  explicit NearHill(const Scene &s) {
    for (int i = 0; i <= W / 4; i++)
      y[i] = s.land->hill[2][std::min(W - 1, i * 4)];
  }
  int at(int x) const {
    x = std::clamp(x, 0, W - 1);
    return (y[x / 4] * (4 - x % 4) + y[x / 4 + 1] * (x % 4) + 2) / 4;
  }
};
// The row of the near hill's lowest point over columns l to r - 1
inline int hill_low(const Scene &s, int l, int r) {
  int y = 0;
  for (int x = l; x < r; x++)
    y = std::max<int>(y, s.land->hill[2][x]);
  return y;
}

// A walk along a hill from x0 to x1 in n legs about step px long, its top dy above the hill's top under its middle,
// mid px in, at its points 0 to n
struct Walk {
  int x0, x1, n;
  std::vector<int> y, ms;  // its top at each point, and the ms of the leg to it, quicker downhill with a downhill
  bool steep;              // with a downhill, so its legs take their own ms
  int x(int k) const { return x0 + (x1 - x0) * k / n; }
  int all() const {
    int t = 0;
    for (int m : ms)
      t += m;
    return t;
  }
  // ms of a leg at speed px/s
  int leg(float speed) const { return (int) ((float) abs(x1 - x0) * 1000 / speed / (float) n); }
  // The script's loop at label: from point k a leg at a time to point b, d 1 or -1 the way, each leg over t ms or its
  // own if it's steep. Registers k, b, d, t and v, its row.
  std::string loop(const char *label) const {
    std::string ys, ts;
    for (int k = 0; k <= n; k++)
      ys += ", " + num(y[k]);
    for (int k = 0; k <= n; k++)
      ts += ", " + num(ms[std::clamp(k - 1, 0, n - 1)]);
    return std::string(label) + ":\n  k = k + d\n  v = pick(k" + ys + ")\n" +
           (steep ? "  t = pick(k" + ts + ")\n" : "") + "  move " + num(x0) + " + " + num(x1 - x0) + " * k / " +
           num(n) + ", v over t\n  wait t\n  if k != b goto " + label + "\n";
  }
};
inline Walk walk(const NearHill &h, int x0, int x1, float speed, int mid, int dy, float downhill = 0, int step = 12) {
  Walk w{x0, x1, std::max(1, abs(x1 - x0) / step), {}, {}, downhill != 0};
  for (int k = 0; k <= w.n; k++)
    w.y.push_back(h.at(w.x(k) + mid) - dy);
  for (int k = 1; k <= w.n; k++) {
    int dx = std::max(1, abs(w.x(k) - w.x(k - 1)));
    float v = speed * std::clamp(1 + downhill * (float) (w.y[k] - w.y[k - 1]) / (float) dx, 0.6f, 1.5f);
    w.ms.push_back((int) ((float) dx * 1000 / v));
  }
  return w;
}

// ---- when things happen

// fn by chance, first after first ms, then about every mean ms after the last one ended, or just once with mean
// 0. It returns the ms it takes.
inline void sometimes(uint32_t mean, uint32_t first, std::function<uint32_t()> fn) {
  R.timers.push_back({millis() + first, std::move(fn), mean});
}
// fn in ms, once
template<class F> void later(uint32_t ms, F fn) {
  sometimes(0, ms, [fn] {
    fn();
    return 0u;
  });
}

// The longest a visitor's sprite takes to get to the panel, the link busy
inline constexpr uint32_t ON_PANEL = 5000;

// fn, a visit or an encounter, which places what comes and returns the ms it takes, and what it newly placed goes
// when its scripts end it, or by then and wait. The ms with wait, 0 if it doesn't fit.
inline uint32_t come(const std::function<uint32_t()> &fn, uint32_t wait) {
  std::vector<std::string> had = R.names;
  uint32_t ms = fn();
  for (const std::string &k : R.names)
    if (std::find(had.begin(), had.end(), k) == had.end())
      R.comers[k.substr(2)] = millis() + ms + wait;
  return ms ? ms + wait : 0;
}
// A script ended the last copy of animation k: a visitor's sprite goes
inline void emptied(const std::string &k) {
  if (k.rfind("w_", 0) == 0 && R.comers.count(k.substr(2)))
    unload(k.substr(2));
}

// The show's visitors, one at a time so the panel has room for them: about every mean ms one of n at random. fn(k)
// brings the k-th and returns the ms it takes, 0 if it doesn't fit the picture.
inline void visitors(uint32_t mean, uint32_t first, int n, std::function<uint32_t(int)> fn) {
  R.visit = [fn](int k) { return come([&] { return fn(k); }, ON_PANEL); };
  R.visits = n;
  sometimes(mean, first, [n] {
    uint32_t ms = 0;
    for (int k = (int) rnd(0, (float) n), tries = 0; !ms && tries < n; k = (k + 1) % n, tries++)
      ms = R.visit(k);
    return ms;
  });
}

// ---- the sky

// The top row of what stands in front of the sky in each column: the land, the mountains' far ridge with far, the
// sea's horizon, the trees, the cabin, the flagpole and its flag, the lighthouse, and the town's buildings with
// what's on their roofs
inline void skyline(const Scene &s, uint8_t top[W], bool far = true) {
  for (int x = 0; x < W; x++) {
    int y = std::min({s.land->hill[0][x], s.land->hill[1][x], s.land->hill[2][x]});
    if (s.ridge && far)
      y = std::min<int>(y, s.land->ridge[x]);
    if (s.sea)
      y = std::min<int>(y, s.sea_top);
    top[x] = (uint8_t) y;
  }
  auto up = [&](int l, int r, int y) {  // up to row y over columns l to r
    for (int x = std::max(0, l); x <= std::min(W - 1, r); x++)
      top[x] = (uint8_t) std::clamp<int>(y, 0, top[x]);
  };
  for (int i = 0; i < s.ntrees; i++) {
    const Tree &t = s.tree[i];
    int half = (t.base - t.top) / 2 + 2;
    up(t.x - half, t.x + half, t.top);
  }
  if (s.cabin)
    up(s.cabin_x - 17, s.cabin_x + 17, s.cabin_y - 32);
  if (s.pole_x >= 0)
    up(s.pole_x - PENNANT_LEFT, s.pole_x + PENNANT_RIGHT, s.pole_top);
  if (s.lighthouse)
    up(LH_X - 6, LH_X + 6, s.lh_y - 34);
  static constexpr int8_t ROOF[] = {0, 5, 12, 14};  // a flat roof, a box on it, an antenna, a spire
  for (int i = 0; i < s.nbld; i++)
    up(s.bld[i].x, s.bld[i].x + s.bld[i].w - 1, s.bld[i].top - ROOF[s.bld[i].kind & 3]);
}

// The moon's whole disc in the sky: under the forecast, away from the sides it would wrap round, and above
// skyline() and the fog
inline bool moon_clear(const Scene &s) {
  int l = s.moon_x - MOON_R, r = s.moon_x + MOON_R, below = s.moon_y + MOON_R;
  if (!s.moon || l < 0 || r >= W || s.moon_y - MOON_R < (s.overlay ? OV_H : 0))
    return false;
  uint8_t top[W];
  skyline(s, top);
  for (int x = l; x <= r; x++)
    if (std::min({top[x], s.fog_top[0][x], s.fog_top[1][x]}) <= below)
      return false;
  return true;
}

// Whether pixel c on row y is the night or day sky, or a star in it
inline bool sky_px(const Scene &s, uint8_t c, int y) {
  return c == s.ix[C_SKY + band_of(y)] || c == s.ix[C_DIM] || c == s.ix[C_STAR];
}

// The row at the left edge for a w x h sprite to cross the sky to the right edge from, its top dy lower there, with
// nothing but sky and stars on its way: no land, trees, roofs, moon, clouds or forecast. From lo to hi, -1 if none
// of a few tries is clear.
inline int sky_line(const Scene &s, int w, int h, int lo, int hi, int dy) {
  int y0[6];
  bool clear[6];
  for (int i = 0; i < 6; i++) {
    y0[i] = (int) rnd((float) lo, (float) hi);
    clear[i] = true;
  }
  uint8_t px[W];
  for (int y = std::max(0, std::min(lo, lo + dy) - 1); y <= std::min(H - 1, std::max(hi, hi + dy) + h); y++) {
    row(s, y, px);
    for (int x = 0; x < W; x++) {
      for (int i = 0; i < 6 && !sky_px(s, px[x], y); i++) {
        int top = y0[i] + dy * std::max(0, x - w) / W;  // the sprite over this column, a row either side
        clear[i] = clear[i] && (y < top - 1 || y > top + h);
      }
    }
  }
  for (int i = 0; i < 6; i++)
    if (clear[i])
      return y0[i];
  return -1;
}

// A witch on her broomstick from off one side across to the other at 40 px/s, through the moon at moon_x, moon_y,
// slowing to 15 px/s over the 60 px across it, or anywhere in the sky with moon_x -1. No higher than row top.
struct Ride {
  int x[4], y[4];  // off one side, the moon's near and far side, off the other side
  int ms[3];       // each leg's
  bool left() const { return x[3] < x[0]; }
  int all() const { return ms[0] + ms[1] + ms[2]; }
  // Its script from where it starts: along the legs, and gone
  std::string text() const {
    std::string t;
    for (int i = 0; i < 3; i++)
      t += "move " + num(x[i + 1]) + ", " + num(y[i + 1]) + " over " + num(ms[i]) + "; wait " + num(ms[i]) + "\n";
    return t + "end\n";
  }
};
inline Ride witch_ride(int moon_x, int moon_y, int top) {
  bool right = chance(0.5f), moon = moon_x >= 0 && moon_y - 6 >= top;
  int mx = moon ? moon_x - 10 : (int) rnd(60, W - 80), my = moon ? moon_y - 6 : (int) rnd(top + 10, HORIZON - 50);
  int from = right ? -22 : W + 2, to = right ? W + 2 : -22, slow = moon ? 30 : 0, dir = right ? 1 : -1;
  // its slope, as much as 0.15 either way while neither end leaves the sky
  float lo = -0.15f, hi = 0.15f;
  for (int x : {from, to}) {
    float d = (float) (x - mx), a = (float) (top - my) / d, b = (float) (HORIZON - 12 - my) / d;
    lo = std::max(lo, std::min(a, b));
    hi = std::min(hi, std::max(a, b));
  }
  float k = rnd(lo, hi);
  Ride r;
  int x[4] = {from, mx - dir * slow, mx + dir * slow, to};
  for (int i = 0; i < 4; i++) {
    r.x[i] = right ? std::clamp(x[i], from, to) : std::clamp(x[i], to, from);
    r.y[i] = my + (int) lroundf(k * (float) (r.x[i] - mx));
  }
  for (int i = 0; i < 3; i++)
    r.ms[i] = abs(r.x[i + 1] - r.x[i]) * 1000 / (i == 1 ? 15 : 40);
  return r;
}

// ---- shared by the shows

// A full gale or storm gusts, wind that takes what isn't tied down. The rain and snow leave room then for something
// blowing by, see met_live_rare.h.
inline bool gale(const At &a) { return a.wind >= 17 || a.gust >= 25; }
// The most of the pool a rare encounter's sprite takes. The firmware's sim/esp_yaml/live_rare.inc checks it.
inline constexpr int RARE_BYTES = 1200;

// Copy 0 of n, drawn facing right, from x0, y0 off one edge across to x1, y1 off the other in ms, facing the way it
// goes, doing what also says on the way, then it's gone. The ms.
inline uint32_t across(const char *n, int x0, int y0, int x1, int y1, int ms, const std::string &also = "",
                       How o = {}) {
  o.left = x1 < x0;
  place(n, 0, x0, y0, also + "\n" + path({{x1, y1, ms}}) + "end", {}, o);
  return (uint32_t) ms;
}

// Copy 0 of n soars in from off the side away from the sun, circles rx by ry px round row y there a while and goes
// off higher up, facing the way it flies. The ms it takes.
inline uint32_t soar(const char *n, int y, int rx, int ry) {
  bool left = R.sun_x > W / 2;  // where it comes in from
  place(n, 0, left ? -40 : W + 20, y - 16, R"(
    face 0 mirror level
    wave x -rx, rx, 12000, 12000 jitter 20 slow both
    wave y -ry, ry, 8000, 8000 jitter 20 slow both
    move x1, y over 12000 ease out
    wait 40000
    move x2, y - 28 over 14000 ease in
    wait 16000
    end
  )",
        {{"rx", rx}, {"ry", ry}, {"y", y}, {"x1", left ? 60 : 225}, {"x2", left ? -60 : W + 40}});
  return 56000;
}

// Gulls back and forth across the picture and round out of sight, on gusts of slow waves, facing the way they fly.
// Each from x, y at speed s0..s1, between rows top and bottom, the others beating in their own time.
struct Gull {
  int x, y;
  float s0, s1;
  int top, bottom;
};
inline void flock(std::initializer_list<Gull> gulls) {
  int i = 0;
  for (const Gull &g : gulls) {
    place("gull", i, g.x, g.y, std::string(R"(
      area -24, top, 344, bottom bounce
      wave speed 70, 130, 4000, 4000 hold 2000, 2000 jitter 50 random slow both
      face 0 mirror level
      wander s0, s1, 0, 20, 3000, 9000, 0, 0
    )") + (i ? "frame " + num(5 * i % 8) : ""),
          {{"top", g.top}, {"bottom", g.bottom}, {"s0", g.s0}, {"s1", g.s1}});
    i++;
  }
}

// Puffs of name, after one another so they overlap into a plume, with their middle at x, y: each rises rise px and
// drifts drift px downwind for exactly as long as its frames last, growing and thinning as it goes, then starts
// again from there. Thinner higher up too. Each but the first hidden till it starts.
inline void chimney(const char *name, int puffs, int x, int y, int rise, int drift, int wdir) {
  const Frames *sp = frames_of(name);
  if (!sp)
    return;
  int ms = sp->ms() - STEP;  // a step less for the waves' jump back keeps them in step with the frames
  int left = wdir < 0 ? drift + 20 : 20, right = wdir < 0 ? 30 : drift + 30, x0 = x - sp->w / 2;
  for (int i = 0; i < puffs; i++)
    place(name, i, x0, y - sp->h / 2,
          std::string(i ? "glide opacity 0\n" : "") + R"(
            area l, t, r, b screen
            map opacity 100, 100, 0, 100
            wait after
            clip 0 to last
            wave y -rise, 0, 0, period
          )" + (wdir < 0 ? "wave x -drift, 0, 0, period slow high\n" : "wave x 0, drift, period, 0 slow low\n") +
              (i ? "glide opacity 100 over 300\n" : ""),
          {{"l", x0 - left},
           {"t", y - rise - 3},
           {"r", x0 + right},
           {"b", y + 2},
           {"after", ms * i / puffs},
           {"last", sp->count() - 1},
           {"rise", rise},
           {"drift", drift},
           {"period", ms}});
}

// Some of the picture's bright stars in full view sparkle now and then, each from another of the twinkle's dark
// whiles, so they don't line up: the big ones first, as far from the others as they can be. Not near the moon, nor
// over the forecast or skip.
inline void twinkle_stars(const Scene &s, Box skip = {}) {
  uint8_t px[W];
  int pick[6], n = 0;
  bool seen[std::size(STARS)] = {};
  for (int i = 0; i < s.nbright; i++) {
    int x = s.bright_x[i], y = s.bright_y[i];
    row(s, y, px);
    seen[i] = x > 2 && x < W - 3 && px[x] == s.ix[C_STAR] && px[x + 1] == s.ix[C_STAR] &&
              (!s.moon || abs(x - s.moon_x) > 20 || abs(y - s.moon_y) > 20) && !(s.overlay && y < OV_H + 2) &&
              !(skip.w && x > skip.x - 3 && x < skip.x + skip.w + 2 && y > skip.y - 3 && y < skip.y + skip.h + 2);
  }
  for (; n < 6; n++) {
    int best = -1, far = -1;
    for (int i = 0; i < s.nbright; i++) {
      int d = 9999;
      for (int j = 0; j < n && seen[i]; j++)
        d = std::min(d, abs(s.bright_x[i] - s.bright_x[pick[j]]) + abs(s.bright_y[i] - s.bright_y[pick[j]]));
      d += s.bright_big[i] * 60;
      if (seen[i] && d > far)
        far = d, best = i;
    }
    if (best < 0)
      break;
    seen[best] = false;
    pick[n] = best;
  }
  static constexpr int DARK[] = {0, 9, 4, 13, 8, 17};  // its frames that start a dark while
  for (int i = 0; i < 6; i++)
    if (i < n)
      place("twinkle", i, s.bright_x[pick[i]] - 2, s.bright_y[pick[i]] - 2, "frame " + num(DARK[i]));
    else
      remove("twinkle", i);
}

// Every animation of the show goes, faded out
inline void stop() {
  for (auto &k : R.names) {
    overlay::clear(k);
    overlay::sources.erase(k);
  }
  R.names.clear();
  R.sprites.clear();
  R.comers.clear();
  R.lit.clear();
  R.timers.clear();
  free_px();
  R.show = -1;
  R.flag = false;
  R.rare.clear();
  R.visit = nullptr;
  R.visits = 0;
}

// Every 50 ms or so: what's due. shown: the weather is the panel's picture.
inline void loop(bool shown) {
  if (R.show < 0)
    return;
  if (!R.on || !shown) {
    stop();
    return;
  }
  if (overlay::W.phase != overlay::COUNT && overlay::W.phase != overlay::MAKE)
    free_px();  // overlay.h only reads it while it counts or makes frames
  uint32_t now = millis();
  for (auto it = R.comers.begin(); it != R.comers.end();) {
    std::string n = (it++)->first;
    if ((int32_t) (now - R.comers[n]) >= 0)
      unload(n);  // still there, its sprite never got to the panel
  }
  for (size_t i = 0; i < R.timers.size(); i++) {
    if ((int32_t) (now - R.timers[i].at) < 0)
      continue;
    std::function<uint32_t()> fn = R.timers[i].fn;  // it may add timers, or stop the show
    uint32_t mean = R.timers[i].mean;
    float k = mean ? rnd(0.4f, 1.6f) : 0;
    if (!mean)
      R.timers.erase(R.timers.begin() + (long) i--);
    int show = R.show;
    uint32_t ms = fn();
    if (R.show != show)
      return;
    if (mean)
      R.timers[i].at = now + ms + (uint32_t) (mean * k);
  }
}

}  // namespace flexit::panel::met::live
