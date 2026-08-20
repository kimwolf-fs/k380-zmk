# K380 ZMK Flash 分区与最小 Board 设计

**状态：** 已批准设计，待规格审阅。

## 目标

为 K380 创建最小 ZMK board，使 ZMK 应用只链接到经 K380 Bootloader 验证的可写 Flash
窗口，并为持久化 Settings 保留独立存储空间。

本设计依据 `k380-bootloader` 的 `k380` 分支合并提交 `476577b` 及其成功的
`K380 Bootloader` CI。Bootloader 对应用 Flash 的边界为：

```text
起始：0x00026000
结束：0x000EA000
总长：0x000C4000
```

## 范围

本阶段创建 `k380/nrf52840/zmk` 的最小构建目标，并包含：

- nRF52840-QIAA SoC 定义。
- Adafruit UF2 Bootloader 的 boot-mode retention 映射。
- `&reg1` 的 DC/DC 模式。
- Flash `fixed-partitions`、`zephyr,code-partition` 和 Settings 存储分区。
- 仅用于 CI 的最小构建夹具。

本阶段不实现矩阵 GPIO、kscan 实例、matrix transform、默认 keymap、WS2812B、LED 状态机、
电池采样、USB 充电状态或实板烧写流程。

## Board 结构

新增目录为：

```text
app/boards/kimwolf/k380/
  board.yml
  Kconfig.k380
  k380_nrf52840_zmk.dts
  k380_nrf52840_zmk_defconfig
```

`board.yml` 注册 `k380` board 和 `nrf52840/zmk` variant。`Kconfig.k380` 选择
`SOC_NRF52840_QIAA`，并在 ZMK variant 启用时选择 ZMK board 兼容配置和 boot-mode retention
依赖。

`k380_nrf52840_zmk.dts` 包含：

```dts
#include <nordic/nrf52840_qiaa.dtsi>
#include <common/nordic/nrf52840_uf2_boot_mode.dtsi>
#include <zephyr/dt-bindings/regulator/nrf5x.h>
```

其中 `nrf52840_uf2_boot_mode.dtsi` 将 ZMK 的 `&bootloader` 行为映射到 Adafruit nRF52
Bootloader 所需的 retention magic；本阶段不定义调用该行为的 keymap。

`&reg1` 必须设置为：

```dts
&reg1 {
    regulator-initial-mode = <NRF5X_REG_MODE_DCDC>;
};
```

不为 REG0/DCDC0 添加 DC/DC 配置。`UICR.REGOUT0 = 2.7 V` 继续只由 Bootloader 或 SWD
首次刷写负责。

## Flash 分区

`k380_nrf52840_zmk.dts` 在 `&flash0` 定义连续且完整的 1 MiB `fixed-partitions`。保留分区
标记为只读，只有代码和 Settings 存储分区可由应用使用。

| 分区 | 起始 | 长度 | 结束 | 用途 |
| --- | --- | --- | --- | --- |
| `mbr_softdevice_partition` | `0x00000000` | `0x00026000` | `0x00026000` | MBR 与 S140 6.1.1，保留 |
| `code_partition` | `0x00026000` | `0x000A4000` | `0x000CA000` | ZMK 可执行代码 |
| `storage_partition` | `0x000CA000` | `0x00020000` | `0x000EA000` | ZMK NVS/Settings |
| `dfu_app_data_partition` | `0x000EA000` | `0x0000A000` | `0x000F4000` | Adafruit DFU 应用数据，保留 |
| `boot_partition` | `0x000F4000` | `0x0000C000` | `0x00100000` | Bootloader、配置、MBR 参数与 settings 页，保留 |

`zephyr,code-partition` 只能指向 `code_partition`。`storage_partition` 是同一 784 KiB 应用
窗口的一部分，不属于 Bootloader，也不得越过 `0x000EA000`。因此代码可用空间为 656 KiB，
持久化存储为 128 KiB。

## 默认配置

`k380_nrf52840_zmk_defconfig` 启用：

- `CONFIG_ARM_MPU`、`CONFIG_GPIO` 和 nRF52 所需的基础运行配置。
- `CONFIG_USE_DT_CODE_PARTITION` 与 `CONFIG_BUILD_OUTPUT_UF2`。
- `CONFIG_FLASH`、`CONFIG_FLASH_PAGE_LAYOUT`、`CONFIG_FLASH_MAP`、`CONFIG_NVS`、
  `CONFIG_SETTINGS_NVS` 与 `CONFIG_MPU_ALLOW_FLASH_WRITE`。
- `CONFIG_ZMK_USB` 与 `CONFIG_ZMK_BLE`。

该配置不启用任何 K380 专有输入、灯效或电池外设。

## CI 与验证

K380 CI 增加 board 构建夹具。夹具可提供只用于编译的最小 overlay/keymap，但不得表示真实
K380 矩阵、GPIO 或默认键位。

验证必须覆盖：

1. `west build` 能为 `k380/nrf52840/zmk` 完成最小 ZMK 构建并生成 UF2。
2. 生成的 `zephyr.dts` 包含表中五个分区，起始、长度、结束连续且总计 1 MiB。
3. `zephyr,code-partition` 指向 `code_partition`，其范围严格为
   `0x00026000..0x000CA000`。
4. `storage_partition` 严格为 `0x000CA000..0x000EA000`。
5. `reg1` 为 DC/DC，且 DTS 中没有 REG0/DCDC0 的 DC/DC 启用配置。
6. 应用 ELF、HEX 或 UF2 的 Flash 段不落入 MBR/S140、DFU 应用数据或 Bootloader 保留区域。

## 后续阶段

分区与最小 board 合并后，后续独立设计和实施：

1. K380 矩阵实例、matrix transform 和默认 keymap。
2. WS2812B 蓝牙与系统状态灯。
3. 电池采样、低电量策略和 USB 供电提示。
4. SWD、UF2、`&bootloader`、分区边界和电源切换的实板 bring-up。
