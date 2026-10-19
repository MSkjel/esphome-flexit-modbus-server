#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace esphome::uart {

// The bus: what the others sent us, and what we sent
class UARTComponent {
 public:
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  uint32_t get_baud_rate() const { return 115200; }
  size_t get_rx_full_threshold() { return 1; }
};

class UARTDevice {
 public:
  void set_uart_parent(UARTComponent *parent) { parent_ = parent; }
  size_t available() { return parent_->rx.size(); }
  bool read_array(uint8_t *data, size_t len) {
    if (len > parent_->rx.size())
      return false;
    for (size_t i = 0; i < len; i++) {
      data[i] = parent_->rx.front();
      parent_->rx.pop_front();
    }
    return true;
  }
  void write_array(const uint8_t *data, size_t len) { parent_->tx.insert(parent_->tx.end(), data, data + len); }
  void flush() {}

 protected:
  UARTComponent *parent_{nullptr};
};

}  // namespace esphome::uart
