// What the tests share: CHECK, and bus frames
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include "panel/pbus.h"

inline int fails;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("%s:%d: %s\n", __FILE__, __LINE__, #c); \
      fails++; \
    } \
  } while (0)

// f with its CRC
inline std::vector<uint8_t> frame(std::vector<uint8_t> f) {
  uint16_t crc = pbus_crc16(f.data(), (int) f.size());
  f.push_back(crc & 0xFF);
  f.push_back(crc >> 8);
  return f;
}

inline std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// the CX60 writing a register to everyone
inline std::vector<uint8_t> fc6(uint16_t reg, uint16_t value) {
  return frame({0, 0x06, (uint8_t) (reg >> 8), (uint8_t) reg, (uint8_t) (value >> 8), (uint8_t) value});
}

// and registers from `reg`
inline std::vector<uint8_t> fc16(uint16_t reg, const std::vector<uint16_t> &values) {
  std::vector<uint8_t> f = {0,
                            0x10,
                            (uint8_t) (reg >> 8),
                            (uint8_t) reg,
                            (uint8_t) (values.size() >> 8),
                            (uint8_t) values.size(),
                            (uint8_t) (2 * values.size())};
  for (uint16_t v : values) {
    f.push_back(v >> 8);
    f.push_back(v);
  }
  return frame(f);
}

// the CX60 passing on the value it took for a register it read from a panel
inline std::vector<uint8_t> fc65(uint16_t reg, uint16_t value) {
  return frame({0, 0x65, (uint8_t) (reg >> 8), (uint8_t) reg, (uint8_t) (value >> 8), (uint8_t) value});
}

// the CX60's status broadcast, these registers set and the rest 0
inline std::vector<uint8_t> status(const std::map<uint16_t, uint16_t> &regs) {
  std::vector<uint8_t> f(PBUS_STATUS_HEAD, PBUS_STATUS_HEAD + sizeof PBUS_STATUS_HEAD);
  for (int reg = PBUS_STATUS_FIRST; reg <= PBUS_STATUS_LAST; reg++) {
    auto it = regs.find(reg);
    uint16_t v = it == regs.end() ? 0 : it->second;
    f.push_back(v >> 8);
    f.push_back(v);
  }
  return frame(f);
}
