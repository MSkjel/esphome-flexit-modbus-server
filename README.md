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


sensor:
  - platform: template
    name: Setpoint Air Temperature
    update_interval: 1s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: "return id(server)->read_holding_register_temperature(flexit_modbus_server::REG_SETPOINT_TEMP);"

  - platform: template
    name: Supply Air Temperature
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: "return id(server)->read_holding_register_temperature(flexit_modbus_server::REG_SUPPLY_TEMPERATURE);"

  - platform: template
    name: Outdoor Air Temperature
    update_interval: 60s
    device_class: temperature
    unit_of_measurement: "°C"
    lambda: "return id(server)->read_holding_register_temperature(flexit_modbus_server::REG_OUTDOOR_TEMPERATURE);"

  - platform: template
    name: Heater Percentage
    update_interval: 20s
    unit_of_measurement: "%"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_HEATER_PERCENTAGE);"

  - platform: template
    name: Heat Exchanger Percentage
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_HEAT_EXCHANGER_PERCENTAGE);"
    
  - platform: template
    name: Supply Air Fan Speed Percentage
    update_interval: 5s
    unit_of_measurement: "%"
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_SUPPLY_AIR_FAN_SPEED_PERCENTAGE);"

binary_sensor:
  - platform: template
    name: Heater Enabled
    lambda: "return id(server)->read_holding_register(flexit_modbus_server::REG_HEATER_ENABLED);"
    
text_sensor:
  - platform: template
    name: Current Mode
    update_interval: 1s
    lambda: "return flexit_modbus_server::mode_to_string(id(server)->read_holding_register(flexit_modbus_server::REG_REGULATION_MODE));"
```

## Credits
- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
