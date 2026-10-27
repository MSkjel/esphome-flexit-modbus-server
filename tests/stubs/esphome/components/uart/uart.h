#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace esphome::uart {

// The bus: what the others sent us, and what we sent
class UARTComponent {
 public:
  std::vector<uint8_t> rx;
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
    std::copy_n(parent_->rx.begin(), len, data);
    parent_->rx.erase(parent_->rx.begin(), parent_->rx.begin() + len);
    return true;
  }
  void write_array(const uint8_t *data, size_t len) { parent_->tx.insert(parent_->tx.end(), data, data + len); }
  void flush() {}

 protected:
  UARTComponent *parent_{nullptr};
};

}  // namespace esphome::uart
