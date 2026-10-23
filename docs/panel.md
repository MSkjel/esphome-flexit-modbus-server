# The CI600 panel

With the [replacement CI600 firmware](https://github.com/MSkjel/flexit-ci600-firmware), the panel and this ESP
share the bus. The panel shows what the ESP is doing, Home Assistant can put its own things on the panel's screen
and control the panel, and the panel's firmware is flashed from Home Assistant. It's all YAML packages, ESP32 only.

- [Setup](#setup)
- [Your own things on the panel](#your-own-things-on-the-panel)
- [The comfort packages](#the-comfort-packages)
- [Pictures, animations and the weather](#pictures-animations-and-the-weather)
- [Messages, views and settings](#messages-views-and-settings)
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

`panel.yaml` gives Home Assistant `Panel Connected`, `Panel Temperature` (the panel's own sensor),
`Panel Firmware`, `Panel Action` (filter reset and the like, picked on the panel), the screensaver and LED
selects, and the brightness and night mode settings. `Panel Link Windows Too Short`, `Panel Link Unanswered` and
`Panel Link Lost` count link trouble since the ESP started. The rest of the panel's settings are in its own
menu and the `panel_set` action below.

| File | What |
|---|---|
| `panel.yaml` | The link and the panel's own entities |
| `panel/ha_sensor.yaml`, `ha_binary_sensor.yaml`, `ha_text.yaml`, `ha_switch.yaml` | A Home Assistant entity on the panel |
| `panel/sensor.yaml`, `binary_sensor.yaml`, `text.yaml`, `switch.yaml`, `number.yaml`, `button.yaml` | One of the ESP's own entities on the panel |
| `panel/fireplace.yaml`, `humidity_boost.yaml`, `co2.yaml`, `summer_cooling.yaml` | That package on the panel's comfort page |
| `panel/picture.yaml`, `online_picture.yaml`, `ha_picture.yaml`, `animation.yaml`, `met.yaml` | Screensaver pictures |
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

A Home Assistant entity goes on with an `ha_` package, which makes the `homeassistant` entity on the ESP for you.
`name` is the item's name on the link and makes the ESPHome id, `ha_<name>`:

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
| `name`, `entity_id` | `ha_` packages: the item's name and the Home Assistant entity |
| `entity` | Plain packages: the ESPHome id |
| `label`, `help` | `English\|Norsk`, the panel shows the part for its language: up to 19 bytes of label and 78 of help. Help shows on ? |
| `icon` | An id from the [icons](#icons), in lower case |
| `page` | `smart_home` (default) or `comfort` |
| `view`, `menu` | `"false"` leaves it off the page's view or its menu |
| `name` | Plain packages: the item's name on the link, the entity id if empty. The panel keeps its dashboard tiles by it |
| `unit`, `decimals` | Sensors and numbers; the entity's own if empty. Up to 7 bytes of unit. An `ha_sensor` has no unit unless you give it one |
| `levels`, `min`, `max` | Sensors: `"bad below, good from, good to, bad above"` colours the value, min and max are the ends of its gauge |
| `trend` | Sensors: `"true"` keeps the last 24 hours for the sensors view, room for four |
| `on_text`, `off_text` | Binary sensors and switches: what to say instead of On and Off, at most 11 bytes each |
| `on_action`, `off_action`, `on_state` | `ha_switch`: the actions and the state that counts as on |

Inside `{}` a text with a comma needs quotes, like `help: "Warm, then cool|Varmt, så kjølig"`.

A text is read-only and shows the first 15 bytes of the entity's state, as a tile on the smart home view, a row in
the menu or a dashboard tile. Like the label it can be `English|Norsk`. The comfort view shows the first two
sensors as gauges and the switches as chips, so the order in your config is the order on the screen.

## The comfort packages

`panel/fireplace.yaml`, `panel/humidity_boost.yaml`, `panel/co2.yaml` and `panel/summer_cooling.yaml` are nothing
more than the includes above, pointed at that package's entities and put on the comfort page. Add the ones you
have the main package for.

<table>
<tr>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/04_comfort.png" width="280" alt="Comfort view"><br><sub>The comfort view: humidity and CO₂ gauges, the automations as chips</sub></td>
<td align="center"><img src="https://github.com/MSkjel/flexit-ci600-firmware/raw/screens/all/shots/14_comfort_menu.png" width="280" alt="Comfort menu"><br><sub>The comfort menu, with the settings</sub></td>
</tr>
</table>

This is all of `panel/fireplace.yaml`:

```yaml
packages:
  fireplace_on: !include
    file: switch.yaml
    vars:
      name: fireplace
      entity: fireplace_mode_switch
      label: Fireplace|Peis
      icon: fireplace
      page: comfort
      help: >-
        Blows in more air than it takes out for a while, so the fireplace draws well.|Blåser inn mer luft enn den tar
        ut en stund, så peisen trekker godt.
  fireplace_minutes: !include
    file: number.yaml
    vars:
      name: fireplace_minutes
      entity: fireplace_duration
      label: Fireplace time|Peistid
      unit: min
      icon: timer_sand
      page: comfort
      view: "false"
      help: How long fireplace mode lasts.|Hvor lenge peismodus varer.
```

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
```

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
| `met.yaml` | `latitude`, `longitude`, `scene` (hills, coast, mountains, city), `aurora` (auto, off, always), `update_interval` (5 min, the forecast is only fetched again when MET says it expired), `redraw_interval`, `name` (weather). `Panel Weather Scene`, `Panel Weather Aurora` and `Panel Weather Forecast` change it from Home Assistant |

`met.yaml` draws the sky for the time of day and the next hour's weather from [MET Norway](https://api.met.no)
(CC BY 4.0). The weather right now is also an item, so it can be a dashboard tile, and the cabin's chimney smokes
when the heater is on. A failed request with `Code: 304` in the log is fine, the forecast just hasn't changed.

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
| `panel_set` (`setting`, `value`) | A panel setting, see the list in `panel.yaml`: theme, brightness, idle_after, screensaver, night hours, key_lock, language and more |
| `panel_show_picture` (`name`), `panel_clear_picture` | The screensaver picture, same as the `Panel Picture` select |
| `panel_show_animation` (`name`), `panel_clear_animation` (`name`, or none for all) | An animation over it, where its yaml puts it, the same as its `Panel Animation <name>` switch. Any combination of them, within the panel's 6 KB |
| `panel_place_animation` (`name`, `copy`, `x`, `y`, `own`, `start`), `panel_remove_animation` (`name`, `copy`) | The same animation in more places. Copy 0 is the one `panel_show_animation` shows, copy 1 and up are extra. They play in step and cost nothing extra, unless `own: true` gives the copy a frame of its own, which takes another w x h / 2 bytes of the 6 KB and starts `start` frames in |
| `panel_move_animation` (`name`, `copy`, `x`, `y`, `ms`), `panel_drift_animation` (`name`, `copy`, `vx`, `vy`) | Glide a copy to `x`, `y` over `ms` (0 jumps, 65535 at most), or drift it at `vx`, `vy` pixels per second, round the screen or as its area says; 0, 0 stops it. Drifting over a JPEG photo costs more redraws than over a palette picture, so keep it slow there |
| `panel_turn_animation` (`name`, `copy`, `angle`, `ms`), `panel_spin_animation` (`name`, `copy`, `speed`), `panel_flip_animation` (`name`, `copy`, `horizontal`, `vertical`) | Turn a copy round its middle to `angle` degrees clockwise, the short way round over `ms` (0 at once), or spin it at `speed` degrees per second, negative the other way; 0 stops it where it is. Flip mirrors it left to right, upside down or both. Quarter turns and flips stay sharp, other angles look rougher |
| `panel_area_animation` (`name`, `copy`, `left`, `top`, `right`, `bottom`, `edge_x`, `edge_y`) | Where a copy's drift or wander keeps. Each edge is `screen` (round the whole screen, the default), `cross` (out at one edge of the area, in at the other) or `bounce` |
| `panel_wander_animation` (`name`, `copy`, `speed_min`, `speed_max`, `heading`, `spread`, `leg_min`, `leg_max`, `rest_min`, `rest_max`) | Legs of `speed_min` to `speed_max` pixels per second toward `heading` give or take `spread` degrees (`spread` 180 is any way), each `leg_min` to `leg_max` ms (0 0 is one leg), with rests of `rest_min` to `rest_max` ms between (0 0 none). It keeps to its area, and a bounce turns the heading round |
| `panel_face_animation` (`name`, `copy`, `facing`, `mirror`, `tilt`, `rate`, `level`) | Turns a copy toward where it goes. `facing` is the way its frames face, `mirror` flips it to face the other way, `tilt` the most it turns (0 to 180), `rate` degrees per second (0 at once), `level` back upright when it stops. A turn or spin ends it |
| `panel_size_animation` (`name`, `copy`, `size`, `ms`) | A copy at `size` percent (10 to 400) over `ms` (0 at once) |
| `panel_fade_animation` (`name`, `copy`, `opacity`, `ms`) | A copy at `opacity` percent over `ms` (0 at once) |
| `panel_rounds_animation` (`name`, `copy`, `rounds`) | A copy plays to the end of `rounds` rounds, then fades out and goes. 0 plays on. With `own: true` and `start: 0` it plays from the first frame and holds the last |

For the animation actions:

- The screen is 320 x 240, 0,0 top left, y down. `x`, `y` is the top left of a copy's frame at full size. Sizes and
  turns go round its middle.
- `angle`: degrees clockwise from the frames as drawn, -90 is 270. `speed`, `rate`: degrees per second, negative
  anticlockwise.
- `heading`, `facing`: 0 right, 90 down, 180 left, 270 up.
- Times in ms, 0 at once. Speeds in pixels per second.
- An area's `right` and `bottom` are just past it, 0, 0, 320, 240 is the whole screen.
- `copy` 0 to 7, copy 0 is the yaml's. The panel shows 8 copies at most.
- A placed copy starts still, upright, full size, on the whole screen. Move, drift and wander replace each other, so
  do turn, spin and face.

Examples, for an ESP called `flexit`:

```yaml
# a sun, turning and growing
- action: esphome.flexit_panel_spin_animation
  data: {name: sun, copy: 0, speed: 10}
- action: esphome.flexit_panel_size_animation
  data: {name: sun, copy: 0, size: 120, ms: 2000}
# a boat drawn facing right, gliding left
- action: esphome.flexit_panel_flip_animation
  data: {name: boat, copy: 0, horizontal: true, vertical: false}
- action: esphome.flexit_panel_move_animation
  data: {name: boat, copy: 0, x: 10, y: 180, ms: 20000}
# a ghost, half see-through, leaning back
- action: esphome.flexit_panel_fade_animation
  data: {name: ghost, copy: 0, opacity: 50, ms: 1000}
- action: esphome.flexit_panel_turn_animation
  data: {name: ghost, copy: 0, angle: -15, ms: 500}
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
# a fish in a tank
- action: esphome.flexit_panel_area_animation
  data: {name: fish, copy: 0, left: 20, top: 120, right: 300, bottom: 220, edge_x: bounce, edge_y: bounce}
- action: esphome.flexit_panel_wander_animation
  data: {name: fish, copy: 0, speed_min: 5, speed_max: 25, heading: 0, spread: 30, leg_min: 1000, leg_max: 5000,
         rest_min: 500, rest_max: 3000}
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
```

The panel's LEDs have their own selects (`Panel Alarm LED`, `Panel Filter LED`, `Panel OK LED`): Auto works like
the stock panel, the rest let Home Assistant decide.

## Flashing the panel

The panel firmware is the `.fxup` from the firmware repo's
[releases](https://github.com/MSkjel/flexit-ci600-firmware/releases). With `panel/update.yaml`, press
`Flash Panel Firmware` and watch `Panel Firmware Update`. It downloads from `Panel Firmware URL`, which starts at the
newest release and goes back to the `url` var each time the ESP restarts.

Before anything is flashed the ESP looks for the panel on the bus the way the unit does at start-up, and the reply
says what runs there. A stock panel is flashed if it's a 7.x, and our firmware if the file is another version.
`Panel Firmware Force` overrides that: the same version again, or a stock panel that isn't 7.x. Only 7.x has been
tested, so with another stock version you're on your own. The bootloader always answers at address 1, so to flash
a panel at 3, take anything else at 1 off the bus first. If a flash fails halfway, the panel stays dark with its
three LEDs on, in its bootloader. Press `Flash Panel Firmware` again and the ESP picks it up there, before any
other panel.

A stock panel is at address 1, so the ESP has to be at 2 until the panel is flashed. The new panel comes up at 3,
so move the ESP to 1 then. Restart the ventilation unit after flashing, it only looks for panels when it starts.
The panel and the ESP then find each other on their own, and `Panel Connected` shows when they're linked. To
build a `.fxup` yourself you need the stock bootloader's key: `tools/mkupdate.py --stock <a stock flash dump>`
in the firmware repo.

## Icons

The ids in [`code/panel/plink_icons.h`](../packages/code/panel/plink_icons.h), in lower case: fan, thermo, fire,
snow, filter, humidity, co2, home, bell, alert, clock, sun, night, leaf, fireplace, water, shower, window, bulb,
plug, door, battery, solar, lock, motion, washer, vacuum, tv, cash and about 80 more.

How the panel and the ESP share the bus is in the
[firmware's README](https://github.com/MSkjel/flexit-ci600-firmware#the-esp-link). The C files in
[`packages/code/panel/`](../packages/code/panel) are copies of its `proto/`.
