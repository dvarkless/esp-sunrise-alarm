# Team Lead hacks — agent orchestration & release playbook

How this project was run as **Team Lead/PM with delegated agents**: task-board protocol, delegation
prompts, parallel-vs-serialized work, independent verification, release discipline, and the traps
found along the way. This file is process-only — firmware engineering is in
[`technical-notes.md`](technical-notes.md), the implementer's day-to-day workflow is in
[`workflow.md`](workflow.md), and the project facts are in [`PROJECT.md`](../PROJECT.md).

Current release state: `e97cec3` → `2cf0285` (gated tap + technical notes) → `84c65d0`
(`.gitignore`: `libs/`) → compile-fix + docs-sync release (2026-11-04: ESP32 core 3.x LEDC API) →
dual-platform ESP32/ESP8266 port (2026-11-06, commit `ffdb340`); HEAD == origin/main. The local
working tree keeps the user's Wi-Fi credentials in `config.h` uncommitted (committed copy: empty).

---

## 1. The task board is the source of truth

- Every piece of work started as an **unowned pending task** with: full requirements, acceptance
  criteria, write scopes, and `blocked_by` dependencies. The task id is the contract; prompts to
  agents reference it instead of restating everything.
- **CAS lifecycle:** `team_task_get` (read latest revision) → `claim` with `expected_revision` →
  work → `complete` with the post-claim revision. A task cannot be completed unless it is
  `in_progress` (claim first), and stale revisions fail loudly.
- **Reopen when acceptance is not met.** Two real examples: a docs acceptance criterion said
  "grep for `scheduleHour/scheduleMin` returns nothing", but the migration note still contained the
  names → reopen task-7 → claim → edit → complete. Same for a QA-found behavior nuance → task-9
  (decision) → task-11 (implementation). Do not silently patch completed work; the board must
  reflect what actually happened.
- **No comment field.** Completion reports go through `send_message`; durable results (QA matrix, a
  product decision) are appended to the task description via `edit` before completing. That keeps the
  task self-contained for later readers.
- **Dependencies are for correctness, scopes are advisory.** `blocked_by` enforces hard ordering
  (e.g. release commit before the `.gitignore` commit); write scopes only warn about overlap — the
  Lead must still sequence overlapping work.
- Useful dependency shapes from this project:
  - research/decision (`task-1`, `task-9`) → implementation;
  - foundation (`task-2`) → scheduler/UI/curve/button;
  - all code → docs (`task-7`) → QA (`task-8`);
  - release (`task-12`) → tooling that also touches git (`task-14`).

---

## 2. Delegation prompt pattern

Roles used: `impl-core` (firmware implementation), `release-bot` (git only), `qa-reviewer` (read-only
independent QA), `libs-bot` (tooling), `compile-checker` (**sole owner of `arduino-cli`**: builds and
fixes compile errors **only when necessary**, reports pass/fail + errors — runs no git and produces no
diffs), `git-release` (**sole owner of all git commands and code diffs**: `status/diff/log/add/commit/
push`, release verification), plus short-lived subagents for focused research. Spawn **fresh** contexts
for independent reviewers/release work; resume a durable inactive teammate with `send_message` when
continuity helps.

Every delegation prompt was self-contained and included:

1. **Identity and role** ("you are the sole implementer / release engineer / independent QA").
2. **Absolute workspace path**, branch/remote facts, current HEAD.
3. **Task id(s)** to read with `team_task_get`, plus the claim → work → complete → report protocol.
4. **Authoritative decisions** (from the user): weekly model, snooze semantics, gated tap, curve
   strategy — so the agent does not re-litigate product choices.
5. **Exact file scopes** and the rule: *if a change needs a file outside your write scope, STOP and
   report* — this prevented `config.h`/`sensors.*` drift twice.
6. **Verification expectations** (mock compile, simulation, greps, evidence in the report) and the
   environment reality (no toolchain, no hardware).
7. **Hard constraints**: no commits/installs unless the task is a release task, no history rewrite,
   no user-config changes, non-blocking firmware, Russian comments/UI.
8. **Report format**: task id + status, files, what/why, evidence (numbers, PASS counts), risks,
   next action.

**Phase boundaries matter.** The implementer was told: "Phase A: task-1+2, report and STOP;
do not start Phase B until I send `continue`." The Lead then reviewed file contents and independent
checks before releasing the next phase. This caught issues early and kept context focused.

**Prompt the stop condition explicitly.** For release: "once task-12 is complete, STOP; do not start
task-14 — another agent handles it." For QA: "if you find a defect, do NOT fix it — report and
recommend reopening." Agents follow stop instructions well when they are stated as hard rules.

---

## 3. Parallelism vs serialization

**Run in parallel** when write scopes are disjoint and no shared external state is touched:

- scheduler (`LightScheduler.ino`) ∥ web UI (`webui.cpp`) — different files, same data model;
- curve implementation (`light.*`) ∥ scheduler/UI;
- research/doc task (`docs/light-curve.md`) ∥ data model (`config.h`/`sensors.*`);
- read-only QA can run while a release commit is being prepared (QA checked blob hashes against the
  committed tree afterwards).

**Serialize** when the same files or the git index are involved:

- Release commit (`task-12`) **before** the `.gitignore`/libraries task (`task-14`) — two agents
  running `git add/commit/push` concurrently would corrupt intent even if the index survived.
- Any task that rewrites files another task owns; use `blocked_by` rather than hoping timings don't
  collide.
- Never have two agents push, and never let QA/release modify the same tree simultaneously.

The practical rule: **one writer per file, one writer per git index.** Advisory write scopes are not
locks; the Lead assigns ordering.

---

## 4. Independent verification (the Lead gate)

- **QA is surface-level by default (user preference, 2026-11-04).** The acceptance criteria that
  matter: the firmware **compiles** (real `arduino-cli` build via `compile-checker`) and **runs
  correctly on the chip** (user/hardware). QA must be quick — read the code for obvious defects,
  confirm the build evidence, check docs match. **No Python behavior models, no tick-by-tick
  simulations, no host mock compiles unless the user explicitly asks for deep verification.** Deeper
  QA is reserved for security-critical or high-risk changes, and only on request; a long-running
  QA pass that re-derives the design is a process failure, not thoroughness.
- **Never trust a single agent's report.** Read the actual files for the claimed change and confirm
  the critical invariants (diff, key greps) instead of relying on a summary.
- **Delegate QA, don't self-review.** The final sign-off QA (`task-13`) was given to a fresh
  `qa-reviewer` with "do not trust prior reports" as an explicit instruction. The Lead still verifies
  git state and reads the QA evidence, but does not own "does it work" alone. (Earlier Lead-run QA was
  a trust gate; the process now consistently delegates it.)
- **Use a different method than the implementer.** If the implementer says "LUT verified by script",
  the Lead parses the LUT from both docs and code and recomputes the formula; if the implementer says
  "simulation passes", QA writes its own model with a different structure.
- **Independent QA results are binding.** `qa-reviewer` produced PASS/FAIL with evidence and flagged
  three stale comments; the Lead treated it as the gate and logged the nits as `task-15` rather than
  overwriting the report.
- **Check the boring invariants:** `git status` clean, `HEAD == origin/main == git ls-remote`, task
  board revisions, no unexpected untracked files, no secrets in the diff.
- **Separate "defect" from "product decision".** The early-tap double-fade was not a code bug against
  the spec; it became task-9 (decision) → user decision → task-11 (implementation), not a hotfix.
- **Acceptance is literal.** "grep returns nothing" means nothing; a migration note that mentions the
  retired field names is still a failure of that criterion. Reopen and fix, don't argue.

---

## 5. Verification toolkit (and the real build)

(Details and snippets in [`technical-notes.md`](technical-notes.md) §2.)

- **Real build first (since 2026-11-04):** the workspace now has `arduino-cli 1.5.1`, cores
  `esp32:esp32 3.3.0` and `esp8266:esp8266 3.1.2`, and the Gyver libraries installed, so the firmware
  is compiled for real: `arduino-cli compile --fqbn esp32:esp32:esp32 LightScheduler/` and, since the
  dual-platform port, `--fqbn esp8266:esp8266:nodemcuv2`. This immediately caught an ESP32 core 3.x
  API removal (`ledcSetup`/`ledcAttachPin`) that the stub compile could never catch — stubs are
  whatever we wrote. **Only the `compile-checker` teammate runs `arduino-cli`; the Lead and every
  other agent must not.** Builds happen only when necessary (a code change to verify or an explicit
  Lead request) and only with the exact commands — no flag experiments, no duplicate builds.
  `compile-checker` reports pass/fail + compiler errors only; **all git commands and code diffs belong
  to `git-release`**, docs stay with the Lead.
- **Sandbox cache workaround:** if an agent sandbox mounts `~/.cache` read-only, `arduino-cli` fails on
  `build.options.json`; set `XDG_CACHE_HOME=/tmp/…` or pass `--build-path /tmp/…` (project unaffected).
- **Upstream source instead of memory:** `curl -fsSL https://raw.githubusercontent.com/GyverLibs/<lib>/main/<path>`
  and GitHub contents API to confirm exact signatures/semantics (`setHoldTimeout`, `AnyPtr`,
  `weekDay`, Saver version behavior, Settings label serialization).
- **Stub-header mock compilation:** `g++ -std=c++11 -Wall -Wextra -Werror -fsyntax-only` on the real
  sources with hand-written `Arduino.h`/`WiFi.h`/`SettingsGyver.h`/… stubs; compile `.ino` with
  `-x c++`. Catches missing includes, wrong call shapes, `static_assert` failures.
- **Host unit tests of real modules:** compile `light.cpp` against stubs with controllable `millis()`
  and a `ledcWriteChannel` capture; define `Config config` in the test. This caught the actual semantics of
  `light_isOn()` vs `light_active()`.
- **Loop-order simulation:** a Python port of `loop()` with 1-second ticks, exact unsigned `millis()`
  deadline math, and events injected at exact seconds. It found the snooze-deadline/schedule
  collision and the early-tap nuance that hand-checking missed.
- **Data checks:** LUT docs ⇄ code ⇄ formula (comment-strip the table rows!), struct layout via
  `sizeof`, web-UI label uniqueness by expanding the `snprintf` templates, stale-field greps.
- **Harness quirks:** every `bash` tool call gets a fresh sandbox — `/tmp` files do not persist, so
  create stubs and compile in one invocation (`D=$(mktemp -d); …; rm -rf "$D"`). Background subagent
  notifications are turns too; don't duplicate their work.

---

## 6. Git / release protocol

- **Single git owner:** all git commands (`status/diff/log/add/commit/push`) and code-diff generation
  are delegated to the `git-release` agent. The Lead requests a release/diff from it; `compile-checker`
  never runs git. One writer per git index.
- **Pre-flight expected set:** `git status --short` compared against the Lead's known change set;
  unexpected files ⇒ STOP. This caught nothing malicious but makes "commit everything" safe.
- **Explicit-path staging**, never `git add -A` (no build artifacts, no accidental secrets).
- **Secret scan** the diff for `PASSWORD|TOKEN|API_KEY|PRIVATE KEY`; Wi-Fi creds stay empty.
- **Commit message** states user-visible behavior and the docs touched; release commits are separate
  from tooling commits (`.gitignore`).
- **Push verification:** `git rev-parse HEAD == git rev-parse origin/main` **and**
  `git ls-remote origin refs/heads/main` equals HEAD; `git status` clean.
- **Fast-forward only.** No force, no rebase, no amend of pushed history. A signed-commit retrofit
  after push would require an approved force-push; not done silently.
- **GPG sandbox workaround:** `~/.gitconfig` enables `commit.gpgsign`, but the sandbox mounts
  `~/.gnupg` read-only → first commit exits 128. Use a per-invocation
  `git -c commit.gpgsign=false commit …`, never edit user config, and **disclose the unsigned
  commit** to the user.
- History: `ed8c201` (user's baseline import; agent agents do not amend it) → `e97cec3` (features) →
  `2cf0285` (gated tap + technical notes) → `84c65d0` (`.gitignore` `libs/`) → compile-fix + docs-sync
  release (2026-11-04: ESP32 core 3.x LEDC API, docs split).

---

## 7. Monitoring teammate liveness

- `wait_agent` returns on the next status/message/task change; afterwards **re-list** agents and
  tasks — the board's `in_progress` list can be empty for a moment between claim steps, so empty does
  not mean "done".
- A teammate `inactive` status does not describe task success/failure — check the task status and its
  completion report. Reports arrive as peer messages starting with the sender name.
- Read the actual file content/diff after each phase rather than relying on the summary; verify
  claimed numbers (LUT, PASS counts) with a quick script.
- One writer per git index: when two delegated tasks both touch git, order them with `blocked_by` and
  check the remote HEAD after each.

---

## 8. Docs and backlog hygiene

- Separate docs by audience: [`PROJECT.md`](../PROJECT.md) = factual project state;
  [`technical-notes.md`](technical-notes.md) = firmware engineering;
  [`workflow.md`](workflow.md) = implementer process; this file = Lead/orchestration.
- Keep project docs **factual**: after behavior changes, sync intro, API tables, logic sections and
  "nuances"; grep for retired terms (`scheduleHour`, "tap always postpones", old linear-fade text).
- **Task-board snapshot is local-only:** the full task export lives in `TASKS.md` at the repo root and
  is listed in `.gitignore` — the DSH board is the source of truth, the file is a git-ignored
  point-in-time snapshot for offline reference.
- QA nits that fall outside the current task's write scope become follow-up tasks (`task-15`), not
  drive-by edits — keeps diffs reviewable and the audit trail intact.
- User-owned artifacts: when the user says "I asked for this file", leave it untouched even if it is
  untracked; don't commit unrequested files. Commit what the user asked for, then ask about the rest.
- When the user says "delegate it", create the task and spawn/reuse an agent — the Lead's job is
  assignment, review and final state, not doing the work in the same context.

---

## 9. Pitfalls found

- Trusting a confident report without reading the files (caught a stale docs claim twice).
- Two agents touching git or the same file concurrently (avoid via `blocked_by` + one-writer rule).
- Advisory write scopes are not locks — they do not prevent conflicts.
- Task board has no comment field; reports and decisions must be recorded another way.
- `/tmp` does not persist between `bash` calls in this harness.
- GPG signing + read-only `~/.gnupg` breaks commits; unsigned commits must be disclosed.
- LUT parsing: comment columns (`// 0..15`) pollute naive number extraction — strip comments first.
- A no-op behavior (tap in the dark) needs an explicit serial log and docs, otherwise it looks like a
  broken button.
- Subagents may finish after the main turn; their output can add untracked files — check `git status`
  before declaring the tree clean.
- "Headless" QA is still valuable: a fresh agent with a different model/context catches things the
  implementer and Lead rationalised.
- Stub compilation cannot catch removed/changed real APIs: the stubs are written from the same
  assumptions as the code. The first real `arduino-cli` build found ESP32 core 3.x LEDC removals (§5).

---

## 10. Quick reference

**Task id map (this project):**

| Id | Purpose |
|---|---|
| task-1 | curve research (`docs/light-curve.md`) |
| task-2 | Config/data model + sensors weekday/date |
| task-3 | per-weekday scheduler |
| task-4 | per-weekday web UI |
| task-5 | integer LUT fade |
| task-6 | tap-snooze + 2 s hold skip |
| task-7 | docs sync |
| task-8 | static QA (Lead-run) |
| task-9 | snooze-vs-schedule decision (RESOLVED: tap no-op before light-up) |
| task-10 | release #1 |
| task-11 | gated tap implementation |
| task-12 | release #2 (gated tap + notes) |
| task-13 | independent QA of task-11 (delegated to `qa-reviewer`) |
| task-14 | download Gyver libs + `.gitignore` |
| task-15 | pending doc-comment nits from QA |

> **New run, 2026-11-04 (new Lead):** board ids restarted — `task-1` = firmware compile-fix
> (`compile-checker`, first real build), `task-2` = docs sync + release (Lead), `task-3` =
> `git-release` ownership + process-rule docs release. The map above is the historical 2026-11-02 run;
> ids are per-board-session, not global.

**Every release:** request it from `git-release` (sole git owner): pre-flight `git status --short` →
explicit `git add <paths>` → commit (GPG workaround if needed) → `git push origin main` → verify
`HEAD == origin/main == ls-remote` → report hash + unsigned status.

**Every build check:** request `compile-checker` (sole `arduino-cli` owner) only when necessary (code
changed or the Lead asks) → it runs the exact per-target builds (ESP32 and ESP8266), fixes errors,
reports pass/fail + errors (no diff). Request `git-release` for the code diff and git state. Never run `arduino-cli` outside
`compile-checker`, and never run git outside `git-release`.

**Every phase:** claim task → read files → minimal diff → agent self-verifies → Lead re-verifies or
delegates QA → complete task → report → next phase.
