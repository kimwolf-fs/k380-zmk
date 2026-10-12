# K380 ZMK 机器可观测子项验证

## 验证信息

- 验证日期：2026-08-29
- 硬件版本或样机标识：K380 实板；J-Link S/N 851000967
- 固件仓库：`kimwolf-fs/k380-zmk`
- 固件 commit：`b2c96c7939646c521b822a1dd1e979ee25d587d7`
- GitHub Actions workflow：`K380 CI #68`
- workflow run ID：`33195425364`
- artifact 名称：`k380-matrix-rtt-diagnostics-output`
- 使用的固件文件：`k380-matrix-rtt-diagnostics-output/zephyr/zmk.hex`
- 测试工具：`JLink.exe`、`jlink_exec.py`、`jlink_rtt.py`、PowerShell `Get-PnpDevice`

## 测试步骤

1. 通过 RTT 抓取 ZMK 诊断固件启动日志。
2. 通过 PowerShell 查询 Windows 当前 USB/HID 枚举。
3. 通过 J-Link 读取 `k380_kscan_diag_snapshot` 内存快照。

## 期望结果

- RTT 出现 K380 诊断启动文本和 ZMK 启动文本。
- Windows 侧出现 ZMK USB HID 设备。
- `k380_kscan_diag_snapshot` 的 magic 和初始化计数有效。
- 无物理按键输入时，矩阵事件计数保持为 0。

## 实际结果

- RTT 输出出现：
  - `K380_RTT_DIRECT_BOOT ready`
  - `K380_KSCAN_BOOT ready`
  - `K380_KSCAN_INIT ready`
  - `Welcome to ZMK!`
  - `usb_hid: Device configured`
  - `zmk: Endpoint changed: USB`
- Windows 侧出现：
  - `USB\VID_1D50&PID_615E\191A51F1083758C7`
  - `HID\VID_1D50&PID_615E&COL01...`，设备类型为 `HID Keyboard Device`
  - `HID\VID_1D50&PID_615E&COL02...`，设备类型为 HID 用户控制设备
- `k380_kscan_diag_snapshot` 地址：`0x20002c84`
- 快照字段：
  - `magic0 = 0x4B333830` (`K380`)
  - `magic1 = 0x44494147` (`DIAG`)
  - `pre_kernel_probe_count = 1`
  - `post_kernel_probe_count = 1`
  - `boot_probe_count = 1`
  - `application_probe_count = 1`
  - `kscan_init_count = 1`
  - `kscan_enable_count = 2`
  - `heartbeat_entry_count = 0`
  - `heartbeat_count = 0`
  - `matrix_event_count = 0`
  - `raw_matrix_event_count = 0`
  - `last_row = 0`
  - `last_col = 0`
  - `last_pressed = 0`

## 结论

部分通过。

## 备注

- 本记录覆盖无人值守可观测子项：ZMK app 启动、USB HID 枚举、矩阵扫描驱动初始化和无按键输入时事件计数保持为 0。
- 未覆盖 40 个未使用坐标，因为没有外部夹具或人工按键输入去激励矩阵坐标。
- 未覆盖多键矩形歧义过滤的实板输入，因为没有物理多键操作或矩阵注入夹具。
- 未覆盖 GPIO 唤醒、扫描停止和异常恢复的完整实板闭环。
- 未覆盖 WS2812B 颜色、顺序和闪烁节奏，因为这些需要视觉观察或光学采集。
- 未覆盖电池阈值和软关机，因为需要可控 VDDH/USB 供电切换或等效硬件注入。
