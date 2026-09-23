# K380 电池空闲休眠与 system-off 验证

## 固件和配置

- 提交：`0d17678a`，分支 `fix/k380-matrix-settle-delay`
- GitHub Actions push K380 CI：run `35825756640`，结果成功
- 正式 artifact：`k380-zmk-firmware`
- artifact 目录：`E:/project/k380-keyboard/.artifacts/k380-zmk/35825756640-firmware/`
- `CONFIG_K380_IDLE_SLEEP_TIMEOUT_MS=600000`
- `CONFIG_ZMK_PM_SOFT_OFF=y`
- `CONFIG_ZMK_SLEEP` 未启用
- `CONFIG_K380_AUTO_SYSTEM_OFF` 未启用为独立旧路径；电池空闲休眠由本次低功耗策略门禁控制
- Intel HEX：14747 条记录，校验通过，数据范围 `[0x26000,0x5F8B8)`，应用分区 `[0x26000,0xCA000)`
- `zmk.hex` SHA-256：`370477935BC9C1331C829C1CAF2376D51948572840BB1BE655983554538E936A`

## 自动验证

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| native low-power | 通过 | push K380 CI 的 `native-tests (low-power)` |
| shutdown-input | 通过 | `native-tests (shutdown-input)` |
| shutdown-behavior | 通过 | `native-tests (shutdown-behavior)` |
| system-off-wake | 通过 | `native-tests (system-off-wake)` |
| battery-policy | 通过 | `native-tests (battery-policy)` |
| formal power contract | 通过 | `module-metadata` 的 generated power contract checker |
| board/firmware artifact | 通过 | `k380-zmk-firmware` artifact 已生成 |
| J-Link info | 通过 | nRF52840_xxAA，SWD 4000 kHz，探针 `851000967`，VTref 1.812 V |
| J-Link flash | 通过 | 新 `zmk.hex` 烧写 `verified: true` |
| reset/info | 通过 | 烧写后复位并重新探测成功 |

自动测试覆盖策略边界、计时器和唤醒输入消费；不能证明实测电流、LED 时序或真实首键。
J-Link 连接可能改变低功耗行为，不用于判定 system-off 电流。

## 设计规则

1. 只有电池供电累计空闲时间；USB 供电不启动、不累计计时。
2. USB 转电池时重新开始完整 10 分钟窗口。
3. 矩阵按下和释放都重置空闲窗口；普通运行态 idle 不消费首键。
4. 所有 system-off 唤醒源使用同一语义：首键只唤醒，释放后第二次按键才产生 HID 输入。

## 人工测试 H05-H12

每项使用功耗仪记录进入前后电流；system-off 电流和首键语义不要在 J-Link 连接状态下判断。

| 编号 | 操作 | 预期现象 | 实际结果 | 判定 |
| --- | --- | --- | --- | --- |
| H05 | 电池供电，保持无输入 10 分钟 | 约 10 分钟进入 system-off；LED 熄灭；电流显著下降 | 待填写 | 待判定 |
| H06 | H05 后按一次任意键，再按一次普通键 | 首键只唤醒无 HID；第二键产生一次正常输入 | 待填写 | 待判定 |
| H07 | USB 供电静置超过 10 分钟 | 不进入本次电池空闲 system-off；连接和输入保持正常 | 待填写 | 待判定 |
| H08 | USB 静置接近 10 分钟后拔出 USB | 从拔出时重新计满约 10 分钟才休眠，不沿用 USB 时间 | 待填写 | 待判定 |
| H09 | 电池静置约 5 分钟后按键，再静置 | 按下和释放均重置窗口；从最后一次事件重新计时 10 分钟 | 待填写 | 待判定 |
| H10 | 分别用 Z5、Z7、低压唤醒源唤醒 | 每个 system-off 唤醒源均首键只唤醒，第二键才输出 | 待填写 | 待判定 |
| H11 | 在进入关机边界时保持按键不释放 | 不发生重复唤醒/重启；释放后才允许进入 system-off | 待填写 | 待判定 |
| H12 | 记录 active、普通 idle、system-off 三种电流 | system-off 最低；USB 不触发电池空闲休眠；数据可复现 | 待填写 | 待判定 |

填写规则：你每次只回复一个编号和实际观察，我根据预期逐项判断通过、失败或受阻，
再给出下一项。若出现重启、按键卡住、误输出或电流不降，立即停止该项并保留时序。
