# Overview
The `XIAO-ESP32-C3` and `RS485 Breakout Board for Seeed Studio-XIAO` is easy to assemble and results in a small package. Buy the pre soldered header version to avoid soldering. 

## Links
[`XIAO-ESP32-C3` - Seeedstudio](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html)<br>
[`RS485 Breakout Board for Seeed Studio-XIAO` - Seeedstudio](https://www.seeedstudio.com/RS485-Breakout-Board-for-XIAO-p-6306.html)

## Config
Relevant part of config to configure the uart and flexit_modbus_server correctly
```yaml 
uart:
  id: modbus_uart
  tx_pin: GPIO6
  rx_pin: GPIO7
  baud_rate: 115200

flexit_modbus_server:
  - id: server
    uart_id: modbus_uart
    address: 3
    # Optional settings
    tx_enable_pin: GPIO4
    tx_enable_direct: true 
```