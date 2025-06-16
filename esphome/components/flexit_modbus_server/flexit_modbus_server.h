#pragma once
#define DEBUG

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome.h"
#include "ModbusRTUServer.h"

#define MODBUS_DISABLE_READ_INPUT_REGISTERS
#define MODBUS_DISABLE_WRITE_MULTIPLE_COILS
#define MODBUS_DISABLE_WRITE_SINGLE_COIL
#define MODBUS_DISABLE_REPORT_SERVER_ID

namespace {
static constexpr const char *const TAG = "FlexitModbus";

// The starting addresses of each Modbus table (coils, holding regs, etc.).
static constexpr uint16_t COIL_START_ADDRESS              = 0x00;
static constexpr uint16_t HOLDING_REGISTER_START_ADDRESS  = 0x00;

static constexpr uint16_t MAX_NUM_COILS                   = 1000;

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
enum HoldingRegisterIndex {
  // Main registers
  REG_TEMPERATURE_SETPOINT                  = 0xBE,
  REG_MODE                                  = 0xBF,
  REG_UNKNOWN_1                             = 0xC0, // Possibly related to the MAX TIMER function.
  REG_UNKNOWN_2                             = 0xC1,
  REG_TEMPERATURE_SETPOINT_2                = 0xC2,
  REG_TEMPERATURE_SUPPLY_AIR                = 0xC3,
  REG_TEMPERATURE_EXTRACT_AIR               = 0xC4,
  REG_TEMPERATURE_OUTDOOR_AIR               = 0xC5,
  REG_TEMPERATURE_RETURN_WATER              = 0xC6,
  REG_PERCENTAGE_COOLING                    = 0xC7,
  REG_PERCENTAGE_HEAT_EXCHANGER             = 0xC8,
  REG_PERCENTAGE_HEATING                    = 0xC9,
  REG_PERCENTAGE_SUPPLY_FAN                 = 0xCA,

  // Alarm registers
  REG_ALARM_SENSOR_SUPPLY_FAULTY            = 0x104,
  REG_ALARM_SENSOR_EXTRACT_FAULTY           = 0x105,
  REG_ALARM_SENSOR_OUTDOOR_FAULTY           = 0x106,
  REG_ALARM_SENSOR_RETURN_WATER_FAULTY      = 0x107,
  REG_ALARM_SENSOR_OVERHEAT_TRIGGERED       = 0x108,
  REG_ALARM_SENSOR_SMOKE_EXTERNAL_TRIGGERED = 0x109,
  REG_ALARM_SENSOR_WATER_COIL_FAULTY        = 0x10A,
  REG_ALARM_SENSOR_HEAT_EXCHANGER_FAULTY    = 0x10B,
  REG_ALARM_FILTER_CHANGE                   = 0x10C,
  REG_CMD_CLEAR_FILTER_ALARM                = 0x116,
  REG_CMD_CLEAR_ALARMS                      = 0x13C,

  // Control variables
  REG_CMD_MODE                              = 0x00,
  REG_CMD_PERCENTAGE_SUPPLY_FAN_MIN         = 0x02,
  REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL      = 0x03,
  REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX         = 0x04,
  REG_CMD_PERCENTAGE_EXTRACT_FAN_MIN        = 0x07,
  REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL     = 0x08,
  REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX        = 0x09,
  REG_CMD_TEMPERATURE_SETPOINT              = 0x0C,
  REG_CMD_TEMPERATURE_SUPPLY_MIN            = 0x0D,
  REG_CMD_TEMPERATURE_SUPPLY_MAX            = 0x0E,
  REG_CMD_DAYS_FILTER_CHANGE_INTERVAL       = 0x16,
  REG_CMD_MINUTES_MAX_TIMER                 = 0x91,
  REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL    = 0x114,
  REG_CMD_START_MAX_TIMER                   = 0x13A,
  REG_CMD_STOP_MAX_TIMER                    = 0x13B,

  // Heater
  REG_STATUS_HEATER                         = 0x10E,
  REG_CMD_HEATER                            = 0x13F,

  // Runtime registers
  REG_RUNTIME_STOP_HIGH                     = 0x14C,
  REG_RUNTIME_STOP_LOW                      = 0x14D,
  REG_RUNTIME_MIN_HIGH                      = 0x14E,
  REG_RUNTIME_MIN_LOW                       = 0x14F,
  REG_RUNTIME_NORMAL_HIGH                   = 0x150,
  REG_RUNTIME_NORMAL_LOW                    = 0x151,
  REG_RUNTIME_MAX_HIGH                      = 0x152,
  REG_RUNTIME_MAX_LOW                       = 0x153,
  REG_RUNTIME_ROTOR_HIGH                    = 0x154,
  REG_RUNTIME_ROTOR_LOW                     = 0x155,
  REG_RUNTIME_HEATER_HIGH                   = 0x156,
  REG_RUNTIME_HEATER_LOW                    = 0x157,

  REG_RUNTIME_HIGH                          = 0x15C,
  REG_RUNTIME_LOW                           = 0x15D,
  REG_RUNTIME_FILTER_HIGH                   = 0x15E,
  REG_RUNTIME_FILTER_LOW                    = 0x15F,

  MAX_NUM_HOLDING_REGISTERS = 0x160
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
 * @param mode_str The mode to convert.
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
 *  - Inherits from Stream to satisfy the ModbusRTUServer library’s interface.
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
   * @param reg   Which holding register to write. Valid range: [0 .. MAX_NUM_HOLDING_REGISTERS-1].
   * @param value A 16-bit value to store in the Holding Register array.
   */
  void write_holding_register(HoldingRegisterIndex reg, uint16_t value);

  /**
   * @brief Read a value from a Holding Register (by enum index).
   *
   * @param reg Which holding register to read.
   * @return The 16-bit value stored, or 0 if the index is out of range.
   */
  uint16_t read_holding_register(HoldingRegisterIndex reg);

  /**
   * @brief Read a temperature value from a Holding Register (by enum index).
   *
   * @param reg Which holding register to read.
   * @return The temperature (value/10.0), or 0 if the index is out of range.
   */
  float read_holding_register_temperature(HoldingRegisterIndex reg);

  /**
   * @brief Read a time (in hours) from two consecutive Holding Registers.
   *
   * @param high_reg The register holding the high word.
   * @return The time in hours.
   */
  float read_holding_register_hours(HoldingRegisterIndex high_reg);

  /**
   * @brief Send a command by writing to a Holding Register and setting the associated coil.
   *
   * @param cmd_register The command register index.
   * @param value        The value to write.
   */
  void send_cmd(HoldingRegisterIndex cmd_register, uint16_t value);

  // ----------------------------------------------------------------
  // Accessors for configuration (used by __init__.py in ESPHome)
  // ----------------------------------------------------------------
  /**
   * @brief Return the baud rate of the underlying UART device.
   * @return The currently configured baud rate.
   */
  uint32_t baudRate();

  /**
   * @brief Set the Modbus server/slave address.
   *
   * @param address The server address.
   */
  void set_server_address(uint8_t address);

  /**
   * @brief Configure the TX enable pin (if using half-duplex RS485).
   *
   * @param pin The GPIO pin that controls the RS485 driver enable line.
   */
  void set_tx_enable_pin(int16_t pin);

  /**
   * @brief Configure whether TX enable is high active or low.
   *
   * @param val True if TX enable is active high.
   */
  void set_tx_enable_direct(bool val);

  // ----------------------------------------------------------------
  // Stream interface (required by the ModbusRTUServer library)
  // ----------------------------------------------------------------
  size_t write(uint8_t data) override;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;

private:
  /// @brief The new Modbus RTU server object.
  ModbusRTUServer mb_;

  /**
   * @brief Reset a command coil if the associated state register already reflects the command.
   *
   * @param cmd_register   The command register index.
   * @param state_register The state register index.
   */
  // void reset_cmd_coil(HoldingRegisterIndex cmd_register, HoldingRegisterIndex state_register);

  /// @brief The Modbus server (server) address.
  uint8_t server_address_{1};

  /// @brief GPIO pin controlling driver enable for RS485 (if needed).
  int16_t tx_enable_pin_{-1};

  /// @brief Whether TX enable is active high (true) or low.
  bool tx_enable_direct_{true};
};

}  // namespace flexit_modbus
}  // namespace esphome
