// see pjpeg.h
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
enum { DC_Y, AC_Y, DC_C, AC_C };

// each table's code and code length per symbol, made from the tables above on first use
static uint16_t code[4][256];
static uint8_t code_len[4][256];

static void make_codes(void) {
  if (code_len[DC_Y][0])
    return;
  for (int t = 0; t < 4; t++) {
    unsigned c = 0, k = 0;
    for (int len = 1; len <= 16; len++, c <<= 1)
      for (int i = 0; i < HUFF_BITS[t][len - 1]; i++) {
        uint8_t s = HUFF_SYMBOLS[t][k++];
        code[t][s] = (uint16_t) c++;
        code_len[t][s] = (uint8_t) len;
      }
  }
}

static void byte(pjpeg_t *j, unsigned b) {
  if (j->len < j->cap)
    j->out[j->len] = (uint8_t) b;
  else
    j->full = true;
  j->len++;
}

static void word(pjpeg_t *j, unsigned w) {
  byte(j, w >> 8);
  byte(j, w & 255);
}

// n bits of v into the entropy-coded data, a 0 after every FF byte
static void put(pjpeg_t *j, unsigned v, int n) {
  j->bits = j->bits << n | (v & ((1u << n) - 1));
  j->nbits += n;
  while (j->nbits >= 8) {
    unsigned b = j->bits >> (j->nbits - 8) & 255;
    byte(j, b);
    if (b == 0xFF)
      byte(j, 0);
    j->nbits -= 8;
  }
  j->bits &= (1u << j->nbits) - 1;
}

static void flush(pjpeg_t *j) {  // the last byte padded with 1s
  if (j->nbits)
    put(j, 0x7F, 8 - j->nbits);
}

static int bit_length(int v) {
  int n = 0;
  for (v = v < 0 ? -v : v; v; v >>= 1)
    n++;
  return n;
}

static void put_value(pjpeg_t *j, int v, int n) { put(j, (unsigned) (v < 0 ? v - 1 : v), n); }

// One 8x8 block of level-shifted samples in 16ths. comp is 0 Y, 1 Cb, 2 Cr. weight is how many pixels a sample
// stands for.
static void block(pjpeg_t *j, const int32_t *in, int comp, int weight) {
  int32_t t[64], f[64];
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
        s += C[v][y] * t[y * 8 + u];         // under 2^31: 2.83 * 8192 * 46341 at most
      f[v * 8 + u] = (s + (1 << 15)) >> 16;  // in 16ths
    }
  const uint8_t *q = j->q[comp ? 1 : 0];
  int z[64];
  for (int k = 0; k < 64; k++) {
    int n = ZIGZAG[k], d = q[n] * 16, v = f[n];
    z[k] = v < 0 ? -((d / 2 - v) / d) : (v + d / 2) / d;  // rounded half away from zero
    int e = v - z[k] * d;
    j->err += (uint32_t) (weight * e * e);
  }
  int dc = comp ? DC_C : DC_Y, ac = comp ? AC_C : AC_Y;
  int diff = z[0] - j->dc[comp], n = bit_length(diff);
  j->dc[comp] = z[0];
  put(j, code[dc][n], code_len[dc][n]);
  if (n)
    put_value(j, diff, n);
  int run = 0;
  for (int k = 1; k < 64; k++) {
    if (!z[k]) {
      run++;
      continue;
    }
    for (; run > 15; run -= 16)
      put(j, code[ac][0xF0], code_len[ac][0xF0]);  // 16 zeros
    n = bit_length(z[k]);
    int s = run << 4 | n;
    put(j, code[ac][s], code_len[ac][s]);
    put_value(j, z[k], n);
    run = 0;
  }
  if (run)
    put(j, code[ac][0], code_len[ac][0]);  // the rest are zeros
}

void pjpeg_begin(pjpeg_t *j, int w, int h, int quality, uint8_t *out, uint32_t cap) {
  make_codes();
  *j = (pjpeg_t) {.w = (uint16_t) w, .h = (uint16_t) h, .out = out, .cap = cap};
  quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;
  int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;  // as libjpeg does
  for (int t = 0; t < 2; t++)
    for (int k = 0; k < 64; k++) {
      int v = (QUANT[t][k] * scale + 50) / 100;
      j->q[t][k] = (uint8_t) (v < 1 ? 1 : v > 255 ? 255 : v);
    }
  word(j, 0xFFD8);  // SOI
  word(j, 0xFFDB);  // DQT: both tables, zigzag order
  word(j, 2 + 2 * 65);
  for (int t = 0; t < 2; t++) {
    byte(j, (unsigned) t);
    for (int k = 0; k < 64; k++)
      byte(j, j->q[t][ZIGZAG[k]]);
  }
  word(j, 0xFFC0);  // SOF0: 8 bits, h, w, Y at 2x1 with table 0, Cb and Cr at 1x1 with table 1
  word(j, 17);
  byte(j, 8);
  word(j, (unsigned) h);
  word(j, (unsigned) w);
  byte(j, 3);
  static const uint8_t COMPS[9] = {1, 0x21, 0, 2, 0x11, 1, 3, 0x11, 1};
  for (int i = 0; i < 9; i++)
    byte(j, COMPS[i]);
  word(j, 0xFFC4);  // DHT: all four
  word(j, 2 + 4 * 17 + 2 * 12 + 2 * 162);
  static const uint8_t CLASS_ID[4] = {0x00, 0x10, 0x01, 0x11};
  for (int t = 0; t < 4; t++) {
    byte(j, CLASS_ID[t]);
    int n = 0;
    for (int i = 0; i < 16; i++) {
      byte(j, HUFF_BITS[t][i]);
      n += HUFF_BITS[t][i];
    }
    for (int i = 0; i < n; i++)
      byte(j, HUFF_SYMBOLS[t][i]);
  }
  word(j, 0xFFDD);  // DRI: one MCU row
  word(j, 4);
  word(j, (unsigned) w / 16);
  word(j, 0xFFDA);  // SOS: Y with tables 0, Cb and Cr with tables 1
  word(j, 12);
  byte(j, 3);
  static const uint8_t SCAN[9] = {1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
  for (int i = 0; i < 9; i++)
    byte(j, SCAN[i]);
}

bool pjpeg_mcu(pjpeg_t *j, pjpeg_pixel_t px, void *ctx) {
  if (j->full || j->row * 8u >= j->h)
    return false;
  int x0 = j->col, y0 = j->row * 8;
  int32_t lum[2][64], cb[64], cr[64];  // level-shifted, in 16ths
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 16; x += 2) {
      int32_t c[2][3];  // level-shifted Y, Cb, Cr in 65536ths, of the two pixels that share a chroma sample
      for (int k = 0; k < 2; k++) {
        uint32_t p = px(ctx, x0 + x + k, y0 + y);
        int32_t r = (int32_t) (p >> 16 & 255), g = (int32_t) (p >> 8 & 255), b = (int32_t) (p & 255);
        c[k][0] = 19595 * r + 38470 * g + 7471 * b - (128 << 16);
        c[k][1] = -11058 * r - 21710 * g + 32768 * b;
        c[k][2] = 32768 * r - 27439 * g - 5329 * b;
        lum[(x + k) / 8][y * 8 + (x + k) % 8] = (c[k][0] + 2048) >> 12;
      }
      int32_t mb = (c[0][1] + c[1][1] + 4096) >> 13, mr = (c[0][2] + c[1][2] + 4096) >> 13;
      cb[y * 8 + x / 2] = mb;
      cr[y * 8 + x / 2] = mr;
      for (int k = 0; k < 2; k++) {  // what sharing the chroma sample costs
        int32_t eb = ((c[k][1] + 2048) >> 12) - mb, er = ((c[k][2] + 2048) >> 12) - mr;
        j->err += (uint32_t) (eb * eb + er * er);
      }
    }
  block(j, lum[0], 0, 1);
  block(j, lum[1], 0, 1);
  block(j, cb, 1, 2);
  block(j, cr, 2, 2);
  if (j->full)
    return false;
  j->col += 16;
  if (j->col < j->w)
    return true;
  j->col = 0;
  j->row++;
  if (j->row * 8u < j->h) {  // RSTn after MCU row n, the next row starts fresh
    flush(j);
    byte(j, 0xFF);
    byte(j, 0xD0 + (j->row - 1u) % 8);
    j->dc[0] = j->dc[1] = j->dc[2] = 0;
  }
  return !j->full && j->row * 8u < j->h;
}

uint32_t pjpeg_end(pjpeg_t *j) {
  flush(j);
  word(j, 0xFFD9);  // EOI
  return j->full ? 0 : j->len;
}

double pjpeg_mse(const pjpeg_t *j) { return j->w && j->h ? (double) j->err / 256 / ((double) j->w * j->h) : 0; }
