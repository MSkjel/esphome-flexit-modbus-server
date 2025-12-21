#include "flexit_modbus_server.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace flexit_modbus_server {

std::string mode_to_string(uint16_t mode) {
  if (mode < NUM_MODES) {
    return MODE_STRINGS[mode];
  }
  return "Invalid mode";
}

uint16_t string_to_mode(std::string &mode_str) {
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

  ESP_LOGCONFIG(TAG, "  TCP Bridge Enabled: %s", tcp_bridge_enabled_ ? "YES" : "NO");

  if (tcp_bridge_enabled_) {
    ESP_LOGCONFIG(TAG, "  TCP Bridge Port: %u", tcp_bridge_port_);
    ESP_LOGCONFIG(TAG, "  TCP Bridge Max Clients: %u", tcp_bridge_max_clients_);

    if (tcp_server_ != nullptr &&
        wifi::global_wifi_component != nullptr &&
        wifi::global_wifi_component->is_connected()) {

      auto ips = wifi::global_wifi_component->get_ip_addresses();

      if (!ips.empty()) {
        ESP_LOGCONFIG(TAG, "  TCP Bridge Status: Running on tcp://%s:%u",
                      ips[0].str().c_str(), tcp_bridge_port_);
      } else {
        ESP_LOGCONFIG(TAG, "  TCP Bridge Status: Waiting for IP...");
      }
    } else {
      ESP_LOGCONFIG(TAG, "  TCP Bridge Status: Waiting for WiFi...");
    }
  }
}

void FlexitModbusServer::setup() {
  // Initialize the new ModbusRTUServer instance using our Stream interface (this),
  // the baud rate from our UART parent, the server address, and the maximum number
  // of coils and holding registers.
  mb_.begin(this, baudRate(), server_address_, tx_enable_pin_, tx_enable_direct_, MAX_NUM_COILS, MAX_NUM_HOLDING_REGISTERS, 0, 4);
  // The CS/CU/CE60 doesnt actually follow the Modbus RTU spec. It just ignores any interframe timeout and blasts request.
  // It doesnt actually matter that much to us, until they send the 0x65 reset cmd coil/register frame. It gets blasted as a broadcast right after we send our response.
  mb_.setInterframeTimeout(0);

  // This is used as a cmd coil/register reset. Should we check the CRC?
  mb_.onInvalidFunction = [this](uint8_t* data, size_t length, bool broadcast) {
    uint8_t function_code = data[1];
    
    if (function_code == 0x65) {
        uint16_t address = (data[2] << 8) | data[3];
        uint16_t value = (data[4] << 8) | data[5];
        
        mb_.setHoldingRegister(address, value);
        mb_.setCoil(address, 0);
      
        return;
    }

    mb_.sendException(data[1], 0x01, broadcast);
  };

  #ifdef DEBUG
  // This is needed since the CS60 doesnt respect interframe timeouts. We get multiple frames in one buffer.
  mb_.onInvalidServer = [this](uint8_t* data, size_t length, bool broadcast) {
    size_t offset = 0;

    // Walk the buffer, frame by frame
    while (offset + 4 <= length) {
      size_t avail = length - offset;
      size_t flen  = modbus_frame_length(data + offset, avail);

      if (flen == 0 || flen > avail) 
        break;
      
      uint8_t id = data[offset + 0];
      uint8_t fn = data[offset + 1];

      if (fn == 0x65 && id == 0x0) {
        uint16_t addr  = (data[offset+2] << 8) | data[offset + 3];
        int16_t value = static_cast<int16_t>((data[offset + 4] << 8) | data[offset + 5]);
        
        //These two registers get spammed alot. Don't know what they are for.
        if (addr != 0x94 && addr != 0x95) {
          ESP_LOGW(TAG, "=== 0x65 PDU @ offset %u, len %u ===", (unsigned)offset, (unsigned)flen);
          ESP_LOG_BUFFER_HEXDUMP(TAG, data + offset, flen, ESP_LOG_ERROR);
          ESP_LOGW(TAG, "Received 0x65 reset command: address=0x%04X, value hex=0x%04X, value dec=%i",
                  addr, value, value);
        }
      }

      offset += flen;
    }
  };
  #endif

  if (tcp_bridge_enabled_) {
    setup_tcp_bridge_();
  }
}

void FlexitModbusServer::loop() {
  mb_.update();

  if (tcp_bridge_enabled_) {
    handle_tcp_bridge_();
  }
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
// Debugging functions
// ---------------------------------------------------------
#ifdef DEBUG
size_t FlexitModbusServer::modbus_frame_length(const uint8_t *buf, size_t avail) {
  if (avail < 4) return 0;
  
  const uint8_t fn = buf[1];
  
  // Exception responses
  if (fn & 0x80) {
      return (avail >= 5) ? 5 : 0;
  }
  
  // This controller uses 0x01 for the big read (332 registers)
  if (fn == 0x01) {
      if (avail < 3) return 0;
      
      // Response: byte count in buf[2], expect ~83 bytes for 332 registers (bits)
      if (buf[2] > 0 && buf[2] <= 250) {
          return 1 + 1 + 1 + buf[2] + 2;  // Response format
      }
      return (avail >= 8) ? 8 : 0;  // Request format
  }
  
  // 0x03 used for individual register reads
  if (fn == 0x03) {
      if (avail < 3) return 0;
      
      // Single register response will have byte count = 2
      if (buf[2] == 2 && avail >= 7) {
          return 7;  // ID + Fn + Count(1) + Data(2) + CRC(2)
      }
      return (avail >= 8) ? 8 : 0;  // Request format
  }
  
  // 0x06 (write single) and 0x65 (custom reset): always 8 bytes
  if (fn == 0x06 || fn == 0x65) {
      return (avail >= 8) ? 8 : 0;
  }
  
  return 0;
}
#endif

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

void FlexitModbusServer::set_tcp_bridge_enabled(bool enabled) {
  tcp_bridge_enabled_ = enabled;
}

void FlexitModbusServer::set_tcp_bridge_port(uint16_t port) {
  tcp_bridge_port_ = port;
}

void FlexitModbusServer::set_tcp_bridge_max_clients(uint8_t max_clients) {
  tcp_bridge_max_clients_ = max_clients;
}

// ---------------------------------------------------------
// Stream interface implementation (required by ModbusRTUServer)
// ---------------------------------------------------------
size_t FlexitModbusServer::write(uint8_t data) {
  size_t result = uart::UARTDevice::write(data);

  if (tcp_bridge_enabled_ && tcp_server_ != nullptr && !tcp_clients_.empty()) {
    uart_to_tcp_buffer_.push_back(data);
  }

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

  if (tcp_bridge_enabled_ && tcp_server_ != nullptr && !tcp_clients_.empty()) {
    uart_rx_mirror_.push_back(static_cast<uint8_t>(v));
  }

  return v;
}

int FlexitModbusServer::peek() {
  return uart::UARTDevice::peek();
}

void FlexitModbusServer::flush() {
  uart::UARTDevice::flush();

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
}

// ---------------------------------------------------------
// TCP Bridge Implementation
// ---------------------------------------------------------
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

  ESP_LOGI(TAG, "TCP Bridge: Server started on tcp://%s:%u (max clients: %u)",
          ips[0].str().c_str(), tcp_bridge_port_, tcp_bridge_max_clients_);
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

}  // namespace flexit_modbus_server
}  // namespace esphome
