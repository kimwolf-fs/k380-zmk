# K380 专用无二极管矩阵驱动设计

**状态：** 已批准

**目标：** 在独立的 `zmk-keyboard-k380` module 中注册并实现 K380 专用
`kscan` 驱动。驱动将无二极管矩阵歧义过滤器接入完整 GPIO 扫描帧和现有
debounce 流程之间，且不修改 ZMK 的共享矩阵驱动。

## 范围

本阶段只实现 module 注册、Kconfig、DTS binding 和 K380 专用驱动。不创建：

- K380 board DTS 或 board 定义。
- matrix transform 或默认 keymap。
- UF2、Bootloader 或闪存分区配置。
- 物理按键到矩阵坐标的映射。

这些内容属于后续 board 集成阶段，且必须先获得完整按键行列对照表与确切的
Adafruit nRF52 Bootloader 应用分区起始地址。

## 架构边界

驱动文件受控派生自 ZMK 上游的
`app/module/drivers/kscan/kscan_gpio_matrix.c`。派生时在文件头记录上游基线
commit 和源文件哈希，以便日后对比上游修复。

不修改以下共享文件或目录：

```text
app/module/drivers/kscan/
app/module/dts/bindings/kscan/
app/boards/
```

module 新增以下独立文件：

```text
zmk-keyboard-k380/
  zephyr/module.yml
  Kconfig
  CMakeLists.txt
  dts/bindings/kscan/
    k380,kscan-no-diode-matrix.yaml
  drivers/kscan/
    CMakeLists.txt
    Kconfig
    kscan_k380_no_diode_matrix.c
```

新的 devicetree compatible 固定为：

```text
k380,kscan-no-diode-matrix
```

只有后续的 K380 board DTS 显式实例化这个 compatible 时，K380 驱动才会参与
构建和运行；已有 ZMK board 不会受到影响。

## 扫描与过滤流程

驱动的 GPIO 扫描方向为 `row2col`，面向固定的 8 行 x 15 列矩阵：

1. 依次激活每一行并读取全部列输入，收集完整的原始 8 x 15 矩阵帧。
2. 调用 `k380_ghost_filter_apply()`，输入原始帧和当前已接受的按键状态。
3. 过滤器返回仅包含允许进入 debounce 的稳定候选状态；歧义掩码仅在驱动内部
   使用。
4. 将过滤后的帧接入派生驱动现有的 debounce、扫描调度和唤醒流程。
5. debounce 确认后的状态通过标准 `kscan_callback_handler_t` 上报给 ZMK。

驱动不会向 ZMK 上层暴露歧义掩码、被暂缓按键或额外调试事件，也不会扩展
共享 `kscan` API。上层只看到标准按键按下与释放事件。

## 配置约束和故障处理

- 驱动仅接受 8 个 row GPIO 和 15 个 col GPIO；数量不匹配应在构建期报错。
- GPIO 获取、配置、读取、写入失败时，沿用上游矩阵驱动的返回值、日志和扫描
  终止策略。
- 过滤器是无动态内存、无 GPIO 访问、无异步调度、无事件发送的纯逐帧函数。
- 列位图中超出第 15 列的位不参与过滤或上报。

## 验证策略

保留并持续执行现有 `native_sim` 纯过滤器测试。第二阶段还必须增加：

- K380 驱动的构建验证，确认独立 module 能被 Zephyr 发现和编译。
- 过滤接入行为测试，至少验证单键、非歧义多键、矩形歧义按键暂缓、已接受按键
  保持和释放、列边界处理。
- 隔离性验证：在注册 K380 module 的条件下构建一个现有 ZMK board，确认其
  未启用 K380 驱动，也未引入 K380 驱动对象文件。
- 源码差异验证：共享 `app/module/drivers/kscan/`、共享 binding 和
  `app/boards/` 不产生功能性修改。

## 成功标准

- K380 专用驱动可由独立 compatible 注册并构建。
- 每个完整扫描帧在进入 debounce 前都经过 `k380_ghost_filter_apply()`。
- 上层继续只依赖标准 `kscan` 回调接口。
- 现有 ZMK 驱动、binding 和 board 保持未修改。
- 纯过滤器测试与新增驱动验证均可重复通过。
