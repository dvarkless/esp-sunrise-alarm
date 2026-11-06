# Code style — writing firmware code that matches this repository

Purpose: a compact, evidence-backed style guide for any agent or developer who edits the firmware.
Follow it unless a task explicitly says otherwise. The project is a small ESP32/ESP8266 Arduino
sketch built on **GyverLibs** (vendored read-only in git-ignored [`libs/`](../libs/README.md)); firmware code
should look and think like the libraries it uses. Architecture and feature facts live in
[`PROJECT.md`](../PROJECT.md); engineering techniques in [`technical-notes.md`](technical-notes.md);
process/delegation in [`workflow.md`](workflow.md) and [`lead-hacks.md`](lead-hacks.md).

Sources of this guide: full read of the 14 vendored libraries in `libs/*/src` (21k LOC, ~170 example
sketches) plus the firmware in `LightScheduler/`. Numbers below are measured over `libs/*/src` unless
stated otherwise. Path shorthand in this file: `<Lib>/src/...` means `libs/<Lib>/src/...`, and
`config.h`, `light.cpp`, etc. mean the firmware files under `LightScheduler/`.

## 0. Hard rules (apply always)

- **Language:** comments, log messages and web-UI labels are **Russian**; identifiers, docs, commit
  messages and code structure are English. Comments explain *why* and units/ranges, not restate code.
- **Formatting:** 4-space indent, no tabs, K&R braces (opening brace on the same line), 3-space
  indent for class access specifiers. See §3.
- **Non-blocking `loop()`:** no `delay()`, no `while` waits, no blocking I/O in `loop()` or in any
  handler. Blocking waits are allowed only inside `setup()` (e.g. the Wi-Fi connect loop in
  `webui.cpp`). Everything periodic is a `millis()`-based ticker/state machine.
- **Modules, not classes:** firmware is split into `.h`/`.cpp` module pairs exposing free functions
  named `module_action()` (`config_setup`, `light_update`, `sensors_tick`, …). No singletons, no
  event bus, no framework. `loop()` in the `.ino` orchestrates.
- **Read-only dependencies:** `libs/` is a git-ignored upstream checkout — never edit, "fix" or
  reformat it. If an API behaves unexpectedly, adapt the caller.
- **No tests/tooling** unless the user asks; build verification is only through the `compile-checker`
  role — both targets must pass, ESP32 `arduino-cli compile --fqbn esp32:esp32:esp32 LightScheduler/`
  and ESP8266 `--fqbn esp8266:esp8266:nodemcuv2` — git only through `git-release`. Do not add CI,
  pytest, PlatformIO, CMake, scripts or package managers.
- **No secrets in tracked files:** `WIFI_SSID`/`WIFI_PASS` may be filled locally in the working tree,
  never in a diff/commit/docs; secret-scan diffs for `PASSWORD|TOKEN|API_KEY|PRIVATE KEY|WIFI_PASS`.
- **Keep docs in sync** with behavior changes (`PROJECT.md` + `LightScheduler/README.md` + relevant
  `docs/*.md`); grep for retired field names/labels after refactors.

## 1. Files, folders, includes

The Arduino sketch folder is flat (`LightScheduler/*.h/*.cpp` next to the `.ino` — mandatory).

- One concern per module pair: `config.*` (pins/struct/state), `sensors.*` (BME280/AHT/DS3231 +
  cache), `light.*` (PWM fade), `button.*` (EncButton wrapper), `webui.*` (Settings UI + Wi-Fi),
  `log.*` (Serial + web log). The `.ino` holds `setup()`, `loop()` and only top-level loop state.
- `#pragma once` is the **first line** of every header (144/145 lib headers; the only old-style
  `#ifndef` guard is the legacy `GyverBME280.h` — do not imitate it).
- `board.h` is the only platform-dispatch header: `config.h` includes it, it defines
  `LS_BOARD_ESP8266` / `LS_BOARD_ESP32` and the per-board `PIN_*`, so every other module sees the same
  pin names regardless of the chip family.
- **Include order.** In project `.cpp`: own header first, then project headers, blank line, then
  angle-bracket system/library headers (`sensors.cpp:1-9`, `webui.cpp:1-8`). In `.ino`:
  `<Arduino.h>` first, then project headers. Inside third-party-style modules (if ever written):
  `<Arduino.h>`, blank, quoted local headers.
- Headers declare the module contract: `extern` globals, function prototypes with a short trailing
  Russian comment (units/return semantics), `static const` tables when needed. Definitions go to
  `.cpp`; only trivial one-line wrappers are `inline` in a header (`log.h:14-23`).
- Include only what you use; prefer specific headers (`<SaverFile.h>`, `<SettingsGyver.h>`) over
  umbrella ones. Vendor libs use umbrella headers (`GTL.h`, `StringUtils.h`) internally — not a
  firmware pattern.
- File names: firmware modules lowercase (`sensors.cpp`); lib classes PascalCase matching the class
  (`GyverDS3231.h`), lib internals in lowercase subfolders (`core/`, `utils/`, `array/`).
- Every file ends with exactly one newline; no trailing whitespace (the vendored libs fail this in
  ~120 files — their defect, not a model).

### Header skeleton (project convention)

```cpp
#pragma once
#include <Arduino.h>

// ===================== Свет: плавный розжиг через ШИМ (LEDC/analogWrite) ===============
// Кривая розжига перцептуальная: d = p^1.7369656 (50 % прогресса → 30 % скважности).
void light_setup();        // pwm_setup() (LEDC на ESP32, analogWrite на ESP8266), свет выключен
void light_startFadeIn();  // запустить плавный розжиг с нуля до максимума
void light_stop();         // мгновенно погасить и остановить розжиг
```

## 2. Naming

| Kind | Convention | Example (evidence) |
|---|---|---|
| Classes / structs | `PascalCase` | `SaverFile`, `DaySchedule`, `Config` (`Saver/src/SaverFile.h:9`, `config.h:34`) |
| Lib methods | `camelCase`, verb-first | `begin()`, `tick()`, `readTemperature()`, `setFilter()` |
| Method role prefixes | `begin`/`init`, `tick`, `set`, `get`, `read` (hardware sample), `to` (convert), `is`/`has`, `on` (callback), `attach`/`detach` | `GyverBME280.h:52-64`, `StampKeeper.h:84-97` |
| Lib private/protected members & helpers | leading underscore `_name` | `_data`, `_size`, `_tmr`, `_crcByte()` (`Saver/src/Saver.h:53-100`); zero `m_`/trailing-underscore |
| Project module functions | `snake_case` with module prefix | `light_isOn()`, `sensors_date_key()`, `config_save()` |
| Project module state | `static` in `.cpp`, camelCase, **no** underscore | `static bool fading`, `static uint32_t fadeStart` (`light.cpp:31-34`) |
| Globals exported from a module | `extern` in `.h`, defined once in `.cpp` | `extern Config config;` / `Config config;` (`config.h:49`, `config.cpp:9`) |
| Macros / compile-time config | `UPPER_SNAKE` | `PIN_PWM_LED`, `PWM_FREQ`, `FADE_STEP_MS` (`config.h:5-14`) |
| File-scope constants/tables | `static const` + `UPPER_SNAKE` | `CURVE_LUT`, `WEEKDAY_NAMES`, `AHT_PERIOD_MS` (`light.cpp:10`, `sensors.cpp:30-35`) |
| Event vs state | bare noun = one-shot event, `-ing` = current state | `press()` / `pressing()` (`EncButton/src/core/VirtButton.h:198-254`) |
| Callback typedefs | `typedef void (*NameCallback)(args);` | `ChangeCallback`, `SecondCallback` (`GyverDB/src/GyverDB.h:19`, `Stamp/src/StampKeeper.h:14`) |
| Event flags | `#define LIB_X (1 << n)` mirrored by `enum class` | `EB_CLICK (1 << 4)` ↔ `EBAction::Click` (`EncButton/src/core/VirtButton.h:13-37`) |

Names carry units/semantics: `*Ms`, `*Timeout`, `dur`, `elapsed`, `idx`, `hour`, `dayIdx`. Public
data members and struct fields have **no** underscore (`Stamp::unix`, `Config.days`,
`DaySchedule.hour`); only implementation-private state gets `_`.

## 3. Formatting (mechanical — match exactly)

- 4 spaces per level, no tabs, LF, UTF-8 (Cyrillic comments are normal in UTF-8).
- K&R braces: `class X {`, `void f() {`, `if (cond) {`, `} else {`, `} else if (...) {`,
  `switch (x) {` + `case:`; closing brace at the parent indent. No Allman style (≈1700 same-line
  braces vs 1 exception in libs). One-line bodies are fine for trivial getters and guard clauses:
  `if (!len) return false;` (173 in libs) — do not split them onto two lines.
- Class access specifiers are indented **3 spaces**, members 4; nested classes add 4 (7):
  ```cpp
  class SaverFile : public Saver {
     public:
      template <typename T>
      SaverFile(fs::FS& fs, const char* path, T& data, ...) : ... {}

     private:
      fs::FS* _fs;
  };
  ```
  (130 `   public:`, 94 `   private:`, 24 `   protected:` at 3 spaces in `libs/*/src` — no other indent.)
- `}  // namespace sets` / `}  // namespace gtl` — closing-brace comment for namespaces only.
- Spaces: `if (`, `for (`, `while (`, `switch (` always with a space (0 `if(` occurrences);
  `return a + b;`, `f(a, b)`; pointer/reference binds to the type: `const char* s`, `Config& cfg`.
- Align related declarations/initializers when it improves scanning: struct fields (`config.h:34-46`),
  static constants (`sensors.cpp:30-35`), `#define` values (`config.h:5-8`), consecutive assignments.
  Do not invent alignment for unrelated lines.
- Section separators in longer files: `// ===================== Section =====================`
  (`config.h`, `light.cpp`, `libs/Saver/src/Saver.h:4`); use them to structure a file, not around
  every block.
- Wrap long signatures/expressions with 4-space continuation, one param per line if needed
  (`SaverFile.h:11-16`); ctor init-lists may start a new line as `        : _a(a), _b(b) {`.
  Target ≤ 100 columns (GTL's discipline); allow longer for macros, generated tables and
  many-parameter signatures.
- Blank lines: one between functions, between logical blocks inside a function, after the include
  block; exactly one, never two. Comments go directly above the line/block they describe; trailing
  comments only for short unit/range notes on constants/fields (`uint8_t hour = 7;  // 0-23`).

## 4. Comments and documentation

- **Russian, `//` only** — dominant in every modern lib (17–181 Cyrillic comment lines each); the
  legacy `GyverBME280` (English comments, `/* */` blocks) is an outlier, not a model. Doxygen exists
  only in `StringUtils` (~170 tags across 21k LOC) — **not required**; use plain `//`.
- **No per-file author/license/version banners** in new code (only the legacy BME280 header has one).
  Metadata lives in `README`/`library.properties`/`LICENSE`. Firmware module headers start with a
  short purpose block instead:
  ```cpp
  // ============================================================================
  // Smart Scheduled Light Controller (ESP32 / ESP8266)
  // Главный файл: setup() и неблокирующий loop().
  // ============================================================================
  ```
- One line above a declaration is the default doc: imperative, with units, ranges, defaults,
  ordering/preconditions: `// установить таймаут удержания, умолч. 600 (макс. 4000 мс)`
  (`VirtButton.h:116`), `// вызывать в loop` (`GyverDS3231.h:38`). Extra params documented inline as
  `name - описание` (lib style) or in the header comment paragraph.
- Tag duplicated switch/enum/table mirrors with a searchable comment (libs use `// TABLE_TYPES`,
  `libs/Table/src/core/table_t.h:7,32`) so a future edit can grep both sides.
- Document the *why* when code encodes a decision: keep Saver format-letter migration block
  (`config.cpp:11-20`) and LUT provenance comment (`light.cpp:4-9`) current.
- `// todo` lowercase for known imperfections; no commented-out implementation blocks (legacy debt in
  old libs — do not copy); keep documented alternative config like `// #define EB_NO_FOR // …`.
- Layout in a `.cpp`: public functions first (grouped with banner comments), private/static helpers
  last (`libs/GyverBME280/src/GyverBME280.cpp:3,15,62,123,135` is the model).

## 5. Language level and idioms

- The vendored libs are portable from AVR to ESP32 and stay C++11-compatible, guarding C++11-only
  features with `#if __cplusplus >= 201103L` (`StringUtils/src/utils/Text.h:25-33`). The ESP32 core
  3.3.0 actually compiles with `-std=gnu++2a`, RTTI off, exceptions on.
- **Firmware code stays within a C++11 subset**, like the libs. No STL containers, no `std::string`,
  no exceptions thrown, no RTTI, no smart pointers. `std::function` only if portability is gated like
  `EncButton` does (`#if !defined(EB_NO_CALLBACK) && (defined(ESP8266) || defined(ESP32))`,
  `EncButton/src/core/VirtButton.h:8-11,108-112`); firmware uses plain function pointers
  (`sett.onBuild(build)`).
- Used freely: templates, `constexpr`, `static_assert`, `enum class`, `nullptr`, `override`,
  `noexcept` (move/swap), references, `= delete`/`= default`, `explicit` on converting ctors and
  `operator bool`, NSDMI defaults at declaration (`bool _synced = false;` `Saver/src/Saver.h:93-100`).
  `auto` is rare in libs (4 uses for opaque handles) — prefer explicit types.
- `static_assert` for binary layout/size contracts: `sizeof(Header) == 5` (`Saver/src/Saver.h:46`),
  `sizeof(CURVE_LUT) == 256` (`light.cpp:29`).
- `enum class` with explicit underlying type for domain enums (`enum class EBAction`,
  `enum Status : uint8_t` in Saver with per-value Russian comments — `Saver/src/Saver.h:6-13`);
  PascalCase enumerators.
- Namespaces (lib pattern): short lowercase `su`, `gdb`, `ghttp`, `tbl`, `sets`, `gtl`, closed with
  `}  // namespace X`; public class re-exported to global scope via the umbrella header
  (`StringUtils/src/StringUtils.h:20-32`). Firmware needs no namespace — module prefixes suffice.
- Compile-out config macros (reusable-module pattern): optional feature switches as commented
  `// #define LIB_NO_X` lines at the top of the header + `#ifndef` guards for their purpose, and
  `#ifndef X / #define X default / #endif` blocks immediately above first use
  (`GyverDB/src/GyverDB.h:12-15`, `StringUtils/src/utils/Value.h:9-11`). Platform dispatch uses an
  `#if defined(__AVR__) … #elif defined(ESP8266) … #elif defined(ESP32) … #else` chain
  (`GyverIO/src/gio/gio.h:9-17`). The firmware uses exactly this chain in `board.h`, ESP8266 first:
  it normalizes to `LS_BOARD_ESP8266` / `LS_BOARD_ESP32` and keeps the board-specific pins (`PIN_*`)
  there; `config.h` keeps timings/PWM constants as `#define`. Platform `#if` is allowed only in the
  four localized places — `board.h` (dispatch/pins), `light.cpp` PWM helpers
  (`pwm_setup()`/`pwm_write()`), the `webui.cpp` WiFi include, and the `config.cpp` LittleFS mount
  (`FS::begin()` signature differs) — feature logic always uses `LS_BOARD_*`/`PIN_*` and stays
  platform-agnostic.
- C-style casts dominate in the libs and in firmware (`(uint32_t)`, `(unsigned)`); `static_cast` is
  rare. Use C-style casts for register/byte work, and explicit casts for varargs (`%u` → `(unsigned)`,
  `%lu` → `(unsigned long)`, `technical-notes.md:267-268`).

## 6. API and logic patterns to emulate

- **Lifecycle:** constructors configure only — no hardware/FS I/O there (global objects are built
  before `setup()`). `begin()`/`init()` acquires resources and returns `bool` (or a `Status` enum when
  the caller must distinguish outcomes); `tick()` runs in `loop()` and returns an event (`bool` per
  second, action flags, write status). Cheap `const` getters; cache values to avoid repeated bus
  access (`sensors.cpp:17-21`). Config setters that must precede init say so in the comment
  (`bme.setFilter()` before `begin()`; `sensors.cpp:48-51,86-88`).
- **Bus/device injection:** new modules should accept `TwoWire*`/`Stream&`/`Print&` like
  `GyverDS3231::begin(TwoWire* wire = &Wire, uint8_t addr = 0x68)`, not hard-code global `Wire`
  (legacy BME280 hard-codes it; `GyverBME280.cpp:24`).
- **Multi-outcome results** use a named `Status` enum with explicit values and trailing comments
  (`Saver::Status {None, Read, Write, Default, Grow, Error}`, `Saver/src/Saver.h:6-13`), not magic
  ints. Failure is `false`/sentinel + safe default; never an exception. Validity wrappers provide
  `explicit operator bool()` (`TableFileStatic.h:23`, `Text.h:189`).
- **Time:** `millis()` deadlines with wraparound-safe subtraction `(uint32_t)(now - start) >= timeout`;
  never `delay()` in `loop()`. State machines advance one step per call (AHT10/20 in `sensors.cpp`
  is the reference: trigger → poll status → read → cache; one 1 Hz gate via `sensors_tick()`).
- **Storage:** versioned packed header + CRC (`__attribute__((packed)) struct Header`,
  `static_assert` size), atomic replace, `beginGrow()` for compatible growth
  (`libs/Saver/src/Saver.h`, `SaverFile.h`). Bump the version letter on incompatible struct changes
  and document the migration in the comment block (`config.cpp:11-20`).
- **Output is a sink, not a print:** libs expose `Print&`/`writeTo(T&)`/`dump(Print&)` and never call
  `Serial`. Firmware centralizes output in `log.h`/`log.cpp` (`log_print*`, `app_log_printf`); modules
  never call `Serial.*` directly.
- **Config UI:** wire `Config` fields by pointer into `sets::Builder` widgets; unique labels page-wide;
  `char label[32] + snprintf` for dynamic labels; `onUpdate(update)` for polled values by widget ID;
  `b.Log(id, logger, "Лог")` for the web log (`webui.cpp`, `log.h`).
- **Text/strings:** `const char*`/`F()` for literals (124 `F(` uses in libs; firmware uses `F("…")`
  everywhere); `String` only via Arduino where a library API returns it (`rtc.dateToString()`), never
  concatenation in `loop()`. Non-owning text views (`Text`) are a lib idea — don't pull into firmware.
- **Fixed buffers:** stack arrays + `snprintf(buf, sizeof(buf), …)` (`log.cpp:9-13`,
  `webui.cpp:37-45`), `static const` tables in flash (`static const uint8_t CURVE_LUT[256]`,
  `static const ... PROGMEM` in libs), no `new`/`malloc` in firmware, no heap growth in `loop()`.
- **Integer math in hot paths:** no float in the fade/curve path; document overflow bounds when
  multiplying (`light.cpp:70-80`).
- **Proxy objects** (`operator[]` returning `gdb::Access`, `tbl::Row`/`Cell`) are the lib
  way to hook writes (`GyverDB/src/GyverDB.h:72-77`, `Table/src/Table.h:56-87`) — not needed in
  firmware, but don't return references to transient internals from module APIs either.

### Module skeleton (firmware)

```cpp
// sensors.h
#pragma once
#include <Arduino.h>

// ===================== Датчики: BME280 + AHT10/20 + DS3231 =====================
void  sensors_setup();   // Wire + BME280 + AHT10/20 + DS3231
bool  sensors_tick();    // вызывать в loop: вернёт true раз в секунду (обновит кэш)
uint8_t sensors_hour();  // текущий час по RTC (0-23)
```

```cpp
// sensors.cpp
#include "sensors.h"
#include "config.h"
#include "log.h"

#include <Wire.h>
#include <GyverBME280.h>

static float temp = 0.0f;  // кэш последнего измерения
```

## 7. Memory, performance, streaming

- Static allocation first: global module objects, fixed buffers, flash tables. Libs show three
  strategies for reusable containers — heap (`array`), caller/external buffer (`_ext`), embedded
  static buffer (`_static` template variant) — choose at compile time, no virtual dispatch
  (`GTL/src/{array,stack,fifo,lbuf}/`).
- When heap is truly needed (none in firmware today): allocate in small owned chunks, check failure,
  free in the same scope; the libs' budget is one block per call (`StreamReader.h:154-203`,
  `StreamWriter.h:49-112`). `String` growth reserves first (`contentType.reserve(50)`,
  `HeadersParser.h:20`; `s.reserve(200)`, `ServerBase.h:30`).
- Streaming over buffering: parse/forward in fixed blocks (default ~128 B), never read a whole
  payload into a String; reader/writer objects expose the data (`StreamReader::writeTo`,
  `ServerBase` request body). Not needed in firmware yet, but the principle applies to any future
  file/network work.
- Bit-level state: bit flags / `uint8_t` enums / packed structs for persisted/wire data; ISR-shared
  state `volatile` with short interrupt-free windows (lib pattern, `EncButton/src/core/flags.h`).

## 8. Error handling, logging, UI ranges

- Clamp/validate at boundaries instead of failing late: keep UI widget ranges as the contract
  (`0…23` hours, `0…55` minutes step 5, minutes `1…60`); code still guards `? : 1` for zero
  (`LightScheduler.ino:69,114`). The libs silently clamp with a documented comment (negative indices
  fold from the end, oversized strings truncated) — document the accepted trade-off.
- Log through the project wrappers, never raw `Serial.*` in modules: `log_println(F("…"))`,
  `log_println(info)` with a subsystem tag (`"Sensors: …"`, `"WiFi: …"`, `"Кнопка: …"`), and
  `app_log_printf("… %u …", (unsigned)v)` with explicit casts for varargs (`log.h:11-23`,
  `sensors.cpp:59-94`). Successful setup paths must say so; failures must say why.
- Library code is silent: 0 `Serial.` in lib `src/` (one doc-comment exception in StringUtils). Status
  is `bool`/`Status`; optional debug logging in libs uses a commented `// #define LIB_DEBUG` switch
  with an empty fallback macro (`GyverHTTP/src/utils/Client.h:17-23`). Firmware always logs via
  `log_*`; do not add a separate debug macro system.
- User-facing strings/UI labels/units are Russian and human ("Длительность (мин)", "Запуск
  будильника"); keep them unique and stable — Settings addresses widgets by label.
- Never assert/abort/throw in firmware; `static_assert` is compile-time only. Guard clauses first,
  work second (lib pattern).

## 9. What NOT to do (including library traits not to copy)

- Do not edit `libs/`; do not vendor lib code into git; do not "modernize" module public APIs.
- Do not introduce classes/singletons for firmware modules, dynamic allocation, `delay()` in
  `loop()`, blocking `while` waits in handlers, or float math in the fade path.
- Do not add ESP32/ESP8266 `#if` branches outside the four localized places (`board.h`, the
  `light.cpp` PWM helpers, the `webui.cpp` WiFi include, the `config.cpp` LittleFS mount); do not move
  board-specific pins back into `config.h`.
- Do not switch to Allman braces, tabs, 2-space indent (legacy BME280 examples), camelCase free
  functions, English user-facing strings, or `Serial.*` calls in modules.
- Do not use STL containers/`std::string`/exceptions/RTTI/smart pointers in firmware; do not add
  tests, CI, PlatformIO, CMake, formatters or lint tooling.
- Do not add per-file license/author banners, English-only comments, `NULL` (use `nullptr`),
  200+-column non-generated lines, commented-out implementation blocks, or files without a final
  newline — these are divergences inside the vendored libs, not the target style.
- Do not commit user Wi-Fi credentials, generated artifacts, `libs/`, or `TASKS.md` (git-ignored).
- Do not silently rewrite history or amend releases; release/git work goes through `git-release`.
- Do not duplicate docs in code comments; reference `docs/*.md` instead.
- Do not keep dead API without a marker: if an API must change, keep a delegating shim under a
  `// ===== DEPRECATED =====` banner or a `// legacy` comment for one transition period (lib pattern).

## 10. Reference map — read the lib when unsure

| Pattern | Where to look |
|---|---|
| Class/API style, packed header + CRC, versioned storage | `libs/Saver/src/Saver.h`, `SaverFile.h` |
| Settings builder/updater widgets, `sets::` namespace, containers | `libs/Settings/src/SettingsT.h`, `core/containers.h`, `core/builder.h` |
| Event flags + `enum class`, callbacks, debounce state machine | `libs/EncButton/src/core/VirtButton.h`, `Button.h` |
| `begin()`/`tick()`/getters, caller contract, no callbacks required | `libs/GyverDS3231/src/GyverDS3231.h`, `libs/GyverBME280/src/GyverBME280.h` |
| Header/impl split order — public first, private last | `libs/GyverBME280/src/GyverBME280.cpp` |
| Templates over virtual, container variants, `namespace gtl` | `libs/GTL/src/{stack,array,fifo,lbuf}/*`, `GTL/src/utils.h` |
| String views, `Text`, compile-time hashing `SH()`, umbrella re-exports | `libs/StringUtils/src/StringUtils.h`, `utils/Text.h`, `utils/hash.h` |
| Proxy access via `operator[]`, `namespace gdb`/`tbl` | `libs/GyverDB/src/GyverDB.h`, `libs/Table/src/Table.h` |
| Streaming/chunked parsing, no full-payload buffering | `libs/GyverHTTP/src/StreamReader.h`, `StreamWriter.h`, `libs/StreamIO/src` |
| Platform dispatch, pins, PWM differences | `LightScheduler/board.h`, `light.cpp` helpers, `docs/technical-notes.md` §1.17 |
| Project architecture and feature behavior | `PROJECT.md`, `docs/technical-notes.md` |
| Firmware build/verification rules and agent ownership | `docs/workflow.md`, `docs/lead-hacks.md` |

Version pins of the vendored libs: `libs/README.md` (Settings 1.4.3, Saver 1.1.0, EncButton 3.7.6,
GyverBME280 1.5.4, GyverDS3231 1.2.1, GTL 1.4.7, GyverDB 1.4.4, StringUtils 1.6.1, GyverHTTP 1.0.34,
BSON 3.0.1, Stamp 1.5.1, Table 1.3.6, StreamIO 1.0.5, GyverIO 1.4.2).

## 11. Final checklist before reporting a code change

1. Correct file(s): module pair flat in `LightScheduler/`, no edits under `libs/`.
2. `#pragma once` first; includes ordered; no unused includes; final newline; no trailing whitespace.
3. 4-space indent, K&R braces, 3-space access specifiers, aligned struct/constant blocks, ≤100 cols.
4. Russian comments; units/ranges/preconditions documented; doc `// =====` separators consistent.
5. Names follow the module-prefix / `_`-private / UPPER_SNAKE rules; no `m_`, no `NULL`.
6. `loop()` stays non-blocking; timers use wraparound-safe `millis()` subtraction; state machines
   advance one step per call.
7. New hardware/FS init in `begin()`-style setup (`*_setup()`), not in global constructors; status
   logged through `log_*`; no raw `Serial` in modules.
8. `F()` for literals; fixed buffers with `snprintf`; no heap; no float in the fade path.
9. Saver `Config` struct change ⇒ bump the format letter and update the migration comment + docs.
10. Docs updated (`PROJECT.md`, module README section, `docs/*.md`); no stale names/labels.
11. No secrets/artifacts staged; build verified only via `compile-checker` if needed; report files,
    decisions, evidence, risks.
12. Dual-platform check: platform `#if` only in `board.h`, the `light.cpp` PWM helpers, the
    `webui.cpp` WiFi include and the `config.cpp` LittleFS mount; both ESP32 and ESP8266 builds pass;
    board pins live in `board.h`, not `config.h`.
