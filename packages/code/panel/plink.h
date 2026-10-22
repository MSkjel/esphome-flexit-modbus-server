// Messages between the CI600 and the ESP, and the panel's queue of outgoing ones. Keys are in plink_keys.h and
// plink_values.h, pbulk.h carries them.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "plink_keys.h"
#include "plink_values.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLINK_VERSION 1

// Message kinds. A newer VALUE replaces one still waiting, but every EVENT counts. BYTES are texts and pictures,
// every one counts too.
enum { PLINK_VALUE, PLINK_EVENT, PLINK_BYTES };
int plink_kind(uint16_t key);
uint32_t plink_number(const uint8_t *b, int n);  // up to 4 bytes, low first

// ---- The panel's messages to the ESP, numbers only
#define PLINK_QUEUE 32
#define PLINK_NUMBER_BYTES 7  // a number in a payload: key (2), length, value (4)

typedef struct {
  struct {
    uint16_t key;
    uint32_t value;
  } q[PLINK_QUEUE];
  uint8_t qn;
  uint32_t sent, dropped;
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
// Animation frame cur as the rectangle that changed since prev, or the whole frame if prev is NULL. w x h, w even,
// packed like picture rows. The record is delay (10 ms units), x / 2, y, w / 2, h, then the rectangle deflated,
// nothing if it's empty. out needs 5 + PDEFLATE_BOUND(w / 2 x h) bytes. Returns the record's length, 0 if there
// wasn't memory to compress it.
unsigned plink_anim_record(const uint8_t *prev, const uint8_t *cur, unsigned w, unsigned h, uint8_t delay,
                           uint8_t *out);

#ifdef __cplusplus
}
#endif
