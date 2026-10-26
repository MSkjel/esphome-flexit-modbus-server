// Screensaver animations from animation.yaml. All frames share 16 colours, one transparent if any pixel is, and
// each frame after the first is the deflated rectangle that changed. The panel keeps up to ANIM_SLOTS of them in
// 6 KB and shows each in up to ANIM_PLACES places, the copies of one in step unless a copy gets a frame of its own.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "panel_link.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"

namespace flexit::panel::anim {

inline int16_t clamp16(int v) { return (int16_t) std::clamp(v, -32768, 32767); }
inline uint16_t clamp_u16(int v) { return (uint16_t) std::clamp(v, 0, 65535); }

// x, y is where it goes on the screen, pixel(frame, x, y) gives 0xAARRGGBB, vx, vy a drift in px/s. on_shown sets
// its switch, so it follows what the actions show and clear. Its copies fade in and out over fade_ms.
struct Source {
  int w, h, frames, frame_ms, x, y, scale;
  std::function<uint32_t(int, int, int)> pixel;
  int vx = 0, vy = 0;
  std::function<void(bool)> on_shown = {};
  int fade_ms = 0;
  std::vector<uint16_t> delays = {};  // ms each frame shows, if not all frame_ms
};

// ms frame f of s shows
inline uint32_t frame_ms(const Source &s, int f) { return s.delays.empty() ? s.frame_ms : s.delays[f]; }
inline std::map<std::string, Source> sources;

// frame_ms goes in ANIM_TICK_MS steps, and the panel shows a frame ANIM_FRAME_MIN_MS at least
static constexpr int MAX_FRAME_MS = ANIM_DELAY_MAX * ANIM_TICK_MS;
inline void add(const std::string &name, Source s) {
  int ms = std::clamp(s.frame_ms, ANIM_FRAME_MIN_MS, MAX_FRAME_MS);
  if (ms != s.frame_ms) {
    ESP_LOGW("panel", "Animation %s: frame_ms %d is outside %d..%d, it plays at %d", name.c_str(), s.frame_ms,
             ANIM_FRAME_MIN_MS, MAX_FRAME_MS, ms);
  }
  s.frame_ms = ms;
  sources[name] = std::move(s);
}

// An animated GIF (animation.yaml) and its switch
template<typename Img, typename Sw>
void add_gif(const std::string &name, Img &&image, Sw &&sw, int frame_ms, int x, int y, int scale, int vx, int vy,
             int fade_ms) {
  auto *img = picture::ptr_of(image);
  auto *s = picture::ptr_of(sw);
  add(name, {img->get_width(), img->get_height(), (int) img->get_animation_frame_count(), frame_ms, x, y, scale,
             [img](int f, int px, int py) -> uint32_t {
               img->set_frame(f);
               auto c = img->get_pixel(px, py);
               return (uint32_t) c.w << 24 | (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
             },
             vx, vy, [s](bool on) { s->publish_state(on); }, clamp_u16(fade_ms)});
}

// A copy on the panel: one of its places
struct Copy {
  bool used = false, placed = false;  // placed: the panel has it, sent once the animation is in
  uint8_t place = 0;                  // the panel's place
  int16_t x = 0, y = 0;
  bool own = false;  // a frame of its own, out of step with the others
  uint8_t start = 0;
  int fade = -1;        // ms it fades in and out, -1 for the animation's
  std::string doing;    // how it moves, for the status
  uint16_t rounds = 0;  // to the end of, 0 = on and on
  uint8_t tag = 0;      // of its ANIM_ROUNDS, the panel's ANIM_GONE has it
  // What it was told since it was placed, the last of each kind and channel, sent again after a panel restart
  struct Told {
    uint16_t key;
    uint8_t ch;   // GLIDE's and DRIFT's a bit per channel
    uint32_t at;  // millis(), so a glide sent again ends on time
    std::vector<uint8_t> m;
  };
  std::vector<Told> told;
};

enum State { WANT, SENT, FAILED };  // WANT: to be made and sent, or sent again after a panel restart

// A slot on the panel, the index is the slot
struct Anim {
  std::string name;  // "" = free slot
  const Source *src = nullptr;
  int state = WANT, w = 0, h = 0, scale = 1;
  bool half = false;
  uint8_t transp = ANIM_OPAQUE;
  uint32_t pal[16] = {};
  std::vector<uint8_t> data;  // the frame records
  std::vector<Copy> copies;
  uint16_t frame_rate = 100;  // percent, 0 = paused
  std::string why;            // when it failed
  bool lasting = false;       // it would fail again, so it waits until it's shown again
  size_t frame_bytes() const { return (size_t) w / 2 * h; }
};
inline Anim anims[ANIM_SLOTS];

// count() builds the palette from all frames, make() the records, send() sends them. One animation at a time.
enum Phase { IDLE, COUNT, MAKE, SEND };
static constexpr int ROWS_PX = 256;  // pixels a call takes at least, a row or a few, so the time is looked at seldom
static constexpr uint32_t FNV_START = 2166136261u, FNV_PRIME = 16777619u;  // FNV-1a, for the frames' hashes

inline struct {
  Anim *a = nullptr;
  int phase = IDLE, f = 0, y = 0;  // y: the row of frame f
  bool any_transp = false;
  picture::Transfer t;
  std::vector<uint32_t> hist, hash;         // hash: each frame's pixels, to find a GIF that repeats itself
  uint32_t sum = 0;                         // frame f's hash so far
  int frames = 0;                           // to make, a repeating GIF's first round only
  size_t last = 0;                          // the last record in data, 0 = only the first frame's
  std::vector<uint8_t> frame, prev, first;  // 2 pixels per byte
  std::vector<uint8_t> lut;                 // colour at 4 bits per channel -> palette index
} W;

inline Anim *find(const std::string &name) {
  for (auto &a : anims)
    if (!a.name.empty() && a.name == name)
      return &a;
  return nullptr;
}

// Bytes of the panel's ANIM_POOL that the animations it has or is getting take, with their copies' own frames,
// other than a's
inline size_t used_by_others(const Anim *a) {
  size_t n = 0;
  for (auto &o : anims) {
    if (&o == a || !(o.state == SENT || (&o == W.a && W.t.head)))
      continue;
    n += o.frame_bytes() + o.data.size();
    for (auto &c : o.copies)
      n += c.placed && c.own ? o.frame_bytes() : 0;
  }
  return n;
}

// What's left of the pool for the one being made
inline size_t room() { return ANIM_POOL - std::min<size_t>(ANIM_POOL, used_by_others(W.a)); }

inline std::string shown() {
  std::string s, failed;
  for (auto &a : anims) {
    if (a.name.empty())
      continue;
    if (a.state == FAILED) {
      failed += (failed.empty() ? "" : "; ") + a.name + ": " + a.why;
      continue;
    }
    if (a.state != SENT)
      continue;
    int n = 0;
    for (auto &c : a.copies)
      n += c.used;
    s += (s.empty() ? "" : ", ") + a.name;
    if (n > 1) {
      s += " (" + std::to_string(n) + " copies)";
    } else if (n == 1) {
      for (auto &c : a.copies)
        if (c.used)
          s += c.doing;
    }
  }
  std::string all = s.empty() ? "" : "Shown: " + s;
  if (!failed.empty())
    all += (all.empty() ? "" : "; ") + failed;
  return all.empty() ? "None" : all;
}

inline void status(const std::string &s) { picture::status(id(panel_animation_status), s); }

inline bool opaque(uint32_t c) { return c >> 24 >= 128; }  // 0xAARRGGBB, at least half opaque

// 0xAARRGGBB of the animation being made. At half size the 2x2 average, transparent if 3 or more of the 4 are.
inline uint32_t px(int f, int x, int y) {
  const Source &s = *W.a->src;
  if (!W.a->half)
    return x < s.w && y < s.h ? s.pixel(f, x, y) : 0;
  uint32_t r = 0, g = 0, b = 0, n = 0;
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++) {
      int sx = 2 * x + dx, sy = 2 * y + dy;
      uint32_t c = sx < s.w && sy < s.h ? s.pixel(f, sx, sy) : 0;
      if (!opaque(c))
        continue;
      r += c >> 16 & 255;
      g += c >> 8 & 255;
      b += c & 255;
      n++;
    }
  return n >= 2 ? 0xFF000000u | (r / n) << 16 | (g / n) << 8 | b / n : 0;
}

// Frees what count() and make() work in
inline void drop_work() {
  picture::drop(W.hist);
  picture::drop(W.hash);
  picture::drop(W.lut);
  picture::drop(W.frame);
  picture::drop(W.prev);
  picture::drop(W.first);
}

// Stops making or sending the one in hand. What was made of one being made goes.
inline void stop() {
  if (W.phase == COUNT || W.phase == MAKE)
    picture::drop(W.a->data);
  W.t.head = false;
  W.phase = IDLE;
  W.a = nullptr;
  drop_work();
}

// Gives up on the one in hand with the reason in the status, always false. A lasting failure isn't tried again
// after a hello.
inline bool fail(const std::string &why, bool lasting = false) {
  Anim *a = W.a;
  stop();
  picture::drop(a->data);
  a->why = why;
  a->state = FAILED;
  a->lasting = lasting;
  status(shown());
  return false;
}

// The panel draws 2x at most, so only scale 1 can go to half size
inline bool can_halve() { return !W.a->half && W.a->src->scale == 1; }

inline std::string too_big(size_t n) {
  return (W.a->half ? "too big even at half size (over " : "too big at scale 2 (over ") + std::to_string(n) + " bytes)";
}

// The most the records take while it's made: up to the room there can be, and one record over
inline size_t data_max() {
  return ANIM_POOL - W.a->frame_bytes() + ANIM_RECORD_HEAD + PDEFLATE_BOUND(W.a->frame_bytes());
}

// Whether the ESP has the RAM to make it: the colour counts, then the colour table, three frames, the records and
// what pdeflate takes for a frame. Without exceptions a failed allocation restarts the ESP.
inline bool has_ram() {
  size_t f = W.a->frame_bytes();
  return picture::has_ram(std::max<size_t>(picture::COUNT_RAM, picture::BINS + 3 * f + data_max() + PDEFLATE_HEAP(f)),
                          std::max<size_t>({picture::BINS * 4, data_max(), PDEFLATE_HEAP(0), 4 * f + 4}));
}

// ---- the link's messages

// Messages are laid out as in plink_keys.h
inline void put16(std::vector<uint8_t> &m, int at, int v) { plink_put16(&m[at], (uint16_t) v); }

// A glide sent again goes on for the time it had left
inline void send_told(const Copy::Told &t) {
  std::vector<uint8_t> m = t.m;
  if (t.key == K_ANIM_GLIDE) {
    uint32_t ms = plink_get16(m.data() + ANIM_GLIDE_MS), gone = millis() - t.at;
    put16(m, ANIM_GLIDE_MS, (int) (gone < ms ? ms - gone : 0));
  }
  link::send(t.key, m.data(), (int) m.size());
}

// tell() reads a WAVE's or MAP's channel and a GLIDE's or DRIFT's channels from one place
static_assert(ANIM_WAVE_CHANNEL == (int) ANIM_MAP_CHANNEL && ANIM_GLIDE_CHANNELS == (int) ANIM_DRIFT_CHANNELS &&
                  ANIM_WAVE_CHANNEL == (int) ANIM_GLIDE_CHANNELS,
              "the channels in the same place");

// Sends m to copy c of a with its place filled in, now if the panel has the copy and again after a restart.
// Replaces earlier ones with the same key and channel, for a GLIDE or DRIFT the ones on only its channels.
inline void tell(Anim &a, Copy &c, uint16_t key, std::vector<uint8_t> m) {
  m[ANIM_MSG_PLACE] = c.place;
  bool bits = key == K_ANIM_GLIDE || key == K_ANIM_DRIFT;
  uint8_t ch = bits || key == K_ANIM_WAVE || key == K_ANIM_MAP ? m[ANIM_GLIDE_CHANNELS] : 0;
  c.told.erase(std::remove_if(c.told.begin(), c.told.end(),
                              [&](const Copy::Told &t) { return t.key == key && (bits ? !(t.ch & ~ch) : t.ch == ch); }),
               c.told.end());
  c.told.push_back({key, ch, millis(), std::move(m)});
  if (a.state == SENT && c.placed)
    send_told(c.told.back());
}

// Copy c's channels from ch on to the values v over ms, in the channels' units, one message
inline void glide_copy(Anim &a, Copy &c, int ch, std::initializer_list<int> v, int ms, int ease) {
  std::vector<uint8_t> m(ANIM_GLIDE_HEAD + 2 * v.size());
  m[ANIM_GLIDE_CHANNELS] = (uint8_t) (((1u << v.size()) - 1) << ch);
  put16(m, ANIM_GLIDE_MS, clamp_u16(ms));
  m[ANIM_GLIDE_EASE] = (uint8_t) ease;
  int at = ANIM_GLIDE_HEAD;
  for (int x : v) {
    put16(m, at, clamp16(x));
    at += 2;
  }
  tell(a, c, K_ANIM_GLIDE, m);
}

inline int tenths(float v) { return clamp16((int) lroundf(v * 10)); }

// Drifts x and y, or turn, per second
inline void drift_copy(Anim &a, Copy &c, int ch, std::initializer_list<float> v) {
  std::vector<uint8_t> m(ANIM_DRIFT_HEAD + 2 * v.size());
  m[ANIM_DRIFT_CHANNELS] = (uint8_t) (((1u << v.size()) - 1) << ch);
  int at = ANIM_DRIFT_HEAD;
  for (float x : v) {
    put16(m, at, tenths(x));
    at += 2;
  }
  tell(a, c, K_ANIM_DRIFT, m);
}

inline void drift_xy(Anim &a, Copy &c, float vx, float vy) {
  drift_copy(a, c, ANIM_X, {vx, vy});
  c.doing = vx || vy ? ", drifting" : "";
}

inline void send_frame_rate(const Anim &a) {
  uint8_t m[ANIM_FRAMES_LEN];
  m[ANIM_FRAMES_SLOT] = (uint8_t) (&a - anims);
  plink_put16(m + ANIM_FRAMES_RATE, a.frame_rate);
  link::send(K_ANIM_FRAMES, m, sizeof m);
}

// Whether the panel's pool has room for a frame of a's own, once c's own one, if it has one there, is gone.
// Warns if not.
inline bool own_room(const Anim &a, const Copy *c) {
  if (used_by_others(nullptr) + a.frame_bytes() - (c && c->placed && c->own ? a.frame_bytes() : 0) <= ANIM_POOL)
    return true;
  ESP_LOGW("panel", "Animation %s: no room in the panel's %u bytes for a copy with a frame of its own", a.name.c_str(),
           (unsigned) ANIM_POOL);
  return false;
}

inline int fade_of(const Anim &a, const Copy &c) { return c.fade >= 0 ? c.fade : a.src->fade_ms; }

// Puts a copy on the panel with what it was told
inline void send_place(Anim &a, Copy &c) {
  uint8_t m[ANIM_PLACE_LEN];
  m[ANIM_MSG_PLACE] = c.place;
  m[ANIM_PLACE_SLOT] = (uint8_t) (&a - anims);
  // one on the screen stays on it, the sizes round up at half size
  const Source &s = *a.src;
  bool in = c.x >= 0 && c.y >= 0 && c.x + s.w * s.scale <= PIC_MAX_W && c.y + s.h * s.scale <= PIC_MAX_H;
  plink_put16(m + ANIM_PLACE_X, (uint16_t) (in ? std::min<int>(c.x, PIC_MAX_W - a.w * a.scale) : c.x));
  plink_put16(m + ANIM_PLACE_Y, (uint16_t) (in ? std::min<int>(c.y, PIC_MAX_H - a.h * a.scale) : c.y));
  m[ANIM_PLACE_OWN] = c.own;
  m[ANIM_PLACE_START] = c.start;
  plink_put16(m + ANIM_PLACE_FADE, (uint16_t) fade_of(a, c));
  link::send(K_ANIM_PLACE, m, sizeof m);
  c.placed = true;
  for (auto &t : c.told)
    send_told(t);
}

// A slot, ANIM_PLACE_BIT | a place, or ANIM_ALL, faded out over ms first
inline void send_clear(unsigned what, int ms = 0) { link::send(K_ANIM_CLEAR, (uint32_t) what | (uint32_t) ms << 8); }

// ---- making one

// False if it doesn't fit on the screen or next to the others, or the ESP hasn't the RAM. Straight to half size if
// a frame alone is over what's left, or the ESP only has the RAM for that, and it can.
inline bool start() {
  Anim &a = *W.a;
  const Source &s = *a.src;
  if (s.scale != 1 && s.scale != 2)
    return fail("scale has to be 1 or 2", true);
  if (s.frames < 1 || s.x < 0 || s.y < 0 || s.x + s.w * s.scale > PIC_MAX_W || s.y + s.h * s.scale > PIC_MAX_H)
    return fail("doesn't fit on the screen at " + std::to_string(s.x) + "," + std::to_string(s.y), true);
  size_t left = room();
  for (;;) {
    a.scale = s.scale * (a.half ? 2 : 1);
    a.w = ((a.half ? (s.w + 1) / 2 : s.w) + 1) & ~1;  // even, so rows are whole bytes
    a.h = a.half ? (s.h + 1) / 2 : s.h;
    bool fits = a.frame_bytes() <= left;
    if (fits && has_ram())
      break;
    if (!can_halve())
      return fits ? fail("the ESP doesn't have the RAM to make it") : fail(too_big(a.frame_bytes()), true);
    if (fits) {
      ESP_LOGW("panel", "Animation %s: the ESP has the RAM to make it at half size only", a.name.c_str());
    }
    a.half = true;
  }
  W.hist.assign(picture::BINS, 0);
  W.lut.resize(picture::BINS);
  W.hash.clear();
  W.sum = FNV_START;
  W.any_transp = false;
  W.f = W.y = 0;
  W.phase = COUNT;
  status("Making " + a.name);
  return true;
}

// Counts the colours of a row or a few, and at the end of a frame its hash. After the last frame, builds the
// palette.
inline void count() {
  Anim &a = *W.a;
  uint32_t h = W.sum;
  for (int n = 0; n < ROWS_PX && W.y < a.h; n += a.w, W.y++)
    for (int x = 0; x < a.w; x++) {
      uint32_t c = px(W.f, x, W.y);
      h = (h ^ c) * FNV_PRIME;
      if (opaque(c))
        W.hist[bin(c)]++;
      else
        W.any_transp = true;
    }
  W.sum = h;
  if (W.y < a.h)
    return;
  W.y = 0;
  W.hash.push_back(W.sum);
  W.sum = FNV_START;
  if (++W.f < a.src->frames)
    return;
  int n = a.src->frames;
  W.frames = n;
  const std::vector<uint16_t> &d = a.src->delays;
  for (int p = 1; p < n && W.frames == n; p++)
    if (n % p == 0 && std::equal(W.hash.begin() + p, W.hash.end(), W.hash.begin()) &&
        (d.empty() || std::equal(d.begin() + p, d.end(), d.begin())))
      W.frames = p;
  picture::drop(W.hash);
  std::vector<uint16_t> bins;
  memset(a.pal, 0, sizeof a.pal);
  int colours = picture::median_cut(W.hist, W.any_transp ? 15 : 16, a.pal, bins);
  a.transp = W.any_transp ? 15 : ANIM_OPAQUE;
  picture::fill_lut(W.lut, W.hist, a.pal, colours);
  picture::drop(W.hist);
  a.data.clear();
  a.data.reserve(data_max());  // so it never moves
  W.frame.resize(a.frame_bytes());
  W.prev.resize(a.frame_bytes());
  W.f = 0;
  W.last = 0;
  W.phase = MAKE;
}

// Appends the record that turns `from` into `to`, or the whole frame if from is null, then waits ms. A record
// waits MAX_FRAME_MS at most, the rest goes in empty records after it. A frame like the one before adds its time to
// the last record instead as far as that goes, unless that's the first frame's, which only plays once. The record
// back to the first frame, which the panel ends a round on, waits as long as the first frame's own, the empty
// ones after that are in the loop. False if there was no memory to compress it.
inline bool record(const std::vector<uint8_t> *from, const std::vector<uint8_t> &to, uint32_t ms, bool back = false) {
  Anim &a = *W.a;
  uint32_t ticks = std::max<uint32_t>(1, (ms + ANIM_TICK_MS / 2) / ANIM_TICK_MS);
  if (back)
    ticks = std::min<uint32_t>(ticks, ANIM_DELAY_MAX);
  else if (from && W.last && *from == to) {
    uint8_t &last = a.data[W.last + ANIM_RECORD_DELAY];
    uint32_t k = std::min<uint32_t>(ticks, ANIM_DELAY_MAX - last);
    last = (uint8_t) (last + k);
    ticks -= k;
  }
  for (; ticks; from = &to) {
    uint8_t delay = (uint8_t) std::min<uint32_t>(ticks, ANIM_DELAY_MAX);
    size_t at = a.data.size();
    a.data.resize(at + ANIM_RECORD_HEAD + PDEFLATE_BOUND(a.frame_bytes()));
    unsigned n = plink_anim_record(from ? from->data() : nullptr, to.data(), a.w, a.h, delay, a.data.data() + at);
    a.data.resize(at + n);
    if (!n)
      return false;
    W.last = at;
    ticks -= delay;
  }
  return true;
}

// Made again at half size
inline void halve() {
  W.t.head = false;
  picture::drop(W.a->data);
  W.a->half = true;
  start();
}

// For make(): all frames are in, or it's over the room left or there was no memory to compress it. Then it's
// sent, or made again at half size if it can.
inline void made(bool ok, size_t left) {
  Anim &a = *W.a;
  size_t need = a.frame_bytes() + a.data.size();
  drop_work();
  if (!ok || need > left) {
    if (!can_halve()) {
      fail(ok ? too_big(need) : "no memory to compress it", ok);
      return;
    }
    if (ok) {
      ESP_LOGI("panel", "Animation %s: over %u bytes after %d of %d frames, making it at half size", a.name.c_str(),
               (unsigned) left, W.f, W.frames);
    } else {
      ESP_LOGW("panel", "Animation %s: no memory to compress it, making it at half size", a.name.c_str());
    }
    halve();
    return;
  }
  a.data.shrink_to_fit();
  ESP_LOGI("panel", "Animation %s: %dx%d%s, %d frames%s, %u bytes", a.name.c_str(), a.w, a.h, a.half ? " drawn 2x" : "",
           W.frames, W.frames < a.src->frames ? " (the rest repeat them)" : "", (unsigned) need);
  W.t.reset();
  W.phase = SEND;
  status("Sending " + a.name);
}

// Maps a row of the frame or a few to the palette. Once the frame is in, adds its record, and after the last frame
// the one back to the first.
inline void make() {
  Anim &a = *W.a;
  if (W.y < a.h) {
    for (int n = 0; n < ROWS_PX && W.y < a.h; n += a.w, W.y++) {
      uint8_t *row = W.frame.data() + (size_t) W.y * (a.w / 2);
      for (int x = 0; x < a.w; x++) {
        uint32_t c = px(W.f, x, W.y);
        uint8_t i = opaque(c) ? W.lut[bin(c)] : a.transp;
        row[x / 2] = (uint8_t) (x & 1 ? row[x / 2] | i : i << 4);
      }
    }
    return;
  }
  W.y = 0;
  size_t left = room();
  bool ok = record(W.f ? &W.prev : nullptr, W.frame, frame_ms(*a.src, W.f));
  if (!W.f)
    W.first = W.frame;
  W.prev.swap(W.frame);
  bool last = ++W.f >= W.frames;
  if (ok && last && W.f > 1 && a.frame_bytes() + a.data.size() <= left)
    ok = record(&W.prev, W.first, frame_ms(*a.src, 0), true);
  if (!ok || last || a.frame_bytes() + a.data.size() > left)
    made(ok, left);
}

// The header once the link is free, then the records as it has room, then END
inline void send() {
  Anim &a = *W.a;
  if (W.t.no_answer()) {  // the panel lost it or its answer
    if (W.t.retry()) {
      ESP_LOGW("panel", "Animation %s: no answer from the panel, sending it again", a.name.c_str());
    } else {
      fail("the panel didn't answer");
    }
    return;
  }
  if (!W.t.head) {
    if (!link::connected || link::waiting() > link::BACKLOG || W.t.blocked())
      return;
    W.t.open();
    uint8_t h[ANIM_BEGIN_HEAD + PIC_PAL_LEN];
    link::send(K_ANIM_BEGIN, h,
               plink_anim_begin(h, W.t.id, (uint8_t) (&a - anims), (uint16_t) a.w, (uint16_t) a.h, (uint8_t) a.scale,
                                a.transp, (uint32_t) a.data.size(), a.pal));
  }
  W.t.send_raw(a.data.data(), a.data.size());
}

// The panel has it: its copies go on
inline void done() {
  Anim &a = *W.a;
  W.phase = IDLE;
  W.t.reset();
  W.a = nullptr;
  a.state = SENT;
  if (a.frame_rate != 100)
    send_frame_rate(a);
  for (auto &c : a.copies) {
    if (c.used && c.own && !own_room(a, &c))
      c = Copy();
    if (c.used)
      send_place(a, c);
  }
  status(shown());
}

// The panel's K_PIC_ACK = status << 24 | id << 16 | bytes received, from panel.h
inline void ack(uint32_t v) {
  if (W.phase != SEND || !W.t.answers(v))
    return;  // for a picture or an earlier transfer
  Anim &a = *W.a;
  uint8_t st = v >> 24;
  if (st == PIC_PROGRESS) {
    status(picture::progress(a.name, v, a.data.size()));
  } else if (st == PIC_DONE) {
    done();
  } else if (st == PIC_TOO_BIG && can_halve()) {
    halve();
  } else if ((st != PIC_BAD && st != PIC_BAD_CRC) || !W.t.retry()) {  // a bad one goes again while it has tries
    fail("the panel didn't take it (" + std::to_string(st) + ")");
  }
}

// To be sent again, and made again from scratch if it failed
inline void again(Anim &a) {
  if (a.state == FAILED)
    a.half = false;
  a.state = WANT;
  for (auto &c : a.copies)
    c.placed = false;
}

// After a hello the panel has none of them: it restarted, or cleared them for our hello. They all go again, but
// for failures that would happen again.
inline void restarted() {
  stop();
  for (auto &a : anims)
    if (!(a.state == FAILED && a.lasting))
      again(a);
}

// ---- what to show

// A free place on the panel, -1 with a warning about name's copy if all are taken
inline int free_place(const std::string &name) {
  for (int p = 0; p < ANIM_PLACES; p++) {
    bool taken = false;
    for (auto &a : anims)
      for (auto &c : a.copies)
        taken |= c.used && c.place == p;
    if (!taken)
      return p;
  }
  ESP_LOGW("panel", "Animation %s: the panel shows %d copies at most", name.c_str(), ANIM_PLACES);
  return -1;
}

// Copy i of a, made if it wasn't there. nullptr if the panel has no place left for it.
inline Copy *copy_of(Anim &a, int i) {
  if ((int) a.copies.size() <= i)
    a.copies.resize(i + 1);
  Copy &c = a.copies[i];
  if (!c.used) {
    int p = free_place(a.name);
    if (p < 0)
      return nullptr;
    c = Copy();
    c.used = true;
    c.place = (uint8_t) p;
  }
  return &c;
}

inline const Source *source(const std::string &name) {
  auto it = sources.find(name);
  if (it != sources.end())
    return &it->second;
  ESP_LOGW("panel", "No animation called '%s'", name.c_str());
  return nullptr;
}

// The slot for name, taken if it hasn't one. nullptr if there's no such animation, or no slot or place left for it.
inline Anim *slot_for(const std::string &name) {
  if (Anim *a = find(name))
    return a;
  const Source *s = source(name);
  if (!s)
    return nullptr;
  Anim *a = nullptr;
  for (auto &o : anims)
    if (o.name.empty() && !a)
      a = &o;
  if (!a) {
    ESP_LOGW("panel", "No room for animation %s: the panel holds %d at a time", name.c_str(), ANIM_SLOTS);
    return nullptr;
  }
  if (free_place(name) < 0)
    return nullptr;
  *a = Anim();
  a->name = name;
  a->src = s;
  if (s->on_shown)
    s->on_shown(true);
  return a;
}

// slot_for(), and a failed one is tried again
inline Anim *want(const std::string &name) {
  Anim *a = slot_for(name);
  if (a && a->state == FAILED)
    again(*a);
  return a;
}

inline void free_slot(Anim &a, bool send = true) {
  if (send && (a.state == SENT || (W.a == &a && W.t.head)))
    send_clear((unsigned) (&a - anims), a.src->fade_ms);
  if (W.a == &a)
    stop();
  if (a.src->on_shown)
    a.src->on_shown(false);
  a = Anim();
}

// Clears one animation and all its copies, or all of them with no name, fading them out
inline void clear(const std::string &name) {
  if (name.empty() || name == "None") {
    int ms = 0;
    for (auto &a : anims)
      if (!a.name.empty()) {
        ms = std::max(ms, a.src->fade_ms);
        free_slot(a, false);
      }
    send_clear(ANIM_ALL, ms);
  } else if (Anim *a = find(name)) {
    free_slot(*a);
  }
  status(shown());
}

// panel_show_animation and the switches: shows it where its yaml puts it, alongside the others, and copy 0 there
// with its drift again if it went. A failed one is tried again.
inline void show(const std::string &name) {
  Anim *a = want(name);
  if (!a)
    return;
  Copy *c = !a->copies.empty() && a->copies[0].used ? nullptr : copy_of(*a, 0);
  if (c) {
    const Source &s = *a->src;
    c->x = (int16_t) s.x;
    c->y = (int16_t) s.y;
    if (s.vx || s.vy)
      drift_xy(*a, *c, s.vx, s.vy);
    if (a->state == SENT)
      send_place(*a, *c);
  }
  status(shown());
}

// panel_place_animation: copy i of name at x, y, which can be off the screen, own = a frame of its own started start
// frames in. Shows the animation if it isn't yet, a failed one is tried again. fade: ms it fades in and out, -1
// for the animation's.
inline void place(const std::string &name, int i, int x, int y, bool own, int start, int fade = -1) {
  if (i < 0 || i >= ANIM_PLACES) {
    ESP_LOGW("panel", "Animation %s: copy %d isn't one of 0 to %d", name.c_str(), i, ANIM_PLACES - 1);
    return;
  }
  Anim *a = want(name);
  if (!a)
    return;
  if (own && a->state == SENT && !own_room(*a, i < (int) a->copies.size() ? &a->copies[i] : nullptr))
    return;
  Copy *c = copy_of(*a, i);
  if (!c)
    return;
  c->x = clamp16(x);
  c->y = clamp16(y);
  c->own = own;
  c->fade = std::min(65535, fade);
  c->start = (uint8_t) std::clamp(start, 0, 255);
  c->doing.clear();
  c->told.clear();
  c->rounds = 0;
  c->placed = false;
  if (a->state == SENT)
    send_place(*a, *c);
  status(shown());
}

// Copy i of name's animation and the animation, nulls if it isn't shown. what is the action, panel_<what>_animation,
// for the warning, none without it.
inline std::pair<Anim *, Copy *> copy_for(const std::string &name, int i, const char *what = nullptr) {
  Anim *a = find(name);
  if (a && i >= 0 && i < (int) a->copies.size() && a->copies[i].used)
    return {a, &a->copies[i]};
  if (what) {
    ESP_LOGW("panel", "panel_%s_animation: no copy %d of animation '%s' shown", what, i, name.c_str());
  }
  return {};
}

// panel_remove_animation: copy i of name goes, fading out over fade ms, -1 for its own. The animation stays on
// the panel.
inline void remove(const std::string &name, int i, int fade = -1) {
  auto [a, c] = copy_for(name, i);
  if (!c)
    return;
  if (a->state == SENT && c->placed)
    send_clear(ANIM_PLACE_BIT | c->place, fade >= 0 ? std::min(65535, fade) : fade_of(*a, *c));
  *c = Copy();
  status(shown());
}

// Which of names s is, or -1 with a warning from panel_<what>_animation
template<size_t N> int option(const char *const (&names)[N], const std::string &s, const char *what) {
  std::string all;
  for (size_t i = 0; i < N; i++) {
    if (s == names[i])
      return (int) i;
    all += (i ? ", " : "") + std::string(names[i]);
  }
  ESP_LOGW("panel", "panel_%s_animation: '%s' isn't one of %s", what, s.c_str(), all.c_str());
  return -1;
}

static const char *const CHANNELS[] = {"x", "y", "turn", "size", "opacity", "speed"};
static const char *const EASES[] = {"none", "in", "out", "both"};    // ANIM_EASE_* bits
static const char *const SLOWS[] = {"none", "low", "high", "both"};  // ANIM_WAVE_SLOW_* bits
static_assert(sizeof CHANNELS / sizeof *CHANNELS == ANIM_CHANNELS, "a name for each channel");

inline int no_change(int ch) { return ch >= ANIM_SIZE ? 100 : 0; }

// For panel_glide_animation: copy i's x, y, turn, size, opacity or speed to v over ms, eased. A turn goes the short
// way round, size, opacity and speed are percent.
inline void glide(const std::string &name, int i, const std::string &what, int to, int ms, const std::string &ease) {
  static const int LO[] = {-32768, -32768, -32768, ANIM_SIZE_MIN, 0, 0},
                   HI[] = {32767, 32767, 32767, ANIM_SIZE_MAX, ANIM_OPACITY_MAX, ANIM_SPEED_MAX};
  auto [a, c] = copy_for(name, i, __func__);
  int ch = option(CHANNELS, what, __func__), e = option(EASES, ease, __func__);
  if (!c || ch < 0 || e < 0)
    return;
  if (to < LO[ch] || to > HI[ch] || ms < 0 || ms > 65535) {
    ESP_LOGW("panel", "panel_%s_animation: %s %d over %d ms, it takes %d..%d over 0..65535 ms", __func__, what.c_str(),
             to, ms, LO[ch], HI[ch]);
    return;
  }
  glide_copy(*a, *c, ch, {to}, ms, e);
  if (ch <= ANIM_Y)
    c->doing = ", moved";
  status(shown());
}

// For panel_move_animation: glide copy i to x, y over ms, 0 jumps
inline void move(const std::string &name, int i, int x, int y, int ms, const std::string &ease) {
  auto [a, c] = copy_for(name, i, __func__);
  int e = option(EASES, ease, __func__);
  if (!c || e < 0)
    return;
  if (clamp16(x) != x || clamp16(y) != y || clamp_u16(ms) != ms) {
    ESP_LOGW("panel", "panel_%s_animation: %d,%d over %d ms is out of range, it goes to %d,%d over %d ms", __func__, x,
             y, ms, clamp16(x), clamp16(y), clamp_u16(ms));
  }
  glide_copy(*a, *c, ANIM_X, {x, y}, ms, e);
  c->doing = ", moved to " + std::to_string(clamp16(x)) + "," + std::to_string(clamp16(y));
  status(shown());
}

// For panel_drift_animation: vx, vy in px/s, to a tenth, wrapping round the screen. 0, 0 stops it where it is.
inline void drift(const std::string &name, int i, float vx, float vy) {
  if (auto [a, c] = copy_for(name, i, __func__); c) {
    drift_xy(*a, *c, vx, vy);
    status(shown());
  }
}

// For panel_spin_animation: degrees per second clockwise, negative the other way. 0 stops it where it is.
inline void spin(const std::string &name, int i, float speed) {
  if (auto [a, c] = copy_for(name, i, __func__); c)
    drift_copy(*a, *c, ANIM_TURN, {speed});
}

// For panel_flip_animation: mirrored left to right, upside down, or both
inline void flip(const std::string &name, int i, bool x, bool y) {
  if (auto [a, c] = copy_for(name, i, __func__); c) {
    std::vector<uint8_t> m(ANIM_FLIP_LEN);
    m[ANIM_FLIP_FLAGS] = (uint8_t) ((x ? ANIM_FLIP_X : 0) | (y ? ANIM_FLIP_Y : 0));
    tell(*a, *c, K_ANIM_FLIP, m);
  }
}

// For panel_area_animation: where a drift or wander keeps and a map goes across. Each edge is screen, cross or
// bounce.
inline void area(const std::string &name, int i, int left, int top, int right, int bottom, const std::string &edge_x,
                 const std::string &edge_y) {
  static const char *const EDGES[] = {"screen", "cross", "bounce"};  // ANIM_EDGE_*
  auto [a, c] = copy_for(name, i, __func__);
  int ex = option(EDGES, edge_x, __func__), ey = option(EDGES, edge_y, __func__);
  if (!c || ex < 0 || ey < 0)
    return;
  if (right <= left || bottom <= top) {
    ESP_LOGW("panel", "panel_%s_animation: %d,%d to %d,%d, right and bottom have to be past left and top", __func__,
             left, top, right, bottom);
    return;
  }
  std::vector<uint8_t> m(ANIM_AREA_LEN);
  put16(m, ANIM_AREA_X0, clamp16(left));
  put16(m, ANIM_AREA_Y0, clamp16(top));
  put16(m, ANIM_AREA_X1, clamp16(right));
  put16(m, ANIM_AREA_Y1, clamp16(bottom));
  m[ANIM_AREA_EDGES] = (uint8_t) ex;
  m[ANIM_AREA_EDGES + 1] = (uint8_t) ey;
  tell(*a, *c, K_ANIM_AREA, m);
}

// For panel_wander_animation: legs of speed_min..speed_max px/s toward heading +- spread for leg_min..leg_max ms,
// with rests of rest_min..rest_max ms between. The panel keeps the heading, spread and speeds in range.
inline void wander(const std::string &name, int i, float speed_min, float speed_max, int heading, int spread,
                   int leg_min, int leg_max, int rest_min, int rest_max) {
  auto [a, c] = copy_for(name, i, __func__);
  if (!c)
    return;
  if (speed_max < speed_min || leg_max < leg_min || rest_max < rest_min) {
    ESP_LOGW("panel", "panel_%s_animation: each max has to be at least its min", __func__);
    return;
  }
  std::vector<uint8_t> m(ANIM_WANDER_LEN);
  put16(m, ANIM_WANDER_SPEED, std::max(0, tenths(speed_min)));
  put16(m, ANIM_WANDER_SPEED + 2, std::max(0, tenths(speed_max)));
  put16(m, ANIM_WANDER_HEADING, clamp16(heading));
  put16(m, ANIM_WANDER_SPREAD, clamp16(spread));
  put16(m, ANIM_WANDER_LEG, clamp_u16(leg_min));
  put16(m, ANIM_WANDER_LEG + 2, clamp_u16(leg_max));
  put16(m, ANIM_WANDER_REST, clamp_u16(rest_min));
  put16(m, ANIM_WANDER_REST + 2, clamp_u16(rest_max));
  tell(*a, *c, K_ANIM_WANDER, m);
  c->doing = ", wandering";
  status(shown());
}

// For panel_face_animation: turns toward where it goes, at most tilt degrees from facing, at rate degrees per second
inline void face(const std::string &name, int i, int facing, bool mirror, int tilt, int rate, bool level) {
  auto [a, c] = copy_for(name, i, __func__);
  if (!c)
    return;
  std::vector<uint8_t> m(ANIM_FACE_LEN);
  m[ANIM_FACE_FLAGS] = (uint8_t) ((mirror ? ANIM_FACE_MIRROR : 0) | (level ? ANIM_FACE_LEVEL : 0));
  put16(m, ANIM_FACE_FACING, (facing % 360 + 360) % 360);
  put16(m, ANIM_FACE_TILT, std::clamp(tilt, 0, 180));
  put16(m, ANIM_FACE_RATE, std::clamp(rate, 0, 32767));
  tell(*a, *c, K_ANIM_FACE, m);
}

// For panel_wave_animation: copy i's channel goes between low and high on its own, see K_ANIM_WAVE. One back to no
// change is let go, here and on the panel, which takes its place for a new one.
inline void wave(const std::string &name, int i, const std::string &what, int low, int high, int rise, int fall,
                 int hold_high, int hold_low, int jitter, bool random, const std::string &slow) {
  auto [a, c] = copy_for(name, i, __func__);
  int ch = option(CHANNELS, what, __func__), s = option(SLOWS, slow, __func__);
  if (!c || ch < 0 || s < 0)
    return;
  if (high < low || clamp16(low) != low || clamp16(high) != high || jitter < 0 || jitter > 100) {
    ESP_LOGW("panel", "panel_%s_animation: low %d and high %d, jitter %d: high has to be at least low, jitter 0..100",
             __func__, low, high, jitter);
    return;
  }
  bool off = low == high && low == no_change(ch);
  int n = 0;
  for (auto &other : anims)
    for (auto &o : other.copies)
      for (auto &t : o.told)
        n += t.key == K_ANIM_WAVE && !(&o == c && t.ch == ch);
  if (!off && n >= ANIM_WAVES) {
    ESP_LOGW("panel", "panel_%s_animation: the panel runs %d waves at most", __func__, ANIM_WAVES);
    return;
  }
  std::vector<uint8_t> m(ANIM_WAVE_LEN);
  m[ANIM_WAVE_CHANNEL] = (uint8_t) ch;
  put16(m, ANIM_WAVE_LO, low);
  put16(m, ANIM_WAVE_HI, high);
  put16(m, ANIM_WAVE_MS, clamp_u16(rise));
  put16(m, ANIM_WAVE_MS + 2, clamp_u16(fall));
  put16(m, ANIM_WAVE_HOLD, clamp_u16(hold_high));
  put16(m, ANIM_WAVE_HOLD + 2, clamp_u16(hold_low));
  m[ANIM_WAVE_JITTER] = (uint8_t) jitter;
  m[ANIM_WAVE_FLAGS] = (uint8_t) (s | (random ? ANIM_WAVE_RANDOM : 0));
  tell(*a, *c, K_ANIM_WAVE, m);
  if (off)
    c->told.pop_back();  // a restarted panel has none there either
}

// For panel_map_animation: copy i's turn, size, opacity or speed follows where it is across its area
inline void map(const std::string &name, int i, const std::string &what, int left, int right, int top, int bottom) {
  auto [a, c] = copy_for(name, i, __func__);
  int ch = option(CHANNELS, what, __func__);
  if (!c || ch < 0)
    return;
  if (ch < ANIM_TURN) {
    ESP_LOGW("panel", "panel_%s_animation: x and y don't follow where it is", __func__);
    return;
  }
  std::vector<uint8_t> m(ANIM_MAP_LEN);
  m[ANIM_MAP_CHANNEL] = (uint8_t) ch;
  int at = ANIM_MAP_VALUES;
  for (int v : {left, right, top, bottom}) {
    put16(m, at, clamp16(v));
    at += 2;
  }
  tell(*a, *c, K_ANIM_MAP, m);
}

inline uint8_t tags;  // so an old ANIM_GONE doesn't take a copy placed again

// For panel_rounds_animation: plays to the end of rounds rounds, then fades out and goes. 0 plays on.
inline void rounds(const std::string &name, int i, int n) {
  auto [a, c] = copy_for(name, i, __func__);
  if (!c)
    return;
  c->rounds = clamp_u16(n);
  c->tag = ++tags;
  std::vector<uint8_t> m(ANIM_ROUNDS_LEN);
  put16(m, ANIM_ROUNDS_COUNT, c->rounds);
  put16(m, ANIM_ROUNDS_FADE, fade_of(*a, *c));
  m[ANIM_ROUNDS_TAG] = c->tag;
  tell(*a, *c, K_ANIM_ROUNDS, m);
}

// For panel_frame_rate_animation: how fast name's frames go, percent, 0 holds the frame it's on
inline void frame_rate(const std::string &name, int percent) {
  Anim *a = find(name);
  if (!a) {
    ESP_LOGW("panel", "panel_%s_animation: animation '%s' isn't shown", __func__, name.c_str());
    return;
  }
  a->frame_rate = (uint16_t) std::clamp(percent, 0, ANIM_RATE_MAX);
  if (a->state == SENT)
    send_frame_rate(*a);
}

// The panel's K_ANIM_GONE = place | tag << 8: a copy played its rounds and went
inline void gone(uint32_t v) {
  for (auto &a : anims)
    for (auto &c : a.copies)
      if (c.used && c.placed && c.rounds && c.place == (v & 0xFF) && c.tag == (v >> 8 & 0xFF)) {
        c = Copy();
        status(shown());
      }
}

// every 20 ms
inline void loop() {
  if (!link::connected && (W.phase == COUNT || W.phase == MAKE))  // nothing is made without the panel
    stop();
  if (W.phase == IDLE) {
    if (!link::connected)
      return;
    for (auto &a : anims)
      if (!a.name.empty() && a.state == WANT) {
        W.a = &a;
        if (!a.data.empty()) {  // made already, sent again after a hello. It fit before, so it fits again.
          W.t.reset();
          W.phase = SEND;
        } else if (!start()) {
          return;
        }
        break;
      }
    if (W.phase == IDLE)
      return;
  }
  uint32_t t0 = micros();
  while (W.phase == COUNT && micros() - t0 < picture::SLICE_US)
    count();
  while (W.phase == MAKE && micros() - t0 < picture::SLICE_US)
    make();
  if (W.phase == SEND)
    send();
}

}  // namespace flexit::panel::anim
