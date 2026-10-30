// Baseline JPEG encoder, always 4:2:2. For the screensaver photos with a restart marker after each MCU row, since the
// panel's TJpgDec decodes each 8-line strip on its own. Width a multiple of 16, height of 8. For the panel's
// screenshots a tile at a time, each its own restart interval, see pjpeg_tile().
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PJPEG_ALSO
#define PJPEG_ALSO 2  // other qualities whose sizes it can count along with the one it writes, 0 on the panel
#endif

typedef uint32_t (*pjpeg_pixel_t)(void *ctx, int x, int y);  // 0xRRGGBB

typedef struct {     // one quality's output
  uint8_t q[2][64];  // quantizers for luma and chroma, in natural order
  uint32_t bits;     // bits waiting to be written, nbits of them, at the bottom
  int nbits;
  uint32_t len;  // bytes so far, also past the end of out
  int dc[3];     // last block's DC for Y, Cb, Cr
  int quality;
} pjpeg_out_t;

typedef struct {
  uint16_t w, h, row, col;  // size, next MCU row to encode and x of its next MCU
  uint8_t *out;
  uint32_t cap;  // out's size
  uint64_t err;  // squared error so far, Y + Cb + Cr per pixel, in 256ths
  int n;         // outputs, o[0] goes to out and the others are only counted
  int drop;      // AC values this small or smaller become 0, see pjpeg_tile()
  pjpeg_out_t o[1 + PJPEG_ALSO];
} pjpeg_t;

// Writes the headers. quality 1..100
void pjpeg_begin(pjpeg_t *j, int w, int h, int quality, uint8_t *out, uint32_t cap);
// Right after pjpeg_begin: also counts the size at another quality, up to PJPEG_ALSO of them. Costs a fraction of an
// encode each, they share the transform.
void pjpeg_also(pjpeg_t *j, int quality);
// Encodes the next 16x8 pixels, and after the last of a row its restart marker. An MCU per call lets the ESP spread
// the work over its loop. False when done.
bool pjpeg_mcu(pjpeg_t *j, pjpeg_pixel_t px, void *ctx);
// Finishes the JPEG. Returns its length, over cap if it didn't fit. Each output's is in o[i].len.
uint32_t pjpeg_end(pjpeg_t *j);
// Only the headers, SOI to SOS, with a restart marker every restart MCUs. Returns their length, written as far as cap
// goes. A JPEG of tiles is these, the tiles with RST0..RST7 in turn between them and EOI.
uint32_t pjpeg_head(uint8_t *out, uint32_t cap, int w, int h, int quality, int restart);
// One restart interval: mcus MCUs from x, y on, rightwards, padded to a whole byte, without its marker. drop > 0
// makes each block's AC values up to that size 0, for a tile that has to fit cap: faint detail goes first, edges
// last. Returns its length, over cap if it didn't fit, then it stops. No state outside the call, the rest of the
// JPEG can be made elsewhere.
uint32_t pjpeg_tile(uint8_t *out, uint32_t cap, int quality, int drop, pjpeg_pixel_t px, void *ctx, int x, int y,
                    int mcus);
#define PJPEG_DC_ONLY 0x7FFF  // a drop that leaves each block only its average
// Mean squared error per pixel, Y + Cb + Cr. The ESP compares it with the 16-colour version to pick the better one.
double pjpeg_mse(const pjpeg_t *j);

#ifdef __cplusplus
}
#endif
