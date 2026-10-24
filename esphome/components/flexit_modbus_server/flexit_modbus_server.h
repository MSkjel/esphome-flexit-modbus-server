#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/string_ref.h"
#include "ModbusRTUServer.h"

#ifdef USE_FLEXIT_TCP_BRIDGE
#include "esphome/components/socket/socket.h"
#include <memory>
#endif

#include <string>
#include <vector>

namespace {
static constexpr const char *const TAG = "FlexitModbus";

static constexpr uint16_t MAX_NUM_COILS = 1000;

// Shortest frame we try to parse (an exception response).
static constexpr size_t MIN_FRAME_LENGTH = 5;

static constexpr uint8_t FRAME_DIR_TX = 'T';
static constexpr uint8_t FRAME_DIR_RX = 'R';

/**
 * @brief Possible textual representations for the different operation modes
 *        in the ventilation system.
 */
static constexpr const char *const MODE_STRINGS[] = {"Stop", "Min", "Normal", "Max"};

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
  REG_TEMPERATURE_SETPOINT = 0xBE,         // x0.1 C, setpoint from the panels (or the external pot)
  REG_MODE = 0xBF,                         // 0 = stop, 1 = min, 2 = normal, 3 = max
  REG_HOURS_SINCE_FILTER_RESET = 0xC0,     // h
  REG_SECONDS_REMAINING_MAX_TIMER = 0xC1,  // s, 0 = not running
  REG_TEMPERATURE_SETPOINT_2 = 0xC2,       // x0.1 C, setpoint in use (after week program, home/away etc.)
  REG_TEMPERATURE_SUPPLY_AIR = 0xC3,       // x0.1 C, signed
  REG_TEMPERATURE_EXTRACT_AIR = 0xC4,      // x0.1 C, signed
  REG_TEMPERATURE_OUTDOOR_AIR = 0xC5,      // x0.1 C, signed
  REG_TEMPERATURE_RETURN_WATER = 0xC6,     // x0.1 C, signed
  REG_PERCENTAGE_COOLING = 0xC7,           // %
  REG_PERCENTAGE_HEAT_EXCHANGER = 0xC8,    // %
  REG_PERCENTAGE_HEATING = 0xC9,           // %
  REG_PERCENTAGE_SUPPLY_FAN = 0xCA,        // %
  REG_HOURS_TOTAL_RUNTIME = 0xCB,          // h

  // Alarm registers (1 = active)
  REG_ALARM_SENSOR_SUPPLY_FAULTY = 0x104,
  REG_ALARM_SENSOR_EXTRACT_FAULTY = 0x105,
  REG_ALARM_SENSOR_OUTDOOR_FAULTY = 0x106,
  REG_ALARM_SENSOR_RETURN_WATER_FAULTY = 0x107,
  REG_ALARM_SENSOR_OVERHEAT_TRIGGERED = 0x108,
  REG_ALARM_SENSOR_SMOKE_EXTERNAL_TRIGGERED = 0x109,
  REG_ALARM_SENSOR_WATER_COIL_FAULTY = 0x10A,
  REG_ALARM_SENSOR_HEAT_EXCHANGER_FAULTY = 0x10B,
  REG_ALARM_FILTER_CHANGE = 0x10C,
  REG_CMD_CLEAR_FILTER_ALARM = 0x116,  // write 1, also restarts the filter hours
  REG_CMD_CLEAR_ALARMS = 0x13C,        // write 1

  // Control variables
  REG_CMD_MODE = 0x00,                             // 0 = stop, 1 = min, 2 = normal, 3 = max
  REG_CMD_PERCENTAGE_SUPPLY_FAN_MIN = 0x02,        // %
  REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL = 0x03,     // %, only taken from the lowest panel address
  REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX = 0x04,        // %
  REG_CMD_PERCENTAGE_EXTRACT_FAN_MIN = 0x07,       // %
  REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL = 0x08,    // %, only taken from the lowest panel address
  REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX = 0x09,       // %
  REG_CMD_TEMPERATURE_SETPOINT = 0x0C,             // x0.1 C, only taken from the lowest panel address
  REG_CMD_TEMPERATURE_SUPPLY_MIN = 0x0D,           // x0.1 C
  REG_CMD_TEMPERATURE_SUPPLY_MAX = 0x0E,           // x0.1 C
  REG_CMD_DAYS_FILTER_CHANGE_INTERVAL = 0x16,      // days
  REG_CMD_MINUTES_MAX_TIMER = 0x91,                // min
  REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL = 0x114,  // 1 = supply air, 0 = extract air regulation
  REG_CMD_START_MAX_TIMER = 0x13A,                 // write 1
  REG_CMD_STOP_MAX_TIMER = 0x13B,                  // write 1

  // Heater
  REG_STATUS_HEATER = 0x10E,      // 1 = heater allowed to run
  REG_CMD_HEATER = 0x13F,         // heater on/off flag of panel address 1, then those of 2 and 3. The CS60
  REG_CMD_HEATER_PANEL3 = 0x141,  // ORs the flags of the panels present

  // Runtime registers (seconds, 32 bit split in a high and low word)
  REG_RUNTIME_STOP_HIGH = 0x14C,
  REG_RUNTIME_STOP_LOW = 0x14D,
  REG_RUNTIME_MIN_HIGH = 0x14E,
  REG_RUNTIME_MIN_LOW = 0x14F,
  REG_RUNTIME_NORMAL_HIGH = 0x150,
  REG_RUNTIME_NORMAL_LOW = 0x151,
  REG_RUNTIME_MAX_HIGH = 0x152,
  REG_RUNTIME_MAX_LOW = 0x153,
  REG_RUNTIME_ROTOR_HIGH = 0x154,
  REG_RUNTIME_ROTOR_LOW = 0x155,
  REG_RUNTIME_HEATER_HIGH = 0x156,
  REG_RUNTIME_HEATER_LOW = 0x157,

  REG_RUNTIME_HIGH = 0x15C,  // total
  REG_RUNTIME_LOW = 0x15D,
  REG_RUNTIME_FILTER_HIGH = 0x15E,  // since the last filter reset
  REG_RUNTIME_FILTER_LOW = 0x15F,

  // Status / diagnostics (read only)
  REG_SW_VERSION_CONTROLLER = 0x097,     // major << 8 | minor
  REG_NUM_FILTER_RESETS = 0x0CC,         // count
  REG_OVERRIDING_SYSTEM_ACTIVE = 0x10D,  // Home/away input active
  REG_DS1_HEATING_TYPE = 0x10F,          // DIP switch: 0 = water, 1 = electric
  REG_DS2_ROTOR_GUARD_TYPE = 0x110,      // DIP switch
  REG_DS3_ROTOR_MOTOR_TYPE = 0x111,      // DIP switch
  REG_DS4_FAN_TYPE = 0x112,              // DIP switch: 0 = EC, 1 = AC
  REG_WEEK_PROGRAM_ACTIVE = 0x117,       // the CS60 follows the week program's periods only while 1. A stock
                                         // CI600 sets it while a period runs, the replacement one while it's on

  // Advanced settings (CI600 "Advanced user" / "Service" menus)
  REG_CMD_AIR_VOLUME_COMP_SUPPLY_SPEED = 0x005,   // AC fans: supply mode 1-3 while hood/fireplace input is active
  REG_CMD_AIR_VOLUME_COMP_EXTRACT_SPEED = 0x00A,  // AC fans: extract mode 1-3 while hood/fireplace input is active
  REG_CMD_FIRE_SMOKE_MODE = 0x00B,                // 1 = stop, 2 = supply+extract max, 3 = extract max, 4 = supply max
  REG_CMD_NEUTRAL_ZONE_COOLING_ROTOR = 0x011,     // x0.1 K
  REG_CMD_NEUTRAL_ZONE_ROTOR_HEATING = 0x012,     // x0.1 K
  REG_CMD_COOLING_MIN_OUTDOOR_TEMP = 0x013,       // x0.1 C
  REG_CMD_COOLING_RESTART_DELAY = 0x014,          // s
  REG_CMD_COOLING_MIN_SPEED = 0x015,              // 2 = normal, 3 = max
  REG_CMD_START_DELAY_1 = 0x017,                  // s
  REG_CMD_START_DELAY_2 = 0x018,                  // s
  REG_CMD_START_DELAY_3 = 0x019,                  // s
  REG_CMD_START_DELAY_4 = 0x01A,                  // s
  REG_CMD_STOP_SEQUENCE_HEATER = 0x01B,           // s
  REG_CMD_CALIBRATION_SUPPLY_AIR = 0x08C,         // x0.1 K, signed
  REG_CMD_CALIBRATION_EXTRACT_AIR = 0x08D,        // x0.1 K, signed
  REG_CMD_CALIBRATION_OUTDOOR_AIR = 0x08E,        // x0.1 K, signed
  REG_CMD_CALIBRATION_RETURN_WATER = 0x08F,       // x0.1 K, signed
  REG_CMD_MAX_TIMER_SPEED = 0x090,                // AC fans: mode 1-3 used by the max timer
  REG_CMD_COOLNESS_RECOVERY_DIFF = 0x092,         // x0.1 K
  REG_CMD_HOME_AWAY_TIME_DELAY = 0x0A5,           // min
  REG_CMD_HOME_AWAY_SPEED = 0x0A6,                // 1-3
  REG_CMD_HOME_AWAY_TEMPERATURE = 0x0A7,          // x0.1 C
  REG_CMD_FILTER_GUARD_ENABLE = 0x113,            // filter pressure switch
  REG_CMD_COOLING_ENABLE = 0x115,
  REG_CMD_EXTERNAL_TEMP_CONTROL_ENABLE = 0x134,  // setpoint from external potentiometer
  REG_CMD_SENSOR_SUPPLY_ENABLE = 0x135,
  REG_CMD_SENSOR_EXTRACT_ENABLE = 0x136,
  REG_CMD_SENSOR_OUTDOOR_ENABLE = 0x137,
  REG_CMD_SENSOR_RETURN_WATER_ENABLE = 0x138,
  REG_CMD_COOLNESS_RECOVERY_ENABLE = 0x139,
  REG_CMD_CLEAR_ALARM_LOG = 0x13D,   // write 1
  REG_CMD_ROTOR_ALARM_TYPE = 0x14B,  // 0 = A (stop unit), 1 = B (continue)
  REG_CMD_DEICING_ENABLE = 0x158,
  REG_CMD_DEICING_START_TEMP = 0x159,          // x0.1 C, signed
  REG_CMD_DEICING_FAN_REDUCTION_TEMP = 0x15A,  // x0.1 C, signed
  REG_CMD_DEICING_ROTOR_SPEED = 0x15B,         // 15-180

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
uint16_t string_to_mode(StringRef mode_str);

// ------------------------------------------------------------------
// FlexitModbusServer
// ------------------------------------------------------------------
/**
 * @brief A Modbus server implementation for a Flexit ventilation system.
 *
 * This class:
 *  - Inherits from UARTDevice to handle UART-based Modbus RTU communication.
 *  - Inherits from Component for integration with the ESPHome lifecycle.
 */
class FlexitModbusServer : public esphome::uart::UARTDevice, public Component {
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

  /**
   * @brief Dump configuration (called during config phase).
   */
  void dump_config() override;

  /**
   * @brief Saves a setting changed in this loop and the runtime counters, before a reboot or OTA update.
   */
  void on_safe_shutdown() override;

  // ----------------------------------------------------------------
  // Custom methods for manipulating registers and coils
  // ----------------------------------------------------------------
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
   * @return The time in hours, or NAN if the counter is not known yet.
   */
  float read_holding_register_hours(HoldingRegisterIndex high_reg);

  /**
   * @brief Send a command by writing to a Holding Register and setting the associated coil.
   *
   * @param cmd_register The command register index.
   * @param value        The value to write.
   * @param temporary    True for a boost. The saved setting stays the real one, restore() puts it back, and so
   *                     does the next boot if we go down first.
   */
  void send_cmd(HoldingRegisterIndex cmd_register, uint16_t value, bool temporary = false);

  /// @brief The real value of a setting, never a boost's. 0 = not known yet. Other registers as they are.
  uint16_t real_value(HoldingRegisterIndex reg) const;

  /// @brief Sends the real value of a setting a temporary one was sent for. Nothing if it has it already.
  void restore(HoldingRegisterIndex reg);

  /**
   * @brief Called for every frame with a good CRC, in bus order, before we handle it. Our own frames too
   *        once they're out, and junk bytes between frames.
   */
  void add_on_frame_callback(std::function<void(const uint8_t *, size_t)> &&callback) {
    frame_callback_.add(std::move(callback));
  }

  /// @brief Called with register and value when a setting changes for good, from send_cmd() without
  ///        temporary or from a panel via the CS60. Not for temporary values or restore().
  void add_on_setting_callback(std::function<void(uint16_t, uint16_t)> &&callback) {
    setting_callback_.add(std::move(callback));
  }

  /// @brief Called every loop after the bus is read, after the status callback.
  void add_on_read_callback(std::function<void()> &&callback) { read_callback_.add(std::move(callback)); }

  /// @brief Frames to `address` get their size from `length` instead, for the CI600 panel link. It returns
  ///        the frame length, 0 = not a frame, more than `available` = wait for more.
  void set_frame_length(uint8_t address, size_t (*length)(const uint8_t *frame, size_t available)) {
    other_addr_ = address;
    other_len_ = length;
  }

  /// @brief Max time since the frame ended. Only from inside the frame callback.
  uint32_t frame_age_us();

  /// @brief Min time since the last frame we read ended.
  uint32_t quiet_min_us() { return micros() - last_frame_us_; }

  /// @brief Nothing waiting, no bytes in the UART and no part of a frame read.
  bool rx_empty() { return !rx_left_ && !available(); }

  /**
   * @brief Called right after the CS60's status broadcast if nothing came after it and we didn't just
   *        answer the CS60. The panel link uses this quiet gap.
   */
  void add_on_status_callback(std::function<void()> &&callback) { status_callback_.add(std::move(callback)); }

  /**
   * @brief Max time since the last status broadcast ended. UINT32_MAX if something came after it.
   */
  uint32_t status_age_us();

  /**
   * @brief Min time since the last status broadcast ended, counted from when we read it. UINT32_MAX if
   *        something came after it.
   */
  uint32_t status_min_age_us() { return status_last_ ? micros() - status_seen_us_ : UINT32_MAX; }

  /**
   * @brief True once we've seen a status broadcast since boot, so mode and temperatures are live. Settings
   *        are still the saved ones until the CS60 sends them.
   */
  bool synced() const { return status_heard_; }

  /**
   * @brief Send our own frame and wait until it's out. Adds the CRC after len bytes, needs room for 2 more.
   */
  void send_frame(uint8_t *frame, size_t len) { mb_.sendResponse(frame, len, false); }

  /// @brief The next frame from `address` with `function` is a reply of `len` bytes, for a read we sent.
  void expect_reply(uint8_t address, uint8_t function, size_t len) {
    reply_addr_ = address;
    reply_function_ = function;
    reply_len_ = len;
  }

  /**
   * @brief For panel firmware updates. The updater drives the bus and the panel's bootloader answers at
   *        our address (1), so we stop answering. We still read the bus and call the frame callback.
   */
  void set_updating(bool updating) {
    updating_ = updating;
    mb_.setEchoFilter(!updating);  // a 0x64 reply can look just like its request, and the echo filter would drop it
  }
  bool updating() const { return updating_; }

  /// @brief True while the frame callback has one of our own frames.
  bool is_own_frame() const { return own_frame_; }

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
  uint8_t server_address() const { return server_address_; }

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

  /**
   * @brief Enable/disable keeping the setting registers in flash.
   *
   * @param val False to neither restore them at boot nor save them.
   */
  void set_persist_settings(bool val);

  /**
   * @brief Enable/disable keeping the runtime counters in flash.
   *
   * @param val False to neither restore them at boot nor save them.
   */
  void set_persist_runtime_counters(bool val);

  /// @brief Mixed into the flash record keys, so two servers keep theirs apart.
  void set_preference_hash(uint32_t hash) { preference_hash_ = hash; }

#ifdef USE_FLEXIT_TCP_BRIDGE
  /**
   * @brief Enable/disable TCP bridge mode.
   *
   * @param enabled True to enable TCP bridge.
   */
  void set_tcp_bridge_enabled(bool enabled);

  /**
   * @brief Set the TCP server port for the bridge.
   *
   * @param port The TCP port number (default: 502 for Modbus TCP).
   */
  void set_tcp_bridge_port(uint16_t port);

  /**
   * @brief Set maximum number of concurrent TCP clients.
   *
   * @param max_clients Maximum number of clients (default: 4).
   */
  void set_tcp_bridge_max_clients(uint8_t max_clients);
#endif  // USE_FLEXIT_TCP_BRIDGE

 private:
  /// @brief The library's way to the UART
  class UartPort : public ModbusRTUServer::Port {
   public:
    explicit UartPort(FlexitModbusServer *server) : server_(server) {}
    size_t read(uint8_t *data, size_t max) override;
    size_t write(const uint8_t *data, size_t length) override;
    void flush() override;

   protected:
    FlexitModbusServer *server_;
  };

  /// @brief The Modbus RTU server.
  ModbusRTUServer mb_;
  UartPort port_{this};

  CallbackManager<void(const uint8_t *, size_t)> frame_callback_;
  CallbackManager<void()> status_callback_;
  CallbackManager<void()> read_callback_;
  CallbackManager<void(uint16_t, uint16_t)> setting_callback_;
  uint8_t other_addr_{0};
  size_t (*other_len_)(const uint8_t *, size_t){nullptr};  // set_frame_length

  /// @brief Our frame going out, for the frame callback, and when the last frame was read or sent.
  uint8_t tx_frame_[256];
  size_t tx_frame_len_{0};
  bool own_frame_{false};
  uint32_t last_frame_us_{0};

  /// @brief For status_age_us(), when the last loops read the UART and how much of a status frame they had.
  uint32_t loop_us_{0}, prev_loop_us_{0}, status_from_us_{0}, status_seen_us_{0};
  int status_part_{0}, prev_status_part_{0}, status_from_part_{0};
  bool status_new_{false}, status_last_{false}, answered_{false}, status_heard_{false};
  bool updating_{false};  // panel update running, see set_updating()

  /// @brief Reply we expect next, from another server answering a read. See setup().
  uint8_t reply_addr_{0};
  uint8_t reply_function_{0};
  size_t reply_len_{0};

  /// @brief What the splitter left in the library's buffer last time, and when the buffer last grew. See setup().
  size_t rx_left_{0};
  uint32_t rx_grew_us_{0};

  /// @brief The register the CS60 last read from us and its value then, for its FC65 answer.
  uint16_t read_reg_{UINT16_MAX};
  uint16_t read_value_{0};

  /// @brief RX bytes reach us at this many, or 2 byte times after the line goes quiet.
  size_t rx_threshold_();

  /// @brief The Modbus server (server) address.
  uint8_t server_address_{1};

  /// @brief GPIO pin controlling driver enable for RS485 (if needed).
  int16_t tx_enable_pin_{-1};

  /// @brief Whether TX enable is active high (true) or low.
  bool tx_enable_direct_{true};

  /// @brief Whether the setting registers are restored from and saved to flash.
  bool persist_settings_{true};

  /// @brief Whether the runtime counters are restored from and saved to flash.
  bool persist_runtime_counters_{true};

  uint32_t preference_hash_{0};

  // ----------------------------------------------------------------
  // Settings persistence
  // ----------------------------------------------------------------
  // The CS60 only sends all settings when it boots, so we keep the real ones in
  // flash and load them on our own boot. A boost's temporary value never goes
  // in. Real changes are written to flash right away, and so is which ones the
  // CS60 may have a temporary value for (held, until it has the real one again),
  // so after a power cut we send it the real ones.
  struct SettingsRange {
    uint16_t start;
    uint16_t count;
  };
  static constexpr SettingsRange SETTINGS_RANGES[] = {
      {0x000, 0x0F},  // mode, fan %, setpoint and supply limits
      {0x010, 0x0C},  // rotor, neutral zones, cooling, filter, start delays
      {0x08C, 0x08},  // calibration, max timer, coolness diff, rest mode
      {0x0A4, 0x04},  // language, home/away
      {0x113, 0x03},  // filter guard, regulation type, cooling
      {0x117, 0x01},  // week program on, with the replacement CI600
      {0x118, 0x22},  // week program slots, external setpoint, sensors, coolness recovery
      {0x13F, 0x03},  // heater flags of panel 1-3
      {0x14B, 0x01},  // rotor alarm type
      {0x158, 0x04},  // de-icing
  };
  static constexpr size_t NUM_SAVED_REGISTERS = [] {
    size_t n = 0;
    for (const auto &range : SETTINGS_RANGES)
      n += range.count;
    return n;
  }();
  // Hashed from the ranges, so a changed layout starts empty rather than loading into the wrong registers
  static constexpr uint32_t SETTINGS_KEY = [] {
    uint32_t hash = 2166136261u;
    for (const auto &range : SETTINGS_RANGES)
      hash = (hash * 16777619u) ^ (range.start << 16 | range.count);
    return hash;
  }();
  static constexpr uint32_t RUNTIME_CHECK_INTERVAL_MS = 10000;

  struct SavedSettings {
    uint16_t values[NUM_SAVED_REGISTERS];
    uint32_t held[(NUM_SAVED_REGISTERS + 31) / 32];  // bit per value, the CS60 may have a temporary one
  };

  ESPPreferenceObject settings_pref_;
  SavedSettings saved_{};
  bool settings_dirty_{false};  // save it
  bool settings_sync_{false};   // and write it to flash now
  uint32_t last_runtime_check_{0};

  /// @brief Index in SavedSettings, -1 if it's not a setting.
  int slot_(uint16_t reg) const;
  bool held_(int i) const { return saved_.held[i / 32] & (1u << (i % 32)); }
  void set_held_(int i, bool held);
  void set_real_(int i, uint16_t reg, uint16_t value);
  bool keep_ours_(uint16_t reg, uint16_t before);
  void bus_written_(uint16_t reg, uint16_t before);
  void restore_settings_();
  void save_settings_();

  // ----------------------------------------------------------------
  // Runtime counter persistence
  // ----------------------------------------------------------------
  // The counters are 32 bit seconds split in a high and low word. The CS60
  // only sends a word when it changes, so the high word comes every ~18 h
  // and counters that aren't running don't come at all. We save them to
  // flash, and if the low word wrapped while we were off (new low < saved
  // low) we bump the high word until the CS60 sends a real one. The filter
  // and total high words are taken from the status hours.
  static constexpr uint16_t RUNTIME_FIRST = 0x14C;
  static constexpr uint16_t RUNTIME_LAST = 0x15F;
  static constexpr uint16_t RUNTIME_HIGH_REGS[] = {
      REG_RUNTIME_STOP_HIGH,  REG_RUNTIME_MIN_HIGH,    REG_RUNTIME_NORMAL_HIGH, REG_RUNTIME_MAX_HIGH,
      REG_RUNTIME_ROTOR_HIGH, REG_RUNTIME_HEATER_HIGH, REG_RUNTIME_HIGH,        REG_RUNTIME_FILTER_HIGH,
  };
  static constexpr size_t NUM_RUNTIME_COUNTERS = 8;
  static constexpr uint32_t RUNTIME_SAVE_INTERVAL_MS = 60 * 60 * 1000;

  struct SavedRuntime {
    uint16_t high[NUM_RUNTIME_COUNTERS];
    uint16_t low[NUM_RUNTIME_COUNTERS];
    uint8_t known;  // bit per counter
  };
  static_assert((sizeof(SavedSettings) + 3) / 4 == 46 && (sizeof(SavedRuntime) + 3) / 4 == 9,
                "update the ESP8266 flash words in __init__.py");

  ESPPreferenceObject runtime_pref_;
  SavedRuntime saved_runtime_{};
  uint32_t last_runtime_save_{0};
  uint32_t runtime_received_{0};  // bit per register since boot, (reg - RUNTIME_FIRST)
  uint8_t runtime_restored_{0};   // bit per counter loaded from flash
  uint16_t last_low_[NUM_RUNTIME_COUNTERS]{};

  int runtime_index_(uint16_t high_reg) const;
  bool runtime_known_(size_t i) const;
  void on_registers_written_(uint16_t start, uint16_t count);
  void high_from_hours_();
  void restore_runtime_();
  /// @brief Saves new counters and high words, and the low words once an hour or if `now`.
  void save_runtime_(bool now);

  // ----------------------------------------------------------------
  // TCP Bridge Members
  // ----------------------------------------------------------------
#ifdef USE_FLEXIT_TCP_BRIDGE
  /// @brief Whether TCP bridge mode is enabled.
  bool tcp_bridge_enabled_{false};

  /// @brief TCP server port for the bridge.
  uint16_t tcp_bridge_port_{502};

  /// @brief Maximum number of concurrent TCP clients.
  uint8_t tcp_bridge_max_clients_{4};

  /// @brief The listening socket, once the network is up.
  std::unique_ptr<socket::ListenSocket> tcp_server_;
  uint32_t tcp_retry_ms_{0};  // last try at listening, we retry after 10 s

  // The bridge must never hold up the bus or eat the heap, so both buffers
  // have a fixed limit and a client that reads too slowly misses data.
  static constexpr size_t TCP_BUFFER_MAX = 2048;

  struct TcpClient {
    std::unique_ptr<socket::Socket> sock;
    std::vector<uint8_t> pending;  // blocks the socket had no room for yet
    bool lagging;
    bool gone;                            // closed by the other end, or broken
    char peer[socket::SOCKADDR_STR_LEN];  // its address, for the log
  };

  /// @brief Connected TCP clients.
  std::vector<TcpClient> tcp_clients_;

  /// @brief Blocks collected since the last loop, with their headers, in bus order.
  std::vector<uint8_t> tcp_out_;

  /// @brief Adds a frame from the frame callback as a block, 'T' for ours, else 'R'.
  void tcp_mirror_(const uint8_t *data, size_t len);

  /**
   * @brief Initialize the TCP server.
   */
  void setup_tcp_bridge_();

  /**
   * @brief Handle TCP client connections and data forwarding.
   */
  void handle_tcp_bridge_();

  /**
   * @brief Accept new TCP client connections.
   */
  void accept_tcp_clients_();

  /**
   * @brief Remove disconnected TCP clients.
   */
  void cleanup_tcp_clients_();

  /**
   * @brief Send the collected blocks to the clients without waiting for any of them.
   */
  void send_to_tcp_clients_();
#endif  // USE_FLEXIT_TCP_BRIDGE
};

}  // namespace flexit_modbus_server
}  // namespace esphome
