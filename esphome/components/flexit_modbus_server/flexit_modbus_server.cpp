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
}

void FlexitModbusServer::loop() {
  mb_.update();

  // Reset command coils if the associated state has been applied. No longer needes as we have implemented the 0x65 reset?
  // reset_cmd_coil(REG_CMD_MODE, REG_MODE);
  // reset_cmd_coil(REG_CMD_TEMPERATURE_SETPOINT, REG_TEMPERATURE_SETPOINT);
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

// No longer needed as we have implemented the 0x65 reset?
// void FlexitModbusServer::reset_cmd_coil(HoldingRegisterIndex cmd_register, HoldingRegisterIndex state_register) {
//   if (mb_.getCoil(state_register) && mb_.getHoldingRegister(state_register) == mb_.getHoldingRegister(cmd_register)) {
//     mb_.setHoldingRegister(cmd_register, 0);
//     mb_.setCoil(cmd_register, 0);
//   }
// }

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
