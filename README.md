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
       address: 3
       # Depending on hardware/optional:
       # tx_enable_pin: GPIO16    # Set according to your hardware.
       # tx_enable_direct: true   # Set according to your hardware. Inverts the DE signal
   ```

3. **Add Controls and Sensors:**  
   You can add switches, buttons, numbers, sensors, etc., using the provided examples.  
   See the full configuration example below for details. You can pick and choose from these sensors/switches.

---

## Example Configuration

<details>
<summary>Click to expand</summary>

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
      bool heater_on = (id(server)->read_holding_register(
        flexit_modbus_server::REG_STATUS_HEATER
      ) != 0) ;
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
```
</details>

### Experimental Climate component

<details>
<summary>Click to expand</summary>
   
You can use the *experimental* template climate component from the [pull-request by polyfloyd](https://github.com/esphome/esphome/pull/8031) to provide a single GUI in home assistant.

> **Note:** This is purely experimental and might disappear or break at anytime. Currently the component also uses `climate.CLIMATE_SCHEMA` which is deprecated and will be removed in ESPHome 2025.11.0

In order to configure this:

```yaml
# Add the component from the pull-request under external_components:
external_components:
  - source: github://MSkjel/esphome-flexit-modbus-server@main
    refresh: 60s
    components: 
      - flexit_modbus_server
  - source: github://pr#8031
    refresh: 1h
    components:
      - template
```

```yaml
# Extend your configuration with the required entities
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
    internal: True
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
    internal: True
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
      bool heater_on = (id(server)->read_holding_register(
        flexit_modbus_server::REG_STATUS_HEATER
      ) != 0) ;
      std::string current_mode = flexit_modbus_server::mode_to_string(
        id(server)->read_holding_register(flexit_modbus_server::REG_MODE)
      );
      if (heater_on) {
        return std::string("HEAT");
      } else if (!heater_on && current_mode != "Stop") {
        return std::string("FAN_ONLY");
      } else if (!heater_on && current_mode == "Stop") {
        return std::string("OFF");
      } else {
        return std::string("UNKNOWN");
      }
```

```yaml
# Configure the climate component to use these entries

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
```
</details>

---

## TODO

- Add support for more sensors and switches

---

## License

[MIT License](LICENSE.md)

---

## Credits

- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
- [template-climate](https://github.com/polyfloyd/esphome/tree/template-climate)
