# K380 J-Link RTT Matrix Diagnostics Design

**Status:** Approved for planning.

## Goal

Add a K380-only diagnostic firmware build that lets bring-up use J-Link RTT to
observe raw matrix coordinates from the real K380 scanning path.

The diagnostic build must print each K380 kscan event as `(row, column, state)`
so the operator can validate the 80 recorded keys and the 40 unused matrix
coordinates against `docs/k380/matrix-layout.md`.

## Scope

This stage includes:

- A non-default K380 diagnostic build entry for `k380/nrf52840/zmk`.
- J-Link RTT logging for matrix press and release events.
- Reuse of the production K380 board DTS, real `k380_kscan` node, and existing
  `k380,kscan-no-diode-matrix` driver.
- A diagnostic callback path that reports original kscan `(row, column)` events
  before they are hidden by HID keycodes.
- Build or static checks that prove the diagnostic entry exists and does not
  change the default K380 firmware path.
- Documentation for the J-Link RTT bring-up workflow and expected log format.

This stage does not include:

- USB CDC diagnostics.
- A separate raw GPIO scanner that bypasses the K380 kscan driver.
- LED, battery, BLE, USB identity, or bootloader behavior changes.
- Marking real-board matrix validation complete.
- Changing `pinmap.md` or `matrix-layout.md` unless real hardware evidence later
  proves a recorded fact is wrong.

## Architecture

The diagnostic firmware should be an opt-in build configuration. Normal
`k380/nrf52840/zmk` builds keep the existing `k380.keymap`, `Fn+Del` bootloader
entry, physical layout transform, Flash partitions, and production kscan
behavior.

The diagnostic build should select a small K380 diagnostic component that:

1. Gets the selected K380 kscan device from devicetree.
2. Configures the kscan callback directly.
3. Enables scanning.
4. Logs every callback event over RTT using a stable line format.

The first implementation should use the real K380 kscan driver instead of a
manual GPIO scanner. That keeps the diagnostic evidence tied to the path used by
the application: row GPIO drive, column reads, no-diode ghost filtering,
debounce, and interrupt or polling behavior.

## Output Format

Every matrix event must produce one single-line RTT record:

```text
K380_MATRIX row=<0-7> col=<0-14> state=<down|up>
```

Example:

```text
K380_MATRIX row=4 col=7 state=down
K380_MATRIX row=4 col=7 state=up
```

The format is intentionally plain text so it can be copied from RTT Viewer,
captured from `JLinkRTTClient`, or parsed later by a simple script. The
diagnostic build may print a boot banner and error lines, but matrix events must
keep the exact prefix and key-value names above.

## Build Integration

The diagnostic entry should live close to the K380 module and tests, not as a
replacement for the default board files. Acceptable implementation shapes are:

- a K380 diagnostic sample or test application that builds with
  `k380/nrf52840/zmk` and `ZMK_EXTRA_MODULES=zmk-keyboard-k380`;
- or a K380-specific extra config and overlay pair that enables a diagnostic
  source file while preserving the production board DTS.

The implementation plan should choose the smallest shape that builds cleanly in
the existing ZMK/Zephyr workflow and can be reproduced by CI.

The diagnostic build must enable RTT logging. It must not require USB CDC to
observe matrix events.

## Data Flow

The intended data path is:

```text
physical key -> K380 matrix GPIO -> k380_kscan driver -> diagnostic kscan callback -> RTT log
```

The diagnostic callback reports the `(row, column)` coordinates received from
kscan. It must not transform coordinates into physical key positions or HID
keycodes. That keeps the output directly comparable with `matrix-layout.md`.

## Error Handling

If the kscan device is missing or not ready, the diagnostic firmware should log
a clear RTT error and avoid reporting false pass/fail results.

If kscan configuration or enable fails, the firmware should log the return code.
The real-board validation task remains incomplete until the operator has usable
event output and records hardware results.

The diagnostic firmware must not infer success from booting alone. Only observed
event logs for the required keys and combinations count as hardware evidence.

## Verification

CI or local build verification should prove:

- The diagnostic entry builds for `k380/nrf52840/zmk`.
- RTT logging support is enabled for that entry.
- The production K380 board build still uses the normal keymap and does not
  include the diagnostic callback path.
- The K380 board contract checks for Flash partitions, GPIO lists, physical
  layout transform, and forbidden out-of-scope peripherals still pass.

Real-board validation remains separate and must record:

- PCB revision.
- Test date.
- Firmware commit.
- SWD/J-Link flashing path.
- RTT capture method.
- Per-key results for all 80 valid keys.
- Evidence that the 40 unused coordinates do not report events.
- Multi-key and ghost-filter observations.
- Interrupt wake, scan stop, and recovery observations.

## Success Criteria

- A developer can build and flash a K380 diagnostic image with J-Link.
- Opening RTT shows each press and release as
  `K380_MATRIX row=<r> col=<c> state=<down|up>`.
- The diagnostic image uses the real K380 kscan driver and board GPIO contract.
- Default K380 firmware behavior is unchanged.
- The deferred real-board checklist remains unchecked until actual hardware
  results are recorded.
