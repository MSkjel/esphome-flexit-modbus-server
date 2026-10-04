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
- **Settings read-back:** Settings are read from what the CS60 broadcasts: all of them when the CS60 starts, and every change made from any panel. The ESP saves them to flash and loads them on boot, so if a setting was changed on the CI600 while the ESP was off it shows the old value until it changes again or the CS60 restarts. On a fresh install they show up once the CS60 has restarted with the ESP running.
- **Runtime counters:** The CS60 only sends a counter word when it changes, so the ESP saves them to flash and loads them on boot. On a fresh install they show as unknown until the CS60 has sent both words (at the latest when it restarts).
- **Address:** Address 1 is required for Heater On/Off to function, but this wont work if you have a CI600 connected.

---

## Quick Start

1. **Connect hardware:** wire the ESP to the RS485 transceiver and connect it to the Flexit controller
   (see [Recommended Hardware](#recommended-hardware)).

2. **Create your ESPHome config.** This is [`example.yaml`](example.yaml), set up for a XIAO ESP32-C3 on
   the XIAO RS485 board. Change the board and pins to match your hardware:

   ```yaml
   # Example config. Board and pins are for a XIAO ESP32-C3 on the XIAO RS485 board (see hardware/),
   # change them to whatever you're using. Any ESP32/ESP8266 with an RS485 transceiver should work.
   esphome:
     name: flexit
     friendly_name: Flexit

   esp32:
     board: seeed_xiao_esp32c3
     framework:
       type: arduino

   logger:
     level: WARN
     # on ESP8266, keep the logger off the modbus uart:
     # hardware_uart: UART1

   api:
   ota:
     - platform: esphome

   wifi:
     ssid: !secret wifi_ssid
     password: !secret wifi_password
     fast_connect: true             # needed if powered from the CS60

   substitutions:
     # XIAO RS485 board pins, change these for your hardware
     flexit_tx_pin: GPIO6
     flexit_rx_pin: GPIO7
     # DE/RE pin. Remove if your RS485 board doesn't have one
     flexit_tx_enable_pin: GPIO4
     # flexit_tx_enable_direct: "false"   # if your board wants DE active low
     flexit_address: "3"
     # branch/tag for packages and component, e.g. dev for testing
     flexit_ref: main
     # 0s if you want every build to pull the latest
     flexit_refresh: 1d

   packages:
     flexit:
       url: https://github.com/MSkjel/esphome-flexit-modbus-server
       ref: ${flexit_ref}
       refresh: ${flexit_refresh}
       files:
         - packages/core.yaml         # required
         - packages/controls.yaml
         - packages/sensors.yaml
         - packages/diagnostics.yaml
         - packages/fan_speeds.yaml   # EC fans only
         # - packages/fireplace.yaml  # needs controls + fan_speeds
         # - packages/advanced.yaml
   ```

3. **Pick the packages you want** from the table below, then flash.

---

## Packages

Each package is a YAML file in [`packages/`](packages). Add the ones you want to the `files:` list.

| Package | Contents | Notes |
|---|---|---|
| [`core.yaml`](packages/core.yaml) | UART + `flexit_modbus_server` (`id: server`) | Required. Pins and address via substitutions |
| [`controls.yaml`](packages/controls.yaml) | Mode, setpoint, heater, supply air control, max timer, filter interval, supply temp min/max, alarm/filter reset | |
| [`sensors.yaml`](packages/sensors.yaml) | Temperatures, heating/cooling/heat exchanger %, runtime counters, alarms | |
| [`diagnostics.yaml`](packages/diagnostics.yaml) | Fan type (EC/AC), heating type, controller SW version, week program active, home/away input, filter resets | Read only |
| [`fan_speeds.yaml`](packages/fan_speeds.yaml) | Supply/extract fan % for MIN/NORMAL/MAX | EC fans only. Disabled by default |
| [`fireplace.yaml`](packages/fireplace.yaml) | Fireplace mode (temporary MAX with custom fan speeds) | Needs `controls.yaml` and `fan_speeds.yaml` |
| [`advanced.yaml`](packages/advanced.yaml) | Installer settings from the CI600 menus (see below) | All disabled by default |

All settings are read back from the CS60, so changes made on a CI600 panel show up in Home Assistant as well
(see [Limitations](#limitations)).

### Customizing

Every entity has an `id`, so you can change or drop entities without copying the package:

```yaml
number:
  - id: !extend temperature
    step: 0.1                                  # override an option

sensor:
  - id: !remove return_water_temperature       # no water heating coil
```

Hardware settings are substitutions in `core.yaml`: `flexit_tx_pin`, `flexit_rx_pin`, `flexit_address`,
and two optional ones that depend on your RS485 hardware:
`flexit_tx_enable_pin` (the DE/RE pin, only if your transceiver has one wired to the ESP; default `none`) and
`flexit_tx_enable_direct` (default `true`; set `false` to invert the DE signal if your transceiver needs it). Other server options can be added with `!extend server`, see
[TCP Bridge](#optional-extras).

`flexit_ref` selects the branch or tag for both the packages and the component, e.g. `dev` to test a
branch or a release tag to pin a version. While testing a branch you keep pushing to, set
`flexit_refresh: 0s` so every build fetches the latest commit.

### Fireplace mode

Temporarily switches to MAX with separate supply/extract fan speeds (for example high supply, low extract to
help a fireplace draw), then restores the previous mode and fan speeds after the configured duration. It turns
itself off if the mode is changed elsewhere while it is active.

### Advanced settings

`advanced.yaml` mirrors the installer settings in the CI600's *Advanced user* (PIN 1000) and *Service*
(PIN 8888) menus: sensor enables and calibration, filter guard, external temperature control, cooling and
coolness recovery, neutral zones, fire/smoke mode, home/away, start/stop sequence, AC-fan speeds, rotor alarm
type, de-icing, and clearing the alarm log. They are `disabled_by_default`; enable the ones you need in
Home Assistant. Some only apply to one hardware variant: the fan type (DIP switch DS4, shown by
`diagnostics.yaml`) decides whether the EC fan percentages or the AC-fan speed settings are used, and the
return water settings only matter for units with a water heating coil.

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
  - id: !extend server
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
