# K380 J-Link RTT Matrix Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an opt-in K380 diagnostic firmware configuration that prints real kscan `(row, column, state)` events over J-Link RTT.

**Architecture:** Keep the production K380 board and keymap unchanged. Add a K380 module Kconfig switch that is off by default, a diagnostic extra config file that enables RTT logging and that switch, and a tiny guarded `printk()` in the existing K380 kscan driver immediately before it forwards events to ZMK. CI builds the opt-in diagnostic image and proves the default board build does not enable the diagnostic path.

**Tech Stack:** ZMK, Zephyr devicetree/Kconfig/CMake, nRF52840, J-Link RTT, GitHub Actions, PowerShell.

---

## File Structure And Responsibilities

| File                                                                     | Responsibility                                                                                                                |
| ------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| `zmk-keyboard-k380/drivers/kscan/Kconfig`                                | Define `CONFIG_K380_MATRIX_DIAGNOSTICS_RTT`, default off, depending on the K380 no-diode driver.                              |
| `zmk-keyboard-k380/drivers/kscan/kscan_k380_no_diode_matrix.c`           | Print stable `K380_MATRIX row=<r> col=<c> state=<down\|up>` lines only when the diagnostic Kconfig is enabled.                |
| `zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf`          | Opt-in build config enabling RTT logging and the K380 matrix diagnostic switch.                                               |
| `.github/workflows/k380-ci.yml`                                          | Add a diagnostic build job and validation gates; keep the default board build proving diagnostics are not enabled by default. |
| `docs/k380/matrix-rtt-diagnostics.md`                                    | Operator workflow for building, flashing with J-Link, opening RTT, interpreting output, and recording results.                |
| `docs/superpowers/plans/2026-08-25-k380-jlink-rtt-matrix-diagnostics.md` | Track implementation steps; do not mark real-board validation complete.                                                       |

## Task 1: Add Failing Gates For The Diagnostic Contract

**Files:**

- Modify: `.github/workflows/k380-ci.yml`
- Modify: `docs/superpowers/plans/2026-08-25-k380-jlink-rtt-matrix-diagnostics.md`
- Test: local PowerShell static checks

- [x] **Step 1: Run the pre-implementation source probe and capture the expected failure**

Run from `E:\project\k380-keyboard\k380-zmk`:

```powershell
$driver = Get-Content -Raw 'zmk-keyboard-k380\drivers\kscan\kscan_k380_no_diode_matrix.c'
$kconfig = Get-Content -Raw 'zmk-keyboard-k380\drivers\kscan\Kconfig'
$diagConfExists = Test-Path 'zmk-keyboard-k380\diagnostics\matrix-rtt\k380-matrix-rtt.conf'

if ($driver -notmatch 'K380_MATRIX row=%u col=%u state=%s\\n') {
  throw 'expected failure: driver does not emit K380_MATRIX RTT lines yet'
}
if ($kconfig -notmatch 'config K380_MATRIX_DIAGNOSTICS_RTT') {
  throw 'expected failure: K380_MATRIX_DIAGNOSTICS_RTT is not defined yet'
}
if (-not $diagConfExists) {
  throw 'expected failure: matrix RTT diagnostic config is missing'
}
```

Expected result before Task 2:

```text
expected failure: driver does not emit K380_MATRIX RTT lines yet
```

- [x] **Step 2: Add the CI diagnostic build skeleton**

In `.github/workflows/k380-ci.yml`, add a new job after `board-build` and before `module-isolation`:

```yaml
matrix-rtt-diagnostics:
  needs: module-metadata
  if: ${{ needs.module-metadata.outputs.module-registered == 'true' }}
  runs-on: ubuntu-latest
  container:
    image: docker.io/zmkfirmware/zmk-build-arm:4.1
  steps:
    - uses: actions/checkout@v7
    - uses: actions/cache@v6
      continue-on-error: true
      with:
        path: |
          modules/
          tools/
          zephyr/
          bootloader/
        key: ${{ runner.os }}-k380-${{ hashFiles('app/west.yml') }}
        restore-keys: |
          ${{ runner.os }}-k380-
    - run: west init -l app
    - run: west update --fetch-opt=--filter=tree:0
    - run: west zephyr-export
    - run: |
        west build -s app -d build/k380-matrix-rtt -p always -b k380/nrf52840/zmk -- \
          -DZMK_EXTRA_MODULES="${GITHUB_WORKSPACE}/zmk-keyboard-k380" \
          -DEXTRA_CONF_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf"
    - run: |
        grep -q '^CONFIG_K380_MATRIX_DIAGNOSTICS_RTT=y$' \
          build/k380-matrix-rtt/zephyr/.config
        grep -q '^CONFIG_ZMK_RTT_LOGGING=y$' \
          build/k380-matrix-rtt/zephyr/.config
        if grep -q '^CONFIG_LOG_MODE_IMMEDIATE=y$' \
          build/k380-matrix-rtt/zephyr/.config; then
          echo "::error title=K380 RTT diagnostics must use deferred logging::CONFIG_LOG_MODE_IMMEDIATE is incompatible with the Bluetooth software Link Layer"
          exit 1
        fi
        if grep -q '^CONFIG_ZMK_USB_LOGGING=y$' \
          build/k380-matrix-rtt/zephyr/.config; then
          echo "::error title=K380 RTT diagnostics must not use USB logging::CONFIG_ZMK_USB_LOGGING is enabled"
          exit 1
        fi
        if grep -q '^CONFIG_USB_CDC_ACM=y$' \
          build/k380-matrix-rtt/zephyr/.config; then
          echo "::error title=K380 RTT diagnostics must not use USB CDC ACM::CONFIG_USB_CDC_ACM is enabled"
          exit 1
        fi
        grep -R "K380_MATRIX row=%u col=%u state=%s" \
          build/k380-matrix-rtt/zephyr
    - if: always()
      uses: actions/upload-artifact@v7
      with:
        name: k380-matrix-rtt-diagnostics-output
        path: |
          build/k380-matrix-rtt/**/*.log
          build/k380-matrix-rtt/**/zephyr/.config
          build/k380-matrix-rtt/**/zephyr/zephyr.dts
          build/k380-matrix-rtt/**/zephyr/zmk.hex
          build/k380-matrix-rtt/**/zephyr/zmk.uf2
        if-no-files-found: ignore
```

Expected result at this point: the workflow is intentionally red because
`zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf` does not exist yet.

- [x] **Step 3: Add default-board diagnostic-off assertions**

In the existing `board-build` job, extend the post-build validation step after the board contract validation with:

```yaml
- name: Validate K380 diagnostics are opt-in
  run: |
    test -f build/k380-board/zephyr/.config
    set +e
    grep -q '^CONFIG_K380_MATRIX_DIAGNOSTICS_RTT=y$' \
      build/k380-board/zephyr/.config
    config_status=$?
    set -e
    if [ "$config_status" -eq 0 ]; then
      echo "::error title=K380 diagnostics must be opt-in::CONFIG_K380_MATRIX_DIAGNOSTICS_RTT is enabled in the default board build"
      exit 1
    fi
    if [ "$config_status" -ne 1 ]; then
      echo "::error title=K380 diagnostics inspection failed::Unable to inspect K380 board .config"
      exit "$config_status"
    fi

    set +e
    grep -R "K380_MATRIX row=%u col=%u state=%s" \
      build/k380-board/zephyr >/tmp/k380_diag_string_matches.txt
    grep_status=$?
    set -e
    if [ "$grep_status" -eq 0 ]; then
      echo "::error title=K380 diagnostics must be opt-in::diagnostic RTT format string is present in the default board build"
      cat /tmp/k380_diag_string_matches.txt
      exit 1
    elif [ "$grep_status" -ne 1 ]; then
      exit "$grep_status"
    fi
```

Expected result: default board builds must fail if the diagnostic path is compiled in by accident.

- [x] **Step 4: Run YAML and whitespace checks**

Run:

```powershell
git diff --check
rg -n "matrix-rtt-diagnostics|Validate K380 diagnostics are opt-in|k380-matrix-rtt.conf" .github\workflows\k380-ci.yml
```

Expected result:

```text
.github\workflows\k380-ci.yml:<line>:  matrix-rtt-diagnostics:
.github\workflows\k380-ci.yml:<line>:      - name: Validate K380 diagnostics are opt-in
.github\workflows\k380-ci.yml:<line>:            -DEXTRA_CONF_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf"
```

- [x] **Step 5: Commit the failing gates**

Run:

```powershell
git add .github\workflows\k380-ci.yml docs\superpowers\plans\2026-08-25-k380-jlink-rtt-matrix-diagnostics.md
git commit -m "test(k380): add matrix rtt diagnostic gates"
```

Expected result: one commit containing only the workflow gate and updated plan checklist.

## Task 2: Add The Opt-In Diagnostic Configuration

**Files:**

- Modify: `zmk-keyboard-k380/drivers/kscan/Kconfig`
- Create: `zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf`
- Modify: `docs/superpowers/plans/2026-08-25-k380-jlink-rtt-matrix-diagnostics.md`
- Test: local source checks

- [x] **Step 1: Add the Kconfig switch**

In `zmk-keyboard-k380/drivers/kscan/Kconfig`, inside the existing `if KSCAN` block, add:

```kconfig
config K380_MATRIX_DIAGNOSTICS_RTT
    bool "Emit K380 matrix events over RTT"
    depends on K380_KSCAN_NO_DIODE_MATRIX
    default n
    help
      Emit each K380 kscan press and release as a plain text RTT line:
      K380_MATRIX row=<row> col=<column> state=<down|up>.

      This is a hardware bring-up diagnostic path. It must stay disabled in
      normal K380 firmware builds.
```

Expected result: the option exists, is scoped to the K380 driver, and defaults off.

- [x] **Step 2: Create the diagnostic RTT config**

Create `zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf` with:

```conf
CONFIG_ZMK_RTT_LOGGING=y
CONFIG_K380_MATRIX_DIAGNOSTICS_RTT=y
CONFIG_LOG_BACKEND_SHOW_COLOR=n
```

Expected result: diagnostic builds enable RTT matrix diagnostics without enabling USB CDC diagnostics or Zephyr immediate logging.

- [x] **Step 3: Run the config source check**

Run:

```powershell
$kconfig = Get-Content -Raw 'zmk-keyboard-k380\drivers\kscan\Kconfig'
$conf = Get-Content -Raw 'zmk-keyboard-k380\diagnostics\matrix-rtt\k380-matrix-rtt.conf'
if ($kconfig -notmatch 'config K380_MATRIX_DIAGNOSTICS_RTT') {
  throw 'missing K380_MATRIX_DIAGNOSTICS_RTT'
}
if ($kconfig -notmatch 'default n') {
  throw 'diagnostic Kconfig must default off'
}
if ($conf -notmatch 'CONFIG_ZMK_RTT_LOGGING=y') {
  throw 'diagnostic config must enable RTT logging'
}
if ($conf -notmatch 'CONFIG_K380_MATRIX_DIAGNOSTICS_RTT=y') {
  throw 'diagnostic config must enable K380 matrix diagnostics'
}
if ($conf -match 'USB_CDC|ZMK_USB_LOGGING') {
  throw 'diagnostic config must not depend on USB CDC logging'
}
git diff --check
```

Expected result: no exception and no whitespace errors.

- [x] **Step 4: Commit the diagnostic config**

Run:

```powershell
git add zmk-keyboard-k380\drivers\kscan\Kconfig zmk-keyboard-k380\diagnostics\matrix-rtt\k380-matrix-rtt.conf docs\superpowers\plans\2026-08-25-k380-jlink-rtt-matrix-diagnostics.md
git commit -m "feat(k380): add matrix rtt diagnostic config"
```

Expected result: one commit with Kconfig and diagnostic config only.

## Task 3: Emit Matrix Events From The Real K380 Driver

**Files:**

- Modify: `zmk-keyboard-k380/drivers/kscan/kscan_k380_no_diode_matrix.c`
- Modify: `docs/superpowers/plans/2026-08-25-k380-jlink-rtt-matrix-diagnostics.md`
- Test: local source checks and existing ghost-filter test

- [x] **Step 1: Add the printk include**

In `zmk-keyboard-k380/drivers/kscan/kscan_k380_no_diode_matrix.c`, add this include with the other Zephyr includes:

```c
#include <zephyr/sys/printk.h>
```

Expected result: the driver can use `printk()` without relying on logging prefixes.

- [x] **Step 2: Add a focused diagnostic helper**

In the same file, below `static int state_index_rc(...)`, add:

```c
static void k380_kscan_diagnostic_report(uint32_t row, uint32_t col, bool pressed) {
#if IS_ENABLED(CONFIG_K380_MATRIX_DIAGNOSTICS_RTT)
    printk("K380_MATRIX row=%u col=%u state=%s\n", row, col, pressed ? "down" : "up");
#else
    ARG_UNUSED(row);
    ARG_UNUSED(col);
    ARG_UNUSED(pressed);
#endif
}
```

Expected result: the format exactly matches the approved spec and compiles away for normal builds,
including the format string.

- [x] **Step 3: Call the helper before forwarding kscan events**

In `k380_kscan_read()`, inside the `if (zmk_debounce_get_changed(state))` block, change the event section to:

```c
if (zmk_debounce_get_changed(state)) {
    const bool pressed = zmk_debounce_is_pressed(state);

    LOG_DBG("Sending event at %i,%i state %s", row, col, pressed ? "on" : "off");
    k380_kscan_diagnostic_report(row, col, pressed);
    data->callback(dev, row, col, pressed);
}
```

Expected result: the diagnostic line reports the original kscan row and column before ZMK transforms them to positions or keycodes.

- [x] **Step 4: Run the exact format source check**

Run:

```powershell
$driver = Get-Content -Raw 'zmk-keyboard-k380\drivers\kscan\kscan_k380_no_diode_matrix.c'
if ($driver -notmatch '#include <zephyr/sys/printk.h>') {
  throw 'missing printk include'
}
if ($driver -notmatch 'CONFIG_K380_MATRIX_DIAGNOSTICS_RTT') {
  throw 'missing diagnostic Kconfig guard'
}
if ($driver -notmatch 'K380_MATRIX row=%u col=%u state=%s\\n') {
  throw 'diagnostic output format changed'
}
if ($driver -notmatch 'k380_kscan_diagnostic_report\(row, col, pressed\);\s*data->callback') {
  throw 'diagnostic report must run immediately before callback forwarding'
}
git diff --check
```

Expected result: no exception and no whitespace errors.

- [x] **Step 5: Run the existing host unit test**

If the local Zephyr workspace is initialized, run:

```powershell
west twister -T zmk-keyboard-k380\tests\ghost-filter -p native_sim
```

Expected result:

```text
1 test scenarios ... passed
```

If `west` or the Zephyr workspace is unavailable locally, record the exact failure and rely on the remote `K380 CI / ghost-filter` job after push.

Result: local command failed because `west` is unavailable: `The term 'west' is not recognized as a name of a cmdlet, function, script file, or executable program.`

- [x] **Step 6: Commit the diagnostic emitter**

Run:

```powershell
git add zmk-keyboard-k380\drivers\kscan\kscan_k380_no_diode_matrix.c docs\superpowers\plans\2026-08-25-k380-jlink-rtt-matrix-diagnostics.md
git commit -m "feat(k380): emit matrix events over rtt"
```

Expected result: one commit with the guarded driver emitter only.

## Task 4: Document The J-Link RTT Bring-Up Workflow

**Files:**

- Create: `docs/k380/matrix-rtt-diagnostics.md`
- Modify: `docs/k380/hardware-contract.md`
- Modify: `docs/superpowers/plans/2026-08-25-k380-jlink-rtt-matrix-diagnostics.md`
- Test: document consistency checks

- [x] **Step 1: Create the operator workflow document**

Create `docs/k380/matrix-rtt-diagnostics.md` with:

````text
# K380 J-Link RTT Matrix Diagnostics

**Status:** Diagnostic firmware workflow defined; real-board validation remains incomplete until captured hardware results are recorded.

## Purpose

Use this diagnostic image to observe K380 matrix `(row, column)` events over
J-Link RTT while exercising the real K380 kscan driver and board GPIO contract.

## Build

From the repository root:

```powershell
west build -s app -d build/k380-matrix-rtt -p always -b k380/nrf52840/zmk -- `
  -DZMK_EXTRA_MODULES="$PWD/zmk-keyboard-k380" `
  -DEXTRA_CONF_FILE="$PWD/zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf"
```

The build must use the production K380 board DTS and the opt-in diagnostic
config. Do not modify `app/boards/kimwolf/k380/k380.keymap` for this test.

## Flash

Flash the generated image with J-Link/SWD using the normal K380 bring-up
procedure. Record the exact command used, the PCB revision, and the firmware
commit.

## RTT Capture

Open J-Link RTT Viewer or `JLinkRTTClient` after flashing. Matrix events use
this exact line format:

```text
K380_MATRIX row=<0-7> col=<0-14> state=<down|up>
```

Example for `Del`, which is `RC(4,7)`:

```text
K380_MATRIX row=4 col=7 state=down
K380_MATRIX row=4 col=7 state=up
```

## Validation

Compare each observed event with `matrix-layout.md`.

- Every one of the 80 valid keys must report only its recorded coordinate.
- The 40 unused coordinates must not report events.
- Non-ambiguous multi-key combinations must keep reporting accepted keys.
- Rectangular no-diode ambiguity must follow the K380 ghost-filter behavior.
- Interrupt wake, scan stop, and recovery behavior must be recorded separately.

Do not mark the real-board matrix checklist complete from a successful build
alone. Only captured RTT output from real hardware counts as matrix evidence.

````

Expected result: the document gives an operator a reproducible J-Link RTT workflow.

- [x] **Step 2: Link the workflow from the hardware contract**

In `docs/k380/hardware-contract.md`, in the `实板验证清单` verification paragraph, append this sentence:

```markdown
矩阵诊断固件的 J-Link RTT 操作流程见 [`matrix-rtt-diagnostics.md`](matrix-rtt-diagnostics.md)。
```

Expected result: the main contract points to the diagnostic workflow but does not mark any hardware task complete.

- [x] **Step 3: Run document checks**

Run:

```powershell
$doc = Get-Content -Raw 'docs\k380\matrix-rtt-diagnostics.md'
$contract = Get-Content -Raw 'docs\k380\hardware-contract.md'
if ($doc -notmatch 'K380_MATRIX row=<0-7> col=<0-14> state=<down\\|up>') {
  throw 'diagnostic doc must define the exact output format'
}
if ($doc -match '(?i)verified|passed' -and $doc -notmatch 'remains incomplete') {
  throw 'diagnostic doc must not imply hardware validation is complete'
}
if ($contract -notmatch 'matrix-rtt-diagnostics.md') {
  throw 'hardware contract must link the matrix RTT workflow'
}
git diff --check
```

Expected result: no exception and no whitespace errors.

- [x] **Step 4: Commit the operator documentation**

Run:

```powershell
git add docs\k380\matrix-rtt-diagnostics.md docs\k380\hardware-contract.md docs\superpowers\plans\2026-08-25-k380-jlink-rtt-matrix-diagnostics.md
git commit -m "docs(k380): document matrix rtt diagnostics"
```

Expected result: one documentation commit.

## Task 5: Verify Builds And Push For Remote CI

**Files:**

- Test: default K380 board build, diagnostic build, K380 CI
- Modify: `docs/superpowers/plans/2026-08-25-k380-jlink-rtt-matrix-diagnostics.md`

- [x] **Step 1: Run local static validation**

Run:

```powershell
$driver = Get-Content -Raw 'zmk-keyboard-k380\drivers\kscan\kscan_k380_no_diode_matrix.c'
$conf = Get-Content -Raw 'zmk-keyboard-k380\diagnostics\matrix-rtt\k380-matrix-rtt.conf'
$workflow = Get-Content -Raw '.github\workflows\k380-ci.yml'
if ($driver -notmatch 'K380_MATRIX row=%u col=%u state=%s\\n') {
  throw 'missing diagnostic output'
}
if ($conf -notmatch 'CONFIG_ZMK_RTT_LOGGING=y') {
  throw 'diagnostic config does not enable RTT'
}
if ($workflow -notmatch 'matrix-rtt-diagnostics') {
  throw 'CI does not build the diagnostic image'
}
git diff --check
```

Expected result: no exception and no whitespace errors.

- [x] **Step 2: Build the default K380 board locally when toolchain is available**

Run:

```powershell
west build -s app -d build/k380-board -p always -b k380/nrf52840/zmk -- `
  -DZMK_EXTRA_MODULES="$PWD/zmk-keyboard-k380" `
  -DEXTRA_CONF_FILE="$PWD/zmk-keyboard-k380/tests/board-build/k380-board.conf" `
  -DEXTRA_DTC_OVERLAY_FILE="$PWD/zmk-keyboard-k380/tests/board-build/k380-board.overlay"
```

Expected result:

```text
... build/k380-board/zephyr/zmk.uf2
```

Then run:

```powershell
Select-String -Path 'build\k380-board\zephyr\.config' -Pattern '^CONFIG_K380_MATRIX_DIAGNOSTICS_RTT=y$' -Quiet
```

Expected result: PowerShell returns `False`; the default board build does not enable diagnostics.

Local result: skipped because `west --version` failed with:

```text
The term 'west' is not recognized as a name of a cmdlet, function, script file, or executable program.
```

- [x] **Step 3: Build the RTT diagnostic image locally when toolchain is available**

Run:

```powershell
west build -s app -d build/k380-matrix-rtt -p always -b k380/nrf52840/zmk -- `
  -DZMK_EXTRA_MODULES="$PWD/zmk-keyboard-k380" `
  -DEXTRA_CONF_FILE="$PWD/zmk-keyboard-k380/diagnostics/matrix-rtt/k380-matrix-rtt.conf"
```

Expected result:

```text
... build/k380-matrix-rtt/zephyr/zmk.uf2
```

Then run:

```powershell
Select-String -Path 'build\k380-matrix-rtt\zephyr\.config' -Pattern '^CONFIG_K380_MATRIX_DIAGNOSTICS_RTT=y$','^CONFIG_ZMK_RTT_LOGGING=y$'
```

Expected result: both config lines are present.

Local result: skipped because `west --version` failed with:

```text
The term 'west' is not recognized as a name of a cmdlet, function, script file, or executable program.
```

- [x] **Step 4: Record local build limitations if needed**

Local `west build` cannot run because `west` is not available in this PowerShell environment:

```text
The term 'west' is not recognized as a name of a cmdlet, function, script file, or executable program.
```

Static checks passed, but local static checks are not a full firmware build.

- [x] **Step 5: Push and verify remote K380 CI**

Run:

```powershell
git status --short --branch
git push
```

Expected result: branch `feat/k380-real-matrix-integration` pushes all local commits.

Wait for `K380 CI` and require these jobs to pass:

```text
module-metadata
ghost-filter
driver-build
board-build
matrix-rtt-diagnostics
module-isolation
```

Record the successful run ID in the final handoff.

Result: pushed branch `feat/k380-real-matrix-integration` at
`214ca1d077328c2ee4a79a2f5fce6b7dd8b2f698`. `K380 CI` run
`32806902278` passed with:

```text
module-metadata
ghost-filter
driver-build
board-build
matrix-rtt-diagnostics
module-isolation
```

- [x] **Step 6: Keep real-board validation unchecked**

Run:

```powershell
$realMatrixPlan = Get-Content -Raw 'docs\superpowers\plans\2026-08-21-k380-real-matrix-integration.md'
if ($realMatrixPlan -match '(?m)^- \[x\].*(80 个有效按键|40 个未使用坐标|真实 P0/P1)') {
  throw 'real-board matrix validation was incorrectly marked complete'
}
```

Expected result: no exception. Building the diagnostic firmware does not complete real-board validation.
