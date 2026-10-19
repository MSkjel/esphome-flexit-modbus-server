// Raw deflate (RFC 1951) for the animation records. Both directions work on a rectangle in a bigger buffer:
// width bytes per row, rows stride bytes apart.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Max output for n bytes, which is n stored plus 5 bytes per 64 KiB block
#define PDEFLATE_BOUND(n) ((n) + 5 * ((n) / 65535 + 1))

// Deflates the rectangle at in into out, len <= 65535. Returns the length, 0 if it doesn't fit cap or malloc
// fails. Takes about 7 x len + 17 KiB of heap.
uint32_t pdeflate(const uint8_t *in, unsigned width, unsigned stride, uint32_t len, uint8_t *out, uint32_t cap);
// Inflates in[0..n) into the rectangle at out, or only checks the data if out is NULL. Returns the bytes of in
// used, 0 if malformed or it doesn't make exactly len bytes. Needs about 1 KiB of stack.
uint32_t pinflate(const uint8_t *in, uint32_t n, uint8_t *out, unsigned width, unsigned stride, uint32_t len);

#ifdef __cplusplus
}
#endif
