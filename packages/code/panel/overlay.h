// Screensaver overlays: the animations from animation.yaml and the weather's. All frames share 16 colours, each with
// its own opacity: the source's own, or counted from the frames with one clear if any pixel is. One frame goes as a
// still, which the panel keeps packed. More go as an animation, each frame after the first the deflated rectangle
// that changed. The panel keeps up to OVERLAY_SLOTS of them in OVERLAY_POOL and shows each in up to OVERLAY_PLACES
// places, the copies of one in step unless a copy gets a frame of its own. A copy can run a script there, see
// overlay_script.h.
#pragma once
#include <strings.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "overlay_script.h"
#include "panel_link.h"
#include "pdeflate.h"
#include "plink.h"
#include "quant.h"
#include "rgb.h"
#include "transfer.h"
#include "util.h"

namespace flexit::panel::overlay {

// x, y is where it goes on the screen, pixel(frame, x, y) gives 0xAARRGGBB, vx, vy a drift in px/s. on_shown sets
// its switch, so it follows what the actions show and clear. Its copies fade in and out over fade_ms, and their light
// below 100 takes its colours toward shade, but for those that glow.
struct Source {
  int w, h, frames, frame_ms, x, y, scale;
  std::function<uint32_t(int, int, int)> pixel;
  int vx = 0, vy = 0;
  std::function<void(bool)> on_shown = {};
  int fade_ms = 0;
  std::vector<uint16_t> delays = {};  // ms each frame shows, if not all frame_ms
  // Colours of its own, 0xAARRGGBB, 16 at most, each pixel the nearest. Counted from the frames if there are none.
  std::vector<uint32_t> colours = {};
  Clips clips = {};                  // for its scripts
  Program script = {};               // copy 0's when it's shown
  std::vector<uint32_t> glows = {};  // 0xRRGGBB, its pixels in them glow
  uint32_t shade = 0;                // 0xRRGGBB
};

// ms frame f of s shows
inline uint32_t frame_ms(const Source &s, int f) { return s.delays.empty() ? s.frame_ms : s.delays[f]; }
inline std::map<std::string, Source> sources;

// frame_ms goes in OVERLAY_TICK_MS steps, and the panel shows a frame OVERLAY_FRAME_MIN_MS at least
inline constexpr int MAX_FRAME_MS = OVERLAY_DELAY_MAX * OVERLAY_TICK_MS;
inline void add(const std::string &name, Source s) {
  int ms = std::clamp(s.frame_ms, OVERLAY_FRAME_MIN_MS, MAX_FRAME_MS);
  if (ms != s.frame_ms) {
    ESP_LOGW("panel", "Animation %s: frame_ms %d is outside %d..%d, it plays at %d", name.c_str(), s.frame_ms,
             OVERLAY_FRAME_MIN_MS, MAX_FRAME_MS, ms);
  }
  s.frame_ms = ms;
  sources[name] = std::move(s);
}

// A copy on the panel: one of its places
struct Copy {
  bool used = false, placed = false;  // placed: the panel has it, sent once the animation is in
  uint8_t place = 0;                  // the panel's place
  int16_t x = 0, y = 0;
  bool own = false;                 // a frame of its own, out of step with the others
  uint8_t depth = OVERLAY_NEAREST;  // K_OVERLAY_DEPTH's, sent with its PLACE
  int fade = -1;                    // ms it fades in and out, -1 for the animation's
  std::string doing;                // how it moves, for the status
  Program script;                   // what it runs on the panel, sent after what it was told
  uint8_t tag = 0;                  // its script's END's, the panel's OVERLAY_GONE has it. 0 without one.
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
struct Overlay {
  std::string name;  // "" = free slot
  const Source *src = nullptr;
  int state = WANT, w = 0, h = 0, scale = 1;
  bool half = false, still = false;  // still: one frame, OVERLAY_STILL
  uint32_t pal[16] = {};             // 0xAARRGGBB
  uint16_t glows = 0;                // a bit for each of pal that glows
  std::vector<uint8_t> data;         // the frame records, or a still's pieces
  std::vector<Copy> copies;
  uint16_t frame_rate = 100;  // percent, 0 = paused
  std::string why;            // when it failed
  bool lasting = false;       // it would fail again, so it waits until it's shown again
  size_t frame_bytes() const { return (size_t) (w + 1) / 2 * h; }
  size_t panel_frame() const { return still ? 0 : frame_bytes(); }  // what the panel keeps of its frame
};
inline Overlay overlays[OVERLAY_SLOTS];

// count() builds the palette from all frames, make() the records, send() sends them. One overlay at a time.
enum Phase { IDLE, COUNT, MAKE, SEND };
inline constexpr int ROWS_PX = 256;  // pixels a call takes at least, a row or a few, so the time is looked at seldom
inline constexpr uint32_t FNV_START = 2166136261u, FNV_PRIME = 16777619u;  // FNV-1a, for the frames' hashes

struct Work {
  Overlay *a = nullptr;
  int phase = IDLE, f = 0, y = 0;  // y: the row of frame f
  bool any_transp = false;
  Transfer t;
  std::vector<uint32_t> hist, hash;         // hash: each frame's pixels, to find a GIF that repeats itself
  uint32_t sum = 0;                         // frame f's hash so far
  int frames = 0;                           // to make, a repeating GIF's first round only
  std::vector<uint8_t> frame, prev, first;  // 2 pixels per byte
  std::vector<uint8_t> lut;                 // colour at 4 bits per channel -> palette index, 0xFF until looked up
  std::vector<uint32_t> enc, tok;           // pdeflate's work for a frame, see pdeflate_work_t
  std::vector<uint16_t> rest;
  int colours = 0;                  // in the palette
  uint32_t record_us = UINT32_MAX;  // what a record takes, as fits() has it
};
inline Work W;

inline Overlay *find(const std::string &name) {
  for (auto &a : overlays)
    if (!a.name.empty() && a.name == name)
      return &a;
  return nullptr;
}

inline size_t script_bytes(const Copy &c) {
  return c.script.bytes.empty() ? 0 : OVERLAY_SCRIPT_POOL + c.script.bytes.size();
}

// Bytes of the panel's OVERLAY_POOL that the overlays it has or is getting take, with their copies' own frames and
// scripts, other than a's
inline size_t used_by_others(const Overlay *a) {
  size_t n = 0;
  for (auto &o : overlays) {
    if (&o == a || !(o.state == SENT || (&o == W.a && W.t.head)))
      continue;
    n += o.panel_frame() + o.data.size();
    for (auto &c : o.copies)
      n += c.placed ? (c.own ? o.panel_frame() : 0) + script_bytes(c) : 0;
  }
  return n;
}

// What's left of the pool for the one being made
inline size_t room() { return OVERLAY_POOL - std::min<size_t>(OVERLAY_POOL, used_by_others(W.a)); }

inline std::string shown() {
  std::string s, failed;
  for (auto &a : overlays) {
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

inline void status(const std::string &s) { panel::status(id(panel_animation_status), s); }

inline bool opaque(uint32_t c) { return c >> 24 >= 128; }  // 0xAARRGGBB, at least half opaque

// 0xAARRGGBB of the animation being made. At half size the 2x2 average, by alpha with colours of its own, else
// transparent if 3 or more of the 4 are, an odd width's last column taken twice.
inline uint32_t px(int f, int x, int y) {
  const Source &s = *W.a->src;
  if (!W.a->half)
    return s.pixel(f, x, y);
  bool soft = !s.colours.empty();
  uint32_t r = 0, g = 0, b = 0, al = 0, n = 0;
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++) {
      int sx = std::min(2 * x + dx, s.w - 1), sy = 2 * y + dy;
      uint32_t c = sy < s.h ? s.pixel(f, sx, sy) : 0, k = soft ? c >> 24 : 255;
      if (!soft && !opaque(c))
        continue;
      r += (c >> 16 & 255) * k;
      g += (c >> 8 & 255) * k;
      b += (c & 255) * k;
      al += k;
      n++;
    }
  if (!al || (!soft && n < 2))
    return 0;
  return (soft ? al / 4 : 255) << 24 | (r / al) << 16 | (g / al) << 8 | b / al;
}

// The source's own colour nearest c, as they look over anything: each channel by its alpha, and the alpha
inline uint8_t nearest_own(uint32_t c, const uint32_t *pal, int n) {
  auto by_alpha = [](uint32_t v, int at) { return (int) ((v >> at & 255) * (v >> 24) / 255); };
  int best = 0;
  uint32_t bd = UINT32_MAX;
  for (int i = 0; i < n; i++) {
    int dr = by_alpha(c, 16) - by_alpha(pal[i], 16), dg = by_alpha(c, 8) - by_alpha(pal[i], 8),
        db = by_alpha(c, 0) - by_alpha(pal[i], 0), da = (int) (c >> 24) - (int) (pal[i] >> 24);
    uint32_t d = (uint32_t) (2 * dr * dr + 4 * dg * dg + 3 * db * db + 3 * da * da);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  return (uint8_t) best;
}

// The colours the panel draws none of, at an opacity that rounds to 0 of its 15ths
inline uint16_t clear_colours(const uint32_t *pal) {
  uint16_t m = 0;
  for (int i = 0; i < 16; i++)
    m |= (uint16_t) (((pal[i] >> 24) * 15 + 127) / 255 == 0) << i;
  return m;
}

// Frees what count() and make() work in
inline void drop_work() {
  drop(W.hist);
  drop(W.hash);
  drop(W.lut);
  drop(W.frame);
  drop(W.prev);
  drop(W.first);
  drop(W.enc);
  drop(W.tok);
  drop(W.rest);
}

// Stops making or sending the one in hand. What was made of one being made goes.
inline void stop() {
  if (W.phase == COUNT || W.phase == MAKE)
    drop(W.a->data);
  W.t.head = false;
  W.phase = IDLE;
  W.a = nullptr;
  drop_work();
}

// Gives up on the one in hand with the reason in the status, always false. A lasting failure isn't tried again
// after a hello.
inline bool fail(const std::string &why, bool lasting = false) {
  Overlay *a = W.a;
  stop();
  drop(a->data);
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

// The most the records take while it's made: up to the room there can be, and one record over. A still's pieces up
// to the pool.
inline size_t data_max() {
  if (W.a->still)
    return OVERLAY_POOL;
  return OVERLAY_POOL - W.a->frame_bytes() + OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(W.a->frame_bytes());
}

// Whether the ESP has the RAM to make it: the colour counts, then the colour table, three frames, the records and
// pdeflate's work for a frame, in three blocks. A still has one frame and no deflate. Without exceptions a failed
// allocation restarts the ESP.
inline bool has_ram() {
  size_t f = W.a->frame_bytes();
  if (W.a->still)
    return panel::has_ram(std::max<size_t>(COUNT_RAM, BINS + f + data_max()),
                          std::max<size_t>({BINS * 4, data_max(), f}));
  size_t work = PDEFLATE_ENC + PDEFLATE_TOK(f) + PDEFLATE_REST(f);
  return panel::has_ram(std::max<size_t>(COUNT_RAM, BINS + 3 * f + data_max() + work),
                        std::max<size_t>({BINS * 4, data_max(), PDEFLATE_ENC, PDEFLATE_TOK(f)}));
}

// ---- the link's messages

// A glide sent again goes on for the time it had left
inline void send_told(const Copy::Told &t) {
  std::vector<uint8_t> m = t.m;
  if (t.key == K_OVERLAY_GLIDE) {
    uint32_t ms = plink_get16(m.data() + OVERLAY_GLIDE_MS), gone = millis() - t.at;
    msg::put16(m, OVERLAY_GLIDE_MS, (int) (gone < ms ? ms - gone : 0));
  }
  link::send(t.key, m.data(), (int) m.size());
}

// tell() reads a WAVE's or MAP's channel and a GLIDE's or DRIFT's channels from one place
static_assert(OVERLAY_WAVE_CHANNEL == (int) OVERLAY_MAP_CHANNEL &&
                  OVERLAY_GLIDE_CHANNELS == (int) OVERLAY_DRIFT_CHANNELS &&
                  OVERLAY_WAVE_CHANNEL == (int) OVERLAY_GLIDE_CHANNELS,
              "the channels in the same place");

// Sends m to copy c of a with its place filled in, now if the panel has the copy and again after a restart.
// Replaces earlier ones with the same key and channel, for a GLIDE or DRIFT the ones on only its channels.
inline void tell(Overlay &a, Copy &c, uint16_t key, std::vector<uint8_t> m) {
  m[OVERLAY_MSG_PLACE] = c.place;
  bool bits = key == K_OVERLAY_GLIDE || key == K_OVERLAY_DRIFT;
  uint8_t ch = bits || key == K_OVERLAY_WAVE || key == K_OVERLAY_MAP ? m[OVERLAY_GLIDE_CHANNELS] : 0;
  c.told.erase(std::remove_if(c.told.begin(), c.told.end(),
                              [&](const Copy::Told &t) { return t.key == key && (bits ? !(t.ch & ~ch) : t.ch == ch); }),
               c.told.end());
  c.told.push_back({key, ch, millis(), std::move(m)});
  if (a.state == SENT && c.placed)
    send_told(c.told.back());
}

// Copy c's channels from ch on to the values v over ms, in the channels' units, one message
inline void glide_copy(Overlay &a, Copy &c, int ch, std::initializer_list<int> v, int ms, int ease) {
  tell(a, c, K_OVERLAY_GLIDE, msg::glide(ch, v, ms, ease));
}

// Drifts x and y, or turn, per second
inline void drift_copy(Overlay &a, Copy &c, int ch, std::initializer_list<float> v) {
  std::vector<int> t;
  for (float x : v)
    t.push_back(ch == OVERLAY_TURN ? tenths(x) : hundredths(x));
  tell(a, c, K_OVERLAY_DRIFT, msg::drift(ch, t));
}

inline void drift_xy(Overlay &a, Copy &c, float vx, float vy) {
  drift_copy(a, c, OVERLAY_X, {vx, vy});
  c.doing = vx || vy ? ", drifting" : "";
}

inline void send_frame_rate(const Overlay &a) {
  uint8_t m[OVERLAY_FRAMES_LEN];
  m[OVERLAY_FRAMES_SLOT] = (uint8_t) (&a - overlays);
  plink_put16(m + OVERLAY_FRAMES_RATE, a.frame_rate);
  link::send(K_OVERLAY_FRAMES, m, sizeof m);
}

// Whether the panel's pool has room for a frame of a's own, once c's own one, if it has one there, is gone.
// Warns if not.
inline bool own_room(const Overlay &a, const Copy *c) {
  if (used_by_others(nullptr) + a.panel_frame() - (c && c->placed && c->own ? a.panel_frame() : 0) <= OVERLAY_POOL)
    return true;
  ESP_LOGW("panel", "Animation %s: no room in the panel's %u bytes for a copy with a frame of its own", a.name.c_str(),
           (unsigned) OVERLAY_POOL);
  return false;
}

inline int fade_of(const Overlay &a, const Copy &c) { return c.fade >= 0 ? c.fade : a.src->fade_ms; }

inline void send_depth(const Copy &c) {
  std::vector<uint8_t> m = msg::depth(c.depth);
  m[OVERLAY_MSG_PLACE] = c.place;
  link::send(K_OVERLAY_DEPTH, m.data(), (int) m.size());
}

// The place copy i of the animation called name has, for a script that looks at it. One no copy has if it isn't
// shown, so the script sees nothing there.
inline uint8_t place_of(const std::string &name, int i) {
  for (auto &a : overlays)
    if (a.name == name && i >= 0 && i < (int) a.copies.size() && a.copies[i].used)
      return a.copies[i].place;
  for (int p = 0; p < OVERLAY_PLACES; p++) {
    bool taken = false;
    for (auto &a : overlays)
      for (auto &c : a.copies)
        taken |= c.used && c.place == p;
    if (!taken)
      return (uint8_t) p;
  }
  return OVERLAY_SELF;
}

// Copy c's script to the panel, the places it looks at, its END's tag and fade filled in
inline void send_script(const Overlay &a, const Copy &c) {
  std::vector<uint8_t> b = c.script.bytes;
  for (auto &p : c.script.patches) {
    if (p.kind == Program::PLACE)
      b[p.at] = place_of(p.name, p.copy);
    else if (p.kind == Program::TAG)
      b[p.at] = c.tag;
    else
      plink_put16(&b[p.at], (uint16_t) fade_of(a, c));
  }
  if (used_by_others(nullptr) + (c.placed ? 0 : script_bytes(c) + (c.own ? a.panel_frame() : 0)) > OVERLAY_POOL) {
    ESP_LOGW("panel", "Animation %s: no room in the panel's %u bytes for copy %d's script", a.name.c_str(),
             (unsigned) OVERLAY_POOL, (int) (&c - a.copies.data()));
  }
  size_t at = 0;
  do {
    uint8_t m[PBULK_MSG_MAX] = {c.place};
    plink_put16(m + OVERLAY_SCRIPT_AT, (uint16_t) at);
    plink_put16(m + OVERLAY_SCRIPT_LEN, (uint16_t) b.size());
    size_t n = std::min(b.size() - at, sizeof m - OVERLAY_SCRIPT_BYTES);
    std::copy_n(b.begin() + (long) at, n, m + OVERLAY_SCRIPT_BYTES);
    link::send(K_OVERLAY_SCRIPT, m, (int) (OVERLAY_SCRIPT_BYTES + n));
    at += n;
  } while (at < b.size());
}

// Puts a copy on the panel with what it was told
inline void send_place(Overlay &a, Copy &c) {
  uint8_t m[OVERLAY_PLACE_LEN];
  m[OVERLAY_MSG_PLACE] = c.place;
  m[OVERLAY_PLACE_SLOT] = (uint8_t) (&a - overlays);
  // one on the screen stays on it, the sizes round up at half size
  const Source &s = *a.src;
  bool in = c.x >= 0 && c.y >= 0 && c.x + s.w * s.scale <= PIC_MAX_W && c.y + s.h * s.scale <= PIC_MAX_H;
  plink_put16(m + OVERLAY_PLACE_X, (uint16_t) (in ? std::min<int>(c.x, PIC_MAX_W - a.w * a.scale) : c.x));
  plink_put16(m + OVERLAY_PLACE_Y, (uint16_t) (in ? std::min<int>(c.y, PIC_MAX_H - a.h * a.scale) : c.y));
  m[OVERLAY_PLACE_OWN] = c.own;
  plink_put16(m + OVERLAY_PLACE_FADE, (uint16_t) fade_of(a, c));
  link::send(K_OVERLAY_PLACE, m, sizeof m);
  if (c.depth != OVERLAY_NEAREST)
    send_depth(c);
  for (auto &t : c.told)
    send_told(t);
  if (!c.script.bytes.empty())
    send_script(a, c);
  c.placed = true;
}

// A slot, OVERLAY_PLACE_BIT | a place, or OVERLAY_ALL, faded out over ms first
inline void send_clear(unsigned what, int ms = 0) { link::send(K_OVERLAY_CLEAR, (uint32_t) what | (uint32_t) ms << 8); }

// ---- making one

// False if it doesn't fit on the screen or next to the others, or the ESP hasn't the RAM. Straight to half size if
// a frame alone is over what's left, or the ESP only has the RAM for that, and it can.
inline bool start() {
  Overlay &a = *W.a;
  const Source &s = *a.src;
  if (s.scale != 1 && s.scale != 2)
    return fail("scale has to be 1 or 2", true);
  if (s.frames < 1 || s.x < 0 || s.y < 0 || s.x + s.w * s.scale > PIC_MAX_W || s.y + s.h * s.scale > PIC_MAX_H)
    return fail("doesn't fit on the screen at " + std::to_string(s.x) + "," + std::to_string(s.y), true);
  size_t left = room();
  for (;;) {
    a.scale = s.scale * (a.half ? 2 : 1);
    a.w = a.half ? (s.w + 1) / 2 : s.w;
    a.h = a.half ? (s.h + 1) / 2 : s.h;
    a.still = s.frames == 1;
    bool fits = a.panel_frame() <= left;
    if (fits && has_ram())
      break;
    if (!can_halve())
      return fits ? fail("the ESP doesn't have the RAM to make it") : fail(too_big(a.frame_bytes()), true);
    if (fits) {
      ESP_LOGW("panel", "Animation %s: the ESP has the RAM to make it at half size only", a.name.c_str());
    }
    a.half = true;
  }
  W.hist.assign(BINS, 0);
  W.hash.clear();
  W.sum = FNV_START;
  W.any_transp = false;
  W.f = W.y = 0;
  W.phase = COUNT;
  status("Making " + a.name);
  return true;
}

// Counts the colours of a row or a few, and at the end of a frame its hash
inline void count() {
  Overlay &a = *W.a;
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
  W.f++;
}

// After the last frame is counted: the palette, at once, and the RAM to make the frames
inline void palette() {
  Overlay &a = *W.a;
  int n = a.src->frames;
  W.frames = n;
  const std::vector<uint16_t> &d = a.src->delays;
  for (int p = 1; p < n && W.frames == n; p++)
    if (n % p == 0 && std::equal(W.hash.begin() + p, W.hash.end(), W.hash.begin()) &&
        (d.empty() || std::equal(d.begin() + p, d.end(), d.begin())))
      W.frames = p;
  drop(W.hash);
  a.still = W.frames == 1;
  std::vector<uint16_t> bins;
  memset(a.pal, 0, sizeof a.pal);
  const std::vector<uint32_t> &own = a.src->colours;
  if (!own.empty()) {
    W.colours = (int) std::min<size_t>(own.size(), 16);
    std::copy_n(own.begin(), W.colours, a.pal);
  } else {
    W.colours = median_cut(W.hist, W.any_transp ? 15 : 16, a.pal, bins);
    for (int i = 0; i < W.colours; i++)
      a.pal[i] |= 0xFF000000u;  // and 15 clear if any pixel is
  }
  a.glows = 0;  // as make() meets them
  drop(W.hist);
  drop(bins);                // the lut takes their place, so has_ram()'s peak holds
  W.lut.assign(BINS, 0xFF);  // filled as make() meets the colours
  a.data.clear();
  a.data.reserve(data_max());  // so it never moves
  W.frame.resize(a.frame_bytes());
  if (!a.still) {
    W.prev.resize(a.frame_bytes());
    W.enc.resize(PDEFLATE_ENC / 4);
    W.tok.resize(PDEFLATE_TOK(a.frame_bytes()) / 4);
    W.rest.resize((PDEFLATE_REST(a.frame_bytes()) + 1) / 2);
  }
  W.f = 0;
  W.record_us = UINT32_MAX;
  W.phase = MAKE;
}

// ms in OVERLAY_TICK_MS, 1 at least
inline uint32_t ticks_of(uint32_t ms) { return std::max<uint32_t>(1, (ms + OVERLAY_TICK_MS / 2) / OVERLAY_TICK_MS); }

// Appends the record that turns `from` into `to`, or the whole frame if from is null, then waits ms: a frame of the
// panel's for each of the source's, so a script's frame numbers are the source's. A record waits MAX_FRAME_MS at
// most, the rest goes in OVERLAY_RECORD_MORE records after it. The record back to the first frame waits as long as
// the first frame's own, the first frame's MORE ones after that are in the loop. False without the memory to
// compress it. Stops where data would have to grow, which only happens past the pool, where made() turns it down
// anyway.
inline bool record(const std::vector<uint8_t> *from, const std::vector<uint8_t> &to, uint32_t ms, bool back = false) {
  Overlay &a = *W.a;
  const size_t most = OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(a.frame_bytes());  // what a record takes while it's made
  uint32_t ticks = ticks_of(ms);
  uint8_t delay = (uint8_t) std::min<uint32_t>(ticks, OVERLAY_DELAY_MAX);
  size_t at = a.data.size();
  if (at + most > a.data.capacity())
    return true;  // it would move, a peak has_ram() didn't count
  a.data.resize(at + most);
  pdeflate_work_t work = {W.enc.data(), W.tok.data(), W.rest.data()};
  unsigned n =
      plink_overlay_record_in(&work, from ? from->data() : nullptr, to.data(), a.w, a.h, delay, a.data.data() + at);
  a.data.resize(at + n);
  if (!n)
    return false;
  for (ticks -= delay; ticks && !back; ticks -= delay) {
    delay = (uint8_t) std::min<uint32_t>(ticks, OVERLAY_DELAY_MAX);
    if (a.data.size() + OVERLAY_RECORD_HEAD > a.data.capacity())
      return true;
    const uint8_t more[OVERLAY_RECORD_HEAD] = {delay, OVERLAY_RECORD_MORE, 0, 0, 0};
    a.data.insert(a.data.end(), more, more + OVERLAY_RECORD_HEAD);
  }
  return true;
}

// Made again at half size
inline void halve() {
  W.t.head = false;
  drop(W.a->data);
  W.a->half = true;
  start();
}

// For make(): all frames are in, or what they need is over the room left or there was no memory to compress them.
// Then it's sent, or made again at half size if it can.
inline void made(bool ok, size_t left, size_t need) {
  Overlay &a = *W.a;
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
  ESP_LOGI("panel", "Animation %s: %dx%d%s, %s%s, %u bytes", a.name.c_str(), a.w, a.h, a.half ? " drawn 2x" : "",
           a.still ? "still" : (std::to_string(W.frames) + " frames").c_str(),
           W.frames < a.src->frames ? " (the rest repeat them)" : "", (unsigned) need);
  W.t.reset();
  W.phase = SEND;
  status("Sending " + a.name);
}

// Maps a row of the frame or a few to the palette. Once the frame is in, adds its record, and after the last frame
// the one back to the first. A still's pieces instead. A record goes in one go, so it waits for the next slice
// unless this one is fresh or has the time the last record took. False when it waits.
inline bool make(uint32_t deadline_us, bool fresh) {
  Overlay &a = *W.a;
  if (W.y < a.h) {
    bool own = !a.src->colours.empty();
    uint8_t *lut = W.lut.data();
    const std::vector<uint32_t> &glows = a.src->glows;
    for (int n = 0; n < ROWS_PX && W.y < a.h; n += a.w, W.y++) {
      uint8_t *row = W.frame.data() + (size_t) W.y * ((a.w + 1) / 2);
      for (int x = 0; x < a.w; x++) {
        uint32_t c = px(W.f, x, W.y);
        uint8_t i = own ? nearest_own(c, a.pal, W.colours) : opaque(c) ? lut_index(lut, c, a.pal, W.colours) : 15;
        if (opaque(c) && std::find(glows.begin(), glows.end(), c & 0xFFFFFF) != glows.end())
          a.glows |= (uint16_t) (1u << i);
        row[x / 2] = (uint8_t) (x & 1 ? row[x / 2] | i : i << 4);
      }
      if (a.w & 1)  // the half byte past an odd width, never drawn, as the last so it packs with it
        row[a.w / 2] |= row[a.w / 2] >> 4;
    }
    return true;
  }
  if (!fits(deadline_us, W.record_us, fresh))
    return false;
  W.y = 0;
  size_t left = room();
  if (a.still) {
    a.data.resize(left);
    uint8_t delay = (uint8_t) std::min<uint32_t>(ticks_of(frame_ms(*a.src, 0)), OVERLAY_DELAY_MAX);
    uint32_t n =
        plink_overlay_pieces(W.frame.data(), a.w, a.h, clear_colours(a.pal), delay, a.data.data(), (uint32_t) left);
    a.data.resize(n);
    W.f = 1;
    made(true, left, n ? n : left + 1);
    return true;
  }
  uint32_t t0 = micros();
  bool ok = record(W.f ? &W.prev : nullptr, W.frame, frame_ms(*a.src, W.f));
  if (!W.f)
    W.first = W.frame;
  W.prev.swap(W.frame);
  bool last = ++W.f >= W.frames;
  if (ok && last && W.f > 1 && a.frame_bytes() + a.data.size() <= left)
    ok = record(&W.prev, W.first, frame_ms(*a.src, 0), true);
  if (!ok || last || a.frame_bytes() + a.data.size() > left)
    made(ok, left, a.frame_bytes() + a.data.size());
  uint32_t us = micros() - t0;  // a frame that changed little goes quicker, the next may not
  W.record_us = W.record_us == UINT32_MAX ? us : std::max(us, W.record_us / 4 * 3);
  return true;
}

// The header once the link is free, then the records as it has room until deadline_us, then END
inline void send(uint32_t deadline_us) {
  Overlay &a = *W.a;
  Transfer::Pumped p = W.t.pump(deadline_us, [&a] {
    W.t.open(a.data.data(), (uint32_t) a.data.size());
    uint8_t h[OVERLAY_BEGIN_LEN];
    link::send(K_OVERLAY_BEGIN, h,
               plink_overlay_begin(h, W.t.id, (uint8_t) (&a - overlays), a.still ? OVERLAY_STILL : OVERLAY_ANIMATED,
                                   (uint16_t) a.w, (uint16_t) a.h, (uint8_t) a.scale, (uint32_t) a.data.size(), a.pal,
                                   a.glows, a.src->shade));
  });
  if (p == Transfer::AGAIN) {  // the panel lost it or its answer
    ESP_LOGW("panel", "Animation %s: no answer from the panel, sending it again", a.name.c_str());
  } else if (p == Transfer::GAVE_UP) {
    fail("the panel didn't answer");
  }
}

// The panel has it: its copies go on
inline void done() {
  Overlay &a = *W.a;
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

// The panel's K_PIC_ACK = status << 24 | id << 16 | bytes received, see plink_keys.h
inline void ack(uint32_t v) {
  if (W.phase != SEND || !W.t.answers(v))
    return;  // for a picture or an earlier transfer
  Overlay &a = *W.a;
  uint8_t st = v >> 24;
  if (st == PIC_PROGRESS) {
    status(progress(a.name, v, a.data.size()));
  } else if (st == PIC_DONE) {
    done();
  } else if (st == PIC_TOO_BIG && can_halve()) {
    halve();
  } else if (!W.t.refused(st)) {
    fail("the panel didn't take it (" + std::to_string(st) + ")");
  }
}

// To be sent again, and made again from scratch if it failed
inline void again(Overlay &a) {
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
  for (auto &a : overlays)
    if (!(a.state == FAILED && a.lasting))
      again(a);
}

// ---- what to show

// A free place on the panel, -1 with a warning about name's copy if all are taken
inline int free_place(const std::string &name) {
  for (int p = 0; p < OVERLAY_PLACES; p++) {
    bool taken = false;
    for (auto &a : overlays)
      for (auto &c : a.copies)
        taken |= c.used && c.place == p;
    if (!taken)
      return p;
  }
  ESP_LOGW("panel", "Animation %s: the panel shows %d copies at most", name.c_str(), OVERLAY_PLACES);
  return -1;
}

// Copy i of a, made if it wasn't there. nullptr if the panel has no place left for it.
inline Copy *copy_of(Overlay &a, int i) {
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

// The slot for name, taken if it hasn't one, and tried again if it failed. nullptr if there's no such animation, or
// no slot or place left for it.
inline Overlay *want(const std::string &name) {
  if (Overlay *a = find(name)) {
    if (a->state == FAILED)
      again(*a);
    return a;
  }
  const Source *s = source(name);
  if (!s)
    return nullptr;
  Overlay *a = nullptr;
  for (auto &o : overlays)
    if (o.name.empty() && !a)
      a = &o;
  if (!a) {
    ESP_LOGW("panel", "No room for animation %s: the panel holds %d at a time", name.c_str(), OVERLAY_SLOTS);
    return nullptr;
  }
  if (free_place(name) < 0)
    return nullptr;
  *a = Overlay();
  a->name = name;
  a->src = s;
  if (s->on_shown)
    s->on_shown(true);
  return a;
}

inline void free_slot(Overlay &a, bool send = true) {
  if (send && (a.state == SENT || (W.a == &a && W.t.head)))
    send_clear((unsigned) (&a - overlays), a.src->fade_ms);
  if (W.a == &a)
    stop();
  if (a.src->on_shown)
    a.src->on_shown(false);
  a = Overlay();
}

// Clears one animation and all its copies, or all of them with no name, fading them out
inline void clear(const std::string &name) {
  if (name.empty() || name == "None") {
    int ms = 0;
    for (auto &a : overlays)
      if (!a.name.empty()) {
        ms = std::max(ms, a.src->fade_ms);
        free_slot(a, false);
      }
    send_clear(OVERLAY_ALL, ms);
  } else if (Overlay *a = find(name)) {
    free_slot(*a);
  }
  status(shown());
}

inline uint8_t tags;  // so an old OVERLAY_GONE doesn't take a copy placed again

// Copy c runs p from now on, the one it ran before gone, told what p sends before it does anything else
inline void set_script(Overlay &a, Copy &c, Program p) {
  for (auto &t : p.told)
    tell(a, c, t.key, std::move(t.m));
  p.told.clear();
  c.script = std::move(p);
  c.tag = 0;
  if (c.script.ends() && !(c.tag = ++tags))
    c.tag = ++tags;  // 0 is none
  if (a.state == SENT && c.placed)
    send_script(a, c);
}

// panel_show_animation and the switches: shows it where its yaml puts it, alongside the others, and copy 0 there
// with its drift and its script again if it went. A failed one is tried again.
inline void show(const std::string &name) {
  Overlay *a = want(name);
  if (!a)
    return;
  Copy *c = !a->copies.empty() && a->copies[0].used ? nullptr : copy_of(*a, 0);
  if (c) {
    const Source &s = *a->src;
    c->x = (int16_t) s.x;
    c->y = (int16_t) s.y;
    c->own = s.script.frames;  // its script plays them
    if (s.vx || s.vy)
      drift_xy(*a, *c, s.vx, s.vy);
    set_script(*a, *c, s.script);
    if (a->state == SENT)
      send_place(*a, *c);
  }
  status(shown());
}

// An animated GIF from animation.yaml and its switch, shown if the switch was restored on. clips: "sail 0-3, tack
// 4-8", script: what copy 0 runs, see overlay_script.h.
template<typename Img, typename Sw>
void add_gif(const std::string &name, Img &&image, Sw &&sw, int frame_ms, int x, int y, int scale, int vx, int vy,
             int fade_ms, const std::string &clips = "", const std::string &script = "") {
  auto *img = ptr_of(image);
  auto *s = ptr_of(sw);
  Source src{img->get_width(),
             img->get_height(),
             (int) img->get_animation_frame_count(),
             frame_ms,
             x,
             y,
             scale,
             [img](int f, int px, int py) -> uint32_t {
               img->set_frame(f);
               auto c = img->get_pixel(px, py);
               return (uint32_t) c.w << 24 | (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
             },
             vx,
             vy,
             [s](bool on) { s->publish_state(on); },
             clamp_u16(fade_ms)};
  std::string error;
  src.clips = parse_clips(clips, error);
  if (error.empty())
    src.script = compile(script, error, src.clips);
  if (!error.empty()) {
    ESP_LOGW("panel", "Animation %s: %s", name.c_str(), error.c_str());
  }
  add(name, std::move(src));
  if (s->state)
    show(name);
}

// animation.yaml's switch turned on. If that's before add_gif(), add_gif() shows it.
inline void switched_on(const std::string &name) {
  if (sources.count(name))
    show(name);
}

// The script of start frames in, for a copy with a frame of its own
inline Program started(int start) {
  std::string error;
  return start > 0 ? compile("frame " + std::to_string(start), error) : Program();
}

// Copy i of name at x, y, which can be off the screen, running p, with a frame of its own if own or p plays frames.
// Shows the animation if it isn't yet, a failed one is tried again. fade: ms it fades in and out, -1 for the
// animation's. depth: K_OVERLAY_DEPTH's. False if it isn't placed.
inline bool place(const std::string &name, int i, int x, int y, Program p, bool own = false, int fade = -1,
                  uint8_t depth = OVERLAY_NEAREST) {
  if (i < 0 || i >= OVERLAY_PLACES) {
    ESP_LOGW("panel", "Animation %s: copy %d isn't one of 0 to %d", name.c_str(), i, OVERLAY_PLACES - 1);
    return false;
  }
  Overlay *a = want(name);
  if (!a)
    return false;
  own |= p.frames;
  if (own && a->state == SENT && !own_room(*a, i < (int) a->copies.size() ? &a->copies[i] : nullptr))
    return false;
  Copy *c = copy_of(*a, i);
  if (!c)
    return false;
  c->x = clamp16(x);
  c->y = clamp16(y);
  c->own = own;
  c->fade = std::min(65535, fade);
  c->depth = depth;
  c->doing.clear();
  c->told.clear();
  c->placed = false;
  set_script(*a, *c, std::move(p));
  if (a->state == SENT)
    send_place(*a, *c);
  status(shown());
  return true;
}
// panel_place_animation: the same with own a frame of its own started start frames in
inline bool place(const std::string &name, int i, int x, int y, bool own, int start, int fade = -1,
                  uint8_t depth = OVERLAY_NEAREST) {
  return place(name, i, x, y, own ? started(start) : Program(), own, fade, depth);
}

// Copy i of name's animation and the animation, nulls if it isn't shown. what is the action, panel_<what>_animation,
// for the warning, none without it.
inline std::pair<Overlay *, Copy *> copy_for(const std::string &name, int i, const char *what = nullptr) {
  Overlay *a = find(name);
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
    send_clear(OVERLAY_PLACE_BIT | c->place, fade >= 0 ? std::min(65535, fade) : fade_of(*a, *c));
  *c = Copy();
  status(shown());
}

// Copy i of name goes to depth, K_OVERLAY_DEPTH's
inline void depth(const std::string &name, int i, uint8_t d) {
  auto [a, c] = copy_for(name, i);
  if (!c || c->depth == d)
    return;
  c->depth = d;
  if (a->state == SENT && c->placed)
    send_depth(*c);
}

// Which of names s is, in any case, or -1 with a warning from panel_<what>_animation
template<size_t N> int option(const char *const (&names)[N], const std::string &s, const char *what) {
  int i = panel::find(names, s);
  if (i < 0) {
    std::string all;
    for (size_t k = 0; k < N; k++)
      all += (k ? ", " : "") + std::string(names[k]);
    ESP_LOGW("panel", "panel_%s_animation: '%s' isn't one of %s", what, s.c_str(), all.c_str());
  }
  return i;
}

inline int no_change(int ch) { return ch >= OVERLAY_SIZE ? 100 : 0; }

// For panel_glide_animation: copy i's x, y, turn, size, opacity, speed or light to v over ms, eased. A turn goes the
// short way round, the rest but x and y are percent.
inline void glide(const std::string &name, int i, const std::string &what, int to, int ms, const std::string &ease) {
  static const int LO[] = {-32768, -32768, -32768, OVERLAY_SIZE_MIN, 0, 0, 0}, HI[] = {32767,
                                                                                       32767,
                                                                                       32767,
                                                                                       OVERLAY_SIZE_MAX,
                                                                                       OVERLAY_OPACITY_MAX,
                                                                                       OVERLAY_SPEED_MAX,
                                                                                       OVERLAY_LIGHT_MAX};
  static_assert(sizeof LO / sizeof *LO == OVERLAY_CHANNELS && sizeof HI / sizeof *HI == OVERLAY_CHANNELS,
                "a range for each channel");
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
  if (ch <= OVERLAY_Y)
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
  glide_copy(*a, *c, OVERLAY_X, {x, y}, ms, e);
  c->doing = ", moved to " + std::to_string(clamp16(x)) + "," + std::to_string(clamp16(y));
  status(shown());
}

// For panel_drift_animation: vx, vy in px/s, to a hundredth, wrapping round the screen. 0, 0 stops it where it is.
inline void drift(const std::string &name, int i, float vx, float vy) {
  if (auto [a, c] = copy_for(name, i, __func__); c) {
    drift_xy(*a, *c, vx, vy);
    status(shown());
  }
}

// For panel_spin_animation: degrees per second clockwise, negative the other way. 0 stops it where it is.
inline void spin(const std::string &name, int i, float speed) {
  if (auto [a, c] = copy_for(name, i, __func__); c)
    drift_copy(*a, *c, OVERLAY_TURN, {speed});
}

// For panel_tint_animation: copy i's colours as shades of colour, like FF77BB or #FF77BB, but for those that glow.
// "" or none for its own.
inline void tint(const std::string &name, int i, const std::string &colour) {
  auto [a, c] = copy_for(name, i, __func__);
  if (!c)
    return;
  std::string hex = colour.substr(!colour.empty() && colour[0] == '#');
  bool none = hex.empty() || strcasecmp(hex.c_str(), "none") == 0;
  if (!none && (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](char k) { return isxdigit(k); }))) {
    ESP_LOGW("panel", "panel_%s_animation: '%s' isn't a colour like FF77BB", __func__, colour.c_str());
    return;
  }
  std::optional<uint32_t> rgb;
  if (!none)
    rgb = (uint32_t) strtoul(hex.c_str(), nullptr, 16);
  tell(*a, *c, K_OVERLAY_TINT, msg::tint(rgb));
}

// For panel_flip_animation: mirrored left to right, upside down, or both
inline void flip(const std::string &name, int i, bool x, bool y) {
  if (auto [a, c] = copy_for(name, i, __func__); c)
    tell(*a, *c, K_OVERLAY_FLIP, msg::flip(x, y));
}

// For panel_area_animation: where a drift or wander keeps and a map goes across. Each edge is screen, cross or
// bounce.
inline void area(const std::string &name, int i, int left, int top, int right, int bottom, const std::string &edge_x,
                 const std::string &edge_y) {
  auto [a, c] = copy_for(name, i, __func__);
  int ex = option(EDGES, edge_x, __func__), ey = option(EDGES, edge_y, __func__);
  if (!c || ex < 0 || ey < 0)
    return;
  if (right <= left || bottom <= top) {
    ESP_LOGW("panel", "panel_%s_animation: %d,%d to %d,%d, right and bottom have to be past left and top", __func__,
             left, top, right, bottom);
    return;
  }
  tell(*a, *c, K_OVERLAY_AREA, msg::area(left, top, right, bottom, ex, ey));
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
  tell(*a, *c, K_OVERLAY_WANDER,
       msg::wander(tenths(speed_min), tenths(speed_max), heading, spread, leg_min, leg_max, rest_min, rest_max));
  c->doing = ", wandering";
  status(shown());
}

// For panel_face_animation: turns toward where it goes, at most tilt degrees from facing, at rate degrees per second
inline void face(const std::string &name, int i, int facing, bool mirror, int tilt, int rate, bool level) {
  auto [a, c] = copy_for(name, i, __func__);
  if (!c)
    return;
  tell(*a, *c, K_OVERLAY_FACE, msg::face(facing, mirror, tilt, rate, level));
}

// For panel_wave_animation: copy i's channel goes between low and high on its own, see K_OVERLAY_WAVE. One back to no
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
  for (auto &other : overlays)
    for (auto &o : other.copies)
      for (auto &t : o.told)
        n += t.key == K_OVERLAY_WAVE && !(&o == c && t.ch == ch);
  if (!off && n >= OVERLAY_WAVES) {
    ESP_LOGW("panel", "panel_%s_animation: the panel runs %d waves at most", __func__, OVERLAY_WAVES);
    return;
  }
  tell(*a, *c, K_OVERLAY_WAVE,
       msg::wave(ch, low, high, rise, fall, hold_high, hold_low, jitter, s | (random ? OVERLAY_WAVE_RANDOM : 0)));
  if (off)
    c->told.pop_back();  // a restarted panel has none there either
}

// For panel_map_animation: copy i's turn, size, opacity or speed follows where it is across its area
inline void map(const std::string &name, int i, const std::string &what, int left, int right, int top, int bottom) {
  auto [a, c] = copy_for(name, i, __func__);
  int ch = option(CHANNELS, what, __func__);
  if (!c || ch < 0)
    return;
  if (ch < OVERLAY_TURN) {
    ESP_LOGW("panel", "panel_%s_animation: x and y don't follow where it is", __func__);
    return;
  }
  tell(*a, *c, K_OVERLAY_MAP, msg::map(ch, left, right, top, bottom));
}

// Copy c of a runs the script in text, with numbers by name in consts. False with a warning from
// panel_<what>_animation if it can't.
inline bool run_script(Overlay &a, Copy &c, const std::string &text, const Consts &consts, const char *what) {
  std::string error;
  Program p = compile(text, error, a.src->clips, consts);
  if (error.empty() && p.frames && !c.own)
    error = "it plays frames, and copy " + std::to_string(&c - a.copies.data()) + " has no frame of its own";
  if (!error.empty()) {
    ESP_LOGW("panel", "panel_%s_animation: %s's script, %s", what, a.name.c_str(), error.c_str());
    return false;
  }
  set_script(a, c, std::move(p));
  return true;
}

// For panel_script_animation: copy i runs the script in text, see overlay_script.h, with numbers by name in consts.
// One that plays frames needs a frame of its own. "" stops the one it runs.
inline bool script(const std::string &name, int i, const std::string &text, const Consts &consts = {}) {
  auto [a, c] = copy_for(name, i, __func__);
  return c && run_script(*a, *c, text, consts, __func__);
}

// For panel_rounds_animation: plays to the end of rounds rounds, then fades out and goes. 0 plays on.
inline void rounds(const std::string &name, int i, int n) {
  if (auto [a, c] = copy_for(name, i, __func__); c)
    run_script(*a, *c, n > 0 ? "wait rounds " + std::to_string(clamp_u16(n)) + "; end" : "", {}, __func__);
}

// For panel_frame_rate_animation: how fast name's frames go, percent, 0 holds the frame it's on
inline void frame_rate(const std::string &name, int percent) {
  Overlay *a = find(name);
  if (!a) {
    ESP_LOGW("panel", "panel_%s_animation: animation '%s' isn't shown", __func__, name.c_str());
    return;
  }
  a->frame_rate = (uint16_t) std::clamp(percent, 0, OVERLAY_RATE_MAX);
  if (a->state == SENT)
    send_frame_rate(*a);
}

// Called with the animation when its script ended the last copy it had
inline std::function<void(const std::string &)> emptied;

// The panel's K_OVERLAY_GONE = place | tag << 8: a copy's script ended it
inline void gone(uint32_t v) {
  for (auto &a : overlays)
    for (auto &c : a.copies)
      if (c.used && c.placed && c.tag && c.place == (v & 0xFF) && c.tag == (v >> 8 & 0xFF)) {
        c = Copy();
        status(shown());
        if (emptied && std::none_of(a.copies.begin(), a.copies.end(), [](const Copy &o) { return o.used; }))
          emptied(std::string(a.name));  // which may clear it
        return;
      }
}

// Called with the animation, the copy and the number when a copy's script gets to a mark
inline std::function<void(const std::string &, int, int)> marked;

// The panel's K_OVERLAY_MARK = place | mark << 8
inline void mark(uint32_t v) {
  for (auto &a : overlays)
    for (size_t i = 0; i < a.copies.size(); i++)
      if (a.copies[i].used && a.copies[i].placed && a.copies[i].place == (v & 0xFF)) {
        ESP_LOGD("panel", "Animation %s: copy %d at mark %u", a.name.c_str(), (int) i, (unsigned) (v >> 8 & 0xFF));
        if (marked)
          marked(a.name, (int) i, (int) (v >> 8 & 0xFF));
      }
}

// Every 20 ms, until deadline_us
inline void loop(uint32_t deadline_us) {
  if (!link::connected && (W.phase == COUNT || W.phase == MAKE))  // nothing is made without the panel
    stop();
  if (W.phase == IDLE) {
    if (!link::connected)
      return;
    for (auto &a : overlays)
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
  while (W.phase == COUNT && W.f < W.a->src->frames && before(deadline_us))
    count();
  if (W.phase == COUNT && W.f == W.a->src->frames && fits(deadline_us, UINT32_MAX, true))
    palette();
  for (bool fresh = true; W.phase == MAKE && before(deadline_us) && make(deadline_us, fresh); fresh = false) {
  }
  if (W.phase == SEND)
    send(deadline_us);
}

}  // namespace flexit::panel::overlay
