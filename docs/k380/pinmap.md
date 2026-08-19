# K380 引脚分配

**状态：** 当前硬件的唯一有效引脚分配。物理按键坐标表见
[`matrix-layout.md`](matrix-layout.md)，仍需通过实板逐键测试确认。

**用途：** 本文档是 K380 硬件 GPIO 分配的唯一来源。后续 board DTSI 只可从本文档
生成矩阵和外设 GPIO 配置；扫描器源码不得依赖具体 nRF52840 GPIO 编号。

**相关文档：**

- [`matrix-layout.md`](matrix-layout.md)：物理按键到 RC 坐标的映射。
- [`hardware-contract.md`](hardware-contract.md)：电源、USB、状态灯和 Bootloader/ZMK 配置契约。

## 硬件与矩阵约束

| 属性 | 值 |
| --- | --- |
| Board | K380 |
| SoC | nRF52840-QIAA |
| 矩阵 | 8 行 x 15 列 |
| 二极管 | 无 |
| 扫描方向 | row2col |
| 行模式 | 高电平有效、开源输出 |
| 列模式 | 高电平有效、下拉输入 |

`R0` 至 `R7` 和 `C0` 至 `C14` 是 K380 扫描器使用的逻辑矩阵坐标。它们描述
矩阵网络与 SoC GPIO 的连接，不描述 Esc、字母键或功能键等物理按键的位置。
物理按键到 `(row, column)` 的对应关系记录在
[`matrix-layout.md`](matrix-layout.md)，并用于后续的 matrix transform 和 keymap。

## 矩阵 GPIO

| 信号 | GPIO | 方向 | Devicetree flags |
| --- | --- | --- | --- |
| R0 | P1.09 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R1 | P0.26 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R2 | P0.06 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R3 | P0.08 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R4 | P0.04 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R5 | P0.12 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R6 | P0.07 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R7 | P0.15 | 输出 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| C0 | P0.05 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C1 | P0.17 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C2 | P0.20 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C3 | P0.22 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C4 | P1.02 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C5 | P1.04 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C6 | P1.06 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C7 | P0.31 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C8 | P0.29 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C9 | P0.02 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C10 | P1.13 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C11 | P0.28 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C12 | P0.03 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C13 | P1.10 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C14 | P1.11 | 输入 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |

## 配置边界

- 23 个矩阵 GPIO 在当前配置中不得分配给 LED、ADC、显示屏、编码器、QSPI 或 trace。
- `GPIO_PULL_DOWN` 表示列输入由固件请求下拉；原理图不得为这些网络增加与该行为冲突
  的外部上下拉或驱动电路。
- P0.20 不占用 SWD；J-Link/SWD 继续使用芯片的 SWDIO、SWCLK 和复位焊盘。
- P0.07、P0.12 和 P1.09 不得启用 trace 输出；P0.22 不得同时用于 QSPI。
- 矩阵无二极管。K380 专用驱动在 debounce 前执行矩形歧义过滤，但这不替代硬件
  矩阵逐键测试。

## 板级集成前的硬件确认

硬件必须提供以下资料：

1. 标明 PCB revision 的原理图或源工程。
2. PCB 网表，能够核对每条 `R*`、`C*` 网络与上述 GPIO 的连接。
3. 与 [`matrix-layout.md`](matrix-layout.md) 一致的物理按键表：键名或位置、
   开关位号、行网络、列网络。
4. SWD、USB、复位和其他外设的连接说明，用于排除 GPIO 复用冲突。

实板验证必须确认空闲列为低电平；逐行激活时，每个实体按键只上报预期的
`(row, column)`；多键按下不导致异常复位或其他外设失效。

## 后续硬件变化

当前项目只支持本文档定义的唯一硬件版本。未来若硬件改线或增加外设，应新建独立的
硬件版本文档和对应 board 配置，不能直接修改本文件后继续刷写旧硬件。
