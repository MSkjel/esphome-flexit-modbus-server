// Raw deflate (RFC 1951) for the animation records and the pictures. pdeflate and pinflate work on a rectangle in a
// bigger buffer: width bytes per row, rows stride bytes apart.
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Max output for n bytes, which is n stored plus 5 bytes per 64 KiB block
#define PDEFLATE_BOUND(n) ((n) + 5 * ((n) / 65535 + 1))
// Heap pdeflate takes for n bytes. Its biggest blocks are PDEFLATE_HEAP(0) and 4 x n.
#define PDEFLATE_HEAP(n) (7 * (n) + 17 * 1024)

// Deflates the rectangle at in into out, len <= 65535. Returns the length, 0 if it doesn't fit cap or malloc
// fails. Takes PDEFLATE_HEAP(len).
uint32_t pdeflate(const uint8_t *in, unsigned width, unsigned stride, uint32_t len, uint8_t *out, uint32_t cap);
// Inflates in[0..n) into the rectangle at out, or only checks the data if out is NULL. Matches may reach hist bytes
// before out, which needs width == stride. Returns the bytes of in used, 0 if malformed or it doesn't make exactly
// len bytes. Needs about 1.4 KiB of stack.
uint32_t pinflate(const uint8_t *in, uint32_t n, uint8_t *out, unsigned width, unsigned stride, uint32_t len,
                  uint32_t hist);

// A picture goes to the panel as pieces deflated against what it holds: the picture it has, then what came before
// in this transfer. Each piece inflates on its own, straight into place: the deflated length, the length it makes
// (2), then raw deflate with fixed codes, or stored.
#define PZIP_HEAD 3
#define PZIP_MAX 128  // a piece with its head
#define PZIP_HASH_BITS 11
#define PZIP_TABLE (1 << PZIP_HASH_BITS)  // entries in the match table, of 16-bit positions: hist and data under 64 KiB
typedef struct {
  const uint8_t *hist;  // what the panel holds right before where data goes
  uint32_t nh;
  const uint8_t *data;
  uint32_t n, done;  // done: bytes already in pieces
  uint16_t *table;   // PZIP_TABLE entries, kept for all the data's pieces. Without, only like() is tried
  uint32_t ins;      // bytes of hist in the table so far
  // Where in hist data byte i most likely repeats, like the same place in the row it replaces, -1 for nowhere.
  // Optional.
  int32_t (*like)(void *ctx, uint32_t i);
  void *ctx;
} pzip_t;
// The next piece into out[PZIP_MAX]. Returns its length, 0 once all the data is in pieces.
unsigned pzip(pzip_t *z, uint8_t *out);

#ifdef __cplusplus
}
#endif
