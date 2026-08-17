# K380 专用无二极管矩阵驱动实施计划

> **面向 agent 执行者：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐任务执行。本计划使用复选框跟踪进度。

**目标：** 注册 K380 独立 Zephyr module，并实现固定 8 行 x 15 列的 `row2col` 无二极管 `kscan` 驱动，使完整 GPIO 扫描帧在进入 ZMK debounce 前经过既有的矩形歧义过滤器，且所有构建与验证均由 GitHub Actions 完成。

**架构：** 驱动受控派生自 `app/module/drivers/kscan/kscan_gpio_matrix.c`，上游基线提交为 `6941abc2afab16502cff9c5149d8dc0fcd5112c9`，基线源文件 blob SHA-1 为 `d68f1593009fe22df8e1d3d70af661fe44f8dbf3`。新驱动以独立 compatible 和 Kconfig 启用，不修改 ZMK 共享驱动、binding 或 board；过滤器的歧义掩码只在驱动帧处理期间使用，上层继续接收标准 `kscan_callback_t` 事件。

**技术栈：** C11、Zephyr devicetree/Kconfig/CMake、ZMK debounce、ZMK `kscan` API、GitHub Actions、`zmkfirmware/zmk-build-arm:4.1`、nRF52840DK 编译夹具、Zephyr Twister `native_sim`。

---

## 文件结构与边界

本阶段创建或修改的生产文件：

```text
zmk-keyboard-k380/
  zephyr/module.yml
  Kconfig
  CMakeLists.txt
  dts/bindings/kscan/
    k380,kscan-no-diode-matrix.yaml
  drivers/kscan/
    CMakeLists.txt
    Kconfig
    kscan_k380_no_diode_matrix.c
```

新增的 GitHub Actions workflow：

```text
.github/workflows/k380-ci.yml
```

新增的编译夹具：

```text
zmk-keyboard-k380/tests/driver-build/
  k380-driver.conf
  k380-driver.overlay
```

已有且必须继续通过的单元测试：

```text
zmk-keyboard-k380/tests/ghost-filter/
```

不得修改：

```text
app/module/drivers/kscan/
app/module/dts/bindings/kscan/
app/boards/
```

本阶段不创建 K380 board、matrix transform、默认 keymap、UF2 分区或物理按键坐标表。

### Task 1: 建立 GitHub Actions 验证入口和会失败的编译夹具

**文件：**
- 创建：`.github/workflows/k380-ci.yml`
- 创建：`zmk-keyboard-k380/tests/driver-build/k380-driver.conf`
- 创建：`zmk-keyboard-k380/tests/driver-build/k380-driver.overlay`
- 测试：`zmk-keyboard-k380/tests/driver-build/`

- [ ] **步骤 1：创建轮询模式配置片段**

写入 `zmk-keyboard-k380/tests/driver-build/k380-driver.conf`：

```conf
CONFIG_ASSERT=y
CONFIG_ZMK_KSCAN_MATRIX_POLLING=y
```

轮询模式避免编译夹具要求 nRF52840DK 上所有输入 GPIO 都能安全注册中断；生产 K380 DTS 不强制此选项，仍沿用上游矩阵驱动的中断或轮询选择。

- [ ] **步骤 2：创建固定 8x15 的 nRF52840DK overlay**

写入 `zmk-keyboard-k380/tests/driver-build/k380-driver.overlay`：

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

/ {
    chosen {
        zmk,kscan = &k380_driver_build_kscan;
    };

    k380_driver_build_kscan: k380_driver_build_kscan {
        compatible = "k380,kscan-no-diode-matrix";
        row-gpios =
            <&gpio0 0 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 1 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 2 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 3 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 4 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 5 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 6 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>,
            <&gpio0 7 (GPIO_ACTIVE_HIGH | GPIO_OPEN_SOURCE)>;
        col-gpios =
            <&gpio0 8 GPIO_PULL_DOWN>,
            <&gpio0 9 GPIO_PULL_DOWN>,
            <&gpio0 10 GPIO_PULL_DOWN>,
            <&gpio0 11 GPIO_PULL_DOWN>,
            <&gpio0 12 GPIO_PULL_DOWN>,
            <&gpio0 13 GPIO_PULL_DOWN>,
            <&gpio0 14 GPIO_PULL_DOWN>,
            <&gpio0 15 GPIO_PULL_DOWN>,
            <&gpio0 16 GPIO_PULL_DOWN>,
            <&gpio0 17 GPIO_PULL_DOWN>,
            <&gpio0 18 GPIO_PULL_DOWN>,
            <&gpio0 19 GPIO_PULL_DOWN>,
            <&gpio0 20 GPIO_PULL_DOWN>,
            <&gpio0 21 GPIO_PULL_DOWN>,
            <&gpio0 22 GPIO_PULL_DOWN>;
        debounce-press-ms = <1>;
        debounce-release-ms = <1>;
        debounce-scan-period-ms = <1>;
        poll-period-ms = <10>;
    };
};
```

这里的 GPIO 只用于编译，不代表 K380 pinmap，也不能复制到后续 K380 board DTS。

- [ ] **步骤 3：创建 K380 专用 GitHub Actions workflow**

写入 `.github/workflows/k380-ci.yml`：

```yaml
name: K380 CI

on:
  push:
    paths:
      - ".github/workflows/k380-ci.yml"
      - "zmk-keyboard-k380/**"
      - "docs/superpowers/specs/2026-08-14-k380-pinmap.md"
      - "docs/superpowers/specs/2026-08-17-k380-no-diode-matrix-driver-design.md"
      - "docs/superpowers/specs/2026-08-17-k380-github-actions-validation-design.md"
      - "docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md"
  pull_request:
    paths:
      - ".github/workflows/k380-ci.yml"
      - "zmk-keyboard-k380/**"
      - "docs/superpowers/specs/2026-08-14-k380-pinmap.md"
      - "docs/superpowers/specs/2026-08-17-k380-no-diode-matrix-driver-design.md"
      - "docs/superpowers/specs/2026-08-17-k380-github-actions-validation-design.md"
      - "docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md"
  workflow_dispatch:

permissions:
  contents: read

jobs:
  ghost-filter:
    runs-on: ubuntu-latest
    container:
      image: docker.io/zmkfirmware/zmk-build-arm:4.1
    steps:
      - uses: actions/checkout@v7
      - uses: actions/cache@v6
        continue-on-error: true
        with:
          path: |
            modules/
            tools/
            zephyr/
            bootloader/
          key: ${{ runner.os }}-k380-${{ hashFiles('app/west.yml') }}
          restore-keys: |
            ${{ runner.os }}-k380-
      - run: west init -l app
      - run: west update --fetch-opt=--filter=tree:0
      - run: west zephyr-export
      - run: ZEPHYR_TOOLCHAIN_VARIANT=host west twister -T zmk-keyboard-k380/tests/ghost-filter -p native_sim
      - if: always()
        uses: actions/upload-artifact@v7
        with:
          name: k380-ghost-filter-logs
          path: |
            twister-out*/**/*.log
            twister-out*/**/zephyr/.config
          if-no-files-found: ignore

  driver-build:
    runs-on: ubuntu-latest
    container:
      image: docker.io/zmkfirmware/zmk-build-arm:4.1
    steps:
      - uses: actions/checkout@v7
      - uses: actions/cache@v6
        continue-on-error: true
        with:
          path: |
            modules/
            tools/
            zephyr/
            bootloader/
          key: ${{ runner.os }}-k380-${{ hashFiles('app/west.yml') }}
          restore-keys: |
            ${{ runner.os }}-k380-
      - run: west init -l app
      - run: west update --fetch-opt=--filter=tree:0
      - run: west zephyr-export
      - run: |
          west build -s app -d build/k380-driver -p always -b nrf52840dk/nrf52840 -- \
            -DZMK_EXTRA_MODULES="${GITHUB_WORKSPACE}/zmk-keyboard-k380" \
            -DEXTRA_CONF_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/tests/driver-build/k380-driver.conf" \
            -DEXTRA_DTC_OVERLAY_FILE="${GITHUB_WORKSPACE}/zmk-keyboard-k380/tests/driver-build/k380-driver.overlay"
      - if: always()
        uses: actions/upload-artifact@v7
        with:
          name: k380-driver-build-output
          path: |
            build/k380-driver/**/*.log
            build/k380-driver/**/zephyr/.config
            build/k380-driver/**/zephyr/zephyr.dts
          if-no-files-found: ignore

  module-isolation:
    runs-on: ubuntu-latest
    container:
      image: docker.io/zmkfirmware/zmk-build-arm:4.1
    steps:
      - uses: actions/checkout@v7
      - uses: actions/cache@v6
        continue-on-error: true
        with:
          path: |
            modules/
            tools/
            zephyr/
            bootloader/
          key: ${{ runner.os }}-k380-${{ hashFiles('app/west.yml') }}
          restore-keys: |
            ${{ runner.os }}-k380-
      - run: west init -l app
      - run: west update --fetch-opt=--filter=tree:0
      - run: west zephyr-export
      - working-directory: app
        run: |
          ZMK_EXTRA_MODULES="${GITHUB_WORKSPACE}/zmk-keyboard-k380" \
            ./run-test.sh tests/matrix-input/kp-press-release
          ! find build -name 'kscan_k380_no_diode_matrix.c.obj' -print -quit | grep -q .
      - if: always()
        uses: actions/upload-artifact@v7
        with:
          name: k380-module-isolation-logs
          path: |
            app/build/**/*.log
          if-no-files-found: ignore
```

- [ ] **步骤 4：提交并推送 workflow 与夹具，确认 CI 预期失败**

```bash
git add .github/workflows/k380-ci.yml zmk-keyboard-k380/tests/driver-build
git commit -m "test(k380): 添加 GitHub 驱动编译夹具"
git push --set-upstream origin feat/k380-no-diode-matrix-driver
```

预期：`K380 CI / ghost-filter` 和 `K380 CI / module-isolation` 通过；`K380 CI /
driver-build` 失败，原因是 `zmk-keyboard-k380` 尚未拥有 `zephyr/module.yml` 和
devicetree binding。通过 GitHub Actions 页面检查失败日志，不要求本地安装构建
工具。

### Task 2: 注册独立 module、Kconfig 和 devicetree binding

**文件：**
- 创建：`zmk-keyboard-k380/zephyr/module.yml`
- 创建：`zmk-keyboard-k380/Kconfig`
- 创建：`zmk-keyboard-k380/CMakeLists.txt`
- 创建：`zmk-keyboard-k380/dts/bindings/kscan/k380,kscan-no-diode-matrix.yaml`
- 创建：`zmk-keyboard-k380/drivers/kscan/Kconfig`
- 创建：`zmk-keyboard-k380/drivers/kscan/CMakeLists.txt`
- 测试：`zmk-keyboard-k380/tests/driver-build/`

- [ ] **步骤 1：编写 module 元数据和根构建入口**

写入 `zmk-keyboard-k380/zephyr/module.yml`：

```yaml
build:
  cmake: .
  kconfig: Kconfig
  settings:
    dts_root: .
```

写入 `zmk-keyboard-k380/Kconfig`：

```kconfig
menu "K380 keyboard module"

rsource "drivers/kscan/Kconfig"

endmenu
```

写入 `zmk-keyboard-k380/CMakeLists.txt`：

```cmake
zephyr_include_directories(include)
add_subdirectory(drivers/kscan)
```

- [ ] **步骤 2：添加仅在 K380 compatible 存在时启用的 Kconfig**

写入 `zmk-keyboard-k380/drivers/kscan/Kconfig`：

```kconfig
DT_COMPAT_K380_KSCAN_NO_DIODE_MATRIX := k380,kscan-no-diode-matrix

if KSCAN

config K380_KSCAN_NO_DIODE_MATRIX
    bool
    default $(dt_compat_enabled,$(DT_COMPAT_K380_KSCAN_NO_DIODE_MATRIX))
    select ZMK_KSCAN_GPIO_DRIVER

endif # KSCAN
```

`ZMK_KSCAN_GPIO_DRIVER` 只在 K380 compatible 被启用时被选择。这样复用 ZMK 全局
debounce 配置和轮询开关，但不改变其共享源码或任何已有 board 的配置。

- [ ] **步骤 3：添加 binding 和驱动 CMake 入口**

写入 `zmk-keyboard-k380/dts/bindings/kscan/k380,kscan-no-diode-matrix.yaml`：

```yaml
description: K380 固定 8x15 row2col 无二极管 GPIO 键盘矩阵控制器

compatible: "k380,kscan-no-diode-matrix"

include: kscan.yaml

properties:
  row-gpios:
    type: phandle-array
    required: true
    description: 8 个高电平有效、开源输出的行 GPIO。
  col-gpios:
    type: phandle-array
    required: true
    description: 15 个高电平有效、下拉输入的列 GPIO。
  debounce-press-ms:
    type: int
    default: 5
  debounce-release-ms:
    type: int
    default: 5
  debounce-scan-period-ms:
    type: int
    default: 1
  poll-period-ms:
    type: int
    default: 10
```

写入 `zmk-keyboard-k380/drivers/kscan/CMakeLists.txt`：

```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_K380_KSCAN_NO_DIODE_MATRIX
  kscan_k380_no_diode_matrix.c
  ../../src/ghost_filter.c
)
```

- [ ] **步骤 4：推送 module 注册并确认 GitHub CI 失败原因前移**

```bash
git add zmk-keyboard-k380/zephyr/module.yml zmk-keyboard-k380/Kconfig \
  zmk-keyboard-k380/CMakeLists.txt \
  zmk-keyboard-k380/dts/bindings/kscan/k380,kscan-no-diode-matrix.yaml \
  zmk-keyboard-k380/drivers/kscan/Kconfig \
  zmk-keyboard-k380/drivers/kscan/CMakeLists.txt
git commit -m "feat(k380): 注册专用矩阵扫描模块"
git push
```

预期：`K380 CI / driver-build` 已识别 binding，失败原因变为
`kscan_k380_no_diode_matrix.c` 不存在。若仍显示 binding 未找到，检查 workflow
日志中的 `ZMK_EXTRA_MODULES` 是否等于 `${GITHUB_WORKSPACE}/zmk-keyboard-k380`，
以及 `module.yml` 中的 `dts_root`。

- [ ] **步骤 5：确认本任务不产生第二个提交**

步骤 4 已包含 module 注册的提交和推送。本步骤只确认 GitHub Actions 日志与预期
失败原因一致，不额外创建提交。

### Task 3: 受控派生矩阵驱动并在 debounce 前过滤完整扫描帧

**文件：**
- 创建：`zmk-keyboard-k380/drivers/kscan/kscan_k380_no_diode_matrix.c`
- 依赖：`zmk-keyboard-k380/src/ghost_filter.c`
- 依赖：`zmk-keyboard-k380/include/zmk_keyboard_k380/ghost_filter.h`
- 测试：`zmk-keyboard-k380/tests/driver-build/`

- [ ] **步骤 1：从锁定上游基线复制驱动并记录来源**

确认基线提交和当前源文件 blob 哈希没有变化：

```bash
git log -1 --format='%H' -- zmk/app/module/drivers/kscan/kscan_gpio_matrix.c
git hash-object zmk/app/module/drivers/kscan/kscan_gpio_matrix.c
```

预期：第一行输出 `6941abc2afab16502cff9c5149d8dc0fcd5112c9`，第二行输出
`d68f1593009fe22df8e1d3d70af661fe44f8dbf3`。随后复制：

```bash
cp zmk/app/module/drivers/kscan/kscan_gpio_matrix.c \
  zmk/zmk-keyboard-k380/drivers/kscan/kscan_k380_no_diode_matrix.c
```

在新文件 SPDX 头之后加入：

```c
/*
 * Controlled derivative of:
 * app/module/drivers/kscan/kscan_gpio_matrix.c
 * Baseline commit: 6941abc2afab16502cff9c5149d8dc0fcd5112c9
 * Baseline source blob SHA-1: d68f1593009fe22df8e1d3d70af661fe44f8dbf3
 *
 * K380-specific change: collect a complete 8x15 row2col scan frame and
 * filter no-diode rectangular ambiguity before debounce.
 */
```

- [ ] **步骤 2：把驱动身份改为 K380 固定 row2col 实现**

在新文件中完成以下确定性替换：

```text
DT_DRV_COMPAT:
  zmk_kscan_gpio_matrix -> k380_kscan_no_diode_matrix

所有 kscan_matrix 前缀:
  -> k380_kscan

所有 KSCAN_MATRIX 宏前缀:
  -> K380_KSCAN
```

删除以下上游通用方向代码，因为 K380 binding 不允许 `col2row`：

```c
#define INST_DIODE_DIR(...)
#define COND_DIODE_DIR(...)
#define INST_INPUTS_LEN(...)
enum kscan_diode_direction { ... };
static int state_index_io(...);
struct kscan_matrix_config 中的 diode_direction 字段;
KSCAN_MATRIX_INIT 中的 COND_DIODE_DIR(...) 和 .diode_direction = ...;
```

加入固定矩阵常量、K380 公开过滤器和固定索引函数：

```c
#include <zmk_keyboard_k380/ghost_filter.h>

#define K380_KSCAN_ROWS K380_GHOST_FILTER_ROWS
#define K380_KSCAN_COLS K380_GHOST_FILTER_COLS
#define K380_KSCAN_MATRIX_LEN (K380_KSCAN_ROWS * K380_KSCAN_COLS)

static int state_index_rc(const int row, const int col) {
    __ASSERT(row < K380_KSCAN_ROWS, "Invalid row %i", row);
    __ASSERT(col < K380_KSCAN_COLS, "Invalid column %i", col);
    return (col * K380_KSCAN_ROWS) + row;
}
```

驱动不得包含 `diode-direction` 属性或支持 `col2row` 分支。

- [ ] **步骤 3：把端口批量读取辅助逻辑放入 K380 驱动**

不得包含 ZMK 私有的 `"kscan_gpio.h"`，也不得引用
`app/module/drivers/kscan/kscan_gpio.c`。在新驱动内定义下列私有类型和函数，
函数体与上游 `kscan_gpio.c` 等价：

```c
struct k380_kscan_gpio {
    struct gpio_dt_spec spec;
    size_t index;
};

struct k380_kscan_gpio_list {
    struct k380_kscan_gpio *gpios;
    size_t len;
};

struct k380_kscan_gpio_port_state {
    const struct device *port;
    gpio_port_value_t value;
};

#define K380_KSCAN_GPIO_GET_BY_IDX(node_id, prop, idx) \
    ((struct k380_kscan_gpio){ \
        .spec = GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx), .index = idx})

#define K380_KSCAN_GPIO_LIST(gpio_array) \
    ((struct k380_kscan_gpio_list){.gpios = gpio_array, .len = ARRAY_SIZE(gpio_array)})

static int k380_kscan_compare_ports(const void *left, const void *right) {
    const struct k380_kscan_gpio *left_gpio = left;
    const struct k380_kscan_gpio *right_gpio = right;

    return left_gpio->spec.port - right_gpio->spec.port;
}

static void k380_kscan_sort_inputs(struct k380_kscan_gpio_list *inputs) {
    qsort(inputs->gpios, inputs->len, sizeof(inputs->gpios[0]), k380_kscan_compare_ports);
}

static int k380_kscan_pin_get(const struct k380_kscan_gpio *gpio,
                               struct k380_kscan_gpio_port_state *state) {
    if (gpio->spec.port != state->port) {
        state->port = gpio->spec.port;
        const int err = gpio_port_get(state->port, &state->value);

        if (err) {
            return err;
        }
    }

    return (state->value & BIT(gpio->spec.pin)) != 0;
}
```

加入 `#include <stdlib.h>`，并把原上游 `struct kscan_gpio`、
`struct kscan_gpio_list` 和 `struct kscan_gpio_port_state` 的所有引用改为上面的
K380 私有类型。

- [ ] **步骤 4：重写完整帧读取和 debounce 更新顺序**

在 `k380_kscan_read()` 中，保留行激活、可选等待、列读取、行失活、错误处理、
后续事件上报和继续扫描决策。将每次列读取时的 `zmk_debounce_update()` 删除，
改为先填充 `raw`，扫描完八行后再执行下面的完整帧处理：

```c
uint16_t raw[K380_KSCAN_ROWS] = {0};
uint16_t accepted[K380_KSCAN_ROWS] = {0};
uint16_t filtered[K380_KSCAN_ROWS];
uint16_t ambiguous[K380_KSCAN_ROWS];

for (int row = 0; row < K380_KSCAN_ROWS; row++) {
    for (int col = 0; col < K380_KSCAN_COLS; col++) {
        const struct zmk_debounce_state *state =
            &data->matrix_state[state_index_rc(row, col)];

        if (zmk_debounce_is_pressed(state)) {
            accepted[row] |= BIT(col);
        }
    }
}

k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

for (int row = 0; row < K380_KSCAN_ROWS; row++) {
    for (int col = 0; col < K380_KSCAN_COLS; col++) {
        const bool active = (filtered[row] & BIT(col)) != 0U;

        zmk_debounce_update(&data->matrix_state[state_index_rc(row, col)], active,
                            config->debounce_scan_period_ms, &config->debounce_config);
    }
}
```

原始矩阵采集循环中，使用未排序前的 GPIO `index` 保存逻辑坐标：

```c
const int active = k380_kscan_pin_get(in_gpio, &port_state);
if (active < 0) {
    LOG_ERR("Failed to read port %s: %i", in_gpio->spec.port->name, active);
    return active;
}

if (active) {
    raw[out_gpio->index] |= BIT(in_gpio->index);
}
```

`ambiguous` 只作为 `k380_ghost_filter_apply()` 的输出缓冲区；禁止写日志上报、
回调上报、全局变量或新的 public API。其余 `continue_scan` 判断必须基于
debounce state，保证已按下键、按下去抖和释放去抖继续快速扫描。

- [ ] **步骤 5：在实例宏中强制 8x15，且只生成 row2col GPIO 数组**

将实例宏改为 `K380_KSCAN_INIT(n)`，在开头加入：

```c
BUILD_ASSERT(DT_INST_PROP_LEN(n, row_gpios) == K380_KSCAN_ROWS,
             "K380 kscan requires exactly 8 row-gpios");
BUILD_ASSERT(DT_INST_PROP_LEN(n, col_gpios) == K380_KSCAN_COLS,
             "K380 kscan requires exactly 15 col-gpios");
```

数组和配置必须固定如下，不保留方向条件宏：

```c
static struct k380_kscan_gpio k380_kscan_rows_##n[] = {
    LISTIFY(K380_KSCAN_ROWS, K380_KSCAN_ROW_CFG_INIT, (, ), n)};
static struct k380_kscan_gpio k380_kscan_cols_##n[] = {
    LISTIFY(K380_KSCAN_COLS, K380_KSCAN_COL_CFG_INIT, (, ), n)};
static struct zmk_debounce_state k380_kscan_state_##n[K380_KSCAN_MATRIX_LEN];

static struct k380_kscan_data k380_kscan_data_##n = {
    .inputs = K380_KSCAN_GPIO_LIST(k380_kscan_cols_##n),
    .matrix_state = k380_kscan_state_##n,
    COND_INTERRUPTS((.irqs = k380_kscan_irqs_##n, ))};

static const struct k380_kscan_config k380_kscan_config_##n = {
    .rows = K380_KSCAN_ROWS,
    .cols = K380_KSCAN_COLS,
    .outputs = K380_KSCAN_GPIO_LIST(k380_kscan_rows_##n),
    .debounce_config = {
        .debounce_press_ms = INST_DEBOUNCE_PRESS_MS(n),
        .debounce_release_ms = INST_DEBOUNCE_RELEASE_MS(n),
    },
    .debounce_scan_period_ms = DT_INST_PROP(n, debounce_scan_period_ms),
    .poll_period_ms = DT_INST_PROP(n, poll_period_ms),
};
```

保留上游 `PM_DEVICE_DT_INST_DEFINE()`、`DEVICE_DT_INST_DEFINE()`、输入中断、
GPIO 就绪检查、GPIO 配置、工作队列和 PM suspend/resume 路径；只将其符号前缀
替换为 `k380_kscan_`。

- [ ] **步骤 6：在 workflow 中加入驱动编译后的精确断言**

在 `.github/workflows/k380-ci.yml` 的 `driver-build` job 中，紧接 `west build`
步骤后加入：

```yaml
      - run: |
          grep -q '^CONFIG_K380_KSCAN_NO_DIODE_MATRIX=y$' \
            build/k380-driver/zephyr/.config
          find build/k380-driver -name 'kscan_k380_no_diode_matrix.c.obj' \
            -print -quit | grep -q .
```

- [ ] **步骤 7：提交驱动和 CI 断言并确认 K380 CI 全部通过**

```bash
git add zmk-keyboard-k380/drivers/kscan/kscan_k380_no_diode_matrix.c \
  .github/workflows/k380-ci.yml
git commit -m "feat(k380): 添加无二极管矩阵扫描驱动"
git push
```

预期：`K380 CI` 的三个 job 全部通过。

### Task 4: 回归、隔离验证与阶段记录

**文件：**
- 修改：`docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md`
- 测试：`zmk-keyboard-k380/tests/ghost-filter/`
- 测试：`zmk-keyboard-k380/tests/driver-build/`

- [ ] **步骤 1：确认最新 GitHub Actions 运行结果为全绿**

在 GitHub 的 Actions 页面打开本分支最新一次 `K380 CI` 运行。预期：

```text
ghost-filter: success
driver-build: success
module-isolation: success
```

下载并保留三个 job 上传的日志 artifact，作为本阶段的 CI 验证证据。

- [ ] **步骤 2：验证共享 ZMK 源码完全未变**

在 `zmk` 工作树执行：

```bash
git diff --exit-code main...HEAD -- \
  app/module/drivers/kscan \
  app/module/dts/bindings/kscan \
  app/boards
```

预期：退出码为 `0`。

- [ ] **步骤 3：确认隔离断言已由 GitHub Actions 执行**

检查 `module-isolation` job 日志。预期：

```text
tests/matrix-input/kp-press-release: PASS
```

并且执行 job 内的 `find` 断言成功，证明加载 module 但未实例化 compatible 时，
K380 驱动对象不存在。

- [ ] **步骤 4：标记计划完成并提交阶段记录**

将本计划四个 Task 的复选框更新为 `[x]`，在文档末尾追加：

```markdown
## 计划自检

- 规格覆盖：module 注册、独立 binding、固定 8x15 row2col 扫描、完整帧过滤、
  debounce 前接入、标准 kscan 回调、构建验证和隔离验证均有对应任务。
- 范围控制：未创建 K380 board、matrix transform、keymap、UF2 配置或物理按键表。
- 共享隔离：未修改 ZMK 树内共享 kscan、binding 和 board。
- 后续门禁：开始 board 集成前仍需完整物理按键行列坐标表与确切 Bootloader 应用
  分区起始地址。
```

然后提交：

```bash
git add docs/superpowers/plans/2026-08-17-k380-no-diode-matrix-driver.md
git commit -m "docs(k380): 标记专用矩阵驱动阶段完成"
```
