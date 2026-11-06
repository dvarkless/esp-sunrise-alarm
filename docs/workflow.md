# Implementation workflow — teammate `impl-core`

Brief description of how the firmware work for this project was carried out: task-board
protocol, phased delivery, verification without a toolchain, and reporting. Engineering
details of the code itself are in [`technical-notes.md`](technical-notes.md); the curve
research is in [`light-curve.md`](light-curve.md); the project state is in
[`PROJECT.md`](../PROJECT.md).

## 1. Task lifecycle

1. `team_task_get` — read description, acceptance criteria, write scope, blockers.
2. `team_task_update(action="claim", expected_revision=…)` — work only after claiming;
   never bypass a `blocked_by` dependency.
3. Read `PROJECT.md` and **every** file touched by the task before editing.
4. Implement the smallest coherent diff (Russian comments/UI labels, non-blocking loop).
5. Verify statically (section 3), mark the task complete, then `send_message` to the Lead
   with files, decisions, evidence and risks. The board has no comment field — the report
   carries the details.
6. Stop after the agreed phase and wait for the Lead's "continue".

## 2. Phases used

| Phase | Tasks | Result |
|---|---|---|
| A | task-1, task-2 | `docs/light-curve.md`; `Config.days[7]`, `snoozeMinutes`; `sensors_weekday()`/`sensors_date_key()`; Saver version `'B'` |
| B | task-3, task-4 | per-weekday trigger; 7 nested day groups in the web UI |
| C | task-5, task-6 | integer `CURVE_LUT` fade; tap-snooze + 2 s hold skip |
| post-release | task-11 | tap is a no-op unless the light is on (`light_isOn()`) |

The Lead reviewed/approved the diff after each phase. Docs (task-7) and QA (task-8)
were owned by the Lead, not by the implementer.

## 3. Verification without hardware or a toolchain

The workspace originally had no Arduino core/libraries, no `arduino-cli`/PlatformIO and no device, so
all verification was static:

Since 2026-11-04 a real toolchain is available (`arduino-cli 1.5.1`, cores `esp32:esp32 3.3.0` and
`esp8266:esp8266 3.1.2`, Gyver libraries installed). The **`compile-checker`** teammate is the only
agent allowed to run builds, and only when necessary — after a code change to verify or when the Lead
explicitly asks; no flag experiments and no duplicate builds. Both targets must pass:
`arduino-cli compile --fqbn esp32:esp32:esp32 LightScheduler/` and
`arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 LightScheduler/`. It fixes compile errors and
reports pass/fail + compiler errors only. All git commands and code diffs
belong to the **`git-release`** agent; docs stay with the Lead. Static checks below remain useful for
semantics and behavior, which a compile cannot verify.

- **Upstream API checks** — fetched raw sources from GyverLibs to confirm semantics instead
  of guessing: `SaverFile` header match (version/size), `Datime::weekDay()` ISO 1–7,
  Settings `AnyPtr`/containers (`Tabs` is a value widget), EncButton hold/click exclusivity.
- **Mock compilation** — `g++ -std=c++11 -Wall -Wextra -Werror -fsyntax-only` with hand-written
  stub headers (`Arduino.h`, `WiFi.h`, `SettingsGyver.h`, `EncButton.h`, …), compiling the
  real sources; the `.ino` is compiled with `-x c++`. Stubs are created and used in the
  **same** bash call (each call gets a fresh `/tmp`).
- **Host-side unit tests** — compiled the real `light.cpp`/`config.h` against stub
  `millis()`/`pwm_setup()`/`pwm_write(...)` to check fade semantics and `light_isOn()`/
  `light_active()` transitions.
- **Behavioral simulation** — a Python model of `loop()` (1 s ticks, real `millis()` deadline
  math, injected tap/hold/web/master events) runs the schedule, snooze, skip and gated-tap
  scenario matrices.
- **Data checks** — LUT tri-verification (`docs ⇄ code ⇄ formula`), struct layout/defaults,
  web-UI label uniqueness, stale-field greps, no-blocking-in-loop greps.

## 4. Scope and hygiene rules

- Edit **only** files in the claimed task's write scope; if a change needs another file
  (`config.h`, `sensors.*`, …), stop and report instead of editing it.
- Git commands (`status/diff/add/commit/push`) and code diffs are owned by `git-release`; never run
  git or produce diffs in implementer/compile agents.
- No `git commit`/`checkout`/`push` — changes stay in the working tree; release is a
  separate task.
- No toolchain/package installation; the toolchain is pre-installed — do not modify it or the libraries.
  No hardware available → hardware-only checks are explicitly flagged as pending.
- Small diffs, no dead code, no unrelated refactors; Russian comments and web-UI labels;
  keep `loop()` non-blocking (no `delay()`/blocking waits in loop or handlers).
- Platform differences live only in `board.h` (dispatch/pins), `light.cpp`
  (`pwm_setup()`/`pwm_write()`), the `webui.cpp` WiFi include and the `config.cpp` LittleFS mount
  (the two cores' `FS::begin()` signatures differ); keep both ESP32 and ESP8266 targets building.

## 5. Report template

`task-N complete` + files changed + what/why + verification evidence (numbers, PASS counts)
+ open questions/risks (including hardware-only gaps). Keep it self-contained, because the
Lead and QA rely on it as the task's completion record.
