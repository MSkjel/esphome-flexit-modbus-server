// Raw deflate, see pdeflate.h. pinflate works like zlib's puff.c, slow but small, decoding a bit at a time and
// checking every read and write. pdeflate makes one block, LZ77 with hash chains and one step of lazy matching,
// then stored, fixed or own Huffman codes, whichever is smallest.
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
typedef struct {
  const uint8_t *in;
  uint32_t n, pos;
  uint32_t bits;  // bits read but not used yet, nbits of them, at the bottom
  int nbits;
  bool short_;  // the data ended early
  uint8_t *out;
  unsigned width, stride;
  uint32_t len, done;  // bytes to make, bytes made
} inf_t;

typedef struct {
  int16_t count[16];  // codes of each length
  int16_t *symbol;    // the symbols in code order
} huff_t;

static unsigned getbits(inf_t *s, int need) {
  while (s->nbits < need) {
    if (s->pos == s->n) {
      s->short_ = true;
      return 0;
    }
    s->bits |= (uint32_t) s->in[s->pos++] << s->nbits;
    s->nbits += 8;
  }
  unsigned v = s->bits & ((1u << need) - 1);
  s->bits >>= need;
  s->nbits -= need;
  return v;
}

// Builds h from n code lengths. < 0 = too many codes, 0 = complete, > 0 = some left over.
static int build(huff_t *h, const uint8_t *lens, int n) {
  memset(h->count, 0, sizeof h->count);
  for (int i = 0; i < n; i++)
    h->count[lens[i]]++;
  if (h->count[0] == n)
    return 0;  // no codes: nothing will decode
  int left = 1;
  for (int l = 1; l < 16; l++) {
    left = (left << 1) - h->count[l];
    if (left < 0)
      return left;
  }
  int16_t offs[16];
  offs[1] = 0;
  for (int l = 1; l < 15; l++)
    offs[l + 1] = (int16_t) (offs[l] + h->count[l]);
  for (int i = 0; i < n; i++)
    if (lens[i])
      h->symbol[offs[lens[i]]++] = (int16_t) i;
  return left;
}

// The next symbol, -1 if the bits aren't a code
static int decode(inf_t *s, const huff_t *h) {
  int code = 0, first = 0, index = 0;
  for (int l = 1; l < 16; l++) {
    code |= (int) getbits(s, 1);
    int count = h->count[l];
    if (code - count < first)
      return h->symbol[index + code - first];
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  return -1;
}

// The symbols of a block, up to its end code
static bool codes(inf_t *s, const huff_t *lit, const huff_t *dist) {
  for (;;) {
    int sym = decode(s, lit);
    if (s->short_ || sym < 0)
      return false;
    if (sym == 256)
      return true;
    if (sym < 256) {
      if (s->done == s->len)
        return false;
      if (s->out)
        s->out[at(s->done, s->width, s->stride)] = (uint8_t) sym;
      s->done++;
      continue;
    }
    sym -= 257;
    if (sym >= 29)
      return false;
    uint32_t n = LBASE[sym] + getbits(s, LEXT[sym]);
    int d = decode(s, dist);
    if (s->short_ || d < 0 || d >= 30)
      return false;
    uint32_t back = DBASE[d] + getbits(s, DEXT[d]);
    if (s->short_ || back > s->done || n > s->len - s->done)
      return false;
    if (s->out)
      for (uint32_t i = s->done; i < s->done + n; i++)
        s->out[at(i, s->width, s->stride)] = s->out[at(i - back, s->width, s->stride)];
    s->done += n;
  }
}

static bool stored(inf_t *s) {
  s->bits = 0;  // skip the rest of the byte
  s->nbits = 0;
  if (s->n - s->pos < 4)
    return false;
  const uint8_t *p = s->in + s->pos;
  uint32_t n = p[0] | p[1] << 8;
  if ((uint32_t) (p[2] | p[3] << 8) != (~n & 0xFFFF))
    return false;
  s->pos += 4;
  if (s->n - s->pos < n || n > s->len - s->done)
    return false;
  if (s->out)
    for (uint32_t i = 0; i < n; i++)
      s->out[at(s->done + i, s->width, s->stride)] = s->in[s->pos + i];
  s->pos += n;
  s->done += n;
  return true;
}

static bool fixed(inf_t *s, huff_t *lit, huff_t *dist, uint8_t *lens) {
  memset(lens, 8, 144);
  memset(lens + 144, 9, 112);
  memset(lens + 256, 7, 24);
  memset(lens + 280, 8, 8);
  build(lit, lens, 288);
  memset(lens, 5, 30);
  build(dist, lens, 30);
  return codes(s, lit, dist);
}

static bool dynamic(inf_t *s, huff_t *lit, huff_t *dist, uint8_t *lens) {
  int nlen = (int) getbits(s, 5) + 257, ndist = (int) getbits(s, 5) + 1, ncode = (int) getbits(s, 4) + 4;
  if (s->short_ || nlen > 286 || ndist > 30)
    return false;
  memset(lens, 0, 19);
  for (int i = 0; i < ncode; i++)
    lens[CLORDER[i]] = (uint8_t) getbits(s, 3);
  if (s->short_ || build(lit, lens, 19) != 0)
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
  // An incomplete code is only OK as a single 1-bit code
  int left = build(lit, lens, nlen);
  if (left < 0 || (left > 0 && nlen != lit->count[0] + lit->count[1]))
    return false;
  left = build(dist, lens + nlen, ndist);
  if (left < 0 || (left > 0 && ndist != dist->count[0] + dist->count[1]))
    return false;
  return codes(s, lit, dist);
}

uint32_t pinflate(const uint8_t *in, uint32_t n, uint8_t *out, unsigned width, unsigned stride, uint32_t len) {
  int16_t lsym[288], dsym[30];
  uint8_t lens[288 + 32];
  huff_t lit = {.symbol = lsym}, dist = {.symbol = dsym};
  inf_t s = {.in = in, .n = n, .out = out, .width = width, .stride = stride, .len = len};
  if (!width)
    return 0;
  for (bool last = false; !last;) {
    last = getbits(&s, 1);
    unsigned type = getbits(&s, 2);
    if (s.short_)
      return 0;
    bool ok = type == 0   ? stored(&s)
              : type == 1 ? fixed(&s, &lit, &dist, lens)
              : type == 2 ? dynamic(&s, &lit, &dist, lens)
                          : false;
    if (!ok)
      return 0;
  }
  return s.done == len ? s.pos : 0;
}

// ---------------------------------------------------------------- deflate
#define HASH_BITS 12
#define CHAIN 128  // most earlier places tried for a match
#define MATCH(len, dist) (0x80000000u | (uint32_t) ((len) - 3) << 16 | (uint32_t) ((dist) - 1))

typedef struct {
  const uint8_t *in;
  uint32_t n, ins;                // input length, positions below ins are in the hash chains
  uint16_t *prev;                 // the position + 1 before each one with the same hash, 0 for none
  uint16_t head[1 << HASH_BITS];  // the last position + 1 with each hash
  uint32_t lf[286], df[30], cf[19];
  uint8_t llen[286], dlen[30], cllen[19], all[286 + 30];
  uint16_t lcode[286], dcode[30], clcode[19], cl[286 + 30];  // cl: the code length symbols, extra bits << 5
  uint8_t fl[288], fd[30];                                   // the fixed codes
  uint16_t flc[288], fdc[30];
  uint32_t w[2 * 286];  // huffman(): node weights, parents, depths
  int16_t parent[2 * 286], sym[286];
  uint8_t depth[2 * 286];
} enc_t;

typedef struct {
  uint8_t *out;
  uint32_t cap, len, bits;
  int nbits;
} wr_t;

// Adds n bits of v. Past cap it only counts them.
static void put(wr_t *w, uint32_t v, int n) {
  w->bits |= v << w->nbits;
  w->nbits += n;
  for (; w->nbits >= 8; w->bits >>= 8, w->nbits -= 8, w->len++)
    if (w->len < w->cap)
      w->out[w->len] = (uint8_t) w->bits;
}

static unsigned hash3(const uint8_t *p) {
  return ((uint32_t) p[0] << 16 | (uint32_t) p[1] << 8 | p[2]) * 2654435761u >> (32 - HASH_BITS);
}

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

// Bits for the tokens and the end code with these codes
static uint32_t cost(const uint32_t *tok, uint32_t nt, const uint8_t *llen, const uint8_t *dlen) {
  uint32_t bits = llen[256];
  for (uint32_t i = 0; i < nt; i++) {
    if (!(tok[i] >> 31)) {
      bits += llen[tok[i]];
      continue;
    }
    int l = lcode((tok[i] >> 16 & 0xFF) + 3), d = dcode((tok[i] & 0xFFFF) + 1);
    bits += llen[257 + l] + LEXT[l] + dlen[d] + DEXT[d];
  }
  return bits;
}

static void put_tokens(wr_t *w, const uint32_t *tok, uint32_t nt, const uint8_t *llen, const uint16_t *lc,
                       const uint8_t *dlen, const uint16_t *dc) {
  for (uint32_t i = 0; i < nt; i++) {
    if (!(tok[i] >> 31)) {
      put(w, lc[tok[i]], llen[tok[i]]);
      continue;
    }
    unsigned len = (tok[i] >> 16 & 0xFF) + 3, dist = (tok[i] & 0xFFFF) + 1;
    int l = lcode(len), d = dcode(dist);
    put(w, lc[257 + l], llen[257 + l]);
    put(w, len - LBASE[l], LEXT[l]);
    put(w, dc[d], dlen[d]);
    put(w, dist - DBASE[d], DEXT[d]);
  }
  put(w, lc[256], llen[256]);
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

  // LZ77, with a match put off by one byte if the next byte starts a longer one
  uint32_t nt = 0, d = 0, d2 = 0;
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

  // Own codes. Frequencies, lengths, then the lengths as code length symbols
  e->lf[256] = 1;
  for (uint32_t i = 0; i < nt; i++)
    if (tok[i] >> 31) {
      e->lf[257 + lcode((tok[i] >> 16 & 0xFF) + 3)]++;
      e->df[dcode((tok[i] & 0xFFFF) + 1)]++;
    } else {
      e->lf[tok[i]]++;
    }
  huffman(e, e->lf, 286, 15, e->llen);
  huffman(e, e->df, 30, 15, e->dlen);
  int nlen = 286, ndist = 30, ncl = 0;
  while (nlen > 257 && !e->llen[nlen - 1])
    nlen--;
  while (ndist > 1 && !e->dlen[ndist - 1])
    ndist--;
  memcpy(e->all, e->llen, (size_t) nlen);
  memcpy(e->all + nlen, e->dlen, (size_t) ndist);
  // 16: the length before 3-6 more times, 17: 3-10 zeros, 18: 11-138 zeros
  for (int i = 0; i < nlen + ndist;) {
    int run = 1;
    while (i + run < nlen + ndist && e->all[i + run] == e->all[i])
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
  for (int i = 0; i < ncl; i++)
    e->cf[e->cl[i] & 31]++;
  huffman(e, e->cf, 19, 7, e->cllen);
  int nclen = 19;
  while (nclen > 4 && !e->cllen[CLORDER[nclen - 1]])
    nclen--;

  uint32_t dyn = 3 + 14 + 3 * (uint32_t) nclen + cost(tok, nt, e->llen, e->dlen);
  for (int i = 0; i < ncl; i++) {
    unsigned s = e->cl[i] & 31;
    dyn += e->cllen[s] + (s == 16 ? 2 : s == 17 ? 3 : s == 18 ? 7 : 0);
  }
  memset(e->fl, 8, 144);
  memset(e->fl + 144, 9, 112);
  memset(e->fl + 256, 7, 24);
  memset(e->fl + 280, 8, 8);
  memset(e->fd, 5, 30);
  uint32_t fix = 3 + cost(tok, nt, e->fl, e->fd), store = len + 5 * (len / 65535 + 1);

  wr_t w = {.out = out, .cap = cap};
  if (store <= (fix + 7) / 8 && store <= (dyn + 7) / 8) {
    uint32_t i = 0;
    do {
      uint32_t k = len - i < 65535 ? len - i : 65535;
      put(&w, i + k == len, 1);
      put(&w, 0, 2);
      put(&w, 0, (8 - w.nbits) & 7);
      put(&w, k, 16);
      put(&w, ~k & 0xFFFF, 16);
      for (uint32_t j = 0; j < k; j++)
        put(&w, src[i + j], 8);
      i += k;
    } while (i < len);
  } else if (fix <= dyn) {
    canonical(e->fl, 288, e->flc);
    canonical(e->fd, 30, e->fdc);
    put(&w, 1, 1);
    put(&w, 1, 2);
    put_tokens(&w, tok, nt, e->fl, e->flc, e->fd, e->fdc);
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
      unsigned s = e->cl[i] & 31, x = e->cl[i] >> 5;
      put(&w, e->clcode[s], e->cllen[s]);
      if (s >= 16)
        put(&w, x, s == 16 ? 2 : s == 17 ? 3 : 7);
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
