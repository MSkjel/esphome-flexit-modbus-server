#include "flexit_modbus_server.h"
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include "panel/pbus.h"
#ifdef USE_FLEXIT_TCP_BRIDGE
#include "esphome/components/network/util.h"
#endif

namespace esphome {
namespace flexit_modbus_server {

static constexpr const char *const TAG = "flexit_modbus_server";

static constexpr const char *const MODE_STRINGS[] = {"Stop", "Min", "Normal", "Max"};

static constexpr size_t MIN_FRAME_LENGTH = 5;     // the shortest frame we try to parse, an exception response
static constexpr uint8_t FC_UPDATE = 0x64;        // the panel bootloader's update protocol
static constexpr uint8_t FC_CX60_WRITE = 0x65;    // the CX60 passing on a register it read, with the value it took
static constexpr size_t MAX_WRITE_REGS = 123;     // most registers in an FC16
static constexpr size_t MAX_UPDATE_DATA = 249;    // most data in a 0x64 frame
static constexpr size_t NUM_INPUT_REGISTERS = 4;  // the CX60 reads 4 with FC4 to find its panels

static constexpr uint32_t ISR_US = 150;   // UART interrupt latency, worst case
static constexpr uint32_t T35_US = 1900;  // Modbus t3.5 above 19200 baud is 1.75 ms, plus a bit

#ifdef USE_FLEXIT_TCP_BRIDGE
static constexpr uint8_t FRAME_DIR_TX = 'T';  // block header, ours
static constexpr uint8_t FRAME_DIR_RX = 'R';
static constexpr uint32_t TCP_RETRY_MS = 10000;  // between tries at listening
#endif

// Expected total length of the frame starting at `frame` (function code at
// frame[1]), given `available` bytes are present. The CX60 blasts frames with no
// interframe gap, so we size each one by its function code and let the CRC confirm
// the boundary. Every frame on this bus is 8 bytes except the 0x10 register
// broadcasts (sized by their byte-count field) and exceptions (5). Returns 0 for
// a function code we don't handle, so the caller resyncs.
static size_t expected_frame_length(const uint8_t *frame, size_t available) {
  const uint8_t function = frame[1];

  switch (function) {
    case 0x01:  // read coils (the CX60's coil poll)
    case 0x03:  // read holding registers
    case 0x04:  // read input registers (startup server discovery)
    case 0x06:  // write single register
    case FC_CX60_WRITE:
      return 8;
    case 0x10: {  // write multiple registers
      if (available < 7)
        return available + 1;  // need the byte count (frame[6]) first
      // Modbus requires byte_count == 2 * quantity, with quantity in 1..123. Reject anything
      // else at once, so a phantom 0x10 header can't stall the splitter waiting for bytes.
      size_t quantity = (frame[4] << 8) | frame[5];
      size_t byte_count = frame[6];
      if (quantity == 0 || quantity > MAX_WRITE_REGS || byte_count != quantity * 2)
        return 0;
      return byte_count + 9;  // 7 header + byte_count data + 2 CRC
    }
    case FC_UPDATE:  // [addr 64 sub len data[len] crc] both ways
      if (available < 4)
        return available + 1;  // need the length byte first
      return frame[3] <= MAX_UPDATE_DATA ? frame[3] + 6 : 0;
    default:
      return (function & 0x80) ? 5 : 0;  // exception response, else unknown
  }
}

std::string mode_to_string(uint16_t mode) {
  if (mode < std::size(MODE_STRINGS)) {
    return MODE_STRINGS[mode];
  }
  return "Invalid mode";
}

uint16_t string_to_mode(StringRef mode_str) {
  for (uint16_t i = 0; i < std::size(MODE_STRINGS); ++i) {
    if (mode_str == MODE_STRINGS[i]) {
      return i;
    }
  }
  return MODE_NORMAL;
}

void FlexitModbusServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Flexit Modbus Server:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", server_address_);
  ESP_LOGCONFIG(TAG, "  Baud Rate: %" PRIu32, baud_rate());

  if (tx_enable_pin_ >= 0) {
    ESP_LOGCONFIG(TAG, "  TX Enable Pin: GPIO%d", tx_enable_pin_);
    ESP_LOGCONFIG(TAG, "  TX Enable Direct: %s", tx_enable_direct_ ? "YES" : "NO");
  }

  ESP_LOGCONFIG(TAG, "  Persist Settings: %s", persist_settings_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Persist Runtime Counters: %s", persist_runtime_counters_ ? "YES" : "NO");

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_) {
    ESP_LOGCONFIG(TAG, "  TCP Bridge: port %u, max %u clients", tcp_bridge_port_, tcp_bridge_max_clients_);
  }
#endif  // USE_FLEXIT_TCP_BRIDGE
}

void FlexitModbusServer::setup() {
  // A coil per register, set = a value of ours the CX60 hasn't read yet
  mb_.begin(&port_, baud_rate(), server_address_, tx_enable_pin_, tx_enable_direct_, MAX_NUM_HOLDING_REGISTERS,
            MAX_NUM_HOLDING_REGISTERS, 0, NUM_INPUT_REGISTERS);
  mb_.onRawBuffer = [this](uint8_t *data, size_t length) { return split_frames_(data, length); };

  // The CX60 reads each register a panel flagged, then sends the value it took with FC65
  mb_.onReadHoldingRegisters = [this, read = mb_.onReadHoldingRegisters](uint8_t *data, size_t length, bool broadcast) {
    read_reg_ = length >= 6 ? (data[2] << 8) | data[3] : UINT16_MAX;
    read_value_ = mb_.getHoldingRegister(read_reg_);
    read(data, length, broadcast);
  };
  mb_.onInvalidFunction = [this](uint8_t *data, size_t length, bool broadcast) { on_fc65_(data, length, broadcast); };

  // An FC6 is an FC16 of one register
  mb_.onWriteSingleRegister = [this, write = mb_.onWriteSingleRegister](uint8_t *data, size_t length, bool broadcast) {
    on_cx60_write_((data[2] << 8) | data[3], 1, [&] { write(data, length, broadcast); });
  };
  mb_.onWriteMultipleRegisters = [this, write = mb_.onWriteMultipleRegisters](uint8_t *data, size_t length,
                                                                              bool broadcast) {
    on_cx60_write_((data[2] << 8) | data[3], (data[4] << 8) | data[5], [&] { write(data, length, broadcast); });
  };

  restore_settings_();
  restore_runtime_();
  this->set_interval(RUNTIME_CHECK_INTERVAL_MS, [this]() { save_runtime_(false); });

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_)
    add_on_frame_callback([this](const uint8_t *data, size_t len) { tcp_mirror_(data, len); });
#endif  // USE_FLEXIT_TCP_BRIDGE
}

// The CX60 doesnt follow the Modbus RTU spec: it ignores interframe gaps and blasts frames back-to-back, so we can't
// delimit by timing. Take over framing instead: walk the raw buffer, accept the first length whose CRC validates,
// dispatch it, and keep any trailing partial frame for next time.
size_t FlexitModbusServer::split_frames_(uint8_t *data, size_t length) {
  size_t offset = 0;
  size_t passed = 0;  // how far the frame callback has been given
  if (length != rx_left_)
    rx_grew_us_ = micros();

  while (length - offset >= MIN_FRAME_LENGTH) {
    size_t avail = length - offset;
    size_t len = other_len_ && data[offset] == other_addr_ ? other_len_(data + offset, avail)
                                                           : expected_frame_length(data + offset, avail);

    // Another server's reply to a CX60 read is 5 + data bytes, not 8 like the request. Sized wrong, we
    // lost the frames right after it while resyncing, like the CX60 passing on a panel's change.
    bool as_reply = reply_len_ && data[offset] == reply_addr_ && data[offset + 1] == reply_function_;
    // The server may not answer, and the CX60's next request to it starts the same. Take that once it's here
    // instead of waiting for a whole reply's worth of bytes.
    if (as_reply && reply_len_ > avail && len >= MIN_FRAME_LENGTH && len <= avail && mb_.checkCrc(data + offset, len))
      as_reply = false;
    if (as_reply)
      len = reply_len_;

    if (len == 0) {  // not a frame start we know -> resync
      ++offset;
      reply_len_ = 0;
      continue;
    }
    if (len > avail)  // frame not fully here yet -> wait
      break;
    if (!mb_.checkCrc(data + offset, len)) {
      reply_len_ = 0;
      if (!as_reply)  // right size, bad CRC -> resync
        ++offset;
      continue;  // not the reply we waited for, try it as a request
    }

    reply_len_ = 0;
    const uint8_t function = data[offset + 1];

    // the status broadcast, for status_age_us() and the status callback
    bool status = pbus_is_status(data + offset, len);
    if (status) {
      status_from_us_ = prev_loop_us_;
      status_seen_us_ = micros();
      status_ms_ = millis();
      status_from_part_ = prev_status_part_;
    }
    status_new_ |= status;
    status_heard_ |= status;
    status_last_ = status;
    if (data[offset] == server_address_)
      answered_ = true;
    // The bytes we skipped and then the frame, each on its own, before a reply to it is written
    if (offset > passed)
      frame_callback_.call(data + passed, offset - passed);
    frame_callback_.call(data + offset, len);
    passed = offset + len;
    last_frame_us_ = micros();
    if (!as_reply && len == 8 && function >= 0x01 && function <= 0x04 && data[offset] != 0 &&
        data[offset] != server_address_) {
      uint16_t count = (data[offset + 4] << 8) | data[offset + 5];
      size_t reply = function <= 0x02 ? 5 + (count + 7) / 8 : 5 + 2 * count;
      expect_reply(data[offset], function, reply <= 256 ? reply : 0);  // more than a frame holds gets an exception
    }

    // The CX60 finds its panels at startup by reading 4 input registers (0x04).
    // The library answers that with zeros, which is enough for it to see us as
    // online. An exception reply would make it treat us as not present.
    // During a panel update the panel's bootloader answers at our address, so only parse and mirror.
    if (!updating_)
      mb_.processFrame(data + offset, len);
    offset += len;
  }

  // While updating, a reply after junk or missing its head never splits off as a frame. Once the line's been
  // quiet for t3.5 plus the RX threshold, the rest goes on as is and the updater matches its tail.
  if (updating_ && offset < length && micros() - rx_grew_us_ >= T35_US + rx_delay_us_()) {
    offset = length;
    reply_len_ = 0;
  }

  if (offset > passed) {
    frame_callback_.call(data + passed, offset - passed);
    last_frame_us_ = micros();
  }
  if (offset < length)  // something after the status broadcast
    status_last_ = false;
  status_part_ = pbus_status_part(data + offset, length - offset);
  rx_left_ = length - offset;
  return offset;
}

// Our coil set = a value of ours the CX60 hasn't read yet. Its FC6/FC16 writes don't overwrite a setting (it sends
// every setting when it restarts, over a restore we just queued), its FC65 answer does: that's what it took.
// processFrame() has checked the CRC already.
void FlexitModbusServer::on_fc65_(uint8_t *data, size_t, bool broadcast) {
  if (data[1] != FC_CX60_WRITE) {
    mb_.sendException(data[1], 0x01, broadcast);  // illegal function
    return;
  }
  uint16_t address = (data[2] << 8) | data[3];
  uint16_t value = (data[4] << 8) | data[5];
  uint16_t before = mb_.getHoldingRegister(address);

  // A filter reset zeroes the filter counter. Its high word only comes when it changes, so it's 0 now, and
  // the low word going back isn't a wrap.
  if (address == REG_CMD_CLEAR_FILTER_ALARM) {
    mb_.setHoldingRegister(REG_RUNTIME_FILTER_HIGH, 0);
    runtime_received_ |= 1u << (REG_RUNTIME_FILTER_HIGH - REG_RUNTIME_STOP_HIGH);
  }

  // Ours changed again after the CX60 read it, it reads the new one at its next poll
  bool newer = mb_.getCoil(address) && address == read_reg_ && before != read_value_;
  read_reg_ = UINT16_MAX;
  if (newer)
    return;
  mb_.setHoldingRegister(address, value);
  mb_.setCoil(address, 0);
  track_runtime_(address, 1);
  setting_written_(address, before);
}

// Also tells us which runtime counter words the CX60 has sent since boot, and which settings changed
template<typename F> void FlexitModbusServer::on_cx60_write_(uint16_t start, uint16_t count, F &&write) {
  if (count > MAX_WRITE_REGS) {  // the library answers that with an exception
    write();
    return;
  }
  uint16_t before[MAX_WRITE_REGS];
  bool ours[MAX_WRITE_REGS];
  for (uint16_t i = 0; i < count; i++) {
    before[i] = mb_.getHoldingRegister(start + i);
    ours[i] = mb_.getCoil(start + i);
  }
  write();
  for (uint16_t i = 0; i < count; i++)
    if (ours[i])
      ours[i] = keep_ours_(start + i, before[i]);
  track_runtime_(start, count);
  for (uint16_t i = 0; i < count; i++)
    if (!ours[i])
      setting_written_(start + i, before[i]);
}

void FlexitModbusServer::loop() {
  save_settings_();  // before the CX60 can read a coil we just set
  prev_loop_us_ = loop_us_;
  prev_status_part_ = status_part_;
  loop_us_ = micros();
  status_part_ = 0;
  status_new_ = answered_ = false;
  mb_.update();
  if (status_new_ && status_last_ && !answered_)
    status_callback_.call();
  read_callback_.call();

#ifdef USE_FLEXIT_TCP_BRIDGE
  if (tcp_bridge_enabled_) {
    handle_tcp_bridge_();
  }
#endif  // USE_FLEXIT_TCP_BRIDGE
}

uint32_t FlexitModbusServer::frame_age_us() {
  if (own_frame_)
    return 0;
  // The previous loop's read didn't have the end of this frame yet
  return micros() - prev_loop_us_ + ISR_US + rx_delay_us_();
}

uint32_t FlexitModbusServer::status_age_us() {
  if (!status_last_)
    return UINT32_MAX;
  // The previous loop's read only had status_from_part_ bytes of the status frame
  return pbus_since_status(micros() - status_from_us_, status_from_part_, rx_threshold_(), ISR_US);
}

size_t FlexitModbusServer::rx_threshold_() {
  size_t threshold = this->parent_->get_rx_full_threshold();
  return threshold == 0 || threshold > 128 ? 128 : threshold;
}

// The UART hands bytes over at its RX threshold, or 2 byte times after the line goes quiet if nothing follows
uint32_t FlexitModbusServer::rx_delay_us_() { return std::max<uint32_t>(rx_threshold_() - 1, 2) * PBUS_BYTE_US; }

int FlexitModbusServer::slot_(uint16_t reg) const {
  int i = 0;
  for (const auto &range : SETTINGS_RANGES) {
    if (reg >= range.start && reg < range.start + range.count)
      return i + reg - range.start;
    i += range.count;
  }
  return -1;
}

void FlexitModbusServer::restore_settings_() {
  if (!persist_settings_)
    return;

  settings_pref_ = global_preferences->make_preference<SavedSettings>(preference_hash_ ^ SETTINGS_KEY, true);

  if (!settings_pref_.load(&saved_)) {
    saved_ = SavedSettings{};
    ESP_LOGW(TAG, "No saved settings (new install, or an update changed what's saved), restart the CX60 to get them");
    return;
  }

  // Only fills our local registers. Where the CX60 may still have a boost's value we send it the real one, the
  // coil makes it win over the settings the CX60 sends when it starts too.
  int i = 0;
  for (const auto &range : SETTINGS_RANGES) {
    for (int reg = range.start; reg < range.start + range.count; reg++, i++) {
      mb_.setHoldingRegister(reg, saved_.values[i]);
      if (held_(i)) {
        mb_.setCoil(reg, 1);
        ESP_LOGI(TAG, "Sending 0x%03X = %u again, it had a temporary value", reg, saved_.values[i]);
      }
    }
  }
  ESP_LOGI(TAG, "Restored %u saved settings", (unsigned) NUM_SAVED_REGISTERS);
}

void FlexitModbusServer::save_settings_() {
  if (!settings_dirty_)
    return;
  if (persist_settings_) {
    settings_pref_.save(&saved_);
    global_preferences->sync();
  }
  settings_dirty_ = false;
}

void FlexitModbusServer::on_safe_shutdown() {
  // Runs before ESPHome writes everything to flash on a reboot
  save_settings_();
  save_runtime_(true);
}

void FlexitModbusServer::set_held_(int i, bool held) {
  if (held_(i) == held)
    return;
  saved_.held[i / 32] ^= 1u << (i % 32);
  settings_dirty_ = true;  // rare, and needed after a power cut
}

// A setting changed for good, from Home Assistant or from a panel via the CX60. Written to flash right away, a
// boost that ends puts this value back.
void FlexitModbusServer::set_real_(int i, uint16_t reg, uint16_t value) {
  if (saved_.values[i] != value) {
    saved_.values[i] = value;
    settings_dirty_ = true;
  }
  setting_callback_.call(reg, value);
}

// The CX60 wrote over a value of ours it hasn't read. A setting keeps ours. A command gives way, as on a stock
// CI600, so one pressed while the CX60 wasn't polling doesn't run when it restarts. True if ours was kept.
bool FlexitModbusServer::keep_ours_(uint16_t reg, uint16_t before) {
  if (slot_(reg) >= 0) {
    mb_.setHoldingRegister(reg, before);
    return true;
  }
  mb_.setCoil(reg, 0);
  return false;
}

// The CX60 wrote a register, so it has that value now
void FlexitModbusServer::setting_written_(uint16_t reg, uint16_t before) {
  uint16_t now = mb_.getHoldingRegister(reg);
  register_callback_.call(reg, now);
  int i = slot_(reg);
  if (i < 0)
    return;
  if (now != before)
    set_real_(i, reg, now);
  if (now == saved_.values[i])
    set_held_(i, false);
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
  uint16_t high_bit = RUNTIME_HIGH_REGS[i] - REG_RUNTIME_STOP_HIGH;
  return (runtime_received_ & (1u << high_bit)) && (runtime_received_ & (1u << (high_bit + 1)));
}

void FlexitModbusServer::track_runtime_(uint16_t start, uint16_t count) {
  uint32_t end = static_cast<uint32_t>(start) + count;
  if (start <= REG_HOURS_SINCE_FILTER_RESET && end > REG_HOURS_TOTAL_RUNTIME)
    high_from_hours_();
  if (end <= REG_RUNTIME_STOP_HIGH || start > REG_RUNTIME_FILTER_LOW)
    return;

  for (uint32_t reg = std::max<uint32_t>(start, REG_RUNTIME_STOP_HIGH);
       reg < std::min<uint32_t>(end, REG_RUNTIME_FILTER_LOW + 1); reg++)
    runtime_received_ |= 1u << (reg - REG_RUNTIME_STOP_HIGH);

  for (size_t i = 0; i < NUM_RUNTIME_COUNTERS; i++) {
    uint16_t high = RUNTIME_HIGH_REGS[i];
    uint16_t low = high + 1;
    if (low < start || low >= end)
      continue;

    uint16_t new_low = mb_.getHoldingRegister(low);
    bool high_received = runtime_received_ & (1u << (high - REG_RUNTIME_STOP_HIGH));

    // Low word went backwards = it wrapped since we saved, so the saved high is one behind.
    if ((runtime_restored_ & (1u << i)) && !high_received && new_low < last_low_[i]) {
      mb_.setHoldingRegister(high, mb_.getHoldingRegister(high) + 1);
      ESP_LOGD(TAG, "Runtime 0x%03X wrapped while we were off, high word bumped", high);
    }
    last_low_[i] = new_low;
  }
}

// Filter and total high words from the status hours, so they're right after the ESP was off, and the filter's is
// known on a fresh install. Total hours wrap at 65536 h (3600 high words), the total takes the one nearest its own.
void FlexitModbusServer::high_from_hours_() {
  for (uint16_t high : {REG_RUNTIME_FILTER_HIGH, REG_RUNTIME_HIGH}) {
    bool total = high == REG_RUNTIME_HIGH;
    uint32_t high_bit = 1u << (high - REG_RUNTIME_STOP_HIGH);
    if ((runtime_received_ & high_bit) || !(runtime_received_ & (high_bit << 1)) ||
        (total && !(runtime_restored_ & (1u << runtime_index_(high)))))
      continue;
    int hours = mb_.getHoldingRegister(total ? REG_HOURS_TOTAL_RUNTIME : REG_HOURS_SINCE_FILTER_RESET);
    int now = mb_.getHoldingRegister(high);
    // the hours are whole, so take the middle of the hour and round
    int value = std::max(0, (hours * 3600 + 1800 - mb_.getHoldingRegister(high + 1) + 0x8000) >> 16);
    if (total)
      value += (now - value + 1800) / 3600 * 3600;
    if (value != now) {
      mb_.setHoldingRegister(high, value);
      ESP_LOGD(TAG, "Runtime 0x%03X high word %d from the status, was %d", high, value, now);
    }
    runtime_received_ |= high_bit;  // as good as the CX60's
  }
}

void FlexitModbusServer::restore_runtime_() {
  if (!persist_runtime_counters_)
    return;

  runtime_pref_ =
      global_preferences->make_preference<SavedRuntime>(preference_hash_ ^ fnv1_hash("flexit_runtime"), true);

  if (!runtime_pref_.load(&saved_runtime_)) {
    saved_runtime_ = SavedRuntime{};
    ESP_LOGI(TAG, "No saved runtime counters, they stay unknown until the CX60 sends them");
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

void FlexitModbusServer::save_runtime_(bool now) {
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

  // Low words tick every second, so only save them once an hour, or now before a reboot. A new counter
  // or a changed high word gets saved right away.
  bool important =
      current.known != saved_runtime_.known || memcmp(current.high, saved_runtime_.high, sizeof(current.high)) != 0;
  bool low_changed = memcmp(current.low, saved_runtime_.low, sizeof(current.low)) != 0;
  bool due = now || millis() - last_runtime_save_ >= RUNTIME_SAVE_INTERVAL_MS;

  if (!important && !(low_changed && due))
    return;

  saved_runtime_ = current;
  runtime_pref_.save(&saved_runtime_);
  last_runtime_save_ = millis();
  ESP_LOGD(TAG, "Runtime counters saved");
}

float FlexitModbusServer::read_holding_register_hours(uint16_t high_reg) {
  // Unknown until we have both words, either from flash or from the CX60
  int i = runtime_index_(high_reg);
  if (i >= 0 && !runtime_known_(i))
    return NAN;

  // Combine two registers: the high word and the subsequent low word.
  uint32_t seconds = (static_cast<uint32_t>(mb_.getHoldingRegister(high_reg)) << 16) +
                     static_cast<uint32_t>(mb_.getHoldingRegister(high_reg + 1));
  return seconds / 3600.0f;
}

void FlexitModbusServer::send_cmd(uint16_t reg, uint16_t value, bool temporary) {
  uint16_t before = mb_.getHoldingRegister(reg);
  mb_.setHoldingRegister(reg, value);
  mb_.setCoil(reg, 1);
  int i = slot_(reg);
  if (i < 0)
    return;
  if (temporary)
    set_held_(i, true);
  else if (value != before || held_(i))
    set_real_(i, reg, value);  // held until the CX60 has it
}

uint16_t FlexitModbusServer::real_value(uint16_t reg) const {
  int i = slot_(reg);
  return i < 0 ? mb_.getHoldingRegister(reg) : saved_.values[i];
}

void FlexitModbusServer::restore(uint16_t reg) {
  int i = slot_(reg);
  if (i < 0 || !held_(i) || mb_.getHoldingRegister(reg) == saved_.values[i])
    return;
  mb_.setHoldingRegister(reg, saved_.values[i]);
  mb_.setCoil(reg, 1);  // held until the CX60 has it
}

// ---------------------------------------------------------
// The library's way to the UART
// ---------------------------------------------------------
size_t FlexitModbusServer::UartPort::read(uint8_t *data, size_t max) {
  size_t n = std::min(server_->available(), max);
  return n > 0 && server_->read_array(data, n) ? n : 0;
}

size_t FlexitModbusServer::UartPort::write(const uint8_t *data, size_t length) {
  server_->write_array(data, length);
  size_t n = std::min(length, sizeof server_->tx_frame_ - server_->tx_frame_len_);
  memcpy(server_->tx_frame_ + server_->tx_frame_len_, data, n);
  server_->tx_frame_len_ += n;
  return length;
}

void FlexitModbusServer::UartPort::flush() {
  server_->uart::UARTDevice::flush();

  // frame is out, the frame callback gets it too
  server_->last_frame_us_ = micros();
  server_->own_frame_ = true;
  server_->frame_callback_.call(server_->tx_frame_, server_->tx_frame_len_);
  server_->own_frame_ = false;
  server_->tx_frame_len_ = 0;
}

// ---------------------------------------------------------
// TCP Bridge Implementation
// ---------------------------------------------------------
#ifdef USE_FLEXIT_TCP_BRIDGE
// Hands over what fits in the socket's send buffer without waiting. Returns bytes taken, -1 if the socket is gone.
static ssize_t write_nonblocking(socket::Socket &sock, const uint8_t *data, size_t len) {
  if (len == 0)
    return 0;
  ssize_t sent = sock.write(data, len);
  if (sent >= 0)
    return sent;
  return errno == EWOULDBLOCK || errno == EAGAIN ? 0 : -1;
}

void FlexitModbusServer::setup_tcp_bridge_() {
  if (!network::is_connected() || (tcp_retry_ms_ && millis() - tcp_retry_ms_ < TCP_RETRY_MS))
    return;
  tcp_retry_ms_ = millis() | 1;  // 0 means it hasn't failed
  auto sock = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (!sock) {
    ESP_LOGW(TAG, "TCP Bridge: no socket");
    return;
  }
  int one = 1;
  sock->setsockopt(SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_storage addr;
  socklen_t len = socket::set_sockaddr_any((struct sockaddr *) &addr, sizeof addr, tcp_bridge_port_);
  if (sock->setblocking(false) != 0 || len == 0 || sock->bind((struct sockaddr *) &addr, len) != 0 ||
      sock->listen(tcp_bridge_max_clients_) != 0) {
    ESP_LOGW(TAG, "TCP Bridge: can't listen on port %u (errno %d), trying again in %u s", tcp_bridge_port_, errno,
             (unsigned) (TCP_RETRY_MS / 1000));
    return;
  }
  tcp_server_ = std::move(sock);
  tcp_out_.reserve(TCP_BUFFER_MAX);
  ESP_LOGI(TAG, "TCP Bridge: listening on port %u (max clients: %u)", tcp_bridge_port_, tcp_bridge_max_clients_);
}

void FlexitModbusServer::tcp_mirror_(const uint8_t *data, size_t len) {
  if (tcp_clients_.empty() || tcp_out_.size() + 3 + len > TCP_BUFFER_MAX)
    return;
  uint8_t dir = own_frame_ ? FRAME_DIR_TX : FRAME_DIR_RX;
  tcp_out_.insert(tcp_out_.end(), {dir, static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len & 0xFF)});
  tcp_out_.insert(tcp_out_.end(), data, data + len);
}

void FlexitModbusServer::handle_tcp_bridge_() {
  if (!tcp_server_) {
    setup_tcp_bridge_();
    return;
  }

  accept_tcp_clients_();
  cleanup_tcp_clients_();
  send_to_tcp_clients_();
}

void FlexitModbusServer::accept_tcp_clients_() {
  if (!tcp_server_->ready())
    return;
  for (;;) {  // all of them, ready() only goes true again once accept() has taken every one
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    auto sock = tcp_server_->accept((struct sockaddr *) &addr, &len);
    if (!sock)
      return;
    TcpClient c{std::move(sock), {}, false, false, {}};
    c.sock->getpeername_to(c.peer);
    if (tcp_clients_.size() >= tcp_bridge_max_clients_) {
      ESP_LOGW(TAG, "TCP Bridge: Max clients (%u) reached, rejecting connection from %s", tcp_bridge_max_clients_,
               c.peer);
      c.sock->close();
      continue;
    }
    int one = 1;
    c.sock->setblocking(false);
    c.sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    ESP_LOGI(TAG, "TCP Bridge: Client connected from %s (total: %u)", c.peer, (unsigned) tcp_clients_.size() + 1);
    tcp_clients_.push_back(std::move(c));
  }
}

void FlexitModbusServer::cleanup_tcp_clients_() {
  for (auto it = tcp_clients_.begin(); it != tcp_clients_.end();) {
    // Clients only listen. Whatever they send is dropped, and reading is how a closed connection shows.
    uint8_t junk[64];
    for (;;) {
      ssize_t n = it->sock->read(junk, sizeof junk);
      if (n > 0)
        continue;
      if (n == 0 || (errno != EWOULDBLOCK && errno != EAGAIN))
        it->gone = true;
      break;
    }
    if (it->gone) {
      ESP_LOGI(TAG, "TCP Bridge: Client disconnected from %s (total: %u)", it->peer,
               (unsigned) tcp_clients_.size() - 1);
      it->sock->close();
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
      ssize_t sent = write_nonblocking(*c.sock, tcp_out_.data(), tcp_out_.size());
      if (sent < 0)
        c.gone = true;
      else if ((size_t) sent < tcp_out_.size())
        c.pending.assign(tcp_out_.begin() + sent, tcp_out_.end());
      continue;
    }

    // This client is behind. What's new is queued whole or skipped whole, so it
    // never gets half a block.
    if (c.pending.size() + tcp_out_.size() <= TCP_BUFFER_MAX) {
      c.pending.insert(c.pending.end(), tcp_out_.begin(), tcp_out_.end());
    } else if (!c.lagging) {
      c.lagging = true;
      ESP_LOGW(TAG, "TCP Bridge: Client %s reads too slowly, it will miss data", c.peer);
    }

    ssize_t sent = write_nonblocking(*c.sock, c.pending.data(), c.pending.size());
    if (sent < 0)
      c.gone = true;
    else
      c.pending.erase(c.pending.begin(), c.pending.begin() + sent);
  }

  tcp_out_.clear();
}
#endif  // USE_FLEXIT_TCP_BRIDGE

}  // namespace flexit_modbus_server
}  // namespace esphome
