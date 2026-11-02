// What the panel's headers share: entities from id(), RAM, the loop's time slice and status texts
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace flexit::panel {

// Max time a loop spends on pictures and animations together, so the main loop and the bus callbacks keep running
inline constexpr uint32_t SLICE_US = 5000;
// Kept clear of the end of the CX60's status broadcast, so the link's window right after it isn't held up: the UART
// interrupt, the task wake and the piece of work under way
inline constexpr uint32_t STATUS_MARGIN_US = 1500;

// Whether micros() hasn't reached deadline_us yet
inline bool before(uint32_t deadline_us) { return (int32_t) (micros() - deadline_us) < 0; }

// Most of a cycle before the next status broadcast ends, as right after one
inline constexpr uint32_t QUIET_LONG_US = 80000;

// Until STATUS_MARGIN_US before the status broadcast ends, UINT32_MAX when the bus can't tell
inline uint32_t quiet_us() {
  uint32_t until = id(server).until_status_end_us();
  if (until == UINT32_MAX)
    return UINT32_MAX;
  return until > STATUS_MARGIN_US ? until - STATUS_MARGIN_US : 0;
}

// The deadline for this loop's slice of work: SLICE_US from now, or less so it ends STATUS_MARGIN_US before the status
// broadcast does. Now, so only the quick bits run, when the broadcast is about to end.
inline uint32_t slice_deadline_us() {
  uint32_t quiet = quiet_us(), now = micros();
  return quiet < 1000 ? now : now + std::min(SLICE_US, quiet);
}

// Whether a piece of work that can't stop halfway may start, cost_us what it took last time, UINT32_MAX if not known.
// It fits what's left of the slice, or as the first such piece in the loop it can't reach the status broadcast: past
// the slice, but the main loop only waits once. One of a cost not known waits for the quiet right after a broadcast.
inline bool fits(uint32_t deadline_us, uint32_t cost_us, bool first) {
  uint32_t now = micros(), left = (int32_t) (deadline_us - now) > 0 ? deadline_us - now : 0;
  return cost_us <= left || (first && quiet_us() >= (cost_us == UINT32_MAX ? QUIET_LONG_US : cost_us));
}

// id() is a pointer in ESPHome's lambdas and a reference in the sim's harness
template<typename T> T *ptr_of(T *p) { return p; }
template<typename T> T *ptr_of(T &r) { return &r; }

template<typename T> void drop(std::vector<T> &v) { std::vector<T>().swap(v); }  // empty, with its RAM freed

// HTTPS requests in tasks of their own, met.h's and picture_online.h's, whose TLS buffers come and go meanwhile
inline std::atomic<int> requests{0};
inline constexpr size_t REQUEST_RAM = 24 * 1024;

// Whether the ESP has `bytes` free with some to spare, `block` of it in one piece
inline bool has_ram(size_t bytes, size_t block) {
  RAMAllocator<uint8_t> ram(RAMAllocator<uint8_t>::ALLOC_INTERNAL);  // vectors and new come from there, not PSRAM
  return ram.get_free_heap_size() >= bytes + 8192 + requests * REQUEST_RAM && ram.get_max_free_block_size() >= block;
}

// A status text sensor, published only when it changes
template<typename T> void status(T &sensor, const std::string &s) {
  if (sensor.state != s)
    sensor.publish_state(s);
}

// The status for a K_PIC_ACK's progress, len bytes in all
inline std::string progress(const std::string &name, uint32_t ack, uint32_t len) {
  return "Sending " + name + ": " + std::to_string(std::min<uint32_t>(99, (ack & 0xFFFF) * 100 / len)) + " %";
}

// Also in picture::, where met's code uses them
namespace picture {
using panel::drop;
using panel::fits;
using panel::has_ram;
using panel::progress;
using panel::ptr_of;
using panel::status;
}  // namespace picture

}  // namespace flexit::panel
