#include "flexit_modbus_server.h"

namespace esphome {
namespace flexit_modbus {

std::string mode_to_string(uint16_t mode) {
  if (mode < NUM_MODES) {
    return MODE_STRINGS[mode];
  }
  return "Invalid mode";
}

// ---------------------------------------------------------
// Constructor is needed for esphome
// ---------------------------------------------------------
FlexitModbusServer::FlexitModbusServer() {}

// ---------------------------------------------------------
// Setup
// ---------------------------------------------------------
void FlexitModbusServer::setup() {
  // Initialize the ModbusRTU instance with the underlying stream,
  mb_.begin(this, tx_enable_pin_, tx_enable_direct_);

  // Set this node's Modbus address
  mb_.server(server_address_);

  // Add Modbus registers to serve:
  mb_.addHreg(HOLDING_REGISTER_START_ADDRESS, 0, NUM_HOLDING_REGS);
  mb_.addIreg(INPUT_REGISTER_START_ADDRESS, 0, NUM_INPUT_REGS);
  mb_.addCoil(COIL_START_ADDRESS, false, NUM_COILS);
  mb_.addIsts(DISC_INPUT_START_ADDRESS, false, NUM_DISC_INPUTS);
}

// ---------------------------------------------------------
// Loop (called repeatedly)
// ---------------------------------------------------------
void FlexitModbusServer::loop() {
  // Let the Modbus library handle incoming requests.
  mb_.task();
}

// ---------------------------------------------------------
// Helpers to Set/Get Specific Registers or Coils
// ---------------------------------------------------------
void FlexitModbusServer::write_input_register(InputRegisterIndex reg, uint16_t value) {
  mb_.Ireg(reg, value);
}

void FlexitModbusServer::write_holding_register(HoldingRegisterIndex reg, uint16_t value) {
  mb_.Hreg(reg, value);
}

uint16_t FlexitModbusServer::read_holding_register(HoldingRegisterIndex reg) {
  return mb_.Hreg(reg);
}

void FlexitModbusServer::write_coil(CoilIndex coil, bool state) {
  mb_.Coil(coil, state);
}

bool FlexitModbusServer::read_coil(CoilIndex coil) {
  return mb_.Coil(coil);
}

void FlexitModbusServer::write_discrete_input(DiscreteInputIndex disc, bool state) {
  mb_.Ists(disc, state);
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
