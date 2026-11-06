# esp-sunrise-alarm

🇬🇧 English · 🇷🇺 [Русский](README.md)

> A per-weekday sunrise alarm on ESP8266/ESP32: a gradual, perceptually smooth fade-in at your wake-up time, room sensors and a browser settings page — no cloud, no phone app, no account.

| | |
|---|---|
| **Board** | Wemos D1 mini (ESP8266) — the same sketch also builds for ESP32 |
| **Light** | Any LED strip or lamp switched through a logic-level N-MOSFET |
| **Clock** | DS3231 RTC with backup battery (NTP refills it after a power loss) |
| **Sensors** | BME280 (temperature, pressure), AHT10/AHT20 (humidity, optional) |
| **Control** | Web UI on port 80 + one push button |
| **Storage** | `/config.cfg` on LittleFS (atomic writes, survives power cuts) |

## Why this exists

A phone alarm jolts you awake; a light that brightens over 15 minutes does not. Commercial wake-up lights tie your schedule to a vendor app and a cloud account, and plain timer plugs only switch on — they don't fade. This project runs entirely on the board: the schedule, the fade curve, the sensors and the settings page all live in the firmware, and the only thing you need to reach it is a browser on the same Wi-Fi.

## What it does

- **Per-weekday schedule.** Seven independent on-times (Monday…Sunday, ISO order), each with its own enable switch, plus one master switch for the whole schedule.
- **A fade that ramps the way your eye expects.** PWM duty follows a fixed perceptual curve (`d(p) = p^1.73697`) instead of a straight line: 50 % of the fade time gives ≈30 % duty, so the light creeps up gently and spends most of its visible brightening in the second half, rather than bursting on in the first seconds. The ceiling is set by a max-brightness slider (0–100 %).
- **Room sensors in the same page.** Temperature and pressure from a BME280, humidity from an AHT10/AHT20, date and time from the DS3231.
- **One button, three gestures.** Tap during the fade to stop it and snooze; tap at full brightness to switch off; hold for two seconds to skip today.
- **Everything persists.** Settings are written back to flash automatically a few seconds after you change them.

## What you need

| Part | Notes |
|---|---|
| Wemos D1 mini (ESP8266, 4 MB flash) | The board this project is wired for; the same sketch also builds for ESP32 — see [ESP32 builds](#esp32-builds-too) |
| DS3231 RTC module + CR2032 cell | Keeps time across power cuts. Without a working RTC (and no internet for NTP) the schedule never fires |
| BME280 breakout | Temperature and pressure, I²C address `0x77` (some modules are `0x76`, see [troubleshooting](#troubleshooting)) |
| AHT10 or AHT20 breakout | Humidity, I²C address `0x38`. Optional — without it humidity falls back to the BME280 |
| Logic-level N-MOSFET or a MOSFET/constant-current LED driver module | Must switch fully from a 3.3 V gate. `IRLZ44N` for larger loads, `AO3400`/`IRLML2502` for short strips |
| LED strip or lamp + its own DC supply | 5 V or 12 V strip, supply rated for the strip current |
| Momentary push button (normally open) | Any small tactile button; no external pull-up needed |
| Resistors: 220 Ω and 10 kΩ | Gate series resistor and gate pull-down (see wiring) |
| 3.3 V supply for the board and sensors | A USB supply is fine for the board; the strip gets its own supply |
| Breadboard + jumper wires | Or solder it onto perfboard once it works |

> **Never drive the LED strip from a GPIO pin.** The pins source/sink about 12 mA; a strip needs hundreds. The MOSFET is the switch, the pin only drives its gate.

## Wiring (Wemos D1 mini)

| Signal | Board pin | GPIO | Connects to |
|---|---|---|---|
| PWM to light | **D8** | GPIO 15 | Gate of the N-MOSFET (through 220 Ω) |
| I²C SDA | **D2** | GPIO 4 | SDA of DS3231, BME280 and AHT10/AHT20 |
| I²C SCL | **D1** | GPIO 5 | SCL of DS3231, BME280 and AHT10/AHT20 |
| Button | **D4** | GPIO 2 | One leg to the pin, the other to GND (`INPUT_PULLUP`) |

These numbers come from [`LightScheduler/board.h`](LightScheduler/board.h) and are identical for both platforms — pins are board wiring, not a chip property.

### Shared I²C bus

```text
                       3V3 ────┬──────────────┬──────────────┐
                               │              │              │
     Wemos D1 mini        ┌────┴─────┐   ┌────┴─────┐   ┌────┴──────┐
     ┌───────────────┐    │ DS3231   │   │ BME280   │   │ AHT10/20  │
     │ D1 (GPIO5) ───┼────┤ SCL      │   │ SCL      │   │ SCL       │
     │ D2 (GPIO4) ───┼────┤ SDA      │   │ SDA      │   │ SDA       │
     │ 3V3 ──────────┼────┤ VCC      │   │ VCC      │   │ VCC       │
     │ GND ──────────┼────┤ GND      │   │ GND      │   │ GND       │
     └───────────────┘    └──────────┘   └──────────┘   └───────────┘
```

All three modules sit on the same two wires and are powered from 3.3 V. The AHT is optional; the firmware logs which sensors it found at boot and falls back to the BME280 for humidity if the AHT is missing.

### LED strip (low-side N-MOSFET switch)

```text
     +V (strip supply) ─────────────── + of LED strip
     − of LED strip ────────────────── D  (drain)
     strip supply GND ──┬────────────── S  (source)
                        └────────────── GND of the board   ← common ground, mandatory

     D8 (GPIO15) ──[ 220 Ω ]── G (gate)
                                │
                            [ 10 kΩ ]
                                │
                               GND
```

- **Common ground between the strip supply and the board is not optional** — without it the gate voltage has no reference and the strip flickers or stays off.
- **The 10 kΩ gate pull-down is not optional either.** GPIO15 is an ESP8266 *strapping* pin that must be LOW at boot; the pull-down keeps it low, keeps the strip dark during boot, and stops a floating gate from glowing faintly. If your driver module has its own pull-up on the input, add a level shifter or a series resistor instead of fighting it.
- The 220 Ω gate resistor limits the pin's transient current while charging the gate capacitance.

### The button

One leg to **D4**, the other to **GND**. The firmware enables the internal pull-up, so pressing the button pulls GPIO2 low. EncButton handles debounce; no capacitor or external resistor is needed.

## Set up the build environment

The sketch is a plain Arduino sketch in [`LightScheduler/`](LightScheduler) — all `.h`/`.cpp` files must stay next to the `.ino`. There is no `platformio.ini`, CMake or CI.

### Cores

| Platform | Core | Requirement |
|---|---|---|
| ESP32 | `esp32 by Espressif Systems` | **≥ 3.0** (verified on 3.3.0). The firmware uses the core-3.x LEDC API `ledcAttachChannel()` / `ledcWriteChannel()`; the old `ledcSetup`/`ledcAttachPin`/`ledcWrite(channel, …)` calls were removed |
| ESP8266 | `esp8266 by ESP8266 Community` | **3.x** (verified on 3.1.2). The ESP8266 has no LEDC — PWM is `analogWrite` (`analogWriteFreq`, `analogWriteRange`) |

### Libraries

Install from *Sketch → Include Library → Manage Libraries*. Versions in the right column are the ones the project was verified with; newer patch releases are normally fine.

| Library | Verified | Purpose |
|---|---|---|
| **Settings** | 1.4.3 | Web UI framework (class `SettingsGyver`, header `<SettingsGyver.h>`) |
| **Saver** | 1.1.0 | Persists `Config` to LittleFS, atomic replace |
| **EncButton** | 3.7.6 | Click/hold detection and debounce |
| **GyverBME280** | 1.5.4 | BME280 driver (IIR filter ×16 set explicitly) |
| **Adafruit AHTX0** | 2.0.6 | AHT10/AHT20 driver, plus **Adafruit BusIO** and **Adafruit Unified Sensor** |
| **GyverDS3231** | 1.2.1 | DS3231 driver with time zone support |

Dependencies install themselves: GTL, GyverDB, StringUtils, GyverHTTP, BSON, Stamp, Table, StreamIO (for Settings), GyverIO (for EncButton), Stamp (for GyverDS3231).

> **Naming trap:** older guides call the web-UI library **SettingsGyver**. In the Library Manager the package is now **Settings**, while the class inside is still `SettingsGyver` — that is what [`LightScheduler/webui.cpp`](LightScheduler/webui.cpp) includes.

### Partition scheme (ESP32)

`Tools → Partition Scheme` must leave room for a filesystem, because settings live in `/config.cfg` on LittleFS. The default scheme **`Default 4MB with spiffs`** (1.2 MB app / 1.5 MB FS) is what the project uses. Do not pick `Huge APP` or `No FS`. On ESP8266 the standard 4 MB layout of the D1 mini already includes the FS.

### ESP32 builds too

The same sketch compiles for ESP32 (`LightScheduler/board.h` selects the platform from the core macros). The pin macros are unchanged, so a D1-mini-shaped wiring does not transfer literally: GPIO 15, 4, 5 and 2 are valid ESP32 outputs, but you must wire the ESP32 board's own pins and its 3.3 V/GND. Verified build: ESP32 core 3.3.0 — flash 85 %, RAM 15 %.

### Command line (`arduino-cli`)

```bash
# one-time: board indexes
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli config add board_manager.additional_urls \
  https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli core install esp8266:esp8266

# libraries
arduino-cli lib install "Settings" "Saver" "EncButton" \
  "GyverBME280" "GyverDS3231" "Adafruit AHTX0"
```

## Build and flash

### ESP8266 (Wemos D1 mini) — the reference target

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 LightScheduler/
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 LightScheduler/
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

`nodemcuv2` is the FQBN the project verifies against; `esp8266:esp8266:d1_mini` selects the same chip and 4 MB layout and works equally well. On Windows the port is something like `COM4`; on macOS `/dev/cu.usbserial-*`.

Verified build: flash (IROM) 36 % of 1 MB, RAM 39 %, **IRAM 95 %** — IRAM is the tight constraint on ESP8266 (≈2.7 KB of slack), so extra features can overflow it.

### ESP32

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 LightScheduler/
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 LightScheduler/
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### Arduino IDE

1. *Tools → Board*: **LOLIN(WEMOS) D1 R2 & mini** (or **NodeMCU 1.0**) for ESP8266, **ESP32 Dev Module** for ESP32.
2. *Tools → Port*: the serial port of the board.
3. Open [`LightScheduler/LightScheduler.ino`](LightScheduler/LightScheduler.ino) — the whole folder must be present, not just the `.ino`.
4. *Upload* (→), then open *Tools → Serial Monitor* at **115200** baud.

If the upload fails, close the Serial Monitor first (it holds the port) and check that your USB cable carries data, not just power.

## First boot

1. **Serial monitor at 115200.** The board prints a banner, then what it found on the I²C bus, then the Wi-Fi result, then the start time. It ends with `Готово` ("ready").
2. **Wi-Fi.** With `WIFI_SSID` set, the board joins your network in station mode and waits up to 15 s. If the SSID is empty or the join fails, it starts its own **open** access point named `LightScheduler` at **`http://192.168.4.1`**.
3. **Open the settings page** at the IP printed on the serial line (`WiFi: подключено, IP …`) or at `192.168.4.1` in AP mode. The web server starts only after the Wi-Fi attempt (up to 15 s), so give it a moment.
4. **Set the clock.** A DS3231 that lost power reports `Sensors: RTC был сброшен!`. When the board is on an internet-connected network it fetches the time from `pool.ntp.org` and writes it into the RTC; there is no NTP in AP mode, so a flat RTC battery means you must connect to real Wi-Fi once.

A typical boot log:

```text
Smart Scheduled Light Controller
Sensors: BME280 OK
Sensors: AHT10/20 OK — влажность с AHT      # humidity from AHT
WiFi: подключение...                        # connecting
WiFi: подключено, IP 192.168.1.42           # connected
Sensors: синхронизация времени по NTP... ок # NTP sync ok
Старт: …                                    # start date/time
Готово                                      # ready
```

## The web interface

Open the board's IP in any browser; the page is served on port 80 and needs no WebSocket (values refresh by polling). The labels are in Russian — that is the project's house style, and the table below maps them.

| Group | Control | Range / action | Stored in |
|---|---|---|---|
| Расписание (Schedule) | «Расписание включено» — master switch | on/off | `config.enabled` |
| Расписание | Seven rows Пн…Вс, each with «Включено (X)» | on/off per day | `config.days[i].enabled` |
| Расписание | «Часы» — hour spinner | 0–23, step 1 | `config.days[i].hour` |
| Расписание | «Минуты» — minute spinner | **0–55, step 5** | `config.days[i].minute` |
| Розжиг (Fade) | «Длительность (мин)» — fade length | 0–60 min | `config.fadeDuration` |
| Розжиг | «Максимальная яркость» | 0–100 % | `config.maxBrightness` |
| Розжиг | «Запуск будильника» / «Выключить будильник» | start / stop the fade immediately | — |
| Кнопка (Button) | «Отложить (мин)» — snooze length | 1–60 min | `config.snoozeMinutes` |
| Кнопка | «Время свечения (мин)» — how long the light stays on | 0–120 min | `config.turnOffDelay` |
| Датчики (Sensors) | Дата, Время, Температура, Влажность, Давление | read-only, one decimal | — |
| — | Лог (Log) | tails the serial log, last ~1 KB | — |

Notes that save you a bug report:

- **Times are set in 5-minute steps** in the UI (the stored field accepts 0–59, so a value edited in `config.h` still works, but the browser can only pick multiples of five).
- **The fade lengths add up.** The fade runs for `fadeDuration`, then the light stays at full brightness for `turnOffDelay` more minutes, then switches itself off. Defaults 15 + 60 = 75 minutes of light.
- **`fadeDuration = 0`** jumps to full brightness on the first step (≤0.5 s); **`turnOffDelay = 0`** switches off as soon as the fade finishes.
- **`maxBrightness = 0 %`** produces a fade that never lights the strip, yet the firmware still considers the light "on" until it switches off — useful for testing the schedule, confusing if you forget the slider.
- Changes are saved by themselves about 5–10 s after you stop dragging; the log prints a timestamped line when the write happens.

## Button gestures

| Gesture | Light state | Effect |
|---|---|---|
| Tap | Fade in progress | Fade stops, lamp goes dark, and a new fade starts after the snooze time (default 10 min) |
| Tap | At full brightness (fade finished) | Lamp off only — **no** snooze; a countdown left over from an earlier tap is cancelled |
| Tap | Light off, including while a snooze countdown is running | Nothing at all (log line only). A pending countdown cannot be restarted or extended by tapping |
| Hold ≥ 2 s | Any | Lamp off, snooze cancelled, **today's** fade skipped |

Things worth knowing before you rely on it:

- **A tap does not cancel today's scheduled fade.** If the snooze fires before the scheduled time, the light fades in on the snooze, goes off, and then fades in *again* at the scheduled time. Holding for 2 s is the only way to cancel the whole day.
- **The snooze countdown is a manual override** — it runs even if the master switch is off or today is disabled. That is intentional: you asked for light *now*.
- **Only today is skipped.** The hold gesture stores the date, so tomorrow's schedule re-arms by itself.
- **The snooze countdown, the skip date and the "already fired this minute" flag live in RAM.** A reboot clears them; the schedule then behaves as if nothing happened.
- Hold and tap are mutually exclusive in the button library: a hold that exceeds 2 s never produces a tap afterwards.

## Defaults and what is stored

| Setting | Field | Default | Range |
|---|---|---|---|
| Schedule master switch | `enabled` | on | — |
| Per-day time | `days[i].hour` / `.minute` | **07:30, all seven days enabled** | 0–23 / UI 0–55 step 5 |
| Fade length | `fadeDuration` | 15 min | 0–60 |
| Light duration after the fade | `turnOffDelay` | 60 min | 0–120 |
| Snooze | `snoozeMinutes` | 10 min | 1–60 |
| Max brightness | `maxBrightness` | 100 % (PWM 255) | 0–100 |

Persistence details:

- Settings live in **`/config.cfg` on LittleFS**, written as one binary struct by the **Saver** library with `FileStore::Atomic` (a power cut mid-write cannot corrupt the file).
- The format version is the character **`'D'`**. When a firmware update changes the layout, Saver refuses the old file and rewrites it with defaults — that is a **deliberate, one-time settings reset**, not a bug.
- On first boot of a fresh board, LittleFS is formatted automatically (ESP32: `LittleFS.begin(true)`; ESP8266: auto-format on a failed mount).
- All settings have defaults compiled in at `LightScheduler/config.h`, so a reset restores exactly what is documented above.

## Configuration: pins, time zone, credentials

| What | Where |
|---|---|
| Pins (PWM, I²C, button) | [`LightScheduler/board.h`](LightScheduler/board.h) — one set for both platforms |
| Wi-Fi SSID/password, AP name, time zone, PWM frequency, BME280 address, all defaults | [`LightScheduler/config.h`](LightScheduler/config.h) (the AHT address lives in `sensors.cpp`) |

```cpp
// LightScheduler/config.h — the parts you are likely to edit
#define WIFI_SSID  "your-2.4-ghz-ssid"  // leave empty to boot straight into AP mode
#define WIFI_PASS  "your-password"
#define AP_NAME    "LightScheduler"     // access-point name when there is no SSID
#define TIME_ZONE  3                    // hours east of UTC (default: Moscow, UTC+3)
```

- **Wi-Fi credentials stay local.** The committed copy of `config.h` ships with empty strings; fill in your own values in the working tree and never commit them. Code, logs and documentation must not contain real credentials (a rule in [docs/code-style.md](docs/code-style.md)). Because `config.h` is tracked, stage files explicitly instead of using `git add -A` or `git commit -a`.
- **2.4 GHz only.** ESP8266 and ESP32 cannot see 5 GHz networks.
- **Set `TIME_ZONE` for your location.** It drives both the DS3231 and the NTP call; the wrong value shifts the whole schedule by whole hours.
- **Pin changes are rare** — but if you move the light output, keep it off the strapping pins GPIO 0, 2, 15 and off GPIO 16, which has no PWM.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| The board does not boot, or the serial monitor prints garbage after a reset while you hold the button | GPIO2 (D4) is a strapping pin that must be HIGH at boot. **Release the button when powering up or pressing reset**; press it only after the board has booted |
| The board resets in a loop, or the light flashes while the board boots | GPIO15 (D8) must be LOW at boot. Fit the 10 kΩ gate pull-down, make sure the driver input has no pull-up to 3.3 V/5 V, and add a series resistor. A driver that pulls D8 high keeps the chip from booting |
| A blue LED on the board flickers at boot and lights while the button is pressed | That is the module's onboard LED, wired to GPIO2 (D4) — the same pin as the button. Expected, not a fault |
| The strip glows faintly while the light is supposed to be off | Floating or leaking gate: fit the 10 kΩ pull-down to GND and make sure the MOSFET is a logic-level type that is fully off at 0 V on the gate |
| The web page does not open | The page starts only after the Wi-Fi attempt (up to 15 s) in `setup()`. Check the IP in the serial log; in AP mode join the open network `LightScheduler` and browse to `http://192.168.4.1`. Router "AP isolation" blocks access too |
| The board always falls back to AP mode | Wrong SSID/password, a 5 GHz network, or a router that hides the SSID. Check the `WiFi: подключение...` line — no `подключено` means the join failed |
| The schedule never fires | The RTC is not synced, so the scheduler never ticks. Look for `Sensors: RTC был сброшен!` or `DS3231 не найден`; connect to internet-enabled Wi-Fi once so NTP can set the clock. Also check the master switch, the day's switch, and `TIME_ZONE` |
| The light comes on at the wrong hour | `TIME_ZONE` does not match your location, or the RTC itself is off by hours |
| Humidity is missing, or suspiciously identical to the BME280 reading | The AHT was not found at `0x38` (log line says so) or its reading is older than 10 s; the BME280 is the designed fallback. Check the AHT wiring or its `0x39` address jumper |
| `Sensors: BME280 не найден` | Wrong I²C address (`0x77` vs `0x76` — change `BME280_ADDR`) or SDA/SCL swapped (D2/D1) |
| Compile error: `ledcAttachChannel` / `ledcWriteChannel` not declared | ESP32 core older than 3.0. Update *esp32 by Espressif Systems* |
| Compile error around `analogWriteRange` or `LittleFS` | ESP8266 core older than 3.x. Update *esp8266 by ESP8266 Community* |
| ESP8266 link error: `iram1_0_seg overflowed` | The ESP8266 build uses 95 % of IRAM; the feature no longer fits. Trim it or build for ESP32 |
| Upload fails, "port busy" or no port at all | Close the Serial Monitor, install the CH340/CP210x driver, try another (data-capable) USB cable |
| All settings reverted after a firmware update | Expected once when the Saver format changes (`'C'`/`'B'` → `'D'`): the old file is ignored and defaults are written |
| The light never reaches full brightness | `maxBrightness` is below 100 %, or the fade length is long and you are watching the ramp |

## Repository map

```text
esp-sunrise-alarm/
├── README.md                  Russian README (primary language)
├── README.en.md               English README (this file)
├── LightScheduler/            firmware (flat Arduino sketch folder)
│   ├── LightScheduler.ino     setup(), non-blocking loop(), schedule and button state
│   ├── board.h                platform selection + pin map (single set, Wemos D1 mini)
│   ├── config.h / .cpp        Config struct, defaults, Saver over LittleFS
│   ├── sensors.h / .cpp       Wire, BME280, AHT10/20, DS3231, NTP sync
│   ├── light.h / .cpp         PWM abstraction, fade curve CURVE_LUT, brightness scaling
│   ├── button.h / .cpp        EncButton wrapper (click, hold ≥ 2 s)
│   ├── webui.h / .cpp         Wi-Fi connect + Settings web UI
│   ├── log.h / .cpp           Serial + browser log widget
│   └── README.md              firmware-focused quickstart and library table
├── docs/                      engineering notes, curve research, code style, process
├── PROJECT.md                 full architecture and behaviour reference
└── LICENSE                    MIT
```

## Further reading

- [PROJECT.md](PROJECT.md) — architecture, module APIs, exact behaviour matrix, verified build sizes.
- [docs/technical-notes.md](docs/technical-notes.md) — how the firmware techniques were chosen and verified (PWM abstraction, saver migration, sensor timing).
- [docs/light-curve.md](docs/light-curve.md) — why the fade follows `p^1.73697`, the generated LUT, and the perception models behind it.
- [docs/code-style.md](docs/code-style.md) — conventions for editing the firmware (Russian log/UI text, non-blocking `loop()`, no secrets in tracked files).
- [LightScheduler/README.md](LightScheduler/README.md) — short firmware quickstart, library list, per-module logic.
- [LICENSE](LICENSE) — MIT.

## License

MIT © 2026 Amir Suleymanov. See [LICENSE](LICENSE). The bundled GyverLibs dependencies are MIT as well, in their respective repositories.
