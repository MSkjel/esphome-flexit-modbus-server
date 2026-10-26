// The weather picture's animations, one show at a time: what fits the weather, the light and the day, like gulls
// over the cabin, rain from under the clouds or the lights on the Christmas spruce. met_sprites.h draws them in
// their frames, the panel moves them on its own, and now and then something happens by chance. The picture leaves
// out what a show draws itself. The shows are in met_live_*.h, met_live_shows.h picks one, met.h runs it, see the
// Panel Animation Weather switch in met.yaml.
#pragma once
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../panel/animation.h"
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
};

// The show that runs and what it made
struct Running {
  bool on;         // the switch
  int show = -1;   // met_live_shows.h's SHOWS, -1 none
  uint32_t where;  // the place, the landscape and what the show is made of, see apply()
  bool flag;       // the flag is up with it
  int8_t flown;    // the way the wind blew it when it went up
  int16_t sun_x;   // the sun's now, -1 down
  std::vector<Timer> timers;
  std::map<std::string, Frames> sprites;  // the show's, by name
  std::vector<std::string> names;         // the panel animations it made
  Rng rng{1};
  std::vector<uint32_t> px;  // the frame frame_pixel() drew last, empty without the RAM for it
  const Frames *drawn;
  int drawn_f;
  int8_t flag_light;  // the light the flag was dimmed to, in tenths
};
inline Running R;

// ---- what the shows use, the panel's animation actions by sprite name

// a or more, below b
inline float rnd(float a, float b) { return a + (b - a) * R.rng.f(); }
inline bool chance(float p) { return rnd(0, 1) < p; }
// a to b minutes at random, or m minutes, in ms
inline uint32_t minutes(float a, float b) { return (uint32_t) (rnd(a, b) * 60000); }
constexpr uint32_t minutes(float m) { return (uint32_t) (m * 60000); }

inline std::string key(const std::string &name) { return "w_" + name; }

inline void free_px() {
  std::vector<uint32_t>().swap(R.px);
  R.drawn = nullptr;
}

// Pixel x, y of sp's frame f, for animation.h. The frame is drawn whole and kept, 4 bytes a pixel, up to 28 KB
// for the rain. Without the RAM it stays empty: a failed allocation would restart the ESP.
inline uint32_t frame_pixel(const Frames *sp, int f, int x, int y) {
  if (R.drawn != sp || R.drawn_f != f) {
    size_t n = (size_t) sp->w * sp->h;
    if (n > R.px.capacity())
      free_px();  // before the bigger one is taken
    R.drawn = sp;
    R.drawn_f = f;
    if (R.px.empty() && !picture::has_ram(n * sizeof(uint32_t), n * sizeof(uint32_t))) {
      if (!f) {
        ESP_LOGW("met", "No RAM to draw the frames of a %dx%d weather animation", sp->w, sp->h);
      }
      return 0;
    }
    R.px.assign(n, 0);
    sp->draw(f, Canvas{sp->w, sp->h, R.px.data()});
  }
  return R.px.empty() ? 0 : R.px[(size_t) y * sp->w + x];
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

// name's sprite as a panel animation
inline bool ready(const std::string &name) {
  std::string k = key(name);
  if (anim::sources.count(k))
    return true;
  const Frames *sp = frames_of(name);
  if (!sp)
    return false;
  auto pixel = [sp](int f, int x, int y) { return frame_pixel(sp, f, x, y); };
  anim::Source s{sp->w, sp->h, sp->count(), 100, 0, 0, 1, pixel};
  s.delays = sp->delays;
  s.fade_ms = 300;
  anim::add(k, std::move(s));
  R.names.push_back(k);
  return true;
}

// Takes name's animation off the panel, to be made again
inline void unload(const std::string &name) {
  std::string k = key(name);
  anim::clear(k);
  anim::sources.erase(k);
  R.names.erase(std::remove(R.names.begin(), R.names.end(), k), R.names.end());
  R.sprites.erase(name);
  R.drawn = nullptr;
}

// sp in the light there is, 1 day to 0 night, the way the picture's colours go
inline Frames dimmed(Frames sp, float light) {
  sp.draw = [draw = sp.draw, light](int f, const Canvas &c) {
    draw(f, c);
    for (int i = 0; i < c.w * c.h; i++)
      if (c.px[i])
        c.px[i] = 0xFF000000u | met::mix(0x223344, c.px[i] & 0xFFFFFF, light);
  };
  return sp;
}

// How a copy is placed
struct How {
  bool own = false;  // a frame of its own
  int start = 0;     // frames in, with one
  int fade = -1;     // ms it fades in, -1 the usual 300
};
inline void place(const std::string &n, int i, int x, int y, How o = {}) {
  if (ready(n))
    anim::place(key(n), i, x, y, o.own, o.start, o.fade);
}
inline void flip(const std::string &n, int i, bool h, bool v) { anim::flip(key(n), i, h, v); }
// The panel pads a sprite to an even width, so a mirrored odd-width one moves a column. x that makes up for it.
inline int flip_x(const std::string &n, int x, bool flipped) {
  const Frames *sp = frames_of(n);
  return flipped && sp ? x - (sp->w & 1) : x;
}
// Copy i of n at x, y, mirrored if flipped, see flip_x()
inline void place_flipped(const std::string &n, int i, int x, int y, bool flipped, How o = {}) {
  place(n, i, flip_x(n, x, flipped), y, o);
  if (flipped)
    flip(n, i, true, false);
}
inline void remove(const std::string &n, int i, int fade = 300) { anim::remove(key(n), i, fade); }
// Copy i of from swaps for to at x, y, with own a frame of its own start frames in. Neither fades, so it doesn't
// show.
inline void swap(const std::string &from, int i, const std::string &to, int x, int y, bool own = false, int start = 0) {
  remove(from, i, 1);
  place(to, i, x, y, {own, start, 0});
}
// name's animation and its copies go, faded out
inline void clear(const std::string &n) { anim::clear(key(n)); }
// a show's sprite ready on the panel before it's placed, so a swap to it doesn't wait
inline void preload(const std::string &n) {
  if (ready(n))
    anim::slot_for(key(n));
}
inline void move(const std::string &n, int i, int x, int y, int ms, const char *ease = "none") {
  anim::move(key(n), i, x, y, ms, ease);
}
inline void drift(const std::string &n, int i, float vx, float vy) { anim::drift(key(n), i, vx, vy); }
inline void glide(const std::string &n, int i, const char *what, int to, int ms, const char *ease = "none") {
  anim::glide(key(n), i, what, to, ms, ease);
}
inline void area(const std::string &n, int i, int l, int t, int r, int b, const char *ex, const char *ey) {
  anim::area(key(n), i, l, t, r, b, ex, ey);
}
inline void wander(const std::string &n, int i, float s0, float s1, int heading, int spread, int l0, int l1, int r0,
                   int r1) {
  anim::wander(key(n), i, s0, s1, heading, spread, l0, l1, r0, r1);
}
// it faces the way it goes, mirrored from facing, upright
inline void face(const std::string &n, int i, int facing = 0) { anim::face(key(n), i, facing, true, 0, 0, true); }
inline void wave(const std::string &n, int i, const char *what, int lo, int hi, int rise, int fall, int hold_high,
                 int hold_low, int jitter, bool random, const char *slow) {
  anim::wave(key(n), i, what, lo, hi, rise, fall, hold_high, hold_low, jitter, random, slow);
}
inline void map(const std::string &n, int i, const char *what, int l, int r, int t, int b) {
  anim::map(key(n), i, what, l, r, t, b);
}
inline void rounds(const std::string &n, int i, int k) { anim::rounds(key(n), i, k); }

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
// move() and remove() a while later. remove_later() returns at, for an event's ms.
inline void move_later(uint32_t at, const char *n, int i, int x, int y, int ms, const char *ease = "none") {
  later(at, [=] { move(n, i, x, y, ms, ease); });
}
inline uint32_t remove_later(uint32_t at, const char *n, int i, int fade = 300) {
  later(at, [=] { remove(n, i, fade); });
  return at;
}
// Copy 0 of n, drawn facing right, from x0, y0 off one edge across to x1, y1 off the other in ms, facing the way it
// goes, then it's gone. The ms.
inline uint32_t across(const char *n, int x0, int y0, int x1, int y1, uint32_t ms) {
  place(n, 0, x0, y0);
  if (x1 < x0)
    flip(n, 0, true, false);
  move(n, 0, x1, y1, (int) ms);
  return remove_later(ms, n, 0);
}

// ---- shared by the shows

// The near hill's top every 4 px, for walks along it after the scene's gone
struct NearHill {
  uint8_t y[W / 4 + 1];
  explicit NearHill(const Scene &s) {
    for (int i = 0; i <= W / 4; i++)
      y[i] = s.land->hill[2][std::min(W - 1, i * 4)];
  }
  int at(int x) const {
    x = std::clamp(x, 0, W - 1);
    return (y[x / 4] * (4 - x % 4) + y[x / 4 + 1] * (x % 4) + 2) / 4;
  }
};
// The row of the near hill's lowest point over columns l to r - 1, and of its highest
inline int hill_low(const Scene &s, int l, int r) {
  int y = 0;
  for (int x = l; x < r; x++)
    y = std::max<int>(y, s.land->hill[2][x]);
  return y;
}
inline int hill_high(const Scene &s, int l, int r) {
  int y = H;
  for (int x = l; x < r; x++)
    y = std::min<int>(y, s.land->hill[2][x]);
  return y;
}

// Copy i of n from x0 to x1 along the hill, a step every step px or so at speed px/s, and with downhill quicker
// downhill and slower uphill. Its top is dy above the hill's top under its middle, mid px in. Returns the ms it
// takes.
inline uint32_t amble(const char *n, int i, const NearHill &h, int x0, int x1, float speed, int mid, int dy,
                      float downhill = 0, int step = 12) {
  int steps = std::max(1, abs(x1 - x0) / step), px = x0, py = h.at(x0 + mid) - dy;
  uint32_t t = 0;
  for (int k = 1; k <= steps; k++) {
    int x = x0 + (x1 - x0) * k / steps, y = h.at(x + mid) - dy, dx = std::max(1, abs(x - px));
    float v = speed * std::clamp(1 + downhill * (y - py) / dx, 0.6f, 1.5f);
    int ms = (int) (dx * 1000 / v);
    move_later(t, n, i, x, y, ms);
    t += ms;
    px = x;
    py = y;
  }
  return t;
}

// Gulls, drawn at two sizes instead of scaled: near ones lower and quicker, far ones high, slow and hazy, on gusts
// of slow waves, facing the way they fly. Each from x, y at speed s0..s1, between rows top and bottom.
struct Gull {
  int x, y;
  float s0, s1;
  int top, bottom;
};
inline void flock(std::initializer_list<Gull> near, std::initializer_list<Gull> far) {
  int i = 0;
  for (const Gull &g : near) {
    place("gull", i, g.x, g.y, {false, (5 * i) % 8});
    area("gull", i, -20, g.top, 340, g.bottom, "cross", "bounce");
    wave("gull", i, "speed", 70, 130, 4000, 4000, 2000, 2000, 50, true, "both");
    face("gull", i);
    wander("gull", i, g.s0, g.s1, 0, 180, 3000, 9000, 0, 0);
    i++;
  }
  i = 0;
  for (const Gull &g : far) {
    place("gullfar", i, g.x, g.y, {false, i * 3});
    area("gullfar", i, -10, g.top, 330, g.bottom, "cross", "bounce");
    glide("gullfar", i, "opacity", 75, 0);
    face("gullfar", i);
    wander("gullfar", i, g.s0, g.s1, 0, 180, 5000, 12000, 0, 0);
    i++;
  }
}

// Puff i of name with its middle at x, y: it rises rise px and drifts drift px downwind for exactly as long as its
// frames last, growing and thinning as it goes, then starts again from there. Thinner higher up too.
inline void puff(const char *name, int i, int x, int y, int rise, int drift, int wdir) {
  const Frames *sp = frames_of(name);
  if (!sp)
    return;
  int ms = sp->ms() - STEP;  // a step less for the waves' jump back keeps them in step with the frames
  int left = wdir < 0 ? drift + 20 : 20, right = wdir < 0 ? 30 : drift + 30;
  x -= sp->w / 2;
  place(name, i, x, y - sp->h / 2, {true});
  area(name, i, x - left, y - rise - 3, x + right, y + 2, "screen", "screen");
  map(name, i, "opacity", 100, 100, 0, 100);
  wave(name, i, "y", -rise, 0, 0, ms, 0, 0, 0, false, "none");
  if (wdir < 0)
    wave(name, i, "x", -drift, 0, 0, ms, 0, 0, 0, false, "high");
  else
    wave(name, i, "x", 0, drift, ms, 0, 0, 0, 0, false, "low");
}
// puffs of them their frames apart, so they overlap into a plume
inline void chimney(const char *name, int puffs, int x, int y, int rise, int drift, int wdir) {
  puff(name, 0, x, y, rise, drift, wdir);
  for (int i = 1; i < puffs; i++)
    later((uint32_t) ((ms_of(name) - STEP) * i / puffs), [=] { puff(name, i, x, y, rise, drift, wdir); });
}

// Some of the picture's bright stars in full view sparkle now and then, two kinds by turns on times that don't line
// up: the big ones first, as far from the others as they can be. Not near the moon, nor over the forecast or skip.
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
  for (int i = 0; i < 6; i++) {
    const char *name = i % 2 ? "twinkle2" : "twinkle";
    if (i < n)
      place(name, i / 2, s.bright_x[pick[i]] - 2, s.bright_y[pick[i]] - 2, {true, i * 4 % 9});
    else
      remove(name, i / 2);
  }
}

// Every animation of the show goes, faded out
inline void stop() {
  for (auto &k : R.names) {
    anim::clear(k);
    anim::sources.erase(k);
  }
  R.names.clear();
  R.sprites.clear();
  R.timers.clear();
  free_px();
  R.show = -1;
  R.flag = false;
}

// Every 50 ms or so: what's due. shown: the weather is the panel's picture.
inline void loop(bool shown) {
  if (R.show < 0)
    return;
  if (!R.on || !shown) {
    stop();
    return;
  }
  if (anim::W.phase != anim::COUNT && anim::W.phase != anim::MAKE)
    free_px();  // animation.h only reads it while it counts or makes frames
  uint32_t now = millis();
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
