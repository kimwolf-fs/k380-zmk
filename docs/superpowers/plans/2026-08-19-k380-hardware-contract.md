# K380 硬件契约文档实施计划

> **面向 agent 执行者：** 必须使用 `superpowers:subagent-driven-development`（推荐）或
> `superpowers:executing-plans` 逐任务执行。本计划使用复选框跟踪进度。

**目标：** 在 `docs/k380/` 建立 K380 的长期硬件契约，固化已确认的板级、电源、USB、
状态灯和 Bootloader/ZMK 配置边界，并通过 GitHub Actions 验证文档变更仍触发 K380 CI。

**架构：** `pinmap.md` 保持矩阵网络到 SoC GPIO 的唯一来源，`matrix-layout.md` 保持
物理按键到 RC 坐标的唯一来源。新建 `hardware-contract.md` 只记录跨 Bootloader、ZMK
board 和实板 bring-up 的硬件事实、问答结论、实现影响、验证方法和变更规则。所有文档
使用中文；不提交 USB VID/PID 授权文件或其他私密资料。

**技术栈：** Markdown、Git、PowerShell、GitHub Actions、K380 CI。

---

## 文件结构与责任

```text
docs/k380/
  pinmap.md
    矩阵 R/C 网络、GPIO 分配及矩阵硬件边界。
  matrix-layout.md
    80 个物理按键到 RC(行,列) 的映射。
  hardware-contract.md
    电源、时钟、USB、恢复、状态灯、电池采样、Bootloader/ZMK 契约和验证门禁。

docs/superpowers/specs/
  2026-08-19-k380-hardware-contract-design.md
    本计划的已批准设计规格；实施完成后标记完成。

.github/workflows/
  k380-ci.yml
    已监听 docs/k380/**；文档提交后触发 K380 CI。
```

本计划会同时提交当前工作树中尚未提交的 K380 文档迁移内容：

```text
.github/workflows/k380-ci.yml
docs/k380/pinmap.md
docs/k380/matrix-layout.md
docs/k380/hardware-contract.md
docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md
docs/superpowers/specs/2026-08-14-k380-no-diode-matrix-design.md
docs/superpowers/specs/2026-08-17-k380-github-actions-validation-design.md
docs/superpowers/specs/2026-08-14-k380-pinmap.md  # 删除
```

不得修改：

```text
app/module/drivers/kscan/
app/module/dts/bindings/kscan/
app/boards/
zmk-keyboard-k380/drivers/
```

本计划不创建 K380 Bootloader board、ZMK board、Flash 分区、keymap 或状态灯 C 代码。

### Task 1: 创建硬件契约正文

**文件：**
- 创建：`docs/k380/hardware-contract.md`
- 参考：`docs/k380/pinmap.md`
- 参考：`docs/k380/matrix-layout.md`
- 参考：`docs/superpowers/specs/2026-08-19-k380-hardware-contract-design.md`

- [ ] **步骤 1：写入职责、来源和问答格式**

创建文件并写入以下开头。该文件明确哪些内容由它负责，哪些内容仍由 pinmap 和矩阵布局
文档负责，避免复制 23 个 GPIO 表或 80 个 RC 表。

```markdown
# K380 硬件契约

**状态：** 已确认硬件事实；Flash 应用分区参数待 K380 Bootloader linker map 产出后记录。

**用途：** 本文档是 K380 Bootloader、ZMK board 和实板 bring-up 共用的板级硬件契约。
矩阵 GPIO 分配见 [`pinmap.md`](pinmap.md)，物理按键 RC 映射见
[`matrix-layout.md`](matrix-layout.md)。

## 记录规则

每个主题包含：

- **问题：** 要解决的硬件或固件边界。
- **已确认答案：** 已由项目所有者确认的硬件事实或配置。
- **实现影响：** Bootloader、ZMK 或两者必须遵守的行为。
- **验证方式：** 构建、实板或文件检查的可重复验证方法。
```

- [ ] **步骤 2：写入已确认硬件总览**

在 `## 已确认硬件总览` 中写入下表。不要写入授权文件位置、授权方联系人或其他私密
信息；只记录项目所有者持有书面授权。

```markdown
| 项目 | 已确认值 |
| --- | --- |
| SoC | nRF52840-QIAA |
| 矩阵 | 8 行 x 15 列、无二极管、row2col |
| USB | USB-C；CC1/CC2 分别经 5.1 kOhm 下拉至 GND；VBUS 有保护；D+/D- 有 ESD 保护 |
| USB 身份 | UF2+CDC：`0x303A:0x1011`；CDC-only：`0x303A:0x1012` |
| USB 授权 | 项目所有者保管对应 VID/PID 的书面授权；授权文件不提交到仓库 |
| 高频时钟 | 外接 32 MHz 晶振，连接 XC1/XC2，已有匹配负载电容 |
| 低频时钟 | 无外接 32.768 kHz 晶振，使用内部 RC |
| NFC | P0.09/P0.10 未使用 |
| 其他外设 | 除本文档和矩阵文档所列项目外，当前无其他 nRF52840 外设或控制信号 |
```

- [ ] **步骤 3：写入电源、时钟和恢复契约**

创建 `## 电源与时钟` 和 `## USB、SWD 与恢复路径`。每节必须使用“问题、已确认答案、
实现影响、验证方式”四项。

电源节必须包含以下明确值：

```markdown
**问题：** 如何在 USB 与电池模式下为 nRF52840 供电并保证低电量运行。

**已确认答案：**

- USB 5 V 与单节锂电池经自动切换电路接入 VDDH。
- 电池模式下 VDDH 低于 2.75 V 时断开。
- REG0 使用内部 LDO；`UICR.REGOUT0` 固定为 2.7 V。
- REG1 使用内部 DC/DC；DCC-DEC4 的 LC 网络已确认符合参考设计。
- DCCH 没有电感，REG0 DC/DC 不可启用。

**实现影响：**

```c
#define UICR_REGOUT0_VALUE UICR_REGOUT0_VOUT_2V7
#define ENABLE_DCDC_0 0
#define ENABLE_DCDC_1 1
```

**验证方式：** 在 USB 和接近 2.75 V 的电池模式下测量 VDDH 与 VDD，确认 VDD 保持
2.7 V；确认 DCDC0 未启用且 DCDC1 已启用。
```

恢复节必须包含：

```markdown
- RESET 只有测试点，无用户按键。
- SWDIO、SWCLK、RESET、GND、VTref 均有测试点；VTref 必须接 nRF VDD。
- 日常进入 UF2 使用 ZMK `&bootloader`；无法启动应用时用 RESET 测试点双击复位；
  无法恢复时使用 SWD。
- Bootloader 使用 USB UF2 与 CDC，不启用 BLE OTA、签名固件或双 bank 回滚。
```

- [ ] **步骤 4：写入 WS2812B 硬件与灯效状态机**

创建 `## WS2812B 状态灯`，先写入硬件连接，再写入蓝牙和系统灯效。连接与索引必须
完全一致，不能把 LED1-LED3 的蓝牙槽位顺序写为递增：

```markdown
**已确认答案：**

- P0.13 连接 SN74LVC1T45 的 A 端。
- SN74LVC1T45 的 VCCA 接 nRF VDD，VCCB 接 VDDH，DIR 固定接 VCCA。
- B 端驱动第 1 颗 WS2812B DIN；四颗 WS2812B 串联。
- 硬件数据方向为 LED1 -> LED2 -> LED3 -> LED4。

| 固件索引 | 物理 LED | 基本功能 |
| --- | --- | --- |
| 0 | LED1 | 蓝牙配置槽 3 |
| 1 | LED2 | 蓝牙配置槽 2 |
| 2 | LED3 | 蓝牙配置槽 1 |
| 3 | LED4 | 系统状态 |
```

继续写入蓝牙灯状态机：

```markdown
LED1、LED2、LED3 只表达蓝牙状态，三者互斥；LED4 不表达蓝牙状态。

| 蓝牙状态 | 灯效 | 结束条件 |
| --- | --- | --- |
| 连接中 | 当前槽位绿色慢速呼吸 | 连接成功、超时或切换槽位 |
| 配对中 | 当前槽位绿色快速呼吸 | 配对成功、超时或切换槽位 |
| 已连接 | 当前槽位绿色常亮 | 5 秒后熄灭 |
| 未选择、连接失败、配对失败或断开 | 熄灭 | 无 |
```

写入 LED4 系统状态、优先级与功耗规则：

```markdown
| 系统事件 | 灯效 | 结束条件 |
| --- | --- | --- |
| Bootloader / UF2 就绪 | 蓝色慢速呼吸 | 10 秒后熄灭 |
| UF2 正在写入 | 黄色快速呼吸 | 写入结束 |
| UF2 写入成功 | 绿色常亮 | 5 秒后熄灭 |
| UF2 写入失败 | 红色快速闪烁 | 5 秒后熄灭 |
| USB 外部供电 / 充电路径可用 | 黄色双闪 | USB 接入时显示一次 |
| 低电量 | 红色单闪 | 每 30 秒一次；恢复电压或插入 USB 后停止 |
| 启动完成 | 白色短闪 | 显示一次 |
| 致命错误 | 红色三连闪 | 5 秒后熄灭 |

系统状态优先级由高到低为：UF2 写入或写入结果、致命错误、低电量、USB 接入提示、
启动完成。状态灯不得有无限期常亮或呼吸效果；每次系统提示只点亮 LED4，并必须设置
全局亮度上限。
```

- [ ] **步骤 5：写入电池采样、Bootloader/ZMK 门禁和实板验证清单**

创建 `## 电池电压采样`、`## Bootloader 与 ZMK 配置门禁`、`## 实板验证清单` 和
`## 变更规则`。

电池采样节必须写入：

```markdown
- 使用内部 `VDDHDIV5`，不使用外接电池分压网络。
- USB 未插入时，采样值解释为电池电压；USB 插入时，只解释为外部 USB 供电存在。
- VDDH 低于 3.20 V 触发低电量提示；高于 3.30 V 恢复。
- 不得将 VDDH 高于电池电压解释为“电池正在充电”或“电池已充满”。
```

配置门禁必须写入：

```markdown
K380 Bootloader 的应用 Flash 起始地址、长度和 ZMK `fixed-partitions` 只能从首次成功
构建的 K380 Bootloader linker map 获得。本文档不得在该 map 产出前填写或猜测这些值。
```

实板验证清单必须逐条包含：SWD 首刷/擦除/救砖、USB-C 的 UF2+CDC 与 CDC-only 枚举、
`&bootloader`、RESET 测试点双击、VDDHDIV5 与万用表比对、低电量回差、四灯数据顺序、
所有 LED 状态和 USB 供电提示。

变更规则必须说明：矩阵 GPIO 改动更新 `pinmap.md`，物理按键改动更新
`matrix-layout.md`，供电/时钟/USB/状态灯/分区改动更新本文档并先创建新功能分支。

### Task 2: 建立文档交叉引用并完成迁移

**文件：**
- 修改：`docs/k380/pinmap.md`
- 修改：`docs/k380/matrix-layout.md`
- 修改：`.github/workflows/k380-ci.yml`
- 修改：`docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md`
- 修改：`docs/superpowers/specs/2026-08-14-k380-no-diode-matrix-design.md`
- 修改：`docs/superpowers/specs/2026-08-17-k380-github-actions-validation-design.md`
- 删除：`docs/superpowers/specs/2026-08-14-k380-pinmap.md`

- [ ] **步骤 1：在 pinmap 文档添加相关文档链接**

在 `docs/k380/pinmap.md` 的用途段落后添加：

```markdown
**相关文档：**

- [`matrix-layout.md`](matrix-layout.md)：物理按键到 RC 坐标的映射。
- [`hardware-contract.md`](hardware-contract.md)：电源、USB、状态灯和 Bootloader/ZMK
  配置契约。
```

- [ ] **步骤 2：在物理按键文档添加相关文档链接**

在 `docs/k380/matrix-layout.md` 的用途段落后添加：

```markdown
**相关文档：**

- [`pinmap.md`](pinmap.md)：矩阵网络到 nRF52840 GPIO 的电气分配。
- [`hardware-contract.md`](hardware-contract.md)：与矩阵共同构成 K380 的板级硬件契约。
```

- [ ] **步骤 3：确认并保留已完成的 pinmap 路径迁移**

确认以下条件全部成立：

```text
旧文件 docs/superpowers/specs/2026-08-14-k380-pinmap.md 已删除。
docs/k380/pinmap.md 存在。
.github/workflows/k380-ci.yml 的 push 与 pull_request 均监听 docs/k380/**。
迁移目标范围 `.github`、`docs/k380`、`docs/superpowers/specs` 和 `zmk-keyboard-k380`
不再引用旧 pinmap 路径；历史实施计划不在此检查范围内，因其必须保留迁移说明。
```

执行：

```powershell
rg -n -F "docs/superpowers/specs/2026-08-14-k380-pinmap.md" .github docs\k380 docs\superpowers\specs zmk-keyboard-k380
```

预期：上述范围无匹配，命令退出码为 `1`；历史实施计划不在检查范围内。

### Task 3: 验证、提交并通过 GitHub Actions 检查文档路径

**文件：**
- 修改：`docs/superpowers/specs/2026-08-19-k380-hardware-contract-design.md`
- 测试：`docs/k380/`
- 测试：`.github/workflows/k380-ci.yml`

- [ ] **步骤 1：执行 Markdown 结构与关键值检查**

在仓库根目录执行：

```powershell
$contract = "docs\k380\hardware-contract.md"
$required = @(
  "# K380 硬件契约",
  "## 已确认硬件总览",
  "## 电源与时钟",
  "## USB、SWD 与恢复路径",
  "## WS2812B 状态灯",
  "## 电池电压采样",
  "## Bootloader 与 ZMK 配置门禁",
  "0x303A:0x1011",
  "0x303A:0x1012",
  "UICR_REGOUT0_VOUT_2V7",
  "ENABLE_DCDC_0 0",
  "ENABLE_DCDC_1 1",
  "LED1 | 蓝牙配置槽 3",
  "LED2 | 蓝牙配置槽 2",
  "LED3 | 蓝牙配置槽 1",
  "LED4 | 系统状态"
)

$content = Get-Content -Raw $contract
$missing = $required | Where-Object { -not $content.Contains($_) }
if ($missing) {
  throw "硬件契约缺少内容: $($missing -join ', ')"
}
Write-Output "硬件契约关键章节和配置值完整。"
```

预期输出：

```text
硬件契约关键章节和配置值完整。
```

- [ ] **步骤 2：执行 RC 对照、链接和补丁格式检查**

执行：

```powershell
$layout = Get-Content -Raw "docs\k380\matrix-layout.md"
$matches = [regex]::Matches($layout, 'RC\((\d+),(\d+)\)')
$coords = @($matches | ForEach-Object { '{0},{1}' -f $_.Groups[1].Value, $_.Groups[2].Value })
if ($coords.Count -ne 80) { throw "预期 80 个 RC 坐标，实际 $($coords.Count)" }
if (($coords | Group-Object | Where-Object Count -gt 1).Count -gt 0) { throw "RC 坐标存在重复" }
if (($matches | Where-Object { [int]$_.Groups[1].Value -gt 7 -or [int]$_.Groups[2].Value -gt 14 }).Count -gt 0) {
  throw "RC 坐标超出 8x15 范围"
}
$pinmap = "docs\k380\pinmap.md"
$matrixLayout = "docs\k380\matrix-layout.md"
$contract = "docs\k380\hardware-contract.md"
foreach ($document in @($pinmap, $matrixLayout, $contract)) {
  if (-not (Test-Path -LiteralPath $document -PathType Leaf)) {
    throw "缺少文档: $document"
  }
}
$pinmapContent = Get-Content -Raw $pinmap
$matrixLayoutContent = Get-Content -Raw $matrixLayout
if (-not $pinmapContent.Contains("](matrix-layout.md)")) {
  throw "pinmap 缺少 matrix-layout 相对链接"
}
if (-not $pinmapContent.Contains("](hardware-contract.md)")) {
  throw "pinmap 缺少 hardware-contract 相对链接"
}
if (-not $matrixLayoutContent.Contains("](pinmap.md)")) {
  throw "matrix-layout 缺少 pinmap 相对链接"
}
if (-not $matrixLayoutContent.Contains("](hardware-contract.md)")) {
  throw "matrix-layout 缺少 hardware-contract 相对链接"
}
git diff --check
rg -n -F "docs/superpowers/specs/2026-08-14-k380-pinmap.md" .github docs\k380 docs\superpowers\specs zmk-keyboard-k380
if ($LASTEXITCODE -ne 1) { throw "旧 pinmap 路径仍存在引用" }
Write-Output "RC 对照、链接、旧路径和补丁格式检查通过。"
```

预期输出：

```text
RC 对照、链接、旧路径和补丁格式检查通过。
```

- [ ] **步骤 3：将设计规格标记为已完成**

将 `docs/superpowers/specs/2026-08-19-k380-hardware-contract-design.md` 的状态改为：

```markdown
**状态：** 已完成。
```

不修改其中已批准的设计内容。

- [ ] **步骤 4：提交 K380 硬件文档集**

仅暂存本计划列出的 K380 文档迁移、硬件契约、相关 workflow 路径变更和设计规格状态
更新；不要暂存生产 C 代码或不相关的用户修改。

```powershell
git add `
  .github/workflows/k380-ci.yml `
  docs/k380/pinmap.md `
  docs/k380/matrix-layout.md `
  docs/k380/hardware-contract.md `
  docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md `
  docs/superpowers/specs/2026-08-14-k380-no-diode-matrix-design.md `
  docs/superpowers/specs/2026-08-17-k380-github-actions-validation-design.md `
  docs/superpowers/specs/2026-08-19-k380-hardware-contract-design.md
git add -u docs/superpowers/specs/2026-08-14-k380-pinmap.md
git commit -m "docs(k380): 建立硬件契约"
```

预期：提交仅包含 K380 硬件文档、相关历史引用迁移和 K380 CI 文档路径监听。

- [ ] **步骤 5：推送并检查 GitHub Actions**

```powershell
git push origin feat/k380-board-integration
```

在 GitHub Actions 检查本分支最新的 `K380 CI`。预期四个 job 均成功：

```text
module-metadata: success
ghost-filter: success
driver-build: success
module-isolation: success
```

文档变更不修改生产驱动；此 CI 仅验证 `docs/k380/**` 路径监听正常、既有 K380 模块
验证未回归。

## 计划自检

- 规格覆盖：硬件契约正文、pinmap 与矩阵布局交叉引用、状态灯映射、低电量策略、
  Bootloader/ZMK 门禁、文档路径迁移和 GitHub Actions 验证均有对应任务。
- 范围控制：不实现 Bootloader board、ZMK board、Flash 分区或状态灯代码。
- 隔离控制：不修改 ZMK 共享 kscan、binding、board 或 K380 生产驱动。
- 无占位符：所有待未来确定的 Flash 分区值均有明确来源和门禁，未伪造数值。
