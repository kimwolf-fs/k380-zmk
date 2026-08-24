# K380 Bootloader 硬件验证

**状态：** 验证清单已定义；当前缺少实板测试条件，实板项目延期未完成。

**用途：** 本文档只记录 K380 Bootloader 与 ZMK 应用进入 Bootloader 的硬件验证路径。
板级硬件契约见 [`hardware-contract.md`](hardware-contract.md)。

## 恢复入口

K380 常规恢复路径只使用 `Del` 冷启动入口。RESET 仅作为测试点保留给开发调试、SWD 首刷和救砖。

常规用户可操作恢复路径为：

1. 断电。
2. 按住键盘上的 `Del`。
3. 保持 `Del` 按下并上电。
4. Bootloader 在冷启动检测窗口内确认 `Del` 被按下后进入 UF2+CDC 或救援模式。

ZMK 应用运行时入口为 `Fn+Del`，通过 keymap 中的 `&bootloader` 绑定进入 Bootloader/UF2。
它和上电前按住 `Del` 是两个不同入口：前者需要 ZMK 应用仍可运行，后者由 Bootloader
在冷启动窗口内检测。

## 首刷与 USB 边界

首次写入 Bootloader 只能通过 J-Link/SWD 完成。USB/UF2 只适用于 Bootloader 已经存在后的
应用更新、恢复或重新进入 UF2。

必须验证：

- J-Link/SWD 可首刷、擦除和救砖。
- USB 可枚举 UF2+CDC。
- CDC-only 枚举与 Bootloader 设计一致。
- ZMK `Fn+Del` 可从应用运行态进入 Bootloader/UF2。
- 上电前按住 `Del` 可从冷启动进入常规恢复入口。

## 延期实板项目

以下项目因当前没有实板测试条件而延期，不能删除，也不能用 CI 绿色代替：

- 上电前按住 `Del` 的冷启动检测窗口。
- ZMK `Fn+Del` 运行时入口。
- UF2+CDC 与 CDC-only 枚举。
- SWD 首刷、擦除和救砖。
- 与矩阵诊断固件配合的逐键坐标验证。
