// Raw deflate, see pdeflate.h. pinflate is small and checks every read and write: it takes the bits from a word,
// reverses the next 15 so a code's first bit is on top, and finds the code's length by where that falls between
// the lengths' limits. pdeflate makes one block, LZ77 with hash chains and one step of lazy matching, then stored,
// fixed or own Huffman codes, whichever is smallest.
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#include "pdeflate.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Lengths 3..258 and distances 1..32768: base and extra bits of each code
static const uint16_t LBASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                   33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// The order the lengths of the code length code come in
static const uint8_t CLORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// Where byte i of the rectangle is
static uint32_t at(uint32_t i, unsigned width, unsigned stride) { return i / width * stride + i % width; }

// ---------------------------------------------------------------- inflate
#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
static const uint8_t REV[256] = {R6(0), R6(2), R6(1), R6(3)};  // each byte with its bits the other way round

typedef struct {
  uint16_t limit[17];  // codes up to l bits, filled out to 15 bits, are below limit[l]. limit[16] stops the search
  int16_t offs[16];    // a code of l bits is symbol[offs[l] + code]
  uint8_t start[16];   // the shortest code that starts with each 4 bits, where the search starts
  const uint16_t *symbol;
} huff_t;

// The fixed codes. Literals and lengths: 7 bits for 256-279, 8 for 0-143 and 280-287, 9 for 144-255. Distances: 5
// bits, the symbol is the code, from the 0-143 run.
#define S4(n) n, n + 1, n + 2, n + 3
#define S16(n) S4(n), S4(n + 4), S4(n + 8), S4(n + 12)
static const uint16_t FIX_SYM[288] = {S16(256), S4(272),  S4(276),  S16(0),   S16(16),  S16(32),  S16(48),
                                      S16(64),  S16(80),  S16(96),  S16(112), S16(128), S4(280),  S4(284),
                                      S16(144), S16(160), S16(176), S16(192), S16(208), S16(224), S16(240)};
static const huff_t FIX_LIT = {
    {0, 0, 0, 0, 0, 0, 0, 24 << 8, 200 << 7, 32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768},
    {0, 0, 0, 0, 0, 0, 0, 0, -24, -224},
    {7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9},
    FIX_SYM};
static const huff_t FIX_DIST = {{0, 0, 0, 0, 0, 30 << 10, 30 << 10, 30 << 10, 30 << 10, 30 << 10, 30 << 10, 30 << 10,
                                 30 << 10, 30 << 10, 30 << 10, 30 << 10, 32768},
                                {0},
                                {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},
                                FIX_SYM + 24};

typedef struct {
  const uint8_t *in, *start, *end;
  uint32_t bits;             // bits read but not used yet, nbits of them, at the bottom
  int nbits, pad;            // pad: how many of those are the zeros past the end
  bool short_;               // the data ended early
  uint8_t *out, *o;          // o: where the next byte goes, when out isn't NULL
  unsigned width, skip;      // bytes per row, stride - width
  unsigned col;              // o's column
  bool flat;                 // rows one after the other, no gaps
  uint32_t len, done, hist;  // bytes to make, bytes made, bytes before out that matches may reach
} inf_t;

static inline void fill(inf_t *s) {  // at least 25 bits
  for (; s->nbits <= 24; s->nbits += 8) {
    if (s->in < s->end)
      s->bits |= (uint32_t) *s->in++ << s->nbits;
    else
      s->pad += 8;
  }
}

static inline void drop(inf_t *s, int n) {
  s->bits >>= n;
  s->nbits -= n;
  s->short_ |= s->nbits < s->pad;
}

static inline unsigned getbits(inf_t *s, int need) {  // up to 16
  if (s->nbits < need)
    fill(s);
  unsigned v = s->bits & ((1u << need) - 1);
  drop(s, need);
  return v;
}

// Builds h from n code lengths, its symbols into sym. < 0 = too many codes, 0 = complete, > 0 = some left over.
static int build(huff_t *h, uint16_t *sym, const uint8_t *lens, int n) {
  int16_t count[16] = {0}, offs[16];
  for (int i = 0; i < n; i++)
    count[lens[i]]++;
  int left = 1, first = 0, index = 0;
  for (int l = 1; l < 16; l++) {
    left = (left << 1) - count[l];
    if (left < 0)
      return left;
    h->offs[l] = (int16_t) (index - first);
    offs[l] = (int16_t) index;
    index += count[l];
    first += count[l];
    h->limit[l] = (uint16_t) (first << (15 - l));
    first <<= 1;
  }
  h->limit[16] = 32768;
  for (int p = 0, l = 1; p < 16; h->start[p++] = (uint8_t) l)
    while ((unsigned) p << 11 >= h->limit[l])
      l++;
  for (int i = 0; i < n; i++)
    if (lens[i])
      sym[offs[lens[i]]++] = (uint16_t) i;
  h->symbol = sym;
  return count[0] == n ? 0 : left;  // no codes: nothing will decode
}

// The next symbol, -1 if the bits aren't a code
static inline int decode(inf_t *s, const huff_t *h) {
  if (s->nbits < 15)
    fill(s);
  unsigned r = (unsigned) (REV[s->bits & 255] << 7 | REV[s->bits >> 8 & 255] >> 1), l = h->start[r >> 11];
  while (r >= h->limit[l])
    l++;
  if (l > 15)
    return -1;
  drop(s, (int) l);
  return h->symbol[h->offs[l] + (int) (r >> (15 - l))];
}

static inline void emit(inf_t *s, uint8_t b) {
  *s->o++ = b;
  if (++s->col == s->width) {
    s->col = 0;
    s->o += s->skip;
  }
}

// The symbols of a block, up to its end code
static bool codes(inf_t *z, const huff_t *lit, const huff_t *dist) {
  inf_t t = *z, *s = &t;  // in registers while it lasts
  bool ok = false;
  for (;;) {
    int sym = decode(s, lit);
    if (s->short_ || sym < 0)
      break;
    if (sym < 256) {
      if (s->done == s->len)
        break;
      if (s->out)
        emit(s, (uint8_t) sym);
      s->done++;
      continue;
    }
    if (sym == 256) {
      ok = true;
      break;
    }
    sym -= 257;
    if (sym >= 29)
      break;
    uint32_t n = LBASE[sym] + getbits(s, LEXT[sym]);
    int d = decode(s, dist);
    if (s->short_ || d < 0 || d >= 30)
      break;
    uint32_t back = DBASE[d] + getbits(s, DEXT[d]);
    if (s->short_ || back > s->done + s->hist || n > s->len - s->done)
      break;
    if (s->out && s->flat) {  // also into hist
      for (uint8_t *o = s->o, *f = o - back, *e = o + n; o < e;)
        *o++ = *f++;
      s->o += n;
    } else if (s->out) {
      uint32_t j = s->done - back, fcol = j % s->width;
      const uint8_t *f = s->out + j / s->width * (s->width + s->skip) + fcol;
      for (uint32_t k = 0; k < n; k++) {
        emit(s, *f++);
        if (++fcol == s->width) {
          fcol = 0;
          f += s->skip;
        }
      }
    }
    s->done += n;
  }
  *z = t;
  return ok;
}

static bool stored(inf_t *s) {
  s->in -= (s->nbits - s->pad) / 8;  // the rest of the byte is skipped, the bytes after it weren't used yet
  s->bits = 0;
  s->nbits = s->pad = 0;
  if (s->end - s->in < 4)
    return false;
  const uint8_t *p = s->in;
  uint32_t n = p[0] | p[1] << 8;
  if ((uint32_t) (p[2] | p[3] << 8) != (~n & 0xFFFF))
    return false;
  s->in += 4;
  if ((uint32_t) (s->end - s->in) < n || n > s->len - s->done)
    return false;
  if (s->out && s->flat) {
    memcpy(s->o, s->in, n);
    s->o += n;
  } else if (s->out)
    for (uint32_t i = 0; i < n; i++)
      emit(s, s->in[i]);
  s->in += n;
  s->done += n;
  return true;
}

static bool dynamic(inf_t *s, huff_t *lit, huff_t *dist, uint16_t *lsym, uint16_t *dsym, uint8_t *lens) {
  int nlen = (int) getbits(s, 5) + 257, ndist = (int) getbits(s, 5) + 1, ncode = (int) getbits(s, 4) + 4;
  if (s->short_ || nlen > 286 || ndist > 30)
    return false;
  memset(lens, 0, 19);
  for (int i = 0; i < ncode; i++)
    lens[CLORDER[i]] = (uint8_t) getbits(s, 3);
  if (s->short_ || build(lit, lsym, lens, 19) != 0)
    return false;  // the code length code has to be complete
  for (int i = 0; i < nlen + ndist;) {
    int sym = decode(s, lit);
    if (s->short_ || sym < 0)
      return false;
    if (sym < 16) {
      lens[i++] = (uint8_t) sym;
      continue;
    }
    uint8_t len = 0;
    int rep;
    if (sym == 16) {
      if (!i)
        return false;  // nothing to repeat
      len = lens[i - 1];
      rep = 3 + (int) getbits(s, 2);
    } else {
      rep = sym == 17 ? 3 + (int) getbits(s, 3) : 11 + (int) getbits(s, 7);
    }
    if (s->short_ || i + rep > nlen + ndist)
      return false;
    while (rep--)
      lens[i++] = len;
  }
  if (!lens[256])
    return false;  // no end code
  // An incomplete code is only OK as a single 1-bit code, nothing longer after it
  int left = build(lit, lsym, lens, nlen);
  if (left < 0 || (left > 0 && lit->limit[15] != lit->limit[1]))
    return false;
  left = build(dist, dsym, lens + nlen, ndist);
  if (left < 0 || (left > 0 && dist->limit[15] != dist->limit[1]))
    return false;
  return codes(s, lit, dist);
}

uint32_t pinflate(const uint8_t *in, uint32_t n, uint8_t *out, unsigned width, unsigned stride, uint32_t len,
                  uint32_t hist) {
  uint16_t lsym[288], dsym[30];
  uint8_t lens[288 + 32];
  huff_t lit, dist;
  inf_t s = {.in = in,
             .start = in,
             .end = in + n,
             .out = out,
             .o = out,
             .width = width,
             .skip = stride - width,
             .flat = width == stride,
             .len = len,
             .hist = hist};
  if (!width || stride < width || (hist && !s.flat))
    return 0;
  if (s.flat)
    s.width = UINT32_MAX;  // never wraps
  for (bool last = false; !last;) {
    last = getbits(&s, 1);
    unsigned type = getbits(&s, 2);
    if (s.short_)
      return 0;
    bool ok = type == 0   ? stored(&s)
              : type == 1 ? codes(&s, &FIX_LIT, &FIX_DIST)
              : type == 2 ? dynamic(&s, &lit, &dist, lsym, dsym, lens)
                          : false;
    if (!ok)
      return 0;
  }
  return s.done == len ? (uint32_t) (s.in - s.start) - (uint32_t) (s.nbits - s.pad) / 8 : 0;
}

// ---------------------------------------------------------------- deflate
#define HASH_BITS 12
#define CHAIN 128  // most earlier places tried for a match
// A token: a literal byte, or a match with the top bit set
#define MATCH(len, dist) (0x80000000u | (uint32_t) ((len) - 3) << 16 | (uint32_t) ((dist) - 1))
#define IS_MATCH(t) ((t) >> 31)
#define MATCH_LEN(t) (((t) >> 16 & 0xFF) + 3)
#define MATCH_DIST(t) (((t) & 0xFFFF) + 1)

// Extra bits of the code length symbols: 16 repeats the length before, 17 and 18 are zeros
static const uint8_t CLEXT[19] = {[16] = 2, [17] = 3, [18] = 7};

typedef struct {
  const uint8_t *in;
  uint32_t n, ins;                // input length, positions below ins are in the hash chains
  uint16_t *prev;                 // the position + 1 before each one with the same hash, 0 for none
  uint16_t head[1 << HASH_BITS];  // the last position + 1 with each hash
  uint32_t lf[286], df[30], cf[19];
  uint8_t llen[286], dlen[30], cllen[19], all[286 + 30];
  uint16_t lcode[286], dcode[30], clcode[19], cl[286 + 30];  // cl: the code length symbols, extra bits << 5
  uint32_t w[2 * 286];                                       // huffman(): node weights, parents, depths
  int16_t parent[2 * 286], sym[286];
  uint8_t depth[2 * 286];
} enc_t;

typedef struct {
  uint8_t *out;
  uint32_t cap, len, bits;
  int nbits;
} wr_t;

// The fixed codes, made on first use. The distances are all 5 bits.
static uint8_t fix_len[288];
static uint16_t fix_lit[288], fix_dist[30];
static const uint8_t FIX_DLEN[30] = {[0 ... 29] = 5};

// Adds n bits of v. Past cap it only counts them.
static void put(wr_t *w, uint32_t v, int n) {
  w->bits |= v << w->nbits;
  w->nbits += n;
  for (; w->nbits >= 8; w->bits >>= 8, w->nbits -= 8, w->len++)
    if (w->len < w->cap)
      w->out[w->len] = (uint8_t) w->bits;
}

// b[0..n) as the last block, stored
static void put_stored(wr_t *w, const uint8_t *b, uint32_t n) {
  put(w, 1, 1);
  put(w, 0, 2);
  put(w, 0, (8 - w->nbits) & 7);
  put(w, n, 16);
  put(w, ~n & 0xFFFF, 16);
  for (uint32_t i = 0; i < n; i++)
    put(w, b[i], 8);
}

// Multiplicative hash of the 3 bytes in v, bits wide
static unsigned hash(uint32_t v, int bits) { return v * 2654435761u >> (32 - bits); }

static unsigned hash3(const uint8_t *p) { return hash((uint32_t) p[0] << 16 | (uint32_t) p[1] << 8 | p[2], HASH_BITS); }

// The longest match for position p with what came before, 0 if none worth it. Its distance goes to *dist.
static unsigned longest(enc_t *e, uint32_t p, uint32_t *dist) {
  for (; e->ins < p; e->ins++)
    if (e->ins + 2 < e->n) {
      unsigned h = hash3(e->in + e->ins);
      e->prev[e->ins] = e->head[h];
      e->head[h] = (uint16_t) (e->ins + 1);
    }
  if (p + 3 > e->n)
    return 0;
  unsigned best = 0, max = e->n - p < 258 ? e->n - p : 258;
  uint32_t c = e->head[hash3(e->in + p)];
  for (int tries = CHAIN; c && tries; tries--, c = e->prev[c - 1]) {
    uint32_t q = c - 1;
    if (p - q > 32768)
      break;
    if (e->in[q + best] != e->in[p + best])
      continue;
    unsigned k = 0;
    while (k < max && e->in[q + k] == e->in[p + k])
      k++;
    if (k > best) {
      best = k;
      *dist = p - q;
      if (k == max)
        break;
    }
  }
  return best > 3 || (best == 3 && *dist <= 4096) ? best : 0;  // 3 bytes far back cost more than they save
}

static int lcode(unsigned len) {
  int i = 28;
  while (LBASE[i] > len)
    i--;
  return i;
}

static int dcode(unsigned dist) {
  int i = 29;
  while (DBASE[i] > dist)
    i--;
  return i;
}

// Code lengths for n symbol frequencies, at most limit bits. Always at least two codes, since a lone code is
// incomplete and some decoders refuse it. If the lengths come out too long, flatten the frequencies and retry.
static void huffman(enc_t *e, const uint32_t *freq, int n, int limit, uint8_t *len) {
  for (int shift = 0;; shift++) {
    int m = 0;
    for (int i = 0; i < n; i++)
      if (freq[i])
        e->sym[m++] = (int16_t) i;
    for (int i = 0; m < 2; i++)
      if (!freq[i])
        e->sym[m++] = (int16_t) i;
    // leaves by weight, lightest first
    for (int i = 0; i < m; i++) {
      int16_t s = e->sym[i];
      uint32_t ws = freq[s] >> (shift < 32 ? shift : 31);
      ws = ws ? ws : 1;
      int j = i;
      for (; j > 0 && e->w[j - 1] > ws; j--) {
        e->w[j] = e->w[j - 1];
        e->sym[j] = e->sym[j - 1];
      }
      e->w[j] = ws;
      e->sym[j] = s;
    }
    // Leaves 0..m-1 in order, new nodes m.. in order: the two lightest are at the front of one or the other
    int leaf = 0, node = m;
    for (int k = m; k < 2 * m - 1; k++) {
      int a = leaf < m && (node >= k || e->w[leaf] <= e->w[node]) ? leaf++ : node++;
      int b = leaf < m && (node >= k || e->w[leaf] <= e->w[node]) ? leaf++ : node++;
      e->w[k] = e->w[a] + e->w[b];
      e->parent[a] = e->parent[b] = (int16_t) k;
    }
    int deepest = 0;
    e->depth[2 * m - 2] = 0;
    for (int k = 2 * m - 3; k >= 0; k--) {
      e->depth[k] = (uint8_t) (e->depth[e->parent[k]] + 1);
      deepest = e->depth[k] > deepest ? e->depth[k] : deepest;
    }
    if (deepest > limit)
      continue;
    memset(len, 0, (size_t) n);
    for (int i = 0; i < m; i++)
      len[e->sym[i]] = e->depth[i];
    return;
  }
}

// Canonical codes from lengths, bit-reversed since deflate sends codes from their top bit
static void canonical(const uint8_t *len, int n, uint16_t *code) {
  uint16_t count[16] = {0}, next[16];
  for (int i = 0; i < n; i++)
    count[len[i]]++;
  count[0] = 0;
  unsigned c = 0;
  for (int b = 1; b < 16; b++)
    next[b] = (uint16_t) (c = (c + count[b - 1]) << 1);
  for (int i = 0; i < n; i++) {
    if (!len[i])
      continue;
    unsigned v = next[len[i]]++, r = 0;
    for (int b = 0; b < len[i]; b++, v >>= 1)
      r = r << 1 | (v & 1);
    code[i] = (uint16_t) r;
  }
}

// Makes the fixed codes once. Inline, as a call GCC -Os makes pzip and pdeflate slower.
__attribute__((always_inline)) static inline void fixed(void) {
  if (fix_len[0])
    return;
  memset(fix_len, 8, 144);
  memset(fix_len + 144, 9, 112);
  memset(fix_len + 256, 7, 24);
  memset(fix_len + 280, 8, 8);
  canonical(fix_len, 288, fix_lit);
  canonical(FIX_DLEN, 30, fix_dist);
}

// Bits for the tokens and the end code with these codes
static uint32_t cost(const uint32_t *tok, uint32_t nt, const uint8_t *llen, const uint8_t *dlen) {
  uint32_t bits = llen[256];
  for (uint32_t i = 0; i < nt; i++) {
    if (!IS_MATCH(tok[i])) {
      bits += llen[tok[i]];
      continue;
    }
    int l = lcode(MATCH_LEN(tok[i])), d = dcode(MATCH_DIST(tok[i]));
    bits += llen[257 + l] + LEXT[l] + dlen[d] + DEXT[d];
  }
  return bits;
}

static void put_tokens(wr_t *w, const uint32_t *tok, uint32_t nt, const uint8_t *llen, const uint16_t *lc,
                       const uint8_t *dlen, const uint16_t *dc) {
  for (uint32_t i = 0; i < nt; i++) {
    if (!IS_MATCH(tok[i])) {
      put(w, lc[tok[i]], llen[tok[i]]);
      continue;
    }
    unsigned len = MATCH_LEN(tok[i]), dist = MATCH_DIST(tok[i]);
    int l = lcode(len), d = dcode(dist);
    put(w, lc[257 + l], llen[257 + l]);
    put(w, len - LBASE[l], LEXT[l]);
    put(w, dc[d], dlen[d]);
    put(w, dist - DBASE[d], DEXT[d]);
  }
  put(w, lc[256], llen[256]);
}

// LZ77 over e->in into tok, with a match put off by one byte if the next byte starts a longer one. Returns the
// number of tokens.
static uint32_t lz77(enc_t *e, uint32_t *tok) {
  const uint8_t *src = e->in;
  uint32_t len = e->n, nt = 0, d = 0, d2 = 0;
  unsigned l = longest(e, 0, &d);
  for (uint32_t p = 0; p < len;) {
    if (l) {
      unsigned l2 = p + 1 < len ? longest(e, p + 1, &d2) : 0;
      if (l2 > l) {
        tok[nt++] = src[p++];
        l = l2;
        d = d2;
        continue;
      }
      tok[nt++] = MATCH(l, d);
      p += l;
    } else {
      tok[nt++] = src[p++];
    }
    l = p < len ? longest(e, p, &d) : 0;
  }
  return nt;
}

// The nlen + ndist code lengths as code length symbols into e->cl. Returns how many.
// 16: the length before 3-6 more times, 17: 3-10 zeros, 18: 11-138 zeros
static int rle(enc_t *e, int nlen, int ndist) {
  int n = nlen + ndist, ncl = 0;
  memcpy(e->all, e->llen, (size_t) nlen);
  memcpy(e->all + nlen, e->dlen, (size_t) ndist);
  for (int i = 0; i < n;) {
    int run = 1;
    while (i + run < n && e->all[i + run] == e->all[i])
      run++;
    if (!e->all[i] && run >= 3) {
      run = run > 138 ? 138 : run;
      e->cl[ncl++] = (uint16_t) (run >= 11 ? 18 | (run - 11) << 5 : 17 | (run - 3) << 5);
      i += run;
    } else if (i && e->all[i - 1] == e->all[i] && run >= 3) {
      run = run > 6 ? 6 : run;
      e->cl[ncl++] = (uint16_t) (16 | (run - 3) << 5);
      i += run;
    } else {
      e->cl[ncl++] = e->all[i++];
    }
  }
  return ncl;
}

uint32_t pdeflate(const uint8_t *in, unsigned width, unsigned stride, uint32_t len, uint8_t *out, uint32_t cap) {
  if (len > 65535 || (len && !width))
    return 0;
  enc_t *e = calloc(1, sizeof *e);
  uint8_t *src = malloc(len + 1);
  uint32_t *tok = malloc(4 * (size_t) len + 4);
  uint16_t *prev = malloc(2 * (size_t) len + 2);
  uint32_t result = 0;
  if (!e || !src || !tok || !prev)
    goto done;
  e->prev = prev;
  for (uint32_t i = 0; i < len; i++)
    src[i] = in[at(i, width, stride)];
  e->in = src;
  e->n = len;
  uint32_t nt = lz77(e, tok);

  // Own codes. Frequencies, lengths, then the lengths as code length symbols
  e->lf[256] = 1;
  for (uint32_t i = 0; i < nt; i++)
    if (IS_MATCH(tok[i])) {
      e->lf[257 + lcode(MATCH_LEN(tok[i]))]++;
      e->df[dcode(MATCH_DIST(tok[i]))]++;
    } else {
      e->lf[tok[i]]++;
    }
  huffman(e, e->lf, 286, 15, e->llen);
  huffman(e, e->df, 30, 15, e->dlen);
  int nlen = 286, ndist = 30;
  while (nlen > 257 && !e->llen[nlen - 1])
    nlen--;
  while (ndist > 1 && !e->dlen[ndist - 1])
    ndist--;
  int ncl = rle(e, nlen, ndist);
  for (int i = 0; i < ncl; i++)
    e->cf[e->cl[i] & 31]++;
  huffman(e, e->cf, 19, 7, e->cllen);
  int nclen = 19;
  while (nclen > 4 && !e->cllen[CLORDER[nclen - 1]])
    nclen--;

  uint32_t dyn = 3 + 14 + 3 * (uint32_t) nclen + cost(tok, nt, e->llen, e->dlen);
  for (int i = 0; i < ncl; i++)
    dyn += e->cllen[e->cl[i] & 31] + CLEXT[e->cl[i] & 31];
  fixed();
  uint32_t fix = 3 + cost(tok, nt, fix_len, FIX_DLEN), store = len + 5 * (len / 65535 + 1);

  wr_t w = {.out = out, .cap = cap};
  if (store <= (fix + 7) / 8 && store <= (dyn + 7) / 8) {
    put_stored(&w, src, len);  // one block, len is under 64 KiB
  } else if (fix <= dyn) {
    put(&w, 1, 1);
    put(&w, 1, 2);
    put_tokens(&w, tok, nt, fix_len, fix_lit, FIX_DLEN, fix_dist);
  } else {
    canonical(e->llen, 286, e->lcode);
    canonical(e->dlen, 30, e->dcode);
    canonical(e->cllen, 19, e->clcode);
    put(&w, 1, 1);
    put(&w, 2, 2);
    put(&w, (uint32_t) nlen - 257, 5);
    put(&w, (uint32_t) ndist - 1, 5);
    put(&w, (uint32_t) nclen - 4, 4);
    for (int i = 0; i < nclen; i++)
      put(&w, e->cllen[CLORDER[i]], 3);
    for (int i = 0; i < ncl; i++) {
      unsigned s = e->cl[i] & 31;
      put(&w, e->clcode[s], e->cllen[s]);
      if (s >= 16)
        put(&w, e->cl[i] >> 5, CLEXT[s]);
    }
    put_tokens(&w, tok, nt, e->llen, e->lcode, e->dlen, e->dcode);
  }
  put(&w, 0, (8 - w.nbits) & 7);
  result = w.len <= cap ? w.len : 0;
done:
  free(prev);
  free(e);
  free(src);
  free(tok);
  return result;
}

// ---------------------------------------------------------------- pieces
#define PZIP_OUT 4096  // most a piece makes, so neither end takes long over one

static uint8_t pz_at(const pzip_t *z, uint32_t v) { return v < z->nh ? z->hist[v] : z->data[v - z->nh]; }

static unsigned pz_hash(const pzip_t *z, uint32_t v) {
  return hash((uint32_t) pz_at(z, v) << 16 | (uint32_t) pz_at(z, v + 1) << 8 | pz_at(z, v + 2), PZIP_HASH_BITS);
}

// Makes the match for the byte at v from the one at s longer than *best, up to max bytes
static void pz_try(const pzip_t *z, uint32_t v, uint32_t s, unsigned max, unsigned *best, uint32_t *dist) {
  if (*best >= max || s >= v || v - s > 32768 || pz_at(z, s + *best) != pz_at(z, v + *best))
    return;
  unsigned k = 0;
  while (k < max && pz_at(z, s + k) == pz_at(z, v + k))
    k++;
  if (k > *best) {
    *best = k;
    *dist = v - s;
  }
}

unsigned pzip(pzip_t *z, uint8_t *out) {
  if (z->done >= z->n)
    return 0;
  if (z->table && !z->done && !z->ins)
    memset(z->table, 0, PZIP_TABLE * sizeof *z->table);
  fixed();
  wr_t w = {.out = out + PZIP_HEAD, .cap = PZIP_MAX - PZIP_HEAD};
  uint32_t bits = 3 + 7, room = 8 * w.cap, i = z->done;  // the block head and the end code
  // hist goes into the table over the first pieces, top first, so no piece takes long. Without taking a place from
  // the data, which is nearer.
  for (uint32_t stop = z->ins + PZIP_TABLE; z->table && z->ins < z->nh && z->ins < stop; z->ins++) {
    uint16_t *t = &z->table[pz_hash(z, z->ins)];
    if (!*t && z->ins + 2 < z->nh + z->n)
      *t = (uint16_t) (z->ins + 1);
  }
  put(&w, 1, 1);
  put(&w, 1, 2);
  while (i < z->n && i - z->done < PZIP_OUT) {  // greedy: the longest match if it costs less than its literals
    uint32_t v = z->nh + i, dist = 0, cost = fix_len[z->data[i]];
    unsigned max = z->n - i < 258 ? z->n - i : 258, len = 0;
    if (max >= 3) {  // the last place with the same 3 bytes, and where like() says
      len = 2;
      uint16_t t = z->table ? z->table[pz_hash(z, v)] : 0;
      if (t)
        pz_try(z, v, t - 1u, max, &len, &dist);
      int32_t s = z->like ? z->like(z->ctx, i) : -1;
      if (s >= 0 && (uint32_t) s < z->nh)
        pz_try(z, v, (uint32_t) s, max, &len, &dist);
    }
    int l = len >= 3 ? lcode(len) : 0, d = len >= 3 ? dcode(dist) : 0;
    if (len >= 3) {
      uint32_t as_lit = 0, mbits = fix_len[257 + l] + LEXT[l] + 5u + DEXT[d];
      for (unsigned k = 0; k < len; k++)
        as_lit += fix_len[z->data[i + k]];
      if (mbits < as_lit)
        cost = mbits;
      else
        len = 0;
    }
    if (bits + cost > room)
      break;
    bits += cost;
    for (uint32_t k = 0, step = len >= 3 ? len : 1; z->table && k < step && v + k + 2 < z->nh + z->n; k++)
      if (k < 8 || k + 3 >= step)  // a long match's ends, its middle adds little
        z->table[pz_hash(z, v + k)] = (uint16_t) (v + k + 1);
    if (len >= 3) {
      put(&w, fix_lit[257 + l], fix_len[257 + l]);
      put(&w, len - LBASE[l], LEXT[l]);
      put(&w, fix_dist[d], 5);
      put(&w, dist - DBASE[d], DEXT[d]);
      i += len;
    } else {
      put(&w, fix_lit[z->data[i]], fix_len[z->data[i]]);
      i++;
    }
  }
  put(&w, fix_lit[256], 7);
  put(&w, 0, (8 - w.nbits) & 7);
  uint32_t made = i - z->done, left = z->n - z->done, raw = w.cap - 5 < left ? w.cap - 5 : left;
  if (raw > made) {  // stored carries more
    w = (wr_t) {.out = out + PZIP_HEAD, .cap = PZIP_MAX - PZIP_HEAD};
    made = raw;
    put_stored(&w, z->data + z->done, made);
  }
  out[0] = (uint8_t) w.len;
  out[1] = (uint8_t) made;
  out[2] = (uint8_t) (made >> 8);
  z->done += made;
  return PZIP_HEAD + w.len;
}
