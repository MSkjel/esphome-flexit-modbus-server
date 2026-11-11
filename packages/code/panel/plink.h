// Messages between the CI600 and the ESP, and the panel's queue of outgoing ones. Keys are in plink_keys.h and
// plink_values.h, pbulk.h carries them.
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pdeflate.h"
#include "plink_keys.h"
#include "plink_values.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLINK_VERSION 7

// Message kinds. A newer VALUE replaces one still waiting, but every EVENT counts. BYTES are texts and pictures,
// every one counts too.
enum { PLINK_VALUE, PLINK_EVENT, PLINK_BYTES };
int plink_kind(uint16_t key);
// pbulk_esp_send's flags for this key
unsigned plink_bulk_flags(uint16_t key);
uint32_t plink_number(const uint8_t *b, int n);  // up to 4 bytes, low first
int plink_put_number(uint8_t *out, uint32_t v);  // the other way round, without high zero bytes. Returns 0..4

// Fixed-size numbers in messages and frames, low byte first
static inline uint16_t plink_get16(const uint8_t *b) { return (uint16_t) (b[0] | b[1] << 8); }
static inline int16_t plink_i16(const uint8_t *b) { return (int16_t) plink_get16(b); }
static inline void plink_put16(uint8_t *b, uint16_t v) {
  b[0] = (uint8_t) v;
  b[1] = (uint8_t) (v >> 8);
}
static inline void plink_put32(uint8_t *b, uint32_t v) {
  plink_put16(b, (uint16_t) v);
  plink_put16(b + 2, (uint16_t) (v >> 16));
}

// An ESP item's id, a 14-bit FNV-1a of its name, never 0. The ESP moves it on if two items get the same.
static inline uint16_t plink_item_id(const char *name) {
  uint32_t h = 2166136261u;
  for (; *name; name++)
    h = (h ^ (uint8_t) *name) * 16777619u;
  return (uint16_t) (h & ITEM_ID_MASK ? h & ITEM_ID_MASK : 1);
}

// ---- The panel's messages to the ESP, numbers only. Screenshots go alongside, see app/shot.c
#define PLINK_QUEUE 32

typedef struct {
  uint32_t value[PLINK_QUEUE];  // the queue as two arrays, so there's no padding between key and value
  uint16_t key[PLINK_QUEUE];
  uint8_t qn;
} plink_t;

void plink_init(plink_t *l);
// False if the queue is full. A help request replaces a waiting one, like a VALUE. A K_HELLO goes first,
// replaces a waiting HELLO and always fits.
bool plink_send(plink_t *l, uint16_t key, uint32_t value);
// Moves the waiting messages, oldest first, into a payload of at most max bytes. Returns its length.
int plink_pack(plink_t *l, uint8_t *out, int max);
// The other end restarted. Drops events and item values, which are about state it lost.
void plink_forget(plink_t *l);
uint8_t plink_room(const plink_t *l);  // free places, less the one kept for a HELLO
int plink_waiting(const plink_t *l);   // bytes of messages waiting

uint32_t plink_crc32(uint32_t crc, const void *data, unsigned n);  // start with 0, chain the result

// PackBits for one picture row of 16-colour pixels, high nibble first. out needs n + n/128 + 1 bytes. A byte
// c < 128 is followed by c + 1 literal bytes, c >= 128 by one byte to repeat c - 126 times.
unsigned plink_pack_row(const uint8_t *row, unsigned n, uint8_t *out);
static inline unsigned plink_run(uint8_t c) { return c < 128 ? c + 1u : c - 126u; }  // bytes of the row c makes
static inline unsigned plink_run_bytes(uint8_t c) { return c < 128 ? c + 1u : 1u; }  // and the bytes after c
// A record's head at out, see OVERLAY_RECORD_*: its delay, the rectangle in the frame's bytes and how many bytes of
// it follow. All 0 after the delay for an empty one, or OVERLAY_RECORD_MORE or OVERLAY_RECORD_FRAME as its x.
// Returns OVERLAY_RECORD_HEAD.
unsigned plink_overlay_head(uint8_t *out, uint8_t delay, unsigned x, unsigned y, unsigned w, unsigned h, unsigned len);
// An animated overlay's frame cur as the rectangle that changed since prev, or the whole frame if prev is NULL. w x h,
// packed like picture rows, (w + 1) / 2 bytes each. The record is its head, delay in OVERLAY_TICK_MS, then the
// rectangle's bytes deflated, nothing if it's empty. out needs OVERLAY_RECORD_HEAD +
// PDEFLATE_BOUND of the frame's bytes. Returns the record's length, 0 if it couldn't be compressed. It deflates in
// work's blocks for the frame's bytes, so nothing is malloc'd.
unsigned plink_overlay_record_in(const pdeflate_work_t *work, const uint8_t *prev, const uint8_t *cur, unsigned w,
                                 unsigned h, uint8_t delay, uint8_t *out);
// A still overlay's pieces from its frame, w x h packed like an animated one's, into out: one for each 8 rows of it
// with a colour that isn't clear, trimmed to those, each with delay. clear has a bit for each colour that is. A frame
// that's all clear is one empty piece. Returns their length, 0 if they might not fit in cap.
uint32_t plink_overlay_pieces(const uint8_t *frame, unsigned w, unsigned h, uint16_t clear, uint8_t delay, uint8_t *out,
                              uint32_t cap);

// ---- The ESP's picture and overlay transfers, see plink_keys.h
// 16 0xRRGGBB colours as PIC_PAL_LEN bytes
void plink_put_palette(uint8_t *b, const uint32_t *pal);
// A BEGIN or PATCH in b, the palette after its head unless pal is NULL. b needs PIC_BEGIN_HEAD or PIC_PATCH_HEAD +
// PIC_PAL_LEN bytes. Returns the message's length.
int plink_pic_begin(uint8_t *b, uint8_t id, uint8_t format, uint16_t w, uint16_t h, uint32_t size, const uint32_t *pal);
int plink_pic_patch(uint8_t *b, uint8_t id, uint16_t y, uint16_t count, uint32_t size, const uint32_t *pal);
// An OVERLAY_BEGIN in b, OVERLAY_BEGIN_LEN bytes. pal is 16 0xAARRGGBB colours, each one's opacity its alpha in
// 15ths, rounded, glows a bit for each that glows, shade 0xRRGGBB, cycles a bit for each a place's phase takes
// round. Returns the message's length.
int plink_overlay_begin(uint8_t *b, uint8_t id, uint8_t slot, uint8_t kind, uint16_t w, uint16_t h, uint8_t scale,
                        uint32_t size, const uint32_t *pal, uint16_t glows, uint32_t shade, uint16_t cycles);

#ifdef __cplusplus
}
#endif
