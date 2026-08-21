# K380 Board Contract Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a K380 board-contract CI failure identify the exact assertion stage and generated-artifact state without changing firmware behavior.

**Architecture:** The `board-build` job keeps its existing build and assertion semantics. Its Python validator receives an outer diagnostic handler that prints the assertion message and compact summaries of the generated DTS and firmware artifact address ranges before re-raising the failure.

**Tech Stack:** GitHub Actions YAML, embedded Python 3, Zephyr generated DTS, Intel HEX, UF2.

---

### Task 1: Make Contract Failures Observable

**Files:**
- Modify: `.github/workflows/k380-ci.yml`
- Test: inline Python smoke fixture executed with `python3`

- [x] **Step 1: Write the failing regression probe**

Run a Python fixture that raises `AssertionError("generated row GPIO contract mismatch")` inside the planned diagnostic handler and assert that its output contains both the assertion text and the `K380 board contract diagnostics` heading.

- [x] **Step 2: Verify the probe fails before implementation**

Run: `python3 -c "raise AssertionError('diagnostic handler is not defined')"`

Expected: nonzero exit with `AssertionError`, because the diagnostic handler does not yet exist in the workflow.

- [x] **Step 3: Add the minimal diagnostic handler**

Wrap the existing board-contract assertions in `try`/`except AssertionError`. On failure, print the assertion message, matching `k380_kscan` and `chosen` sections from `zephyr.dts`, parsed partition data, and internal-flash ranges from `zmk.hex` and `zmk.uf2`; then re-raise.

- [x] **Step 4: Run syntax and regression verification**

Run the workflow's embedded Python through `ast.parse`, run the diagnostic fixture, and run `git diff --check`.

Expected: the diagnostic fixture prints the required heading and assertion message; Python parsing and whitespace validation pass.

- [x] **Step 5: Commit and push**

### Task 2: Publish Failure Evidence as Check-Run Annotations

**Files:**
- Modify: `.github/workflows/k380-ci.yml`
- Test: inline Python fixtures executed with `python3` and extracted validator shell executed with `bash`

- [x] **Step 1: Write the failing regression probe**

Require the workflow to capture validator stderr, emit a `K380 board contract assertion` error annotation, and emit diagnostic notices.

- [x] **Step 2: Capture the assertion without changing its exit status**

Run the existing validator with stderr redirected to `contract_failure.log`. On a nonzero exit, emit its final assertion line as a GitHub Actions error annotation, replay stderr, and exit with the original status.

- [x] **Step 3: Publish compact generated-artifact diagnostics**

Emit notice annotations for generated `chosen`, `k380_kscan`, fixed partitions, and internal Flash address ranges from the HEX and UF2 artifacts.

- [x] **Step 4: Verify annotations locally**

Execute the extracted validator shell with a missing source fixture and verify that it emits the error annotation and remains nonzero. Execute the extracted diagnostics Python with generated DTS, HEX, and UF2 fixtures and verify its notice annotations.

- [x] **Step 5: Push and read remote check-run annotations**

Push the workflow change, wait for `K380 CI`, retrieve the check-run annotations through the public GitHub API, and use the exact assertion to start the source-level fix.

### Task 3: Correct Zephyr GPIO Flag Contract

**Files:**
- Modify: `.github/workflows/k380-ci.yml`
- Test: inline Python fixture executing the embedded contract validator

- [x] **Step 1: Reproduce the generated GPIO flag mismatch**

Use a generated DTS fixture with row flags of `0x02`. Verify the prior workflow mapping rejects it because `GPIO_OPEN_SOURCE` and the expected row flags were set to `0x04`.

- [x] **Step 2: Correct the GPIO flag constants**

Match Zephyr's definitions: `GPIO_OPEN_SOURCE` is `0x02` and `GPIO_OPEN_DRAIN` is `0x06`. Set the eight K380 expected row flags to `0x02`.

- [x] **Step 3: Verify positive and negative cases**

Run the actual embedded contract validator with an otherwise valid fixture. Require generated row flags of `0x02` to pass and `0x04` to fail with `generated row GPIO contract mismatch`.

- [x] **Step 4: Push and verify remote K380 CI**

Push the corrected contract and require the `board-build` job to complete successfully before marking the K380 CI integration verified.

Evidence: `K380 CI` run `32458066955` completed with all five jobs successful.

Commit only the workflow and plan updates with a conventional `fix(k380)` message, then push the current feature branch to trigger the remote board build.
