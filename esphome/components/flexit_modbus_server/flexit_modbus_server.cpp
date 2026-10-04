#include "flexit_modbus_server.h"
#ifdef USE_FLEXIT_TCP_BRIDGE
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace flexit_modbus_server {

// Expected total length of the frame starting at `frame` (function code at
// frame[1]), given `available` bytes are present. The CS60 blasts frames with no
// interframe gap, so we size each one by its function code and let the CRC confirm
// the boundary. Every frame on this bus is 8 bytes except the 0x10 register
// broadcasts (sized by their byte-count field) and exceptions (5). Returns 0 for
// a function code we don't handle, so the caller resyncs.
static size_t expected_frame_length(const uint8_t *frame, size_t available) {
  const uint8_t function = frame[1];

  switch (function) {
    case 0x01:    // read coils (the CS60's coil poll)
    case 0x03:    // read holding registers
    case 0x04:    // read input registers (startup server discovery)
    case 0x06:    // write single register
    case 0x65:    // CS60 custom register/coil write
      return 8;
    case 0x10: {  // write multiple registers
      if (available < 7)
        return available + 1;                            // need the byte count (frame[6]) first
      // Modbus requires byte_count == 2 * quantity, with quantity in 1..123. Reject anything
      // else at once, so a phantom 0x10 header can't stall the splitter waiting for bytes.
      size_t quantity = (frame[4] << 8) | frame[5];
      size_t byte_count = frame[6];
      if (quantity == 0 || quantity > 123 || byte_count != quantity * 2)
        return 0;
      return byte_count + 9;                             // 7 header + byte_count data + 2 CRC
    }
    default:
      return (function & 0x80) ? 5 : 0;                  // exception response, else unknown
  }
}

std::string mode_to_string(uint16_t mode) {
  if (mode < NUM_MODES) {
    return MODE_STRINGS[mode];
  }
  return "Invalid mode";
}

uint16_t string_to_mode(StringRef mode_str) {
  for (uint16_t i = 0; i < NUM_MODES; ++i) {
    if (mode_str == MODE_STRINGS[i]) {
      return i;
    }
  }
  // Default to "Normal" mode if not found.
  return 2;
}

FlexitModbusServer::FlexitModbusServer() {}

void FlexitModbusServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Flexit Modbus Server:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", server_address_);
  ESP_LOGCONFIG(TAG, "  Baud Rate: %u", baudRate());

  if (tx_enable_pin_ >= 0) {
    ESP_LOGCONFIG(TAG, "  TX Enable Pin: GPIO%d", tx_enable_pin_);
    ESP_LOGCONFIG(TAG, "  TX Enable Direct: %s", tx_enable_direct_ ? "YES" : "NO");
  }

#ifdef USE_FLEXIT_TCP_BRIDGE
  ESP_LOGCONFIG(TAG, "  TCP Bridge Enabled: %s", tcp_bridge_enabled_ ? "YES" : "NO");

  if (tcp_bridge_enabled_) {
    ESP_LOGCONFIG(TAG, "  TCP Bridge Port: %u", tcp_bridge_port_);
    ESP_LOGCONFIG(TAG, "  TCP Bridge Max Clients: %u", tcp_bridge_max_clients_);

    if (tcp_server_ != nullptr &&
        wifi::global_wifi_component != nullptr &&
        wifi::global_wifi_component->is_connected()) {

      auto ips = wifi::global_wifi_component->get_ip_addresses();

      if (!ips.empty()) {
        char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
        ips[0].str_to(ip_buf);
        ESP_LOGCONFIG(TAG, "  TCP Bridge Status: Running on tcp://%s:%u",
                      ip_buf, tcp_bridge_port_);
      } else {
        ESP_LOGCONFIG(TAG, "  TCP Bridge Status: Waiting for IP...");
      }
    } else {
      ESP_LOGCONFIG(TAG, "  TCP Bridge Status: Waiting for WiFi...");
    }
  }
#endif  // USE_FLEXIT_TCP_BRIDGE
}

void FlexitModbusServer::setup() {
  // Initialize the new ModbusRTUServer instance using our Stream interface (this),
  // the baud rate from our UART parent, the server address, and the maximum number
  // of coils and holding registers.
  mb_.begin(this, baudRate(), server_address_, tx_enable_pin_, tx_enable_direct_, MAX_NUM_COILS, MAX_NUM_HOLDING_REGISTERS, 0, 4);

  // The CX60 doesnt follow the Modbus RTU spec: it ignores interframe gaps
  // and blasts frames back-to-back, so we can't delimit by timing. Take over
  // framing instead: walk the raw buffer, accept the first length whose CRC
  // validates, dispatch it, and keep any trailing partial frame for next time.
  mb_.onRawBuffer = [this](uint8_t* data, size_t length) -> size_t {
    size_t offset = 0;

    while (length - offset >= MIN_FRAME_LENGTH) {
      size_t avail = length - offset;
      size_t len = expected_frame_length(data + offset, avail);

      if (len == 0) {                            // not a frame start we know -> resync
        ++offset;
        continue;
      }
      if (len > avail)                           // frame not fully here yet -> wait
        break;
      if (!mb_.checkCrc(data + offset, len)) {   // right size, bad CRC -> resync
        ++offset;
        continue;
      }

      // The startup input-register reads (0x04) aren't implemented, so they reply
      // with an exception via onInvalidFunction. That's still a valid response, so
      // the CS60 sees us as online without us having to serve input registers.
      mb_.processFrame(data + offset, len);
      offset += len;
    }

    return offset;
  };

  // This is used as a cmd coil/register reset. Should we check the CRC?
  mb_.onInvalidFunction = [this](uint8_t* data, size_t length, bool broadcast) {
    uint8_t function_code = data[1];
    
    if (function_code == 0x65) {
        uint16_t address = (data[2] << 8) | data[3];
        uint16_t value = (data[4] << 8) | data[5];
        
        mb_.setHoldingRegister(address, value);
        mb_.setCoil(address, 0);
        on_registers_written_(address, 1);

        return;
    }

    mb_.sendException(data[1], 0x01, broadcast);
  };

  // Wrap the library's register write handlers so we know which runtime
  // counter words the CS60 has actually sent since boot.
  auto write_single = mb_.onWriteSingleRegister;
  mb_.onWriteSingleRegister = [this, write_single](uint8_t* data, size_t length, bool broadcast) {
    write_single(data, length, broadcast);
    if (length >= 6)
      on_registers_written_((data[2] << 8) | data[3], 1);
  };

  auto write_multiple = mb_.onWriteMultipleRegisters;
  mb_.onWriteMultipleRegisters = [this, write_multiple](uint8_t* data, size_t length, bool broadcast) {
    write_multiple(data, length, broadcast);
    if (length >= 6) {
      uint16_t quantity = (data[4] << 8) | data[5];
      if (quantity <= 123)
        on_registers_written_((data[2] << 8) | data[3], quantity);
    }
  };

  restore_settings_();
  restore_runtime_();

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_) {
    setup_tcp_bridge_();
  }
#endif  // USE_FLEXIT_TCP_BRIDGE
}

void FlexitModbusServer::loop() {
  mb_.update();

  if (millis() - last_save_check_ >= SAVE_CHECK_INTERVAL_MS) {
    last_save_check_ = millis();
    save_settings_if_changed_();
    save_runtime_if_needed_();
  }

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_) {
    handle_tcp_bridge_();
  }
#endif  // USE_FLEXIT_TCP_BRIDGE
}

void FlexitModbusServer::collect_settings_(SavedSettings &out) {
  size_t i = 0;
  for (const auto &range : SETTINGS_RANGES) {
    for (uint16_t reg = range.start; reg < range.start + range.count; reg++) {
      out.values[i++] = mb_.getHoldingRegister(reg);
    }
  }
}

void FlexitModbusServer::restore_settings_() {
  settings_pref_ = global_preferences->make_preference<SavedSettings>(fnv1_hash("flexit_settings"), true);

  if (!settings_pref_.load(&saved_settings_)) {
    ESP_LOGI(TAG, "No saved settings, waiting for the CS60 to send them");
    return;
  }

  // Only fills our local registers, no coils are set so nothing is sent to the CS60.
  size_t i = 0;
  for (const auto &range : SETTINGS_RANGES) {
    for (uint16_t reg = range.start; reg < range.start + range.count; reg++) {
      mb_.setHoldingRegister(reg, saved_settings_.values[i++]);
    }
  }
  ESP_LOGI(TAG, "Restored %u saved settings", (unsigned) NUM_SAVED_REGISTERS);
}

void FlexitModbusServer::save_settings_if_changed_() {
  SavedSettings current;
  collect_settings_(current);

  if (memcmp(&current, &saved_settings_, sizeof(SavedSettings)) == 0)
    return;

  // ESPHome batches the actual flash writes (flash_write_interval)
  saved_settings_ = current;
  settings_pref_.save(&saved_settings_);
  ESP_LOGD(TAG, "Settings changed, saved");
}

int FlexitModbusServer::runtime_index_(uint16_t high_reg) const {
  for (size_t i = 0; i < NUM_RUNTIME_COUNTERS; i++) {
    if (RUNTIME_HIGH_REGS[i] == high_reg)
      return i;
  }
  return -1;
}

bool FlexitModbusServer::runtime_known_(size_t i) const {
  if (runtime_restored_ & (1u << i))
    return true;
  uint16_t high_bit = RUNTIME_HIGH_REGS[i] - RUNTIME_FIRST;
  return (runtime_received_ & (1u << high_bit)) && (runtime_received_ & (1u << (high_bit + 1)));
}

void FlexitModbusServer::on_registers_written_(uint16_t start, uint16_t count) {
  uint32_t end = static_cast<uint32_t>(start) + count;
  if (end <= RUNTIME_FIRST || start > RUNTIME_LAST)
    return;

  for (uint32_t reg = std::max<uint32_t>(start, RUNTIME_FIRST); reg < std::min<uint32_t>(end, RUNTIME_LAST + 1); reg++)
    runtime_received_ |= 1u << (reg - RUNTIME_FIRST);

  for (size_t i = 0; i < NUM_RUNTIME_COUNTERS; i++) {
    uint16_t high = RUNTIME_HIGH_REGS[i];
    uint16_t low = high + 1;
    if (low < start || low >= end)
      continue;

    uint16_t new_low = mb_.getHoldingRegister(low);
    bool high_received = runtime_received_ & (1u << (high - RUNTIME_FIRST));

    // Low word went backwards = it wrapped since we saved, so the saved high is one behind.
    if ((runtime_restored_ & (1u << i)) && !high_received && new_low < last_low_[i]) {
      mb_.setHoldingRegister(high, mb_.getHoldingRegister(high) + 1);
      ESP_LOGD(TAG, "Runtime 0x%03X wrapped while we were off, high word bumped", high);
    }
    last_low_[i] = new_low;
  }
}

void FlexitModbusServer::restore_runtime_() {
  runtime_pref_ = global_preferences->make_preference<SavedRuntime>(fnv1_hash("flexit_runtime"), true);

  if (!runtime_pref_.load(&saved_runtime_)) {
    saved_runtime_ = SavedRuntime{};
    ESP_LOGI(TAG, "No saved runtime counters, they stay unknown until the CS60 sends them");
    return;
  }

  for (size_t i = 0; i < NUM_RUNTIME_COUNTERS; i++) {
    if (!(saved_runtime_.known & (1u << i)))
      continue;
    mb_.setHoldingRegister(RUNTIME_HIGH_REGS[i], saved_runtime_.high[i]);
    mb_.setHoldingRegister(RUNTIME_HIGH_REGS[i] + 1, saved_runtime_.low[i]);
    last_low_[i] = saved_runtime_.low[i];
    runtime_restored_ |= 1u << i;
  }
}

void FlexitModbusServer::save_runtime_if_needed_() {
  SavedRuntime current{};
  for (size_t i = 0; i < NUM_RUNTIME_COUNTERS; i++) {
    if (!runtime_known_(i))
      continue;
    current.high[i] = mb_.getHoldingRegister(RUNTIME_HIGH_REGS[i]);
    current.low[i] = mb_.getHoldingRegister(RUNTIME_HIGH_REGS[i] + 1);
    current.known |= 1u << i;
  }

  // Low words tick every second, so only save them once an hour. A new counter
  // or a changed high word gets saved right away.
  bool important = current.known != saved_runtime_.known ||
                   memcmp(current.high, saved_runtime_.high, sizeof(current.high)) != 0;
  bool low_changed = memcmp(current.low, saved_runtime_.low, sizeof(current.low)) != 0;
  bool due = millis() - last_runtime_save_ >= RUNTIME_SAVE_INTERVAL_MS;

  if (!important && !(low_changed && due))
    return;

  saved_runtime_ = current;
  runtime_pref_.save(&saved_runtime_);
  last_runtime_save_ = millis();
  ESP_LOGD(TAG, "Runtime counters saved");
}

void FlexitModbusServer::write_holding_register(HoldingRegisterIndex reg, uint16_t value) {
  mb_.setHoldingRegister(reg, value);
}

uint16_t FlexitModbusServer::read_holding_register(HoldingRegisterIndex reg) {
  return mb_.getHoldingRegister(reg);
}

float FlexitModbusServer::read_holding_register_temperature(HoldingRegisterIndex reg) {
  // Convert the raw register value to a temperature (divide by 10).
  return static_cast<int16_t>(mb_.getHoldingRegister(reg)) / 10.0f;
}

float FlexitModbusServer::read_holding_register_hours(HoldingRegisterIndex high_reg) {
  // Unknown until we have both words, either from flash or from the CS60
  int i = runtime_index_(high_reg);
  if (i >= 0 && !runtime_known_(i))
    return NAN;

  // Combine two registers: the high word and the subsequent low word.
  uint32_t rawSeconds = (static_cast<uint32_t>(mb_.getHoldingRegister(high_reg)) << 16)
                          + static_cast<uint32_t>(mb_.getHoldingRegister(high_reg + 1));
  return rawSeconds / 3600.0f;
}

void FlexitModbusServer::send_cmd(HoldingRegisterIndex cmd_register, uint16_t value) {
  // Write the command value to the register and set the corresponding coil.
  mb_.setHoldingRegister(cmd_register, value);
  mb_.setCoil(cmd_register, 1);
}

// ---------------------------------------------------------
// ESPHome UART Device Requirements
// ---------------------------------------------------------
uint32_t FlexitModbusServer::baudRate() {
  // Return the baud rate from the parent UART device.
  return this->parent_->get_baud_rate();
}

// ---------------------------------------------------------
// Setters (for configuration via __init__.py)
// ---------------------------------------------------------
void FlexitModbusServer::set_server_address(uint8_t address) {
  server_address_ = address;
}

void FlexitModbusServer::set_tx_enable_pin(int16_t pin) {
  tx_enable_pin_ = pin;
}

void FlexitModbusServer::set_tx_enable_direct(bool val) {
  tx_enable_direct_ = val;
}

#ifdef USE_FLEXIT_TCP_BRIDGE
void FlexitModbusServer::set_tcp_bridge_enabled(bool enabled) {
  tcp_bridge_enabled_ = enabled;
}

void FlexitModbusServer::set_tcp_bridge_port(uint16_t port) {
  tcp_bridge_port_ = port;
}

void FlexitModbusServer::set_tcp_bridge_max_clients(uint8_t max_clients) {
  tcp_bridge_max_clients_ = max_clients;
}
#endif  // USE_FLEXIT_TCP_BRIDGE

// ---------------------------------------------------------
// Stream interface implementation (required by ModbusRTUServer)
// ---------------------------------------------------------
size_t FlexitModbusServer::write(uint8_t data) {
  size_t result = uart::UARTDevice::write(data);

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_ && tcp_server_ != nullptr && !tcp_clients_.empty()) {
    uart_to_tcp_buffer_.push_back(data);
  }
#endif

  return result;
}

int FlexitModbusServer::available() {
  return uart::UARTDevice::available();
}

int FlexitModbusServer::read() {
  int v = uart::UARTDevice::read();
  
  if (v < 0) {
    return v;
  }

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_ && tcp_server_ != nullptr && !tcp_clients_.empty()) {
    uart_rx_mirror_.push_back(static_cast<uint8_t>(v));
  }
#endif

  return v;
}

int FlexitModbusServer::peek() {
  return uart::UARTDevice::peek();
}

void FlexitModbusServer::flush() {
  uart::UARTDevice::flush();

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (!(tcp_bridge_enabled_ && tcp_server_ != nullptr))
    return;

  auto send_framed_block = [this](const std::vector<uint8_t> &buf, uint8_t dir) {
    if (buf.empty())
      return;

    uint16_t len = static_cast<uint16_t>(buf.size());
    uint8_t header[3] = {
      dir,
      static_cast<uint8_t>((len >> 8) & 0xFF),
      static_cast<uint8_t>(len & 0xFF),
    };

    for (auto &client : tcp_clients_) {
      if (!client.connected())
        continue;

      client.write(header, sizeof(header));
      client.write(buf.data(), buf.size());
    }
  };

  if (!uart_to_tcp_buffer_.empty()) {
    send_framed_block(uart_to_tcp_buffer_, FRAME_DIR_TX);
    uart_to_tcp_buffer_.clear();
  }

  if (!uart_rx_mirror_.empty()) {
    send_framed_block(uart_rx_mirror_, FRAME_DIR_RX);
    uart_rx_mirror_.clear();
  }
#endif  // USE_FLEXIT_TCP_BRIDGE
}

// ---------------------------------------------------------
// TCP Bridge Implementation
// ---------------------------------------------------------
#ifdef USE_FLEXIT_TCP_BRIDGE
void FlexitModbusServer::setup_tcp_bridge_() {
  if (wifi::global_wifi_component == nullptr ||
      !wifi::global_wifi_component->is_connected()) {
    return;
  }

  auto ips = wifi::global_wifi_component->get_ip_addresses();
  if (ips.empty()) {
    return;
  }

  tcp_server_ = new WiFiServer(tcp_bridge_port_);
  tcp_server_->begin();
  tcp_server_->setNoDelay(true);

  char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ips[0].str_to(ip_buf);
  ESP_LOGI(TAG, "TCP Bridge: Server started on tcp://%s:%u (max clients: %u)",
          ip_buf, tcp_bridge_port_, tcp_bridge_max_clients_);
}

void FlexitModbusServer::handle_tcp_bridge_() {
  if (tcp_server_ == nullptr) {
    if (wifi::global_wifi_component != nullptr &&
        wifi::global_wifi_component->is_connected()) {
      setup_tcp_bridge_();
    }
    return;
  }

  accept_tcp_clients_();
  cleanup_tcp_clients_();
}

void FlexitModbusServer::accept_tcp_clients_() {
  if (tcp_server_->hasClient()) {
    WiFiClient new_client = tcp_server_->accept();

    if (tcp_clients_.size() >= tcp_bridge_max_clients_) {
      ESP_LOGW(TAG, "TCP Bridge: Max clients (%u) reached, rejecting connection from %s",
              tcp_bridge_max_clients_, new_client.remoteIP().toString().c_str());
      new_client.stop();
    } else {
      tcp_clients_.push_back(new_client);
      ESP_LOGI(TAG, "TCP Bridge: Client connected from %s (total: %u)",
              new_client.remoteIP().toString().c_str(), tcp_clients_.size());
    }
  }
}

void FlexitModbusServer::cleanup_tcp_clients_() {
  for (auto it = tcp_clients_.begin(); it != tcp_clients_.end();) {
    if (!it->connected()) {
      ESP_LOGI(TAG, "TCP Bridge: Client disconnected from %s (total: %u)",
              it->remoteIP().toString().c_str(), tcp_clients_.size() - 1);

      it->stop();
      it = tcp_clients_.erase(it);
    } else {
      ++it;
    }
  }
}
#endif  // USE_FLEXIT_TCP_BRIDGE

}  // namespace flexit_modbus_server
}  // namespace esphome
