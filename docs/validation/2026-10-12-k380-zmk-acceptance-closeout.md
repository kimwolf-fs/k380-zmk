# K380 ZMK 正式固件验收结项

## 验证信息

- 验证日期：2026-10-12
- 硬件版本或样机标识：K380 实板；J-Link 探针 S/N `851000967`
- 固件仓库：`kimwolf-fs/k380-zmk`
- 固件 commit：`18062af2`（PR #6，合并提交 `a6234fe9`）
- GitHub Actions workflow：`K380 CI`
- workflow run ID：`38196416369`，结论 success
- artifact 名称：`k380-zmk-firmware`
- 使用的固件文件：`zephyr/zmk.hex`（同一 artifact 内另有 `zmk.uf2`、`.config`、`zephyr.dts`、`zmk.map`）
- 测试工具：功耗分析仪（模拟电池 3.7 V）、`JLink.exe`、`gh` CLI

## 1. 正式应用 artifact 来源确认

### 测试步骤

1. 用 `gh run download 38196416369 --name k380-zmk-firmware` 下载正式产物。
2. 检查 artifact 内容与来源 job。
3. 核对 workflow run 的 `headSha` 与本地正式分支 HEAD 一致。
4. 用 J-Link `loadfile` 只烧写应用区 `zmk.hex`，不做全片擦除。

### 期望结果

- artifact 来自正式构建 job `firmware-build`，不是 `board-build`、`driver-build` 或
  `matrix-rtt-diagnostics` 产物。
- 同一 artifact 内 `zephyr.dts` 的 `default_layer` 不是 `<&none>`，包含普通按键层和
  `Fn+Del` bootloader 入口。

### 实际结果

- artifact 内容为 `zmk.hex`、`zmk.uf2`、`.config`、`zephyr.dts`、`zmk.map`，来源 job
  `firmware-build`；run `38196416369` 的 `headSha` 为
  `18062af2be828025e11b593104aea9b944b886e6`。
- `firmware-build` 中的 keymap 契约校验断言：`default_layer` 不是 `&none`、恰好 80 个
  binding、第 71 项为 `&mo`、`&k380_dk` 不少于 70 处；`fn_layer` 含 `&bootloader` 且不含
  `&studio_unlock`；`CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS=5`、
  `CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS=50` 在生成的 `.config` 中生效。
- J-Link 烧写 `zmk.hex` 返回 `verified: true`。
- 烧写后设备正常启动、蓝牙自动回连、普通键与 Fn 组合键输入正常。

### 结论

通过。

### 异常备注和后续动作

无。

## 2. 电池供电拒绝进入 Bootloader（Z8）

### 测试步骤

1. 只使用功耗分析仪 3.7 V 供电，完全断开 J-Link 与 USB。
2. 在 ZMK 运行态按下 `Fn+Del`。

### 期望结果

- 不进入 bootloader；WS1/WS2/WS3 同步红闪 3 次；设备留在应用；不发生重启。

### 实际结果

- 未进入 bootloader；三颗灯红色闪烁提示；无重启；蓝牙连接保持正常，按键输入正常。

### 结论

通过。与 `k380-bootloader/docs/validation/2026-09-23-k380-power-entry-gate.md` 的 H01 记录
一致；本次是在整合后的正式固件上复测确认。

### 异常备注和后续动作

电池供电拒绝进入 bootloader 是规格要求，不作为缺陷跟踪。

## 3. WS2812B 灯位、顺序与状态行为

| 观察项 | 期望 | 实际 | 判定 |
| --- | --- | --- | --- |
| 数据引脚与数量 | `P0.13`，`chain-length = <4>` | CI 对生成的 `zephyr.dts` 断言 `P0.13`、4 颗串行、`worldsemi,ws2812-spi`、80 us reset delay | 通过 |
| WS4 物理位置 | 电源开关键附近 | 低压慢闪、红灯快闪和 3 秒警告均在电源键附近观察到 | 通过 |
| WS1/WS2/WS3 物理位置 | F3/F2/F1 上方，分别对应槽位 3/2/1 | 按 `A` 唤醒时只有正确槽位蓝灯慢闪 | 通过 |
| Z6 连接提示 | 连接成功后短提示 | 回连成功后当前槽位绿灯亮起后熄灭 | 通过 |
| 三灯误闪修复 | system-off 唤醒不应出现三灯蓝闪两次 | 修复后按 `C`、`F`、`G` 唤醒均不再出现三灯蓝闪 | 通过 |

### 结论

通过。

### 异常备注和后续动作

Z6 绿灯会在电源状态变化时重复亮起，属于既有行为，不在本次验收范围内。

## 4. 无二极管多键与矩形歧义过滤复测

### 测试步骤

在电池供电下用正式固件逐组按下并保持组合键，观察主机侧 HID 输出。

### 实际结果

| 组合 | 实际输出 | 判定 |
| --- | --- | --- |
| `H` 单键 | `h` | 通过 |
| `J` 单键 | `j` | 通过 |
| `H` + `J` | 先按 `H`：`h` 后跟 `jjjjjjjjjjj`；先按 `J`：`j` 后跟 `hhhhhhhhhh` | 通过 |
| `J` + `F` + `H` | `jffffff`，连续 3 轮一致，无虚假 `Q` 或 `H` | 通过 |
| `J` + `F` + `Q` | `jffffff`，无虚假 `H` | 通过 |
| `A` + `H` | `a` 后跟连续 `h`，连续 3 轮，无额外字符 | 通过 |

- 三键组合均未产生四个键之外的字符，松开后立即停止输出，无卡键、无重启。

### 结论

通过。

### 异常备注和后续动作

更早的多轮矩阵与列唤醒证据见 `docs/validation/2026-09-18-k380-low-power-wake.md`
的 Test 12–15。

## 5. GPIO 中断唤醒与扫描启动/停止

引用 `docs/validation/2026-09-18-k380-low-power-wake.md` 的 Test 4–21：覆盖跨列唤醒、
唤醒首键消费、重复 20 轮关机/唤醒、按住按键跨越关机边界，以及 column-7 恢复扫描误判的
根因修复与复测。

### 结论

通过（唤醒、扫描启动、扫描停止和已知恢复扫描误判路径）。

### 异常备注和后续动作

未做通用扫描或 SAADC 故障注入，通用错误恢复不在本记录结论内；对应项保留在
`docs/k380-实板验证清单.md` 待验证区。

## 6. 电源与稳压器假设

| 假设 | 证据 | 判定 |
| --- | --- | --- |
| `UICR.REGOUT0 = 1.8 V` | 根 PRD 板级事实，正式构建契约断言 | 通过 |
| Bootloader `VDDH > 4.5 V` USB DFU/UF2 写入门禁 | `k380-bootloader/docs/validation/2026-09-23-k380-power-entry-gate.md` H01–H04 | 通过 |
| ZMK 3.4 V 低电量提醒与 3.2 V 保护性软关机 | `docs/validation/2026-10-09-k380-low-voltage-validation.md` L1–L5 | 通过 |
| 低电压软关机前状态保存与恢复 | 同上 L3/L5 与 Z1/Z2 | 通过 |
| 空闲、Z5 回连超时和 Z7 配对超时自动关机不亮红灯 | 同上「超时关机红灯抑制与低压回归」T1–T4 | 通过 |

### 异常备注和后续动作

`VDDH > 4.5 V` 立即取消低压软关机的实板确认（3 秒警告窗口内插 USB，以及灯灭后再插 USB）
仍需单独测试，见 `docs/k380-实板验证清单.md` 待验证区。

## 关联记录

- `k380-zmk/docs/validation/2026-09-18-k380-low-power-wake.md`
- `k380-zmk/docs/validation/2026-09-23-k380-idle-system-off.md`
- `docs/validation/2026-10-09-k380-low-voltage-validation.md`
- `k380-bootloader/docs/validation/2026-09-23-k380-power-entry-gate.md`
