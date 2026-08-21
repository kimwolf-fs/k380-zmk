# K380 真实矩阵集成设计

**状态：** 待审阅。

## 目标

将已验证的 K380 专用无二极管矩阵驱动实例化到
`k380/nrf52840/zmk` board，使应用使用
[`docs/k380/pinmap.md`](../../k380/pinmap.md) 中唯一认可的 8 行 x 15 列 GPIO
定义进行扫描。

本阶段完成构建期集成和静态 CI 验证。实板逐键验证因当前没有可测试硬件而延期，但必须作为
未完成验收项保留，不能由构建或 CI 成功代替。

## 范围

本阶段包括：

- 在 K380 board DTS 中创建一个 `k380,kscan-no-diode-matrix` 实例。
- 将 `zmk,kscan` 指向该真实矩阵实例。
- 按 `pinmap.md` 固定 8 个行 GPIO、15 个列 GPIO、row2col 扫描方向和 GPIO flags。
- 将现有 board-build 夹具改为构建真实 board 矩阵定义，而非创建虚拟矩阵。
- 在 K380 CI 中验证源 DTS 与生成 DTS 的矩阵 compatible、chosen 节点、全部 23 个 GPIO、
  行列数量和既有 Flash/DC-DC 契约。
- 保留实板逐键、无二极管歧义过滤、唤醒和多键行为验证任务，状态为延期未完成。

本阶段不包括：

- `zmk,matrix-transform`。
- 默认 keymap、用户 keymap 或 K380 物理布局映射。
- WS2812B、LED 状态机、电池采样、低电量策略或 USB 提示。
- 向 Bootloader 仓库添加或修改代码。
- SWD、UF2、USB 或任何实板刷写操作。

## Board 配置

`app/boards/kimwolf/k380/k380_nrf52840_zmk.dts` 保留已合并的 Flash 分区、UF2
boot-mode retention 和 `&reg1` DC/DC 配置，并新增以下逻辑：

```dts
/ {
    chosen {
        zmk,kscan = &k380_kscan;
    };

    k380_kscan: k380_kscan {
        compatible = "k380,kscan-no-diode-matrix";
        /* row-gpios and col-gpios are listed below. */
    };
};
```

行与列严格采用以下分配，不得从 CI 夹具、驱动源码或物理按键布局推断或替换：

| 信号 | GPIO | Devicetree flags |
| --- | --- | --- |
| R0 | P1.09 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R1 | P0.26 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R2 | P0.06 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R3 | P0.08 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R4 | P0.04 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R5 | P0.12 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R6 | P0.07 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| R7 | P0.15 | `GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE` |
| C0 | P0.05 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C1 | P0.17 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C2 | P0.20 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C3 | P0.22 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C4 | P1.02 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C5 | P1.04 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C6 | P1.06 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C7 | P0.31 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C8 | P0.29 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C9 | P0.02 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C10 | P1.13 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C11 | P0.28 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C12 | P0.03 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C13 | P1.10 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |
| C14 | P1.11 | `GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN` |

实例设置 `debounce-press-ms = <5>`、`debounce-release-ms = <5>`、
`debounce-scan-period-ms = <1>` 和 `poll-period-ms = <10>`，与 K380 binding 的既有
默认值一致。正常 board 默认使用驱动的中断唤醒路径；CI 夹具显式设置
`CONFIG_ZMK_KSCAN_MATRIX_POLLING=y`，只为了避免对不存在的 CI GPIO 中断模型作出假设。

`pinmap.md` 仍是 GPIO 的唯一来源，`matrix-layout.md` 仍是物理按键到逻辑 `(row, column)`
的唯一来源。两者都不在本阶段复制到 keymap 或 matrix transform。

## 构建夹具与 CI

`zmk-keyboard-k380/tests/board-build/k380-board.overlay` 不再定义虚拟 `kscan` 节点或伪造
GPIO。它只提供 ZMK 构建所需的测试 keymap，且该 keymap 不属于 K380 board 的默认配置。

`k380-board.conf` 保持 `CONFIG_ZMK_KSCAN_MATRIX_POLLING=y` 和断言配置。该选项是 CI
适配，不改变硬件契约对正常固件唤醒能力的要求。

K380 CI 的 `board-build` 验证扩展为：

1. 源 DTS 和生成的 `zephyr.dts` 都有 `k380,kscan-no-diode-matrix` 实例。
2. 生成 DTS 的 `zmk,kscan` 指向该实例。
3. `row-gpios` 恰有 8 项、`col-gpios` 恰有 15 项，顺序、控制器、引脚和 flags 均等于本设计表。
4. board DTS 没有第二个 K380 矩阵实例，也没有测试夹具虚拟 GPIO。
5. 既有五个 Flash 分区、`zephyr,code-partition`、UF2/HEX 地址范围和 `&reg1` DC/DC 检查
   继续通过。
6. board DTS 仍不含 matrix transform、默认 keymap、LED/WS2812、电池节点或 REG0 DC/DC
   配置。

远程 `K380 CI / board-build` 是本阶段唯一的完整构建证据。当前本地环境缺少 `west` 和
Docker，因此不能将本地静态检查描述为完整的 Zephyr 构建验证。

## 延期实板验证

以下任务明确延期，不删除、不勾选完成，也不以 CI 绿色替代：

1. 使用矩阵诊断固件逐键验证 `matrix-layout.md` 所列 80 个有效按键，每个按键只上报其指定
   `(row, column)`。
2. 验证剩余 40 个未使用坐标不会上报按键。
3. 验证同时按下非歧义组合不会丢键；矩形歧义组合符合 K380 ghost filter 的暂缓和已接受按键
   保持规则。
4. 验证按下能唤醒扫描、释放后扫描停止，并确认中断模式在真实 P0/P1 GPIO 上稳定工作。
5. 记录 PCB revision、测试日期、测试固件提交、烧写方法、逐键结果和异常项。

开始这部分工作前必须具备 K380 实板、可用的 SWD 或已验证的 UF2 进入路径，以及能观察或记录
矩阵坐标的诊断固件。若发现 GPIO 或物理坐标错误，先更新唯一来源文档，再创建独立的修正设计；
不得通过调整默认 keymap 掩盖硬件错误。

## 成功标准

- `k380/nrf52840/zmk` 使用真实 K380 GPIO 实例化专用扫描驱动，并在远程 CI 中通过构建。
- CI 能阻止行列数量、顺序、GPIO 控制器、引脚、flags、chosen 目标或 Flash/DC-DC 契约回归。
- 生产 board 不包含虚拟矩阵、matrix transform、默认 keymap、LED 或电池功能。
- 实板矩阵验证在文档和实施计划中明确保留为延期未完成验收项。
