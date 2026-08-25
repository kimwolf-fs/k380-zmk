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
