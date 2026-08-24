# K380 硬件契约

**状态：** 已确认硬件事实；K380 Bootloader 已验证应用 Flash 区间为
`0x00026000..0x000EA000`，ZMK code 与 Settings 分区必须保持本文档所列边界。

**用途：** 本文档是 K380 Bootloader、ZMK board 和实板 bring-up 共用的板级硬件契约。矩阵 GPIO 分配见 [`pinmap.md`](pinmap.md)，物理按键 RC 映射见 [`matrix-layout.md`](matrix-layout.md)。这两份矩阵文档是已记录、但仍等待实板确认的事实来源；其逐键、多键和中断行为确认仍是必做的 bring-up 任务。

## 记录规则

每个主题包含：

- **问题：** 要解决的硬件或固件边界。
- **已确认答案：** 已由项目所有者确认的硬件事实或配置。
- **实现影响：** Bootloader、ZMK 或两者必须遵守的行为。
- **验证方式：** 构建、实板或文件检查的可重复验证方法。

## 已确认硬件总览

| 项目 | 已确认事实 |
| --- | --- |
| SoC | nRF52840-QIAA |
| 矩阵 | 8 行 x 15 列、无二极管、row2col |
| USB-C | CC1/CC2 分别经 5.1 kOhm 下拉至 GND、VBUS 有保护、D+/D- 有 ESD 保护 |
| USB 身份：UF2+CDC | `0x303A:0x1011` |
| USB 身份：CDC-only | `0x303A:0x1012` |
| USB 授权 | 项目所有者保管对应 VID/PID 的书面授权；授权文件不提交到仓库 |
| 高速时钟 | 外接 32 MHz 晶振连接 XC1/XC2，且有匹配负载电容 |
| 低速时钟 | 无 32.768 kHz 晶振，使用内部 RC |
| NFC | P0.09/P0.10 未使用 |
| 其他外设 | 除了本文档与矩阵文档项目外，无其他 nRF52840 外设或控制信号 |

## 电源与时钟

**问题：** VDDH、VDD 和片内稳压器必须如何配置，才能同时满足 USB 与单节锂电池供电条件？

**已确认答案：**

- USB 5 V 与单节锂电池经自动切换电路接入 VDDH。
- 电池模式下 VDDH 低于 2.75 V 时断开。
- REG0 使用内部 LDO，`UICR.REGOUT0` 固定为 2.7 V。
- REG1 使用内部 DC/DC，DCC-DEC4 的 LC 已确认符合参考设计。
- DCCH 无电感，REG0 DCDC 不可启用。
- 32 MHz 外部晶振接 XC1/XC2 并配置匹配负载电容；无 32.768 kHz 晶振，低速时钟使用内部 RC。

**实现影响：** 以下宏是 Adafruit nRF52 Bootloader 的配置，不是 ZMK 的共享配置：

```c
#define UICR_REGOUT0_VALUE UICR_REGOUT0_VOUT_2V7
#define ENABLE_DCDC_0 0
#define ENABLE_DCDC_1 1
```

`UICR.REGOUT0 = 2.7 V` 也只能由 Bootloader 或 SWD 首次刷写配置。后续 ZMK board 基于本仓库
Zephyr v4.1.0 的 nRF52840 SoC DTS，其中 `&reg1` 默认使用 LDO；board overlay 必须将其设为
DC/DC：

```dts
#include <zephyr/dt-bindings/regulator/nrf5x.h>

&reg1 {
    regulator-initial-mode = <NRF5X_REG_MODE_DCDC>;
};
```

不得使用已废弃的 `CONFIG_SOC_DCDC_NRF52X`，也不得为 REG0/DCDC0 增加任何 DC/DC 配置；
REG0 保持 LDO。

**验证方式：** 分别在 USB 供电和接近 2.75 V 的电池模式下测量 VDDH/VDD，确认 VDD 为 2.7 V、
DCDC0 未启用且 DCDC1 已启用；确认电池模式的 VDDH 低于 2.75 V 时断开；并实测 USB
插入/拔出时的自动切换，确认 USB 存在时由 USB 供电，拔出 USB 后无复位或异常并自动切换至
电池供电。未来 ZMK board 首次构建后，检查构建产物 `zephyr.dts`，确认 `reg1` 为 DC/DC，
且不存在 REG0/DCDC0 的 DC/DC 启用配置。

## USB、SWD 与恢复路径

**问题：** 日常刷写、应用失效恢复和不可恢复故障分别使用什么路径？

**已确认答案：**

- RESET 仅有测试点，无用户按键。
- SWDIO、SWCLK、RESET、GND、VTref 测试点齐全；VTref 接 nRF VDD。
- 日常通过 ZMK `Fn+Del`（`&bootloader` 绑定）进入 UF2；常规恢复入口由上电前按住 `Del` 触发。
- 应用无法启动时，通过 SWD 救砖；RESET 测试点仅保留给调试和救砖。
- 无法通过 UF2 恢复时使用 SWD。
- Bootloader 为 USB UF2+CDC；不支持 BLE OTA、签名固件或双 bank 回滚。

**实现影响：** ZMK 必须保留 `Fn+Del`（`&bootloader` 绑定）可进入 UF2 的路径，并提供上电前按住 `Del` 的常规恢复入口；Bootloader 不得声明或依赖 BLE OTA、签名校验或双 bank 回滚能力。恢复文档与实板操作必须以 `Del` 和 SWD 测试点为准。

**验证方式：** 在实板上验证 SWD 可首刷、擦除和救砖；验证 ZMK `Fn+Del` 可进入 UF2，验证上电前按住 `Del` 可进入常规恢复入口，并验证 USB 的 UF2+CDC 与 CDC-only 枚举。

## WS2812B 状态灯

**问题：** 四颗串联 WS2812B 的硬件连接、蓝牙槽位显示和系统状态显示如何划分？

**已确认答案：**

- P0.13 接 SN74LVC1T45 A 端；VCCA 为 nRF VDD，VCCB 为 VDDH，DIR 固定为 VCCA。
- SN74LVC1T45 B 端接第 1 颗 WS2812B 的 DIN。
- 共 4 颗 WS2812B 串联，数据方向为 LED1 -> LED2 -> LED3 -> LED4。

| 索引 | LED | 基本功能 |
| --- | --- | --- |
| 0 | LED1 | 蓝牙配置槽 3 |
| 1 | LED2 | 蓝牙配置槽 2 |
| 2 | LED3 | 蓝牙配置槽 1 |
| 3 | LED4 | 系统状态 |

- LED1、LED2、LED3 只表达蓝牙状态，且三者互斥。
- LED4 绝不表达蓝牙状态。
- 对当前蓝牙槽位：连接中为绿色慢速呼吸，成功、超时或切换结束时停止；配对中为绿色快速呼吸，成功、超时或切换结束时停止；已连接为绿色常亮 5 秒后熄灭；未选择、连接失败、配对失败和断开均熄灭。
- 状态灯实现规格必须先确认慢速/快速呼吸周期、快速闪烁/双闪/单闪/三连闪/短闪的时序、蓝牙连接/配对超时、RGB 颜色值和全局亮度上限；这些具体数值当前未确认。

| LED4 事件 | 显示 |
| --- | --- |
| Bootloader/UF2 就绪 | 蓝色慢速呼吸 10 秒后熄灭 |
| UF2 写入 | 黄色快速呼吸至写完 |
| UF2 写入成功 | 绿色常亮 5 秒 |
| UF2 写入失败 | 红色快速闪烁 5 秒 |
| USB 外部供电/充电路径可用 | 黄色双闪，且每次 USB 接入只提示一次 |
| 低电量 | 红色单闪，每 30 秒一次；恢复电压或插入 USB 后停止 |
| 启动完成 | 白色短闪一次 |
| 致命错误 | 红色三连闪，持续 5 秒 |

**实现影响：** Bootloader 与 ZMK 必须按 LED1 至 LED4 的链路顺序发送数据。Bootloader 运行时应用不运行，Bootloader 独占 LED4；其状态机包含 Bootloader/UF2 就绪、UF2 写入、UF2 写入成功和 UF2 写入失败，并按事件表处理，无需与应用事件比较优先级。进入 Bootloader 时，LED1、LED2、LED3 必须熄灭且不表达蓝牙。应用运行时的 LED4 事件优先级从高到低为：致命错误 > 低电量 > USB 接入提示 > 启动完成。每次系统提示只能使用 LED4，禁止无限期常亮或呼吸，并必须设置全局亮度上限。慢速/快速呼吸周期、快速闪烁/双闪/单闪/三连闪/短闪的时序、蓝牙连接/配对超时、RGB 颜色值和全局亮度上限具体值必须由状态灯实现规格确认；在确认前不得作为完成验收或烧写实现的依据。USB 存在只能表示外部 USB 供电或充电路径可用，不能称为电池正在充电。

**验证方式：** 在实板上确认 P0.13 经电平转换器驱动四颗 LED，且数据顺序为 LED1、LED2、LED3、LED4；进入 Bootloader 后确认 LED1、LED2、LED3 熄灭，并分别触发 Bootloader/UF2 就绪、UF2 写入、UF2 写入成功和 UF2 写入失败，确认均按事件表处理；运行应用后分别触发所有蓝牙状态和应用 LED4 事件，确认槽位灯互斥、LED4 不显示蓝牙、显示时长、应用运行时优先级与 USB 接入一次性提示均符合本节。完成验收或烧写实现前，必须由状态灯实现规格确认慢速/快速呼吸周期、快速闪烁/双闪/单闪/三连闪/短闪时序、蓝牙连接/配对超时、RGB 颜色值和全局亮度上限具体值。

## 电池电压采样

**问题：** 如何采样和解释单节锂电池电压，避免将 USB 供电状态误判为充电状态？

**已确认答案：**

- 使用内部 `VDDHDIV5` 采样，无外部分压。
- USB 未插入时，采样值解释为电池电压。
- USB 插入时，只代表外部 USB 供电。
- 电池电压低于 3.20 V 时提示；高于 3.30 V 时恢复。
- 高于电池的 VDDH 不能解释为电池正在充电或已经充满。

**实现影响：** ZMK 的电池状态和低电量 LED4 逻辑必须使用 `VDDHDIV5` 与 3.20 V/3.30 V 回差。USB 存在时不得根据 VDDH 推断电池电压、充电中或已充满。

**验证方式：** 在 USB 未插入时，将 `VDDHDIV5` 读数与万用表测得的电池电压比对；跨越 3.20 V 和 3.30 V 阈值验证低电量提示与恢复回差；插入 USB 后确认状态只报告外部 USB 供电，不报告电池正在充电或已充满。

## Bootloader 与 ZMK 配置门禁

**问题：** Bootloader 与 ZMK 如何共享 Flash 应用分区边界，且不在事实未确认前写入错误数值？

**已确认答案：** K380 Bootloader `k380` 分支的合并提交
`476577baf9134af8373f420d88a46e3ca2d4d5d9` 已由 `K380 Bootloader` CI 验证。
MBR 与 S140 6.1.1 占用 `0x00000000..0x00026000`，Adafruit DFU/UF2 排除的应用保存数据
保留区为 `0x000EA000..0x000F4000`，Bootloader 及其配置页占用
`0x000F4000..0x00100000`。

ZMK 只能使用 `0x00026000..0x000EA000` 的 784 KiB 应用窗口。其中
`code_partition` 为 `0x00026000..0x000CA000`（656 KiB），
`storage_partition` 为 `0x000CA000..0x000EA000`（128 KiB）。
`0x000EA000..0x000F4000` 是 DFU/UF2 不写入的应用保存数据区；项目将其标记为只读且
留空，不分配给 ZMK Settings，且它不属于 Bootloader 自身。

**实现影响：** K380 ZMK board 必须把 `zephyr,code-partition` 指向
`code_partition`，不得让链接产物、HEX 或应用 UF2 写入 MBR/S140、Adafruit DFU/UF2
排除的应用保存数据保留区或 Bootloader 保留区域。`storage_partition` 只供 ZMK NVS/Settings
使用，不能扩大应用窗口，也不能替代 Adafruit DFU/UF2 排除的应用保存数据保留区。

**验证方式：** K380 CI 的 board-build job 必须从生成的 `zephyr.dts` 检查五个分区，
并检查内部 Flash HEX 记录和 UF2 block 均位于 `code_partition`。
实板阶段再验证应用 UF2 写入、`Fn+Del` 进入 UF2 和应用重新启动。

## 实板验证清单

**问题：** 哪些最小实板检查必须完成，才能证明本契约已落实？

**已确认答案：** 以下检查是 K380 bring-up 的必做项：矩阵逐键坐标、多键与保持/释放行为、未使用坐标不产生事件、真实 P0/P1 GPIO 中断唤醒/扫描停止/异常恢复；SWD 首刷、擦除、救砖；USB-C 的 UF2+CDC 和 CDC-only 枚举；ZMK `Fn+Del`；上电前按住 `Del` 的常规恢复入口；`VDDHDIV5` 与万用表比对；低电量回差；四灯数据顺序；所有 LED 状态和 USB 提示。矩阵 GPIO 与 RC 映射已记录在 [`pinmap.md`](pinmap.md) 和 [`matrix-layout.md`](matrix-layout.md)，但上述矩阵行为尚未完成实板确认。

**实现影响：** Bootloader、ZMK board 与 bring-up 记录必须覆盖每一项；任一项失败时，不得将对应路径标记为已验证。

**验证方式：** 对每项执行可重复的实板操作并记录 PCB revision、测试日期、固件版本、测量值、枚举结果和异常项。矩阵验证必须使用可观察 `(row, column)` 的诊断固件，覆盖 80 个有效按键、40 个未使用坐标、非歧义多键、矩形歧义、保持/释放以及真实 P0/P1 GPIO 中断唤醒、扫描停止和异常恢复；如发现 GPIO 或 RC 错误，先更新对应唯一来源文档。LED 检查必须覆盖所有蓝牙状态、所有 LED4 事件、优先级和 USB 接入一次性提示。

## 变更规则

**问题：** 如何区分当前唯一硬件版本中已有事实的勘误或配置修正，与必须创建新硬件版本的物理变更？

**已确认答案：**

- 当前唯一硬件版本中已有事实的勘误或配置修正，依文档职责更新对应文件，并在修改前创建新的 `feat/k380-*` 功能分支。
- 对当前唯一硬件版本，矩阵 GPIO 更新 [`pinmap.md`](pinmap.md)，物理按键 RC 更新 [`matrix-layout.md`](matrix-layout.md)，其他板级硬件契约项更新本文档。
- 物理改线、新增外设或新 PCB revision 不得覆盖当前硬件事实；必须创建独立的版本化硬件契约、pinmap、matrix-layout 和对应 board 配置，旧文档保持不变。

**实现影响：** 对当前唯一硬件版本的勘误或配置修正，Bootloader、ZMK board 和 bring-up 配置只能依据对应的唯一事实来源更新，且必须先创建新的 `feat/k380-*` 功能分支；除矩阵 GPIO 与物理按键 RC 外，不得将其他当前板级硬件契约变更记录在本文档以外的来源。物理改线、新增外设或新 PCB revision 必须使用独立的版本化文档与对应 board 配置，不得修改旧文档以兼容新硬件。不得在本文档复制完整矩阵 GPIO 表或完整物理按键 RC 表。

**验证方式：** 变更评审时先判定变更类型。当前唯一硬件版本的勘误或配置修正，检查矩阵 GPIO 仅在 `pinmap.md`、物理按键 RC 仅在 `matrix-layout.md`、其他当前板级硬件契约仅在本文档，并确认修改前已创建名称匹配 `feat/k380-*` 的功能分支。物理改线、新增外设或新 PCB revision，检查已创建独立的版本化硬件契约、pinmap、matrix-layout 和对应 board 配置，且旧文档未改动；同时确认本文档未包含完整的 23 个 GPIO 表或 80 个 RC 表。
