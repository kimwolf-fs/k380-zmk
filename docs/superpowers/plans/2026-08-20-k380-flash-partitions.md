# K380 ZMK Flash Partition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建 `k380/nrf52840/zmk` 最小 board，使 ZMK 应用代码、Settings 存储和 Adafruit nRF52 Bootloader 保留区域具有经过构建验证的固定 Flash 边界。

**Architecture:** 新 board 只声明 nRF52840-QIAA、Adafruit UF2 boot-mode retention、REG1 DC/DC 和连续的 1 MiB `fixed-partitions`。K380 CI 新增独立 board-build job，以测试专用的虚拟矩阵和单键 keymap 触发完整 ZMK 构建，并从生成的 DTS、HEX 和 UF2 验证应用不会进入 SoftDevice、DFU 数据或 Bootloader 区。

**Tech Stack:** Zephyr devicetree、Kconfig、ZMK、nRF52840、Adafruit UF2 Bootloader、GitHub Actions、Python 3。

---

## 边界与前提

已验证的 Bootloader Flash 布局来自 `k380-bootloader` 的 `k380` 合并提交
`476577baf9134af8373f420d88a46e3ca2d4d5d9`：

```text
MBR + S140 6.1.1:      0x00000000 .. 0x00026000
ZMK 应用总窗口:         0x00026000 .. 0x000EA000  (0x000C4000)
Adafruit DFU/UF2 排除的应用保存数据保留区:
                       0x000EA000 .. 0x000F4000  (0x0000A000)
Bootloader 保留区:      0x000F4000 .. 0x00100000  (0x0000C000)
```

本阶段将 ZMK 应用窗口划分为：

```text
code_partition:         0x00026000 .. 0x000CA000  (0x000A4000)
storage_partition:      0x000CA000 .. 0x000EA000  (0x00020000)
```

`0x000EA000..0x000F4000` 是 Adafruit DFU/UF2 排除的应用保存数据保留区；项目选择将其
标记为只读且留空，不分配给 ZMK Settings，且它不属于 Bootloader 自身。

不得修改：

```text
app/module/drivers/kscan/
app/module/dts/bindings/kscan/
app/boards/ 的既有 board
zmk-keyboard-k380/drivers/kscan/
```

不得在此阶段实现真实矩阵 GPIO、matrix transform、默认 keymap、WS2812B、LED 状态机、电池采样
或任何实板烧写逻辑。

## 文件结构与职责

```text
app/boards/kimwolf/k380/
  board.yml
    注册 k380 board 及 nrf52840/zmk variant。
  Kconfig.k380
    选择 nRF52840-QIAA 与 ZMK boot-mode retention 依赖。
  k380_nrf52840_zmk.dts
    SoC、UF2 boot-mode、REG1 DC/DC、Flash 分区与 zephyr chosen 节点。
  k380_nrf52840_zmk_defconfig
    应用分区、UF2、Flash Map、NVS/Settings、USB、BLE 的基础配置。

zmk-keyboard-k380/tests/board-build/
  k380-board.conf
    仅用于编译夹具的轮询与断言配置。
  k380-board.overlay
    仅用于编译夹具的虚拟 8x15 kscan 和单键 keymap；不是 K380 硬件定义。

.github/workflows/k380-ci.yml
  新增 board-build job，验证最小 board 构建、分区、DCDC、HEX 和 UF2 地址。

docs/k380/hardware-contract.md
  记录已审核的 Bootloader 证据和最终分区边界。

docs/superpowers/specs/2026-08-20-k380-flash-partitions-design.md
  实施完成后标记为已完成。
```

### Task 1: 添加会失败的 K380 board 构建夹具和 CI 门禁

**Files:**
- Create: `zmk-keyboard-k380/tests/board-build/k380-board.conf`
- Create: `zmk-keyboard-k380/tests/board-build/k380-board.overlay`
- Modify: `.github/workflows/k380-ci.yml`
- Test: `K380 CI / board-build`

- [x] **Step 1: 创建测试专用配置片段**

创建 `zmk-keyboard-k380/tests/board-build/k380-board.conf`：

```conf
CONFIG_ASSERT=y
CONFIG_ZMK_KSCAN_MATRIX_POLLING=y
```

该配置只让虚拟输入使用轮询，避免 CI 对不存在的 K380 实板中断引脚作出假设。

- [x] **Step 2: 创建会实例化专用矩阵驱动的测试 overlay**

创建 `zmk-keyboard-k380/tests/board-build/k380-board.overlay`：

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <behaviors.dtsi>

/ {
    chosen {
        zmk,kscan = &k380_board_build_kscan;
    };

    k380_board_build_kscan: k380_board_build_kscan {
        compatible = "k380,kscan-no-diode-matrix";
        row-gpios =
            <&gpio0 0 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 1 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 2 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 3 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 4 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 5 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 6 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 7 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>;
        col-gpios =
            <&gpio0 8 GPIO_PULL_DOWN>,
            <&gpio0 9 GPIO_PULL_DOWN>,
            <&gpio0 10 GPIO_PULL_DOWN>,
            <&gpio0 11 GPIO_PULL_DOWN>,
            <&gpio0 12 GPIO_PULL_DOWN>,
            <&gpio0 13 GPIO_PULL_DOWN>,
            <&gpio0 14 GPIO_PULL_DOWN>,
            <&gpio0 15 GPIO_PULL_DOWN>,
            <&gpio0 16 GPIO_PULL_DOWN>,
            <&gpio0 17 GPIO_PULL_DOWN>,
            <&gpio0 18 GPIO_PULL_DOWN>,
            <&gpio0 19 GPIO_PULL_DOWN>,
            <&gpio0 20 GPIO_PULL_DOWN>,
            <&gpio0 21 GPIO_PULL_DOWN>,
            <&gpio0 22 GPIO_PULL_DOWN>;
        debounce-press-ms = <1>;
        debounce-release-ms = <1>;
        debounce-scan-period-ms = <1>;
        poll-period-ms = <10>;
    };

    keymap {
        compatible = "zmk,keymap";

        default_layer {
            bindings = <&none>;
        };
    };
};
```

该 overlay 只为构建准备设备树依赖。上述 GPIO 不能复制到后续 K380 board DTS，也不代表
`docs/k380/pinmap.md`。

- [x] **Step 3: 在 workflow 的触发路径中包含新 board、夹具、规格和计划**

在 `.github/workflows/k380-ci.yml` 的 `push.paths` 与 `pull_request.paths` 各加入：

```yaml
      - "app/boards/kimwolf/k380/**"
      - "zmk-keyboard-k380/tests/board-build/**"
      - "docs/superpowers/specs/2026-08-20-k380-flash-partitions-design.md"
      - "docs/superpowers/plans/2026-08-20-k380-flash-partitions.md"
```

- [x] **Step 4: 添加 board-build job，使缺失 board 时构建失败**

在 `driver-build` job 后加入下列 job。它复用当前 K380 CI 的容器、缓存和 module 注册门禁：

```yaml
  board-build:
    needs: module-metadata
    if: ${{ needs.module-metadata.outputs.module-registered == 'true' }}
    runs-on: ubuntu-latest
    container:
      image: docker.io/zmkfirmware/zmk-build-arm:4.1
    steps:
      - uses: actions/checkout@v7
      - uses: actions/cache@v6
        continue-on-error: true
        with:
          path: |
            modules/
            tools/
            zephyr/
            bootloader/
          key: ${{ runner.os }}-k380-${{ hashFiles('app/west.yml') }}
          restore-keys: |
            ${{ runner.os }}-k380-
      - run: west init -l app
      - run: west update --fetch-opt=--filter=tree:0
      - run: west zephyr-export
      - name: Build minimal K380 board fixture
        run: |
          west build -s app -d build/k380-board -p always -b k380/nrf52840/zmk -- \
            -DZMK_EXTRA_MODULES="${GITHUB_WORKSPACE}/zmk-keyboard-k380" \
            -DEXTRA_CONF_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/tests/board-build/k380-board.conf" \
            -DEXTRA_DTC_OVERLAY_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/tests/board-build/k380-board.overlay"
      - name: Validate K380 board contract
        run: |
          python3 - <<'PY'
          import re
          import struct
          from pathlib import Path

          source = Path("app/boards/kimwolf/k380/k380_nrf52840_zmk.dts").read_text(
              encoding="utf-8"
          )
          generated = Path("build/k380-board/zephyr/zephyr.dts").read_text(
              encoding="utf-8"
          )
          normalized_source = re.sub(r"\s+", "", source)

          required_source = (
              "#include<nordic/nrf52840_qiaa.dtsi>",
              "#include<common/nordic/nrf52840_uf2_boot_mode.dtsi>",
              "regulator-initial-mode=<NRF5X_REG_MODE_DCDC>;",
              "zephyr,code-partition=&code_partition;",
          )
          for fragment in required_source:
              assert fragment in normalized_source, fragment

          forbidden_source = (
              "zmk,kscan",
              "zmk,matrix-transform",
              "zmk,battery",
              "ws2812",
              "led-strip",
              "reg0{regulator-initial-mode=<NRF5X_REG_MODE_DCDC>;",
          )
          for fragment in forbidden_source:
              assert fragment not in normalized_source, fragment

          expected_partitions = {
              "0": (0x00000000, 0x00026000, True, "MBR and S140"),
              "26000": (0x00026000, 0x000A4000, False, "code"),
              "ca000": (0x000CA000, 0x00020000, False, "storage"),
              "ea000": (0x000EA000, 0x0000A000, True, "DFU data"),
              "f4000": (0x000F4000, 0x0000C000, True, "bootloader"),
          }

          def parse_partitions(dts):
              parsed = {}
              for match in re.finditer(
                  r"partition@(?P<address>[0-9a-fA-F]+)\s*\{(?P<body>.*?)\n\s*\};",
                  dts,
                  re.DOTALL,
              ):
                  reg = re.search(
                      r"reg\s*=\s*<\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>;",
                      match.group("body"),
                  )
                  assert reg, match.group("address")
                  parsed[f"{int(match.group('address'), 16):x}"] = (
                      int(reg.group(1), 0),
                      int(reg.group(2), 0),
                      "read-only;" in match.group("body"),
                  )
              return parsed

          for dts, name in ((source, "source"), (generated, "generated")):
              actual_partitions = parse_partitions(dts)
              for address, expected in expected_partitions.items():
                  expected_start, expected_size, expected_read_only, label = expected
                  actual = actual_partitions.get(address)
                  assert actual == (
                      expected_start,
                      expected_size,
                      expected_read_only,
                  ), (name, label, actual)

          code_start = 0x00026000
          code_end = 0x000CA000

          def internal_flash_ranges(hex_path):
              upper = 0
              ranges = []
              for line in hex_path.read_text(encoding="ascii").splitlines():
                  assert line.startswith(":"), line
                  count = int(line[1:3], 16)
                  address = int(line[3:7], 16)
                  record_type = int(line[7:9], 16)
                  data = bytes.fromhex(line[9 : 9 + count * 2])
                  if record_type == 4:
                      upper = int.from_bytes(data, "big") << 16
                  elif record_type == 0:
                      start = upper + address
                      end = start + count
                      if start < 0x00100000:
                          ranges.append((start, end))
              return ranges

          hex_ranges = internal_flash_ranges(Path("build/k380-board/zephyr/zephyr.hex"))
          assert hex_ranges
          for start, end in hex_ranges:
              assert code_start <= start < end <= code_end, (hex(start), hex(end))

          uf2 = Path("build/k380-board/zephyr/zephyr.uf2").read_bytes()
          assert len(uf2) % 512 == 0
          uf2_ranges = []
          for offset in range(0, len(uf2), 512):
              block = uf2[offset : offset + 512]
              magic0, magic1, _flags, target, payload_size = struct.unpack_from(
                  "<IIIII", block
              )
              assert (magic0, magic1) == (0x0A324655, 0x9E5D5157)
              if target < 0x00100000:
                  uf2_ranges.append((target, target + payload_size))
          assert uf2_ranges
          for start, end in uf2_ranges:
              assert code_start <= start < end <= code_end, (hex(start), hex(end))
          PY
      - if: failure()
        run: |
          find build/k380-board -type f -name '*.log' -print -exec tail -n 200 {} \;
      - if: always()
        uses: actions/upload-artifact@v7
        with:
          name: k380-board-build-output
          path: |
            build/k380-board/**/*.log
            build/k380-board/**/zephyr/.config
            build/k380-board/**/zephyr/zephyr.dts
            build/k380-board/**/zephyr/zephyr.hex
            build/k380-board/**/zephyr/zephyr.uf2
          if-no-files-found: ignore
```

`expected_partitions` 的字符串比较需要先以 `re.sub(r"\s+", "", text)` 规整源 DTS 和
生成 DTS；代码已经完成该规整。HEX 解析仅检查 1 MiB 内部 Flash 记录，以避免 nRF UICR
记录干扰应用区间判断。

- [x] **Step 5: 推送夹具并确认 board-build 按预期失败**

```powershell
git add `
  .github/workflows/k380-ci.yml `
  zmk-keyboard-k380/tests/board-build/k380-board.conf `
  zmk-keyboard-k380/tests/board-build/k380-board.overlay
git commit -m "test(k380): 添加 board 分区构建夹具"
git push -u origin feat/k380-flash-partitions
```

预期：`module-metadata`、`ghost-filter`、`driver-build` 和 `module-isolation` 保持成功；
`board-build` 在 `west build -b k380/nrf52840/zmk` 处失败，因为 `k380` board 尚未存在。

### Task 2: 创建最小 K380 board，使分区构建通过

**Files:**
- Create: `app/boards/kimwolf/k380/board.yml`
- Create: `app/boards/kimwolf/k380/Kconfig.k380`
- Create: `app/boards/kimwolf/k380/k380_nrf52840_zmk.dts`
- Create: `app/boards/kimwolf/k380/k380_nrf52840_zmk_defconfig`
- Test: `K380 CI / board-build`

- [x] **Step 1: 创建 board 元数据**

创建 `app/boards/kimwolf/k380/board.yml`：

```yaml
board:
  name: k380
  vendor: kimwolf
  socs:
    - name: nrf52840
      variants:
        - name: zmk
```

- [x] **Step 2: 创建 Kconfig board 选择项**

创建 `app/boards/kimwolf/k380/Kconfig.k380`：

```kconfig
# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

config BOARD_K380
    select SOC_NRF52840_QIAA
    select ZMK_BOARD_COMPAT if BOARD_K380_NRF52840_ZMK
    imply RETAINED_MEM if BOARD_K380_NRF52840_ZMK
    imply RETENTION if BOARD_K380_NRF52840_ZMK
    imply RETENTION_BOOT_MODE if BOARD_K380_NRF52840_ZMK
```

- [x] **Step 3: 创建最小 board DTS 与完整分区表**

创建 `app/boards/kimwolf/k380/k380_nrf52840_zmk.dts`：

```dts
/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

/dts-v1/;
#include <nordic/nrf52840_qiaa.dtsi>
#include <common/nordic/nrf52840_uf2_boot_mode.dtsi>
#include <zephyr/dt-bindings/regulator/nrf5x.h>

/ {
    model = "K380";
    compatible = "kimwolf,k380";

    chosen {
        zephyr,code-partition = &code_partition;
        zephyr,sram = &sram0;
        zephyr,flash = &flash0;
    };
};

&reg1 {
    regulator-initial-mode = <NRF5X_REG_MODE_DCDC>;
};

&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        mbr_softdevice_partition: partition@0 {
            reg = <0x00000000 0x00026000>;
            read-only;
        };

        code_partition: partition@26000 {
            reg = <0x00026000 0x000A4000>;
        };

        storage_partition: partition@ca000 {
            reg = <0x000CA000 0x00020000>;
        };

        dfu_app_data_partition: partition@ea000 {
            reg = <0x000EA000 0x0000A000>;
            read-only;
        };

        boot_partition: partition@f4000 {
            reg = <0x000F4000 0x0000C000>;
            read-only;
        };
    };
};
```

不添加 `zmk,kscan`、`zmk,matrix-transform`、`zmk,battery`、LED、WS2812B、P0.13 或
REG0/DCDC0 配置。

- [x] **Step 4: 创建应用与 Settings 基础 defconfig**

创建 `app/boards/kimwolf/k380/k380_nrf52840_zmk_defconfig`：

```conf
# Copyright (c) 2026
# SPDX-License-Identifier: MIT

CONFIG_ARM_MPU=y
CONFIG_PINCTRL=y
CONFIG_GPIO=y

CONFIG_USE_DT_CODE_PARTITION=y
CONFIG_BUILD_OUTPUT_UF2=y

CONFIG_MPU_ALLOW_FLASH_WRITE=y
CONFIG_NVS=y
CONFIG_SETTINGS_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_MAP=y

CONFIG_ZMK_USB=y
CONFIG_ZMK_BLE=y
```

- [x] **Step 5: 运行同一 CI，确认 board-build 由红变绿**

```powershell
git add app/boards/kimwolf/k380
git commit -m "feat(k380): 添加最小 ZMK board 分区"
git push
```

预期：`K380 CI / board-build` 成功。其验证脚本确认 DCDC、五个分区、代码分区、内部 Flash
HEX 段和 UF2 block 都没有进入 `0x00000000..0x00026000`、
`0x000EA000..0x00100000` 的保留区域。

### Task 3: 记录 Bootloader 证据并完成分区阶段文档

**Files:**
- Modify: `docs/k380/hardware-contract.md`
- Modify: `docs/superpowers/specs/2026-08-20-k380-flash-partitions-design.md`
- Modify: `docs/superpowers/plans/2026-08-20-k380-flash-partitions.md`
- Test: 文档与分区边界静态检查

- [x] **Step 1: 更新硬件契约状态与 Flash 门禁结论**

将 `docs/k380/hardware-contract.md` 开头状态替换为：

```markdown
**状态：** 已确认硬件事实；K380 Bootloader 已验证应用 Flash 区间为
`0x00026000..0x000EA000`，ZMK code 与 Settings 分区必须保持本文档所列边界。
```

将 `## Bootloader 与 ZMK 配置门禁` 的“已确认答案”替换为：

```markdown
**已确认答案：** K380 Bootloader `k380` 分支的合并提交
`476577baf9134af8373f420d88a46e3ca2d4d5d9` 已由 `K380 Bootloader` CI 验证。
MBR 与 S140 6.1.1 占用 `0x00000000..0x00026000`，Adafruit DFU/UF2 排除的应用保存数据
保留区为 `0x000EA000..0x000F4000`，Bootloader 及其配置页占用
`0x000F4000..0x00100000`。

ZMK 只能使用 `0x00026000..0x000EA000` 的 784 KiB 应用窗口。其中
`code_partition` 为 `0x00026000..0x000CA000`（656 KiB），
`storage_partition` 为 `0x000CA000..0x000EA000`（128 KiB）。
`0x000EA000..0x000F4000` 是 DFU/UF2 不写入的应用保存数据区；项目选择将其标记为只读且
留空，不分配给 ZMK Settings，且它不属于 Bootloader 自身。
```

将同节“实现影响”替换为：

```markdown
**实现影响：** K380 ZMK board 必须把 `zephyr,code-partition` 指向
`code_partition`，不得让链接产物、HEX 或应用 UF2 写入 MBR/S140、Adafruit DFU/UF2
排除的应用保存数据保留区或 Bootloader 保留区域。`storage_partition` 只供 ZMK NVS/Settings
使用，不能扩大应用窗口，也不能替代 Adafruit DFU/UF2 排除的应用保存数据保留区。
```

将同节“验证方式”替换为：

```markdown
**验证方式：** K380 CI 的 board-build job 必须从生成的 `zephyr.dts` 检查五个分区，
并检查内部 Flash HEX 记录和 UF2 block 均位于 `code_partition`。
实板阶段再验证应用 UF2 写入、`&bootloader` 进入 UF2 和应用重新启动。
```

- [x] **Step 2: 标记设计规格为已完成**

将 `docs/superpowers/specs/2026-08-20-k380-flash-partitions-design.md` 的状态改为：

```markdown
**状态：** 已完成。
```

- [x] **Step 3: 执行文档和边界静态检查**

在仓库根目录执行：

```powershell
$contract = Get-Content -Raw "docs\k380\hardware-contract.md"
$requiredContract = @(
  "0x00026000..0x000EA000",
  "0x00026000..0x000CA000",
  "0x000CA000..0x000EA000",
  "476577baf9134af8373f420d88a46e3ca2d4d5d9"
)
$missingContract = $requiredContract | Where-Object { -not $contract.Contains($_) }
if ($missingContract) {
  throw "硬件契约缺少分区证据: $($missingContract -join ', ')"
}

$dts = Get-Content -Raw "app\boards\kimwolf\k380\k380_nrf52840_zmk.dts"
$requiredDts = @(
  "reg = <0x00000000 0x00026000>;",
  "reg = <0x00026000 0x000A4000>;",
  "reg = <0x000CA000 0x00020000>;",
  "reg = <0x000EA000 0x0000A000>;",
  "reg = <0x000F4000 0x0000C000>;",
  "zephyr,code-partition = &code_partition;",
  "regulator-initial-mode = <NRF5X_REG_MODE_DCDC>;"
)
$missingDts = $requiredDts | Where-Object { -not $dts.Contains($_) }
if ($missingDts) {
  throw "K380 DTS 缺少分区或 DCDC 配置: $($missingDts -join ', ')"
}

if ($dts -match 'zmk,kscan|zmk,matrix-transform|zmk,battery|ws2812|led-strip') {
  throw "最小 board 包含超出范围的硬件节点"
}

git diff --check
if ($LASTEXITCODE -ne 0) {
  throw "补丁格式检查失败"
}
Write-Output "K380 分区契约、DTS 边界和最小范围检查通过。"
```

预期输出：

```text
K380 分区契约、DTS 边界和最小范围检查通过。
```

- [x] **Step 4: 提交并检查最终 K380 CI**

```powershell
git add `
  docs/k380/hardware-contract.md `
  docs/superpowers/specs/2026-08-20-k380-flash-partitions-design.md `
  docs/superpowers/plans/2026-08-20-k380-flash-partitions.md
git commit -m "docs(k380): 固化 ZMK Flash 分区"
git push
```

在 GitHub Actions 中确认最新 `K380 CI` 的五个 jobs 成功：

```text
module-metadata: success
ghost-filter: success
driver-build: success
board-build: success
module-isolation: success
```

该 CI 证明最小 board 的构建和 Flash 边界；不证明 SWD 首刷、USB 枚举、`&bootloader`
实板跳转、写入应用 UF2、状态灯或电源切换已经通过。

## 计划自检

- 规格覆盖：最小 K380 board、Bootloader boot-mode、REG1 DC/DC、完整分区、NVS/Settings、
  测试夹具、生成 DTS、HEX/UF2 地址验证与硬件契约记录均有对应任务。
- 分区隔离：应用代码只能占用 `0x00026000..0x000CA000`，Settings 只能占用
  `0x000CA000..0x000EA000`；DFU 数据和 Bootloader 区没有被 ZMK 分配。
- 范围控制：未实现真实矩阵、matrix transform、默认 keymap、WS2812B、电池或实板行为。
- 回归控制：既有 K380 模块构建、ghost-filter 和 module-isolation 仍由同一 workflow 执行。
