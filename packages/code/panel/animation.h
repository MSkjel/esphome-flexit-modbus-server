// Screensaver animations from animation.yaml. All frames share 16 colours, one transparent if any pixel is, and
// each frame after the first is the deflated rectangle that changed. The panel keeps and plays it all from 6 KB.
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

// x, y is where it goes on the screen, pixel(frame, x, y) gives 0xAARRGGBB
struct Source {
  int w, h, frames, frame_ms, x, y, scale;
  std::function<uint32_t(int, int, int)> pixel;
};
inline std::map<std::string, Source> sources;

inline void add(const std::string &name, Source s) { sources[name] = std::move(s); }

// An animated GIF (animation.yaml)
template<typename Img> void add_animation(const std::string &name, Img &&image, int frame_ms, int x, int y, int scale) {
  auto *img = picture::ptr_of(image);
  add(name, {img->get_width(), img->get_height(), (int) img->get_animation_frame_count(), frame_ms, x, y, scale,
             [img](int f, int px, int py) -> uint32_t {
               img->set_frame(f);
               auto c = img->get_pixel(px, py);
               return (uint32_t) c.w << 24 | (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
             }});
}

// count() builds the palette from all frames, make() the records
enum Phase { IDLE, COUNT, MAKE, SEND };

inline struct {
  std::string want, name;  // the one asked for, "" = none, and the one the panel shows
  const Source *src = nullptr;
  int phase = IDLE, f = 0, tries = 0, w = 0, h = 0, scale = 1;
  bool half = false, failed = false, head = false, ended = false, any_transp = false;
  uint8_t transp = ANIM_OPAQUE, id = 0;
  uint32_t pal[16] = {};
  std::vector<uint32_t> hist;
  std::vector<uint8_t> frame, prev, first;  // 2 pixels per byte
  std::vector<uint8_t> data;                // the frame records
  uint32_t pos = 0;                         // bytes of data sent so far
  uint32_t end_ms = 0;                      // see picture::no_answer()
  uint8_t lut[4096];
} A;

inline bool sending() { return A.phase == SEND && A.head; }

inline void status(const std::string &s) {
  if (id(panel_animation_status).state != s)
    id(panel_animation_status).publish_state(s);
}

// 0xAARRGGBB. At half size the 2x2 average, transparent if 3 or more of the 4 are.
inline uint32_t px(int f, int x, int y) {
  const Source &s = *A.src;
  if (!A.half)
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

// False if there's no such animation or it doesn't fit on the screen where it goes. Straight to half size if a
// frame alone is over ANIM_POOL.
inline bool start() {
  auto it = sources.find(A.want);
  if (it == sources.end()) {
    status(A.want + ": no animation with that name");
    A.failed = true;
    return false;
  }
  const Source &s = *(A.src = &it->second);
  if ((size_t) ((s.w + 1) & ~1) / 2 * s.h > ANIM_POOL)
    A.half = true;
  A.scale = s.scale * (A.half ? 2 : 1);
  A.w = ((A.half ? (s.w + 1) / 2 : s.w) + 1) & ~1;  // even, so rows are whole bytes
  A.h = A.half ? (s.h + 1) / 2 : s.h;
  if (s.frames < 1 || A.h > 255 || s.x + A.w * A.scale > 320 || s.y + A.h * A.scale > 240) {
    status(A.want + ": doesn't fit on the screen at " + std::to_string(s.x) + "," + std::to_string(s.y));
    A.failed = true;
    return false;
  }
  size_t frame = (size_t) A.w / 2 * A.h;
  if (frame > ANIM_POOL) {
    status(A.want + ": too big even at half size (over " + std::to_string(frame) + " bytes)");
    A.failed = true;
    return false;
  }
  A.hist.assign(4096, 0);
  A.any_transp = false;
  A.f = 0;
  A.phase = COUNT;
  status("Making " + A.want);
  return true;
}

// Counts one frame's colours. After the last frame, builds the palette.
inline void count() {
  for (int y = 0; y < A.h; y++)
    for (int x = 0; x < A.w; x++) {
      uint32_t c = px(A.f, x, y);
      if (c >> 24 < 128)
        A.any_transp = true;
      else
        A.hist[picture::bin(c)]++;
    }
  if (++A.f < A.src->frames)
    return;
  std::vector<uint16_t> bins;
  memset(A.pal, 0, sizeof A.pal);
  int colours = picture::median_cut(A.hist, A.any_transp ? 15 : 16, A.pal, bins);
  A.transp = A.any_transp ? 15 : ANIM_OPAQUE;
  for (int i = 0; i < 4096; i++)
    A.lut[i] = (uint8_t) picture::nearest(picture::bin_rgb(i), A.pal, colours);
  picture::drop(A.hist);
  A.data.clear();
  A.f = 0;
  A.phase = MAKE;
}

// Appends the record that turns `from` into `to`, or the whole frame if from is null. False if there was no
// memory to compress it.
inline bool record(const std::vector<uint8_t> *from, const std::vector<uint8_t> &to) {
  size_t at = A.data.size();
  A.data.resize(at + 5 + PDEFLATE_BOUND((size_t) A.w / 2 * A.h));
  uint8_t delay = (uint8_t) std::max(1, std::min(255, A.src->frame_ms / 10));
  unsigned n = plink_anim_record(from ? from->data() : nullptr, to.data(), A.w, A.h, delay, A.data.data() + at);
  A.data.resize(at + n);
  return n != 0;
}

// Maps one frame to the palette and adds its record, and after the last one back to the first frame. Stops as
// soon as it's over ANIM_POOL or there's no memory to compress it, to try again at half size.
inline void make() {
  A.frame.assign((size_t) A.w / 2 * A.h, 0);
  for (int y = 0; y < A.h; y++)
    for (int x = 0; x < A.w; x++) {
      uint32_t c = px(A.f, x, y);
      uint8_t i = c >> 24 < 128 ? A.transp : A.lut[picture::bin(c)];
      A.frame[(size_t) y * (A.w / 2) + x / 2] |= (uint8_t) (x & 1 ? i : i << 4);
    }
  bool ok = record(A.f ? &A.prev : nullptr, A.frame);
  if (!A.f)
    A.first = A.frame;
  A.prev.swap(A.frame);
  bool last = ++A.f >= A.src->frames;
  if (ok && last && A.f > 1)
    ok = record(&A.prev, A.first);
  size_t need = (size_t) A.w / 2 * A.h + A.data.size();
  if (ok && !last && need <= ANIM_POOL)
    return;
  picture::drop(A.frame);
  picture::drop(A.prev);
  picture::drop(A.first);
  if (!ok || need > ANIM_POOL) {
    picture::drop(A.data);
    if (!A.half) {
      if (ok) {
        ESP_LOGI("panel", "Animation %s: over %u bytes after %d of %d frames, making it at half size", A.want.c_str(),
                 (unsigned) ANIM_POOL, A.f, A.src->frames);
      } else {
        ESP_LOGI("panel", "Animation %s: no memory to compress it, making it at half size", A.want.c_str());
      }
      A.half = true;
      start();
      return;
    }
    status(A.want + (ok ? ": too big even at half size (over " + std::to_string(need) + " bytes)"
                        : std::string(": no memory to compress it")));
    A.failed = true;
    A.phase = IDLE;
    return;
  }
  A.data.shrink_to_fit();
  ESP_LOGI("panel", "Animation %s: %dx%d%s, %d frames, %u bytes", A.want.c_str(), A.w, A.h, A.half ? " drawn 2x" : "",
           A.src->frames, (unsigned) need);
  A.head = false;
  A.tries = 0;
  A.phase = SEND;
  status("Sending " + A.want);
}

// The header once the link is free, then the records as it has room, then END
inline void send() {
  if (picture::no_answer(A.head && A.ended, A.end_ms)) {  // the panel lost it or its answer
    A.head = false;
    if (++A.tries <= 2) {
      ESP_LOGW("panel", "Animation %s: no answer from the panel, sending it again", A.want.c_str());
    } else {
      status(A.want + ": the panel didn't answer");
      A.failed = true;
      A.phase = IDLE;
    }
    return;
  }
  if (!A.head) {
    if (!link::connected || link::waiting() > link::BACKLOG || picture::sending())
      return;
    A.id = ++picture::transfer_ids;
    uint8_t h[63] = {A.id};
    picture::put16(h + 1, A.w);
    picture::put16(h + 3, A.h);
    h[5] = (uint8_t) A.scale;
    picture::put16(h + 6, A.src->x);
    picture::put16(h + 8, A.src->y);
    h[10] = A.transp;
    picture::put32(h + 11, A.data.size());
    picture::put_palette(h + 15, A.pal);
    link::send(K_ANIM_BEGIN, h, sizeof h);
    A.head = true;
    A.ended = false;
    A.pos = 0;
  }
  picture::send_data(A.data.data(), A.data.size(), A.pos, A.ended);
}

// The panel's K_PIC_ACK = status << 24 | id << 16 | bytes received, from panel.h
inline void ack(uint32_t a) {
  if (!sending() || (a >> 16 & 0xFF) != A.id)
    return;  // for a picture or an earlier transfer
  uint8_t st = a >> 24;
  if (st == PIC_PROGRESS) {
    status("Sending " + A.want + ": " + std::to_string(std::min<uint32_t>(99, (a & 0xFFFF) * 100 / A.data.size())) +
           " %");
  } else if (st == PIC_DONE) {
    A.phase = IDLE;
    A.head = false;
    A.name = A.want;
    status("Shown: " + A.name);
  } else if (st == PIC_TOO_BIG && !A.half) {
    A.half = true;
    A.head = false;
    start();
  } else if ((st == PIC_BAD || st == PIC_BAD_CRC) && ++A.tries <= 2) {
    A.head = false;  // send it all again
  } else {
    status(A.want + ": the panel didn't take it (" + std::to_string(st) + ")");
    A.failed = true;
    A.head = false;
    A.phase = IDLE;
  }
}

// The panel restarted and lost the animation, so send what it had or was getting again
inline void restarted() {
  A.head = false;
  if (A.phase == IDLE && !A.name.empty() && !A.data.empty())
    A.phase = SEND;
}

inline void clear() {
  A.want.clear();
  A.name.clear();
  picture::drop(A.data);
  A.failed = A.head = false;
  A.phase = IDLE;
  link::send(K_ANIM_CLEAR, 0);
  id(panel_animation).publish_state("None");
  status("None");
}

// For panel_show_animation and the Panel Animation select. "" is none.
inline void show(const std::string &name) {
  if (name.empty()) {
    clear();
    return;
  }
  if (!sources.count(name)) {
    ESP_LOGW("panel", "No animation called '%s'", name.c_str());
    return;
  }
  if (id(panel_animation).has_option(name))
    id(panel_animation).publish_state(name);
  if (name == A.want && !A.failed)
    return;
  A.want = name;
  A.name.clear();
  A.half = A.failed = A.head = false;
  A.phase = IDLE;
}

// every 20 ms
inline void loop() {
  if (A.want.empty() || A.failed)
    return;
  uint32_t t0 = micros();
  if (A.phase == IDLE && A.name != A.want && !start())
    return;
  while (A.phase == COUNT && micros() - t0 < picture::SLICE_US)
    count();
  while (A.phase == MAKE && micros() - t0 < picture::SLICE_US)
    make();
  if (A.phase == SEND)
    send();
}

}  // namespace flexit::panel::anim
