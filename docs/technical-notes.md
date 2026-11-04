# Technical Notes — esp-sunrise-alarm

Distilled **firmware engineering** notes from the implementation of the three features (per-weekday
schedule, perceptual fade curve, button snooze/skip) plus the post-release gated-tap policy change
(§1.12) and the first real-toolchain compile (§1.14). Release history: `e97cec3` (features) →
`2cf0285` (gated tap + technical notes) → `84c65d0` (`.gitignore`: local `libs/`) → compile-fix +
docs-sync release (2026-11-04, ESP32 core 3.x LEDC API; see `git log`).

Firmware details live here. The Team Lead's orchestration / release / independent-verification
playbook is in [`lead-hacks.md`](lead-hacks.md); the implementer's task-board workflow is in
[`workflow.md`](workflow.md). Companion docs: [`PROJECT.md`](../PROJECT.md),
[`light-curve.md`](light-curve.md).

---

## 1. Firmware techniques

### 1.1 Per-weekday schedule data model

`Config` keeps a fixed array instead of per-day pointers or a sentinel "off" hour:

```cpp
struct DaySchedule {
    uint8_t hour    = 7;
    uint8_t minute  = 30;
    bool    enabled = true;
};

struct Config {
    bool        enabled       = true;   // master switch
    uint8_t     snoozeMinutes = 10;
    DaySchedule days[7];                // [0]=Mon … [6]=Sun (ISO 8601)
    uint32_t    fadeDuration  = 30000;
};
```

- `sizeof(DaySchedule) == 3`, `sizeof(Config) == 28` on xtensa-gcc (Xtensa ABI is 4-byte aligned;
  one pad byte before `fadeDuration`).
- Index mapping is direct: `days[sensors_weekday() - 1]`, because `weekDay()` is already ISO
  (`1=Mon … 7=Sun`). No lookup table needed.
- Two separate enable levels: `config.enabled` (master) and `days[i].enabled` (per day). Trigger
  condition ANDs both, so "temporarily disable all" and "skip Mondays" are independent.
- Defaults are in the struct (NSDMI), so first boot, filesystem format, and Saver version reset all
  produce the same safe state without any init code.

**Hack / caution:** `SaverFile` serializes the struct as raw bytes — including padding. Do not
casually reorder fields or change types: bump the Saver format version instead.

### 1.2 Saver format migration without migration code

```cpp
SaverFile saver(LittleFS, "/config.cfg", config, 'B', 10, FileStore::Atomic);
//                                              ^^^ was 'A'
```

- Saver's header stores `ver` **and** data size. `begin()` copies stored data only when both match;
  otherwise it treats current RAM (the defaults) as the new config and rewrites the file.
- Bumping `'A' → 'B'` therefore gave a free, atomic, one-time settings reset after the struct change.
  No compatibility shim, no versioned parser.
- Trade-off accepted: user settings are lost once on upgrade. This is documented in
  [`PROJECT.md`](../PROJECT.md) and [`LightScheduler/README.md`](../LightScheduler/README.md).
- Autosave is CRC-driven at `timeout/2` (~5 s), so `tick()` must be called every loop.

### 1.3 RTC weekday/date without calendar math

The scheduler needs "what weekday is it" and "has the calendar day changed" but the project has no
date library of its own. Both are derived from the already-cached `StampKeeper` time base:

```cpp
uint8_t sensors_weekday() {                      // ISO 1=Mon … 7=Sun
    return rtc.synced() ? rtc.weekDay() : 1;     // safe index when RTC is lost
}

uint32_t sensors_date_key() {                    // ГГГГММДД, e.g. 20261102
    if (!rtc.synced()) return 0;
    return (uint32_t)rtc.year() * 10000UL + rtc.month() * 100UL + rtc.day();
}
```

- `year()/month()/day()/weekDay()` read the local-time `Datime` cache (zone set by
  `setStampZone`), so no extra I2C transaction per second and no weekday/date drift mismatch with
  `hour()`/`minute()`.
- `date_key` is a single `uint32_t` comparison — used for "skip today" and day-change detection.
  Chosen over `yearDay()` because a year boundary can repeat `yearDay` (1..366) after long uptime.
- Fallbacks (`1` / `0`) are safe: `sensors_tick()` returns `false` while unsynced, so the scheduler
  never runs on garbage. `dayIdx` stays in 0..6 by construction.

### 1.4 Minute-change dedupe that also handles midnight

```cpp
static bool    alarmTriggered = false;
static uint8_t lastMinute     = 255;

if (curMin != lastMinute) { lastMinute = curMin; alarmTriggered = false; }
```

- `alarmTriggered` blocks a second trigger inside the same minute.
- `lastMinute` reset is enough for day rollover: `23:59 → 00:00` always changes the minute, so the
  next day's event can never be suppressed — including a schedule at `00:00`.
- No date comparison needed for this part; the day is just an index into `config.days[]`.

### 1.5 Non-blocking snooze timer (millis-based)

```cpp
static bool     snoozePending = false;
static uint32_t snoozeStartMs = 0;

// tap (only when the light is on or fading — see §1.12):
if (light_isOn()) {
    light_stop();
    snoozePending = false;
    snoozeStartMs = millis();
    snoozePending = true;
}

// loop:
if (snoozePending) {
    uint32_t snoozeMs = (uint32_t)(config.snoozeMinutes ? config.snoozeMinutes : 1) * 60000UL;
    if (millis() - snoozeStartMs >= snoozeMs) {
        light_startFadeIn();
        snoozePending = false;
        alarmTriggered = true;   // no double start in the same minute
    }
}
```

- Unsigned subtraction is overflow-safe (`millis()` wraps ~49.7 days); range 1–60 min fits `uint32`.
- `max(1, snoozeMinutes)` guard makes a malformed/legacy `0` harmless.
- Repeated tap **while the light is on** restarts the countdown (simple and predictable), no
  accumulation. Tap while the light is off is a no-op (§1.12): it cannot restart or extend a pending
  snooze — wait it out, long-press to cancel/skip, or turn the light on from the web UI first.
- A tap before the scheduled event therefore creates no snooze and suppresses nothing: the schedule
  fires normally at its configured time.
- Snooze is a **manual override**: fires regardless of `config.enabled` and per-day enable.
- RAM-only: reboot cancels pending snooze (documented).

### 1.6 Ordering trick: snooze check *after* the schedule block

If the snooze deadline lands exactly on a scheduled minute, a naive order can start the fade twice
(schedule sees no pending snooze → starts; snooze timer then also fires). Putting the snooze block
after the schedule block makes the schedule see `snoozePending == true` in that iteration and
suppress itself; the snooze then starts exactly one fade. Verified by simulation at a 07:20 tap
with a 07:30 event and a 10-min snooze.

### 1.7 "Skip today" as an in-RAM one-shot date key

```cpp
static uint32_t skipDateKey = 0;   // ГГГГММДД of the skipped day

if (config.enabled && day.enabled && !alarmTriggered && !snoozePending &&
    sensors_date_key() != skipDateKey &&
    curHour == day.hour && curMin == day.minute) { ... }
```

- Long press sets `skipDateKey = sensors_date_key()`; the key differs tomorrow, so re-arm is
  automatic — no timer, no stored flag to clear.
- RAM-only => power cycle re-arms (acceptable; long press is a "today" gesture, not a config).
- Button handlers run before the schedule block, so a long press at the trigger second wins.

### 1.8 Button: 2 s hold without breaking tap

```cpp
btn.init(PIN_BUTTON);      // INPUT_PULLUP, active LOW
btn.setHoldTimeout(2000);  // default 600 ms, max 4000 ms
```

- EncButton v3 stores timeouts in 16 ms units (`tout >> 4`), so 2000 ms is exact; values above
  ~4080 ms saturate.
- `click()` requires `EB_HLD` **unset** (`bf.eq(EB_REL_R | EB_REL | EB_HLD, EB_REL_R)`), therefore a
  2 s hold can never also fire a tap — checking `button_click()` then `button_hold()` in the loop is
  safe. (Verified against upstream `VirtButton.h`.)
- `hold()` is an event generated **while still pressed** when the timeout is reached; `holding()` is
  the state. Acting at 2 s (before release) gives immediate feedback.
- v3 events persist until the next `tick()`; call `button_tick()` exactly once per loop.

### 1.9 Perceptual (log-compensated) fade with zero float in the hot path

**Calibration:** 50 % perceived intensity ↔ 30 % PWM duty. Power law

```
d(p) = p^γ,   γ = ln(0.30)/ln(0.50) = 1.7369655941662063
duty(i) = floor(255 · (i/255)^γ + 0.5)      // half-up, i = 0..255
```

**Why not a "log-shaped" duty curve?** A normalized concave curve `f(p)=ln(1+k·p)/ln(1+k)` has
`f(0.5) > 0.5` for every `k > 0` (Jensen: `ln(1+k/2) > ln(1+k)/2`). So no logarithmic-in-time curve
can satisfy `f(0.5)=0.30`; the feasible "logarithmic" reading is *perceived brightness rises
linearly in time while duty rises superlinearly* — which is exactly the power law above (and,
equivalently, an exponential `d(p)=(e^(a·p)−1)/(e^a−1)`, `a=2·ln(7/3)≈1.6946`). Power was chosen
because it is the exact inverse of the implied perception exponent `n = ln(0.5)/ln(0.30) ≈ 0.5757`.

**Runtime (integer only):**

```cpp
static const uint8_t CURVE_LUT[256] = { /* 256-byte table from docs/light-curve.md */ };
static_assert(sizeof(CURVE_LUT) == 256, "CURVE_LUT: ожидается 256 байт");

uint32_t elapsed = now - fadeStart;
uint32_t dur     = config.fadeDuration;

if (!dur || elapsed >= dur) { ledcWriteChannel(PWM_CHANNEL, 255); fading = false; return; }

uint16_t idx = (uint16_t)(elapsed * 255UL / dur);   // elapsed ≤ 60000 => product < 15.3e6
ledcWriteChannel(PWM_CHANNEL, CURVE_LUT[idx]);
```

Hacks / details worth remembering:
- **Time-based progress**, not per-step accumulation: no float drift; a delayed loop tick just jumps
  to the correct duty.
- `elapsed*255` fits `uint32` because the UI slider caps `fadeDuration` at 60 000 ms. `idx ≤ 254` in
  the curve branch; exact `255` is handled by the dedicated end branch.
- On ESP32, `static const` arrays live in flash — no `PROGMEM`/`pgm_read_byte` needed (0 extra RAM).
- `static_assert` makes a table-size regression a compile-time error.
- Quantization reality check: with half-up rounding the first nonzero duty is `LUT[8]` (≈3.1 % of the
  fade); the unavoidable `0→1` step is the visible low-end limit of 8-bit PWM. Future option: 10-bit
  PWM + 1024×`uint16_t` LUT (~2 KB) or dithering.
- `fadeDuration = 0` and `< FADE_STEP_MS` both become "max on the first 50 ms step" — same behavior as
  before the feature.

**Regenerate the table (offline, not shipped):**

```python
import math
GAMMA = math.log(0.30) / math.log(0.50)          # 1.7369655941662063
lut = [int(math.floor(255*(i/255.0)**GAMMA + 0.5)) for i in range(256)]
assert lut[0] == 0 and lut[255] == 255 and lut[128] in (76, 77)
assert all(lut[i] <= lut[i+1] for i in range(255))
```

### 1.10 Web-UI tricks (Settings / SettingsGyver)

- **Pointer binding:** widgets bind directly to `config` fields (`&config.days[i].hour`), so edits
  mutate the struct and Saver persists them with no glue code and no `config_save()` call.
- **`uint8_t*` works with `Slider`:** Settings' `AnyPtr` has an `unsigned char*` constructor
  (`Type::Uint`, length 1), so `snoozeMinutes` did not need to be widened to `uint32_t`.
- **Widgets are keyed by label** → every label must be unique across the whole page. Dynamic labels
  are generated per day: `snprintf(label, 32, "Часы (%s)", DAY_NAMES[i])`, giving 28 unique labels
  (1 master + 7×3 day widgets + fade slider + button + snooze slider + 3 sensors) and 11 unique
  group titles (4 groups + 7 day groups).
- **Reusing one stack `char label[32]` is safe** because Settings serializes the label into the
  response packet synchronously inside the widget call (`(*p)[Code::label] = label;` in
  `Builder::_beginWidget`) — it never stores the pointer. (Checked in `src/core/builder.h`; do not
  do this with a library that defers rendering.)
- **Nested `sets::Group` per day**, not `Tabs`: in current Settings, `Tabs` is a *value* widget
  (`bool Tabs(Text, AnyPtr)` returns the selected index), not a container; containers are
  `Group/Menu/Buttons/Row`. Seven nested groups render fine on phone.
- Widgets only exist during `onBuild`; the sensor values are refreshed via `onUpdate` IDs 1/2/3
  (polling mode, no WebSocket).

### 1.11 Loop hygiene

- `loop()` contains no `delay()`, no `while`, no per-step blocking — only tickers, event checks and
  `millis()` deadlines. The only blocking wait in the project is the pre-existing Wi-Fi connect loop
  in `webui.cpp`, and it is inside `setup()`.
- `Serial.printf` is available on ESP32 `HardwareSerial`; format casts (`(unsigned)`, `(unsigned long)`)
  avoid ABI surprises with `%u`/`%lu`.
- `sensors_tick()` is the single 1 Hz gate; all schedule state hangs off it.

### 1.12 `light_isOn()`: "light is on" vs "fade in progress"

`light_active()` answers "is a fade still running", which becomes `false` once the LED reaches full
brightness. The tap policy needs a different question — "would stopping the light make sense?" — so
the light module keeps a separate `on` flag:

```cpp
static bool fading = false;   // light_active()
static bool on     = false;   // light_isOn()

void light_startFadeIn() { on = true;  fading = true; ... ledcWriteChannel(PWM_CHANNEL, 0); }
void light_stop()        { on = false; fading = false; ledcWriteChannel(PWM_CHANNEL, 0); }
// end of fade: ledcWriteChannel(PWM_CHANNEL, 255); fading = false;   // on stays true
```

- `on` is set **before** the first PWM write in `light_startFadeIn()`, so the first tap after a
  scheduled start already counts as "light is on".
- Fade completion only clears `fading`; the lamp stays at 255 and `on` remains true until
  `light_stop()`.
- `light_stop()` is the single place that clears `on`, so every stop path (tap-snooze, long press,
  web/off) leaves consistent state.
- The .ino tap handler gates on `light_isOn()`, not `light_active()`: a tap must also postpone a lamp
  that is already fully on, and a tap in the dark must do nothing.
- Product rule (2026-11-02): tap is a no-op unless the light is fading or fully on; before the
  scheduled event there is nothing to postpone, so the schedule later fires as usual.

### 1.13 Local Gyver library checkout (`libs/`)

The exact upstream libraries this project was verified against are cloned into the git-ignored
`libs/` directory (shallow `--depth 1`, `main`), so API behavior can be checked offline and a real
compile can be attempted later with the matching versions:

| Library | Version | | Library | Version |
|---|---|---|---|---|
| Settings | 1.4.3 | | GTL | 1.4.7 |
| Saver | 1.1.0 | | GyverDB | 1.4.4 |
| EncButton | 3.7.6 | | StringUtils | 1.6.1 |
| GyverBME280 | 1.5.4 | | GyverHTTP | 1.0.34 |
| GyverDS3231 | 1.2.1 | | BSON | 3.0.1 |
| | | | Stamp | 1.5.1 |
| | | | Table | 1.3.6 |
| | | | StreamIO | 1.0.5 |
| | | | GyverIO | 1.4.2 |

- Clone form: `git clone --depth 1 https://github.com/GyverLibs/<Name>.git libs/<Name>`.
- `libs/` is in `.gitignore` (commit `84c65d0`); the nested upstream `.git` directories stay local and
  never enter this repo.
- An ignored `libs/README.md` records versions, short SHAs and exact commands. Reset with
  `rm -rf libs` and re-clone, or `git -C libs/<Name> pull`.
- The libraries are *not* vendored in git — the Arduino IDE still installs them from Library Manager
  (see [`LightScheduler/README.md`](../LightScheduler/README.md)). This checkout is for offline
  reading/verification, not for the build path.

### 1.14 ESP32 core 3.x: миграция LEDC API (первая реальная компиляция)

The project's first build with the real toolchain (`arduino-cli 1.5.1`, core `esp32:esp32 3.3.0`)
failed on two lines in `light.cpp`: **core 3.0 removed the legacy LEDC API**
(`'ledcSetup' was not declared in this scope`, `'ledcAttachPin' … did you mean 'ledcAttach'?`).
The migration is mechanical and behavior-preserving:

```cpp
// было (core 2.x):
ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
ledcAttachPin(PIN_PWM_LED, PWM_CHANNEL);
ledcWrite(PWM_CHANNEL, duty);

// стало (core 3.x):
ledcAttachChannel(PIN_PWM_LED, PWM_FREQ, PWM_RES, PWM_CHANNEL);
ledcWriteChannel(PWM_CHANNEL, duty);
```

- `ledcAttachChannel(pin, freq, res, channel)` заменяет пару `ledcSetup` + `ledcAttachPin`; параметры
  проекта прежние — канал 0, 5 кГц, 8 бит, GPIO 5 (`PWM_CHANNEL`/`PWM_FREQ`/`PWM_RES`/`PIN_PWM_LED`).
- `ledcWriteChannel(channel, duty)` — запись по номеру канала. В core 3.x появилась и перегрузка
  `ledcWrite(pin, duty)` (по пину); её не используем, чтобы сохранить явный `PWM_CHANNEL`.
- Диапазон `duty` прежний: `2^8 − 1 = 255`; поведение fade не изменилось — изменены только
  API-вызовы в `light_setup` / `light_startFadeIn` / `light_stop` / `light_update`.
- Итог сборки: exit 0, flash 1 093 751 / 1 310 720 B (83 %), RAM 48 972 / 327 680 B (14 %);
  предупреждений из кода проекта нет (два предупреждения — из Settings и ESP32 core).
- Требование: ядро **esp32 ≥ 3.0** (проверено на 3.3.0); со старым core 2.x проект не соберётся.
- Sandbox-нюанс (не свойство проекта): если `~/.cache` смонтирован read-only, `arduino-cli` падает
  на `build.options.json`; помогает `XDG_CACHE_HOME=/tmp/dsh-xdg-cache` или `--build-path /tmp/...`.
- Процесс: компиляцию запускает только агент `compile-checker` (единственный владелец `arduino-cli`)
  и только по необходимости — после изменения кода или по запросу Lead, точной командой без
  экспериментов с флагами; отчёт — pass/fail + ошибки компилятора. Все git-команды и diff'ы — у
  агента `git-release`; документацию обновляет Lead.

### 1.15 AHT10/AHT20 humidity: non-blocking state machine

Requirement (2026-11-04): humidity comes from an AHT10/AHT20 sensor on the shared I2C bus instead of
BME280, while `loop()` stays non-blocking. The Adafruit library's `getEvent()` cannot be used in the
loop: it busy-waits `while (getStatus() & 0x80) delay(10);` for the ≈80 ms measurement. So the
library is used for init/calibration only (`aht.begin(&Wire, 0, 0x38)`, blocking delays are
setup-only), and the measurement is a raw-Wire state machine driven from `sensors_tick()`:

```cpp
// trigger 0xAC 0x33 0x00 -> status byte (0x80 = busy) -> 6 data bytes
raw20 = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
humidity = (float)raw20 * 100.0f / 1048576.0f;   // 2^20
```

- Timing constants (in `sensors.cpp`): `AHT_PERIOD_MS = 2000` (new measurement), `AHT_POLL_MS = 100`
  (status poll cadence), `AHT_TIMEOUT_MS = 500` (abort a stuck measurement), `AHT_STALE_MS = 10000`
  (fall back to BME280 after this).
- No `delay()`, no `while` in the loop path; most passes issue **zero** I2C transfers. Each call does
  at most a couple of short, bounded I2C transfers (one trigger or status poll, or the final 6-byte
  read). `sensors_tick()` still returns `true` exactly once per second from `rtc.tick()`, so the 1 Hz
  scheduler gate is untouched.
- `aht_tick()` runs at the top of `sensors_tick()`, so the 1 Hz sample uses the freshest cached value.
- Fallback: `ahtOk == false` (init failed) or stale AHT data → `bme.readHumidity()`; the web UI never
  shows a frozen/zero value. A one-time Russian Serial log states which source is active.
- Constants (`AHT_ADDR`, protocol/timing) live in `sensors.cpp`; `config.h` stays the user's local
  config (Wi-Fi credentials, BME280 address) and is not part of the feature code.

### 1.16 BME280 IIR filter: explicit `setFilter()` before `begin()`

GyverBME280 implements the BME280 IIR filter; `setFilter(mode)` only stores `_filter_coef`, which is
written to register `0xF5` inside `begin()`. The call order is therefore mandatory:

```cpp
bme.setFilter(BME_FILTER_MODE);   // FILTER_COEF_16 == 0x04
ok = bme.begin(BME280_ADDR);
```

- Modes: `FILTER_DISABLE=0x00`, `FILTER_COEF_2=0x01`, `FILTER_COEF_4=0x02`, `FILTER_COEF_8=0x03`,
  `FILTER_COEF_16=0x04`.
- The library default is already ×16, so the explicit call is behavior-preserving — it makes the
  filter visible and tunable in one place (`BME_FILTER_MODE` in `sensors.cpp`) instead of relying on
  an implicit library default.
- The filter smooths temperature/pressure at the cost of slower response; register `0xF5` also carries
  standby time: `(_standby_time << 5) | (_filter_coef << 2)`.
- If the GyverBME280 version changes, re-check the `FILTER_*` values against `GyverBME280.h`.

---

## 2. Verification without hardware or a toolchain

At the time of the feature work the environment had no `arduino-cli`, no PlatformIO, no ESP32 core,
no Arduino libraries, and no tests. These workarounds gave useful coverage anyway. A real toolchain
was later installed and the firmware compiled for real (results in §1.14–§1.16); the fallbacks below
remain the record of how the features were verified before that.

### 2.1 Tri-verification of the LUT (docs ⇄ code ⇄ formula)

A Python script parses the `CURVE_LUT` from both `docs/light-curve.md` and `light.cpp`, recomputes the
expected 256 values from the formula (comment-stripped parsing! the table contains `// 0..15` column
label comments), and compares all three. This caught nothing by luck, but it is a cheap guard against
copy/paste and rounding drift.

```python
assert doc_lut == code_lut == formula_lut   # 256/256
assert code_lut[0] == 0 and code_lut[127] == 76 and code_lut[128] == 77 and code_lut[255] == 255
assert all(code_lut[i] <= code_lut[i+1] for i in range(255))
```

### 2.2 Mock compilation with stub headers

`g++ -std=c++11 -Wall -Wextra -fsyntax-only` on the real `.cpp`/`.ino` files, with hand-written
stubs for `Arduino.h`, `Wire.h`, `WiFi.h`, `LittleFS.h`, `SaverFile.h`, `GyverBME280.h`,
`GyverDS3231.h`, `SettingsGyver.h`, `Adafruit_AHTX0.h`.

- Catches missing includes, wrong signatures against the *expected* API, typos, `static_assert`
  failures, format-argument mistakes.
- The stub only has to declare what the code touches: `millis()`, `delay()`,
  `ledcAttachChannel`/`ledcWriteChannel` (core 3.x API; before §1.14 it was `ledcSetup/AttachPin/Write`),
  a `Serial` with `println/print/printf`, Wi-Fi calls, `SaverFile` as a template ctor,
  `Builder` widget methods (including both `uint8_t*` and `uint32_t*` Slider overloads), etc.
- Requires compiling the `.ino` with `-x c++`.
- **Harness quirk discovered:** each `bash` tool call runs in a fresh sandbox — files written to
  `/tmp` in one call are gone in the next. Create stubs and compile in the **same** invocation
  (`D=$(mktemp -d); …; rm -rf "$D"`).
- It is **not** a substitute for a real compile against the actual libraries; treat as a syntax/API
  smoke test. Since 2026-11-04 a real `arduino-cli` build is available (§1.14) and is the primary
  build gate; the stub approach remains useful when no toolchain is present.

### 2.3 Host-side layout and default checks

Compiling a tiny program that includes the *real* `config.h` (with the `Arduino.h` stub) verifies
ABI-sensitive assumptions:

```
sizeof(DaySchedule)=3  sizeof(Config)=28
defaults: enabled=1 snooze=10 fade=30000; all 7 days 07:30/enabled -> PASS
```

This matters because `SaverFile` reads/writes the struct raw — an accidental reorder or added field
changes the binary format (hence the version bump).

### 2.4 Behavioral simulation of the whole loop

A Python port of `loop()` (1-second ticks, exact `millis()` snooze math, injected `tap`/`hold`/master
events) ran the scheduler matrix and, after the tap-policy change (§1.12), the gated-tap matrix:
tap idle (light off) → no-op, schedule fires at 07:30; tap 07:29 (off) → no-op, 07:30 fires; tap
during the scheduled fade → light off + snooze; tap at 07:31 with the lamp fully on → light off +
snooze; second tap while light off (snooze pending) → no-op; tap after the snooze fired (lamp on) →
new snooze; hold during fade/snooze → skip today + next-day re-arm; master off + tap with light off
→ no-op. All pass. The 1-second port plus real `millis()` deadline semantics is enough to catch
ordering bugs (e.g. a snooze deadline landing exactly on a scheduled minute, §1.6) that a hand-check
misses.

### 2.5 Upstream API verification by raw source

Instead of guessing library semantics, the relevant upstream files were fetched directly:

| Assumption | Evidence |
|---|---|
| `Button::setHoldTimeout(uint16_t)` (600 default, 4000 max) | `GyverLibs/EncButton/src/core/VirtButton.h:117` |
| hold never also clicks | `VirtButton.h:219` (`click()` requires `EB_HLD` unset) |
| `uint8_t*` accepted by Settings widgets | `GyverLibs/Settings/src/core/AnyPtr.h:40` (`AnyPtr(unsigned char*)`) |
| Saver ignores mismatched version/size and writes defaults | `GyverLibs/Saver/README.md` (begin/version sections) |
| `weekDay()` is ISO 1=Mon…7=Sun | `GyverLibs/Stamp/README.md` (`Datime::weekDay`, `dateToWeekDay` "пн 1..вс 7") |
| Settings serializes labels synchronously | `Settings/src/core/builder.h` (`_beginWidget` → `(*p)[Code::label] = label`) |
| `Tabs` is not a container | `Settings/src/core/builder.h` / `containers.h` |

With `curl -fsSL https://raw.githubusercontent.com/GyverLibs/<lib>/main/<path>`, this is doable even
without cloning the repos.

### 2.6 Label-uniqueness simulation

Expand the `snprintf` templates for all 7 day names plus the static labels, then assert
`len(labels) == len(set(labels))` for widget labels and group titles, and no label/title collisions.
Fast regression check if another widget is added later.

---

## 3. Release state and related docs

- History: `ed8c201` (user's baseline import) → `e97cec3` (three features + curve) → `2cf0285`
  (gated tap + technical notes) → `84c65d0` (`.gitignore`: `libs/`) → compile-fix + docs-sync
  release (2026-11-04: ESP32 core 3.x LEDC API, docs split) → AHT humidity + BME280 filtration
  release (2026-11-04; see `git log`). All pushes were fast-forward; HEAD == origin/main after each
  release.
- Release mechanics (explicit-path staging, secret scan, push verification, GPG-signing sandbox
  workaround, unsigned-commit disclosure) and all agent-orchestration patterns now live in
  [`lead-hacks.md`](lead-hacks.md).
- The implementer's task-board lifecycle and phase conventions are in [`workflow.md`](workflow.md).

---

## 4. Gotchas worth remembering

- Don't change a raw-struct `SaverFile` payload without bumping its version char; padding bytes are
  part of the format.
- Settings keys widgets by label → duplicate labels silently collide; uniqueness matters more than
  pretty wording.
- `Tabs` is a value widget, not a container, in current Settings.
- EncButton v3: tick exactly once per loop; events survive until the next tick; hold event fires at
  the threshold while pressed, not on release; a hold cannot produce a click.
- A tap is only meaningful while the light is on: gate it on `light_isOn()`, not on button state.
  Otherwise a tap in the dark creates a pending snooze with no visible cause (and then suppresses
  the scheduled event right after the user expected it to fire).
- `millis()` deadlines: always compare with unsigned subtraction (`now - start >= period`), never
  `now >= start + period`.
- LUT indexing: do the `elapsed * 255` multiply in `uint32_t` (not `uint16_t`), and handle the exact
  final value in a separate branch.
- 8-bit PWM cannot go below `1/255`; the first visible step is a real perceptual jump on a dim strip.
- ESP32 core 3.x removed the legacy LEDC API (`ledcSetup`/`ledcAttachPin`/`ledcWrite(channel,…)`):
  use `ledcAttachChannel()`/`ledcWriteChannel()` (§1.14). Stub-based checks can hide this because the
  stub is whatever you wrote — only a real compile proves it.
- AHT10/AHT20: `Adafruit_AHTX0::getEvent()` busy-waits ≈80 ms (`while(busy) delay(10)`), so it must
  not be called from `loop()` — use the raw-Wire trigger→poll→read machine (§1.15).
- BME280 IIR: `setFilter()` must come **before** `begin()` — the coefficient is written to register
  `0xF5` only during `begin()` (§1.16); the library default is already ×16.
- `sensors_tick()` gating means the scheduler is inactive when the RTC is unsynced — fallbacks are
  never used for triggering, only for safe indexing.
- In this harness, each `bash` call gets a fresh `/tmp`; do multi-file experiments in one invocation.
- Release/git-specific gotchas (GPG signing, push verification) are in [`lead-hacks.md`](lead-hacks.md).

---

## 5. Quick reference

**Config (28 bytes):** master `enabled`, `snoozeMinutes` (1–60, default 10), `days[7]`
(`0=Mon … 6=Sun`, each hour/minute/enabled, default 07:30/on), `fadeDuration` (0–60000 ms, default
30000).

**Curve:** `d(p)=p^1.7369656`; `LUT[128]=77` → ≈30.2 % duty at 50 % progress; `LUT[0]=0`,
`LUT[255]=255`; first nonzero `LUT[8]`.

**Light state:** `light_active()` = fade running (false once the lamp reaches 255);
`light_isOn()` = on flag, true from `light_startFadeIn()` (including after the fade completes) until
`light_stop()`. The tap handler postpones only when `light_isOn()` is true.

**Scheduler condition:**

```
config.enabled && day.enabled && !alarmTriggered && !snoozePending &&
sensors_date_key() != skipDateKey &&
hour == day.hour && minute == day.minute
```

**Behavior matrix (summary):**

| Input | Effect |
|---|---|
| tap, light on/fading | light off, previous snooze cancelled, fade in `snoozeMinutes` later; does not cancel today's scheduled event |
| tap, light off | no-op: no stop, no snooze start/restart; log line only; the schedule fires normally later |
| tap while a snooze is pending (light off) | no-op (cannot restart/extend the countdown) |
| hold ≥ 2 s | light off, snooze cancelled, skip today only (auto re-arm tomorrow) |
| web «Розжечь сейчас» | immediate fade; no effect on snooze/skip/alarm state |
| master switch off | scheduled events disabled; tap-snooze still works if the light is on (manual override) |
| per-day switch off | that weekday skipped; other days unaffected |

**Sensors:** temp/pressure from BME280 (IIR ×16, `setFilter()` before `begin()`); humidity from
AHT10/AHT20 at `0x38` via a non-blocking trigger→poll→read machine (2 s cadence, 100 ms polling,
500 ms timeout), BME280 fallback when AHT is missing or data is >10 s old; the 1 Hz RTC gate and the
scheduler are untouched (§1.15–§1.16).

**Local libraries:** `libs/` (git-ignored) contains Settings 1.4.3, Saver 1.1.0, EncButton 3.7.6,
GyverBME280 1.5.4, GyverDS3231 1.2.1 plus 9 transitive deps; exact versions/SHAs in the ignored
`libs/README.md` (§1.13). AHT uses the installed Arduino libraries Adafruit AHTX0 2.0.6 +
Adafruit BusIO + Adafruit Unified Sensor 1.1.15 (not part of `libs/`).

**Build (real toolchain, 2026-11-04):** `arduino-cli 1.5.1` + core `esp32:esp32 3.3.0`:
`arduino-cli compile --fqbn esp32:esp32:esp32 LightScheduler/` → exit 0. Latest build (after AHT
humidity + BME280 filtration): flash 1 097 051 / 1 310 720 B (83 %), RAM 49 028 / 327 680 B (14 %),
no project warnings. Requires ESP32 core ≥ 3.0 and the Adafruit AHTX0/BusIO/Unified Sensor libraries;
only the `compile-checker` agent runs `arduino-cli` (§1.14).

**Hardware checklist (still pending — build verified, device not):** Saver `'B'` migration from an old `/config.cfg`;
DS3231 weekday/date mapping; 2 s hold boundary + debounce; tap-vs-hold feel; LED curve low-end and
perceived smoothness; web UI day-group rendering + snooze persistence after reboot; power-cycle
clears RAM snooze/skip (expected); **new:** AHT10/AHT20 present at `0x38` and humidity in the UI,
AHT/BME cross-talk on the shared bus, BME280 IIR ×16 smoothing vs response time, AHT cadence
responsiveness (2 s), fallback when the AHT is unplugged at runtime.

**Useful commands**

```bash
# verify LUT docs ⇄ code ⇄ formula (run from repo root)
python3 - <<'PY'
import math, re

def parse_block(text):
    lines = text.splitlines()
    start = next(n for n, l in enumerate(lines) if 'CURVE_LUT' in l and '[' in l and '{' in l) + 1
    out = []
    for l in lines[start:]:
        l = l.split('//')[0]
        if l.strip().startswith('};'):
            break
        out += [int(x) for x in re.findall(r'\b\d{1,3}\b', l)]
    return out

doc = parse_block(open('docs/light-curve.md', encoding='utf-8').read())
cpp = parse_block(open('LightScheduler/light.cpp', encoding='utf-8').read())
g = math.log(0.30) / math.log(0.50)
exp = [int(math.floor(255 * (i / 255.0) ** g + 0.5)) for i in range(256)]
print('PASS' if doc == cpp == exp else 'FAIL', len(doc), doc[128])
PY

# build with the real toolchain (executed only by the compile-checker agent)
arduino-cli compile --fqbn esp32:esp32:esp32 LightScheduler/

# release state checks
git status --short
git rev-parse HEAD origin/main
git ls-remote origin refs/heads/main
```
