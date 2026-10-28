// A GIF89a writer for the camera's picture with its animations, see picture_gif.h. One table of 128 colours, frames
// left in place, looping for ever. The LZW goes a batch of pixels per step, so the ESP can spread the work over its
// loop. Plain C++ without ESPHome.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flexit::panel::picture {

struct Gif {
  static constexpr int COLOURS = 128;
  static constexpr unsigned CLEAR = COLOURS, STOP = CLEAR + 1, FIRST = STOP + 1;
  // The table of codes, prefix << 20 | colour << 12 | code, 0 = empty. Cleared once it's 3/4 full, so it's never
  // searched long. 16 KB.
  static constexpr unsigned TABLE = 4096, LIMIT = FIRST + TABLE * 3 / 4;
  using Pixel = uint8_t (*)(void *ctx, int x, int y);  // an index into the colours

  uint8_t *out = nullptr;
  uint32_t cap = 0, len = 0;  // len goes past cap, nothing is written there
  uint32_t whole = 0;         // where the last whole frame ends
  int frames = 0;             // whole ones
  // The frame being written: where it is, the next pixel, where its delay goes
  int fx = 0, fy = 0, fw = 0, fh = 0, x = 0, y = 0;
  uint32_t delay_at = 0;
  std::vector<uint32_t> table;
  unsigned next = FIRST, bits = 8;
  int prefix = -1;
  uint32_t acc = 0;  // bits waiting, n of them
  int n = 0;
  uint8_t sub[255];  // the data sub-block being filled
  int nsub = 0;

  // A w x h GIF of the n colours in pal (0xRRGGBB) into buf, as much as fits cap
  void begin(int w, int h, const uint32_t *pal, int n, uint8_t *buf, uint32_t cap);
  // The next frame, the rectangle at x, y, shown for delay_cs hundredths until delay() says otherwise
  void frame(int x, int y, int w, int h, unsigned delay_cs = 0);
  // Changes the delay of the last frame begun
  void delay(unsigned cs);
  // Up to count of the frame's pixels, false once the frame is done
  bool step(Pixel px, void *ctx, int count);
  bool full() const { return len + 1 > cap; }  // nothing more fits, with the trailer
  // The trailer after the last whole frame. Returns the GIF's length, over cap if not even the first frame fit.
  // Frees the table.
  uint32_t end();

  void put(uint8_t b);
  void put16(unsigned v);
  void code(unsigned c);
  void flush_sub();
  void reset_table();
};

}  // namespace flexit::panel::picture
