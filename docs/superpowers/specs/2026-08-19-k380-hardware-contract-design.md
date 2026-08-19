# K380 硬件契约文档设计

**状态：** 已批准，待编写硬件契约正文。

## 目标

在 `docs/k380/hardware-contract.md` 建立 K380 的长期硬件事实与固件配置契约。
该文档面向 Bootloader、ZMK board、实板 bring-up 和后续硬件维护，避免从聊天记录或
多个无关联文档中重新推断关键配置。

## 文档边界

`hardware-contract.md` 不替代以下现有文档：

- `pinmap.md`：矩阵网络和 GPIO 电气分配。
- `matrix-layout.md`：物理按键到逻辑矩阵坐标的映射。

硬件契约只记录跨 Bootloader 和 ZMK 的板级事实、配置值、决策原因、验证方式和变更
规则，并链接到上述文档作为矩阵部分的来源。

不记录完整聊天原文、私密授权文件、密钥、密码或未证实的推测。

## 固定结构

文档按以下章节组织：

1. 文档职责与适用范围。
2. 已确认硬件总览。
3. 电源与时钟。
4. USB-C、SWD 与恢复路径。
5. WS2812B 状态灯。
6. 电池电压采样。
7. Bootloader 配置契约。
8. ZMK board 配置契约。
9. 实板验证清单。
10. 变更规则与授权记录。

每个决策项使用以下固定格式：

```text
问题：需要解决的硬件或固件边界。
已确认答案：硬件事实或已批准的配置值。
实现影响：Bootloader、ZMK 或两者必须采用的行为。
验证方式：实板、构建或文件检查的可重复验证方法。
```

该格式保留问答上下文，但最终只保留已经确认的结论。

## 已确认配置

硬件契约将写入以下已确认事实：

| 项目 | 已确认值 |
| --- | --- |
| SoC | nRF52840-QIAA |
| 矩阵 | 8 行 x 15 列、无二极管、row2col；详细分配见 `pinmap.md` |
| 物理按键矩阵 | 80 个有效按键；详细坐标见 `matrix-layout.md` |
| VDDH | USB 5 V 与单节锂电池经自动切换电路供电 |
| 电池截止 | 2.75 V |
| REG0 | 内部 LDO |
| VDD | `UICR.REGOUT0 = 2.7 V` |
| REG1 | 内部 DC/DC |
| DC/DC 开关 | `DCDC0` 关闭，`DCDC1` 启用 |
| 高频时钟 | 外接 32 MHz 晶振与匹配负载电容 |
| 低频时钟 | 无外接 32.768 kHz 晶振，使用内部 RC |
| USB | USB-C；CC1/CC2 分别经 5.1 kOhm 下拉至 GND；VBUS 有保护；D+/D- 有 ESD 保护 |
| USB 身份 | UF2+CDC：`0x303A:0x1011`；CDC-only：`0x303A:0x1012` |
| USB 授权 | 项目所有者持有对应 VID/PID 的书面授权；授权文件不提交到仓库 |
| 恢复 | RESET 测试点、SWDIO/SWCLK/RESET/GND/VTref 测试点；无用户 RESET 按键 |
| WS2812B | 4 颗串联；P0.13 经 SN74LVC1T45 驱动第 1 颗 DIN |
| WS2812B 电平转换 | VCCA 接 nRF VDD；VCCB 接 VDDH；DIR 固定接 VCCA，方向固定 A 到 B |
| 电池测量 | 内部 `VDDHDIV5`；仅 USB 未插入时解释为电池电压 |
| NFC | P0.09/P0.10 未使用 |
| 其他外设 | 当前不存在其他连接到 nRF52840 的外设或控制信号 |
| DFU 功能 | 不要求 BLE OTA、签名固件或双 bank 回滚 |

## 配置契约

Bootloader 部分必须定义：

```c
UICR_REGOUT0_VALUE = UICR_REGOUT0_VOUT_2V7
ENABLE_DCDC_0 = 0
ENABLE_DCDC_1 = 1
```

K380 Bootloader board 必须使用 nRF52840、S140 v6.1.1、USB UF2 与 CDC，默认不进入
BLE OTA，不启用固件签名或双 bank。

ZMK board 后续必须使用与 Bootloader 一致的应用分区边界、Bootloader 进入机制、电源
模式、低频 RC 时钟和 USB 身份；其中 Flash 分区起始地址和长度只能在 K380 Bootloader
生成 linker map 后填入，不能在本文档中猜测。

## 验证与变更

硬件契约的验证清单至少覆盖：

- GitHub Actions 成功构建 K380 Bootloader 和 ZMK board。
- SWD 能够首次烧入、擦除和救砖。
- USB-C 能枚举 UF2+CDC 和 CDC-only 两种描述符。
- ZMK 的 `&bootloader` 能进入 UF2；RESET 测试点双击也能进入 UF2。
- 电池模式下测量 `VDDHDIV5` 与万用表的 VDDH 一致。
- WS2812B 四灯串行数据、方向和电平转换正确。
- 低电量接近 2.75 V 时，VDD 仍符合 2.7 V 配置的实际工作要求。

任何硬件改线、供电变更、USB 身份变更、时钟变化、增加外设或修改分区均必须先更新
硬件契约，再创建新的功能分支和实施计划。GPIO 变化同时更新 `pinmap.md`；物理按键
矩阵变化同时更新 `matrix-layout.md`。
