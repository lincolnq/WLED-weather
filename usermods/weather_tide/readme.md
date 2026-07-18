# Weather / Tide Dashboard usermod

Adds a selectable 2D effect **"Weather"** for a 20×20 matrix: three vertical
bars — outdoor **T**emperature, **P**recipitation "niceness", and **W**ater
(tide) — each with a trend arrow above and a letter below. Colors are sampled
from the segment's active palette (value → palette position).

- **T** — current temp (bar height/color = temp scaled 20–90 °F); arrow = warmer/colder 24 h out.
- **P** — niceness = 100 − precip% over the next 6 h; arrow = following 18 h better/worse.
- **W** — live tide level (0–4.2 ft MLLW); arrow = rising (in) / falling (out).

Data comes from **Open-Meteo** (plain HTTP, no TLS) and **NOAA CO-OPS** live
water level (one `setInsecure()` HTTPS call), refreshed every 10 minutes in the
usermod's `loop()`. The effect only renders when selected on a segment.

## Configuration

Defaults are compile-time `-D` macros (see top of `usermod_weather_tide.h`),
overridable in `platformio_override.ini`:

| Macro | Default | Meaning |
|-------|---------|---------|
| `WT_LAT` / `WT_LON` | `41.3540` / `-71.9662` | location (Mystic, CT) |
| `WT_TIDE_STATION` | `"8461490"` | NOAA station (New London) |
| `WT_REFRESH_MS` | `600000` | data refresh interval |

## Build & flash

This usermod is enabled by the `esp32dev_wt` env in `platformio_override.ini`
(`-D USERMOD_WEATHER_TIDE`). It targets **WLED v0.15.4** (this branch).

```sh
# PlatformIO in a Python 3.11/3.12 venv
python3.11 -m venv .venv && .venv/bin/pip install platformio

# build -> .pio/build/esp32dev_wt/firmware.bin
.venv/bin/pio run -e esp32dev_wt

# flash over WiFi (or use the web UI at http://<device>/update)
curl -F "update=@.pio/build/esp32dev_wt/firmware.bin" http://ledwall.local/update
```

After it reboots, pick the **Weather** effect in the WLED app and choose a palette.

## Notes

- Build is ~97% of the 4 MB app partition (TLS/mbedtls is heavy) — little room to grow.
- Panel orientation is handled in `px()` (90° CCW). Adjust there if your wiring differs.
