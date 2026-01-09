# ESPHome Flexit Modbus Server

This project implements a Modbus server for Flexit ventilation systems using ESPHome.  
**No Flexit CI66 adapter is required.**  
> **Note:** This is a work in progress and does not yet support all Flexit CS60 sensors or switches.

---

## Features

- Control Flexit ventilation systems (tested on CS60, may work with others compatible with CI600 panel)
- Works with ESP8266 or ESP32 microcontrollers
- Integrates with ESPHome for easy Home Assistant support
- No CI66 needed

---

## Requirements

- Flexit ventilation system with CS60 (or similar) controller
- ESP8266 or ESP32 device
- UART-to-RS485 transceiver (e.g., MAX485, MAX1348)
- Basic ESPHome YAML configuration knowledge

---

## Recommended Hardware

| MCU             | RS485 Breakout Board | Notes                                                                 |
|-----------------|---------------------|-----------------------------------------------------------------------|
| XIAO-ESP32-C3   | XIAO-RS485-Expansion-Board  | [Details](hardware/xiao-esp32-c3-rs485-breakout-board-for-seeed-studio-xiao-tp8485e.md) |

---

## Limitations

- **Supply Air Temperature:** Can only be set if no CI600 is connected (CS60 limitation).
- **Startup Order:** ESP must be powered on before CS60, or CS60 won’t poll it.
- **Optimistic Settings:** Some settings are “optimistic” and may not reflect changes from other panels or servers.
- **Address:** Address 1 is required for Heater On/Off to function, but this wont work if you have a CI600 connected.

---

## Quick Start

1. **Connect Hardware:**  
   Wire your ESP device to the RS485 transceiver and connect to the Flexit controller.

2. **ESPHome Configuration:**  
   Add the following to your ESPHome YAML file (adjust pins and options as needed):

   ```yaml
   wifi:
     fast_connect: true           # Needed if powered from the CS60

   logger:
     baud_rate: 115200
     hardware_uart: UART1
     level: WARN

   external_components:
     - source: github://MSkjel/esphome-flexit-modbus-server@main
       refresh: 60s
       components: 
         - flexit_modbus_server

   uart:
     id: modbus_uart
     tx_pin: GPIO1                # Set according to your hardware
     rx_pin: GPIO3                # Set according to your hardware
     baud_rate: 115200

   flexit_modbus_server:
     - id: server
       uart_id: modbus_uart
       address: 3 # Address 1 is required for heater on/off, but this wont work in conjuction with a CI600
       # Depending on hardware/optional:
       # tx_enable_pin: GPIO16    # Set according to your hardware.
       # tx_enable_direct: true   # Set according to your hardware. Inverts the DE signal
   ```

3. **Add Controls and Sensors:**
   You can add switches, buttons, numbers, sensors, etc., using the provided examples.
   See the full configuration example below for details. You can pick and choose from these sensors/switches.

---

## Configurations

<details>
<summary>Full sensor config with extras</summary>
This config contains the Flexit sensors and switches and some esphome implemented features.
New features might be added here in the future

As of now these are:
- Fireplace mode

```yaml
globals:
  - id: fireplace_mode_active
    type: bool
    restore_value: no
    initial_value: 'false'

  - id: previous_mode_before_fireplace
    type: int
    restore_value: yes
    initial_value: '2'  # Default to Normal mode if we somehow fail

  - id: fireplace_duration_minutes
    type: int
    restore_value: yes
    initial_value: '60'

  - id: original_supply_fan_max
    type: float
    restore_value: no
    initial_value: '100.0'

  - id: original_extract_fan_max
    type: float
    restore_value: no
    initial_value: '100.0'

  - id: fireplace_supply_fan_speed
    type: int
    restore_value: yes
    initial_value: '90'

  - id: fireplace_extract_fan_speed
    type: int
    restore_value: yes
    initial_value: '20'

    # Track when mode was last changed to debounce conflict detection
  - id: fireplace_mode_change_timestamp
    type: unsigned long
    restore_value: no
    initial_value: '0'

switch:
  - platform: template
    id: heater
    name: "Heater"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_STATUS_HEATER);"
    turn_on_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_HEATER,
            1
          );
    turn_off_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_HEATER,
            0
          );

  - platform: template
    id: supply_air_control
    name: "Supply Air Control"
    optimistic: True
    restore_mode: RESTORE_DEFAULT_ON
    turn_on_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL,
            1
          );
    turn_off_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL,
            0
          );

  - platform: template
    name: "Fireplace Mode"
    id: fireplace_mode_switch
    optimistic: false
    lambda: |-
      return id(fireplace_mode_active);

    turn_on_action:
      - lambda: |-
          // Edge case: Don't activate if system is in Stop mode
          int current_mode = id(server)->read_holding_register(flexit_modbus_server::REG_MODE);
          if (current_mode == 0) {
            ESP_LOGW("fireplace", "Cannot activate Fireplace mode while system is stopped");
            return;
          }

          // Save current mode and fan speeds before switching
          id(previous_mode_before_fireplace) = current_mode;

          // Save current Max mode fan speeds (read from the number components)
          id(original_supply_fan_max) = id(supply_air_percentage_max).state;
          id(original_extract_fan_max) = id(extract_air_percentage_max).state;

          ESP_LOGI("fireplace", "Saving current mode: %s (Supply: %.0f%%, Extract: %.0f%%)",
                   flexit_modbus_server::mode_to_string(current_mode).c_str(),
                   id(original_supply_fan_max),
                   id(original_extract_fan_max));

          ESP_LOGI("fireplace", "Activating Fireplace mode");
          id(fireplace_mode_active) = true;

          // Record timestamp BEFORE making changes to prevent false conflict detection
          id(fireplace_mode_change_timestamp) = millis();

      # Set fireplace fan speeds using the existing number components
      - number.set:
          id: supply_air_percentage_max
          value: !lambda "return id(fireplace_supply_fan_speed);"
      - number.set:
          id: extract_air_percentage_max
          value: !lambda "return id(fireplace_extract_fan_speed);"

      - delay: 200ms  # Wait for fan speeds to apply

      # Switch to Max mode using the existing select component
      - select.set:
          id: set_mode
          option: "Max"

      # Start the auto-shutoff timer
      - script.execute: fireplace_timer

      # Update the switch state
      - lambda: |-
          id(fireplace_mode_switch).publish_state(true);

    turn_off_action:
      - script.stop: fireplace_timer
      - lambda: |-
          if (!id(fireplace_mode_active)) {
            ESP_LOGW("fireplace", "Fireplace mode already inactive");
            return;
          }

          ESP_LOGI("fireplace", "Deactivating Fireplace mode");
          id(fireplace_mode_active) = false;

      # Restore original fan speeds using the existing number components
      - number.set:
          id: supply_air_percentage_max
          value: !lambda "return id(original_supply_fan_max);"
      - number.set:
          id: extract_air_percentage_max
          value: !lambda "return id(original_extract_fan_max);"

      - delay: 200ms  # Wait for fan speeds to apply

      # Restore previous mode using the existing select component
      - lambda: |-
          int restore_mode = id(previous_mode_before_fireplace);
          std::string mode_str = flexit_modbus_server::mode_to_string(restore_mode);
          ESP_LOGI("fireplace", "Restoring previous mode: %s", mode_str.c_str());

          // Record timestamp for debouncing conflict detection
          id(fireplace_mode_change_timestamp) = millis();

          auto call = id(set_mode).make_call();
          call.set_option(mode_str);
          call.perform();

      # Update the switch state
      - lambda: |-
          id(fireplace_mode_switch).publish_state(false);

button:
  - platform: template
    id: clear_alarms
    name: "Clear Alarms"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_CLEAR_ALARMS,
            1
          );

  - platform: template
    id: reset_filter_interval
    name: "Reset Filter Interval"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_CLEAR_FILTER_ALARM,
            1
          );

  - platform: template
    id: start_max_timer
    name: "Start Max Timer"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_START_MAX_TIMER,
            1
          );

  - platform: template
    id: stop_max_timer
    name: "Stop Max Timer"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_STOP_MAX_TIMER,
            1
          );
  
number:
  - platform: template
    id: temperature
    name: "Temperature"
    max_value: 30
    min_value: 10
    step: 0.5
    update_interval: 1s
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_SETPOINT
      );
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SETPOINT,
            x * 10
        );

  - platform: template
    id: max_timer_minutes
    name: "Max Timer Minutes"
    max_value: 600
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_MINUTES_MAX_TIMER,
            x
        );

  - platform: template
    id: temperature_supply_min
    name: "Temperature Supply Min"
    max_value: 30
    min_value: 10
    step: 0.5
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_MIN,
            x * 10
        );

  - platform: template
    id: temperature_supply_max
    name: "Temperature Supply Max"
    max_value: 30
    min_value: 10
    step: 0.5
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_MAX,
            x * 10
        );

  - platform: template
    id: filter_change_interval
    name: "Filter Change Interval"
    max_value: 360
    min_value: 30
    step: 30
    optimistic: True
    restore_value: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_DAYS_FILTER_CHANGE_INTERVAL,
            x
        );

  - platform: template
    id: supply_air_percentage_min
    name: "Supply Air Percentage Min"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MIN,
            x
        );
  
  - platform: template
    id: supply_air_percentage_normal
    name: "Supply Air Percentage Normal"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL,
            x
        );
  
  - platform: template
    id: supply_air_percentage_max
    name: "Supply Air Percentage Max"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX,
            x
        );
  
  - platform: template
    id: extract_air_percentage_min
    name: "Extract Air Percentage Min"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MIN,
            x
        );

  - platform: template
    id: extract_air_percentage_normal
    name: "Extract Air Percentage Normal"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL,
            x
        );
  
  - platform: template
    id: extract_air_percentage_max
    name: "Extract Air Percentage Max"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX,
            x
        );

  - platform: template
    name: "Fireplace Mode Duration"
    id: fireplace_duration
    min_value: 1
    max_value: 240
    step: 1
    unit_of_measurement: "min"
    mode: box
    optimistic: false
    lambda: |-
      return id(fireplace_duration_minutes);
    set_action:
      - lambda: |-
          id(fireplace_duration_minutes) = (int)x;
          ESP_LOGI("fireplace", "Duration set to %d minutes", (int)x);
      - component.update: fireplace_duration

  - platform: template
    name: "Fireplace Supply Fan Speed"
    id: fireplace_supply_speed_control
    min_value: 1
    max_value: 100
    step: 1
    unit_of_measurement: "%"
    mode: box
    optimistic: false
    lambda: |-
      return id(fireplace_supply_fan_speed);
    set_action:
      - lambda: |-
          id(fireplace_supply_fan_speed) = (int)x;
          ESP_LOGI("fireplace", "Fireplace supply fan speed set to %d%%", (int)x);
      - component.update: fireplace_supply_speed_control

  - platform: template
    name: "Fireplace Extract Fan Speed"
    id: fireplace_extract_speed_control
    min_value: 1
    max_value: 100
    step: 1
    unit_of_measurement: "%"
    mode: box
    optimistic: false
    lambda: |-
      return id(fireplace_extract_fan_speed);
    set_action:
      - lambda: |-
          id(fireplace_extract_fan_speed) = (int)x;
          ESP_LOGI("fireplace", "Fireplace extract fan speed set to %d%%", (int)x);
      - component.update: fireplace_extract_speed_control

select:
  - platform: template
    id: set_mode
    name: "Set Mode"
    update_interval: 1s
    lambda: |-
      return flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(flexit_modbus_server::REG_MODE)
      );
    options:
      - Stop
      - Min
      - Normal
      - Max
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_MODE,
            flexit_modbus_server::string_to_mode(x)
        );

sensor:
  - platform: template
    id: setpoint_air_temperature
    name: "Setpoint Air Temperature"
    update_interval: 1s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_SETPOINT
      );

  - platform: template
    id: supply_air_temperature
    name: "Supply Air Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_SUPPLY_AIR
      );
    filters:
      - delta: 0.2

  - platform: template
    id: extract_air_temperature
    name: "Extract Air Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_EXTRACT_AIR
      );
    filters:
      - delta: 0.2

  - platform: template
    id: outdoor_air_temperature
    name: "Outdoor Air Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_OUTDOOR_AIR
      );
    filters:
      - delta: 0.2

  - platform: template
    id: return_water_temperature
    name: "Return Water Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_RETURN_WATER
      );

  - platform: template
    id: heating_percentage
    name: "Heating Percentage"
    update_interval: 20s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_HEATING
      );
  
  - platform: template
    id: cooling_percentage
    name: "Cooling Percentage"
    update_interval: 20s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_COOLING
      );

  - platform: template
    id: heat_exchanger_percentage
    name: "Heat Exchanger Percentage"
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_HEAT_EXCHANGER
      );

  - platform: template
    id: supply_fan_speed_percentage
    name: "Supply Fan Speed Percentage"
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_SUPPLY_FAN
      );

  - platform: template
    id: runtime
    name: "Runtime"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_HIGH
      );

  - platform: template
    id: runtime_normal
    name: "Runtime Normal"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_NORMAL_HIGH
      );

  - platform: template
    id: runtime_stop
    name: "Runtime Stop"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_STOP_HIGH
      );

  - platform: template
    id: runtime_min
    name: "Runtime Min"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_MIN_HIGH
      );

  - platform: template
    id: runtime_max
    name: "Runtime Max"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_MAX_HIGH
      );

  - platform: template
    id: runtime_rotor
    name: "Runtime Rotor"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_ROTOR_HIGH
      );

  - platform: template
    id: runtime_heater
    name: "Runtime Heater"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_HEATER_HIGH
      );

  - platform: template
    id: runtime_filter
    name: "Runtime Filter"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_FILTER_HIGH
      );

binary_sensor:
  - platform: template
    id: heater_enabled
    name: "Heater Enabled"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_STATUS_HEATER
      ) != 0);

  - platform: template
    id: alarm_supply_sensor_faulty
    name: "Alarm Supply Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_SUPPLY_FAULTY
      ) != 0);

  - platform: template
    id: alarm_extract_sensor_faulty
    name: "Alarm Extract Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_EXTRACT_FAULTY
      ) != 0);

  - platform: template
    id: alarm_outdoor_sensor_faulty
    name: "Alarm Outdoor Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_OUTDOOR_FAULTY
      ) != 0);

  - platform: template
    id: alarm_return_water_sensor_faulty
    name: "Alarm Return Water Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_RETURN_WATER_FAULTY
      ) != 0);

  - platform: template
    id: alarm_overheat_triggered
    name: "Alarm Overheat Triggered"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_OVERHEAT_TRIGGERED
      ) != 0);

  - platform: template
    id: alarm_external_smoke_sensor_triggered
    name: "Alarm External Smoke Sensor Triggered"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_SMOKE_EXTERNAL_TRIGGERED
      ) != 0);

  - platform: template
    id: alarm_water_coil_faulty
    name: "Alarm Water Coil Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_WATER_COIL_FAULTY
      ) != 0);

  - platform: template
    id: alarm_heat_exchanger_faulty
    name: "Alarm Heat Exchanger Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_HEAT_EXCHANGER_FAULTY
      ) != 0);

  - platform: template
    id: alarm_filter_change
    name: "Alarm Filter Change"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_FILTER_CHANGE
      ) != 0);

  - platform: template
    name: "Fireplace Mode Conflict Detected"
    id: fireplace_conflict
    internal: true
    lambda: |-
      if (!id(fireplace_mode_active)) {
        return false;
      }

      // Debounce: Allow 5 seconds after mode change for Modbus to propagate
      unsigned long time_since_change = millis() - id(fireplace_mode_change_timestamp);
      if (time_since_change < 10000) {  // 10 second debounce
        return false;  // Too soon after our mode change, ignore
      }

      // Check if mode was manually changed away from Max while fireplace mode is active
      int current_mode = id(server)->read_holding_register(flexit_modbus_server::REG_MODE);
      if (current_mode != 3) {  // Not in Max mode
        ESP_LOGD("fireplace", "Conflict detected: current_mode=%d (expected 3), time_since_change=%lums",
                 current_mode, time_since_change);
        return true;  // Conflict detected
      }
      return false;
    on_press:
      - lambda: |-
          ESP_LOGW("fireplace", "Mode changed manually during Fireplace mode - deactivating");
      - switch.turn_off: fireplace_mode_switch

text_sensor:
  - platform: template
    id: current_mode
    name: "Current Mode"
    update_interval: 1s
    lambda: |-
      return flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(
          flexit_modbus_server::REG_MODE
        )
      );

script:
  - id: fireplace_timer
    mode: restart
    then:
      - lambda: |-
          int duration = id(fireplace_duration_minutes);
          ESP_LOGI("fireplace", "Starting %d minute timer", duration);
      - delay: !lambda "return id(fireplace_duration_minutes) * 60 * 1000;"
      - lambda: |-
          ESP_LOGI("fireplace", "Fireplace timer expired, deactivating");
      - switch.turn_off: fireplace_mode_switch
```
</details>
<details>
<summary>Sensors and switches only config</summary>
This config contains only the Flexit sensors and switches

```yaml
switch:
  - platform: template
    name: "Heater"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_STATUS_HEATER);"
    turn_on_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_HEATER,
            1
          );
    turn_off_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_HEATER,
            0
          );

  - platform: template
    name: "Supply Air Control"
    optimistic: True
    restore_mode: RESTORE_DEFAULT_ON
    turn_on_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL,
            1
          );
    turn_off_action:
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_AIR_CONTROL,
            0
          );

button:
  - platform: template
    name: "Clear Alarms"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_CLEAR_ALARMS,
            1
          );

  - platform: template
    name: "Reset Filter Interval"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_CLEAR_FILTER_ALARM,
            1
          );

  - platform: template
    name: "Start Max Timer"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_START_MAX_TIMER,
            1
          );

  - platform: template
    name: "Stop Max Timer"
    on_press: 
      - lambda: |-
          id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_STOP_MAX_TIMER,
            1
          );
  
number:
  - platform: template
    name: "Temperature"
    max_value: 30
    min_value: 10
    step: 0.5
    update_interval: 1s
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_SETPOINT
      );
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SETPOINT,
            x * 10
        );

  - platform: template
    name: "Max Timer Minutes"
    max_value: 600
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_MINUTES_MAX_TIMER,
            x
        );

  - platform: template
    name: "Temperature Supply Min"
    max_value: 30
    min_value: 10
    step: 0.5
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_MIN,
            x * 10
        );

  - platform: template
    name: "Temperature Supply Max"
    max_value: 30
    min_value: 10
    step: 0.5
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_TEMPERATURE_SUPPLY_MAX,
            x * 10
        );

  - platform: template
    name: "Filter Change Interval"
    max_value: 360
    min_value: 30
    step: 30
    optimistic: True
    restore_value: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_DAYS_FILTER_CHANGE_INTERVAL,
            x
        );

  - platform: template
    name: "Supply Air Percentage Min"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MIN,
            x
        );
  
  - platform: template
    name: "Supply Air Percentage Normal"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL,
            x
        );
  
  - platform: template
    name: "Supply Air Percentage Max"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX,
            x
        );
  
  - platform: template
    name: "Extract Air Percentage Min"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MIN,
            x
        );

  - platform: template
    name: "Extract Air Percentage Normal"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL,
            x
        );
  
  - platform: template
    name: "Extract Air Percentage Max"
    max_value: 100
    min_value: 1
    step: 1
    optimistic: True
    restore_value: True
    disabled_by_default: True
    mode: BOX
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX,
            x
        );

select:
  - platform: template
    name: "Set Mode"
    update_interval: 1s
    lambda: |-
      return flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(flexit_modbus_server::REG_MODE)
      );
    options:
      - Stop
      - Min
      - Normal
      - Max
    set_action:
      lambda: |-
        id(server)->send_cmd(
            flexit_modbus_server::REG_CMD_MODE,
            flexit_modbus_server::string_to_mode(x)
        );

sensor:
  - platform: template
    name: "Setpoint Air Temperature"
    update_interval: 1s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_SETPOINT
      );

  - platform: template
    name: "Supply Air Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_SUPPLY_AIR
      );
    filters:
      - delta: 0.2

  - platform: template
    name: "Extract Air Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_EXTRACT_AIR
      );
    filters:
      - delta: 0.2

  - platform: template
    name: "Outdoor Air Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_OUTDOOR_AIR
      );
    filters:
      - delta: 0.2

  - platform: template
    name: "Return Water Temperature"
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: |-
      return id(server)->read_holding_register_temperature(
        flexit_modbus_server::REG_TEMPERATURE_RETURN_WATER
      );

  - platform: template
    name: "Heating Percentage"
    update_interval: 20s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_HEATING
      );
  
  - platform: template
    name: "Cooling Percentage"
    update_interval: 20s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_COOLING
      );

  - platform: template
    name: "Heat Exchanger Percentage"
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_HEAT_EXCHANGER
      );

  - platform: template
    name: "Supply Fan Speed Percentage"
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_PERCENTAGE_SUPPLY_FAN
      );

  - platform: template
    name: "Runtime"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_HIGH
      );

  - platform: template
    name: "Runtime Normal"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_NORMAL_HIGH
      );

  - platform: template
    name: "Runtime Stop"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_STOP_HIGH
      );

  - platform: template
    name: "Runtime Min"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_MIN_HIGH
      );

  - platform: template
    name: "Runtime Max"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_MAX_HIGH
      );

  - platform: template
    name: "Runtime Rotor"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_ROTOR_HIGH
      );

  - platform: template
    name: "Runtime Heater"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_HEATER_HIGH
      );

  - platform: template
    name: "Runtime Filter"
    update_interval: 60s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_FILTER_HIGH
      );

binary_sensor:
  - platform: template
    name: "Heater Enabled"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_STATUS_HEATER
      ) != 0);

  - platform: template
    name: "Alarm Supply Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_SUPPLY_FAULTY
      ) != 0);

  - platform: template
    name: "Alarm Extract Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_EXTRACT_FAULTY
      ) != 0);

  - platform: template
    name: "Alarm Outdoor Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_OUTDOOR_FAULTY
      ) != 0);

  - platform: template
    name: "Alarm Return Water Sensor Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_RETURN_WATER_FAULTY
      ) != 0);

  - platform: template
    name: "Alarm Overheat Triggered"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_OVERHEAT_TRIGGERED
      ) != 0);

  - platform: template
    name: "Alarm External Smoke Sensor Triggered"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_SMOKE_EXTERNAL_TRIGGERED
      ) != 0);

  - platform: template
    name: "Alarm Water Coil Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_WATER_COIL_FAULTY
      ) != 0);

  - platform: template
    name: "Alarm Heat Exchanger Faulty"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_SENSOR_HEAT_EXCHANGER_FAULTY
      ) != 0);

  - platform: template
    name: "Alarm Filter Change"
    lambda: |-
      return (id(server)->read_holding_register(
        flexit_modbus_server::REG_ALARM_FILTER_CHANGE
      ) != 0);

text_sensor:
  - platform: template
    name: "Current Mode"
    update_interval: 1s
    lambda: |-
      return flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(
          flexit_modbus_server::REG_MODE
        )
      );
```
</details>

---

## Optional extras

<details>
<summary>TCP Bridge</summary>
The TCP bridge feature allows you to monitor the Modbus communication over a TCP connection. This is useful for finding new registers.

### How It Works

When enabled, the TCP bridge creates a server that:
- Accepts TCP connections on the configured port (default: 502)
- Mirrors all UART data to connected clients in real-time (both TX and RX)
- Sends data with directional framing so you can distinguish between sent and received frames

### Frame Protocol

Data is sent to TCP clients using a simple 3-byte header + payload format:
```
[Direction (1 byte)][Length High (1 byte)][Length Low (1 byte)][Payload (N bytes)]
```
- **Direction**: `'T'` (0x54) for TX (ESP→UART), `'R'` (0x52) for RX (UART→ESP)
- **Length**: 16-bit big-endian payload length

### Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `tcp_bridge_enabled` | boolean | `false` | Enable/disable TCP bridge |
| `tcp_bridge_port` | integer | `502` | TCP port to listen on |
| `tcp_bridge_max_clients` | integer | `4` | Maximum concurrent clients (1-10) |

### Example Configuration

```yaml
flexit_modbus_server:
  - id: server
    uart_id: modbus_uart
    address: 3
    tcp_bridge_enabled: true
    tcp_bridge_port: 8502
    tcp_bridge_max_clients: 2
```

### Monitoring Tool

A Python script for monitoring and decoding the TCP bridge traffic is included in [scripts/tcp_bridge_monitor.py](scripts/tcp_bridge_monitor.py).

**Features:**
- Decodes Modbus RTU frames
- Color-coded TX (ESP→UART) and RX (UART→ESP) traffic
- Track coil state changes with `--coil-changes` flag

</details>

---

## TODO

- Add support for more sensors and switches

## License

MIT License

---

## Credits

- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
