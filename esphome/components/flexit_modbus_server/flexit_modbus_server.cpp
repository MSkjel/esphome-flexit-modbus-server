#include "flexit_modbus_server.h"

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
}

void FlexitModbusServer::loop() {
  mb_.update();
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

// ---------------------------------------------------------
// Stream interface implementation (required by ModbusRTUServer)
// ---------------------------------------------------------
size_t FlexitModbusServer::write(uint8_t data) {
  return uart::UARTDevice::write(data);
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
}

}  // namespace flexit_modbus
}  // namespace esphome
