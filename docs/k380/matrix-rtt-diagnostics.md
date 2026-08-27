# K380 J-Link RTT Matrix Diagnostics

**Status:** Diagnostic firmware workflow defined; real-board validation remains incomplete until captured hardware results are recorded.

## Purpose

Use this diagnostic image to observe K380 matrix `(row, column, key, state)`
events over J-Link RTT while exercising the real K380 kscan driver and board
GPIO contract.

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
K380_MATRIX row=<0-7> col=<0-14> key=<key-name> state=<down|up>
```

On successful startup, the diagnostic image also prints:

```text
K380_KSCAN_INIT ready
K380_KSCAN_BOOT ready
```

If `K380_KSCAN_INIT ready` appears, the K380 kscan driver initialized and RTT
output is working. If `K380_KSCAN_BOOT ready` appears, the kscan enable path
ran as well.

The diagnostic build is quiet by default: it does not emit the one-line-per-
second heartbeat, and ZMK module logs are limited to non-debug levels so the
per-key matrix output remains readable.

Example for `Del`, which is `RC(4,7)`:

```text
K380_MATRIX row=4 col=7 key=Del state=down
K380_MATRIX row=4 col=7 key=Del state=up
```

## Validation

Compare each observed event with `matrix-layout.md`. A valid key must report
the expected coordinate and key name. Unused matrix coordinates are reported as
`key=UNUSED` if they ever appear, which is a validation failure unless the
hardware map is being deliberately updated.

- Every one of the 80 valid keys must report only its recorded coordinate.
- The 40 unused coordinates must not report events.
- Non-ambiguous multi-key combinations must keep reporting accepted keys.
- Rectangular no-diode ambiguity must follow the K380 ghost-filter behavior.
- Interrupt wake, scan stop, and recovery behavior must be recorded separately.

Do not mark the real-board matrix checklist complete from a successful build
alone. Only captured RTT output from real hardware counts as matrix evidence.
