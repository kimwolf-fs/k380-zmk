# K380 ZMK 应用恢复与 USB 观测记录

## 验证信息

- 验证日期：2026-08-29
- 硬件版本或样机标识：K380 实板；J-Link S/N 851000967
- 固件仓库：`kimwolf-fs/k380-zmk`
- 固件 commit：`b2c96c7939646c521b822a1dd1e979ee25d587d7`
- GitHub Actions workflow：`K380 CI #68`
- workflow run ID：`33195425364`
- artifact 名称：`k380-matrix-rtt-diagnostics-output`
- 使用的固件文件：`_tmp-zmk.hex`（由 `k380-matrix-rtt-diagnostics-output/zephyr/zmk.hex` 复制到 ASCII 路径）
- 测试工具：`JLink.exe`、`JLinkGDBServerCL.exe`、`JLinkRTTClient.exe`、PowerShell `Get-PnpDevice` / `Get-Volume`

## 测试步骤

1. 用 J-Link 读取应用区和 bootloader 区。
2. 将诊断固件烧录到应用区。
3. 复位目标并读取寄存器。
4. 启动 RTT 观察启动日志。
5. 查看 Windows USB / Volume 枚举。

## 期望结果

- 应用区存在有效向量表和代码。
- 复位后 PC 落在应用区。
- RTT 能输出 ZMK 启动日志。
- USB 枚举应体现应用固件的 HID 行为，而不是仅停留在 bootloader 盘符。

## 实际结果

- 烧录前，应用区 `0x26000` 读回全 `0xFFFFFFFF`，bootloader 区 `0xF4000` 有有效向量表。
- 重新烧录 ASCII 路径后的 `zmk.hex` 成功。
- 复位后 `PC=0x000686B6`，落在应用区。
- RTT 输出出现：
  - `K380_RTT_DIRECT_BOOT ready`
  - `K380_KSCAN_BOOT ready`
  - `Welcome to ZMK!`
  - `usb_hid: Device configured`
  - `zmk: Endpoint changed: USB`
- Windows 侧可见 `USB HID` 设备 `VID_1D50&PID_615E`。
- `Get-Volume` 只看到一个无盘符的 `BOOT` 卷，没有看到可交互盘符。

## 结论

通过。

## 备注

- 这次确认了之前看到的盘符现象来自 bootloader 状态，而不是当前已烧录的 ZMK app。
- 这份记录只覆盖“应用已恢复并且 USB/HID 行为可观测”，不替代 Task 8 的完整实板验收。
