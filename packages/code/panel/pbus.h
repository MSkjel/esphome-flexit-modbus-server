// CU60 bus timing as the link sees it, shared by both ends. Works out when the link may send.
//
// The status broadcast is FC16 to address 0, registers 0xBE..0x112, 179 bytes. After it the CU60 stays quiet for
// 20 ms from its start plus at least one 10 ms step, so 13.5 ms or more after it ends. The link uses that every
// cycle, and the gaps after the CU60's polls when it has more to send. Everything has to be off the bus
// PBUS_END_US after the end of the status frame, or by the end of the gap.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PBUS_LINK_ADDR 0xF7  // address of the link frames, nothing else on the bus uses it
#define PBUS_BYTE_US 87      // one byte at 115200 8N1
#define PBUS_STATUS_LEN 179
// Link deadline, counted from the end of the status frame. The CU60 is quiet for 13.5 ms after it, 14.0-14.5 ms
// measured.
#define PBUS_END_US 12000
// The panel refuses the link if the CU60 sends sooner than PBUS_CU60_MIN_US after a status frame. It lets it back
// in after PBUS_REARM good status broadcasts in a row, about 2 minutes, doubling each time it refuses again.
#define PBUS_CU60_MIN_US 13000
#define PBUS_REARM 1000
// Min quiet time on the bus before the ESP starts a frame. The panel spends 0.2 ms on a status frame in its UART
// interrupt and loses any bytes that arrive meanwhile.
#define PBUS_START_MIN_US 400
#define PBUS_ESP_START_US 500  // ESP worst case from deciding to send to the first bit of the frame
// Panel wait after its idle interrupt before answering, for the ESP's RS-485 driver to let go of the bus
#define PBUS_PANEL_WAIT_US 200
#define PBUS_PANEL_US 300  // panel worst case from its idle interrupt to the start of its answer
#define PBUS_TURN_US 400   // worst case from the end of the ESP's frame to the start of the answer

uint16_t pbus_crc16(const uint8_t *p, int n);  // Modbus CRC
bool pbus_crc_ok(const uint8_t *f, int n);
bool pbus_is_status(const uint8_t *f, int n);     // the CU60's status broadcast? Caller checks the CRC
bool pbus_is_cu60(const uint8_t *f, int n);       // f starts with a CU60 frame, not a server's reply. CRC checked
int pbus_status_part(const uint8_t *buf, int n);  // length of an unfinished status frame at the end of buf, 0 = none

// Upper bound on how long ago the status broadcast ended, for an ESP that reads its UART from a loop. The
// component's status_age_us() does the same. dt_us is the time since the previous loop's read, which had k bytes
// of the frame but not its end (pbus_status_part). The UART hands bytes over once rx_threshold have arrived, or 2
// byte times after the line goes quiet. isr_us is the worst-case interrupt latency.
uint32_t pbus_since_status(uint32_t dt_us, int k, int rx_threshold, uint32_t isr_us);

// ESP side. How long to wait before starting a frame when the bus has been quiet for quiet_us
uint32_t pbus_wait_us(uint32_t quiet_us);
// Time left for a frame started now, when the status frame ended at most age_us ago
uint32_t pbus_status_left(uint32_t age_us);

// ---- Gaps after the CU60's polls. Both ends track them the same way.
// The CU60 polls each panel's coils on its own 10 ms steps. The request goes out on one step, the reply is read on
// the next, and only then does it ask about anything the reply flagged. If nothing was flagged it sends nothing
// until the step after that, 20 ms after the request, plus 20 ms per absent panel it would poll next. It doesn't
// listen once the reply has ended. The gap closes PBUS_GAP_SLACK_US before that step.
// The steps keep to a 10 ms grid, but a request is late if the step before ran long, like when it dealt with a
// flag, broadcast counters or looked for panels. So only a request right after another poll or the status
// broadcast, with nothing else from the CU60 in between, and possibly within PBUS_GAP_GRID_US of the grid, counts
// as on time and opens a gap. The panel knows the time to the microsecond, the ESP only roughly.
// The polled panels only change when the CU60 looks for them with FC4, at startup or after a factory reset, so a
// panel seen once counts as present until then. Gaps are only used once two full cycles have passed since it
// looked, or since a second without a status broadcast, which means the CU60 is starting up. They're disabled when
// the CU60 comes back PBUS_GAP_EARLY_US sooner than its steps say, and enabled again after PBUS_GAP_REARM gaps in
// a row it kept to, a few minutes, doubling each time. Both ends keep timing gaps while disabled.
#define PBUS_GAP_STEP_US 10000
#define PBUS_GAP_GRID_US 1000
#define PBUS_GAP_SLACK_US 4000  // for a request PBUS_GAP_GRID_US late, a busy main loop, plus margin
#define PBUS_GAP_EARLY_US 2500
#define PBUS_GAP_REARM 3000

typedef struct {
  uint8_t present;                  // bit per panel address the CU60 has polled since it last looked for panels
  uint8_t cycles;                   // full cycles since it last looked for panels, up to 2
  bool started, probing;            // seen a status broadcast, CU60 looking for panels this cycle
  uint32_t status_us;               // start of the last status broadcast
  uint8_t req;                      // address of the on-time poll waiting for its reply, 0 = none
  bool counters;                    // the last poll reads the counters, coils from 0x14C. Else the settings
  uint32_t req_us;                  // earliest start of the last poll
  uint32_t step_lo_us, step_hi_us;  // start of the last poll or status broadcast, earliest and latest
  bool step_ok;                     // and nothing else from the CU60 since
  uint32_t next_us;                 // time from req_us to the CU60's next step after the gap
  bool open, armed, off;            // gap open now, timing the CU60's next frame, gaps disabled
  uint8_t offs;                     // times gaps were disabled
  uint32_t clean;                   // gaps the CU60 kept to since
  uint32_t gaps;
} pbus_gaps_t;

// Feed it every frame on the bus in order, ours too, and any bytes that aren't a frame, n < 4 is fine. The frame
// started between start_lo_us and start_hi_us, equal when the time is known.
void pbus_gaps_frame(pbus_gaps_t *g, const uint8_t *f, int n, uint32_t start_lo_us, uint32_t start_hi_us);
uint32_t pbus_gaps_left(pbus_gaps_t *g, uint32_t now_us);  // time left in a gap, 0 = none

// ---- Panel side, called from its RX interrupt (it knows the time to the microsecond)
// Bus status in the panel's answers. REFUSED means the CU60 came back too soon after its status broadcast, so no
// link at all. NO_GAPS means it came back too soon after a poll, so don't use the gaps.
enum { PBUS_OK, PBUS_REFUSED, PBUS_NO_GAPS };

typedef struct {
  pbus_gaps_t gaps;
  bool after_status;       // the last burst ended with a status broadcast, only link frames since
  uint32_t status_end_us;  // when that status broadcast ended
  uint32_t status_ms;      // and the 1 ms tick then
  bool refused;            // the CU60 came back too soon after one, no link for now
  uint8_t refusals;        // times refused
  uint32_t clean;          // status broadcasts since with a long enough quiet time after
  // Shortest quiet time after a status broadcast since start or since the link was let back in. UINT32_MAX = none
  uint32_t quiet_min_us;
} pbus_panel_t;

void pbus_panel_init(pbus_panel_t *w);
// A burst on the bus that isn't a link frame, from the CU60, another server or our own reply. It started at
// start_us and the line went idle at idle_us, one byte time after its end. now_ms is the 1 ms tick, to cross-check the
// microsecond clock.
void pbus_panel_burst(pbus_panel_t *w, const uint8_t *b, int n, uint32_t start_us, uint32_t idle_us, uint32_t now_ms);
// A link frame, the line idle after it at idle_us. Returns the time from then until everything has to be off the
// bus, 0 outside the quiet time and the gaps. *say gets the PBUS_* status to report.
uint32_t pbus_panel_window(pbus_panel_t *w, uint32_t idle_us, uint32_t now_ms, uint8_t *say);

#ifdef __cplusplus
}
#endif
