# K380 真实矩阵集成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `docs/k380/pinmap.md` 中的真实 8x15 GPIO 矩阵接入 K380 board，并让 CI 验证扫描器实例、GPIO 合同和既有 Flash 边界，同时保留实板逐键验证为延期未完成任务。

**Architecture:** K380 board DTS 直接实例化现有 `k380,kscan-no-diode-matrix` 驱动，并将 `zmk,kscan` 指向该节点。board-build 使用真实 board 节点，不再使用虚拟 GPIO；CI 在生成 DTS 上检查 23 个 GPIO 的顺序、控制器、引脚和 flags。matrix transform、默认 keymap、灯效、电池和实板刷写继续不实现。

**Tech Stack:** Zephyr devicetree、ZMK、K380 专用 kscan module、GitHub Actions、Python 3、PowerShell。

---

## 文件结构与职责

| 文件 | 职责 |
| --- | --- |
| `app/boards/kimwolf/k380/k380_nrf52840_zmk.dts` | 添加真实 K380 kscan 节点和 `zmk,kscan` chosen，保留分区、UF2 retention 和 REG1 DC/DC。 |
| `zmk-keyboard-k380/tests/board-build/k380-board.overlay` | 只提供 CI 构建所需的测试 keymap，不再伪造矩阵 GPIO。 |
| `zmk-keyboard-k380/tests/board-build/k380-board.conf` | 保持 CI 的断言和轮询配置，避免 CI 对真实 GPIO 中断作硬件假设。 |
| `.github/workflows/k380-ci.yml` | 在 board-build 中验证真实矩阵源 DTS 与生成 DTS，并继续验证 HEX/UF2 分区范围。 |
| `docs/superpowers/specs/2026-08-21-k380-real-matrix-integration-design.md` | 已批准的设计和范围边界，实施中不得扩大。 |
| `docs/superpowers/plans/2026-08-21-k380-real-matrix-integration.md` | 本实施清单；实板验证任务必须保持未勾选，直到具备硬件并完成记录。 |
| `docs/k380/pinmap.md` | 真实 GPIO 唯一来源，只读使用，不在本阶段改写。 |
| `docs/k380/matrix-layout.md` | 物理键到 RC 坐标唯一来源，只读使用；不复制到 keymap。 |

## Task 1: 先建立真实矩阵的 CI 失败门禁

**Files:**
- Modify: `.github/workflows/k380-ci.yml`
- Modify: `zmk-keyboard-k380/tests/board-build/k380-board.overlay`
- Modify: `docs/superpowers/plans/2026-08-21-k380-real-matrix-integration.md`
- Test: 本地源文件静态门禁；远程 `K380 CI / board-build`

- [x] **Step 1: 写入会拒绝旧 board 的矩阵合同检查**

在现有 `board-build` 的 Python 验证脚本中，保留现有分区、`zephyr,code-partition`、REG1
DC/DC、HEX 和 UF2 检查，并新增以下实际检查数据：

```python
expected_matrix = {
    "compatible": 'compatible = "k380,kscan-no-diode-matrix";',
    "chosen": "zmk,kscan = &k380_kscan;",
    "rows": [
        "&gpio1 9 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 26 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 6 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 8 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 4 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 12 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 7 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
        "&gpio0 15 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)",
    ],
    "cols": [
        "&gpio0 5 GPIO_PULL_DOWN",
        "&gpio0 17 GPIO_PULL_DOWN",
        "&gpio0 20 GPIO_PULL_DOWN",
        "&gpio0 22 GPIO_PULL_DOWN",
        "&gpio1 2 GPIO_PULL_DOWN",
        "&gpio1 4 GPIO_PULL_DOWN",
        "&gpio1 6 GPIO_PULL_DOWN",
        "&gpio0 31 GPIO_PULL_DOWN",
        "&gpio0 29 GPIO_PULL_DOWN",
        "&gpio0 2 GPIO_PULL_DOWN",
        "&gpio1 13 GPIO_PULL_DOWN",
        "&gpio0 28 GPIO_PULL_DOWN",
        "&gpio0 3 GPIO_PULL_DOWN",
        "&gpio1 10 GPIO_PULL_DOWN",
        "&gpio1 11 GPIO_PULL_DOWN",
    ],
}
```

Normalize whitespace before comparing source fragments. For generated DTS, parse the `k380_kscan`
node and the `chosen` property rather than relying on the source node label alone, because Zephyr
may normalize labels and whitespace. Assert that there is exactly one node with the K380 compatible,
exactly 8 `row-gpios`, exactly 15 `col-gpios`, and no `zmk,matrix-transform`,
`zmk,battery`, `ws2812`, `led-strip`, or `&reg0` DC/DC fragment in the board source.

- [x] **Step 2: Remove the virtual matrix from the board-build overlay**

Replace `zmk-keyboard-k380/tests/board-build/k380-board.overlay` with a test-only keymap overlay:

```dts
#include <behaviors.dtsi>

/ {
    keymap {
        compatible = "zmk,keymap";

        default_layer {
            bindings = <&none>;
        };
    };
};
```

Do not leave `#include <zephyr/dt-bindings/gpio/gpio.h>`, `row-gpios`, `col-gpios`,
`k380_board_build_kscan`, or any GPIO number in this overlay. The board DTS is now the only
matrix definition used by this build.

- [x] **Step 3: Run the static gate and verify it fails for the expected reason**

Run from `E:\project\k380-keyboard\zmk`:

```powershell
$dts = Get-Content -Raw "app\boards\kimwolf\k380\k380_nrf52840_zmk.dts"
$overlay = Get-Content -Raw "zmk-keyboard-k380\tests\board-build\k380-board.overlay"
if ($overlay -match 'row-gpios|col-gpios|k380_board_build_kscan|<&gpio') {
  throw "board-build overlay 仍包含虚拟矩阵"
}
if ($dts -notmatch 'k380,kscan-no-diode-matrix') {
  throw "预期失败：真实 K380 kscan 尚未加入 board DTS"
}
```

Expected result before Task 2: the command throws
`预期失败：真实 K380 kscan 尚未加入 board DTS`.
This is the red test proving the new contract does not silently pass against the old minimal board.

- [x] **Step 4: Commit the red CI gate**

```powershell
git add .github/workflows/k380-ci.yml `
  zmk-keyboard-k380/tests/board-build/k380-board.overlay `
  docs/superpowers/plans/2026-08-21-k380-real-matrix-integration.md
git commit -m "test(k380): 添加真实矩阵 CI 门禁"
```

Expected result: one commit is created; no board DTS implementation is included yet.

## Task 2: Add the real K380 matrix node

**Files:**
- Modify: `app/boards/kimwolf/k380/k380_nrf52840_zmk.dts`
- Modify: `zmk-keyboard-k380/tests/board-build/k380-board.conf`
- Test: Task 1 static gate and remote `K380 CI / board-build`

- [x] **Step 1: Add the chosen node and exact row/column GPIO arrays**

Inside `/ { ... };` in `app/boards/kimwolf/k380/k380_nrf52840_zmk.dts`, extend `chosen` and add
one labeled node:

```dts
    chosen {
        zephyr,code-partition = &code_partition;
        zephyr,sram = &sram0;
        zephyr,flash = &flash0;
        zmk,kscan = &k380_kscan;
    };

    k380_kscan: k380_kscan {
        compatible = "k380,kscan-no-diode-matrix";
        row-gpios =
            <&gpio1 9 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 26 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 6 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 8 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 4 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 12 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 7 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 15 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>;
        col-gpios =
            <&gpio0 5 GPIO_PULL_DOWN>,
            <&gpio0 17 GPIO_PULL_DOWN>,
            <&gpio0 20 GPIO_PULL_DOWN>,
            <&gpio0 22 GPIO_PULL_DOWN>,
            <&gpio1 2 GPIO_PULL_DOWN>,
            <&gpio1 4 GPIO_PULL_DOWN>,
            <&gpio1 6 GPIO_PULL_DOWN>,
            <&gpio0 31 GPIO_PULL_DOWN>,
            <&gpio0 29 GPIO_PULL_DOWN>,
            <&gpio0 2 GPIO_PULL_DOWN>,
            <&gpio1 13 GPIO_PULL_DOWN>,
            <&gpio0 28 GPIO_PULL_DOWN>,
            <&gpio0 3 GPIO_PULL_DOWN>,
            <&gpio1 10 GPIO_PULL_DOWN>,
            <&gpio1 11 GPIO_PULL_DOWN>;
        debounce-press-ms = <5>;
        debounce-release-ms = <5>;
        debounce-scan-period-ms = <1>;
        poll-period-ms = <10>;
    };
```

Keep the existing `&reg1`, `&flash0`, five fixed partitions, UF2 boot-mode include, and
`zephyr,code-partition` unchanged. Do not add a matrix transform, default keymap, LED node, battery
node, `&reg0` DCDC configuration, or P0.13.

- [x] **Step 2: Keep CI polling explicit**

Verify `zmk-keyboard-k380/tests/board-build/k380-board.conf` contains:

```conf
CONFIG_ASSERT=y
CONFIG_ZMK_KSCAN_MATRIX_POLLING=y
```

Do not add `CONFIG_ZMK_KSCAN_MATRIX_POLLING=y` to the production board defconfig. The board must
retain the driver's normal interrupt-capable path for later hardware validation; CI polling only
avoids depending on physical GPIO interrupt behavior in the build container.

- [x] **Step 3: Run the source contract checks**

Run:

```powershell
$dts = Get-Content -Raw "app\boards\kimwolf\k380\k380_nrf52840_zmk.dts"
$required = @(
  'zmk,kscan = &k380_kscan;',
  'compatible = "k380,kscan-no-diode-matrix";',
  '<&gpio1 9 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>',
  '<&gpio0 26 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>',
  '<&gpio1 11 GPIO_PULL_DOWN>',
  'debounce-press-ms = <5>;',
  'debounce-release-ms = <5>;'
)
$missing = $required | Where-Object { -not $dts.Contains($_) }
if ($missing) { throw "真实矩阵 DTS 缺少: $($missing -join ', ')" }
if ($dts -match 'zmk,matrix-transform|zmk,battery|ws2812|led-strip|reg0') {
  throw "真实矩阵阶段包含越界功能"
}
git diff --check
```

Expected result: no missing fragments, no scope violation, and `git diff --check` exits 0.

- [x] **Step 4: Run the remote build verification**

Push the feature branch:

```powershell
git add app/boards/kimwolf/k380/k380_nrf52840_zmk.dts `
  zmk-keyboard-k380/tests/board-build/k380-board.conf
git commit -m "feat(k380): 接入真实矩阵 GPIO"
git push -u origin feat/k380-real-matrix-integration
```

Wait for `K380 CI` and require these jobs to pass:

```text
module-metadata: success
ghost-filter: success
driver-build: success
board-build: success
module-isolation: success
```

`board-build` must prove that the generated DTS preserves the 23 real GPIO entries and that
`zephyr.hex` and `zephyr.uf2` remain within `0x00026000..0x000CA000`.

Evidence: `K380 CI` run `32458066955` completed with all five jobs successful.

## Task 3: Record deferred hardware verification without marking it complete

**Files:**
- Modify: `docs/superpowers/plans/2026-08-21-k380-real-matrix-integration.md`
- Modify: `docs/k380/hardware-contract.md`
- Test: document consistency and unchecked deferred-task audit

- [x] **Step 1: Add the deferred verification checklist to the plan**

Keep the following checklist unchecked until a physical K380 board is available:

### Deferred: K380 实板矩阵逐键验证

- [ ] 准备可观察 `(row, column)` 的矩阵诊断固件；
- [ ] 记录 PCB revision、测试日期、测试固件提交和 SWD/UF2 烧写路径；
- [ ] 验证 80 个有效按键各自只上报 `matrix-layout.md` 坐标；
- [ ] 验证 40 个未使用坐标不产生事件；
- [ ] 验证非歧义多键、矩形歧义、保持和释放；
- [ ] 验证真实 P0/P1 GPIO 中断唤醒、扫描停止和异常恢复；
- [ ] 记录异常并先更新唯一来源文档。

Do not write “verified”, “passed”, or a completion date for these items. CI success must be described
as build/configuration evidence only.

- [x] **Step 2: Clarify the hardware-contract verification status**

In `docs/k380/hardware-contract.md`, update only the matrix-related verification wording so it
states that the GPIO and RC tables are recorded hardware facts awaiting physical confirmation, and
that the confirmation remains a required bring-up task. Do not alter the GPIO values, RC mapping,
Flash boundaries, or power/Bootloader facts.

- [x] **Step 3: Run the deferred-task audit**

Run:

```powershell
$plan = Get-Content -Raw "docs\superpowers\plans\2026-08-21-k380-real-matrix-integration.md"
if ($plan -notmatch 'Deferred: K380') { throw "缺少延期实板任务标题" }
if ($plan -notmatch '(?m)^- \[ \].*80 个有效按键') { throw "80 键逐键任务不应完成" }
if ($plan -notmatch '(?m)^- \[ \].*40 个未使用坐标') { throw "40 个未使用坐标任务不应完成" }
if ($plan -match '(?m)^- \[x\].*(逐键|未使用坐标|真实 P0/P1)') {
  throw "实板验证被错误标记为完成"
}
git diff --check
```

Expected result: all assertions pass and all deferred physical checks remain unchecked.

- [x] **Step 4: Commit the documentation status**

```powershell
git add docs/k380/hardware-contract.md `
  docs/superpowers/plans/2026-08-21-k380-real-matrix-integration.md
git commit -m "docs(k380): 保留矩阵实板验证任务"
```

## Task 4: Final verification and handoff

**Files:**
- Test: merged feature branch
- No new implementation files

- [x] **Step 1: Check repository and branch state**

```powershell
git status --short
git branch --show-current
git log --oneline -5
```

Expected result: current branch is `feat/k380-real-matrix-integration`; working tree is clean; the
latest commits contain the real GPIO implementation and the deferred verification documentation.

- [x] **Step 2: Run static validation on the merged result**

```powershell
git diff --check k380..HEAD
$dts = Get-Content -Raw "app\boards\kimwolf\k380\k380_nrf52840_zmk.dts"
$overlay = Get-Content -Raw "zmk-keyboard-k380\tests\board-build\k380-board.overlay"
if ($overlay -match 'row-gpios|col-gpios|k380_board_build_kscan|<&gpio') {
  throw "CI overlay 仍伪造矩阵"
}
if ($dts -notmatch 'zmk,kscan = &k380_kscan;') {
  throw "board 未选择真实 K380 kscan"
}
if ($dts -match 'zmk,matrix-transform|zmk,battery|ws2812|led-strip') {
  throw "实现超出当前阶段范围"
}
Write-Output "K380 真实矩阵源配置和范围检查通过。"
```

Expected result:

```text
K380 真实矩阵源配置和范围检查通过。
```

- [x] **Step 3: Confirm remote CI evidence**

Record the exact successful `K380 CI` run ID and its five successful jobs in the final handoff.
State explicitly that this proves board build and configuration contracts only, not SWD, UF2,
interrupt behavior, physical key coordinates, ghost-filter behavior on the real PCB, USB, LED, or
battery behavior.

Evidence: `K380 CI` run `32458066955` completed successfully:
`module-metadata`, `ghost-filter`, `driver-build`, `board-build`, and
`module-isolation`.

- [x] **Step 4: Keep the branch for review**

Do not merge or delete `feat/k380-real-matrix-integration` in this implementation phase. The branch
must remain available for review and for later hardware-validation follow-up.
