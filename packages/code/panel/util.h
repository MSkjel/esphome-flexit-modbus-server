// What the panel's headers share: entities from id(), RAM, the loop's time slice and status texts
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace flexit::panel {

// Max time a loop spends on pictures and animations together, so the main loop and the bus callbacks keep running
inline constexpr uint32_t SLICE_US = 5000;

// Whether micros() hasn't reached deadline_us yet
inline bool before(uint32_t deadline_us) { return (int32_t) (micros() - deadline_us) < 0; }

// id() is a pointer in ESPHome's lambdas and a reference in the sim's harness
template<typename T> T *ptr_of(T *p) { return p; }
template<typename T> T *ptr_of(T &r) { return &r; }

template<typename T> void drop(std::vector<T> &v) { std::vector<T>().swap(v); }  // empty, with its RAM freed

// Whether the ESP has `bytes` free with some to spare, `block` of it in one piece
inline bool has_ram(size_t bytes, size_t block) {
  RAMAllocator<uint8_t> ram(RAMAllocator<uint8_t>::ALLOC_INTERNAL);  // vectors and new come from there, not PSRAM
  return ram.get_free_heap_size() >= bytes + 8192 && ram.get_max_free_block_size() >= block;
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
using panel::has_ram;
using panel::progress;
using panel::ptr_of;
using panel::status;
}  // namespace picture

}  // namespace flexit::panel
