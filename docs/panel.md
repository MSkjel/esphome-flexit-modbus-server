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
the ESP goes to address 1. If your panel is at another address, set `panel_address` to match, like
`- path: packages/panel.yaml` with `vars: {panel_address: "2"}`. The ESP only talks to that panel. A second panel
with this firmware works fine without the ESP, just switch ESP off under System > Equipment on that one.

```yaml
substitutions:
  flexit_address: "1"
  flexit_ref: panel
  flexit_refresh: 1d
  modbus_library_ref: dev

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

The panel packages are on the `panel` branch, and they need the Modbus library's `dev` branch, which switches the
RS-485 direction pin in hardware. The panel's bootloader needs that. If the ESP was built with the library's
`main` before, do a Clean Build Files, or ESPHome keeps the old library.

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
| `panel/camera.yaml` | The screensaver picture as a camera in Home Assistant |
| `panel/update.yaml` | Flashes the panel's firmware from Home Assistant |

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

The pictures and animations have [their own actions](#picture-and-animation-actions).

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
      - path: packages/panel/met.yaml              # the weather where you are, named "weather"
        vars: {latitude: "59.9139", longitude: "10.7522", scene: coast}
      - packages/panel/camera.yaml                 # the picture as a camera in Home Assistant
```

`camera.yaml` gives Home Assistant a `Panel Picture` camera with the picture the panel shows, without the clock
and the animations the panel draws over it, black with none. It's made when Home Assistant asks for it: a JPEG
picture goes as it is, a 16-colour one like the weather is encoded first. That takes RAM only while someone looks,
up to 24 KB, freed again 30 s after, and waits while a picture is being made or sent. ESPHome has one camera per ESP.

A picture is fitted to the screen and converted on the ESP to 16 colours, or to a JPEG when it has more, and sent
in the gaps on the bus: a 320x240 drawing takes a few seconds, a photo half a minute. Only the rows that changed go
when a picture is updated. `Panel Picture Status` and `Panel Animation Status` show how it's going. Names are
letters, digits and `_`.

| Package | Vars |
|---|---|
| `picture.yaml` | `name`, `file` (PNG or JPEG next to your config). `encoding`: `auto`, `palette` (16 colours) or `jpeg`; a palette picture that doesn't fit the panel's 14 KB goes as a JPEG anyway. `quality`: the JPEG quality, or `auto` for the best that fits |
| `online_picture.yaml` | `name`, `url`, `update_interval` (like `10min`, `1.5h` or `1d`, `never` = only when shown), `format` (PNG, JPEG, BMP or AUTO), `resize` (160x120 default, blown up to the screen; 320x240 needs PSRAM), plus `encoding` and `quality`. A failed download is retried after 30 s, then twice as long each time. Before each download the ESP asks the server about the file and skips one it doesn't have the RAM for, with the reason in `Panel Picture Status`. A JPEG has to fit in RAM whole, so the server has to send its size; PNG and BMP are read a bit at a time. For a Home Assistant camera use `ha_picture.yaml` |
| `ha_picture.yaml` | `name`, `entity_id` (a `camera.` or `image.` entity, with `kind: image` for the latter), `ha_url` (`http://homeassistant.local:8123`), `width` (320, HA scales the camera picture down to it when the camera can), plus `update_interval`, `resize`, `format`, `encoding` and `quality` as above. HA only hands out camera pictures with a token that changes now and then; the ESP follows the entity's `access_token` attribute, so nothing to set up. If the camera won't scale, the full picture is often too big for an ESP without PSRAM and is skipped; a Frigate camera's own `latest.jpg?h=120` through `online_picture.yaml` is the way round that |
| `animation.yaml` | `name`, `file` (a GIF), `resize` (64x64 default), `x`, `y` (top left on the 320x240 screen), `scale` (1, or 2 to draw each pixel 2x), `frame_ms` (40 to 2550, the GIF's own timing isn't used), `drift_x`, `drift_y` (pixels per second it drifts, wrapping round the screen seamlessly), `fade_ms` (300, how long copies fade in and out). The panel plays all its animations from 6 KB, up to 4 at a time in up to 8 places, so keep them small: about 64x64 with a few frames, or more frames if little changes between them. Too big at scale 1 and it's made at half size and drawn 2x. `Panel Animation Status` says what's shown and what didn't fit |
| `met.yaml` | `latitude`, `longitude`, `scene` (hills, coast, mountains, city), `aurora` (auto, off, always), `update_interval` (5 min, the forecast is only fetched again when MET says it expired), `redraw_interval`, `name` (weather). `Panel Weather Scene`, `Panel Weather Aurora` and `Panel Weather Forecast` change it from Home Assistant, `Panel Animation Weather` brings it to life |

`met.yaml` draws the sky for the time of day and the next hour's weather from [MET Norway](https://api.met.no)
(CC BY 4.0). The weather right now is also an item, so it can be a dashboard tile, and the cabin's chimney smokes
when the heater is on. A failed request with `Code: 304` in the log is fine, the forecast just hasn't changed.

`Panel Animation Weather`, on unless you turn it off, adds slow animations that fit the picture, one show at a
time: gulls over the cabin, rain and snow falling from under the clouds, the aurora glowing, a moose at dusk, the
sailboat, the town's windows, the lights on the Christmas spruce, fireworks on New Year's Eve, the flag waving on
flag days and more, some of them by chance now and then. Very seldom something stranger comes by. The picture
leaves out what they draw. They take the panel's animations, so turn your other ones off with it.

### Picture and animation actions

Like the [other actions](#messages-views-and-settings), in Home Assistant and as scripts in your YAML.

| Action | Does |
|---|---|
| `panel_show_picture` (`name`), `panel_clear_picture` | The screensaver picture, same as the `Panel Picture` select |
| `panel_show_animation` (`name`), `panel_clear_animation` (`name`, or none for all) | An animation over it, where its yaml puts it, the same as its `Panel Animation <name>` switch. Showing it again puts copy 0 back there if it went. Any combination of them, within the panel's 6 KB |
| `panel_place_animation` (`name`, `copy`, `x`, `y`, `own`, `start`), `panel_remove_animation` (`name`, `copy`) | The same animation in more places, which can start off the screen to come in. Copy 0 is the one `panel_show_animation` shows, copy 1 and up are extra. They play in step and cost nothing extra, unless `own: true` gives the copy a frame of its own, which takes another w x h / 2 bytes of the 6 KB and starts `start` frames in |
| `panel_move_animation` (`name`, `copy`, `x`, `y`, `ms`, `ease`), `panel_drift_animation` (`name`, `copy`, `vx`, `vy`) | Glide a copy to `x`, `y` over `ms` (0 jumps, 65535 at most), or drift it at `vx`, `vy` pixels per second (to a tenth, like 0.5), round the screen or as its area says; 0, 0 stops it. Drifting over a JPEG photo costs more redraws than over a palette picture, so keep it slow there |
| `panel_glide_animation` (`name`, `copy`, `what`, `to`, `ms`, `ease`) | One of a copy's `x`, `y`, `turn`, `size`, `opacity` or `speed` to `to` over `ms`. A turn goes the short way round, size is 10 to 400, opacity 0 to 100, speed 0 to 1000 |
| `panel_spin_animation` (`name`, `copy`, `speed`), `panel_flip_animation` (`name`, `copy`, `horizontal`, `vertical`) | Spin a copy at `speed` degrees per second, negative the other way, 0 stops it where it is. Flip mirrors it left to right, upside down or both. Quarter turns and flips stay sharp, other angles look rougher |
| `panel_area_animation` (`name`, `copy`, `left`, `top`, `right`, `bottom`, `edge_x`, `edge_y`) | Where a copy's drift or wander keeps, and what its maps go across. Each edge is `screen` (round the whole screen, the default), `cross` (out at one edge of the area, in at the other) or `bounce` |
| `panel_wander_animation` (`name`, `copy`, `speed_min`, `speed_max`, `heading`, `spread`, `leg_min`, `leg_max`, `rest_min`, `rest_max`) | Legs of `speed_min` to `speed_max` pixels per second (to a tenth) toward `heading` give or take `spread` degrees (`spread` 180 is any way), each `leg_min` to `leg_max` ms (0 0 is one leg), with rests of `rest_min` to `rest_max` ms between (0 0 none). It keeps to its area, and a bounce turns the heading round |
| `panel_face_animation` (`name`, `copy`, `facing`, `mirror`, `tilt`, `rate`, `level`) | Turns a copy toward where it goes. `facing` is the way its frames face, `mirror` flips it to face the other way, `tilt` the most it turns (0 to 180), `rate` degrees per second (0 at once), `level` back upright when it stops. A glide of its turn or a spin ends it |
| `panel_wave_animation` (`name`, `copy`, `what`, `low`, `high`, `rise`, `fall`, `hold_high`, `hold_low`, `jitter`, `random`, `slow`) | The panel takes `what` between `low` and `high` on its own, for good. `rise` and `fall` are the ms for the whole way up and down, `hold_high` and `hold_low` how long it stays at each end, `jitter` how many percent the times vary. `random` goes to anywhere between low and high each time. `slow` is where it eases: `low`, `high`, `both` or `none`. On `x`, `y` and `turn` it's an offset in pixels or degrees, on the rest percent of what a glide set, so a fade still fades it. Low = high goes there and stays, and at 0 (or 100) the wave is gone. The panel runs 12 waves at a time |
| `panel_map_animation` (`name`, `copy`, `what`, `left`, `right`, `top`, `bottom`) | `turn`, `size`, `opacity` or `speed` follows where the copy's middle is in its area: the value at the left and right edge, and at the top and bottom, in between it goes evenly. Turn adds the two up, the others are percent and multiply. With size it's depth: smaller further up, and the copies bigger from it are drawn in front |
| `panel_rounds_animation` (`name`, `copy`, `rounds`) | A copy plays to the end of `rounds` rounds, then fades out and goes. 0 plays on. With `own: true` and `start: 0` it plays from the first frame and holds the last |
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
- `copy` 0 to 7, copy 0 is the yaml's. The panel shows 8 copies at most.
- A placed copy starts still, upright, full size, on the whole screen. A glide or a drift of `x` or `y` ends a
  wander, and the other way round, and a glide or spin of its turn ends a face. Waves and maps go on with them.

Examples, for an ESP called `flexit`:

```yaml
# a sun, turning and breathing
- action: esphome.flexit_panel_spin_animation
  data: {name: sun, copy: 0, speed: 10}
- action: esphome.flexit_panel_wave_animation
  data: {name: sun, copy: 0, what: size, low: 100, high: 115, rise: 2000, fall: 2000, hold_high: 0, hold_low: 0,
         jitter: 0, random: false, slow: both}
# a flame that flickers
- action: esphome.flexit_panel_wave_animation
  data: {name: flame, copy: 0, what: opacity, low: 70, high: 100, rise: 150, fall: 150, hold_high: 0, hold_low: 0,
         jitter: 60, random: true, slow: none}
# a boat drawn facing right, gliding left and slowing down at the end, rocking
- action: esphome.flexit_panel_flip_animation
  data: {name: boat, copy: 0, horizontal: true, vertical: false}
- action: esphome.flexit_panel_move_animation
  data: {name: boat, copy: 0, x: 10, y: 180, ms: 20000, ease: out}
- action: esphome.flexit_panel_wave_animation
  data: {name: boat, copy: 0, what: turn, low: -5, high: 5, rise: 1500, fall: 1500, hold_high: 0, hold_low: 0,
         jitter: 20, random: false, slow: both}
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
- action: esphome.flexit_panel_map_animation
  data: {name: fish, copy: 0, what: size, left: 100, right: 100, top: 60, bottom: 100}
- action: esphome.flexit_panel_map_animation
  data: {name: fish, copy: 0, what: opacity, left: 100, right: 100, top: 70, bottom: 100}
- action: esphome.flexit_panel_map_animation
  data: {name: fish, copy: 0, what: speed, left: 100, right: 100, top: 60, bottom: 100}
# a ball bouncing on the floor, slow at the top like it should be
- action: esphome.flexit_panel_wave_animation
  data: {name: ball, copy: 0, what: y, low: -80, high: 0, rise: 600, fall: 600, hold_high: 0, hold_low: 0,
         jitter: 0, random: false, slow: low}
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

## Flashing the panel

`panel/update.yaml` flashes the panel's firmware from Home Assistant: press `Flash Panel Firmware` and watch
`Panel Firmware Update`. The steps, and what to do when it doesn't work, are in the firmware's
[flashing docs](https://github.com/MSkjel/flexit-ci600-firmware/blob/main/docs/flashing.md#from-home-assistant).

## Icons

The ids in [`code/panel/plink_icons.h`](../packages/code/panel/plink_icons.h), in lower case: fan, thermo, fire,
snow, filter, humidity, co2, home, bell, alert, clock, sun, night, leaf, fireplace, water, shower, window, bulb,
plug, door, battery, solar, lock, motion, washer, vacuum, tv, cash and about 80 more.

How the panel and the ESP share the bus is in the
[firmware's README](https://github.com/MSkjel/flexit-ci600-firmware#the-esp-link). The C files in
[`packages/code/panel/`](../packages/code/panel) are copies of its `proto/`. The `.cpp` files there are plain C++
without ESPHome entities, like the colours, the JPEG's quality, the pieces a picture goes in and the flashing,
which ESPHome builds on their own, so a change to your YAML doesn't build them again.
