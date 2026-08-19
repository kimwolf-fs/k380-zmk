# K380 GitHub Actions 验证设计

**状态：** 已批准

**目标：** 使用 GitHub Actions 作为 K380 第二阶段的唯一构建和验证入口，使开发者无需
在本地安装 Docker、West、Zephyr SDK 或交叉编译工具链。

## 范围

本设计只覆盖 K380 专用 module 的持续集成验证：

- 既有矩形歧义过滤器的 `native_sim` 单元测试。
- K380 module 注册、DTS binding 和专用 kscan 驱动的 nRF52840DK 编译夹具。
- 已加载 K380 module、但未实例化 K380 compatible 时的现有 ZMK 测试。
- 失败日志和编译输出的 GitHub Actions artifact 上传。

本阶段不构建 K380 board，不生成 UF2，不测试实体矩阵，不烧录硬件，也不修改
ZMK 树内通用 CI workflow。

## 工作流边界

新增独立 workflow：

```text
.github/workflows/k380-ci.yml
```

它只在以下路径变化时触发：

```text
.github/workflows/k380-ci.yml
zmk-keyboard-k380/**
docs/k380/**
docs/superpowers/specs/2026-08-17-k380-no-diode-matrix-driver-design.md
docs/superpowers/specs/2026-08-17-k380-github-actions-validation-design.md
docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md
```

触发事件为 `push`、`pull_request` 和手动 `workflow_dispatch`。workflow 使用
与仓库现有 CI 相同的 ZMK ARM 构建容器、`west init -l app`、`west update` 和
`west zephyr-export` 初始化流程，但不修改现有的 `.github/workflows/test.yml` 或
`.github/workflows/build.yml`。

## 验证作业

工作流包含一个轻量 `module-metadata` job 和三个验证 job。`module-metadata` checkout
后检查 `zmk-keyboard-k380/zephyr/module.yml`，并输出 module 是否已注册。每个验证
job 都独立 checkout、初始化 West workspace 并恢复以 `app/west.yml` 哈希为键的 West
modules 缓存。`driver-build` 和 `module-isolation` 依赖该输出，仅在 module 注册后
启用；这样在 module 注册前不会把普通目录作为 `ZMK_EXTRA_MODULES` 传入 CMake。

### 过滤器单元测试

运行：

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host west twister \
  -T zmk-keyboard-k380/tests/ghost-filter -p native_sim
```

过滤器 job 使用 `zmk-dev-arm:4.1`，该镜像提供 Twister 所需 Python 依赖；
`driver-build` 和 `module-isolation` 继续使用较小的 `zmk-build-arm:4.1`。
成功标准是 `k380.ghost_filter` 的所有测试通过。

### 专用驱动编译夹具

使用 nRF52840DK 的临时 overlay 和 config fragment，执行：

```bash
west build -s app -d build/k380-driver -p always -b nrf52840dk/nrf52840 -- \
  -DZMK_EXTRA_MODULES="${GITHUB_WORKSPACE}/zmk-keyboard-k380" \
  -DEXTRA_CONF_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/tests/driver-build/k380-driver.conf" \
  -DEXTRA_DTC_OVERLAY_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/tests/driver-build/k380-driver.overlay"
```

构建后检查 `zephyr/.config` 含有
`CONFIG_K380_KSCAN_NO_DIODE_MATRIX=y`，并确认生成
`kscan_k380_no_diode_matrix.c.obj`。夹具中的 GPIO 仅用于编译，不能作为 K380
pinmap 或生产 board DTS 的来源。

### module 隔离回归

在设置 `ZMK_EXTRA_MODULES`、但不加入 K380 overlay 的条件下运行现有
`app/tests/matrix-input/kp-press-release` 测试。成功标准：

- 现有测试通过。
- 构建目录中没有 `kscan_k380_no_diode_matrix.c.obj`。

这证明仅加载 module 不会改变其他 ZMK board 或测试的驱动选择。

## 失败诊断和产物

每个 job 均在成功、失败或取消时上传相应 build 目录中的：

```text
**/*.log
**/zephyr/.config
**/zephyr/zephyr.dts
```

验证失败时，开发者通过 GitHub Actions 日志和 artifact 判断错误类别：

- West init/update/export 失败：依赖或容器环境问题。
- `native_sim` 失败：过滤器行为回归。
- 驱动夹具失败：module 注册、binding、Kconfig、CMake 或驱动编译问题。
- 隔离回归失败：K380 module 影响了非 K380 配置。

## 成功标准

- 所有第二阶段验证均可在 GitHub Actions 完成，无本地工具链前置条件。
- K380 代码只触发 K380 专用 CI，不改写 ZMK 通用 CI 的职责。
- driver 在专用 compatible 存在时编译，在不存在时不参与非 K380 构建。
- 在 board 集成前，不发布或上传任何可被视为 K380 成品固件的 UF2。
