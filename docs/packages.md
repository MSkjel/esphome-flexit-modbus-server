# Packages

What each package does, and its entities and settings. The list is in the [README](../README.md#packages), the
panel's packages in [panel.md](panel.md).

- [Boosts](#boosts)
- [Fireplace mode](#fireplace-mode)
- [Humidity boost](#humidity-boost)
- [CO2 control](#co2-control)
- [Summer cooling](#summer-cooling)
- [Computed sensors](#computed-sensors)
- [Advanced settings](#advanced-settings)

## Boosts

Fireplace mode, humidity boost, CO2 control and summer cooling all raise the mode for a while. They don't
set the mode themselves. Each one puts in a request, the highest request wins, and the unit goes back to the
mode it was in when the last request is gone. So they can overlap without fighting, and you can use any
combination of them. `Mode Boosted By` shows who is asking right now.

Changing the mode yourself (Home Assistant, a CI600, the week program) always wins. It cancels the boosts
that are running, and each of them waits until its reason is gone before it starts again. A stopped unit is
never started by a boost. The mode and fan speeds a boost sets (CO2 control's normal speeds, fireplace mode's
max speeds) are never saved, the ESP keeps the real ones. If it restarts or loses power during a boost, even
right as one starts or ends, it sends the real ones back when it is up again. If you set a fan speed while a
boost has it raised, that is the real one from then on: CO2 control adds to it, fireplace mode ends.

To add your own, ask for a mode from a lambda and release it when you're done:

```yaml
- lambda: flexit::controls::request("cooker hood", 3);   // 1 = min, 2 = normal, 3 = max
- delay: 20min
- lambda: flexit::controls::release("cooker hood");
```

To change a setting for a while, send it as temporary, worked out from the real value, and restore it when
you're done. A reboot in between is taken care of, and `id(server).add_on_setting_callback()` tells you when
someone changes a setting by hand meanwhile.

```yaml
- lambda: |-
    auto reg = flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX;
    uint16_t real = id(server).real_value(reg);    // 0 until the CX60 has sent it
    if (real)
      id(server).send_cmd(reg, std::min(real + 20, 100), true);
- delay: 20min
- lambda: id(server).restore(flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX);
```

An automation in Home Assistant that changes the unit's settings (fan speeds and so on) makes real changes
instead. Each one is written to flash right away, and the automation has to put the old value back itself.
Changing them often means writing the flash often, which matters most on an ESP8266.

## Fireplace mode

Temporarily switches to MAX with separate supply/extract fan speeds (for example high supply, low extract to
help a fireplace draw), then restores the fan speeds after the configured duration.

It needs `controls.yaml`, and EC fans, since it changes the MAX fan speeds. The defaults are 60 minutes at
90 % supply and 20 % extract. Change them in Home Assistant, or set another starting value in the config. A
value changed in Home Assistant is saved and wins over the config.

```yaml
packages:
  flexit:
    # url, ref and refresh as in example.yaml
    files:
      - packages/core.yaml
      - packages/controls.yaml
      - packages/fireplace.yaml

number:
  - id: !extend fireplace_duration
    initial_value: 45
```

| Entity | Default | What it does |
|---|---|---|
| `Fireplace Mode` | off | Turn on to start. Turns itself off after the duration |
| `Fireplace Mode Duration` | 60 min | How long it runs |
| `Fireplace Supply Fan Speed` | 90 % | Supply fan speed while it runs |
| `Fireplace Extract Fan Speed` | 20 % | Extract fan speed while it runs |

## Humidity boost

Switches to MAX when humidity jumps above its normal level, until the room has recovered. "Normal" is the
median of the last hour, so it follows the weather and seasons by itself, and the boost reacts to a sudden
rise rather than to a fixed humidity level.

The package needs a humidity sensor from your own config. Any platform works, including a `homeassistant`
sensor. Point `flexit_humidity_sensor` at its `id`:

```yaml
packages:
  flexit:
    # url, ref and refresh as in example.yaml
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

A shower right after another one barely moves the humidity above the baseline, which is still up from the
first. For that there is the rise trigger: it boosts when the humidity has risen that much within the last
`flexit_humidity_rise_minutes` (substitution, default 5), and is above the release delta. It is off until you
set it, graph `Humidity Rise` for a while first to pick a number.

| Entity | Default | What it does |
|---|---|---|
| `Humidity Boost` | off | On while boosting. Can also be turned on by hand, it then ends like any other boost |
| `Humidity Boost Enable` | on | Off means it never starts by itself |
| `Humidity Boost Trigger Delta` | 8 % | Starts when humidity is this far above the baseline |
| `Humidity Boost Release Delta` | 3 % | Ends when humidity is back within this of the baseline |
| `Humidity Boost Rise Trigger` | 0 % | Also starts when humidity rises this much within the rise minutes. 0 = off |
| `Humidity Boost Release Minutes` | 5 min | How long it has to stay there before the boost ends. *Hidden* |
| `Humidity Boost Min Minutes` | 5 min | Shortest boost |
| `Humidity Boost Max Minutes` | 45 min | Longest boost, in case humidity never comes back down |
| `Humidity Boost Cooldown Minutes` | 15 min | Pause after a boost before the next can start. *Hidden* |
| `Humidity Baseline` | | The normal humidity it compares against |
| `Humidity Above Baseline` | | Humidity now minus the baseline |
| `Humidity Rise` | | Humidity now minus the lowest in the rise minutes |

## CO2 control

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
    # url, ref and refresh as in example.yaml
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

## Summer cooling

Switches to MAX when the house is too warm and the outdoor air is at least `Summer Cooling Min Difference`
colder than the extract air, at any time of day. Too warm means the extract air is above `Summer Cooling Above`
and also `Summer Cooling Above Setpoint` degrees over the unit's setpoint, so it doesn't cool a house the unit
is trying to heat. It never runs while the heater is on. It stops when the house has cooled down or the
outdoor air is no longer colder. The CX60 still regulates the supply air to its setpoint, so how much this
cools depends on that setpoint. `Summer Cooling Min Outdoor` keeps it from running in winter.

It only needs the package, the temperatures come from the unit. The numbers can be changed in Home
Assistant, or given other starting values in the config:

```yaml
packages:
  flexit:
    # url, ref and refresh as in example.yaml
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
| `Summer Cooling Above` | 24 C | Extract air has to be warmer than this |
| `Summer Cooling Above Setpoint` | 1 C | and this much warmer than the unit's setpoint |
| `Summer Cooling Min Difference` | 2 C | Outdoor air has to be this much colder than the extract air |
| `Summer Cooling Min Outdoor` | 12 C | Never runs when it is colder than this outside. *Hidden* |
| `Summer Cooling Active` | | On while it is cooling |

## Computed sensors

`computed.yaml` adds values worked out from what the CX60 already sends:

- `Heat Recovery Efficiency`, from the supply, extract and outdoor temperatures. Only published while the
  rotor runs without the heater or cooling, and with at least 5 degrees between inside and outside.
- `Heater Power` and `Heater Energy`, estimated from the heating percentage. Set `flexit_heater_power` to the
  rated power of your electric heater in watts (default `900`).
- `Filter Days Remaining`, and one `Alarm` sensor that is on when any alarm is.

```yaml
packages:
  flexit:
    # url, ref and refresh as in example.yaml
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

## Advanced settings

`advanced.yaml` mirrors the installer settings in the CI600's *Advanced user* (PIN 1000) and *Service*
(PIN 8888) menus: sensor enables and calibration, filter guard, external temperature control, cooling and
coolness recovery, neutral zones, fire/smoke mode, home/away, start/stop sequence, AC-fan speeds, rotor alarm
type, de-icing, and clearing the alarm log. They are `disabled_by_default`; enable the ones you need in
Home Assistant. Some only apply to one hardware variant: the fan type (DIP switch DS4, shown by
`diagnostics.yaml`) decides whether the EC fan percentages or the AC-fan speed settings are used, and the
return water settings only matter for units with a water heating coil.
