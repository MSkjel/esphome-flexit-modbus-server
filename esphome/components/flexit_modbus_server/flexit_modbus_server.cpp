#include "flexit_modbus_server.h"
#ifdef USE_FLEXIT_TCP_BRIDGE
#include "esphome/components/wifi/wifi_component.h"
#ifdef USE_ESP32
#include <lwip/sockets.h>
#endif
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

  ESP_LOGCONFIG(TAG, "  Persist Settings: %s", persist_settings_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Persist Runtime Counters: %s", persist_runtime_counters_ ? "YES" : "NO");

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
    size_t mirrored = 0;                         // how far the TCP bridge has been given

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

      // The bridge gets the bytes we skipped and then the frame, each as its own
      // block, before a reply to it is written.
      mirror_rx_(data + mirrored, offset - mirrored);
      mirror_rx_(data + offset, len);
      mirrored = offset + len;

      // The startup input-register reads (0x04) aren't implemented, so they reply
      // with an exception via onInvalidFunction. That's still a valid response, so
      // the CS60 sees us as online without us having to serve input registers.
      mb_.processFrame(data + offset, len);
      offset += len;
    }

    mirror_rx_(data + mirrored, offset - mirrored);
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
  if (!persist_settings_)
    return;

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

void FlexitModbusServer::save_settings_if_changed_(bool force) {
  if (!persist_settings_)
    return;

  SavedSettings current;
  collect_settings_(current);
  const uint32_t ms = millis();

  if (memcmp(&current, &last_settings_, sizeof(SavedSettings)) != 0) {
    last_settings_ = current;
    settings_changed_at_ = ms;
  }

  // Back to what's in flash already, e.g. a boost that put the fan speeds back
  if (memcmp(&current, &saved_settings_, sizeof(SavedSettings)) == 0) {
    settings_unsaved_ = false;
    return;
  }

  if (!settings_unsaved_) {
    settings_unsaved_ = true;
    settings_unsaved_since_ = ms;
  }

  if (!force && ms - settings_changed_at_ < SETTINGS_SETTLE_MS &&
      ms - settings_unsaved_since_ < SETTINGS_MAX_DELAY_MS)
    return;

  // ESPHome batches the actual flash writes (flash_write_interval)
  saved_settings_ = current;
  settings_pref_.save(&saved_settings_);
  settings_unsaved_ = false;
  ESP_LOGD(TAG, "Settings changed, saved");
}

void FlexitModbusServer::on_safe_shutdown() {
  // Runs before ESPHome writes everything to flash on a reboot
  save_settings_if_changed_(true);
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
  if (!persist_runtime_counters_)
    return;

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
  if (!persist_runtime_counters_)
    return;

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

void FlexitModbusServer::set_persist_settings(bool val) {
  persist_settings_ = val;
}

void FlexitModbusServer::set_persist_runtime_counters(bool val) {
  persist_runtime_counters_ = val;
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
  if (!tcp_clients_.empty()) {
    // A reply is written byte by byte, so the block grows until flush() ends it
    if (tcp_tx_block_ == NO_TCP_BLOCK && tcp_out_.size() + 3 < TCP_BUFFER_MAX) {
      tcp_tx_block_ = tcp_out_.size();
      tcp_out_.insert(tcp_out_.end(), {FRAME_DIR_TX, 0, 0});
    }
    if (tcp_tx_block_ != NO_TCP_BLOCK && tcp_out_.size() < TCP_BUFFER_MAX) {
      tcp_out_.push_back(data);
      size_t len = tcp_out_.size() - tcp_tx_block_ - 3;
      tcp_out_[tcp_tx_block_ + 1] = static_cast<uint8_t>(len >> 8);
      tcp_out_[tcp_tx_block_ + 2] = static_cast<uint8_t>(len & 0xFF);
    }
  }
#endif

  return result;
}

int FlexitModbusServer::available() {
  return uart::UARTDevice::available();
}

int FlexitModbusServer::read() {
  return uart::UARTDevice::read();
}

int FlexitModbusServer::peek() {
  return uart::UARTDevice::peek();
}

void FlexitModbusServer::flush() {
  uart::UARTDevice::flush();

#ifdef USE_FLEXIT_TCP_BRIDGE
  tcp_tx_block_ = NO_TCP_BLOCK;  // the reply is complete
#endif
}

void FlexitModbusServer::mirror_rx_(const uint8_t *data, size_t len) {
#ifdef USE_FLEXIT_TCP_BRIDGE
  if (len == 0 || tcp_clients_.empty())
    return;

  tcp_tx_block_ = NO_TCP_BLOCK;
  if (tcp_out_.size() + 3 + len > TCP_BUFFER_MAX)
    return;

  tcp_out_.insert(tcp_out_.end(), {FRAME_DIR_RX, static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len & 0xFF)});
  tcp_out_.insert(tcp_out_.end(), data, data + len);
#endif
}

// ---------------------------------------------------------
// TCP Bridge Implementation
// ---------------------------------------------------------
#ifdef USE_FLEXIT_TCP_BRIDGE
// Hands over what fits in the socket's send buffer and returns right away
static size_t write_nonblocking(WiFiClient &client, const uint8_t *data, size_t len) {
  if (len == 0)
    return 0;

#ifdef USE_ESP32
  // WiFiClient::write() waits up to 10 s for a client that has stopped reading
  int fd = client.fd();
  if (fd < 0)
    return 0;
  ssize_t sent = lwip_send(fd, data, len, MSG_DONTWAIT);
  return sent > 0 ? static_cast<size_t>(sent) : 0;
#else
  size_t room = client.availableForWrite();
  return room == 0 ? 0 : client.write(data, std::min(len, room));
#endif
}

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
  tcp_out_.reserve(TCP_BUFFER_MAX);

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
  send_to_tcp_clients_();
}

void FlexitModbusServer::accept_tcp_clients_() {
  if (tcp_server_->hasClient()) {
    WiFiClient new_client = tcp_server_->accept();

    if (tcp_clients_.size() >= tcp_bridge_max_clients_) {
      ESP_LOGW(TAG, "TCP Bridge: Max clients (%u) reached, rejecting connection from %s",
              tcp_bridge_max_clients_, new_client.remoteIP().toString().c_str());
      new_client.stop();
    } else {
      tcp_clients_.push_back({new_client, {}, false});
      ESP_LOGI(TAG, "TCP Bridge: Client connected from %s (total: %u)",
              new_client.remoteIP().toString().c_str(), tcp_clients_.size());
    }
  }
}

void FlexitModbusServer::cleanup_tcp_clients_() {
  for (auto it = tcp_clients_.begin(); it != tcp_clients_.end();) {
    if (!it->client.connected()) {
      ESP_LOGI(TAG, "TCP Bridge: Client disconnected from %s (total: %u)",
              it->client.remoteIP().toString().c_str(), tcp_clients_.size() - 1);

      it->client.stop();
      it = tcp_clients_.erase(it);
    } else {
      ++it;
    }
  }
}

void FlexitModbusServer::send_to_tcp_clients_() {
  for (auto &c : tcp_clients_) {
    if (c.pending.empty()) {
      c.lagging = false;
      size_t sent = write_nonblocking(c.client, tcp_out_.data(), tcp_out_.size());
      if (sent < tcp_out_.size())
        c.pending.assign(tcp_out_.begin() + sent, tcp_out_.end());
      continue;
    }

    // This client is behind. What's new is queued whole or skipped whole, so it
    // never gets half a block.
    if (c.pending.size() + tcp_out_.size() <= TCP_BUFFER_MAX) {
      c.pending.insert(c.pending.end(), tcp_out_.begin(), tcp_out_.end());
    } else if (!c.lagging) {
      c.lagging = true;
      ESP_LOGW(TAG, "TCP Bridge: Client %s reads too slowly, it will miss data",
              c.client.remoteIP().toString().c_str());
    }

    size_t sent = write_nonblocking(c.client, c.pending.data(), c.pending.size());
    c.pending.erase(c.pending.begin(), c.pending.begin() + sent);
  }

  tcp_out_.clear();
  tcp_tx_block_ = NO_TCP_BLOCK;
}
#endif  // USE_FLEXIT_TCP_BRIDGE

}  // namespace flexit_modbus_server
}  // namespace esphome
