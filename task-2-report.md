# Task 2 Report

## Status

Implemented and committed as `0eb7c4a7` (`feat(k380): bound BLE wait and pairing power use`).

## Changes

- Added dirty-aware active BLE profile persistence, including retry-safe flush behavior.
- Added startup advertising gate plus explicit stop/resume APIs.
- Wired low-power cleanup to stop advertising, flush dirty profile state, and resume only after safe startup qualification.
- Added Kconfig defaults for 10-second bonded wait and 60-second pairing timeouts.
- Added Z5/Z7 timeout ownership, cancellation, stale-fact checks, battery low-power requests, and USB pairing expiry cleanup.
- Extended native slot-policy fakes/tests for same-slot selection, battery wait expiry, and USB pairing expiry.

## Verification

- `git diff --check`: passed (only existing LF/CRLF conversion warnings).
- Requested native test command could not run because `west` is not installed in this environment. No GitHub workflow was dispatched.

## Concerns

- Full Zephyr/native_sim verification remains pending in CI or an environment with the ZMK west toolchain.
- The timeout callbacks re-check current profile, connection, bond/open state, and power state; normal Zephyr delayed-work cancellation handles superseded callbacks.
