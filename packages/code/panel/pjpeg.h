// Baseline JPEG encoder for the screensaver photos. Always 4:2:2 with a restart marker after each MCU row, since
// the panel's TJpgDec decodes each 8-line strip on its own. Width a multiple of 16, height of 8.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*pjpeg_pixel_t)(void *ctx, int x, int y);  // 0xRRGGBB

typedef struct {
  uint16_t w, h, row;  // size, next MCU row to encode
  uint8_t *out;
  uint32_t cap, len;  // out's size, bytes so far
  bool full;          // out ran out of room
  uint32_t bits;      // bits waiting to be written, nbits of them, at the bottom
  int nbits;
  int dc[3];         // last block's DC for Y, Cb, Cr
  uint8_t q[2][64];  // quantizers for luma and chroma, in natural order
  double err;        // squared error so far, Y + Cb + Cr per pixel
} pjpeg_t;

// Writes the headers. quality 1..100
void pjpeg_begin(pjpeg_t *j, int w, int h, int quality, uint8_t *out, uint32_t cap);
// Encodes the next 8 lines. A row per call lets the ESP spread the work over its loop. False when done or full.
bool pjpeg_row(pjpeg_t *j, pjpeg_pixel_t px, void *ctx);
// Finishes the JPEG. Returns its length, 0 if it didn't fit
uint32_t pjpeg_end(pjpeg_t *j);
// Mean squared error per pixel, Y + Cb + Cr. The ESP compares it with the 16-colour version to pick the better one.
double pjpeg_mse(const pjpeg_t *j);

#ifdef __cplusplus
}
#endif
