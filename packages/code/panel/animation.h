// Screensaver animations from animation.yaml. All frames share 16 colours, one transparent if any pixel is, and
// each frame after the first is the deflated rectangle that changed. The panel keeps up to ANIM_SLOTS of them in
// 6 KB and shows each in up to ANIM_PLACES places, the copies of one in step unless a copy gets a frame of its own.
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "panel_link.h"
#include "pdeflate.h"
#include "picture.h"
#include "plink.h"

namespace flexit::panel::anim {

// x, y is where it goes on the screen, pixel(frame, x, y) gives 0xAARRGGBB, vx, vy a drift in px/s. flip sets its
// switch, so it follows what the actions show and clear. Its copies fade in and out over fade_ms.
struct Source {
  int w, h, frames, frame_ms, x, y, scale;
  std::function<uint32_t(int, int, int)> pixel;
  int vx = 0, vy = 0;
  std::function<void(bool)> flip = {};
  int fade_ms = 0;
};
inline std::map<std::string, Source> sources;

// frame_ms is sent in 10 ms steps up to 2550, and the panel shows a frame for 40 ms at least
inline void add(const std::string &name, Source s) {
  int ms = std::max(40, std::min(2550, s.frame_ms));
  if (ms != s.frame_ms) {
    ESP_LOGW("panel", "Animation %s: frame_ms %d is outside 40..2550, it plays at %d", name.c_str(), s.frame_ms, ms);
  }
  s.frame_ms = ms;
  sources[name] = std::move(s);
}

// An animated GIF (animation.yaml) and its switch
template<typename Img, typename Sw>
void add_animation(const std::string &name, Img &&image, Sw &&sw, int frame_ms, int x, int y, int scale, int vx, int vy,
                   int fade_ms) {
  auto *img = picture::ptr_of(image);
  auto *s = picture::ptr_of(sw);
  add(name, {img->get_width(), img->get_height(), (int) img->get_animation_frame_count(), frame_ms, x, y, scale,
             [img](int f, int px, int py) -> uint32_t {
               img->set_frame(f);
               auto c = img->get_pixel(px, py);
               return (uint32_t) c.w << 24 | (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
             },
             vx, vy, [s](bool on) { s->publish_state(on); }, std::max(0, std::min(65535, fade_ms))});
}

// A copy on the panel: one of its places
struct Copy {
  bool used = false, placed = false;  // placed: the panel has it, sent once the animation is in
  uint8_t place = 0;                  // the panel's place
  int16_t x = 0, y = 0;
  bool own = false;  // a frame of its own, out of step with the others
  uint8_t start = 0;
  uint8_t motion = ANIM_STOP;  // how it moves, with move or drift below
  struct {
    int16_t x, y;
    uint16_t ms;
  } move = {};
  struct {
    int16_t vx, vy;
  } drift = {};
  struct {
    uint16_t speed[2], leg[2], rest[2];  // min, max
    int16_t heading, spread;
  } wander = {};
  struct {
    int16_t x0 = 0, y0 = 0, x1 = 320, y1 = 240;
    uint8_t edge[2] = {ANIM_EDGE_SCREEN, ANIM_EDGE_SCREEN};
  } area;
  uint8_t flip = 0;             // ANIM_FLIP_X, ANIM_FLIP_Y
  uint8_t turning = ANIM_STOP;  // to angle over turn_ms, a spin, or ANIM_FACE
  int16_t angle = 0, spin = 0;  // degrees clockwise, degrees per second
  uint16_t turn_ms = 0;
  uint32_t turn_at = 0;  // millis() when it was asked for, so one sent again ends on time
  struct {
    uint8_t flags;  // ANIM_FACE_*
    int16_t facing, tilt, rate;
  } face = {};
  uint16_t size = 100;  // percent
  uint8_t opacity = 255;
  uint16_t rounds = 0;  // to the end of, 0 = on and on
  uint8_t tag = 0;      // of its ANIM_ROUNDS, the panel's ANIM_GONE has it
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
  std::string why;  // when it failed
  size_t frame() const { return (size_t) w / 2 * h; }
};
inline Anim anims[ANIM_SLOTS];

// count() builds the palette from all frames, make() the records, send() sends them. One animation at a time.
enum Phase { IDLE, COUNT, MAKE, SEND };

inline struct {
  Anim *a = nullptr;
  int phase = IDLE, f = 0, tries = 0;
  bool head = false, ended = false, any_transp = false;
  uint8_t id = 0;
  std::vector<uint32_t> hist, hash;         // hash: each frame's pixels, to find a GIF that repeats itself
  int frames = 0;                           // to make, a repeating GIF's first round only
  size_t last = 0;                          // the last record in data, 0 = only the first frame's
  std::vector<uint8_t> frame, prev, first;  // 2 pixels per byte
  uint32_t pos = 0;                         // bytes of data sent so far
  uint32_t end_ms = 0;                      // see picture::no_answer()
  std::vector<uint8_t> lut;                 // colour at 4 bits per channel -> palette index
} W;

inline bool sending() { return W.phase == SEND && W.head; }

inline Anim *find(const std::string &name) {
  for (auto &a : anims)
    if (!a.name.empty() && a.name == name)
      return &a;
  return nullptr;
}

// Bytes of the panel's ANIM_POOL the animations it has take, other than a
inline size_t used_by_others(const Anim *a) {
  size_t n = 0;
  for (auto &o : anims) {
    if (&o == a || o.name.empty() || o.state != SENT)
      continue;
    n += o.frame() + o.data.size();
    for (auto &c : o.copies)
      if (c.used && c.placed && c.own)
        n += o.frame();
  }
  return n;
}

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
          s += c.motion == ANIM_DRIFT    ? ", drifting"
               : c.motion == ANIM_WANDER ? ", wandering"
               : c.motion == ANIM_GLIDE  ? ", moved to " + std::to_string(c.move.x) + "," + std::to_string(c.move.y)
                                         : "";
    }
  }
  if (s.empty() && failed.empty())
    return "None";
  return (s.empty() ? "" : "Shown: " + s) + (s.empty() || failed.empty() ? "" : "; ") + failed;
}

inline void status(const std::string &s) {
  if (id(panel_animation_status).state != s)
    id(panel_animation_status).publish_state(s);
}

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
      if (c >> 24 < 128)
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

// Gives up on the one being made with the reason in the status, always false
inline bool fail(const std::string &why) {
  Anim *a = W.a;
  a->why = why;
  a->state = FAILED;
  W.head = false;
  W.phase = IDLE;
  W.a = nullptr;
  drop_work();
  picture::drop(a->data);
  status(shown());
  return false;
}

// The panel draws 2x at most, so only scale 1 can go to half size
inline bool can_halve() { return !W.a->half && W.a->src->scale == 1; }

inline std::string too_big(size_t n) {
  return (W.a->half ? "too big even at half size (over " : "too big at scale 2 (over ") + std::to_string(n) + " bytes)";
}

// ---- the link's messages

inline void send_move(const Copy &c) {
  uint8_t m[18] = {c.place, c.motion};
  if (c.motion == ANIM_WANDER) {
    const auto &w = c.wander;
    uint16_t v[8] = {w.speed[0], w.speed[1], (uint16_t) w.heading, (uint16_t) w.spread, w.leg[0], w.leg[1],
                     w.rest[0],  w.rest[1]};
    for (int i = 0; i < 8; i++)
      picture::put16(m + 2 + 2 * i, v[i]);
    link::send(K_ANIM_MOVE, m, 18);
    return;
  }
  picture::put16(m + 2, (uint16_t) (c.motion == ANIM_DRIFT ? c.drift.vx : c.move.x));
  picture::put16(m + 4, (uint16_t) (c.motion == ANIM_DRIFT ? c.drift.vy : c.move.y));
  picture::put16(m + 6, c.move.ms);
  link::send(K_ANIM_MOVE, m, c.motion == ANIM_GLIDE ? 8 : c.motion == ANIM_DRIFT ? 6 : 2);
}

// A sent turn keeps the time it had left
inline void send_turn(const Copy &c) {
  uint8_t m[10] = {c.place, c.turning, c.flip};
  if (c.turning == ANIM_FACE) {
    m[3] = c.face.flags;
    picture::put16(m + 4, (uint16_t) c.face.facing);
    picture::put16(m + 6, (uint16_t) c.face.tilt);
    picture::put16(m + 8, (uint16_t) c.face.rate);
    link::send(K_ANIM_TURN, m, 10);
    return;
  }
  uint32_t gone = millis() - c.turn_at;
  picture::put16(m + 3, (uint16_t) (c.turning == ANIM_DRIFT ? c.spin : c.angle));
  picture::put16(m + 5, (uint16_t) (gone < c.turn_ms ? c.turn_ms - gone : 0));
  link::send(K_ANIM_TURN, m, c.turning == ANIM_GLIDE ? 7 : c.turning == ANIM_DRIFT ? 5 : 3);
}

inline void send_area(const Copy &c) {
  uint8_t m[11] = {c.place};
  picture::put16(m + 1, (uint16_t) c.area.x0);
  picture::put16(m + 3, (uint16_t) c.area.y0);
  picture::put16(m + 5, (uint16_t) c.area.x1);
  picture::put16(m + 7, (uint16_t) c.area.y1);
  m[9] = c.area.edge[0];
  m[10] = c.area.edge[1];
  link::send(K_ANIM_AREA, m, sizeof m);
}

inline void send_size(const Copy &c, uint16_t ms) {
  uint8_t m[5] = {c.place};
  picture::put16(m + 1, c.size);
  picture::put16(m + 3, ms);
  link::send(K_ANIM_SIZE, m, sizeof m);
}

inline void send_fade(const Copy &c, uint16_t ms) {
  uint8_t m[4] = {c.place, c.opacity};
  picture::put16(m + 2, ms);
  link::send(K_ANIM_FADE, m, sizeof m);
}

inline void send_rounds(const Anim &a, const Copy &c) {
  uint8_t m[6] = {c.place};
  picture::put16(m + 1, c.rounds);
  picture::put16(m + 3, (uint16_t) a.src->fade_ms);
  m[5] = c.tag;
  link::send(K_ANIM_ROUNDS, m, sizeof m);
}

inline bool whole_screen(const Copy &c) {
  return c.area.edge[0] == ANIM_EDGE_SCREEN && c.area.edge[1] == ANIM_EDGE_SCREEN;
}

// Puts a copy on the panel, with its motion and turn. A frame of its own needs room in the panel's pool.
inline void send_place(Anim &a, Copy &c) {
  if (c.own) {
    size_t mine = a.frame() + a.data.size();
    for (auto &o : a.copies)
      if (o.used && o.placed && o.own && &o != &c)
        mine += a.frame();
    if (used_by_others(&a) + mine + a.frame() > ANIM_POOL) {
      ESP_LOGW("panel", "Animation %s: no room in the panel's %u bytes for a copy with a frame of its own",
               a.name.c_str(), (unsigned) ANIM_POOL);
      return;
    }
  }
  uint8_t m[10] = {c.place, (uint8_t) (&a - anims)};
  // the sizes round up at half size, so it moves in from the edge
  picture::put16(m + 2, (uint16_t) std::min<int>(c.x, 320 - a.w * a.scale));
  picture::put16(m + 4, (uint16_t) std::min<int>(c.y, 240 - a.h * a.scale));
  m[6] = c.own;
  m[7] = c.start;
  picture::put16(m + 8, (uint16_t) a.src->fade_ms);
  link::send(K_ANIM_PLACE, m, sizeof m);
  c.placed = true;
  if (!whole_screen(c))
    send_area(c);
  if (c.motion != ANIM_STOP)
    send_move(c);
  if (c.turning != ANIM_STOP || c.flip)
    send_turn(c);
  if (c.size != 100)
    send_size(c, 0);
  if (c.opacity != 255)
    send_fade(c, (uint16_t) a.src->fade_ms);  // from 0 as it fades in
  if (c.rounds)
    send_rounds(a, c);
}

// A slot, ANIM_PLACE_BIT | a place, or ANIM_ALL, faded out over ms first
inline void send_clear(unsigned what, int ms = 0) { link::send(K_ANIM_CLEAR, (uint32_t) what | (uint32_t) ms << 8); }

// ---- making one

// False if it doesn't fit on the screen or next to the others. Straight to half size if a frame alone is over
// what's left and it can.
inline bool start() {
  Anim &a = *W.a;
  const Source &s = *a.src;
  if (s.scale != 1 && s.scale != 2)
    return fail("scale has to be 1 or 2");
  if (s.frames < 1 || s.x < 0 || s.y < 0 || s.x + s.w * s.scale > 320 || s.y + s.h * s.scale > 240)
    return fail("doesn't fit on the screen at " + std::to_string(s.x) + "," + std::to_string(s.y));
  size_t room = ANIM_POOL - std::min<size_t>(ANIM_POOL, used_by_others(&a));
  if ((size_t) ((s.w + 1) & ~1) / 2 * s.h > room && can_halve())
    a.half = true;
  a.scale = s.scale * (a.half ? 2 : 1);
  a.w = ((a.half ? (s.w + 1) / 2 : s.w) + 1) & ~1;  // even, so rows are whole bytes
  a.h = a.half ? (s.h + 1) / 2 : s.h;
  if (a.frame() > room)
    return fail(too_big(a.frame()));
  if (!picture::has_ram(picture::COUNT_RAM, 4096 * 4))
    return fail("the ESP doesn't have the RAM to make it");
  W.hist.assign(4096, 0);
  W.lut.resize(4096);
  W.hash.clear();
  W.any_transp = false;
  W.f = 0;
  W.phase = COUNT;
  status("Making " + a.name);
  return true;
}

// Counts one frame's colours. After the last frame, builds the palette.
inline void count() {
  Anim &a = *W.a;
  uint32_t h = 2166136261u;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) {
      uint32_t c = px(W.f, x, y);
      h = (h ^ c) * 16777619u;
      if (c >> 24 < 128)
        W.any_transp = true;
      else
        W.hist[picture::bin(c)]++;
    }
  W.hash.push_back(h);
  if (++W.f < a.src->frames)
    return;
  int n = a.src->frames;
  W.frames = n;
  for (int p = 1; p < n && W.frames == n; p++)
    if (n % p == 0 && std::equal(W.hash.begin() + p, W.hash.end(), W.hash.begin()))
      W.frames = p;
  picture::drop(W.hash);
  std::vector<uint16_t> bins;
  memset(a.pal, 0, sizeof a.pal);
  int colours = picture::median_cut(W.hist, W.any_transp ? 15 : 16, a.pal, bins);
  a.transp = W.any_transp ? 15 : ANIM_OPAQUE;
  for (int i = 0; i < 4096; i++)
    W.lut[i] = (uint8_t) picture::nearest(picture::bin_rgb(i), a.pal, colours);
  picture::drop(W.hist);
  a.data.clear();
  W.f = 0;
  W.last = 0;
  W.phase = MAKE;
}

// Appends the record that turns `from` into `to`, or the whole frame if from is null. A frame like the one before
// adds its time to that one's record instead, unless it's the first frame's, which only plays once. False if there
// was no memory to compress it.
inline bool record(const std::vector<uint8_t> *from, const std::vector<uint8_t> &to) {
  Anim &a = *W.a;
  uint8_t delay = (uint8_t) (a.src->frame_ms / 10);
  if (from && W.last && *from == to && a.data[W.last] + delay <= 255) {
    a.data[W.last] = (uint8_t) (a.data[W.last] + delay);
    return true;
  }
  size_t at = a.data.size();
  a.data.resize(at + 5 + PDEFLATE_BOUND(a.frame()));
  unsigned n = plink_anim_record(from ? from->data() : nullptr, to.data(), a.w, a.h, delay, a.data.data() + at);
  a.data.resize(at + n);
  W.last = at;
  return n != 0;
}

// Maps one frame to the palette and adds its record, and after the last one back to the first frame. Stops as
// soon as it's over the room left or there's no memory to compress it, to try again at half size if it can.
inline void make() {
  Anim &a = *W.a;
  W.frame.assign(a.frame(), 0);
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) {
      uint32_t c = px(W.f, x, y);
      uint8_t i = c >> 24 < 128 ? a.transp : W.lut[picture::bin(c)];
      W.frame[(size_t) y * (a.w / 2) + x / 2] |= (uint8_t) (x & 1 ? i : i << 4);
    }
  bool ok = record(W.f ? &W.prev : nullptr, W.frame);
  if (!W.f)
    W.first = W.frame;
  W.prev.swap(W.frame);
  bool last = ++W.f >= W.frames;
  if (ok && last && W.f > 1)
    ok = record(&W.prev, W.first);
  size_t need = a.frame() + a.data.size(), room = ANIM_POOL - std::min<size_t>(ANIM_POOL, used_by_others(&a));
  if (ok && !last && need <= room)
    return;
  drop_work();
  if (!ok || need > room) {
    picture::drop(a.data);
    if (can_halve()) {
      if (ok) {
        ESP_LOGI("panel", "Animation %s: over %u bytes after %d of %d frames, making it at half size", a.name.c_str(),
                 (unsigned) room, W.f, W.frames);
      } else {
        ESP_LOGI("panel", "Animation %s: no memory to compress it, making it at half size", a.name.c_str());
      }
      a.half = true;
      start();
      return;
    }
    fail(ok ? too_big(need) : "no memory to compress it");
    return;
  }
  a.data.shrink_to_fit();
  ESP_LOGI("panel", "Animation %s: %dx%d%s, %d frames%s, %u bytes", a.name.c_str(), a.w, a.h, a.half ? " drawn 2x" : "",
           W.frames, W.frames < a.src->frames ? " (the rest repeat them)" : "", (unsigned) need);
  W.head = false;
  W.tries = 0;
  W.phase = SEND;
  status("Sending " + a.name);
}

// The header once the link is free, then the records as it has room, then END
inline void send() {
  Anim &a = *W.a;
  if (picture::no_answer(W.head && W.ended, W.end_ms)) {  // the panel lost it or its answer
    W.head = false;
    if (++W.tries <= 2) {
      ESP_LOGW("panel", "Animation %s: no answer from the panel, sending it again", a.name.c_str());
    } else {
      fail("the panel didn't answer");
    }
    return;
  }
  if (!W.head) {
    if (!link::connected || link::waiting() > link::BACKLOG || picture::sending())
      return;
    W.id = ++picture::transfer_ids;
    uint8_t h[60] = {W.id, (uint8_t) (&a - anims)};
    picture::put16(h + 2, a.w);
    picture::put16(h + 4, a.h);
    h[6] = (uint8_t) a.scale;
    h[7] = a.transp;
    picture::put32(h + 8, a.data.size());
    picture::put_palette(h + 12, a.pal);
    link::send(K_ANIM_BEGIN, h, sizeof h);
    W.head = true;
    W.ended = false;
    W.pos = 0;
  }
  picture::send_data(a.data.data(), a.data.size(), W.ended, picture::raw(a.data.data(), a.data.size(), W.pos));
}

// The panel has it: its copies go on
inline void done() {
  Anim &a = *W.a;
  W.phase = IDLE;
  W.head = false;
  W.tries = 0;
  W.a = nullptr;
  a.state = SENT;
  for (auto &c : a.copies)
    if (c.used)
      send_place(a, c);
  status(shown());
}

// The panel's K_PIC_ACK = status << 24 | id << 16 | bytes received, from panel.h
inline void ack(uint32_t v) {
  if (!sending() || (v >> 16 & 0xFF) != W.id)
    return;  // for a picture or an earlier transfer
  Anim &a = *W.a;
  uint8_t st = v >> 24;
  if (st == PIC_PROGRESS) {
    status("Sending " + a.name + ": " + std::to_string(std::min<uint32_t>(99, (v & 0xFFFF) * 100 / a.data.size())) +
           " %");
  } else if (st == PIC_DONE) {
    done();
  } else if (st == PIC_TOO_BIG && can_halve()) {
    a.half = true;
    W.head = false;
    picture::drop(a.data);
    start();
  } else if ((st == PIC_BAD || st == PIC_BAD_CRC) && ++W.tries <= 2) {
    W.head = false;  // send it all again
  } else {
    fail("the panel didn't take it (" + std::to_string(st) + ")");
  }
}

// The panel restarted and lost them all, so they go again, failed ones from scratch
inline void restarted() {
  W.head = false;
  W.phase = IDLE;
  W.a = nullptr;
  drop_work();
  for (auto &a : anims) {
    if (a.name.empty())
      continue;
    if (a.state == FAILED) {
      a.half = false;
      picture::drop(a.data);
    }
    a.state = WANT;
    for (auto &c : a.copies)
      c.placed = false;
  }
}

// ---- what to show

// A free place on the panel, -1 if all are taken
inline int free_place() {
  for (int p = 0; p < ANIM_PLACES; p++) {
    bool taken = false;
    for (auto &a : anims)
      for (auto &c : a.copies)
        taken |= c.used && c.place == p;
    if (!taken)
      return p;
  }
  return -1;
}

// Copy i of a, made if it wasn't there. nullptr if the panel has no place left for it.
inline Copy *copy_of(Anim &a, int i) {
  if (i < 0 || i >= ANIM_PLACES)
    return nullptr;
  if ((int) a.copies.size() <= i)
    a.copies.resize(i + 1);
  Copy &c = a.copies[i];
  if (!c.used) {
    int p = free_place();
    if (p < 0) {
      ESP_LOGW("panel", "Animation %s: the panel shows %d copies at most", a.name.c_str(), ANIM_PLACES);
      return nullptr;
    }
    c = Copy();
    c.used = true;
    c.place = (uint8_t) p;
  }
  return &c;
}

// Takes a slot for name, with copy 0 where the yaml puts it and its drift. The one there already, or nullptr.
inline Anim *add_anim(const std::string &name) {
  if (Anim *a = find(name))
    return a;
  auto it = sources.find(name);
  if (it == sources.end()) {
    ESP_LOGW("panel", "No animation called '%s'", name.c_str());
    return nullptr;
  }
  Anim *a = nullptr;
  for (auto &o : anims)
    if (o.name.empty() && !a)
      a = &o;
  if (!a) {
    ESP_LOGW("panel", "No room for animation %s: the panel holds %d at a time", name.c_str(), ANIM_SLOTS);
    return nullptr;
  }
  *a = Anim();
  a->name = name;
  a->src = &it->second;
  const Source &s = *a->src;
  if (Copy *c = copy_of(*a, 0)) {
    c->x = (int16_t) s.x;
    c->y = (int16_t) s.y;
    c->drift = {(int16_t) s.vx, (int16_t) s.vy};
    c->motion = s.vx || s.vy ? ANIM_DRIFT : ANIM_STOP;
  }
  return a;
}

inline void drop_anim(Anim &a, bool send = true) {
  if (W.a == &a) {
    W.head = false;
    W.phase = IDLE;
    W.a = nullptr;
    drop_work();
  }
  if (a.state == SENT && send)
    send_clear((unsigned) (&a - anims), a.src->fade_ms);
  if (a.src->flip)
    a.src->flip(false);
  a = Anim();
}

// Clears one animation and all its copies, or all of them with no name, fading them out
inline void clear(const std::string &name) {
  if (name.empty() || name == "None") {
    int ms = 0;
    for (auto &a : anims)
      if (!a.name.empty()) {
        ms = std::max(ms, a.src->fade_ms);
        drop_anim(a, false);
      }
    send_clear(ANIM_ALL, ms);
  } else if (Anim *a = find(name)) {
    drop_anim(*a);
  }
  status(shown());
}

inline void clear() { clear(""); }

// panel_show_animation and the switches: shows it where its yaml puts it, alongside the others. "" clears them all.
inline void show(const std::string &name) {
  if (name.empty() || name == "None") {
    clear("");
    return;
  }
  Anim *a = add_anim(name);
  if (!a)
    return;
  if (a->state == FAILED) {  // once more
    a->state = WANT;
    a->half = false;
    picture::drop(a->data);
  }
  if (a->src->flip)
    a->src->flip(true);
  status(shown());
}

// The copy's motion goes out now if the panel has it, else with it once it's there
inline void moved(Anim &a, Copy &c) {
  if (a.state == SENT && c.placed)
    send_move(c);
  status(shown());
}

inline bool sent(const std::string &name, const Copy &c) { return find(name)->state == SENT && c.placed; }

inline int16_t clamp16(int v) { return (int16_t) std::max(-32768, std::min(32767, v)); }
inline uint16_t clamp_u16(int v) { return (uint16_t) std::max(0, std::min(65535, v)); }

inline void turned(Anim &a, Copy &c) {
  if (a.state == SENT && c.placed)
    send_turn(c);
}

// panel_place_animation: copy i of name at x, y, own = a frame of its own started start frames in. Shows the
// animation if it isn't yet.
inline void place(const std::string &name, int i, int x, int y, bool own, int start) {
  Anim *a = find(name);
  if (!a) {
    a = add_anim(name);
    if (!a)
      return;
    if (i != 0 && !a->copies.empty())
      a->copies[0].used = false;  // only the copy asked for
    if (a->src->flip)
      a->src->flip(true);
  }
  Copy *c = copy_of(*a, i);
  if (!c)
    return;
  const Source &s = *a->src;
  int w = s.w * s.scale, h = s.h * s.scale;
  if (x < 0 || y < 0 || x + w > 320 || y + h > 240) {
    ESP_LOGW("panel", "Animation %s: copy %d doesn't fit on the screen at %d,%d", name.c_str(), i, x, y);
    return;
  }
  c->x = (int16_t) x;
  c->y = (int16_t) y;
  c->own = own;
  c->start = (uint8_t) std::max(0, std::min(255, start));
  Copy fresh;
  c->motion = ANIM_STOP;
  c->turning = ANIM_STOP;
  c->flip = 0;
  c->area = fresh.area;
  c->size = 100;
  c->opacity = 255;
  c->rounds = 0;
  c->placed = false;
  if (a->state == SENT)
    send_place(*a, *c);
  status(shown());
}

// panel_remove_animation: copy i of name goes, the animation stays on the panel
inline void remove(const std::string &name, int i) {
  Anim *a = find(name);
  if (!a || i < 0 || i >= (int) a->copies.size() || !a->copies[i].used)
    return;
  Copy &c = a->copies[i];
  if (a->state == SENT && c.placed)
    send_clear(ANIM_PLACE_BIT | c.place, a->src->fade_ms);
  c = Copy();
  status(shown());
}

inline Copy *copy_for(const std::string &name, int i, const char *what) {
  Anim *a = find(name);
  if (!a || i < 0 || i >= (int) a->copies.size() || !a->copies[i].used) {
    ESP_LOGW("panel", "%s: no copy %d of animation '%s' shown", what, i, name.c_str());
    return nullptr;
  }
  return &a->copies[i];
}

// For panel_move_animation: glide copy i to x, y over ms, 0 jumps
inline void move(const std::string &name, int i, int x, int y, int ms) {
  Copy *c = copy_for(name, i, "panel_move_animation");
  if (!c)
    return;
  c->motion = ANIM_GLIDE;
  c->move = {(int16_t) std::max(-32768, std::min(32767, x)), (int16_t) std::max(-32768, std::min(32767, y)),
             (uint16_t) std::max(0, std::min(65535, ms))};
  if (c->move.x != x || c->move.y != y || c->move.ms != ms) {
    ESP_LOGW("panel", "panel_move_animation: %d,%d over %d ms is out of range, it goes to %d,%d over %d ms", x, y, ms,
             c->move.x, c->move.y, c->move.ms);
  }
  moved(*find(name), *c);
}

// For panel_drift_animation: vx, vy in px/s, wrapping round the screen. 0, 0 stops it where it is.
inline void drift(const std::string &name, int i, int vx, int vy) {
  Copy *c = copy_for(name, i, "panel_drift_animation");
  if (!c)
    return;
  c->drift = {(int16_t) std::max(-32768, std::min(32767, vx)), (int16_t) std::max(-32768, std::min(32767, vy))};
  c->motion = vx || vy ? ANIM_DRIFT : ANIM_STOP;
  moved(*find(name), *c);
}

// For panel_turn_animation: copy i turns to angle, degrees clockwise, the short way round over ms. 0 is at once.
inline void turn(const std::string &name, int i, int angle, int ms) {
  Copy *c = copy_for(name, i, "panel_turn_animation");
  if (!c)
    return;
  c->turning = ANIM_GLIDE;
  c->angle = (int16_t) ((angle % 360 + 360) % 360);
  c->turn_ms = (uint16_t) std::max(0, std::min(65535, ms));
  c->turn_at = millis();
  turned(*find(name), *c);
}

// For panel_spin_animation: degrees per second clockwise, negative the other way. 0 stops it where it is.
inline void spin(const std::string &name, int i, int speed) {
  Copy *c = copy_for(name, i, "panel_spin_animation");
  if (!c)
    return;
  c->spin = (int16_t) std::max(-32768, std::min(32767, speed));
  c->turning = speed ? ANIM_DRIFT : ANIM_STOP;
  turned(*find(name), *c);
}

// For panel_flip_animation: mirrored left to right, upside down, or both
inline void flip(const std::string &name, int i, bool x, bool y) {
  Copy *c = copy_for(name, i, "panel_flip_animation");
  if (!c)
    return;
  c->flip = (uint8_t) ((x ? ANIM_FLIP_X : 0) | (y ? ANIM_FLIP_Y : 0));
  turned(*find(name), *c);
}

// For panel_area_animation: where a drift or wander keeps. Each edge is screen, cross or bounce.
inline void area(const std::string &name, int i, int left, int top, int right, int bottom, const std::string &edge_x,
                 const std::string &edge_y) {
  Copy *c = copy_for(name, i, "panel_area_animation");
  if (!c)
    return;
  auto edge = [](const std::string &e) -> int {
    return e == "screen" ? ANIM_EDGE_SCREEN : e == "cross" ? ANIM_EDGE_CROSS : e == "bounce" ? ANIM_EDGE_BOUNCE : -1;
  };
  int ex = edge(edge_x), ey = edge(edge_y);
  if (ex < 0 || ey < 0 || right <= left || bottom <= top) {
    ESP_LOGW("panel",
             "panel_area_animation: %d,%d to %d,%d with edges %s and %s: the edges are screen, cross or "
             "bounce, and right and bottom past left and top",
             left, top, right, bottom, edge_x.c_str(), edge_y.c_str());
    return;
  }
  c->area.x0 = clamp16(left);
  c->area.y0 = clamp16(top);
  c->area.x1 = clamp16(right);
  c->area.y1 = clamp16(bottom);
  c->area.edge[0] = (uint8_t) ex;
  c->area.edge[1] = (uint8_t) ey;
  if (sent(name, *c))
    send_area(*c);
}

// For panel_wander_animation: legs of speed_min..speed_max px/s toward heading +- spread for leg_min..leg_max ms,
// with rests of rest_min..rest_max ms between
inline void wander(const std::string &name, int i, int speed_min, int speed_max, int heading, int spread, int leg_min,
                   int leg_max, int rest_min, int rest_max) {
  Copy *c = copy_for(name, i, "panel_wander_animation");
  if (!c)
    return;
  if (speed_max < speed_min || leg_max < leg_min || rest_max < rest_min) {
    ESP_LOGW("panel", "panel_wander_animation: each max has to be at least its min");
    return;
  }
  auto &w = c->wander;
  w.speed[0] = (uint16_t) std::max(0, std::min(10000, speed_min));
  w.speed[1] = (uint16_t) std::max(0, std::min(10000, speed_max));
  w.heading = (int16_t) ((heading % 360 + 360) % 360);
  w.spread = (int16_t) std::max(0, std::min(180, spread));
  w.leg[0] = clamp_u16(leg_min);
  w.leg[1] = clamp_u16(leg_max);
  w.rest[0] = clamp_u16(rest_min);
  w.rest[1] = clamp_u16(rest_max);
  c->motion = ANIM_WANDER;
  moved(*find(name), *c);
}

// For panel_face_animation: turns toward where it goes, at most tilt degrees from facing, at rate degrees per second
inline void face(const std::string &name, int i, int facing, bool mirror, int tilt, int rate, bool level) {
  Copy *c = copy_for(name, i, "panel_face_animation");
  if (!c)
    return;
  c->turning = ANIM_FACE;
  c->face.flags = (uint8_t) ((mirror ? ANIM_FACE_MIRROR : 0) | (level ? ANIM_FACE_LEVEL : 0));
  c->face.facing = (int16_t) ((facing % 360 + 360) % 360);
  c->face.tilt = (int16_t) std::max(0, std::min(180, tilt));
  c->face.rate = (int16_t) std::max(0, std::min(32767, rate));
  if (sent(name, *c))
    send_turn(*c);
}

// For panel_size_animation: copy i to size percent (10..400) round its middle over ms, 0 at once
inline void size(const std::string &name, int i, int size, int ms) {
  Copy *c = copy_for(name, i, "panel_size_animation");
  if (!c)
    return;
  if (size < 10 || size > 400) {
    ESP_LOGW("panel", "panel_size_animation: %d %% is outside 10..400", size);
    return;
  }
  c->size = (uint16_t) size;
  if (sent(name, *c))
    send_size(*c, clamp_u16(ms));
}

// For panel_fade_animation: copy i to opacity percent over ms, 0 at once
inline void fade(const std::string &name, int i, int opacity, int ms) {
  Copy *c = copy_for(name, i, "panel_fade_animation");
  if (!c)
    return;
  c->opacity = (uint8_t) ((std::max(0, std::min(100, opacity)) * 255 + 50) / 100);
  if (sent(name, *c))
    send_fade(*c, clamp_u16(ms));
}

inline uint8_t tags;  // so an old ANIM_GONE doesn't take a copy placed again

// For panel_rounds_animation: plays to the end of rounds rounds, then fades out and goes. 0 plays on.
inline void rounds(const std::string &name, int i, int n) {
  Copy *c = copy_for(name, i, "panel_rounds_animation");
  if (!c)
    return;
  c->rounds = clamp_u16(n);
  c->tag = ++tags;
  if (sent(name, *c))
    send_rounds(*find(name), *c);
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
  if (!link::connected && (W.phase == COUNT || W.phase == MAKE)) {  // nothing is made without the panel
    W.a->state = WANT;
    picture::drop(W.a->data);
    W.phase = IDLE;
    W.a = nullptr;
    drop_work();
  }
  if (W.phase == IDLE) {
    if (!link::connected)
      return;
    for (auto &a : anims)
      if (!a.name.empty() && a.state == WANT) {
        W.a = &a;
        if (!a.data.empty()) {  // made already, sent again after a panel restart
          if (a.frame() + a.data.size() > ANIM_POOL - std::min<size_t>(ANIM_POOL, used_by_others(&a))) {
            fail("no room next to the others");
            return;
          }
          W.head = false;
          W.tries = 0;
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
