# ESPHome Flexit Modbus Server

This project implements a Modbus server for Flexit ventilation systems using ESPHome.  
**No Flexit CI66 adapter is required.**  

---

## Features

- Control Flexit ventilation systems (tested on CX60, may work with others compatible with CI600 panel)
- Works with ESP8266 or ESP32 microcontrollers
- Integrates with ESPHome for easy Home Assistant support

---

## Requirements

- Flexit ventilation system with CX60 (or similar) controller
- ESP8266 or ESP32 device
- ESPHome 2026.9 or newer
- UART-to-RS485 transceiver (e.g., MAX485, MAX1348)
- Basic ESPHome YAML configuration knowledge

---

## Recommended hardware

| MCU             | RS485 Breakout Board | Notes                                                                 |
|-----------------|---------------------|-----------------------------------------------------------------------|
| XIAO-ESP32-C3   | XIAO-RS485-Expansion-Board  | [Details](hardware/xiao-esp32-c3-rs485-breakout-board-for-seeed-studio-xiao-tp8485e.md) |

---

## Quick start

1. **Connect hardware:** wire the ESP to the RS485 transceiver and connect it to the Flexit controller
   (see [Recommended hardware](#recommended-hardware)).

2. **Create your ESPHome config.** Start from [`example.yaml`](example.yaml). It's set up for a XIAO ESP32-C3 on
   the XIAO RS485 board, so change the board and pins to match your hardware.

3. **Pick the packages you want** from the [table below](#packages) for the `files:` list, then flash. In
   `example.yaml` that's:

   ```yaml
   files:
     - packages/core.yaml         # required
     - packages/controls.yaml
     - packages/sensors.yaml
     - packages/diagnostics.yaml
     - packages/fan_speeds.yaml   # EC fans only
     # - packages/fireplace.yaml       # needs controls, EC fans only
     # - packages/humidity_boost.yaml  # needs controls + your own humidity sensor
     # - packages/co2.yaml             # needs controls + your own CO2 sensor
     # - packages/summer_cooling.yaml  # needs controls
     # - packages/computed.yaml
     # - packages/advanced.yaml
   ```

---

## Packages

Each package is a YAML file in [`packages/`](packages). Add the ones you want to the `files:` list. Packages
with logic beyond plain entities keep their code in [`packages/code/`](packages/code), one C++ header each
(`controls.h`, `humidity_boost.h`, ...), included from the YAML. [`packages/parts/`](packages/parts) holds
pieces the packages share, you don't add those yourself.

| Package | Contents | Notes |
|---|---|---|
| [`core.yaml`](packages/core.yaml) | UART + `flexit_modbus_server` (`id: server`) | Required. Pins and address via substitutions |
| [`controls.yaml`](packages/controls.yaml) | Mode, setpoint, heater, supply air control, max timer, filter interval, supply temp min/max, alarm/filter reset | |
| [`sensors.yaml`](packages/sensors.yaml) | Temperatures, heating/cooling/heat exchanger %, max timer remaining, runtime counters, alarms | |
| [`diagnostics.yaml`](packages/diagnostics.yaml) | CX60 connected, fan type (EC/AC), heating type, controller SW version, week program active, home/away input, filter resets | Read only |
| [`fan_speeds.yaml`](packages/fan_speeds.yaml) | Supply/extract fan % for MIN/NORMAL/MAX | EC fans only. Disabled by default |
| [`fireplace.yaml`](packages/fireplace.yaml) | Fireplace mode (temporary MAX with custom fan speeds) | Needs `controls.yaml`. EC fans only |
| [`humidity_boost.yaml`](packages/humidity_boost.yaml) | Humidity boost (temporary MAX when humidity spikes, e.g. a shower): above its 1 h baseline, or a fast rise | Needs `controls.yaml` and a humidity sensor of your own |
| [`co2.yaml`](packages/co2.yaml) | CO2 control around a target: stepped (MAX), proportional or PI (fan speeds) | Needs `controls.yaml` and a CO2 sensor of your own. The fan speed modes are EC fans only |
| [`summer_cooling.yaml`](packages/summer_cooling.yaml) | Summer cooling (MAX when it is warm inside and cooler outside) | Needs `controls.yaml` |
| [`computed.yaml`](packages/computed.yaml) | Heat recovery efficiency, heater power/energy, filter days remaining, combined alarm | Read only |
| [`advanced.yaml`](packages/advanced.yaml) | Installer settings from the CI600 menus | All disabled by default |
| [`health.yaml`](packages/health.yaml) | The ESP's free RAM, largest free block and loop time | Diagnostic |
| [`panel.yaml`](packages/panel.yaml) | A CI600 running the [replacement firmware](#ci600-panel-replacement-firmware): its settings in Home Assistant, notifications, pictures, your values on its screen | ESP32 only, ESP at address 1. Extras in [`packages/panel/`](packages/panel) |

What each one does, with its entities and settings, is in [docs/packages.md](docs/packages.md). All settings are
read back from the CX60, so changes made on a CI600 panel show up in Home Assistant as well (see
[Saved settings and counters](#saved-settings-and-counters)).

### Customizing

Every entity has an `id`, so you can change or drop entities without copying the package:

```yaml
number:
  - id: !extend temperature
    step: 0.1                                  # override an option

sensor:
  - id: !remove return_water_temperature       # no water heating coil
```

Packages are applied first and your own config last, so anything you put in your YAML wins over the same
thing in a package. The `substitutions:` in `core.yaml` are only defaults for the ones you don't set
yourself. `esphome config yourfile.yaml` prints the merged result if you want to check what a build uses.

Hardware settings are substitutions in `core.yaml`: `flexit_tx_pin`, `flexit_rx_pin`, `flexit_address`,
and two optional ones that depend on your RS485 hardware:
`flexit_tx_enable_pin` (the DE/RE pin, only if your transceiver has one wired to the ESP; default `none`) and
`flexit_tx_enable_direct` (default `true`; set `false` to invert the DE signal if your transceiver needs it).
On ESP32 the pin goes to the UART's RS-485 mode, which switches it right at the end of each transmission. Any
GPIO will do.

Other server options go in with `!extend server`: `persist_settings` and `persist_runtime_counters` (see
[Saved settings and counters](#saved-settings-and-counters)) and the [TCP bridge](#optional-extras).

`flexit_ref` selects the branch or tag for both the packages and the component, e.g. `dev` to test a
branch or a release tag to pin a version. While testing a branch you keep pushing to, set
`flexit_refresh: 0s` so every build fetches the latest commit.

`modbus_library_ref` does the same for [ESP-ModbusRTUServer](https://github.com/MSkjel/ESP-ModbusRTUServer),
the Modbus library the component is built on (default `main`). `flexit_refresh` doesn't apply to it: ESPHome
(PlatformIO on ESP8266) keeps the copy it downloaded, so to get new commits on the same branch use Clean Build
Files.

### In Home Assistant

A few things that are the same for all packages:

- **Where things show up.** The device page has four lists. *Controls* and *Sensors* hold what you use day
  to day. *Configuration* holds settings you set once, like thresholds and timers. *Diagnostic* holds values
  that are mostly useful when tuning or troubleshooting.
- **Hidden settings.** Rarely needed entities are disabled by default, marked *hidden* in
  [docs/packages.md](docs/packages.md). They still work with their default value. To change one, open the device
  page, click the line with the number of entities not shown, open the entity, click the cog and turn on
  *Enabled*.
- **Nothing appears by itself.** The entities are fixed when the firmware is built. Picking a mode in a select
  can't show or hide the settings that go with it, so settings for a mode you don't use are still listed.
- **Settings are saved on the ESP.** A value you change in Home Assistant survives reboots and most updates
  (see [Saved settings and counters](#saved-settings-and-counters)), and wins over a starting value in the
  config.
- **Unknown values.** The mode, temperatures, percentages and alarms come in the CX60's status broadcast, and
  are unknown until the ESP has heard one. When it hasn't for 10 seconds the temperatures and percentages go
  back to unknown, the rest keep their last state, and `CX60 Connected` (`diagnostics.yaml`) goes off: the
  wiring, the RS485 board or the CX60 is down.
- **Seeing why something happened.** The packages log when they start and stop and why. That is at `INFO`
  level, so set `logger: level: INFO` while you are tuning. Their log tags start with `flexit.`, like
  `flexit.co2` and `flexit.mode`, and the component's is `flexit_modbus_server`.

---

## Limitations

- **Start-up order.** The ESP has to be on before the CX60, or the CX60 won't poll it.
- **Address.** The CX60 only takes the setpoint and the NORMAL fan speeds from the lowest address, and runs the
  week program and boosts only when something answers at 1. With a stock CI600 at 1 the ESP has to use 3, and
  then changing those from Home Assistant won't take, nor will CO2 control's proportional and PI modes. Heater
  on/off works from any address. A CI600 with the [replacement firmware](#ci600-panel-replacement-firmware) runs
  at 3, so the ESP can be 1.
- **Settings.** The ESP only hears a setting when the CX60 sends it, see below.

### Saved settings and counters

The ESP learns the settings from what the CX60 broadcasts: all of them when the CX60 starts, and every change
made from any panel. It writes each change to flash right away and loads them on boot, all but the week program's
slot times, speeds and temperatures. A change made on a CI600 while the ESP is off or restarting isn't heard. It
shows the old value until it changes again or the CX60 restarts, and a boost may put the old value back.

On a fresh install, or after an update that changes which settings are saved (the log warns about it), the
settings show up once the CX60 has restarted with the ESP running, and boosts wait until then. A boost running
during such an update leaves its mode and fan speeds in the CX60, so set those back by hand.

The CX60 only sends a runtime counter word when it changes, so the ESP saves those too. On a fresh install they
show as unknown until the CX60 has sent both words, at the latest when it restarts. The filter's high word comes
from the CX60's status hours, which also correct the filter and total after the ESP was off. The others can be
short by multiples of 18 h after a long ESP outage, until the CX60 restarts.

Both can be turned off. Every restart of the ESP is then like a fresh install:

```yaml
flexit_modbus_server:
  - id: !extend server
    persist_settings: false
    persist_runtime_counters: false
```

An ESP8266 has little room for saved values unless `restore_from_flash` is on, and the build warns when WiFi
credentials or an API key from Home Assistant won't fit next to these. The warning says which one to turn off.

---

## CI600 panel (replacement firmware)

With the [replacement CI600 firmware](https://github.com/MSkjel/flexit-ci600-firmware), the panel and this ESP
share the bus. The panel shows what the ESP is doing, Home Assistant puts its own sensors and switches on the
panel's screen and sends it messages and pictures, and the panel's firmware is flashed from Home Assistant. ESP32
only.

<p align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/readme/tour.png" width="420" alt="The panel's views"></p>

How to set it up, every package with pictures of what it makes, the actions and the flashing are in
**[docs/panel.md](docs/panel.md)**.

---

## Optional extras

<details>
<summary>TCP bridge</summary>

The TCP bridge lets you watch the Modbus traffic over a TCP connection. This is useful for finding new registers.

### How it works

When enabled, the TCP bridge creates a server that:
- Accepts TCP connections on the configured port (default: 502)
- Mirrors all UART data to connected clients in real time (both TX and RX)
- Sends data with directional framing so you can tell sent and received frames apart
- Never waits for a client, so it can't hold up the bus. A client that reads too slowly misses data instead

### Frame protocol

Data is sent to TCP clients using a simple 3-byte header + payload format:
```
[Direction (1 byte)][Length High (1 byte)][Length Low (1 byte)][Payload (N bytes)]
```
- **Direction**: `'T'` (0x54) for TX (sent by the ESP), `'R'` (0x52) for RX (received from the bus)
- **Length**: 16-bit big-endian payload length

Each block is one frame. Bytes from the bus that aren't part of a valid frame come as an RX block of their own.

### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `tcp_bridge_enabled` | boolean | `false` | Enable/disable TCP bridge |
| `tcp_bridge_port` | integer | `502` | TCP port to listen on |
| `tcp_bridge_max_clients` | integer | `4` | Maximum concurrent clients (1-10) |

### Example

```yaml
flexit_modbus_server:
  - id: !extend server
    tcp_bridge_enabled: true
    tcp_bridge_port: 8502
    tcp_bridge_max_clients: 2
```

### Monitoring tool

A Python script for monitoring and decoding the TCP bridge traffic is included in
[scripts/tcp_bridge_monitor.py](scripts/tcp_bridge_monitor.py).

**Features:**
- Decodes Modbus RTU frames, writes (0x10) and the CX60's 0x64 included
- Color-coded TX (sent by the ESP) and RX (received) traffic
- Track coil changes per server and address with `--coil-changes`

</details>

---

## License

MIT License

---

## Credits

- [esphome-modbus-server](https://github.com/epiclabs-uc/esphome-modbus-server)
- [modbus-esp8266](https://github.com/emelianov/modbus-esp8266)
