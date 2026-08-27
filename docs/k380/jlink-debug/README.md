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
0x00066D68 -> 0005628D  pre_probe
0x00066DB8 -> 00052DA1  sys_clock_driver_init
0x00066DC0 -> 00033891  xoshiro128_initialize
0x00066DE0 -> 0005629D  post_probe
0x00066E40 -> 000562BD  boot_probe
0x00066E80 -> 0005721D  k380_kscan_init
0x00066E90 -> 000562AD  application_probe
```

Important breakpoint addresses:

```text
0x00052DA0  sys_clock_driver_init
0x00033890  xoshiro128_initialize
0x0005628C  k380_kscan_direct_rtt_pre_kernel_probe
0x0005629C  k380_kscan_direct_rtt_post_kernel_probe
0x000562AC  k380_kscan_direct_rtt_application_probe
0x000562BC  k380_kscan_direct_rtt_boot_probe
0x0005721C  k380_kscan_init
```

Interpretation:

```text
PC=0x00052DA0 -> entered RTC timer init
PC=0x00033890 -> entered xoshiro random init
PC=0x0005628C / 0x0005629C / 0x000562BC -> entered K380 probe hooks
PC=0x0005721C -> K380 matrix init
```

The LFCLK root cause found during bring-up:

```text
K380 has no external 32.768 kHz crystal.
Old artifacts selected CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y.
While halted in lfclk_spinwait, CLOCK.EVENTS_LFCLKSTARTED stayed 0 and LFCLKSRC was 1.
K380 firmware must select CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y.
```

After flashing a new GitHub Actions artifact, run the pre-kernel sequence again. A good RC-LFCLK
artifact should not remain in the `lfclk_spinwait` state, and should reach the K380 probe hooks
listed above. Check `k380_kscan_diag_snapshot` at `0x200029CC`.
