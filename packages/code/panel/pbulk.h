// Link frames between the ESP and the panel. They carry the plink.h messages both ways, in order and without loss.
// The ESP starts one transaction every cycle in the CX60's quiet time, just a poll if it has nothing to send, and
// more in the gaps after the CX60's polls while it has more.
//
//   ESP -> panel   F7 66 n panel esess num psess pnext payload[n] crc          n <= PBULK_MAX
//   panel -> ESP   F7 67 n esess enext status psess pnum want payload[n] crc   n <= PBULK_ANSWER_MAX
//
// Modbus CRC. panel is the linked panel's bus address, only that panel answers. Sessions and message numbers are
// 2 bytes, low byte first. num and pnum number the first message in the frame, enext and pnext are the number the
// receiver wants next. The sender resends from there in frames of whatever size fits, and the receiver skips what
// it already has. A new session doesn't restart the numbers.
// want is the bytes the panel has waiting that didn't fit in this answer, so the ESP leaves room for them. status
// has a PB_* in its low 2 bits and above them the panel's deadline after a status broadcast, as PBUS_END_STEP_US
// past PBUS_END_US.
// Each end picks a random session when it starts. If the other end wants a message we never sent, it's talking to
// an earlier session with the same number, so we pick a new one.
// The payload is a list of messages, each a 2-byte key (low byte first), a length byte and that many value bytes.
// A PBULK_DATA_KEY message counts as one message per 4 bytes, so a frame can carry part of one.
// The ESP only sends what is sure to fit in the time left. The panel only answers if its answer fits.
//
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pbus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PBULK_ADDR PBUS_LINK_ADDR
#define PBULK_FC_SEND 0x66
#define PBULK_FC_ANSWER 0x67
#define PBULK_DATA_KEY 0x102  // plink's K_PIC_DATA, a picture to the panel or a screenshot from it
#define PBULK_DATA_MAX 252    // bytes in a DATA message, the most its length byte takes in multiples of 4
#define PBULK_MSG_HEAD 3      // key (2) and length, before the value

// Byte offsets of the fields in an ESP frame
enum { PBULK_N = 2, PBULK_PANEL = 3, PBULK_ESESS = 4, PBULK_NUM = 6, PBULK_PSESS = 8, PBULK_PNEXT = 10 };
enum { PBULK_PAYLOAD = 12 };
#define PBULK_MAX 113                             // so the whole frame fits the ESP's 128-byte UART FIFO
#define PBULK_FRAME(n) (PBULK_PAYLOAD + (n) + 2)  // frame length for an n-byte payload
#define PBULK_FRAME_MAX PBULK_FRAME(PBULK_MAX)
// The longest message that isn't DATA. Only DATA is split, so the rest must fit a frame, with the panel's empty
// answer, in the best status window: PBUS_START_MIN_US after the status frame, less the start and the turnaround,
// and a millisecond for the ESP, which only learns that the frame has ended two byte times and an interrupt later
#define PBULK_WINDOW_US (PBUS_END_US - PBUS_ESP_START_US - PBUS_START_MIN_US - PBUS_TURN_US - 1000)
#define PBULK_MSG_MAX (PBULK_WINDOW_US / PBUS_BYTE_US - PBULK_FRAME(0) - PBULK_ANSWER(0) - PBULK_MSG_HEAD)

// Byte offsets in the panel's answer. n is at PBULK_N like in an ESP frame
enum { PBULK_A_ESESS = 3, PBULK_A_ENEXT = 5, PBULK_A_STATUS = 7, PBULK_A_PSESS = 8, PBULK_A_PNUM = 10 };
enum { PBULK_A_WANT = 12, PBULK_A_PAYLOAD = 13 };
#define PBULK_ANSWER_MAX 160  // the panel's stream went fastest with this in bulk_test, of 113, 160 and 200
#define PBULK_ANSWER(n) (PBULK_A_PAYLOAD + (n) + 2)
#define PBULK_ANSWER_FRAME_MAX PBULK_ANSWER(PBULK_ANSWER_MAX)
#define PBULK_PAYLOAD_MAX (PBULK_ANSWER_MAX > PBULK_MAX ? PBULK_ANSWER_MAX : PBULK_MAX)  // either way's longest

// Answer status. The bus status from pbus.h, or PB_BUSY when the panel had no room for the ESP's messages
enum { PB_OK = PBUS_OK, PB_REFUSED = PBUS_REFUSED, PB_NO_GAPS = PBUS_NO_GAPS, PB_BUSY };

int pbulk_frame_len(const uint8_t *q, int n);  // full length of the link frame starting at q, else 0
// Calls f for each message in a payload, in order. False if the payload is malformed.
bool pbulk_parse(const uint8_t *p, int n, void (*f)(void *u, uint16_t key, const uint8_t *b, int len), void *u);

// ---- Panel side. pbulk_panel_frame runs in the RX interrupt, the rest in the main loop.
#define PBULK_RX_BUF 512

typedef struct {
  uint8_t buf[PBULK_RX_BUF];     // ring of ESP messages for the main loop, each batch as n, payload[n]
  volatile uint16_t head, tail;  // the interrupt writes at head, the main loop reads at tail
  uint16_t esess;                // the ESP's session and the number of its next message we want
  uint16_t enext;
  uint8_t out[PBULK_ANSWER_MAX];  // our messages from out_num on, resent in answers until the ESP has them all
  uint8_t out_n;
  uint16_t out_num;
  volatile bool out_full;          // set by the main loop when it fills out, cleared by the interrupt
  uint8_t next[PBULK_ANSWER_MAX];  // the ones after out, which the interrupt moves there once the ESP has out
  uint8_t next_n;                  // so the answer that says it has them can carry the next ones
  uint16_t next_num;
  volatile bool next_full;
  volatile uint8_t waiting;  // bytes of messages the main loop has waiting behind out and next
  uint16_t psess;            // our session
  volatile uint16_t pnum;    // number for our next message
  uint8_t ahead;             // frames in a row starting after enext, see resyncs
  uint32_t frames, busy, late, refused, bad;
  uint32_t resyncs;  // times the ESP skipped past the ones we wanted, those are lost
} pbulk_panel_t;

void pbulk_panel_init(pbulk_panel_t *p, uint16_t session);  // session: a random number
// A frame to PBULK_ADDR, addr is our bus address. left_us is the time until everything has to be off the bus, 0
// outside the windows. say is PB_OK, PB_REFUSED or PB_NO_GAPS and end_us our deadline after a status broadcast,
// both reported in the answer. Returns the answer's length, 0 for no answer, like when it wouldn't be off the
// bus in time or it's for another panel.
int pbulk_panel_frame(pbulk_panel_t *p, uint8_t addr, const uint8_t *f, int n, uint32_t left_us, uint8_t say,
                      uint32_t end_us, uint8_t *answer);
int pbulk_panel_take(pbulk_panel_t *p, uint8_t *out);  // next batch of ESP messages, out is PBULK_MAX bytes. 0 = none
// Queues a payload from plink_pack, in out or else in next. False while both are full. DATA in it is a multiple of
// 4 bytes, an answer takes as much of it as fits.
bool pbulk_panel_put(pbulk_panel_t *p, const uint8_t *payload, int n);
static inline bool pbulk_panel_free(const pbulk_panel_t *p) { return !p->next_full; }  // pbulk_panel_put() takes one

// ---- ESP side
#define PBULK_ESP_BUF 2048
#define PBULK_MARGIN_US 250       // extra margin after each frame that wasn't answered
#define PBULK_MARGIN_MAX_US 2000  // the most margin it builds up
#define PBULK_MARGIN_DECAY_US 50  // and what each answer takes off it again

typedef struct {
  uint8_t q[PBULK_ESP_BUF];  // waiting messages in payload format, the first one numbered num
  uint16_t qn, num;
  uint16_t unsent;  // number of the first message that hasn't gone out, the panel may have the ones before
  int16_t last;     // offset of the last message, to append DATA to. -1 = none
  uint16_t session;
  uint8_t panel;       // its bus address
  bool sent;           // a frame went out, no answer yet
  uint16_t margin_us;  // grows when answers don't come, shrinks when they do
  uint16_t psess;      // the panel's session and the number of its next message we want, 0 = not heard yet
  uint16_t pnext;
  uint8_t want;           // bytes the panel has waiting
  bool refused, no_gaps;  // the panel said PB_REFUSED / PB_NO_GAPS, until pbulk_esp_retry()
  uint16_t end_us;        // the panel's deadline after a status broadcast, PBUS_END_US after a frame it didn't answer
  uint8_t ahead;          // answers in a row starting after pnext, see resyncs
  uint32_t frames, polls, answered, missed, busy, short_windows, got;
  uint32_t resyncs;   // times the panel skipped past the ones we wanted, those are lost
  uint32_t sessions;  // new sessions because the panel wanted messages we never sent
} pbulk_esp_t;

void pbulk_esp_init(pbulk_esp_t *t, uint16_t session, uint8_t panel);  // session: a random number
// pbulk_esp_send's flags
#define PBULK_REPLACE 1  // replaces an unsent one with the same key, for a PLINK_VALUE
#define PBULK_FIRST 2    // ahead of the unsent ones, and replaces one with the same key, for K_HELLO
// Queues a message for the panel, false if there's no room. DATA has to be a multiple of 4 bytes up to
// PBULK_DATA_MAX, anything else at most PBULK_MSG_MAX to fit the best window. Takes PBULK_MSG_HEAD + n bytes of
// pbulk_esp_room(), only n for DATA that joins the DATA before it.
bool pbulk_esp_send(pbulk_esp_t *t, uint16_t key, const uint8_t *b, int n, unsigned flags);
// Drops unsent messages except those keep() wants, after the panel restarted.
void pbulk_esp_drop_unsent(pbulk_esp_t *t, bool (*keep)(uint16_t key));
unsigned pbulk_esp_room(const pbulk_esp_t *t);              // free bytes in the queue
bool pbulk_esp_queued(const pbulk_esp_t *t, uint16_t key);  // one with this key the panel doesn't have yet?
// A send window. Counting from the latest the frame could start going out, there's at least left_us until
// everything has to be off the bus. Builds a frame or a poll in out, PBULK_FRAME_MAX bytes with the CRC, and
// returns its length. 0 if not even a poll fits or the panel refused, see pbulk_esp_retry().
int pbulk_esp_window(pbulk_esp_t *t, uint32_t left_us, uint8_t *out);
// Clears PB_REFUSED and PB_NO_GAPS, nothing else does. The ESP calls it a while after one, and the panel's next
// answer says if it still refuses.
void pbulk_esp_retry(pbulk_esp_t *t);
// Time left in a gap after a CX60 poll for a frame starting now, 0 = don't send.
uint32_t pbulk_esp_gap_left(const pbulk_esp_t *t, pbus_gaps_t *g, uint32_t now_us);
// The last frame got no answer. A window finds that out itself, this frees the gaps for frames sooner.
void pbulk_esp_no_answer(pbulk_esp_t *t);
// The last frame took over_us longer to start than allowed. Raises the margin to at least that.
void pbulk_esp_took(pbulk_esp_t *t, int32_t over_us);
// An F7 67 frame seen on the bus. Drops what the panel has of ours and passes its new messages to f.
void pbulk_esp_answer(pbulk_esp_t *t, const uint8_t *a, int n,
                      void (*f)(void *u, uint16_t key, const uint8_t *b, int len), void *u);

#ifdef __cplusplus
}
#endif
