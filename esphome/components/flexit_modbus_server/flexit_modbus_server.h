#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome.h"
#include "ModbusRTU.h"

namespace {
static const char *const TAG = "FlexitModbus";

// The starting addresses of each Modbus table (coils, holding regs, etc.).
// If you don't need dummy indices in HoldingRegisterIndex, you can adjust
// these values and your enum definitions accordingly.
static constexpr uint16_t COIL_START_ADDRESS              = 0x0000;
static constexpr uint16_t DISC_INPUT_START_ADDRESS        = 0x0000;
static constexpr uint16_t HOLDING_REGISTER_START_ADDRESS  = 0x0000;
static constexpr uint16_t INPUT_REGISTER_START_ADDRESS    = 0x0000;

static constexpr uint16_t MAX_NUM_COILS                   = 125;
static constexpr uint16_t MAX_NUM_DISCRETE_INPUTS         = 20;
static constexpr uint16_t MAX_NUM_HOLDING_REGISTERS       = 400;
static constexpr uint16_t MAX_NUM_INPUT_REGISTERS         = 1;

/**
 * @brief Possible textual representations for the different operation modes
 *        in the ventilation system.
 */
static constexpr const char *const MODE_STRINGS[] = {
  "Stop",
  "Min", 
  "Normal",
  "Max",
  "Max In"
};

static constexpr size_t NUM_MODES = sizeof(MODE_STRINGS) / sizeof(MODE_STRINGS[0]);
}  // namespace

namespace esphome {
namespace flexit_modbus {

// ------------------------------------------------------------------
// Coil indices
// ------------------------------------------------------------------
/**
 * @brief Identifiers for each Coil in our Modbus server.
 */
enum CoilIndex {
  COIL_0,
  COIL_1,
  COIL_2,
  COIL_3,
  NUM_COILS = MAX_NUM_COILS
};

// ------------------------------------------------------------------
// Discrete Input indices
// ------------------------------------------------------------------
/**
 * @brief Identifiers for Discrete Inputs in our Modbus server.
 */
enum DiscreteInputIndex {
  DISC_INPUT_1,
  NUM_DISC_INPUTS = MAX_NUM_DISCRETE_INPUTS
};

// ------------------------------------------------------------------
// Holding Register indices
// ------------------------------------------------------------------
/**
 * @brief Identifiers for Holding Registers in our Modbus server.
 */
enum HoldingRegisterIndex {
  REG_DUMMY_0 = 0xBD,  ///< Unused or placeholder register index (hex 0xBD)
  REG_SETPOINT_TEMP,
  REG_REGULATION_MODE,
  REG_FILTER_TIMER,
  REG_UNK_2,
  REG_UNK_3,
  REG_SUPPLY_TEMPERATURE,
  REG_UNK_4,
  REG_OUTDOOR_TEMPERATURE,
  REG_UNK_5,
  REG_HEATER_PERCENTAGE,
  REG_HEAT_EXCHANGER_PERCENTAGE,
  REG_UNK_6,
  REG_SUPPLY_AIR_FAN_SPEED_PERCENTAGE,

  NUM_HOLDING_REGS = MAX_NUM_HOLDING_REGISTERS
};

// ------------------------------------------------------------------
// Input Register indices
// ------------------------------------------------------------------
/**
 * @brief Identifiers for Input Registers in our Modbus server.
 */
enum InputRegisterIndex {
  NUM_INPUT_REGS = MAX_NUM_INPUT_REGISTERS
};

/**
 * @brief Convert a numeric mode value into a human-readable string.
 *
 * @param mode The mode ID to convert.
 * @return A string describing the mode, or "Invalid mode" if out of range.
 */
std::string mode_to_string(uint16_t mode);

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
    * @brief Write a value to an Input Register (by enum index).
    *
    * @param reg   Which input register to write. Valid range: [0 .. NUM_INPUT_REGS-1].
    * @param value A 16-bit value to store in the Input Register array.
    */
    void write_input_register(InputRegisterIndex reg, uint16_t value);

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
    * @brief Write a boolean state to a Coil.
    *
    * @param coil  Which coil to write. Valid range: [0 .. NUM_COILS-1].
    * @param state True for ON (1), false for OFF (0).
    */
    void write_coil(CoilIndex coil, bool state);

    /**
    * @brief Read the boolean state of a Coil.
    *
    * @param coil Which coil to read. Valid range: [0 .. NUM_COILS-1].
    * @return True if ON, false otherwise.
    */
    bool read_coil(CoilIndex coil);

    /**
    * @brief Write a boolean state to a Discrete Input.
    *
    * @param discrete_input Which discrete input to write. Valid range: [0 .. NUM_DISC_INPUTS-1].
    * @param state          True for ON (1), false for OFF (0).
    */
    void write_discrete_input(DiscreteInputIndex discrete_input, bool state);

    // ----------------------------------------------------------------
    // Accessors for configuration (used by __init__.py in ESPHome)
    // ----------------------------------------------------------------
    /**
    * @brief Return the baud rate of the underlying UART device.
    * @return The currently configured baud rate (bits per second).
    */
    uint32_t baudRate();

    /**
    * @brief Set the Modbus server/slave address.
    *
    * @param address The server address (typical range: 1 .. 247).
    */
    void set_server_address(uint8_t address);

    /**
    * @brief Configure the TX enable pin (if using half-duplex RS485).
    *
    * @param pin The GPIO pin that controls the RS485 driver enable line.
    */
    void set_tx_enable_pin(int16_t pin);

    /**
    * @brief Configure whether TX enable is handled directly or automatically by the library.
    *
    * @param val True if we enable/disable TX manually in software;
    *            false if the library manages it automatically.
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

    /// @brief The Modbus server (slave) address (typical range: 1..247).
    uint8_t server_address_{1};

    /// @brief GPIO pin controlling driver enable for RS485 (if needed).
    int16_t tx_enable_pin_{-1};

    /// @brief Whether TX enable is controlled manually or automatically.
    bool tx_enable_direct_{true};
};

}  // namespace flexit_modbus
}  // namespace esphome
