# ESPHome Flexit Modbus Server

A project that implements a Modbus server for Flexit ventilation systems using ESPHome.
You do not need a Flexit CI66 for this to work.
This is still a WIP, and does not yet include all sensors exposed by the Flexit CS60.

## Requirements
- This does not require a Flexit CI66 Modbus adapter to work!
- A Flexit ventilation system with a CS60 or similar controller. This has only been tested on a CS60, but should work with any controller that works with a CI600 panel
- ESP8266 or ESP32
- MAX485, MAX1348 or equivalent UART -> RS485 transciever. I recommend getting one with automatic direction switching like the MAX1348
- Basic knowledge of ESPHome YAML configuration

## Hardware
| MCU  | RS485 | Notes |
| ------------- | ------------- | ------------- |
| XIAO-ESP32-C3  | RS485 Breakout Board for Seeed Studio XIAO (TP8485E)  | [Details](/hardware/xiao-esp32-c3-rs485-breakout-board-for-seeed-studio-xiao-tp8485e.md)|


## TODO
- Reverse/Implement more sensors

## Limitations
- Setting the Supply Air Temperature is only possible without a CI600 connected. This is a limitation emposed by Flexit in the CS60.
- The ESP has to be switched on before the CS60 as the CS60 only starts polling servers that are actually responding when it starts up.
- Setting the server address to 2 does not seem to work, even though the CS60 actually tries to poll this too. Only 1(If no CI600 connected) or 3.
- Some of the settings are set to optimistic and will not reflect changes done using another panel/modbus server. They will also be initialized with default values first time.

## License

This project is licensed under the MIT License.

## Configuration Example

```yaml
wifi:
  fast_connect: true # Add this to your wifi config to be able to power the ESP using the CS60's power. If not enabled the ESP boots too slow.

logger:
  baud_rate: 115200
  hardware_uart: UART1
  level: WARN

external_components:
  - source: github://MSkjel/esphome-flexit-modbus-server@main
    refresh: 60s
    components: 
      - flexit_modbus_server
  - source: github://polyfloyd/esphome@template-climate
    refresh: 60s
    components:
      - template

uart:
  id: modbus_uart
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 115200
    
flexit_modbus_server:
  - id: server
    uart_id: modbus_uart
    address: 3
    # Optional settings
    # tx_enable_pin: GPIO16 # The pin for RE/DE on the MAX485. Not needed with automatic direction control
    # tx_enable_direct: true # Whether to invert the signal for the RE/DE on the MAX485 or not

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
    name: "Set Temperature"
    id: setpoint
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
    id: supply_air_temperature
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
  - platform: template
    name: "Climate Action"
    id: climate_action
    disabled_by_default: True
    lambda: |-
      bool heater_on = id(server)->read_holding_register(flexit_modbus_server::REG_STATUS_HEATER);
      std::string mode = flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(flexit_modbus_server::REG_MODE)
      );

      if (heater_on) {
        return std::string("HEATING");
      } else if (!heater_on && mode != "Stop") {
        return std::string("FAN_ONLY");
      } else if (!heater_on && mode == "Stop") {
        return std::string("OFF");
      } else {
        return std::string("UNKNOWN");
      }     

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
      - lambda: |-
          id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_MODE,
              flexit_modbus_server::string_to_mode(x)
          );
  - platform: template
    name: "Set Fan Mode"
    id: set_fan_mode
    update_interval: 1s
    disabled_by_default: True
    options:
      - "OFF"
      - "LOW"
      - "MEDIUM"
      - "HIGH"
    set_action:
      - lambda: |-
          if (x == "OFF") {
            std::string mode_str = "Stop";
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_MODE,
              flexit_modbus_server::string_to_mode(mode_str)
            );
          } else if (x == "LOW") {
            std::string mode_str = "Min";
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_MODE,
              flexit_modbus_server::string_to_mode(mode_str)
            );
          } else if (x == "MEDIUM") {
            std::string mode_str = "Normal";
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_MODE,
              flexit_modbus_server::string_to_mode(mode_str)
            );
          } else if (x == "HIGH") {
            std::string mode_str = "Max";
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_MODE,
              flexit_modbus_server::string_to_mode(mode_str)
            );
          }
    lambda: |-
      std::string current_mode = flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(flexit_modbus_server::REG_MODE)
      );
      
      if (current_mode == "Stop") {
        return std::string("OFF");
      } else if (current_mode == "Min") {
        return std::string("LOW");
      } else if (current_mode == "Normal") {
        return std::string("MEDIUM");
      } else if (current_mode == "Max") {
        return std::string("HIGH");
      } else {
        return std::string("UNKNOWN");
      }
  - platform: template
    name: "Heater Mode"
    id: heater_mode
    update_interval: 1s
    disabled_by_default: True
    options:
      - "HEAT"
      - "FAN_ONLY"
      - "OFF"
    set_action:
      - lambda: |-
          if (x == "HEAT") {
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_HEATER,
              1
            );
          } else if (x == "FAN_ONLY") {
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_HEATER,
              0
            );
          } else if (x == "OFF") {
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_HEATER,
              0
            );
            std::string mode_str = "Stop";
            id(server)->send_cmd(
              flexit_modbus_server::REG_CMD_MODE,
              flexit_modbus_server::string_to_mode(mode_str)
            );
          }
    lambda: |-
      if (id(server)->read_holding_register(flexit_modbus_server::REG_STATUS_HEATER)) {
        return std::string("HEAT");
      } else if (!id(server)->read_holding_register(flexit_modbus_server::REG_STATUS_HEATER) && 
                 flexit_modbus_server::mode_to_string(
                   id(server)->read_holding_register(flexit_modbus_server::REG_MODE)) != "Stop") {
        return std::string("FAN_ONLY");
      } else if (!id(server)->read_holding_register(flexit_modbus_server::REG_STATUS_HEATER) && 
                 flexit_modbus_server::mode_to_string(
                   id(server)->read_holding_register(flexit_modbus_server::REG_MODE)) == "Stop") {
        return std::string("OFF");
      } else {
        return std::string("UNKNOWN");
      }

climate:
  - platform: template
    name: "UNI3"
    icon: "mdi:air-conditioner"
    target_temperature_id: setpoint
    current_temperature_id: supply_air_temperature
    mode_id: heater_mode
    fan_mode_id: set_fan_mode
    action_id: climate_action
    visual:
      temperature_step: 0.5C     

## Credits
- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
