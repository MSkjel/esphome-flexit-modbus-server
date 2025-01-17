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

## TODO
- Reverse/Implement more sensors

## Limitations
- Setting the Supply Air Temperature is only possible without a CI600 connected. This is a limitation emposed by Flexit in the CS60.
- The ESP has to be switched on before the CS60 as the CS60 only starts polling servers that are actually responding when it starts up.
- Setting the server address to 2 does not seem to work, even though the CS60 actually tries to poll this too. Only 1(If no CI600 connected) or 3. 

## License

This project is licensed under the MIT License.

## Configuration Example

```yaml
logger:
  baud_rate: 115200
  hardware_uart: UART1

external_components:
  - source: github://MSkjel/esphome-flexit-modbus-server@main
    refresh: 60s
    components: 
      - flexit_modbus_server

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

number:
  - platform: template
    name: "Set Temperature"
    max_value: 30
    min_value: 10
    step: 1
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

  # Is this actually the correct register? Shows ~-230C on mine. It does make sense if the sensor isnt connected.
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

  # Is this actually the correct register? Shows ~-230C on mine. It does make sense if the sensor isnt connected.
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
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_HIGH
      );

  - platform: template
    name: "Runtime Normal"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_NORMAL_HIGH
      );

  - platform: template
    name: "Runtime Stop"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_STOP_HIGH
      );

  - platform: template
    name: "Runtime Min"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_MIN_HIGH
      );

  - platform: template
    name: "Runtime Max"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_MAX_HIGH
      );

  - platform: template
    name: "Runtime Rotor"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_ROTOR_HIGH
      );

  - platform: template
    name: "Runtime Heater"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_HEATER_HIGH
      );

  - platform: template
    name: "Runtime Filter"
    update_interval: 5s
    unit_of_measurement: "h"
    lambda: |-
      return id(server)->read_holding_register_hours(
        flexit_modbus_server::REG_RUNTIME_FILTER_HIGH
      );

binary_sensor:
  - platform: template
    name: "Heater Enabled"
    lambda: |-
      return id(server)->read_holding_register(
        flexit_modbus_server::REG_STATUS_HEATER
      ) != 0;

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

## Credits
- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
