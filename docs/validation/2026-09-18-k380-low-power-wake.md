# K380 Battery Low-Power Build And Hardware Validation

## Status

Closed on 2026-10-12. Physical low-power, wake, held-key, ambiguity and
automatic-system-off acceptance passed, so formal
`CONFIG_K380_AUTO_SYSTEM_OFF=y` is enabled. The `AUTO_SYSTEM_OFF=n` and
`AUTO=y` test-image steps below are kept as historical evidence of how that
acceptance was reached. CI success never by itself marked a checklist item as
physically passed.

Follow-up records: `docs/validation/2026-10-12-k380-zmk-acceptance-closeout.md`
and `docs/validation/2026-09-23-k380-idle-system-off.md`.

## Identification

- Date: 2026-09-18, Asia/Shanghai.
- Sample: K380 attached to J-Link probe 851000967; board revision unconfirmed.
- Firmware repository: `kimwolf-fs/k380-zmk`.
- CI branch: `codex/k380-battery-low-power-ci`.
- Previously flashed commit: `8ca26b9f078a152c38e13d6338a4bcb064daf169`.
- Current flashed and source-reviewed commit: `5c8a3bf8bb177c00f2e42a8cb054294355ed5b25`.
- Current temporary AUTO_SYSTEM_OFF=y hardware-test commit: `88885d1a54608f0723ee816a7c9a8759c8153012`.
- Workflow: `K380 CI`; formal job: `firmware-build`.
- Allowed application artifact: `k380-zmk-firmware` only.
- Required files: `zmk.uf2`, `zmk.hex`, `zmk.elf`, `zmk.map`,
  `zephyr.dts`, `.config`.
- Current formal artifact/run: `k380-zmk-firmware`, run `35343743784`, artifact `10545657439`.
- Download directory: `E:/project/k380-keyboard/.artifacts/k380-zmk/35343743784-firmware/zephyr/`.
- SHA256 (`zmk.hex`): `031DD47D7BFB754EBF2F7546213CDD254BD56D3EC29BC2EC6A9B8F70CDCDFA88`.
- Tools: GitHub Actions native_sim/Twister and nRF52840 builds;
  J-Link V9.70, `jlink` skill.
- J-Link device/interface/speed: `nRF52840_xxAA`, SWD, 4000 kHz.
- Current instrument and battery voltage: not established.

## Build Evidence

| Run / commit | Actual result | Action |
| --- | --- | --- |
| 35319255312 / 33ebb065 | Failure; ghost-filter, driver and module isolation passed; declaration/link failures | Diagnostic only; not flashed |
| 35320891289 / 60202e60 | Failure; ghost-filter 10/10, status 17/17, driver and isolation passed; native Kconfig and production coordinator linking blocked | Diagnostic only; not flashed |
| 35322944081 / 71dcf184 | Failure; 10/15 native groups and driver/board/RTT/metadata/isolation passed; 5 native groups, soft-off-only and formal firmware failed | Integration fixes in progress; not flashed |
| 35337502234 / 8ca26b9f | Success; all native, owner, driver, board, RTT, isolation, metadata and formal firmware jobs passed | Downloaded, checked and flashed; physical acceptance pending |
| 35338863230 / b132fef2 | Success with strict wake-arm assertion and declared high-level GPIO emulator support | Downloaded; application HEX identical to the flashed image |
| 35339489230 / 56bfc724 | New warning-timing/USB cancellation and latch-delete regression tests fail as expected | Diagnostic only; no additional flash |
| 35341629375 / 4bddbe74 | Success; warning, runtime qualification, invalid sensor and USB publication fixes | Not flashed; later review found stale voltage decisions |
| 35342354621 / b567c27c | Success with voltage decision generation regression | Not flashed; later review found latch save/delete race |
| 35342674994 / d7cbf161 | Expected failure: soft-off 13/14; old admitted save restores Flash latch after safe clear | RED report retained locally; not flashed |
| 35343244182 / 704d8e83 | Soft-off 15/15 passed, including serialized save/delete and stale save admission | Not flashed; review requires RAM reason setter budget fix |
| 35343407304 / fadc1d4f | Expected RED: RAM reason setter waits on in-flight Flash operation | Diagnostic report retained locally; not flashed |
| 35343743784 / 5c8a3bf8 | Complete K380 CI success; soft-off 16/16, all 16 native suites and all formal/integration builds passed | Source review approved; formal download, application verification and J-Link flash passed |
| 35501335069 / 66cf543f | Expected RED: cold startup does not derive Z5 state | Diagnostic report retained locally; not flashed |
| 35501627096 / c5cc9238 | Expected RED: startup BLE-ready notification is absent and Z5 state is not initialized | Diagnostic report retained locally; not flashed |
| 35502056841 / 88885d1a | Complete K380 CI success; startup notification and Z5 timeout regressions pass with all native and build jobs | AUTO_SYSTEM_OFF=y test artifact downloaded, validated and flashed for Test 4B retest |

Run 35322944081 native logs are retained at
`E:/project/k380-keyboard/.artifacts/k380-zmk/35322944081-diagnostic/native-results/`.
Three runtime failures concern pairing fake state, startup recovery sample
expectations and quiet rendering expectations. Two native compilation failures
concern syscall generation and duplicate log declarations. The minimal
production fixture lacks unrelated dynamic behavior nodes. Formal firmware
is blocked before compilation by legacy workflow command assertions.
Independent Task 7 review also requires a production BLE-slot/no-battery
configuration to compile without a conflicting fallback declaration.

## J-Link Evidence And Flash Gate

The prior info-only preflight identified the configured nRF52840 target through
probe 851000967, with VTref 1.812 V. This is debug-reference voltage, not a
VDDH battery measurement, current measurement, firmware acceptance or flash.

Before application flashing:

1. Require independent source review and a passing current-commit Actions run.
2. Download only the formal artifact into
   `E:/project/k380-keyboard/.artifacts/k380-zmk/<run-id>-firmware/`.
3. Verify file completeness, SHA256, generated formal config, real keymap,
   matrix wake references and enabled GPIO/GPIOTE controllers.
4. Verify every HEX data record and checksum against the generated application
   partition. Source baseline is `[0x26000, 0xCA000)`; generated DTS is authority.
5. Use application `zmk.hex` with the confirmed target and probe. Do not issue
   full-chip erase or overwrite bootloader, settings, DFU storage or UICR.
6. Record successful programming/verification and reset/run evidence.

The user's explicit application J-Link request supersedes the existing
manual's UF2-only application route for this operation. It does not authorize
bootloader replacement or erase-all.

## Hardware Acceptance Still Pending

Earlier J-Link flash on 2026-09-18 (8ca26b9f): probe `851000967`, `nRF52840_xxAA`, SWD 4000,
J-Link V9.70. Preflight VTref was 1.812 V. The formal artifact contained all
six required files; generated config retained AUTO_SYSTEM_OFF=n, SLEEP=n,
PM_SOFT_OFF=y, battery interval 60 and disabled underglow. HEX checks found
14,727 records and zero checksum errors; its data range `[0x26000, 0x5F778)`
is within generated code partition `[0x26000, 0xCA000)`. Flash verified with
errorlevel 0 in 8722 ms, followed by successful reset and probe info. No
full-chip erase was requested, and HEX data excludes bootloader, settings,
DFU and UICR. These results do not demonstrate physical wake or current.

Current J-Link flash on 2026-09-18 20:21:38 +08:00 (5c8a3bf8): same confirmed
probe, target and SWD speed. Formal artifact10545657439 is tied to the current
passing run/commit. Six required files were verified; local generated config,
80-key default layer and Fn bindings, 8x15 matrix wake reference and enabled
GPIO/GPIOTE controllers passed. HEX14,812 records have zero checksum errors;
data `[0x26000,0x5FCC0)` is entirely inside generated application partition
`[0x26000,0xCA000)`. Application `loadfile` verified successfully, elapsed8663ms,
errorlevel0, followed by reset/run in the flash template. Post-flash info at
20:21:49 +08:00 succeeded (VTref1.812V). No full-chip erase was issued. HEX data
does not write settings, DFU, bootloader or UICR; this is address-range evidence,
not a before/after memory comparison of those regions.

Correction: native GPIO emulator supports level interrupts when the overlay
declares `high-level`. Run 35337502234 accepted -ENOTSUP and does not constitute
strict wake-arm acceptance. Commit b132fef2 enables this property and restores
the required zero return assertion; run 35338863230 passed. Actual GPIO
SENSE and first-key behavior still require hardware validation.

| Check | Expected result | Actual result |
| --- | --- | --- |
| Current states | Measure active, connected idle, Z5, Z7, USB Z7 stopped, release-wait and system-off | Pending instrument/sample identification |
| Connected idle | Baseline connection stability and sampling; battery idle system-off evaluated separately | Test 1 baseline passed; 10-minute battery idle system-off passed on a separate unmerged branch, see the 2026-09-23 validation record; integrated-image acceptance pending |
| System-off wake | Representative keys cover every row/column; wake press consumed; next press delivered once | Tests 4/11/20: all 8 matrix rows and all 15 matrix columns represented and passed |
| Repeated wake | At least 20 timeout/wake cycles stable | Test 7 completion: 20 cycles passed |
| Held keys | Radio/LED quiet; no reboot loop; off only after all release; next key wakes | Tests 9/10: single- and multi-key release behavior passed; release-wait current gate remains open |
| BLE/USB | Battery Z5 10 s, Z7 60 s off; USB Z5 retained; USB Z7 stops only radio | Pending qualified AUTO-enabled hardware test |
| Startup latch | Unsafe voltage keeps BLE gated; charging or three safe raw samples qualifies; clear once | Pending controlled VDDH test |
| Persistence | BLE timeout causes never saved; dirty slot flushed once; unchanged slot not saved | Pending hardware settings test |
| Sampling | Boot/wake/activity/USB immediate reads coalesced; periodic interval 60 s | Pending physical trace |
| LED | Z3/Z5 1 s edges; Z4/Z7 250 ms edges; quiet off and no scheduled redraw | Pending physical trace |
| Write budget | One write attempt, shared 1 s admission deadline; measure in-flight synchronous backend overrun | Pending backend timing trace |

## Human Test Results

### Test 1: Battery connected idle baseline

- Date: 2026-09-20.
- Supply: power analyzer, simulated battery at 3.7 V.
- Startup and automatic BLE reconnection: passed.
- Ordinary keys, Fn layer and BLE behavior: passed.
- Connected idle for 5 minutes: remained connected; no visible reset or disconnect.
- Idle current: approximately 850 uA.
- Key press current peak: approximately 5 mA.
- Status indicator: off.
- Anomalies: none reported.
- Assessment: passed; use 850 uA as the same-fixture connected-idle baseline.

### Test 2: Battery disconnected bonded-host wait

- Date: 2026-09-20.
- Connected idle before host disconnect: approximately 800 uA.
- Battery wait state: slot blue indicator slow blink.
- Current after disconnect: approximately 800 uA to 3 mA during both 0-10 s and 10-30 s windows.
- Automatic system-off within 30 s: no, as expected for the formal `AUTO=n` image.
- Abnormal reset or lockup: none observed.
- Host restoration: automatic reconnect succeeded in approximately 20 s; normal key input passed.
- Assessment: passed for the `AUTO=n` broadcast/reconnect baseline. The 20 s reconnect duration is retained for comparison with the AUTO=y test image.

### AUTO=y test image preparation

- CI run: `35497281399`, commit `a8610be1`.
- Artifact: `k380-auto-system-off-test-firmware`.
- Test image SHA256: `C8424F0C1DD90B99E90F0C0BF74A47B87EF8E6D820FE59F7927D9AF59778F9B1`.
- Generated config verified `AUTO_SYSTEM_OFF=y`, `SLEEP=n`, `PM_SOFT_OFF=y`.
- HEX verified: 14,811 records, zero checksum errors, data `[0x26000,0x5FCB0)` inside application `[0x26000,0xCA000)`.
- J-Link programming is currently blocked because the probe/target connection failed before any loadfile operation; no AUTO=y image has been written.
- Connection was restored on 2026-09-20; J-Link info passed with VTref 1.825 V.
- AUTO=y test image was flashed and verified to the application region only, elapsed 9469 ms, errorlevel 0. Post-flash info/reset path passed.

### Test 3: AUTO=y bonded-host timeout and wake

- Date: 2026-09-20.
- Connected idle before disconnect: approximately 800 uA.
- Disconnect-to-system-off time: approximately 10 s.
- Pre-off current: approximately 900 uA to 3 mA.
- System-off current: approximately 70 uA; indicator off.
- First key press: woke the keyboard, produced no character.
- Reconnect after wake: approximately 2 s.
- Second key press: produced exactly one normal character.
- Reboot loop or other anomaly: none observed.
- Assessment: passed; this is the first physical pass for the AUTO=y Z5 timeout, system-off current, wake-key consumption and first post-wake input.

### Test 4: Matrix wake coverage (partial)

- Right Ctrl (R2/C0): wake succeeded in approximately 1 s.
- `4` (R7/C1): wake succeeded in approximately 1 s.
- Both wake presses produced no character and showed the same post-wake behavior.
- Post-wake indicator remained off while current rose; BLE reconnection was not
  observed for more than 2 minutes, then a green connected indication appeared
  and later turned off.
- Host Bluetooth was enabled immediately after each wake. Other Bluetooth
  devices, including the mouse, reconnected promptly, while the host continued
  to show the K380 as disconnected.
- During the delayed-reconnect interval the current continued to fluctuate,
  consistent with ongoing BLE advertising rather than a return to system-off.
- After the green connected indication appeared, pressing `A` produced normal
  input.
- Assessment: electrical wake path passed for two matrix positions, but the
  post-wake BLE recovery timing differs materially from Test 3 and is pending
  isolation before continuing the remaining matrix positions.

### Test 4A: Host-ready wake timing isolation

- Host Bluetooth was enabled and stable before waking the keyboard.
- No blue slow-blink indication appeared after wake.
- K380 reconnected in approximately 7 s and showed the green connected
  indication; current was approximately 800 uA at 15 s.
- A system-off transition around 10 s was not expected after the successful
  connection at 7 s, so that observation is not a timeout pass/fail result.
- Assessment: BLE cold-start advertising and reconnection are functional when
  the host is already ready. The missing blue indication remains consistent
  with an uninitialized startup slot policy, but this run cannot determine
  whether the bonded-disconnected 10 s timer was armed because connection
  completed first.

### Test 4B: Cold-start bonded timeout with host unavailable

- Host Bluetooth remained disabled throughout the run.
- Pre-wake system-off current: approximately 70 uA.
- The wake press raised current to approximately 800-1200 uA with continued
  fluctuation, but no blue slow-blink indication appeared.
- No system-off transition occurred around 10 s; current was still
  approximately 800-1200 uA at 20 s.
- No other indicator activity or reboot was observed.
- Assessment: reproduced. Matrix wake and BLE advertising start correctly, but
  the cold-start bonded-disconnected path does not initialize Z5 status or arm
  its 10 s timeout. This matches the missing startup notification to
  `ble_slot_policy`; an automated startup regression test is required before
  changing production behavior.

### Test 4B retest: Startup BLE policy synchronization

- Test firmware: run `35502056841`, commit `88885d1a`, test-only
  `CONFIG_K380_AUTO_SYSTEM_OFF=y`.
- Artifact SHA256 (`zmk.hex`):
  `77729F9AD30A72DC5903AA9E2BB9387C1D2238BB41A73649313B731C78F1A700`.
- Intel HEX checksums passed; data range `0x26000..0x5FCAF` is inside the
  application partition `[0x26000,0xCA000)`.
- J-Link application-only load and verification passed on probe `851000967` at
  SWD 4000 kHz; preflight VTref was 1.825 V. No full-chip erase was issued.
- Host Bluetooth remained disabled throughout the test.
- Pre-wake system-off current: 77 uA.
- The wake press immediately started the blue slow-blink indication; current
  fluctuated between approximately 800 and 1200 uA.
- System-off occurred after 10 s and current returned to 78 uA.
- No reboot or other abnormal behavior was observed.
- Assessment: passed. The hardware result confirms that the startup BLE-ready
  notification initializes Z5 state and arms the bonded-disconnected timeout
  after a system-off matrix wake. This passes Test 4B only; remaining matrix,
  repeated-wake and held-key coverage is still pending.

### Test 4C: Left Ctrl matrix wake and host-ready reconnect

- Pre-wake system-off current: 75 uA.
- `Left Ctrl` (R3/C14) woke the keyboard and immediately started the blue
  slow-blink indication.
- The keyboard required multiple wake attempts before it connected; the green
  connected indication was not observed closely enough to record.
- After connection, pressing `A` produced one lowercase `a`; Ctrl was not
  stuck and no reboot or other abnormal behavior was observed.
- Assessment: the R3/C14 matrix wake and modifier release path passed, but the
  host-ready reconnect result did not pass. A single-attempt timed trace is
  required to distinguish a valid 10 s timeout that is too short for the host
  from a timeout/state synchronization failure.

### Test 4D: Single-attempt host-ready reconnect trace

- Pre-wake system-off current: 75 uA.
- The host Bluetooth stack was enabled and stable before the single wake press.
- The blue slow-blink indication started immediately, but the keyboard did not
  connect within 10 s.
- At the 10 s boundary the blue indication stopped and current returned to
  77 uA; current remained 77 uA at 20 s and the host still showed the keyboard
  as disconnected.
- No additional key was pressed and no other abnormal behavior was observed.
- Assessment: the repaired Z5 timer and system-off transition are operating as
  designed. The remaining failure is the product assumption that a bonded host
  can reliably reconnect within the configured 10 s window. Timeout policy
  must be reconsidered before continuing reconnect-dependent matrix coverage.

### 30-second Z5 candidate preparation

- CI run: `35505750006`, commit `1146c7b4`; the complete K380 CI passed.
- Artifact: `k380-auto-system-off-test-firmware` with test-only
  `CONFIG_K380_AUTO_SYSTEM_OFF=y`.
- Generated config verified bonded reconnect timeout `30000` ms, pairing
  timeout `60000` ms, `CONFIG_ZMK_SLEEP=n`, and
  `CONFIG_ZMK_PM_SOFT_OFF=y`.
- Artifact SHA256 (`zmk.hex`):
  `9C8E3D8E8E55D3A0E3B845BFE99BF432EDAB63CD8204FB658982E7412BADF5B7`.
- All 14,811 Intel HEX record checksums passed; all 236,709 data bytes are in
  `[0x26000,0x5FCB0)`, inside application partition `[0x26000,0xCA000)`.
- J-Link probe `851000967` connected to `nRF52840_xxAA` over SWD at 4000 kHz;
  preflight VTref was 1.793 V.
- Application-only `loadfile` completed with verification, reset and run in
  1887 ms. No full-chip erase was issued and no settings, DFU, bootloader or
  UICR region was included in the HEX.
- Physical reconnect and no-host timeout results are pending; the formal image
  remains `CONFIG_K380_AUTO_SYSTEM_OFF=n`.

### Test 5: 30-second candidate host-ready Left Ctrl wake

- Date: 2026-09-21.
- Test image: CI run `35505750006`, commit `1146c7b4`,
  `CONFIG_K380_BLE_WAIT_TIMEOUT_MS=30000` and test-only
  `CONFIG_K380_AUTO_SYSTEM_OFF=y`.
- Pre-wake current: approximately 75 uA.
- A single `Left Ctrl` press woke the keyboard and immediately started the
  blue slow-blink indication.
- The keyboard connected in approximately 10 s and showed the green
  indication.
- Pressing `A` produced one uppercase `A`; no Ctrl latch, reboot, or other
  anomaly was observed.
- The operator reported that current had returned to the system-off level
  before the connection was observed. This is inconsistent with a completed
  system-off transition followed by a connection without another wake press,
  so the exact 8-12 s current trace is not yet established.
- Assessment: BLE wake, reconnect and key behavior passed functionally. The
  no-intermediate-system-off gate remains pending because the reported current
  dip needs one time-correlated repeat measurement.

### Test 5 retest: time-correlated host-ready wake

- Pre-wake current: approximately 75 uA; one `Left Ctrl` press was used.
- Current at 8 s was approximately 800 uA, at 10 s approximately 1300 uA,
  and at 12 s approximately 800 uA.
- Green indication appeared at approximately 11 s.
- Current never remained below 100 uA for one second or longer before the
  connection, so no intermediate system-off was observed.
- The reported `A` result was `Aa`; the exact character count/case still needs
  a separate input check, but no Ctrl latch or reboot was observed.
- Assessment: the 30-second bonded-host reconnect gate passed. Proceed to the
  no-host 30-second timeout gate.

### Test 6: 30-second no-host timeout

- Date: 2026-09-21.
- Host Bluetooth remained disabled throughout the test.
- Pre-wake current: approximately 75 uA; one `Left Ctrl` press was used.
- Blue slow-blink indication started immediately.
- Current remained in the advertising range, approximately 800 uA to 3 mA,
  at both 8 s and 20 s.
- At approximately 30 s the current briefly reached about 20 uA, then settled
  at approximately 75 uA; the actual system-off transition was at 30 s.
- No green indication, reboot, or other anomaly was observed.
- Assessment: the 30-second no-host timeout gate passed. Both 30-second Z5
  hardware gates are now complete; formal `AUTO_SYSTEM_OFF` remains disabled.

### Test 7: Repeated no-host wake cycles, first batch

- Date: 2026-09-21.
- Six complete wake/timeout cycles were performed with host Bluetooth disabled
  and `Left Ctrl` as the wake key.
- Every cycle woke on the first press and immediately showed the blue
  slow-blink indication.
- Every cycle returned to system-off at approximately 30 s, with post-off
  current approximately 75 uA.
- No cycle missed wake, remained active, rebooted, locked up, or showed an
  abnormal current trend.
- Assessment: first 6 of the required 20 repeated-wake cycles passed. The
  remaining 14 cycles are still required before closing this gate.

### Test 7 completion: 20 repeated no-host wake cycles

- Fourteen additional cycles were completed under the same conditions, for a
  cumulative total of 20.
- All 20 cycles woke on the first press, entered system-off at approximately
  30 s, and settled at approximately 75 uA.
- No missed wake, missed timeout, reboot, lockup, or current anomaly occurred.
- Assessment: the repeated-wake hardware gate passed.

### Test 8: Single held key across the timeout boundary

- Date: 2026-09-21.
- With host Bluetooth disabled, holding `Left Ctrl` woke the keyboard
  immediately. At 30 s the blue indication turned off.
- Continuing to hold the key from 30-35 s caused no repeated indication,
  reboot, or wake loop, but current remained approximately 4 mA.
- Releasing the key caused an immediate current drop; the reported stable
  value was `75 mA`, which requires unit confirmation because the established
  system-off baseline is approximately 75 uA.
- A subsequent short press woke the keyboard normally; no other anomaly was
  observed.
- Source correlation: while a key remains held, the matrix driver schedules
  another full scan every 1 ms. Each frame includes eight output scans with
  configured 5 us settle and 50 us inter-output busy waits. This high-duty
  scan continues in `K380_LOW_POWER_RELEASE_WAIT` and explains the observed
  current even after radio and LED shutdown.
- Assessment: held-key loop prevention and release-to-system-off behavior
  passed, but release-wait power consumption failed the low-power objective.
  Do not close the held-key gate until the scan duty is reduced and retested.

### Test 9: Single held key after 50 ms release-wait scan optimization

- Date: 2026-09-21.
- Firmware: Task 2 commit `a5b28833a74c99caf67bfb6f5739f1be243c7b1d`,
  AUTO system-off test artifact from K380 CI run `35585468000`.
- Artifact SHA-256 (`zmk.hex`):
  `1832D035D9CBF0D240D10D779F0A5684E22C224A13BDAEF719FB393F919C6EAA`.
- With host Bluetooth disabled, pre-wake current was approximately 75 uA.
- Holding `Left Ctrl` woke the keyboard; the blue indication turned off at
  approximately 30 s.
- Continuing to hold the key from 30-35 s measured approximately 800 uA.
- No light, reboot, repeated wake, or other loop occurred while held.
- Releasing the key returned to approximately 75 uA immediately.
- A subsequent short press woke the keyboard once successfully.
- Assessment: single-key release-wait behavior passed and the measured current
  is a substantial reduction from the previous approximately 4 mA baseline,
  but approximately 800 uA is above the 200 uA design target. Per the current
  test decision, this value is accepted temporarily as an observed real-time
  result; no further release-wait power optimization is being attempted in
  this phase. The <=200 uA power gate remains open and is not claimed as
  passed. The earlier Test 8 remains the pre-optimization failure record.

### Current decision: defer further release-wait power optimization

- Date: 2026-09-22.
- The approximately 800 uA held-key release-wait measurement is retained as
  the current real-time board result.
- No additional scan-period, GPIO, BLE-controller, or LED-power changes are
  authorized in this phase.
- Functional release-wait behavior is allowed to proceed to the next gate:
  multiple held keys must keep the device out of system-off until every key
  is released.
- A future power pass may add synchronized waveform and long-window averaging,
  but that measurement is not a prerequisite for the next functional test.

### Test 10: Multiple held keys release-wait gate

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000` (`zmk.hex` SHA-256
  `1832D035D9CBF0D240D10D779F0A5684E22C224A13BDAEF719FB393F919C6EAA`).
- Matrix combination: `Left Ctrl` (`RC(3,14)`) and `A` (`RC(0,9)`), held
  together after waking from approximately 75 uA system-off.
- `Left Ctrl` woke the keyboard on the first press and the blue indicator
  started immediately. `A` could remain held without causing a wake loop.
- After approximately 30 s with both keys held, current was approximately
  800 uA. Releasing only `A` left `Left Ctrl` held and the keyboard remained
  in release-wait at approximately 800 uA; it did not enter system-off.
- Releasing the final `Left Ctrl` caused an immediate return to approximately
  75 uA. The next short press woke the keyboard once successfully.
- No light loop, reboot, or other abnormal behavior was observed.
- Assessment: the multi-key release gate passed. System-off is reached only
  after all held keys are released. The approximately 800 uA release-wait
  current remains a temporarily accepted observation; the <=200 uA power gate
  remains open and is not claimed as passed.

### Test 11: Wake-to-normal-input recovery

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000`.
- The pre-wake current was approximately `75 uA`, matching the established
  system-off baseline.
- `Left Ctrl` woke the keyboard on the first press and the wake press produced
  no character, as required.
- BLE reconnected in approximately 16 s and the green indication appeared.
- A subsequent `A` press produced exactly one `a`. A later `Left Ctrl` press
  behaved normally; no stuck modifier, reboot, duplicate character, or light
  loop was observed.
- Assessment: the system-off wake precondition, wake-key consumption, BLE
  recovery, and normal post-wake input behavior all passed.

### Test 12: Same-column adjacent-row key regression

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000`.
- `H` (`RC(1,5)`) produced only `h` when pressed alone, and `J` (`RC(0,5)`)
  produced only `j` when pressed alone.
- Holding the pair produced only the two physically pressed keys. When `H`
  was pressed first, the observed sequence was `h` followed by repeated `j`
  while `J` remained held; the inverse order produced `j` followed by repeated
  `h` while `H` remained held. Repetition stopped when the corresponding key
  was released.
- No third/ghost character, post-release output, or stuck key was observed.
- Assessment: same-column adjacent-row scanning and ghost-filter behavior
  passed this hardware sample. The repeated character is normal held-key
  repeat, not a matrix stuck-key or ghosting failure. Full matrix coverage
  remains open.

### Test 13: Three-key rectangle-corner ghost filtering

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000`.
- Rectangle coordinates: `J=RC(0,5)`, `F=RC(0,7)`, `H=RC(1,5)`, and
  `Q=RC(1,7)`.
- Holding `J + F + H` produced `j` and repeated `f`; the missing fourth
  corner `Q` was not reported.
- Holding `J + F + Q` produced `j` and repeated `f`; the missing fourth
  corner `H` was not reported.
- Releasing the keys stopped output with no residual character, stuck key,
  reboot, or other abnormal behavior.
- Assessment: three-key rectangle-corner ghost filtering passed this hardware
  sample. Full four-corner rectangle stability and complete matrix coverage
  remain open.

### Test 14: Full four-corner rectangle stability

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000`.
- The four-corner combination `J + F + H + Q` was held and repeated three
  times. All three rounds produced the stable sequence `jffffff`.
- No character outside the four-corner set was produced, and releasing all
  keys stopped output immediately. No stuck key, reboot, or other abnormal
  behavior occurred.
- The observed partial output is consistent with the implemented no-diode
  policy: once a rectangle is detected, previously accepted keys are retained
  while newly ambiguous keys are filtered. This prevents false-key reports but
  does not promise simultaneous delivery of every key in an ambiguous full
  rectangle.
- Assessment: full-rectangle behavior is stable and explainable and passes the
  current no-diode ghost-filter requirement. A product requirement that all
  four keys in an ambiguous rectangle must be delivered simultaneously would
  require a separate hardware or policy decision; it is not claimed here.

### Test 15: Non-ambiguous cross-row/cross-column multi-key regression

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000`.
- Combination: `A=RC(0,9)` and `H=RC(1,5)`, which does not form a rectangle
  with only the two pressed keys.
- Three rounds produced only `a` and repeated `h` while the keys were held:
  `ahhhhhhh`, `ahhhhhhhhh`, and `ahhhhhhhhhhhhhhhh`.
- No extra character or ghost key appeared. Output stopped immediately after
  release, with no stuck key, reboot, or other abnormal behavior.
- Assessment: the non-ambiguous multi-key combination was not damaged by the
  no-diode ghost filter and passed the functional regression.

### Test 16: A-key system-off wake coverage

- Date: 2026-09-22.
- Firmware: the same AUTO system-off test artifact from K380 CI run
  `35585468000`.
- Pre-wake current was approximately `75 uA`. `A` (`RC(0,9)`) woke the
  keyboard on the first press and the blue indication started immediately.
- The wake press produced no character. The BLE field was reported as no
  timeout, without a numeric connection time; the subsequent successful input
  confirms that normal input recovery completed.
- A later `A` press produced exactly one `a`.
- No reboot, repeated light loop, or stuck key was observed.
- Follow-up observation on the `C` (`RC(5,7)`) wake sample: all three BLE
  indicator LEDs reportedly flashed blue together twice, then only one slot
  LED continued blue blinking. A comparison `A` wake showed only the expected
  selected-slot LED. The normal Z5/Z7 renderer only writes the selected slot
  LED, so this transient three-LED pattern is retained as a pending LED
  anomaly rather than marked as expected.
- Assessment: the A-row/C-row wake, wake-key consumption, and post-wake input
  path passed functionally. The C-wake LED transition requires one repeatable
  observation before closing the LED portion of this gate.

### Test 17: Repeated C-wake LED transient

- Date: 2026-09-22.
- The `C` system-off wake sequence was repeated three times from an
  approximately `75 uA` baseline.
- Every repetition showed all three BLE indicator LEDs flashing blue exactly
  twice, followed by only the currently selected slot LED continuing its blue
  slow blink.
- No reboot, extra persistent light mode, stuck key, or input failure was
  observed.
- Assessment: the three-LED blue double-flash is reproducible and is therefore
  not treated as a random observation. It is outside the normal steady-state
  Z5/Z7 renderer contract, so the LED wake-transition gate remains open for
  root-cause isolation. Functional C wake and input recovery remain passed.

### Test 18: Same-column LED transient correlation

- Date: 2026-09-22.
- `F` (`RC(0,7)`) was tested as a system-off wake key to compare with `C`
  (`RC(5,7)`), sharing the same matrix column.
- From approximately `75 uA`, `F` woke the keyboard on the first press. The
  first wake press produced no character, and the subsequent `F` input
  produced exactly one `f`.
- Every observed wake showed all three BLE indicator LEDs flashing blue twice,
  followed by only the selected-slot LED continuing its blue slow blink.
- No reboot, stuck key, or other abnormal behavior occurred.
- Assessment: the LED transient correlates with the shared matrix column 7,
  not with a single C switch. This narrows the issue to a column-7/system-off
  wake or common wake initialization path; no firmware change is made yet.

### Test 19: Same-row different-column LED isolation

- Date: 2026-09-22.
- `G` (`RC(5,8)`) was tested against `C` (`RC(5,7)`), sharing the same matrix
  row but using the adjacent column 8.
- `G` woke and recovered normally. No three-LED blue double-flash was observed;
  the expected selected-slot indication was shown and normal input behavior
  remained intact.
- Assessment: the transient does not follow row 5 or the general wake path.
  Together with Tests 17 and 18, the evidence isolates it to the column-7
  wake/electrical path or its column-specific post-wake timing. Functional
  wake and input behavior remain passed; LED root-cause investigation is now
  required before further coverage testing.

### Test 20: Full matrix-column wake coverage after recovery-scan fix

- Date: 2026-09-22.
- Firmware: bootloader recovery-scan fix `k380-bootloader` commit `9dcbd6d`,
  built and validated by GitHub Actions run `35697411113`, then flashed to the
  bootloader region only.
- One representative key was tested for each matrix column `C0` through `C14`:
  `Right Ctrl`, `4`, `E`, `B`, `D`, `H`, `2`, `F`, `G`, `A`, `Right Shift`,
  `Space`, `Left GUI`, `Non-US Backslash`, and `Left Ctrl`.
- All 15 column tests passed. Each key woke the keyboard normally without a
  three-LED blue double-flash, without reboot, stuck key, or light loop, and
  normal post-wake input behavior remained intact.
- Assessment: the representative full-matrix wake coverage gate passed: all
  8 matrix rows and all 15 matrix columns are represented by the tested keys.
  An exhaustive every-key electrical sweep is not required for the current
  recovery-scan defect and remains optional.

### Test 21: Sustained system-off stability

- Report date: 2026-10-08.
- The operator reported that holding the device in system-off for the planned
  10-30 minute observation passed with no abnormal behavior.
- Assessment: the sustained system-off stability test passed based on the
  operator's report. Exact observation duration and new current readings were
  not supplied; no additional numerical measurement is inferred.

### Cross-branch validation update: battery idle system-off

- Report date: 2026-10-08.
- The operator confirmed that a separate, not-yet-merged branch puts the
  keyboard into a lower-power sleep state after 10 minutes of battery-powered
  connected idle and that its hardware test passed.
- The local companion record `2026-09-23-k380-idle-system-off.md` documents
  that work, including system-off wake semantics and the restored 30-second
  bonded-host waiting window.
- This is separate-branch evidence, not proof that the previously validated
  image includes the change or that the combined image has passed acceptance.
  Integration/source comparison and a targeted combined-image regression are
  still required before a release claim. No merge or new flash was performed
  for this update.
- USB current measurement remains unavailable; Fn+Del/UF2 behavior and further
  release-wait current optimization remain deferred as previously agreed.

### Root-cause diagnosis: column-7 bootloader recovery false positive

- The bootloader recovery probe in `k380-bootloader/src/main.c` drives
  `K380_RECOVERY_ROW_PIN = P0.4` high and reads
  `K380_RECOVERY_COL_PIN = P0.31`.
- The K380 ZMK matrix assigns `P0.31` to matrix column 7. Therefore the
  recovery probe shares its sense column with `C=RC(5,7)` and `F=RC(0,7)`.
- The bootloader always starts its B1 indicator with WS1/WS2/WS3 blue slow
  blink. If the recovery probe falsely sees column 7 active, it enters the
  battery DFU wait path; after its approximately 3 s no-USB timeout it tears
  down and starts the ZMK application, where the selected-slot LED resumes.
- This exactly matches the hardware sequence: only column-7 wake keys show two
  visible all-three-blue flashes, then the normal single-slot blue indication;
  `G=RC(5,8)` does not show it.
- Root cause: the bootloader recovery scan does not explicitly force all
  non-recovery matrix rows inactive, so a no-diode key in the shared recovery
  column can be interpreted as the recovery coordinate. This is a bootloader
  input-isolation defect, not a ZMK LED renderer defect.
- The fix is now deployed in the bootloader: every matrix row is configured to
  a known inactive output during recovery probing, only the recovery row is
  driven active, the recovery column is sampled after settling, and all
  touched pins are restored before continuing boot. Hardware Test 20 confirms
  that the column-7 false-positive path no longer produces the three-LED
  transient during system-off wake.

No checklist item is marked physically passed based on CI alone. The Flash
latch uses `k380/last_shutdown_reason=low_voltage_protection`; BLE timeout
causes exist only in ordinary RAM. Cold-start recovery derives slot/bond,
connection, power and qualified voltage rather than restoring a RAM reason.

## Conclusion And Follow-Up

Conclusion: independent source review and complete K380 CI35343743784 pass
at 5c8a3bf8. Confirmed production warning, runtime recovery, invalid sensor,
delete error, USB publication, stale voltage decision and latch transaction
regressions have been addressed. Final formal artifact download, application
address/config/checksum validation and J-Link flash verification passed; the
validated current formal image replaces the earlier behavior-risk image.
Low-power physical acceptance and automatic-system-off enablement were
completed afterwards; see
`docs/validation/2026-10-12-k380-zmk-acceptance-closeout.md`. Retain artifacts
and never commit `.artifacts`; the repository `.gitignore` now excludes
`.artifacts/`, `.ci-worktree/`, `.ci-debug-*/` and `.embeddedskills/`.

Latest source review: 704d8e83 serializes latch save/delete and RAM latch access
using a normal mutex. Safe recovery publishes a RAM generation before deletion;
an older save is rejected before admission, or an already admitted save completes
before safe deletion. BLE remains gated until deletion succeeds. No Flash call
runs under the lifecycle spinlock. A remaining budget gap is the RAM reason
setter waiting on the Flash mutex before the one-second deadline starts was
reproduced by fadc1d4f/35343407304 and fixed by 5c8a3bf8 using atomic RAM
publication. The final source review found no further important blocker.
The synchronous Flash backend can still overrun one admitted write; this is
reported and subsequent writes are skipped, not interrupted mid-write.
RED save/delete report: `.artifacts/k380-zmk/35342674994-soft-off-red/`.
RED RAM publication report: `.artifacts/k380-zmk/35343407304-soft-off-budget-red/`.
Final GREEN report: `.artifacts/k380-zmk/35343743784-soft-off-green/`.
Current source is preserved in `k380-zmk/.ci-worktree` and the dedicated remote
CI branch; it has not overwritten or synchronized the original dirty checkout.
Separate repository-wide pre-commit formatting failures remain out of scope;
passing K380 CI is not a claim that all unrelated GitHub workflows pass.
