# K380 PRE_KERNEL_2 J-Link Trace

Run from `E:\project\k380-keyboard`:

```bat
k380-zmk\docs\k380\jlink-debug\run-k380-prekernel2-sequence.bat
```

To inspect nRF CLOCK state while the firmware is waiting:

```bat
k380-zmk\docs\k380\jlink-debug\run-k380-read-clock-regs.bat
```

Close Ozone, RTT Viewer, and J-Link GDB Server first. Only one tool should own the J-Link connection.

The sequence script resets the nRF52840, reads key Zephyr init table entries, sets hardware breakpoints, then repeatedly runs until a breakpoint or timeout and dumps registers plus `k380_kscan_diag_snapshot`.

Expected init table values for the current artifact:

```text
0x00066D68 -> 00055DC9  pre_probe
0x00066DB8 -> 000529C9  sys_clock_driver_init
0x00066DC0 -> 000336F1  xoshiro128_initialize
0x00066DE0 -> 00055DD9  post_probe
0x00066E40 -> 00055DF9  boot_probe
0x00066E80 -> 00056D59  k380_kscan_init
0x00066E90 -> 00055DE9  application_probe
```

Important breakpoint addresses:

```text
0x00057100  z_sys_init_run_level
0x000529C8  sys_clock_driver_init
0x00057110  return from init function call inside z_sys_init_run_level
0x000336F0  xoshiro128_initialize
0x00057314  prepare_multithreading
```

Interpretation:

```text
PC=0x00057100, R0=1 -> PRE_KERNEL_1
PC=0x00057100, R0=2 -> PRE_KERNEL_2
PC=0x000529C8 -> entered RTC timer init
PC=0x000336F0 -> entered xoshiro random init
PC=0x00057314 -> PRE_KERNEL_2 returned; entering multithreading preparation
PC around 0x0005EFD0 with LR around 0x0005168F -> waiting in lfclk_spinwait
```

The LFCLK root cause found during bring-up:

```text
K380 has no external 32.768 kHz crystal.
Old artifacts selected CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y.
While halted in lfclk_spinwait, CLOCK.EVENTS_LFCLKSTARTED stayed 0 and LFCLKSRC was 1.
K380 firmware must select CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y.
```

After flashing a new GitHub Actions artifact, run the pre-kernel sequence again. A good RC-LFCLK
artifact should not remain in the `lfclk_spinwait` state, and should reach either
`xoshiro128_initialize`, `prepare_multithreading`, or later K380 POST_KERNEL/Application probes.
