# K380 ZMK 无人值守 J-Link/RTT 连接验证

## 验证信息

- 验证日期：2026-08-29
- 硬件版本或样机标识：K380 实板，板号未记录；J-Link S/N 851000967
- 固件仓库：`kimwolf-fs/k380-zmk`
- 固件 commit：`b2c96c7939646c521b822a1dd1e979ee25d587d7`
- GitHub Actions workflow：`K380 CI #68`
- workflow run ID：`33195425364`
- artifact 名称：`k380-matrix-rtt-diagnostics-output`
- 使用的固件文件：`k380-zmk/.superpowers/sdd/k380-zmk-主线实施计划/task-7-ci6-success-artifacts/k380-matrix-rtt-diagnostics-output/zephyr/zmk.hex`
- 测试工具：`JLink.exe`、`JLinkGDBServerCL.exe`、`JLinkRTTClient.exe`、`jlink_exec.py`

## 测试步骤

1. 用 J-Link 探测目标连通性。
2. 烧录任务 7 产出的 RTT 诊断固件。
3. 复位目标。
4. 挂起 RTT Client。
5. 等待 30 s 观察是否出现诊断输出。

## 期望结果

- J-Link 连通正常。
- 烧录校验通过。
- 复位后 RTT 输出应出现诊断启动文本。

## 实际结果

- J-Link 探测成功，目标电压约 `1.819 V`。
- 烧录成功，`verified=true`。
- 复位成功。
- GDB Server 与 RTT Client 均成功连接到 `851000967`。
- 在 30 s 观察窗内未收到新的板端 RTT 文本。

## 工具输出摘要

- `info`: `探测成功`
- `flash`: `烧录成功`
- `reset`: `复位成功`
- `JLinkGDBServerCL`: `Connected to target` / `Waiting for GDB connection`
- `JLinkRTTClient`: 已连接到 `localhost:19021`

## 结论

部分通过。

## 备注

- 这次测试证明无人值守场景下，J-Link 探针连通、烧录和复位可以独立完成。
- 当前未获得可归档的板端 RTT 文本，因此不能把 WS2812B 目视验证、矩阵按键验证或项目级清单勾选项一起关闭。
- 后续要完成 Task 8 其余项，仍需要板端可观测输出或人工介入观察。
