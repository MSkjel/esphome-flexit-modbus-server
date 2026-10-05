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
         # - packages/humidity_boost.yaml  # needs controls + your own humidity sensor
         # - packages/co2.yaml             # needs controls + your own CO2 sensor
         # - packages/summer_cooling.yaml  # needs controls
         # - packages/computed.yaml
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
| [`humidity_boost.yaml`](packages/humidity_boost.yaml) | Humidity boost (temporary MAX when humidity spikes, e.g. a shower) | Needs `controls.yaml` and a humidity sensor of your own |
| [`co2.yaml`](packages/co2.yaml) | CO2 control around a target: stepped (MAX), proportional or PI (fan speeds) | Needs `controls.yaml` and a CO2 sensor of your own. The fan speed modes are EC fans only |
| [`summer_cooling.yaml`](packages/summer_cooling.yaml) | Summer cooling (MAX when it is warm inside and cooler outside) | Needs `controls.yaml` |
| [`computed.yaml`](packages/computed.yaml) | Heat recovery efficiency, heater power/energy, filter days remaining, combined alarm | Read only |
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

### In Home Assistant

A few things that are the same for all packages:

- **Where things show up.** The device page has three lists. *Controls* and *Sensors* hold what you use day
  to day. *Configuration* holds settings you set once, like thresholds and timers. *Diagnostic* holds values
  that are mostly useful when tuning or troubleshooting.
- **Hidden settings.** Rarely needed entities are disabled by default, marked *hidden* in the tables below.
  They still work with their default value. To change one, open the device page, click the line with the
  number of entities not shown, open the entity, click the cog and turn on *Enabled*.
- **Nothing appears by itself.** The entities are fixed when the firmware is built. Picking a mode in a select
  can't show or hide the settings that go with it, so settings for a mode you don't use are still listed.
- **Settings are saved on the ESP.** A value you change in Home Assistant survives reboots and updates, and
  wins over a starting value in the config.
- **Seeing why something happened.** The packages log when they start and stop and why. That is at `INFO`
  level, so set `logger: level: INFO` while you are tuning.

### Boosts

Fireplace mode, humidity boost, CO2 control and summer cooling all raise the mode for a while. They don't
set the mode themselves. Each one puts in a request, the highest request wins, and the unit goes back to the
mode it was in when the last request is gone. So they can overlap without fighting, and you can use any
combination of them. `Mode Boosted By` shows who is asking right now.

Changing the mode yourself (Home Assistant, a CI600, the week program) always wins. It cancels the boosts
that are running, and each of them waits until its reason is gone before it starts again. A stopped unit is
never started by a boost. If the ESP reboots during a boost, it goes back to the old mode when it is up again.

To add your own, put a request in from a lambda and take it out when you're done:

```yaml
- lambda: |-
    id(mode_requests)["cooker hood"] = 3;      // 1 = min, 2 = normal, 3 = max
    id(mode_arbiter).execute();
- delay: 20min
- lambda: |-
    id(mode_requests).erase("cooker hood");
    id(mode_arbiter).execute();
```

### Fireplace mode

Temporarily switches to MAX with separate supply/extract fan speeds (for example high supply, low extract to
help a fireplace draw), then restores the fan speeds after the configured duration.

It uses the mode select and the MAX fan speeds from two other packages, so all three go in the `files:` list.
The defaults are 60 minutes at 90 % supply and 20 % extract. Change them in Home Assistant, or set another
starting value in the config. A value changed in Home Assistant is saved and wins over the config.

```yaml
packages:
  flexit:
    # url, ref and refresh as in the example above
    files:
      - packages/core.yaml
      - packages/controls.yaml
      - packages/fan_speeds.yaml
      - packages/fireplace.yaml

globals:
  - id: !extend fireplace_duration_minutes
    initial_value: '45'
```

| Entity | Default | What it does |
|---|---|---|
| `Fireplace Mode` | off | Turn on to start. Turns itself off after the duration |
| `Fireplace Mode Duration` | 60 min | How long it runs |
| `Fireplace Supply Fan Speed` | 90 % | Supply fan speed while it runs |
| `Fireplace Extract Fan Speed` | 20 % | Extract fan speed while it runs |

### Humidity boost

Switches to MAX when humidity jumps above its normal level, until the room has recovered. "Normal" is the
median of the last hour, so it follows the weather and seasons by itself, and the boost reacts to a sudden
rise rather than to a fixed humidity level.

The package needs a humidity sensor from your own config. Any platform works, including a `homeassistant`
sensor. Point `flexit_humidity_sensor` at its `id`:

```yaml
packages:
  flexit:
    # url, ref and refresh as in the example above
    files:
      - packages/core.yaml
      - packages/controls.yaml
      - packages/humidity_boost.yaml

substitutions:
  flexit_humidity_sensor: inside_humidity

sensor:
  - platform: dht
    pin: GPIO10
    humidity:
      id: inside_humidity
      name: "Humidity Inside"
      filters:                 # one bad DHT read shouldn't look like a shower
        - filter_out: nan
        - median:
            window_size: 5
            send_every: 1
    update_interval: 10s
```

Graph `Humidity Above Baseline` for a few days to pick a trigger delta. After a boost that ran into the
maximum runtime it waits for humidity to come back down before it boosts again.

| Entity | Default | What it does |
|---|---|---|
| `Humidity Boost` | off | On while boosting. Can also be turned on by hand, it then ends like any other boost |
| `Humidity Boost Enable` | on | Off means it never starts by itself |
| `Humidity Boost Trigger Delta` | 8 % | Starts when humidity is this far above the baseline |
| `Humidity Boost Release Delta` | 3 % | Ends when humidity is back within this of the baseline |
| `Humidity Boost Release Minutes` | 5 min | How long it has to stay there before the boost ends. *Hidden* |
| `Humidity Boost Min Minutes` | 5 min | Shortest boost |
| `Humidity Boost Max Minutes` | 45 min | Longest boost, in case humidity never comes back down |
| `Humidity Boost Cooldown Minutes` | 15 min | Pause after a boost before the next can start. *Hidden* |
| `Humidity Baseline` | | The normal humidity it compares against |
| `Humidity Above Baseline` | | Humidity now minus the baseline |

### CO2 control

Needs a CO2 sensor from your own config, with `flexit_co2_sensor` set to its `id` (default `co2`). There is
one level to set, `CO2 Target`. Pick what happens above it with the `CO2 Control` select:

- **Stepped** switches to MAX when CO2 is well over the target, and stays there until it is back under it.
- **Proportional** raises the NORMAL mode fan speeds the further CO2 is over the target, up to
  `CO2 Max Fan Increase`. Simple and steady, but CO2 settles somewhere above the target.
- **PI** keeps raising the fan speeds until CO2 is back at the target. It holds the level better, but the two
  PI settings may need tuning for your house if the fans start going up and down.

"Well over" is `CO2 Band` above the target, 400 ppm by default. The fan modes are for EC fans only. Supply
and extract get the same increase, so the balance between them stays. In MIN the NORMAL speeds aren't used,
so the fan modes also switch from MIN to NORMAL when CO2 is a band over the target.

| Entity | Default | What it does |
|---|---|---|
| `CO2 Control` | Off | Off, Stepped, Proportional or PI |
| `CO2 Target` | 800 ppm | The level to keep CO2 at. Nothing happens under it |
| `CO2 Band` | 400 ppm | How far over the target counts as high. Smaller reacts sooner and harder. *Hidden* |
| `CO2 Max Fan Increase` | 30 % | Proportional and PI: the most that is added to the NORMAL fan speeds |
| `CO2 PI Gain` | 5 % | PI: fan speed added for every 100 ppm over the target. Higher is faster. *Hidden* |
| `CO2 PI Integral Time` | 30 min | PI: how quickly it keeps adding while CO2 stays over the target. Shorter is faster, too short and the fans go up and down. *Hidden* |
| `CO2 Control Active` | | On while it is holding a higher mode or has raised the fan speeds |
| `CO2 Fan Increase` | | What proportional or PI has added to the fan speeds right now. Graph it against CO2 when tuning |

With the defaults, proportional adds 15 % at 1000 ppm and the full 30 % at 1200 ppm. PI starts out
careful and can take hours to bring CO2 all the way down to the target. If that is too slow, raise the gain
or shorten the integral time a step at a time.

```yaml
packages:
  flexit:
    # url, ref and refresh as in the example above
    files:
      - packages/core.yaml
      - packages/controls.yaml
      - packages/co2.yaml

substitutions:
  flexit_co2_sensor: co2

i2c:                       # pins for your hardware
  sda: GPIO5
  scl: GPIO3

sensor:
  - platform: scd4x
    co2:
      id: co2
      name: "CO2"
    update_interval: 60s
```

If the sensor stops giving a value, both go back to normal. A `homeassistant` sensor keeps its last value
when Home Assistant goes away, so give it a `timeout` filter:

```yaml
sensor:
  - platform: homeassistant
    id: co2
    entity_id: sensor.living_room_co2
    filters:
      - timeout: 30min
```

### Summer cooling

Switches to MAX when the house is too warm and the outdoor air is at least `Summer Cooling Min Difference`
colder than the extract air, at any time of day. Too warm means the extract air is above `Summer Cooling Above`
and also `Summer Cooling Above Setpoint` degrees over the unit's setpoint, so it doesn't cool a house the unit
is trying to heat. It never runs while the heater is on. It stops when the house has cooled down or the
outdoor air is no longer colder. The CS60 still regulates the supply air to its setpoint, so how much this
cools depends on that setpoint. `Summer Cooling Min Outdoor` keeps it from running in winter.

It only needs the package, the temperatures come from the unit. The numbers can be changed in Home
Assistant, or given other starting values in the config:

```yaml
packages:
  flexit:
    # url, ref and refresh as in the example above
    files:
      - packages/core.yaml
      - packages/controls.yaml
      - packages/summer_cooling.yaml

number:
  - id: !extend summer_cooling_above
    initial_value: 23
  - id: !extend summer_cooling_min_difference
    initial_value: 3
```

| Entity | Default | What it does |
|---|---|---|
| `Summer Cooling Enable` | on | Off means it never starts |
| `Summer Cooling Above` | 24 °C | Extract air has to be warmer than this |
| `Summer Cooling Above Setpoint` | 1 °C | and this much warmer than the unit's setpoint |
| `Summer Cooling Min Difference` | 2 °C | Outdoor air has to be this much colder than the extract air |
| `Summer Cooling Min Outdoor` | 12 °C | Never runs when it is colder than this outside. *Hidden* |
| `Summer Cooling Active` | | On while it is cooling |

### Computed sensors

`computed.yaml` adds values worked out from what the CS60 already sends:

- `Heat Recovery Efficiency`, from the supply, extract and outdoor temperatures. Only published while the
  rotor runs without the heater or cooling, and with at least 5 °C between inside and outside.
- `Heater Power` and `Heater Energy`, estimated from the heating percentage. Set `flexit_heater_power` to the
  rated power of your electric heater in watts (default `900`).
- `Filter Days Remaining`, and one `Alarm` sensor that is on when any alarm is.

```yaml
packages:
  flexit:
    # url, ref and refresh as in the example above
    files:
      - packages/core.yaml
      - packages/computed.yaml

substitutions:
  flexit_heater_power: "1200"

# with a water coil instead of an electric heater, drop the two heater sensors
# sensor:
#   - id: !remove heater_energy
#   - id: !remove heater_power
```

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
