# K380 No-Diode Matrix Design

**Status:** Approved

**Goal:** Add a ZMK board module for an nRF52840-QIAA K380 keyboard with a no-diode matrix scanner that suppresses only electrically ambiguous new key presses.

## Scope

The implementation is isolated in an external module named `zmk-keyboard-k380`.
It must not modify ZMK's in-tree `zmk,kscan-gpio-matrix` binding, its Kconfig,
or `app/module/drivers/kscan/kscan_gpio_matrix.c`.

The module contains both the K380 board definition and the K380-only scanner.
The scanner is selected only by a unique devicetree compatible:

```text
k380,kscan-no-diode-matrix
```

No existing ZMK board or shield will reference this compatible.

## Hardware Baseline

- SoC: nRF52840-QIAA
- Matrix: 8 rows by 15 columns
- Matrix topology: no diodes
- Scan direction: row2col
- Row electrical mode: active high, open source output
- Column electrical mode: active high, pull-down input
- Normal firmware update: Adafruit nRF52 Bootloader and UF2
- First flash, debugging, and recovery: J-Link/SWD

The canonical signal assignments are in
`docs/superpowers/specs/2026-08-14-k380-pinmap.md`.

## Architecture

```text
GPIO scan frame
    -> raw 8x15 matrix
    -> rectangular ambiguity detection
    -> filtered logical matrix
    -> existing debounce rules
    -> ZMK key position events
    -> keymap and HID output
```

The scanner keeps the standard GPIO scan, wakeup, power-management, and
debounce behavior from the ZMK matrix driver. The only functional change is
between collection of one complete scan frame and debounce processing.

## Ambiguity Policy

For every complete scan frame, create one 15-bit active mask per row. For
every pair of rows, calculate the intersection of their active masks.

If an intersection contains two or more active columns, all intersections for
those two rows and common columns belong to an ambiguous rectangle.

The output policy is:

1. A raw inactive cell is released normally.
2. A previously debounced pressed cell remains pressed while its raw cell stays
   active, even when it becomes ambiguous.
3. A newly active ambiguous cell is withheld from debounce and produces no
   press event.
4. A newly active non-ambiguous cell is passed to debounce.
5. A withheld physical key becomes eligible for debounce after the ambiguous
   rectangle disappears and the key remains active.

This policy prevents a ghost corner from producing a HID press while avoiding
a global limit on normal four-key and larger combinations.

No software policy can distinguish three physical corners plus one ghost
corner from four physical corners in the same 2x2 rectangle. The product
behavior for a full physical rectangle is therefore deliberate: later,
ambiguous presses are delayed until the rectangle is no longer ambiguous.

## Module Layout

```text
zmk-keyboard-k380/
  zephyr/module.yml
  Kconfig
  CMakeLists.txt
  boards/k380/k380/
    board.yml
    Kconfig.k380
    Kconfig.defconfig
    board.cmake
    pre_dt_board.cmake
    k380_nrf52840_zmk.dts
    k380_nrf52840_zmk_defconfig
    k380.keymap
    k380.zmk.yml
    k380-pins-rev-a.dtsi
  dts/bindings/kscan/
    k380,kscan-no-diode-matrix.yaml
  drivers/kscan/
    CMakeLists.txt
    Kconfig
    kscan_k380_no_diode_matrix.c
  src/
    ghost_filter.c
  include/zmk_keyboard_k380/
    ghost_filter.h
  tests/ghost-filter/
```

`ghost_filter.c` is a pure frame-to-frame filter. It receives raw matrix state
and prior accepted state and returns a filtered matrix plus an ambiguity mask.
It does not read GPIO, schedule work, emit ZMK events, or access devicetree.

`kscan_k380_no_diode_matrix.c` owns GPIO scanning and debounce integration. It
is a maintained derivative of the ZMK matrix scanner. Its file header must
record the exact upstream ZMK commit from which it was derived.

## Board Integration

The board DTS must:

- Include the nRF52840-QIAA SoC description.
- Include the Bootloader-compatible UF2 boot-mode and partition configuration.
- Include exactly one K380 pinmap revision file.
- Instantiate `k380,kscan-no-diode-matrix`.
- Define the 8 row GPIOs and 15 column GPIOs from the selected pinmap.
- Define a matrix transform containing only physical K380 keys.
- Select the K380 kscan and physical layout through the ZMK chosen node.

The matrix transform and default keymap require a supplied table mapping every
physical K380 key to its row and column coordinates. Unused electrical
intersections are excluded from the transform.

## Pinmap Versioning

All board-level GPIO references must originate from a revisioned pinmap DTSI
file. The pinmap DTSI owns the K380 kscan node and its `row-gpios` and
`col-gpios` properties. The board DTS includes the selected pinmap, then
references the labelled kscan node from its chosen node. The driver accepts
GPIO arrays from devicetree and has no hard-coded nRF52840 pin numbers.

Board revision changes use this sequence:

1. Create `k380-pins-rev-b.dtsi` from the previous revision.
2. Change only signal assignments and optional peripheral nodes in the new
   revision file.
3. Select the new revision from the board DTS or a revision-specific board
   variant.
4. Keep the previous revision file buildable for existing hardware.
5. Update the pinmap document with a revision comparison and validation result.

This permits matrix rerouting and later LED, battery, display, encoder, or
power-control additions without modifying ghost-filter logic.

## Test Plan

### Pure Filter Tests

The module test suite must cover:

- One key, same-row multi-key, and same-column multi-key frames.
- Four or more active keys that do not form a rectangular ambiguity.
- Three physical rectangle corners represented as four raw active corners.
- Release of a known key while an ambiguity exists.
- Resolution of an ambiguity while a real withheld key remains held.
- Multiple independent ambiguous rectangles.
- A full physical rectangle, with expected delayed presses documented.

### Build Isolation Tests

- Build the K380 board and verify the K380 scanner configuration is enabled.
- Build one existing ZMK nRF52840 board with the K380 module registered and
  verify the K380 scanner configuration is disabled.
- Verify that the only K380 driver object is absent from the existing-board
  build.
- Verify the in-tree ZMK worktree has no functional source modifications.

### Hardware Acceptance Tests

- First flash through J-Link/SWD.
- Recovery through J-Link/SWD after an intentionally invalid application image.
- Normal UF2 upgrade through the Adafruit nRF52 Bootloader.
- Full matrix walk test.
- Non-rectangular four-key and modifier combinations.
- Known ambiguous rectangle sequences with no ghost HID event.
- USB and BLE typing.
- Idle, wakeup, and low-power current measurement.

## Pre-Implementation Inputs

Implementation starts only after these two inputs are supplied:

1. The physical-key to row-and-column matrix table for every K380 key.
2. The exact Adafruit nRF52 Bootloader build and application partition start
   address used by the target board.

