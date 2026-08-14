# K380 无二极管矩阵设计

**状态：** 已批准

**目标：** 为使用 nRF52840-QIAA 的 K380 键盘新增一个 ZMK board module，并实现仅抑制电气状态不确定的新按键的无二极管矩阵扫描器。

## 范围

实现放在独立的外部 module `zmk-keyboard-k380` 中。
不得修改 ZMK 树内的 `zmk,kscan-gpio-matrix` binding、其 Kconfig，或
`app/module/drivers/kscan/kscan_gpio_matrix.c`。

该 module 同时包含 K380 board 定义和仅供 K380 使用的扫描器。
扫描器仅由唯一的 devicetree compatible 选择：

```text
k380,kscan-no-diode-matrix
```

现有的 ZMK board 或 shield 都不会引用此 compatible。

## 硬件基线

- SoC：nRF52840-QIAA
- 矩阵：8 行 x 15 列
- 矩阵拓扑：无二极管
- 扫描方向：row2col
- 行电气模式：高电平有效、开源输出
- 列电气模式：高电平有效、下拉输入
- 日常固件升级：Adafruit nRF52 Bootloader 和 UF2
- 首次烧录、调试与救砖：J-Link/SWD

信号分配的唯一来源见
`docs/superpowers/specs/2026-08-14-k380-pinmap.md`。

## 架构

```text
GPIO 扫描帧
    -> 原始 8x15 矩阵
    -> 矩形歧义检测
    -> 过滤后的逻辑矩阵
    -> 现有 debounce 规则
    -> ZMK 键位事件
    -> keymap 和 HID 输出
```

扫描器保留 ZMK 标准矩阵驱动的 GPIO 扫描、唤醒、中断、电源管理和
debounce 行为。唯一的功能变化发生在收集完一帧矩阵数据之后、交给
debounce 之前。

## 歧义处理策略

每完成一帧扫描，为每一行生成一个 15-bit 的 active 位图。对任意两个
行位图计算交集。

若交集至少包含两个 active 列，这两行和这些公共列的交点即构成不确定的
矩形区域。

输出规则如下：

1. 原始状态为 inactive 的交点正常释放。
2. 已经完成 debounce 的按下交点，只要原始状态仍为 active，即使进入
   歧义区域也保持按下。
3. 新出现且位于歧义区域的 active 交点不进入 debounce，也不产生按下事件。
4. 新出现且不在歧义区域的 active 交点正常进入 debounce。
5. 被暂缓的真实按键在矩形歧义消失后，若仍保持 active，则重新具备进入
   debounce 的资格。

该策略避免鬼键角点产生 HID 按下事件，同时不对普通四键和更多按键组合施加
全局数量上限。

软件无法区分同一 2x2 矩形中的“三个真实按键加一个鬼键”和“四个真实按键”。
因此完整物理矩形的产品行为必须明确：后出现且处于歧义区域的按键会被延后，
直至矩形不再歧义。

## Module 目录结构

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

`ghost_filter.c` 是纯粹的逐帧过滤逻辑。它接收原始矩阵状态和此前已接受的
状态，输出过滤后的矩阵和歧义掩码。它不读取 GPIO、不调度工作项、不发送 ZMK
事件，也不访问 devicetree。

`kscan_k380_no_diode_matrix.c` 负责 GPIO 扫描和 debounce 集成。它是 ZMK
矩阵扫描器的受控派生版本。文件头必须记录派生时对应的 ZMK 上游 commit。

## Board 集成

board DTS 必须：

- 包含 nRF52840-QIAA SoC 描述。
- 包含与 Bootloader 兼容的 UF2 boot mode 和分区配置。
- 仅包含一个 K380 pinmap revision 文件。
- 实例化 `k380,kscan-no-diode-matrix`。
- 使用选定 pinmap 中的 8 行和 15 列 GPIO。
- 定义仅包含 K380 真实物理按键的 matrix transform。
- 通过 ZMK chosen node 选择 K380 kscan 和 physical layout。

matrix transform 和默认 keymap 依赖一张完整的“物理 K380 按键到行列坐标”
对照表。未连接的电气交点不应出现在 transform 中。

## Pinmap 版本管理

所有 board 级 GPIO 引用必须来自带 revision 的 pinmap DTSI 文件。pinmap DTSI
拥有 K380 kscan node 及其 `row-gpios` 和 `col-gpios` 属性。board DTS 包含选定
pinmap 后，从 chosen node 引用带 label 的 kscan node。驱动只从 devicetree 接收
GPIO 数组，不得硬编码 nRF52840 引脚号。

board revision 变更遵循以下流程：

1. 从上一 revision 创建 `k380-pins-rev-b.dtsi`。
2. 仅在新 revision 文件中调整信号分配和可选外设节点。
3. 通过 board DTS 或 revision 专用 board variant 选择新 revision。
4. 保持旧 revision 文件可构建，以支持既有硬件。
5. 更新 pinmap 文档中的 revision 对比和验证结果。

该规则支持矩阵改线以及后续新增 LED、电池、显示屏、编码器和电源控制，而无需
修改鬼键过滤逻辑。

## 测试计划

### 纯过滤逻辑测试

module 测试套件必须覆盖：

- 单键、同一行多键和同一列多键的扫描帧。
- 不构成矩形歧义的四键及以上组合。
- 三个真实矩形角点在电气上表现为四个 active 角点的情况。
- 歧义存在时释放一个已确认按键。
- 歧义解除时，一个此前被暂缓的真实键仍保持按下。
- 多个相互独立的歧义矩形。
- 完整物理矩形，并记录预期的延后按键行为。

### 构建隔离测试

- 构建 K380 board，并确认 K380 扫描器配置已启用。
- 在注册 K380 module 的情况下构建一个现有 ZMK nRF52840 board，并确认
  K380 扫描器配置未启用。
- 确认现有 board 的构建产物中不存在 K380 驱动对象文件。
- 确认 ZMK 树内工作区没有功能性源码修改。

### 硬件验收测试

- 使用 J-Link/SWD 进行首次烧录。
- 写入无效应用镜像后使用 J-Link/SWD 救砖。
- 使用 Adafruit nRF52 Bootloader 进行正常 UF2 升级。
- 完整矩阵逐键测试。
- 非矩形四键和修饰键组合。
- 已知矩形歧义序列，确认没有鬼键 HID 事件。
- USB 和 BLE 输入。
- 空闲、唤醒和低功耗电流测量。

## 实现前置输入

仅在收到以下两项信息后开始实现：

1. 每个 K380 物理按键对应行列坐标的完整矩阵表。
2. 目标 board 使用的确切 Adafruit nRF52 Bootloader 构建版本和应用分区起始地址。
