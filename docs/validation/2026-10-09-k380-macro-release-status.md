# K380 宏功能发布核对与实板验收状态

核对日期：2026-10-09（America/Los_Angeles）。GitHub 运行时间使用 UTC，网页运行发生在 UTC 2026-10-10，同一天的洛杉矶当地时间。

## 网页代码与 CI

- 仓库：`kimwolf-fs/k380-keyboard`，分支 `master`。
- 已验收合并提交：`1723b9528e4459605b97095ceb614010a497664b`。
- 本次已推送该提交，不再只是本地合并。
- K380 Config Tool run：`38019501334`，结论 success。
- 地址：<https://github.com/kimwolf-fs/k380-keyboard/actions/runs/38019501334>。
- Browser serial protocol and macro transactions、VM literal vector contract、完整 Python 配置工具测试均通过。
- 合并前后本地回归：Node 161/161，Python 169/169。
- 用户在本对话确认网页测试通过，随后授权合并；包括最新 JSON 源码备份功能的使用验收。此确认不替代所有固件边界项目的逐项实测。

## 固件主线实际构建

- 仓库：`kimwolf-fs/k380-zmk`，主线 `k380`。
- K380 CI run：`37899341191`，提交 `394e75adaaa8d9562f47b59aa18e3bb59f515a02`，结论 success。
- 地址：<https://github.com/kimwolf-fs/k380-zmk/actions/runs/37899341191>。
- 已核对任务：dynamic-macro-vm、dynamic-macro-vm-validate、dynamic-macro-store、dynamic-macro、dynamic-protocol、dynamic-config 均 success；board-build 和 firmware-build 也 success。
- 因此，不能因为运行标题涉及 automatic system off 就认定该运行没有验证宏 VM。
- 已通过 gh 下载正式 artifact `k380-zmk-firmware`，位于 `.artifacts/k380-zmk/37899341191-firmware/zephyr/`。未本地编译固件。
- 生成配置：`K380_DYNAMIC_MACRO_VM=y`、`K380_DYNAMIC_MACRO=y`、`K380_DYNAMIC_PROTOCOL=y`、`K380_DYNAMIC_TRANSPORT=y`、`K380_AUTO_SYSTEM_OFF=y`；`ZMK_STUDIO` 未启用。
- 六个必需产物均存在。应用分区 `[0x26000,0xCA000)`；HEX 数据 `[0x26000,0x5F9CC)`；14764 条记录校验通过，无校验和错误。

| 产物 | SHA-256 |
| --- | --- |
| zmk.hex | `58F89359AF01ADC892D4E9E3AC6C4F4F62171FF3ABFB4AF16B4A1032BFDC1F4C` |
| zmk.elf | `FA246E9456A34D2C5DF3B927392487366BDC28B5499D68FC126620C304D0C6A0` |
| zmk.uf2 | `E8B787134850E6530DF33D9B5D6A60FD0E10E665E7DD26278C67F989D89D1E02` |

## 已有烧写证据与当前设备结论

- 此 HEX 与本地 `37892532422-firmware/zephyr/zmk.hex` 的 SHA-256 完全一致。
- 同一 HEX 哈希在 `2026-09-23-k380-idle-system-off.md` 的“30 秒 Z5 回归镜像与复测”中已有 J-Link 应用区烧写验证成功记录（当时为 AUTO 测试镜像）。
- 当前正式主线产物已启用 AUTO，字节与当时测试镜像一致。正式/测试是产物来源和配置身份，不能仅由相同字节区分。
- 用户已报告当前实板能执行网页宏测试。非空网页测试上传 KVM1 到设备并读取 run id / trace，说明实际 VM 路径可用。
- 本次未读取芯片 Flash，因此不能把历史哈希记录描述为对当前板内每个字节的重新核验。本次未重新烧写相同 HEX，未改动设备持久化宏。

## 仍待逐项归档的固件边界

旧计划的空复选框不能单独证明代码未实现。以下项目只有真实脚本及观察归档后才关闭：

- [ ] M01：长等待停止，按键释放、普通按键响应以及设备终止状态。
- [ ] M02：三个本地计时器与 GE/LE 分支。
- [ ] M03：三层循环、八层调用、最大 code 分块上传。
- [ ] M04：保存后断开网页、断电重启、实体绑定离线触发、重新读回。
- [ ] M05：宏运行时新的宏触发被忽略；普通 USB/BLE 输入和灯效正常。
- [ ] M06：停止开始释放按键的 20 ms 指标，需实际计时测量，网页轮询不能代替。
- [ ] M07：若仍保留旧 v1 升级兼容发布要求，使用受控备份/测试设备验证迁移；不为补记录而覆盖当前用户宏。

当前阶段：网页开发和远端 CI 已完成，固件主线 VM 与正式构建已证实；待关闭的是上述完整实板发布边界，而不是重新开发 VM。Pages 正式部署尚未执行；Github 源码同步不在本版范围，反编译按用户决定不加入。
