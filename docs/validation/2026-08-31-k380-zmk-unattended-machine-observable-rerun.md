# K380 ZMK 无人值守机器可观测复测

## 验证信息

- 验证日期：2026-08-31
- 硬件版本或样机标识：K380 实板；J-Link S/N 851000967
- 固件仓库：`kimwolf-fs/k380-zmk`
- 固件 commit：`b2c96c7939646c521b822a1dd1e979ee25d587d7`
- GitHub Actions workflow：`K380 CI #68`
- workflow run ID：`33195425364`
- artifact 名称：`k380-matrix-rtt-diagnostics-output`
- 使用的固件文件：沿用 `k380-matrix-rtt-diagnostics-output/zephyr/zmk.hex` 所对应固件，未重新烧录
- 测试工具：`JLink.exe`、`JLinkGDBServerCL.exe`、`JLinkRTTClient.exe`、PowerShell `Get-PnpDevice` / `Get-Volume`

## 测试步骤

1. 用 J-Link `info` 确认探针和目标连通性。
2. 读取 `0x20002c84` 的诊断快照。
3. 对目标执行一次 `reset`。
4. 通过 RTT 抓取一段 15 s 启动日志。
5. 继续抓取一段 20 s RTT 日志，同时查看 USB 枚举和卷标。
6. 对当前实例 ID 和 `BOOT` 卷做补充查询。

## 期望结果

- J-Link 探测和内存读取正常。
- 诊断快照 magic 和计数有效。
- 重置后能看到无人值守启动日志。
- Windows 侧能观测到当前 USB/卷标状态。
- 不需要人工在旁边按键或盯灯。

## 实际结果

- `J-Link info` 成功，目标电压约 `1.819 V`。
- `read-mem 0x20002c84` 读到有效诊断快照：
  - `magic0 = 0x4B333830`
  - `magic1 = 0x44494147`
  - `kscan_init_count = 1`
  - `kscan_enable_count = 2`
- `reset` 成功。
- 第一次 RTT 窗口读到的文本包含：
  - `K380_RTT_DIRECT_BOOT ready`
  - `K380_KSCAN_BOOT ready`
  - `fs_nvs: 8 Sectors of 4096 bytes`
  - `bt_hci_core: HW Platform: Nordic Semiconductor`
  - `bt_hci_core: No ID address. App must call settings_load()`
- 第二次 RTT 窗口没有收到新的板端文本。
- `Get-Volume` 仍可见 `BOOT` FAT32 卷。
- `Get-PnpDevice` 的精确实例查询没有给出清晰的当前在线状态，结果偏向历史/Unknown 设备记录，不能据此单独确认 USB 当前枚举态。

## 工具输出摘要

- `info`: `探测成功`
- `read-mem`: `内存读取成功`
- `reset`: `复位成功`
- RTT：先出现启动日志片段，后续窗口无新增板端输出

## 结论

部分通过。

## 备注

- 这轮测试确认了 J-Link 无人值守连通、复位和内存诊断仍可稳定执行。
- 这轮测试没有拿到一组干净、连续的 USB/HID 当前枚举证据，因此不能把 USB 观测、bootloader/app 状态判定和项目级清单更新一起关闭。
- RTT 中出现的启动日志说明目标仍可进入 ZMK 路径；但第二轮 RTT 无新输出，USB 枚举又混有历史记录，当前还不适合把它当成完整闭环。
