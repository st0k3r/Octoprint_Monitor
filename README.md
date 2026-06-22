# OctoPrint Monitor — Flipper Zero

External `.fap` app for **Momentum Firmware** (mmtm-012 31.12.2025).  
Communicates with an OctoPrint server over your local 2.4 GHz network via the
**official WiFi Dev Board** (ESP32-S2) running
[FlipperHTTP](https://github.com/jblanked/FlipperHTTP).

## Hardware wiring

| ESP32 pin | Flipper expansion pin |
|-----------|-----------------------|
| TX        | 14 (USART1_RX)        |
| RX        | 13 (USART1_TX)        |
| GND       | GND                   |
| 3.3 V     | 3.3 V                 |

The official WiFi Dev Board plugs directly into the expansion connector and is
already wired correctly.

## Configuration

On first run the app creates a default config file and displays the path.
Edit `/ext/apps_data/octoprint_monitor/config.txt` on the SD card:

```
ip=192.168.1.100
apikey=YOUR_OCTOPRINT_API_KEY
```

The API key can be found in OctoPrint → Settings → API.

## Building

Requires the [Momentum Firmware SDK](https://github.com/Next-Flip/Momentum-Firmware).

```bash
./fbt fap_octoprint_monitor
```

The compiled `.fap` ends up at
`build/f7-firmware-D/.extapps/octoprint_monitor.fap`.

Copy it to `/ext/apps/GPIO/` on the SD card.

## FlipperHTTP protocol used

| Direction | Frame |
|-----------|-------|
| Ping       | `[PING]\n` → `[PONG]` |
| GET        | `[GET/HTTP]\n{"url":"...","headers":{...}}\n` → body + `[GET/END]` |

## Project structure

```
octoprint_monitor/
├── application.fam          # fbt app manifest
├── octoprint_monitor.h      # shared types / constants
├── octoprint_monitor.c      # entry point, GUI, worker thread
├── uart.h / uart.c          # USART1 driver (interrupt-driven RX)
├── http.h / http.c          # FlipperHTTP framing helpers
├── config.h / config.c      # SD-card config file r/w
└── icon.png                 # 10×10 pixel app icon
```
