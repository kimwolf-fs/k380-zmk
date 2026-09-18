# K380 matrix system-off wake

The native test includes the real driver translation unit and uses Zephyr GPIO
emulation, the real ZMK debouncer, and the real K380 ghost filter. The test fixture
does not register a second Kscan device or introduce production-only test APIs.

```text
west build -s zmk-keyboard-k380/tests/system-off-wake -d build/k380-system-off-wake -b native_sim
build/k380-system-off-wake/zephyr/zephyr.exe
```

Coverage:

- Each of the 120 raw, debounced, and still-debouncing positions blocks release.
- Suspend stops pending work and marks scanning disabled before disconnecting GPIOs.
- Quiet release-wait suppresses every press/release callback and waits for a full
  raw/debounced release frame, including positions rejected by the ghost filter.
- Wake preparation rejects held keys; arming wake drives rows high and configures
  active-level column interrupts without enabling scans or enqueueing events.
- A wake press held for 10,000 scan frames and its release are both consumed;
  only a later ordinary press reaches the normal callback.
- Ordinary connected-idle suspend/resume delivers the first key normally.

The companion `tests/shutdown-input` native suite links the real coordinator,
HID listener, and HID state implementation. It queues a key before shutdown,
clears reports through the coordinator, then delivers the queued event and checks
that HID stays empty with no report sent. The ready/connected-idle case delivers
its first key normally. Only endpoint transport and event dispatch are replaced.

```text
west build -s zmk-keyboard-k380/tests/shutdown-input -d build/k380-shutdown-input -b native_sim
build/k380-shutdown-input/zephyr/zephyr.exe
```

The nRF driver-build fixture also compiles the real PM/HWINFO path:

```text
west build -s app -d build/k380-driver -p always -b nrf52840dk/nrf52840 -- -DZMK_EXTRA_MODULES=E:/project/k380-keyboard/k380-zmk/zmk-keyboard-k380 -DEXTRA_CONF_FILE=E:/project/k380-keyboard/k380-zmk/zmk-keyboard-k380/tests/driver-build/k380-driver.conf -DEXTRA_DTC_OVERLAY_FILE=E:/project/k380-keyboard/k380-zmk/zmk-keyboard-k380/tests/driver-build/k380-driver.overlay
```

Check the generated `zephyr.dts` for `soft_off_wakeup_sources`, with its
`wakeup-sources` phandle targeting the K380 matrix, and `wakeup-source` on that
matrix. Check `.config` for `CONFIG_K380_KSCAN_NO_DIODE_MATRIX=y`, `CONFIG_HWINFO=y`,
and `CONFIG_PM_DEVICE=y`, and confirm `kscan_k380_no_diode_matrix.c.obj` exists.
The driver has compile-time assertions for matrix dimensions, a wake-capable
matrix, and an explicit soft-off wake-source node.

## Reset and physical wake contract

System-off wake on nRF52840 is a reset, not an ordinary PM resume. The driver reads
Zephyr HWINFO `RESET_LOW_POWER_WAKE` during initialization, then clears stale reset
causes. It consumes all matrix events until the first complete released frame,
with no hold timeout. A key already released before the first scan does not cause
the next independent key press to be consumed. Unsupported reset-cause reads fail
driver initialization instead of guessing from a key press or a persisted reason.

The available K380 bootloader sources do not clear RESETREAS. Physical validation
must still confirm the deployed bootloader/MBR preserves the system-off reset
cause for the application and that column GPIO SENSE wakes for every valid key.
No retained-RAM flag or guessed shutdown-reason marker is used.

Physical checks still required before enabling product automatic system-off:

1. Hold a key through a shutdown warning: radio/LED remain quiet, no new HID is
   produced, and system-off waits for all keys to release.
2. Wake with each valid key; a long wake hold and its release produce no HID.
3. Verify subsequent keys are delivered and connected-idle first keys are normal.
4. Verify wake source GPIO levels, leakage, battery current, and USB cancellation
   on the deployed board/bootloader combination.

Automatic system-off remains disabled in the product configuration. These tests
do not substitute for a successful nRF build or physical current/wake measurement.
