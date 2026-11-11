// see pjpeg.h
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#include "pjpeg.h"

// The DCT's basis times 8192: C[u][x] = c(u) / 2 * cos((2x + 1)u pi / 16), c(0) = 1/sqrt(2), else 1. It's
// orthonormal, so the squared error of the quantized coefficients is the squared error of the pixels. Integer maths
// throughout, the ESP32-C3 has no FPU.
static const int16_t C[8][8] = {
    {2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896},     {4017, 3406, 2276, 799, -799, -2276, -3406, -4017},
    {3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784}, {3406, -799, -4017, -2276, 2276, 4017, 799, -3406},
    {2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896}, {2276, -4017, 799, 3406, -3406, -799, 4017, -2276},
    {1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567}, {799, -2276, 3406, -4017, 4017, -3406, 2276, -799},
};

// natural index of each zigzag position
static const uint8_t ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                   12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                   35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                   58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// Quantizers at quality 50, natural order. The example tables from ITU T.81 Annex K
static const uint8_t QUANT[2][64] = {
    {16, 11,  10,  16, 24, 40, 51, 61, 12,  12,  14,  19,  26, 58, 60, 55,  14,  13,  16,  24, 40, 57,
     69, 56,  14,  17, 22, 29, 51, 87, 80,  62,  18,  22,  37, 56, 68, 109, 103, 77,  24,  35, 55, 64,
     81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98,  112, 100, 103, 99},
    {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
     99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99},
};

// Huffman tables: DC luma, AC luma, DC chroma, AC chroma. Codes per length 1..16, then the symbols in order.
static const uint8_t HUFF_BITS[4][16] = {
    {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
    {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d},
    {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77},
};
static const uint8_t DC_SYMBOLS[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t AC_LUMA[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
static const uint8_t AC_CHROMA[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
static const uint8_t *const HUFF_SYMBOLS[4] = {DC_SYMBOLS, AC_LUMA, DC_SYMBOLS, AC_CHROMA};

// The codes from the tables above as T.81 Annex C makes them, written length << 16 | code and kept as the two. DC by
// the value's size, AC by run * 11 + size, luma first. TJpgDec decoding what we write checks them, see
// sim/unit_test/picture.c.
typedef struct __attribute__((packed)) {
  uint16_t bits;
  uint8_t len;
} huff_code_t;
#define H(c) {(uint16_t) (c), (c) >> 16}
static const huff_code_t DC_CODES[2][12] = {{H(0x20000), H(0x30002), H(0x30003), H(0x30004), H(0x30005), H(0x30006),
                                             H(0x4000e), H(0x5001e), H(0x6003e), H(0x7007e), H(0x800fe), H(0x901fe)},
                                            {H(0x20000), H(0x20001), H(0x20002), H(0x30006), H(0x4000e), H(0x5001e),
                                             H(0x6003e), H(0x7007e), H(0x800fe), H(0x901fe), H(0xa03fe), H(0xb07fe)}};
static const huff_code_t AC_CODES[2][16 * 11] = {
    {H(0x4000a),  H(0x20000),  H(0x20001),  H(0x30004),  H(0x4000b),  H(0x5001a),  H(0x70078),  H(0x800f8),
     H(0xa03f6),  H(0x10ff82), H(0x10ff83), H(0x00000),  H(0x4000c),  H(0x5001b),  H(0x70079),  H(0x901f6),
     H(0xb07f6),  H(0x10ff84), H(0x10ff85), H(0x10ff86), H(0x10ff87), H(0x10ff88), H(0x00000),  H(0x5001c),
     H(0x800f9),  H(0xa03f7),  H(0xc0ff4),  H(0x10ff89), H(0x10ff8a), H(0x10ff8b), H(0x10ff8c), H(0x10ff8d),
     H(0x10ff8e), H(0x00000),  H(0x6003a),  H(0x901f7),  H(0xc0ff5),  H(0x10ff8f), H(0x10ff90), H(0x10ff91),
     H(0x10ff92), H(0x10ff93), H(0x10ff94), H(0x10ff95), H(0x00000),  H(0x6003b),  H(0xa03f8),  H(0x10ff96),
     H(0x10ff97), H(0x10ff98), H(0x10ff99), H(0x10ff9a), H(0x10ff9b), H(0x10ff9c), H(0x10ff9d), H(0x00000),
     H(0x7007a),  H(0xb07f7),  H(0x10ff9e), H(0x10ff9f), H(0x10ffa0), H(0x10ffa1), H(0x10ffa2), H(0x10ffa3),
     H(0x10ffa4), H(0x10ffa5), H(0x00000),  H(0x7007b),  H(0xc0ff6),  H(0x10ffa6), H(0x10ffa7), H(0x10ffa8),
     H(0x10ffa9), H(0x10ffaa), H(0x10ffab), H(0x10ffac), H(0x10ffad), H(0x00000),  H(0x800fa),  H(0xc0ff7),
     H(0x10ffae), H(0x10ffaf), H(0x10ffb0), H(0x10ffb1), H(0x10ffb2), H(0x10ffb3), H(0x10ffb4), H(0x10ffb5),
     H(0x00000),  H(0x901f8),  H(0xf7fc0),  H(0x10ffb6), H(0x10ffb7), H(0x10ffb8), H(0x10ffb9), H(0x10ffba),
     H(0x10ffbb), H(0x10ffbc), H(0x10ffbd), H(0x00000),  H(0x901f9),  H(0x10ffbe), H(0x10ffbf), H(0x10ffc0),
     H(0x10ffc1), H(0x10ffc2), H(0x10ffc3), H(0x10ffc4), H(0x10ffc5), H(0x10ffc6), H(0x00000),  H(0x901fa),
     H(0x10ffc7), H(0x10ffc8), H(0x10ffc9), H(0x10ffca), H(0x10ffcb), H(0x10ffcc), H(0x10ffcd), H(0x10ffce),
     H(0x10ffcf), H(0x00000),  H(0xa03f9),  H(0x10ffd0), H(0x10ffd1), H(0x10ffd2), H(0x10ffd3), H(0x10ffd4),
     H(0x10ffd5), H(0x10ffd6), H(0x10ffd7), H(0x10ffd8), H(0x00000),  H(0xa03fa),  H(0x10ffd9), H(0x10ffda),
     H(0x10ffdb), H(0x10ffdc), H(0x10ffdd), H(0x10ffde), H(0x10ffdf), H(0x10ffe0), H(0x10ffe1), H(0x00000),
     H(0xb07f8),  H(0x10ffe2), H(0x10ffe3), H(0x10ffe4), H(0x10ffe5), H(0x10ffe6), H(0x10ffe7), H(0x10ffe8),
     H(0x10ffe9), H(0x10ffea), H(0x00000),  H(0x10ffeb), H(0x10ffec), H(0x10ffed), H(0x10ffee), H(0x10ffef),
     H(0x10fff0), H(0x10fff1), H(0x10fff2), H(0x10fff3), H(0x10fff4), H(0xb07f9),  H(0x10fff5), H(0x10fff6),
     H(0x10fff7), H(0x10fff8), H(0x10fff9), H(0x10fffa), H(0x10fffb), H(0x10fffc), H(0x10fffd), H(0x10fffe)},
    {H(0x20000),  H(0x20001),  H(0x30004),  H(0x4000a),  H(0x50018),  H(0x50019),  H(0x60038),  H(0x70078),
     H(0x901f4),  H(0xa03f6),  H(0xc0ff4),  H(0x00000),  H(0x4000b),  H(0x60039),  H(0x800f6),  H(0x901f5),
     H(0xb07f6),  H(0xc0ff5),  H(0x10ff88), H(0x10ff89), H(0x10ff8a), H(0x10ff8b), H(0x00000),  H(0x5001a),
     H(0x800f7),  H(0xa03f7),  H(0xc0ff6),  H(0xf7fc2),  H(0x10ff8c), H(0x10ff8d), H(0x10ff8e), H(0x10ff8f),
     H(0x10ff90), H(0x00000),  H(0x5001b),  H(0x800f8),  H(0xa03f8),  H(0xc0ff7),  H(0x10ff91), H(0x10ff92),
     H(0x10ff93), H(0x10ff94), H(0x10ff95), H(0x10ff96), H(0x00000),  H(0x6003a),  H(0x901f6),  H(0x10ff97),
     H(0x10ff98), H(0x10ff99), H(0x10ff9a), H(0x10ff9b), H(0x10ff9c), H(0x10ff9d), H(0x10ff9e), H(0x00000),
     H(0x6003b),  H(0xa03f9),  H(0x10ff9f), H(0x10ffa0), H(0x10ffa1), H(0x10ffa2), H(0x10ffa3), H(0x10ffa4),
     H(0x10ffa5), H(0x10ffa6), H(0x00000),  H(0x70079),  H(0xb07f7),  H(0x10ffa7), H(0x10ffa8), H(0x10ffa9),
     H(0x10ffaa), H(0x10ffab), H(0x10ffac), H(0x10ffad), H(0x10ffae), H(0x00000),  H(0x7007a),  H(0xb07f8),
     H(0x10ffaf), H(0x10ffb0), H(0x10ffb1), H(0x10ffb2), H(0x10ffb3), H(0x10ffb4), H(0x10ffb5), H(0x10ffb6),
     H(0x00000),  H(0x800f9),  H(0x10ffb7), H(0x10ffb8), H(0x10ffb9), H(0x10ffba), H(0x10ffbb), H(0x10ffbc),
     H(0x10ffbd), H(0x10ffbe), H(0x10ffbf), H(0x00000),  H(0x901f7),  H(0x10ffc0), H(0x10ffc1), H(0x10ffc2),
     H(0x10ffc3), H(0x10ffc4), H(0x10ffc5), H(0x10ffc6), H(0x10ffc7), H(0x10ffc8), H(0x00000),  H(0x901f8),
     H(0x10ffc9), H(0x10ffca), H(0x10ffcb), H(0x10ffcc), H(0x10ffcd), H(0x10ffce), H(0x10ffcf), H(0x10ffd0),
     H(0x10ffd1), H(0x00000),  H(0x901f9),  H(0x10ffd2), H(0x10ffd3), H(0x10ffd4), H(0x10ffd5), H(0x10ffd6),
     H(0x10ffd7), H(0x10ffd8), H(0x10ffd9), H(0x10ffda), H(0x00000),  H(0x901fa),  H(0x10ffdb), H(0x10ffdc),
     H(0x10ffdd), H(0x10ffde), H(0x10ffdf), H(0x10ffe0), H(0x10ffe1), H(0x10ffe2), H(0x10ffe3), H(0x00000),
     H(0xb07f9),  H(0x10ffe4), H(0x10ffe5), H(0x10ffe6), H(0x10ffe7), H(0x10ffe8), H(0x10ffe9), H(0x10ffea),
     H(0x10ffeb), H(0x10ffec), H(0x00000),  H(0xe3fe0),  H(0x10ffed), H(0x10ffee), H(0x10ffef), H(0x10fff0),
     H(0x10fff1), H(0x10fff2), H(0x10fff3), H(0x10fff4), H(0x10fff5), H(0xa03fa),  H(0xf7fc3),  H(0x10fff6),
     H(0x10fff7), H(0x10fff8), H(0x10fff9), H(0x10fffa), H(0x10fffb), H(0x10fffc), H(0x10fffd), H(0x10fffe)}};

#undef H

static const uint32_t EOB = 0, ZRL = 15 * 11;  // AC_CODES of the rest are zeros and of 16 zeros

// A byte of output o. Only o[0] is kept, as far as there's room
static void byte(pjpeg_t *j, pjpeg_out_t *o, unsigned b) {
  if (o == j->o && o->len < j->cap)
    j->out[o->len] = (uint8_t) b;
  o->len++;
}

static void word(pjpeg_t *j, unsigned w) {  // the headers, the others count them from o[0]
  byte(j, j->o, w >> 8);
  byte(j, j->o, w & 255);
}

// n bits of v into the entropy-coded data, a 0 after every FF byte
static void put(pjpeg_t *j, pjpeg_out_t *o, unsigned v, int n) {
  o->bits = o->bits << n | (v & ((1u << n) - 1));
  o->nbits += n;
  while (o->nbits >= 8) {
    unsigned b = o->bits >> (o->nbits - 8) & 255;
    byte(j, o, b);
    if (b == 0xFF)
      byte(j, o, 0);
    o->nbits -= 8;
  }
  o->bits &= (1u << o->nbits) - 1;
}

static void flush(pjpeg_t *j, pjpeg_out_t *o) {  // the last byte padded with 1s
  if (o->nbits)
    put(j, o, 0x7F, 8 - o->nbits);
}

static int bit_length(int v) {
  int n = 0;
  for (v = v < 0 ? -v : v; v; v >>= 1)
    n++;
  return n;
}

static void put_value(pjpeg_t *j, pjpeg_out_t *o, int v, int n) { put(j, o, (unsigned) (v < 0 ? v - 1 : v), n); }

static void code(pjpeg_t *j, pjpeg_out_t *o, huff_code_t c) { put(j, o, c.bits, c.len); }

// One block's coefficients f quantized for output o and Huffman coded, one after the other in zigzag order. The
// squared error is for pjpeg_mse(), which only one who tries several qualities asks for.
static void quantize(pjpeg_t *j, pjpeg_out_t *o, const int16_t *f, int comp, int weight) {
  const uint8_t *q = o->q[comp ? 1 : 0];
  const huff_code_t *dc = DC_CODES[comp > 0], *ac = AC_CODES[comp > 0];
  uint32_t err = 0;
  int run = 0;
  for (int k = 0; k < 64; k++) {
    int i = ZIGZAG[k], d = q[i] * 16, v = f[i];
    int z = v <= -d / 2 ? -((d / 2 - v) / d) : v >= d / 2 ? (v + d / 2) / d : 0;  // half away from 0, 11 bits at most
    if (k && z >= -j->drop && z <= j->drop)
      z = 0;  // a tile that has to fit, see pjpeg_tile()
    if (PJPEG_ALSO)
      err += (uint32_t) ((v - z * d) * (v - z * d));
    if (!k) {
      int diff = z - o->dc[comp], n = bit_length(diff);
      o->dc[comp] = z;
      code(j, o, dc[n]);
      if (n)
        put_value(j, o, diff, n);
    } else if (!z) {
      run++;
    } else {
      for (; run > 15; run -= 16)
        code(j, o, ac[ZRL]);
      int n = bit_length(z);
      code(j, o, ac[run * 11 + n]);
      put_value(j, o, z, n);
      run = 0;
    }
  }
  if (run)
    code(j, o, ac[EOB]);
  if (PJPEG_ALSO && o == j->o)
    j->err += (uint64_t) err * weight;
}

// One 8x8 block of level-shifted samples in 16ths, coded for each output. comp is 0 Y, 1 Cb, 2 Cr. weight is how
// many pixels a sample stands for. The coefficients, within +-16380 in 16ths, take the samples' place.
static void block(pjpeg_t *j, int16_t *in, int comp, int weight) {
  int32_t t[64];
  for (int y = 0; y < 8; y++)
    for (int u = 0; u < 8; u++) {
      int32_t s = 0;
      for (int x = 0; x < 8; x++)
        s += in[y * 8 + x] * C[u][x];
      t[y * 8 + u] = (s + (1 << 9)) >> 10;  // in 128ths
    }
  for (int v = 0; v < 8; v++)
    for (int u = 0; u < 8; u++) {
      int32_t s = 0;
      for (int y = 0; y < 8; y++)
        s += C[v][y] * t[y * 8 + u];  // under 2^31: 2.83 * 8192 * 46341 at most
      in[v * 8 + u] = (int16_t) ((s + (1 << 15)) >> 16);
    }
  for (pjpeg_out_t *o = j->o; o < j->o + j->n; o++)
    quantize(j, o, in, comp, weight);
}

static void quantizers(pjpeg_out_t *o, int quality) {
  o->quality = quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
  int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;  // as libjpeg does
  for (int t = 0; t < 2; t++)
    for (int k = 0; k < 64; k++) {
      int v = (QUANT[t][k] * scale + 50) / 100;
      o->q[t][k] = (uint8_t) (v < 1 ? 1 : v > 255 ? 255 : v);
    }
}

// SOI to SOS, a restart marker every restart MCUs
static void head(pjpeg_t *j, int restart) {
  int w = j->w, h = j->h;
  word(j, 0xFFD8);  // SOI
  word(j, 0xFFDB);  // DQT: both tables, zigzag order
  word(j, 2 + 2 * 65);
  for (int t = 0; t < 2; t++) {
    byte(j, j->o, (unsigned) t);
    for (int k = 0; k < 64; k++)
      byte(j, j->o, j->o->q[t][ZIGZAG[k]]);
  }
  word(j, 0xFFC0);  // SOF0: 8 bits, h, w, Y at 2x1 with table 0, Cb and Cr at 1x1 with table 1
  word(j, 17);
  byte(j, j->o, 8);
  word(j, (unsigned) h);
  word(j, (unsigned) w);
  byte(j, j->o, 3);
  static const uint8_t COMPS[9] = {1, 0x21, 0, 2, 0x11, 1, 3, 0x11, 1};
  for (int i = 0; i < 9; i++)
    byte(j, j->o, COMPS[i]);
  word(j, 0xFFC4);  // DHT: all four
  word(j, 2 + 4 * 17 + 2 * 12 + 2 * 162);
  static const uint8_t CLASS_ID[4] = {0x00, 0x10, 0x01, 0x11};
  for (int t = 0; t < 4; t++) {
    byte(j, j->o, CLASS_ID[t]);
    int n = 0;
    for (int i = 0; i < 16; i++) {
      byte(j, j->o, HUFF_BITS[t][i]);
      n += HUFF_BITS[t][i];
    }
    for (int i = 0; i < n; i++)
      byte(j, j->o, HUFF_SYMBOLS[t][i]);
  }
  word(j, 0xFFDD);  // DRI
  word(j, 4);
  word(j, (unsigned) restart);
  word(j, 0xFFDA);  // SOS: Y with tables 0, Cb and Cr with tables 1
  word(j, 12);
  byte(j, j->o, 3);
  static const uint8_t SCAN[9] = {1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
  for (int i = 0; i < 9; i++)
    byte(j, j->o, SCAN[i]);
}

static void start(pjpeg_t *j, int w, int h, int quality, uint8_t *out, uint32_t cap) {
  *j = (pjpeg_t) {.w = (uint16_t) w, .h = (uint16_t) h, .out = out, .cap = cap, .n = 1};
  quantizers(j->o, quality);
}

void pjpeg_begin(pjpeg_t *j, int w, int h, int quality, uint8_t *out, uint32_t cap) {
  start(j, w, h, quality, out, cap);
  head(j, w / 16);
}

uint32_t pjpeg_head(uint8_t *out, uint32_t cap, int w, int h, int quality, int restart) {
  pjpeg_t j;
  start(&j, w, h, quality, out, cap);
  head(&j, restart);
  return j.o[0].len;
}

void pjpeg_also(pjpeg_t *j, int quality) {
  if (j->n > PJPEG_ALSO)
    return;
  pjpeg_out_t *o = &j->o[j->n++];
  quantizers(o, quality);
  o->len = j->o[0].len;  // the same headers, other numbers in the DQT
}

// The 16x8 pixels at x0, y0 coded for each output
static void mcu(pjpeg_t *j, pjpeg_pixel_t px, void *ctx, int x0, int y0) {
  int16_t lum[2][64], cb[64], cr[64];  // level-shifted, in 16ths, within +-2048
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 16; x += 2) {
      int32_t c[2][3];  // level-shifted Y, Cb, Cr in 65536ths, of the two pixels that share a chroma sample
      for (int k = 0; k < 2; k++) {
        uint32_t p = px(ctx, x0 + x + k, y0 + y);
        int32_t r = (int32_t) (p >> 16 & 255), g = (int32_t) (p >> 8 & 255), b = (int32_t) (p & 255);
        c[k][0] = 19595 * r + 38470 * g + 7471 * b - (128 << 16);
        c[k][1] = -11058 * r - 21710 * g + 32768 * b;
        c[k][2] = 32768 * r - 27439 * g - 5329 * b;
        lum[(x + k) / 8][y * 8 + (x + k) % 8] = (int16_t) ((c[k][0] + 2048) >> 12);
      }
      int32_t mb = (c[0][1] + c[1][1] + 4096) >> 13, mr = (c[0][2] + c[1][2] + 4096) >> 13;
      cb[y * 8 + x / 2] = (int16_t) mb;
      cr[y * 8 + x / 2] = (int16_t) mr;
      for (int k = 0; PJPEG_ALSO && k < 2; k++) {  // what sharing the chroma sample costs
        int32_t eb = ((c[k][1] + 2048) >> 12) - mb, er = ((c[k][2] + 2048) >> 12) - mr;
        j->err += (uint32_t) (eb * eb + er * er);
      }
    }
  block(j, lum[0], 0, 1);
  block(j, lum[1], 0, 1);
  block(j, cb, 1, 2);
  block(j, cr, 2, 2);
}

bool pjpeg_mcu(pjpeg_t *j, pjpeg_pixel_t px, void *ctx) {
  if (j->row * 8u >= j->h)
    return false;
  mcu(j, px, ctx, j->col, j->row * 8);
  j->col += 16;
  if (j->col < j->w)
    return true;
  j->col = 0;
  j->row++;
  if (j->row * 8u < j->h)  // RSTn after MCU row n, the next row starts fresh
    for (pjpeg_out_t *o = j->o; o < j->o + j->n; o++) {
      flush(j, o);
      byte(j, o, 0xFF);
      byte(j, o, 0xD0 + (j->row - 1u) % 8);
      o->dc[0] = o->dc[1] = o->dc[2] = 0;
    }
  return j->row * 8u < j->h;
}

uint32_t pjpeg_end(pjpeg_t *j) {
  for (pjpeg_out_t *o = j->o; o < j->o + j->n; o++) {
    flush(j, o);
    byte(j, o, 0xFF);  // EOI
    byte(j, o, 0xD9);
  }
  return j->o[0].len;
}

uint32_t pjpeg_tile(uint8_t *out, uint32_t cap, int quality, int drop, pjpeg_pixel_t px, void *ctx, int x, int y,
                    int mcus) {
  pjpeg_t j;
  start(&j, 0, 0, quality, out, cap);
  j.drop = drop;
  for (int i = 0; i < mcus && j.o[0].len <= cap; i++)
    mcu(&j, px, ctx, x + 16 * i, y);
  flush(&j, j.o);
  return j.o[0].len;
}

double pjpeg_mse(const pjpeg_t *j) { return j->w && j->h ? (double) j->err / 256 / ((double) j->w * j->h) : 0; }
