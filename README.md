# ESPHome Flexit Modbus Server

A project implements a Modbus server for Flexit ventilation systems using ESPHome.
This is still a WIP, and does not yet include all sensors exposed by the Flexit CI66.


## Requirements
- A Flexit ventilation system with a CS60 or similar controller. This has only been tested on a CS60, but should work with any controller that works with a CI600 panel
- ESP8266 or ESP32
- MAX485 or equivalent UART -> RS485 transciever
- Basic knowledge of ESPHome YAML configuration

## License
This project is licensed under the MIT License.

## Configuration Example

```yaml
logger:
  baud_rate: 0

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
    address: 2

sensor:
  - platform: template
    name: Test
    id: test
    update_interval: 5s
    lambda: "return id(server)->read_coil(flexit_modbus_server::COIL_1);"

  - platform: template
    name: Setpoint Air Temperature
    id: setpoint_air_temperature
    update_interval: 5s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_SETPOINT_TEMP) / 10.0;"

  - platform: template
    name: Supply Air Temperature
    id: supply_air_temperature
    update_interval: 5s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_SUPPLY_TEMPERATURE) / 10.0;"

  - platform: template
    name: Outdoor Air Temperature
    id: outdoor_air_temperature
    update_interval: 5s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_OUTDOOR_TEMPERATURE) / 10.0;"

  - platform: template
    name: Heater Percentage
    id: heater_percentage
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_HEATER_PERCENTAGE);"

  - platform: template
    name: Heat Exchanger Percentage
    id: heat_exchanger_percentage
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_HEAT_EXCHANGER_PERCENTAGE);"
    
  - platform: template
    name: Supply Air Fan Speed Percentage
    id: supply_air_fan_speed_percentage
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_SUPPLY_AIR_FAN_SPEED_PERCENTAGE);"
    
text_sensor:
  - platform: template
    name: Mode
    id: mode
    update_interval: 5s
    lambda: "return flexit_modbus_server::mode_to_string(id(server)->read_holding_register(flexit_modbus_server::REG_REGULATION_MODE));"
```

## Credits
- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
