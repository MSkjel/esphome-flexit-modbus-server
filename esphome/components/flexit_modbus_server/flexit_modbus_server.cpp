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

  return 2;
}

FlexitModbusServer::FlexitModbusServer() {}

void FlexitModbusServer::setup() {
  // Initialize the ModbusRTU instance with the underlying stream,
  mb_.begin(this, tx_enable_pin_, tx_enable_direct_);
  mb_.server(server_address_);

  // The CS60 seems to need one IReg for it to actually poll the servers....
  mb_.addIreg(0, 0, 1);
  mb_.addHreg(HOLDING_REGISTER_START_ADDRESS, 0, MAX_NUM_HOLDING_REGISTERS);
  mb_.addCoil(COIL_START_ADDRESS, false, MAX_NUM_COILS);
}

void FlexitModbusServer::loop() {
  // Let the Modbus library handle incoming requests.
  mb_.task();

  reset_cmd_coil(REG_CMD_MODE, REG_MODE);
  reset_cmd_coil(REG_CMD_TEMPERATURE_SETPOINT, REG_TEMPERATURE_SETPOINT);
  
  // The setting of the heater is not working correctly
  // reset_cmd_coil(REG_HEATER_CMD, REG_HEATER_ENABLED);
}

void FlexitModbusServer::write_holding_register(HoldingRegisterIndex reg, uint16_t value) {
  mb_.Hreg(reg, value);
}

uint16_t FlexitModbusServer::read_holding_register(HoldingRegisterIndex reg) {
  return mb_.Hreg(reg);
}

float FlexitModbusServer::read_holding_register_temperature(HoldingRegisterIndex reg) {
  return static_cast<int16_t>(mb_.Hreg(reg)) / 10.0f;
}

float FlexitModbusServer::read_holding_register_hours(HoldingRegisterIndex high_reg) 
{
  uint32_t rawSeconds = (static_cast<uint32_t>(mb_.Hreg(high_reg)) << 16) + static_cast<uint32_t>(mb_.Hreg(high_reg + 1));
  float hours = rawSeconds / 3600.0f;

  return hours;
}

void FlexitModbusServer::send_cmd(HoldingRegisterIndex cmd_register, uint16_t value) {
  mb_.Hreg(cmd_register, value);
  mb_.Coil(cmd_register, 1);

  ESP_LOGW(TAG, "Set register/coil %i", cmd_register);
}

void FlexitModbusServer::reset_cmd_coil(HoldingRegisterIndex cmd_register, HoldingRegisterIndex state_register) {
  if(mb_.Coil(cmd_register)) {
    if(mb_.Hreg(state_register) == mb_.Hreg(cmd_register)) {
      mb_.Coil(cmd_register, 0);
      mb_.Hreg(cmd_register, 0);

      ESP_LOGW(TAG, "Reset register/coil %i", cmd_register);
    }
  }
}

// ---------------------------------------------------------
// ESPHome UART Device Requirements
// ---------------------------------------------------------
uint32_t FlexitModbusServer::baudRate() {
  // Return the baud rate from the UART device
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
// Stream interface implementation
// (required by the ModbusRTU library)
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