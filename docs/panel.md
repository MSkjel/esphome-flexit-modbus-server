# The CI600 panel

With the [replacement CI600 firmware](https://github.com/MSkjel/flexit-ci600-firmware), the panel and this ESP
share the bus. The panel shows what the ESP is doing, Home Assistant can put its own things on the panel's screen
and control the panel, and the panel's firmware is flashed from Home Assistant. It's all YAML packages, ESP32 only.

- [Setup](#setup)
- [Your own things on the panel](#your-own-things-on-the-panel)
- [The comfort packages](#the-comfort-packages)
- [Messages, views and settings](#messages-views-and-settings)
- [Pictures, animations and the weather](#pictures-animations-and-the-weather)
- [Flashing the panel](#flashing-the-panel)
- [Icons](#icons)

The pictures here come from the firmware's simulator. The ESP in them has a few sensors and switches set up the
way this page shows.

## Setup

`packages/panel.yaml` is the link. Add the `panel/` packages you want after it. The panel runs at address 3, so
the ESP goes to address 1. A panel still on the stock firmware is at 1 itself, so flash it first with the ESP at 2,
see [Flashing the panel](#flashing-the-panel). If your panel is at another address, set `panel_address` to match,
like `- path: packages/panel.yaml` with `vars: {panel_address: "2"}`. The ESP only talks to that panel. A second
panel with this firmware works fine without the ESP, just switch ESP off under System > Equipment on that one.

```yaml
substitutions:
  flexit_address: "1"
  flexit_ref: main
  flexit_refresh: 1d

packages:
  flexit:
    url: https://github.com/MSkjel/esphome-flexit-modbus-server
    ref: ${flexit_ref}
    refresh: ${flexit_refresh}
    files:
      - packages/core.yaml
      - packages/controls.yaml
      - packages/fireplace.yaml
      - packages/panel.yaml
      - packages/panel/update.yaml             # flash the panel from Home Assistant
      - packages/panel/fireplace.yaml          # one per comfort package you have
      - path: packages/panel/sensor.yaml       # your own things on the panel
        vars: {entity: living_room_temperature, label: Living room|Stue, icon: thermo}
      - path: packages/panel/met.yaml          # the weather as the screensaver
        vars: {latitude: "59.9139", longitude: "10.7522"}
```

The panel packages need the current Modbus library, its `main`. If the ESP was built before October 2026, do a
Clean Build Files, or ESPHome keeps the old library.

`panel.yaml` gives Home Assistant `Panel Connected`, `Panel Temperature` (the panel's own sensor),
`Panel Firmware`, `Panel Action` (filter reset and the like, picked on the panel), the screensaver and LED
selects, `Panel Brightness`, `Panel Idle Brightness`, `Panel Night Mode`, `Panel Key Lock` and `Panel Restart`.
`Panel Link Windows Too Short`, `Panel Link Unanswered` and `Panel Link Lost` count link trouble since the ESP
started. The rest of the panel's settings are in its own menu and [`panel_set`](#panel-settings).

| File | What |
|---|---|
| `panel.yaml` | The link and the panel's own entities |
| `panel/ha_sensor.yaml`, `ha_binary_sensor.yaml`, `ha_text.yaml`, `ha_switch.yaml` | A Home Assistant entity on the panel |
| `panel/sensor.yaml`, `binary_sensor.yaml`, `text.yaml`, `switch.yaml`, `number.yaml`, `button.yaml` | One of the ESP's own entities on the panel |
| `panel/fireplace.yaml`, `humidity_boost.yaml`, `co2.yaml`, `summer_cooling.yaml` | That package on the panel's comfort page |
| `panel/picture.yaml`, `online_picture.yaml`, `ha_picture.yaml`, `animation.yaml`, `met.yaml` | Screensaver pictures |
| `panel/update.yaml` | Flashes the panel's firmware from Home Assistant |
| `panel/screen_camera.yaml` | What the panel shows, as a camera in Home Assistant |

## Your own things on the panel

Any Home Assistant or ESPHome entity goes on the panel with one include. The panel has no idea what it is in
advance: the ESP sends the name, the icon, the range and the texts, and the panel draws it. Sensors become gauges
and numbers, switches become switches, buttons run something, texts show as they are. Changes made on the panel go
back to the entity, and if it hasn't followed within 3 s (15 s for `ha_switch`) the panel shows its state again.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/50_smart_view.png" width="280" alt="Smart home view"><br><sub>The smart home view: a sensor, a price, a switch, a text and a number</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/51_smart_menu.png" width="280" alt="Smart home menu"><br><sub>The same in the menu</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/52c_item_edit.png" width="280" alt="Editing a number"><br><sub>Editing a number on the panel</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/52b_item_help.png" width="280" alt="Help text"><br><sub>The help text, on ?</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/19d_dashboard_tiles.png" width="280" alt="Dashboard tiles"><br><sub>Any item can be a dashboard tile</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/03b_sensors.png" width="280" alt="Sensor trends"><br><sub>Sensors with <code>trend: "true"</code> get 24 h charts</sub></td>
</tr>
</table>

A Home Assistant entity goes on with an `ha_` package, which makes the `homeassistant` entity on the ESP for you:

```yaml
      - path: packages/panel/ha_sensor.yaml
        vars: {name: hot_water, entity_id: sensor.water_heater_temperature, label: Hot water|Varmtvann, icon: shower,
               unit: °C, decimals: "0", levels: "40,50,75,85", min: "20", max: "80"}
      - path: packages/panel/ha_binary_sensor.yaml
        vars: {name: washer_running, entity_id: binary_sensor.washer_running, label: Washer|Vaskemaskin, icon: washer,
               on_text: Running|Går, off_text: Done|Ferdig}
      - path: packages/panel/ha_text.yaml
        vars: {name: dishwasher, entity_id: sensor.dishwasher_phase, label: Dishwasher|Oppvaskmaskin, icon: washer}
      - path: packages/panel/ha_switch.yaml
        vars: {name: garden_lights, entity_id: light.garden, label: Garden lights|Hagelys, icon: bulb}
      - path: packages/panel/ha_switch.yaml
        vars: {name: front_door, entity_id: lock.front_door, label: Front door|Inngangsdør, icon: lock,
               on_action: lock.lock, off_action: lock.unlock, on_state: locked, on_text: Locked|Låst,
               off_text: Unlocked|Ulåst}
```

`ha_switch` runs `on_action` and `off_action` (`homeassistant.turn_on` and `turn_off` unless you say otherwise)
on the entity, and is on while the entity's state is `on_state` (`on`). For that the ESP needs "Allow the device
to perform Home Assistant actions" ticked in Home Assistant's ESPHome integration. The panel shows a dash until
Home Assistant has a state for it, or while the entity is unavailable. `attribute` on the other three takes an
attribute of the entity instead of its state. A scene or a script is a template button of your own, put on with
`button.yaml`:

```yaml
button:
  - platform: template
    id: scene_bed
    on_press:
      - homeassistant.action: {action: scene.turn_on, data: {entity_id: scene.bed}}
```

The ESP's own entities go on with the plain packages, which take the ESPHome id as `entity`:

```yaml
      - path: packages/panel/sensor.yaml
        vars: {entity: living_room_co2, label: Living room|Stue, icon: co2, unit: ppm,
               levels: "400,400,900,1500", min: "400", max: "2000", trend: "true"}
      - path: packages/panel/binary_sensor.yaml
        vars: {entity: front_door_locked, label: Door locked|Dør låst, icon: lock,
               on_text: Locked|Låst, off_text: Unlocked|Ulåst}
      - path: packages/panel/text.yaml
        vars: {entity: mode_boosted_by, label: Boosted by|Forsert av, icon: fan}
      - path: packages/panel/switch.yaml
        vars: {entity: garden_lights, label: Garden lights|Hagelys, icon: bulb}
      - path: packages/panel/number.yaml
        vars: {entity: hot_water_setpoint, label: Hot water|Varmtvann, unit: °C, icon: shower}
      - path: packages/panel/button.yaml
        vars: {entity: scene_goodnight, label: Goodnight|God natt, icon: night, view: "false"}
```

The vars, with their defaults at the top of each file:

| Var | What |
|---|---|
| `name` | The item's name on the link, the panel keeps its dashboard tiles by it. `ha_` packages need one and make the ESPHome id `ha_<name>` from it. Plain packages use the entity id unless you give one |
| `entity_id` | `ha_` packages: the Home Assistant entity |
| `entity` | Plain packages: the ESPHome id |
| `label`, `help` | `English\|Norsk`, the panel shows the part for its language. Help shows on ? |
| `icon` | An id from the [icons](#icons), in lower case |
| `page` | `smart_home` (default) or `comfort` |
| `view`, `menu` | `"false"` leaves it off the page's view or its menu |
| `unit`, `decimals` | Sensors and numbers; the entity's own if empty. An `ha_sensor` has no unit unless you give it one |
| `levels`, `min`, `max` | Sensors: `"bad below, good from, good to, bad above"` colours the value, min and max are the ends of its gauge |
| `trend` | Sensors: `"true"` keeps the last 24 hours for the sensors view, room for four |
| `on_text`, `off_text` | Binary sensors and switches: what to say instead of On and Off |
| `on_action`, `off_action`, `on_state` | `ha_switch`: the actions and the state that counts as on |

Inside `{}` a text with a comma needs quotes, like `help: "Warm, then cool|Varmt, så kjølig"`.

A text is read-only and shows the entity's state, as a tile on the smart home view, a row in the menu or a
dashboard tile. Like the label it can be `English|Norsk`. The comfort view shows the first two sensors as gauges
and the switches as chips, so the order in your config is the order on the screen.

### How long texts can be

The panel has room for this many bytes. In an `English|Norsk` text each part counts on its own, and special
letters, like the Norwegian ones, take two. Longer texts are cut.

| Text | Bytes |
|---|---|
| `label` | 19 |
| `help` | 78 |
| `unit` | 7 |
| `on_text`, `off_text` | 11 each |
| A text item's state | 15 |
| A `panel_notify` message | 64 |
| A `panel_saver_text` line | 64 |

## The comfort packages

`panel/fireplace.yaml`, `panel/humidity_boost.yaml`, `panel/co2.yaml` and `panel/summer_cooling.yaml` are nothing
more than the includes above, pointed at that package's entities and put on the comfort page. Add the ones you
have the main package for.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/04_comfort.png" width="280" alt="Comfort view"><br><sub>The comfort view: humidity and CO2 gauges, the automations as chips</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/14_comfort_menu.png" width="280" alt="Comfort menu"><br><sub>The comfort menu, with the settings</sub></td>
</tr>
</table>

[`panel/fireplace.yaml`](../packages/panel/fireplace.yaml) shows how: a `switch.yaml` and a `number.yaml`, both
with `page: comfort`.

## Messages, views and settings

Home Assistant gets these actions as `esphome.<device>_panel_...`. Your own ESPHome YAML has scripts with the same
names, like `script.execute: {id: panel_notify, message: Hi, icon: bell, level: info}`.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/53_notification.png" width="280" alt="A notification"><br><sub><code>panel_notify</code></sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/61_view_from_esp.png" width="280" alt="A view opened from the ESP"><br><sub><code>panel_show_view</code></sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/60_theme_from_esp.png" width="280" alt="Theme set from the ESP"><br><sub><code>panel_set</code> with <code>theme</code></sub></td>
</tr>
</table>

| Action | Does |
|---|---|
| `panel_notify` (`message`, `icon`, `level`: info, warning, alarm) | A message that pops up on the panel and stays until it's closed |
| `panel_show_view` (`view`: dashboard, airflow, trends, sensors, comfort, smart_home) | Wakes the panel and opens that view |
| `panel_set` (`setting`, `value`) | One of the panel's [settings](#panel-settings) |
| `panel_saver_text` (`lines`, `sizes`) | [Lines on the screensaver](#lines-on-the-screensaver) in place of the temperatures |

The pictures and animations have [their own actions](#picture-and-animation-actions).

### Lines on the screensaver

The screensaver shows the indoor and outdoor temperatures in `Clock and temperatures` and `Picture and
everything`. `panel_saver_text` puts up to 4 lines of your own there instead, each 16, 20 or 28 px tall (`sizes`,
16 for a line without one, other sizes go to the nearest). Over a picture there's room for about 72 px, so a 28 px
line and two 16 px ones. Lines that don't fit are left out, one too wide ends in dots, and an empty one leaves a
gap. No lines bring the temperatures back, and so does the ESP going away. The ESP keeps the lines through a panel
restart, not through its own, so update them from an automation that also runs now and then. In Home Assistant, a
new automation's Edit in YAML:

```yaml
alias: Panel screensaver lines
triggers:
  - trigger: state
    entity_id: [sensor.living_room_temperature, sensor.living_room_humidity, sensor.living_room_co2]
  - trigger: time_pattern
    minutes: /5
actions:
  - action: esphome.flexit_panel_saver_text
    data:
      lines:
        - "{{ states('sensor.living_room_temperature') }} °C"
        - "Humidity {{ states('sensor.living_room_humidity') }} %"
        - "CO₂ {{ states('sensor.living_room_co2') }} ppm"
      sizes: [28, 16, 16]
```

Or in your ESPHome YAML, from the ESP's own sensors. It sends only what changed, so once a minute costs nothing:

```yaml
interval:
  - interval: 1min
    then:
      - script.execute:
          id: panel_saver_text
          lines: !lambda |-
            // a blank line for a sensor that isn't fitted
            auto temp = [](const char *label, float t) {
              return std::isnan(t) ? std::string() : str_sprintf("%s%.1f °C", label, t);
            };
            return {temp("", id(extract_air_temperature).state), temp("Outdoor ", id(outdoor_air_temperature).state),
                    temp("Supply ", id(supply_air_temperature).state)};
          sizes: [28, 16, 16]
```

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/87b_saver_lines.png" width="280" alt="Lines over a picture"><br><sub>Over a picture</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/87d_saver_lines_black.png" width="280" alt="Lines on black"><br><sub>On black</sub></td>
</tr>
</table>

### Panel settings

`panel_set` takes these, in the panel's own units:

| Setting | Value |
|---|---|
| `theme` | 0 Midnight, 1 Daylight, 2 Graphite |
| `language` | 0 English, 1 Norwegian |
| `brightness`, `idle_brightness` | 0 to 100 %, idle 0 is dark |
| `idle_after` | 0 to 7 for 15 s, 30 s, 1, 2, 5, 10 or 30 min, or never |
| `screensaver` | 0 to 5, in the order of the `Panel Screensaver` select |
| `home_view` | 0 to 5 for dashboard, airflow, trends, sensors, comfort, smart home |
| `night` | 0 or 1, night mode |
| `night_from`, `night_to` | 0 to 23, the hours night mode starts and ends |
| `night_brightness`, `night_idle_brightness` | 0 to 100 %, idle 0 is dark |
| `key_lock`, `clock_24h`, `key_hints` | 0 or 1 |
| `indoor_source` | Where the indoor temperature comes from: 0 the extract air, 1 the panel |
| `room_offset` | The panel sensor's offset in tenths of a degree, -90 to 90 |
| `tile1` to `tile4` | The dashboard tiles, `TILE_*` in [`plink_values.h`](../packages/code/panel/plink_values.h) |

The panel's LEDs have their own selects (`Panel Alarm LED`, `Panel Filter LED`, `Panel OK LED`): Auto works like
the stock panel, the rest let Home Assistant decide.

## Pictures, animations and the weather

A picture from a file, from a URL, or the weather drawn on the ESP, with an animated GIF on top if you like. The
panel shows it as the screensaver when `Panel Screensaver` is one of the Picture modes, and `Panel Picture`
picks which one. The panel's own menu has the clock styles that go over it.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/55_saver_picture_clock.png" width="280" alt="Picture with the clock"><br><sub><code>picture.yaml</code>, with the clock on a band</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/59c_saver_photo.png" width="280" alt="A photo"><br><sub>A photo goes as a JPEG</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/58b_saver_animation.png" width="280" alt="An animation"><br><sub><code>animation.yaml</code> over it</sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/met/15_fair_forecast.png" width="280" alt="Weather with the forecast"><br><sub><code>met.yaml</code> with the forecast on</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/met/31_aurora.png" width="280" alt="Northern lights"><br><sub>Northern lights on a clear night</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/met/24_coast.png" width="280" alt="Coast scene"><br><sub>The coast scene</sub></td>
</tr>
</table>

```yaml
      - path: packages/panel/picture.yaml          # a file next to your config, PNG or JPEG
        vars: {name: aurora, file: pictures/aurora.png}
      - path: packages/panel/online_picture.yaml   # a URL, like a weather map
        vars: {name: radar, url: "http://homeassistant.local:8123/local/radar.png", update_interval: 10min}
      - path: packages/panel/ha_picture.yaml       # a Home Assistant camera or image entity
        vars: {name: front_door, entity_id: camera.front_door, update_interval: 1min}
      - path: packages/panel/animation.yaml        # a GIF, at x, y on the picture
        vars: {name: flame, file: animations/flame.gif, resize: 48x48, x: 256, y: 16}
      - path: packages/panel/field.yaml            # light the panel works out itself, see Fields
        vars: {name: glow, kind: radial, w: 60, h: 60, x: 200, y: 20, colours: "00FFF4D8, FFFFF4D8"}
      - path: packages/panel/met.yaml              # the weather where you are, named "weather"
        vars: {latitude: "59.9139", longitude: "10.7522", scene: coast}
```

A picture is fitted to the screen and converted on the ESP to 16 colours, or to a JPEG when it has more, and sent
in the gaps on the bus: a 320x240 drawing takes a few seconds, a photo half a minute. Only the rows that changed go
when a picture is updated. `Panel Picture Status` and `Panel Animation Status` show how it's going. Names are
letters, digits and `_`.

An ESP32 with PSRAM, like a WROVER or an S3 with R in its name, can decode online pictures at `resize: 320x240` and
take bigger JPEGs. Add `psram:` to your config: their download buffer and decoded pixels go there, and the rest stays
in the ESP's own RAM, which is faster. The weather's scene only goes there when that's short. An ESP32-C3 can't have
PSRAM.

| Package | Vars |
|---|---|
| `picture.yaml` | `name`, `file` (PNG or JPEG next to your config). `encoding`: `auto`, `palette` (16 colours) or `jpeg`; a palette picture that doesn't fit the panel's 14 KB goes as a JPEG anyway. `quality`: the JPEG quality, or `auto` for the best that fits |
| `online_picture.yaml` | `name`, `url`, `update_interval` (like `10min`, `1.5h` or `1d`, `never` = only when shown), `format` (PNG, JPEG, BMP or AUTO), `resize` (160x120 default, blown up to the screen; 320x240 needs PSRAM), plus `encoding` and `quality`. A failed download is retried after 30 s, then twice as long each time. Before each download the ESP asks the server about the file and skips one it doesn't have the RAM for, with the reason in `Panel Picture Status`. A JPEG has to fit in RAM whole, so the server has to send its size; PNG and BMP are read a bit at a time. For a Home Assistant camera use `ha_picture.yaml` |
| `ha_picture.yaml` | `name`, `entity_id` (a `camera.` or `image.` entity, with `kind: image` for the latter), `ha_url` (`http://homeassistant.local:8123`), `width` (320, HA scales the camera picture down to it when the camera can), plus `update_interval`, `resize`, `format`, `encoding` and `quality` as above. HA only hands out camera pictures with a token that changes now and then; the ESP follows the entity's `access_token` attribute, so nothing to set up. If the camera won't scale, the full picture is often too big for an ESP without PSRAM and is skipped; a Frigate camera's own `latest.jpg?h=120` through `online_picture.yaml` is the way round that |
| `animation.yaml` | `name`, `file` (a GIF), `resize` (64x64 default), `x`, `y` (top left on the 320x240 screen), `scale` (1, or 2 to draw each pixel 2x), `frame_ms` (40 to 2550, the GIF's own timing isn't used), `drift_x`, `drift_y` (pixels per second it drifts, wrapping round the screen seamlessly), `fade_ms` (300, how long copies fade in and out), `clips` and `script` (see [animation scripts](#animation-scripts)). The panel plays all its animations from 8.75 KB, up to 7 at a time in up to 9 places, so keep them small: about 64x64 with a few frames, or more frames if little changes between them. A single frame goes as a still, which the panel keeps packed, so it takes only that, not w x h / 2 bytes. Too big at scale 1 and it's made at half size and drawn 2x. Making one takes free RAM on the ESP for a moment, 25 KB for a small still and about 60 KB for 64x64 with frames. While the ESP is short of that, the animation waits and the status says so; after half a minute it's made at half size if the RAM is enough for that. `Panel Animation Status` says how full the panel is, then what's shown, what's on its way, what waits and what didn't fit, like `4 of 7, 7 of 9 places, 3.1 of 8.8 KB: kite (wandering), ball ×3, weather: Thunderstorm (3), forecast`. The weather's animations are one entry with the show's name, and over Home Assistant's 255 characters the list ends with how many more there are |
| `field.yaml` | `name`, `kind` (`linear`, `radial`, `cone` or `noise`), `w`, `h`, `x`, `y`, `colours`, and for its kind `mirror`, `down`, `apex`, `layers` and `grain`, plus `fade_ms` and `script`. See [fields](#fields). It takes 4 bytes of the panel's 8.75 KB |
| `met.yaml` | `latitude`, `longitude`, `scene` (hills, coast, mountains, city), `aurora` (auto, off, always, see below), `facing` (the compass direction in the middle of the picture for the sun, moon and stars: 0 north, 90 east, 180 south by default, 270 west), `redraw_interval`, `name` (weather). The forecast is asked for again once MET says it has expired. `Panel Weather Scene`, `Panel Weather Aurora` and `Panel Weather Forecast` change it from Home Assistant, `Panel Animation Weather` brings it to life |

`met.yaml` draws the sky for the time of day and the next hour's weather from [MET Norway](https://api.met.no) (CC BY
4.0). The weather right now is also an item, so it can be a dashboard tile, and the cabin's chimney smokes when the
heater is on. The land follows the year: spring comes three days later for each degree north of Oslo, where the grass
is green from mid April and the birches in leaf from early May. MET's forecast doesn't say whether snow lies, so the
ESP keeps count of what falls and melts; after a restart it goes by the temperature and the month. From the first
Sunday in Advent, or 1 December, to 13 January a julenek stands by the cabin, a spruce has its lights after dark, a
string of lights runs along the eaves from dusk to dawn and an arch of advent candles stands in a window. In snow
there's a snow lantern by the door in the evening, and a snowman once enough has fallen. In town they stand in the
park, and the lights are on the old houses. `The forecast hasn't changed (HTTP 304)` in the log is fine, MET has
nothing newer yet. When MET is busy or down (HTTP 502, 503) the picture keeps the last forecast and asks again a
minute later, then less often, up to every half hour.

With `Panel Weather Forecast` on, the weather now and the next hours go over the picture as a still animation, in
front of the weather's own animations, so the letters stay sharp. It takes up to about 4 KB of the panel's 8.75 KB for
animations, and the weather's own make do with the rest.

On a clear dark night the aurora follows the planetary Kp from
[NOAA SWPC](https://www.swpc.noaa.gov/products/planetary-k-index), asked for every 3 hours while `Panel Weather
Aurora` isn't off. On auto it shows once the Kp for those 3 hours reaches what it takes for the auroral oval to reach
over you: about 0 in Tromsø, 2 in Trondheim, 3.3 in Oslo, 4 in Stockholm and 5.5 in Copenhagen. `Panel Weather Kp`
says the Kp now and what your place needs. Its colours are what's usual at the Kp: a pale green arc when it's quiet,
green under a violet top from about 3, a pink lower edge from 5, a red top from 6 and more red at 8. The further the
Kp is over what your place needs the brighter it glows, and in a storm that only just reaches you it's mostly red.
Until NOAA has answered it shows some clear nights north of 55 degrees.

On the coast, with `Panel Weather Forecast` on, MET's wave height from its ocean forecast (Oceanforecast 2.0) is
written right of the lighthouse, just above the sea, in the forecast's lettering. It's only asked for then, after
the forecast, and again once it expires. Nothing is written when it rounds to 0.0 m, as it mostly does in the fjords.
Inland MET has no waves, and outside its area it answers 422; either way nothing is written and the ESP asks again
the next day.

`Panel Animation Weather`, on unless you turn it off, adds slow animations that fit the picture, several at a time,
as many as the panel has room for: 7 animations, 9 copies of them on the screen and its 8.75 KB. They get the room in
this order:

- **The day's own:** Christmas Eve, fireworks on New Year's Eve, Easter, 17 May, the bonfire at Sankthans, Halloween
  and Valentine's Day.
- **The weather:** rain and snow falling from under the clouds (the far rain fainter and slower behind the near, the
  flakes swaying), lightning, and fog creeping along the valley in banks that thicken and thin. Rain and snow are as
  thick as the room left allows.
- **What lives in the picture,** on whenever the picture has it: the lighthouse's beam turning, the sailboat, the
  lights on the Christmas spruce, the flag waving on flag days, the northern lights' ribbon glowing, the stars
  sparkling, smoke from the chimney, firelight in the window, the town antenna's light, and light shimmering on the
  sea, small out at the horizon and wider toward the shore. So the beam goes round in the rain too.
- **The features,** which take turns of 20 to 40 minutes, two at a time when both fit, drawn by lot each turn and
  never the same twice running: gulls or crows, eiders, glitter on the water, a moose at dusk (which mostly gets its
  hour), reindeer, lambs and a wagtail in spring, bullfinches at the julenek through Christmas, butterflies, a skier,
  birch leaves, drifting clouds and the town's windows and streets. The two change at different minutes, so one
  thing changes at a time, and what stays goes on as it was. `Panel Weather Feature` asks for one of them, which is
  then on whenever it fits; `auto` leaves it to the draw. Of the two gulls or crows that fly back and forth, one
  comes down about every five minutes, against the wind: on the cabin's ridge, the flagpole, the lighthouse, a
  spruce's top or a flat roof in town, smaller on its way to one that's far off, and flies up again.
- **Who comes by,** one at a time when the others leave room for one more: a squirrel or a magpie about the cabin,
  a crow or a gull down on one of those perches while none are flying, a fox at night, a sjark, a porpoise or a sea
  eagle off the coast, the coastal express past the lighthouse, a shooting star, and those the features bring. Very
  seldom something stranger.

The aurora is part of the picture and shifts a little every hour. The picture leaves out what the animations draw,
and tells the panel what in it is nearer, so they go behind it where they should: the sailboat behind the point and
the flagpole, a roe deer out of the forest from behind a spruce. Your own animations are always in front of all of
it. The weather takes the panel's animations, so turn your other ones off with it. `Panel Animation Status` names
what's on.

### Space pictures

What NASA and JPL publish, made into pictures on the ESP and kept up to date. Their text goes over them as a still
animation, so it stays sharp when the picture goes as a JPEG; it takes 2 to 4.5 KB of the panel's 8.75 KB for
animations while it's shown. Times are local, 24-hour.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/esp/esp_mars_today.png" width="280" alt="Mars today"><br><sub><code>mars_today.yaml</code></sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/esp/esp_earth_today.png" width="280" alt="Earth today"><br><sub><code>earth_today.yaml</code></sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/esp/esp_probes.png" width="280" alt="Probes"><br><sub><code>probes.yaml</code></sub></td>
</tr>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/esp/esp_perseverance.png" width="280" alt="Perseverance"><br><sub><code>rover.yaml</code>, Perseverance</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/esp/esp_curiosity.png" width="280" alt="Curiosity"><br><sub>and Curiosity</sub></td>
<td></td>
</tr>
</table>

```yaml
      - path: packages/panel/mars_today.yaml       # Perseverance's newest colour photo, named "mars_today"
      - path: packages/panel/earth_today.yaml      # the whole Earth from DSCOVR, named "earth_today"
      - path: packages/panel/rover.yaml            # a rover's track on its crater, named after it
        vars: {rover: perseverance}
      - path: packages/panel/probes.yaml           # where the probes are, named "probes"
```

| Package | Vars and what it does |
|---|---|
| `mars_today.yaml` | `name`, `update_interval` (6h), `resize` (160x120, 320x240 needs PSRAM), `encoding`, `quality`. Reads NASA's list of Perseverance's newest raw images, about 110 KB, as it streams. The newest sol's colour photo looking out nearest the horizon is downloaded when it's shown, its levels stretched as the raw frames are dim, with the sol, the time on Mars and the date over it |
| `earth_today.yaml` | `name`, `update_interval` (3h). Reads EPIC's list of the day's photos, 14 KB, and draws the newest one's thumbnail at the right, with when it was taken, how far away DSCOVR was and what's in the middle of it |
| `rover.yaml` | `rover` (`perseverance` or `curiosity`, also its name, both can be included), `update_interval` (6h), `encoding`, `quality`. Reads the rover's waypoints, about 650 KB, as they stream and keeps a point a pixel. Its track goes on the CTX map of its crater, the last 120 or 300 sols cyan, with where it is and which way it faces, the years, and how far it has driven and climbed. A map takes 38 KB of flash, 16 colours; `scripts/rover_maps.py` makes them |
| `probes.yaml` | `name`, `update_interval` (1d), `encoding` (jpeg, in 16 colours the Sun's shades would take the planets'), `quality`. Asks JPL Horizons where each of 11 probes is, one after the other, about 4.5 KB each, and works the planets out on the ESP. Mercury to Mars are to scale with the probes near them, the outer planets and the far probes are arrows at the edge, and the three farthest are at the left, with how long Voyager 1's signal takes and the day it takes a day |

Their lists are read one at a time, in turn with `met.yaml`'s forecast, the online pictures and the update check,
and they set `CONFIG_MBEDTLS_DYNAMIC_BUFFER`, so TLS only holds its buffers while one is read. All four take about
105 KB of flash with one rover, and no PSRAM. `Panel Mars Today`, `Panel Earth Today`, `Panel Rover <rover>` and
`Panel Probes` say what each read last. When a server is down or busy the picture stays as it was and it's asked
again as `met.yaml` does.

Raw images NASA/JPL-Caltech/ASU, EPIC photos NASA EPIC Team, waypoints NASA/JPL-Caltech, CTX mosaic
NASA/JPL-Caltech/MSSS and the Murray Lab at Caltech, positions NASA/JPL Horizons, planets' orbits E. M. Standish
(JPL).

### Picture and animation actions

Like the [other actions](#messages-views-and-settings), in Home Assistant and as scripts in your YAML.

| Action | Does |
|---|---|
| `panel_show_picture` (`name`), `panel_clear_picture` | The screensaver picture, same as the `Panel Picture` select |
| `panel_show_animation` (`name`), `panel_clear_animation` (`name`, or none for all) | An animation over it, where its yaml puts it, the same as its `Panel Animation <name>` switch. Showing it again puts copy 0 back there if it went. Any combination of them, within the panel's 8.75 KB |
| `panel_place_animation` (`name`, `copy`, `x`, `y`, `own`, `start`), `panel_remove_animation` (`name`, `copy`) | The same animation in more places, which can start off the screen to come in. Copy 0 is the one `panel_show_animation` shows, copy 1 and up are extra. They play in step and cost nothing extra, unless `own: true` gives the copy a frame of its own, which takes another w x h / 2 bytes of the 8.75 KB and starts `start` frames in. Copies with frames of their own when the animation is made make the ESP send its frames as stills, one after the other, when that's smaller, and then they take nothing more. A still ignores `own` |
| `panel_move_animation` (`name`, `copy`, `x`, `y`, `ms`, `ease`), `panel_drift_animation` (`name`, `copy`, `vx`, `vy`) | Glide a copy to `x`, `y` over `ms` (0 jumps, 65535 at most), or drift it at `vx`, `vy` pixels per second (to a hundredth, like 0.55, 327 at most), round the screen or as its area says; 0, 0 stops it. Drifting over a JPEG photo costs more redraws than over a palette picture, so keep it slow there |
| `panel_glide_animation` (`name`, `copy`, `what`, `to`, `ms`, `ease`) | One of a copy's channels to `to` over `ms`: `x`, `y`, `pan_x`, `pan_y`, `w`, `h` (see [views](#views)), `turn`, `phase`, `size`, `size_x`, `size_y`, `opacity`, `speed`, `light`, `level`, `pivot_x`, `pivot_y` or `depth`. A turn goes the short way round, `w` and `h` are 0 to 1024, size 10 to 400, opacity 0 to 100, speed 0 to 1000, light 0 to 200: 100 as it was made, less takes its colours toward black, the weather's toward the night's blue, more toward white. `size_x` and `size_y` (-400 to 400) make it wider or narrower and taller or lower on top of its size, and below 0 mirror it that way, so a glide of `size_x` from 100 to -100 turns it round like a card. `phase` and `level` are a [field's](#fields), `pivot_x` and `pivot_y` its pivot in tenths of a percent (0 to 1000), and `depth` is 0 the farthest to 255 the nearest, where it starts: a copy is drawn in front of those at less depth, and of the smaller ones at its own |
| `panel_tint_animation` (`name`, `copy`, `colour`) | A copy's colours as shades of `colour`, like `FF77BB`: its dark ones from black to it, its light ones from it to white, so one animation can show in many colours. `""` or `none` for its own. Its light goes on top |
| `panel_area_animation` (`name`, `copy`, `left`, `top`, `right`, `bottom`, `edge_x`, `edge_y`) | Where a copy's drift or wander keeps, and what its maps go across. Each edge is `screen` (round the whole screen, the default), `cross` (out at one edge of the area, in at the other) or `bounce` |
| `panel_wander_animation` (`name`, `copy`, `speed_min`, `speed_max`, `heading`, `spread`, `leg_min`, `leg_max`, `rest_min`, `rest_max`) | Legs of `speed_min` to `speed_max` pixels per second (to a tenth) toward `heading` give or take `spread` degrees (`spread` 180 is any way), each `leg_min` to `leg_max` ms (0 0 is one leg), with rests of `rest_min` to `rest_max` ms between (0 0 none). It keeps to its area, and a bounce turns the heading round |
| `panel_face_animation` (`name`, `copy`, `facing`, `mirror`, `tilt`, `rate`, `level`) | Turns a copy toward where it goes. `facing` is the way its frames face, `mirror` flips it to face the other way, `tilt` the most it turns (0 to 180), `rate` degrees per second (0 at once), `level` back upright when it stops. A glide of its turn or a spin ends it |
| `panel_tell_animation` (`name`, `copy`, `statement`) | One statement of a [script](#animation-scripts) told a copy while the script it runs goes on: anything that moves, shows or [binds](#what-a-script-binds) it, like `spin 10`, `flip x`, `blend add`, `pivot 50, 100`, `wave size 90, 110, 800, 800`, `map size 100, 100, 60, 100`, `smooth on` or `opacity *= 60 + sin(ms / 5, 40)` |
| `panel_rounds_animation` (`name`, `copy`, `rounds`) | A copy plays to the end of `rounds` rounds, then fades out and goes. 0 plays on. With `own: true` and `start: 0` it plays from the first frame and holds the last. It's the script `wait rounds N; end`, so it takes the place of one the copy ran |
| `panel_script_animation` (`name`, `copy`, `code`) | A copy runs the [script](#animation-scripts) in `code` on the panel instead of the one it ran, `""` for none. One that plays frames needs `own: true` |
| `panel_frame_rate_animation` (`name`, `percent`) | How fast an animation's frames go, for all its copies. 100 is as made, up to 1000, 0 holds the frame it's on |

For the animation actions:

- The screen is 320 x 240, 0,0 top left, y down. `x`, `y` is the top left of a copy's frame at full size. Sizes and
  turns go round its middle.
- Turns are degrees clockwise from the frames as drawn, -90 is 270. `speed`, `rate`: degrees per second, negative
  anticlockwise.
- `heading`, `facing`: 0 right, 90 down, 180 left, 270 up.
- Times in ms, 0 at once. Speeds in pixels per second.
- `ease`: `none`, `in` (starts slow), `out` (ends slow) or `both`.
- `speed` is how fast its drift and wander go, in percent.
- An area's `right` and `bottom` are just past it, 0, 0, 320, 240 is the whole screen.
- `copy` 0 to 8, copy 0 is the yaml's. The panel shows 9 copies at most.
- A placed copy starts still, upright, full size, on the whole screen. A glide or a drift of `x` or `y` ends a
  wander, and the other way round, and a glide or spin of its turn ends a face. What's bound goes on with them.
- A copy that wanders or faces where it goes takes 28 bytes of the panel's 8.75 KB while it does.
- Spinning, flipping, blending, the pivot, waves and maps have no action of their own: they're statements for
  `panel_tell_animation`, like `spin 10`, `flip x` and `wave size 100, 115, 2000, 2000 slow both`.

Examples, for an ESP called `flexit`:

```yaml
# a sun, turning and breathing
- action: esphome.flexit_panel_tell_animation
  data: {name: sun, copy: 0, statement: "spin 10"}
- action: esphome.flexit_panel_tell_animation
  data: {name: sun, copy: 0, statement: "wave size 100, 115, 2000, 2000 slow both"}
# a flame that flickers
- action: esphome.flexit_panel_tell_animation
  data: {name: flame, copy: 0, statement: "wave opacity 70, 100, 150, 150 jitter 60 random"}
# a boat drawn facing right, gliding left and slowing down at the end, rocking
- action: esphome.flexit_panel_tell_animation
  data: {name: boat, copy: 0, statement: "flip x"}
- action: esphome.flexit_panel_move_animation
  data: {name: boat, copy: 0, x: 10, y: 180, ms: 20000, ease: out}
- action: esphome.flexit_panel_tell_animation
  data: {name: boat, copy: 0, statement: "wave turn -5, 5, 1500, 1500 jitter 20 slow both"}
# a ghost, half see-through, leaning back
- action: esphome.flexit_panel_glide_animation
  data: {name: ghost, copy: 0, what: opacity, to: 50, ms: 1000, ease: both}
- action: esphome.flexit_panel_glide_animation
  data: {name: ghost, copy: 0, what: turn, to: -15, ms: 500, ease: none}
# a second ghost out of step, later removed
- action: esphome.flexit_panel_place_animation
  data: {name: ghost, copy: 1, x: 40, y: 60, own: true, start: 3}
- action: esphome.flexit_panel_remove_animation
  data: {name: ghost, copy: 1}
# a bird crossing the sky to the right
- action: esphome.flexit_panel_area_animation
  data: {name: bird, copy: 0, left: 0, top: 10, right: 320, bottom: 90, edge_x: cross, edge_y: bounce}
- action: esphome.flexit_panel_wander_animation
  data: {name: bird, copy: 0, speed_min: 15, speed_max: 30, heading: 0, spread: 20, leg_min: 1500, leg_max: 4000,
         rest_min: 0, rest_max: 0}
- action: esphome.flexit_panel_face_animation
  data: {name: bird, copy: 0, facing: 0, mirror: true, tilt: 15, rate: 30, level: true}
# a fish in a tank, smaller, fainter and slower further back
- action: esphome.flexit_panel_area_animation
  data: {name: fish, copy: 0, left: 20, top: 120, right: 300, bottom: 220, edge_x: bounce, edge_y: bounce}
- action: esphome.flexit_panel_wander_animation
  data: {name: fish, copy: 0, speed_min: 5, speed_max: 25, heading: 0, spread: 30, leg_min: 1000, leg_max: 5000,
         rest_min: 500, rest_max: 3000}
- action: esphome.flexit_panel_tell_animation
  data: {name: fish, copy: 0, statement: "map size 100, 100, 60, 100"}
- action: esphome.flexit_panel_tell_animation
  data: {name: fish, copy: 0, statement: "map opacity 100, 100, 70, 100"}
- action: esphome.flexit_panel_tell_animation
  data: {name: fish, copy: 0, statement: "map speed 100, 100, 60, 100"}
# a ball bouncing on the floor, slow at the top like it should be
- action: esphome.flexit_panel_tell_animation
  data: {name: ball, copy: 0, statement: "wave y -80, 0, 600, 600 slow low"}
# a bouncing logo
- action: esphome.flexit_panel_area_animation
  data: {name: logo, copy: 0, left: 0, top: 0, right: 320, bottom: 240, edge_x: bounce, edge_y: bounce}
- action: esphome.flexit_panel_drift_animation
  data: {name: logo, copy: 0, vx: 40, vy: 30}
# a shooting star, once
- action: esphome.flexit_panel_place_animation
  data: {name: star, copy: 1, x: 200, y: 30, own: true, start: 0}
- action: esphome.flexit_panel_rounds_animation
  data: {name: star, copy: 1, rounds: 1}
# a rocket drawn pointing up, pointing where it goes
- action: esphome.flexit_panel_wander_animation
  data: {name: rocket, copy: 0, speed_min: 20, speed_max: 40, heading: 0, spread: 180, leg_min: 1000, leg_max: 3000,
         rest_min: 0, rest_max: 0}
- action: esphome.flexit_panel_face_animation
  data: {name: rocket, copy: 0, facing: 270, mirror: false, tilt: 180, rate: 90, level: false}
# a fan GIF that spins with the real fans, from an automation when the speed changes
- action: esphome.flexit_panel_frame_rate_animation
  data: {name: fan, percent: "{{ states('sensor.flexit_supply_fan_speed_percentage') | int(0) * 2 }}"}
```

### Animation scripts

A copy can run a script on the panel: which of its frames play, where it goes, and what it does after that. The
panel runs it on its own, so it goes on with nothing from the ESP, and a panel restart starts it again. Copy 0 of an
`animation.yaml` runs its `script`, and `clips` names parts of its frames for it. `panel_script_animation` gives any
copy one.

```yaml
      - path: packages/panel/animation.yaml
        vars:
          name: kite
          file: animations/kite.gif   # facing left, frames 0-3 climbing, 4-7 diving
          y: 40
          clips: "climb 0-3, dive 4-7"
          script: |
            left:
              clip climb
              move 20, rand(30, 60) over 6000 ease both
              wait 6000
              clip dive at end        # once the climb's round is over
              wait rounds 1
              flip x
              clip climb
              move 250, rand(30, 60) over 6000 ease both
              wait 6000
              flip none
              goto left
```

```yaml
# a second ghost that follows the first, a little behind
- action: esphome.flexit_panel_place_animation
  data: {name: ghost, copy: 1, x: 0, y: 0, own: false, start: 0}
- action: esphome.flexit_panel_script_animation
  data:
    name: ghost
    copy: 1
    code: |
      on:
        move x(ghost) - 30, y(ghost) + 10 over 400
        wait 500
        goto on
```

A line is one statement, or several with `;` between them, and `#` starts a comment.

| Statement | Does |
|---|---|
| `name:` | A place in the script to go to |
| `v = expr`, `v += expr` | A number of its own, 0 to start with, 8 at most. Also `-=` and `*=` |
| `shared a, b` | Numbers all the copies' scripts share, 4 at most, the same ones in the order they're named. 0 again once all the animations are cleared, and a copy that's fading out to go no longer sets them |
| `goto name`, `if expr goto name`, `stop` | Go on there, if it holds, or stop |
| `wait ms`, `wait lo to hi` | Wait that long, or a random time between |
| `wait until expr` | Wait until it holds. It's checked every 50 ms, so it can follow where things are |
| `wait rounds n` | Wait until n more rounds of its clip have ended, of its frames when it has no frame of its own |
| `clip name`, `clip a to b`, `clip a` | Play those frames round and round from the first, now, or with `at end` once a round of the clip it plays is over |
| `frame n`, `rate percent` | Jump to that frame, the clip going on from there. How fast its frames go, 0 holds the one it's on |
| `move x, y`, `glide what to` | Go there, or its channels to those, as many as you like at once like `glide size_x 50 opacity 80`, `over ms` and with `ease none`, `in`, `out` or `both` if you like |
| `drift vx, vy`, `spin speed` | As `panel_drift_animation`, and spinning at `speed` degrees per second, negative the other way, 0 stops it where it is |
| `scroll vx, vy`, `grow vw, vh`, `cycle n` | Its frame moving through its box, or the box growing, pixels per second, negative the other way. A frame that repeats goes round and round for good, one that doesn't is soon out of its box. Its `phase` going on, colours per second |
| `x += expr`, `size *= expr`, `turn := expr` | [Binds](#what-a-script-binds) a channel: the panel works the expression out again each step from now on. `free x` lets it go |
| `flip none`, `flip x`, `flip y`, `flip both` | Mirror it left to right, upside down, both or neither. Quarter turns and flips stay sharp, other angles look rougher |
| `pivot x, y` | The point it turns, sizes and flips round, percent of its box from the top left (to a tenth): 50, 50 its middle, where it starts, 50, 100 the middle of its bottom, 0, 0 its top left corner. It's also where the copy is for `at_x`, `at_y`, `dist` and `angle`. A bigger box puts it outside the frame |
| `blend mix`, `blend add`, `blend screen`, `blend multiply` | What its colours do to what's behind them: `mix` covers it (where it starts), `add` adds to it as light does, `screen` lightens it as light through haze does, never past white, and `multiply` darkens it like a shadow or tinted glass. Each colour does it at its own opacity, times the copy's |
| `smooth on`, `smooth off` | Each pixel from the four of its frame round where it falls, so it moves and sizes by less than a pixel and turns without steps, in quarters of a pixel. For what creeps, like a cloud: a pixel drawing gets soft edges while it's between two pixels |
| `repeat none`, `repeat x`, `repeat y`, `repeat both` | Its frame once in its box, or round and round across it, down it or both, with `mirror` after it every other one mirrored |
| `shape none`, `shape w, top, bottom, …` | The box shows only these runs of columns from its left: each run `w` wide, from row `top` to just above `bottom`. Up to 32 runs, whole numbers |
| `soft left, right, top, bottom`, `soft none` | It fades in over that many pixels from its box's left and right edge, and from the top and bottom of each of its shape's runs (of its box without one), like rain falling out of the clouds. 0 to 1024 each, whole numbers. Set it after the statements a script starts with, since it's a step that runs, like `shape` |
| `tell copy statement` | One of the statements here that moves, shows or binds a copy, done to another one: `tell kite.2 glide light 50` |
| `tint 0xFF77BB`, `tint none` | As `panel_tint_animation` |
| `depth n` | Drawn in front of the copies at less depth, 0 the farthest and 255 the nearest, where they start |
| `area l, t, r, b edge_x edge_y` | As `panel_area_animation`, the edges `screen`, `cross` or `bounce` (one for both, or none for `screen`) |
| `wander speed_min, speed_max, heading, spread, leg_min, leg_max, rest_min, rest_max` | As `panel_wander_animation` |
| `face facing` | As `panel_face_animation`, followed by those you want of `mirror`, `level`, `tilt t` and `rate r`, in that order |
| `wave what low, high, rise, fall` | The panel takes `what` between `low` and `high` on its own, for good, `rise` and `fall` the ms for the whole way up and down. Followed by those you want of `hold high, low`, how long it stays at each end, `jitter j`, how many percent the times vary, each copy's in its own way, `random`, to anywhere between low and high each time, and `slow s`, where it eases: `low`, `high`, `both` or `none`, in that order. On the channels up to `phase` it's an offset in pixels or degrees, on the shares percent of what a glide set, so a fade still fades it. Low = high stays there. It starts when it's said, and it's a [bind](#what-a-script-binds) of that channel, so it takes the place of one the copy had there, and `free what` ends it |
| `map what left, right, top, bottom` | Any of its channels but `x` and `y` follows where its pivot (its middle unless it has one) is in its area: the value at the left and right edge, and at the top and bottom, in between it goes evenly. The channels up to `phase` add the two up, the shares are percent and multiply. With size it's depth: smaller further up, and the bigger copies are drawn in front. A bind like a wave: a channel has the one or the other |
| `mark n` | Tells the ESP, which logs `Animation kite: copy 0 at mark 3` (n 0 to 255) |
| `end`, `end fade ms` | Fades out, over its fade or ms, and goes. With a frame of its own it holds the one it's on |

Numbers are whole. An expression is numbers, names and these, with brackets: `+`, `-`, `*`, `/`, `%`, the six that
compare (`<`, `<=`, `==`, `!=`, `>=`, `>`, 1 if it holds and 0 if not), and `and`, `or` and `not`. Division drops the
fraction and `/ 0` is 0, and `a * b / c` is worked out whole, so `a * b` can be bigger than a number holds. Brackets
and functions go 6 inside each other, and 12 of what's worked out on top of each other, as in a sum of 12: past that,
work a part of it out into a number of its own first. Speeds can have fractions: drifts to a hundredth, like
`drift -0.55, 0`, spins and wanders to a tenth. These read a copy, its own when bare, or another one in brackets:
`x(kite)` is copy 0 of kite, `x(kite.2)` its copy 2.

| Read | Is |
|---|---|
| `x`, `y`, `w`, `h`, `pan_x`, `pan_y` | Its box's top left and size, and where its frame is in it, in pixels |
| `turn`, `size`, `size_x`, `size_y`, `opacity`, `speed`, `light`, `level` | As it's shown, degrees 0 to 359 and percent |
| `phase`, `pivot_x`, `pivot_y`, `depth` | Colours on, tenths of a percent of its box, and 0 to 255 |
| `frames`, `frame_w`, `frame_h` | How many frames its animation has, and their size in pixels |
| `frame`, `rounds`, `ms` | The frame it's on, rounds of its clip so far, ms since it was placed, in the panel's steps of 50 |
| `at_x`, `at_y` | Where its pivot is across its area and down it, 0 to 1000 |
| `seed` | A number of its own from 0 to 9999, another each time it's placed |
| `here` | 1 if the copy is shown, 0 if not |
| `row` | In what's bound to its rows, the row of its box |

| Function | Is |
|---|---|
| `rand(a, b)` | A whole number from a to b, another each time |
| `hash(k, n)` | 0 to n - 1 at random, but the same one for the same k, on this copy |
| `noise(t, period, n)` | 0 to n, drifting smoothly on to another at random each `period` of t: `noise(ms, 3000, 100)` |
| `wave(t, low, high, rise, fall)` | Between low and high as t goes by, like the `wave` statement. Then, if you like, `hold_high`, `hold_low`, `jitter` and how: `slow_low`, `slow_high`, `slow_both`, `+ random` |
| `sin(deg, n)`, `cos(deg, n)` | n times the sine or cosine of deg |
| `atan(x, y)`, `hypot(x, y)` | The way x right and y down points, in degrees, and how far it is |
| `dist(a, b)`, `angle(a, b)` | From the pivot of copy a to that of b, in pixels, and the way there in degrees, 0 right, 90 down. `self` is its own |
| `abs(v)`, `min(a, b)`, `max(a, b)`, `clamp(v, low, high)`, `wrap(v, n)` | `wrap` is v round 0 to n - 1, also below 0 |
| `if(c, a, b)` | a if c holds, else b |
| `lerp(a, b, v, n)`, `scale(a, b, c)` | From a to b as v goes from 0 to n, and a * b / c |
| `ease(v, n, how)` | v of 0 to n eased: `ease_in`, `ease_out` or `ease_both` |
| `pick(k, a, b, …)` | Number k of the list, from 0, the first below that and the last past it. Up to 255 whole numbers, -32767 to 32767, like rows along a hill for a walk |
| `curve(v, n, a, b, …)` | Along the list as v goes from 0 to n, between two of its numbers: a path through points |

A script is 480 bytes at most, made from the text on the ESP, and takes 24 more of the panel's 8.75 KB while it runs,
and 4 for each number of its own. A `move` takes about 10 bytes, a `wait` 4. What it sets with plain numbers at its
start, before its first label and anything else, is told to the panel apart, so `area`, `wander`, `face` and the like
there cost nothing. A mistake is logged with its line, like `Animation kite: line 3: what's 'mvoe'?`, and the copy
keeps the script it had, if any. A script that plays frames (`clip`, `frame` or `rate`) needs a copy with a frame of
its own: copy 0 of an `animation.yaml` with a script like that gets one.

### What a script binds

A script doesn't have to move its copy a step at a time. It can say what a channel is, and the panel works that out
again every 50 ms from then on, with nothing more from the script or the ESP:

```yaml
          script: |
            y += sin(ms / 10, 4)               # bobbing: 4 px up and down, round in 3.6 s
            opacity *= 70 + noise(ms, 800, 30)  # flickering between 70 and 100 %
            turn := angle(self, kite)           # pointing at the kite, wherever it goes
```

| Bind | Does |
|---|---|
| `x += expr` | Adds it to where glides and drifts have the channel. `-=` takes it off |
| `size *= expr` | That many percent of it |
| `turn := expr` | It is that, whatever glides and drifts say |
| `a := expr` | A number of the script's, worked out before the channels each step, for what several of them go by |
| `frame := expr` | The frame it shows, its own frames held: `frame := x / 4` walks in step with how far it's gone |
| `row.shift := expr` and so on | Each row of its box, see [views](#views) |
| `free x`, `free a, frame` | Lets them go, and the channel is where its glides have it |

A channel takes one bind of each kind, a new one in place of the one it had, and the three go together: what it's
set to, times what it's multiplied by, plus what's added. What's bound is worked out finer than whole numbers where
it can be: `y += sin(ms / 10, 4)` moves in thousandths of a pixel, which shows with `smooth on`. The panel binds 40
at a time, of all copies together. A script's binds go when the script does, and those told by an action or at a
script's start, with plain numbers and `ms`, stay until the copy goes or they're freed. A copy with something bound
is worked out every step, also when nothing else moves.

### Views

A copy shows its animation's frame in a box, the frame's size to start with. `w` and `h` make the box any size,
smaller to show part of the frame, bigger to leave room round it, and `pan_x` and `pan_y` move the frame inside it.
With `repeat` the frame goes round and round across the box, so a small one fills a big box and `scroll` makes it flow
through it without end, like rain or a river, `mirror` mirroring every other one so the repeats show less. A `shape`
cuts the box down to runs of columns, each with its own top and bottom, like rain from under clouds at different
heights. `soft` fades its edges in, and `blend` makes it light up what's behind it, haze it or shade it rather than
cover it. The box glides, drifts and binds like the rest, and turns, sizes and flips with the copy round its pivot.
None of it takes any of the 8.75 KB but a shape's and soft edges' bytes in its script.

With [binds](#what-a-script-binds) a copy can be anything that's worked out from the time and where things are. The
lighthouse's beam is: it turns round its lamp in 3D, the panel working out each step how long it looks from the
side, mirroring it as it points the other way, and how bright it is, and the lamp's glow flares by the same number.

```yaml
          script: |
            pivot 0, 50                  # the lamp, at the middle of its left edge
            shared lit                   # the way it points, for the lamp's script too
            lit := ms / 25 % 360         # 0 facing you, 90 to the right, round in 9 s
            size_x := sin(lit, 100)
            opacity := 60 + cos(lit, 40)
```

What's bound to its rows makes each row of its box its own, where the copy is upright: `row.shift` moves the frame
that many pixels to the right on that row (whole ones, quarters with `smooth on`, 1024 either way at most),
`row.size` makes it that many percent as wide round the pivot (10 to 400), `row.opacity` and `row.light` are percent
of the copy's, and `row.phase` colours on. `row` is the row. It's drawn again only when that changes, and a frame
that repeats fills the row at any size, so rows that get wider down the box are water or a road seen from above it.

```yaml
          script: |
            row.shift := sin(row * 40 + ms / 3, 2)   # a reflection rippling on the water
            row.opacity := 100 - row * 2             # fainter further down
```

```yaml
          script: |
            glide w 320; glide h 120    # a box across the screen
            repeat both mirror
            scroll 0, 60                # the frame falling through it at 60 pixels a second
            blend screen                # lighter where it falls over something dark
            soft 0, 0, 10, 0            # fading in under where it falls from
            shape 100, 20, 120, 220, 0, 120
```

### Fields

A field is light, haze or shade that the panel works out itself instead of playing frames: for each pixel of a
copy's box how much of it there is, drawn that far along 16 colours. It's soft at any size and turn, has no steps in
it, and takes 4 bytes of the panel's 8.75 KB however big it is. Sized, turned, blended, bound and scripted like any
copy.

```yaml
      - path: packages/panel/field.yaml            # a lamp's glow, added to what's behind it, breathing
        vars:
          name: glow
          kind: radial
          w: 60
          h: 60
          x: 200
          y: 20
          colours: "00FFF4D8, FFFFF4D8"
          script: |
            blend add
            size *= 90 + sin(ms / 8, 10)
```

| `kind` | Is | Takes |
|---|---|---|
| `linear` | From none at its left to all of it at its right | `mirror: true` up to the middle and back down, `down: true` from its top to its bottom |
| `radial` | All of it in the middle, none at the ellipse its box holds | |
| `cone` | A wedge from the middle of its left side to its whole right side, most along its middle | `apex`, how wide it is at the left, percent of its height |
| `noise` | Cloudy, here more and there less, another for each copy | `layers` 1 to 3 of detail, `grain` the pixels between its bumps, 4 to 1024. `scroll` moves it, for as long as you like: it's the same again 64 bumps on |

`colours` is what it's drawn in, from where there's none of it to where there's most: 2 to 16 colours in hex,
`RRGGBB` or `AARRGGBB` with how opaque first, spread evenly with those between worked out. `"00FFFFFF, FFFFFFFF"` is
white fading in, `"FF000000, FFFF0000, FFFFFF00, FFFFFFFF"` a fire's black through red and yellow to white, and
opaque colours with a `linear` one are a gradient, or with one colour a plain rectangle to shape.

Two channels are a field's own. `level` is how much of it there is, percent: 200 fills a glow's middle half, 50
makes a haze half as thick. `phase` moves it on along its colours, in colours, round from the last to the first
again: with colours that end as they start, a `radial` one sends rings out and a `noise` one churns. `cycle 4` takes
it on 4 colours a second.

A drawn animation has a `phase` too when the code that draws it on the ESP names some of its colours as cycling
(`Source::cycles`): those go round, each on to the next, the old trick for water that runs and lights that chase.

## The panel's screen in Home Assistant

`panel/screen_camera.yaml` adds `Panel Screen`, a camera of whatever the panel shows: the home page, a menu, the
screensaver. ESP32 only, and it needs panel firmware that takes screenshots. It's a diagnostic entity, so Home
Assistant's own dashboards leave it out: one left open fetches an image every 10 s, and the panel keeps taking
them. It's on the device's page, or add it to a dashboard of your own.

```yaml
      - path: packages/panel/screen_camera.yaml
        vars: {quality: "90"}  # JPEG quality, 1-100: higher is sharper text and slower, best kept at 95 or under
                               # category: "" for an ordinary entity, on the dashboards
```

The panel doesn't keep a copy of its screen. While someone looks, it draws its screen again in 64x8 tiles and
sends each one as a bit of JPEG as it goes to the display, after the first time only the tiles that changed. The
ESP puts them together. The first look shows what came within 4 s, black where nothing has yet, and the live view
fills in over about 10 s. After that an image takes a second or two, the clock ticking over is 4 tiles. The tiles
take about 25 KB of the ESP's RAM, let go a minute after the last look; without the RAM it doesn't ask. While the
ESP sends the panel a picture or an animation, the screen waits. A tile has to fit in about 700 bytes on the panel,
so above quality 95 the busiest tiles lose their faint detail.

## Flashing the panel

`panel/update.yaml` flashes the panel's firmware from Home Assistant: press `Flash Panel Firmware` and watch
`Panel Firmware Update`. A file the ESP refuses, like the version the panel already has, goes on anyway with
`Panel Firmware Force` on. It's off again after a restart. The steps, and what to do when it doesn't work, are in
the firmware's [flashing docs](https://github.com/MSkjel/flexit-ci600-firmware/blob/main/docs/flashing.md#from-home-assistant).

`Panel Firmware` is the same thing as an update in Home Assistant. Every 6 h (`check_interval`, `never` for only
when you ask it to look) the ESP reads the
version from the start of the file at `Panel Firmware URL`, and when that's newer than the panel it shows up under
Settings > Updates, where Install does what the button does. It compares version numbers only, and a panel that
doesn't report one, like a stock panel or one with another link version, shows as unknown. The button works for those.

The file doesn't say which link version it has. If a flash leaves the panel on another one than the ESP,
`Panel Firmware Update` says which of the two to update, and so does `Panel Firmware`'s summary.

## Icons

The ids in [`code/panel/plink_icons.h`](../packages/code/panel/plink_icons.h), in lower case: fan, thermo, fire,
snow, filter, humidity, co2, home, bell, alert, clock, sun, night, leaf, fireplace, water, shower, window, bulb,
plug, door, battery, solar, lock, motion, washer, vacuum, tv, cash and about 80 more.

How the panel and the ESP share the bus is in the
[firmware's README](https://github.com/MSkjel/flexit-ci600-firmware#the-esp-link). The C files in
[`packages/code/panel/`](../packages/code/panel) are copies of its `proto/`. The `.cpp` files there are plain C++
without ESPHome entities, like the colours, the JPEG's quality, the pieces a picture goes in and the flashing,
which ESPHome builds on their own, so a change to your YAML doesn't build them again.
