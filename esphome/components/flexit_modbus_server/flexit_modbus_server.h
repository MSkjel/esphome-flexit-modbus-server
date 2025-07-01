#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome.h"
#include "ModbusRTU.h"

namespace {
static constexpr const char *const TAG = "FlexitModbus";

// The starting addresses of each Modbus table (coils, holding regs, etc.).
static constexpr uint16_t COIL_START_ADDRESS              = 0x00;
static constexpr uint16_t HOLDING_REGISTER_START_ADDRESS  = 0x00;

static constexpr uint16_t MAX_NUM_COILS                   = 335;

/**
 * @brief Possible textual representations for the different operation modes
 *        in the ventilation system.
 */
static constexpr const char *const MODE_STRINGS[] = {
  "Stop",
  "Min", 
  "Normal",
  "Max"
};

static constexpr size_t NUM_MODES = sizeof(MODE_STRINGS) / sizeof(MODE_STRINGS[0]);
}  // namespace

namespace esphome {
namespace flexit_modbus_server {

// ------------------------------------------------------------------
// Holding Register indices
// ------------------------------------------------------------------
/**
 * @brief Identifiers for Holding Registers in our Modbus server.
 */
enum HoldingRegisterIndex
{
  // Command registers
  REG_CMD_MODE                              = 0x00,
  REG_CMD_TEMPERATURE_SETPOINT              = 0x0C,

  // Main registers
  REG_TEMPERATURE_SETPOINT                  = 0xBE,
  REG_MODE,
  REG_UNKNOWN_1,                 // Pretty sure these have to do with the MAX TIMER function
  REG_UNKNOWN_2,
  REG_TEMPERATURE_SETPOINT_2,
  REG_TEMPERATURE_SUPPLY_AIR,
  REG_TEMPERATURE_EXTRACT_AIR,   // Is this correct? Mine doesnt seem to have a sensor for this. It makes sense as its the next item in the official documentation for the CI66
  REG_TEMPERATURE_OUTDOOR_AIR,
  REG_TEMPERATURE_RETURN_WATER,  // Is this correct? Mine doesnt seem to have a sensor for this. It makes sense as its the next item in the official documentation for the CI66
  REG_PERCENTAGE_COOLING,
  REG_PERCENTAGE_HEAT_EXCHANGER,
  REG_PERCENTAGE_HEATING,
  REG_PERCENTAGE_SUPPLY_FAN,

  // Alarm registers
  REG_ALARM_SENSOR_SUPPLY_FAULTY            = 0x104,
  REG_ALARM_SENSOR_EXTRACT_FAULTY,
  REG_ALARM_SENSOR_OUTDOOR_FAULTY,
  REG_ALARM_SENSOR_RETURN_WATER_FAULTY,
  REG_ALARM_SENSOR_OVERHEAT_TRIGGERED,
  REG_ALARM_SENSOR_SMOKE_EXTERNAL_TRIGGERED,
  REG_ALARM_SENSOR_WATER_COIL_FAULTY,
  REG_ALARM_SENSOR_HEAT_EXCHANGER_FAULTY,
  REG_ALARM_FILTER_CHANGE,

  // Heater
  REG_STATUS_HEATER                         = 0x10E,
  REG_CMD_HEATER                            = 0x13F,

  // Runtime registers
  REG_RUNTIME_STOP_HIGH                     = 0x14C,
  REG_RUNTIME_STOP_LOW,
  REG_RUNTIME_MIN_HIGH,
  REG_RUNTIME_MIN_LOW,
  REG_RUNTIME_NORMAL_HIGH,
  REG_RUNTIME_NORMAL_LOW,
  REG_RUNTIME_MAX_HIGH,
  REG_RUNTIME_MAX_LOW,
  REG_RUNTIME_ROTOR_HIGH,
  REG_RUNTIME_ROTOR_LOW,
  REG_RUNTIME_HEATER_HIGH,
  REG_RUNTIME_HEATER_LOW,

  REG_RUNTIME_HIGH                          = 0x15C,
  REG_RUNTIME_LOW,
  REG_RUNTIME_FILTER_HIGH,
  REG_RUNTIME_FILTER_LOW,

  MAX_NUM_HOLDING_REGISTERS
};

/**
 * @brief Convert a numeric mode value into a human-readable string.
 *
 * @param mode The mode ID to convert.
 * @return A string describing the mode, or "Invalid mode" if out of range.
 */
std::string mode_to_string(uint16_t mode);

/**
 * @brief Convert a human-readable string into a numeric mode value.
 *
 * @param mode The mode to convert.
 * @return The mode ID.
 */
uint16_t string_to_mode(std::string &mode_str);

// ------------------------------------------------------------------
// FlexitModbusServer
// ------------------------------------------------------------------
/**
 * @brief A Modbus server implementation for a Flexit ventilation system.
 *
 * This class:
 *  - Inherits from UARTDevice to handle UART-based Modbus RTU communication.
 *  - Inherits from Component for integration with the ESPHome lifecycle.
 *  - Inherits from Stream to satisfy requirements of the ModbusRTU library.
 */
class FlexitModbusServer : public esphome::uart::UARTDevice, public Component, public Stream {
  public:
    /**
    * @brief Default constructor.
    */
    explicit FlexitModbusServer();

    /**
    * @brief Priority for setup. We want this to be set up as soon as possible.
    */
    float get_setup_priority() const override { return setup_priority::BUS; }

    /**
    * @brief Called once by ESPHome during setup.
    */
    void setup() override;

    /**
    * @brief Called repeatedly by ESPHome in its main loop.
    */
    void loop() override;

    // ----------------------------------------------------------------
    // Custom methods for manipulating registers and coils
    // ----------------------------------------------------------------
    /**
    * @brief Write a value to a Holding Register (by enum index).
    *
    * @param reg   Which holding register to write. Valid range: [0 .. NUM_HOLDING_REGS-1].
    * @param value A 16-bit value to store in the Holding Register array.
    */
    void write_holding_register(HoldingRegisterIndex reg, uint16_t value);

    /**
    * @brief Read a value from a Holding Register (by enum index).
    *
    * @param reg Which holding register to read. Valid range: [0 .. NUM_HOLDING_REGS-1].
    * @return The 16-bit value stored, or 0 if the index is out of range.
    */
    uint16_t read_holding_register(HoldingRegisterIndex reg);

    /**
    * @brief Read a temperature value from a Holding Register (by enum index).
    *
    * @param reg Which holding register to read. Valid range: [0 .. NUM_HOLDING_REGS-1].
    * @return The 16-bit value converted to float / 10 , or 0 if the index is out of range.
    */
    float read_holding_register_temperature(HoldingRegisterIndex reg);

    /**
    * @brief Read a time in hours value from a Holding Register (by enum index).
    *
    * @param reg Which holding register to read. Valid range: [0 .. NUM_HOLDING_REGS-1].
    * @return The high value register << 16 + low value register converted to float / 3600.
    */
    float read_holding_register_hours(HoldingRegisterIndex reg);

    /**
    * @brief Write a boolean state to a Coil.
    *
    * @param coil  Which coil to write. Valid range: [0 .. NUM_COILS-1].
    * @param state True for ON (1), false for OFF (0).
    */
    // void write_coil(CoilIndex coil, bool state);

    /**
    * @brief Read the boolean state of a Coil.
    *
    * @param coil Which coil to read. Valid range: [0 .. NUM_COILS-1].
    * @return True if ON, false otherwise.
    */
    // bool read_coil(CoilIndex coil);

    void send_cmd(HoldingRegisterIndex cmd_register, uint16_t value);

    // ----------------------------------------------------------------
    // Accessors for configuration (used by __init__.py in ESPHome)
    // ----------------------------------------------------------------
    /**
    * @brief Return the baud rate of the underlying UART device.
    * @return The currently configured baud rate
    */
    uint32_t baudRate();

    /**
    * @brief Set the Modbus server/slave address.
    *
    * @param address The server address
    */
    void set_server_address(uint8_t address);

    /**
    * @brief Configure the TX enable pin (if using half-duplex RS485).
    *
    * @param pin The GPIO pin that controls the RS485 driver enable line.
    */
    void set_tx_enable_pin(int16_t pin);

    /**
    * @brief Configure whether TX enable is high active or low
    *
    * @param val True if high enable
    */
    void set_tx_enable_direct(bool val);

    // ----------------------------------------------------------------
    // Stream interface required by the ModbusRTU library
    // ----------------------------------------------------------------
    /**
    * @brief Write a single byte to the UART device.
    *
    * @param data The byte to send.
    * @return The number of bytes written (should be 1 on success).
    */
    size_t write(uint8_t data) override;

    /**
    * @brief Get the number of bytes available for reading in the UART buffer.
    * @return The number of bytes available.
    */
    int available() override;

    /**
    * @brief Read a single byte from the UART buffer.
    * @return The byte read (0-255) or -1 if no data is available.
    */
    int read() override;

    /**
    * @brief Peek at the next byte in the UART buffer without consuming it.
    * @return The next byte (0-255) or -1 if no data is available.
    */
    int peek() override;

    /**
    * @brief Flush the UART buffer, blocking until all pending data is sent.
    */
    void flush() override;

  private:
    /// @brief The Modbus RTU object that manages requests and responses.
    ModbusRTU mb_;

    void reset_cmd_coil(HoldingRegisterIndex cmd_register, HoldingRegisterIndex state_register);

    /// @brief The Modbus server (slave) address.
    uint8_t server_address_{1};

    /// @brief GPIO pin controlling driver enable for RS485 (if needed).
    int16_t tx_enable_pin_{-1};

    /// @brief Whether TX enable is high active or low
    bool tx_enable_direct_{true};
};

}  // namespace flexit_modbus
}  // namespace esphome