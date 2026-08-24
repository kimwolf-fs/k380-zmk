# K380 无二极管矩阵过滤器第一阶段实施计划

> **面向 agent 执行者：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐任务执行。本计划使用复选框跟踪进度。

**目标：** 在独立的 `zmk-keyboard-k380` module 中实现并测试策略 2 的纯矩阵歧义过滤器，不修改 ZMK 树内代码。

**架构：** 过滤器接收一个 8 行 x 15 列的原始矩阵和此前已确认按下的矩阵，输出过滤后的输入矩阵及歧义掩码。它仅负责矩形歧义判定；GPIO 扫描、唤醒、中断和 debounce 集成留给第二阶段专用 kscan driver。

**技术栈：** C11、Zephyr ztest、`native_sim`、ZMK west 工作区。

---

## 范围与边界

本计划只创建下列独立 module 文件：

```text
zmk-keyboard-k380/
  include/zmk_keyboard_k380/ghost_filter.h
  src/ghost_filter.c
  tests/ghost-filter/CMakeLists.txt
  tests/ghost-filter/prj.conf
  tests/ghost-filter/testcase.yaml
  tests/ghost-filter/src/main.c
```

不修改下列文件或目录：

```text
zmk/app/module/drivers/kscan/kscan_gpio_matrix.c
zmk/app/module/drivers/kscan/Kconfig
zmk/app/module/drivers/kscan/CMakeLists.txt
zmk/app/module/dts/bindings/kscan/zmk,kscan-gpio-matrix.yaml
zmk/app/boards/
```

派生 driver 的上游基线文件哈希为
`d68f1593009fe22df8e1d3d70af661fe44f8dbf3`。第二阶段从该文件派生，第一阶段不复制该驱动。

### Task 1: 编写过滤器失败测试

**文件：**

- 创建：`zmk-keyboard-k380/tests/ghost-filter/CMakeLists.txt`
- 创建：`zmk-keyboard-k380/tests/ghost-filter/prj.conf`
- 创建：`zmk-keyboard-k380/tests/ghost-filter/testcase.yaml`
- 创建：`zmk-keyboard-k380/tests/ghost-filter/src/main.c`
- 测试：`zmk-keyboard-k380/tests/ghost-filter/src/main.c`

- [x] **步骤 1：创建 Zephyr 测试构建文件**

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(k380_ghost_filter)

target_sources(app PRIVATE src/main.c ../../src/ghost_filter.c)
target_include_directories(app PRIVATE ../../include)
```

```conf
CONFIG_ZTEST=y
```

```yaml
tests:
  k380.ghost_filter:
    platform_allow:
      - native_sim
    tags:
      - k380
      - unit
```

- [x] **步骤 2：编写失败的行为测试**

```c
#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>

#include <zmk_keyboard_k380/ghost_filter.h>

static void assert_rows_equal(const uint16_t actual[K380_GHOST_FILTER_ROWS],
                              const uint16_t expected[K380_GHOST_FILTER_ROWS]) {
    for (size_t row = 0; row < K380_GHOST_FILTER_ROWS; row++) {
        zassert_equal(actual[row], expected[row], "row %u", (unsigned int)row);
    }
}

ZTEST(k380_ghost_filter, test_non_rectangular_presses_are_unchanged) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(3), BIT(1), 0, BIT(7), 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {0};
    const uint16_t expected[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(3), BIT(1), 0, BIT(7), 0, 0, 0, 0,
    };
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    assert_rows_equal(filtered, expected);
    zassert_equal(ambiguous[0], 0, "unexpected ambiguity");
    zassert_equal(ambiguous[1], 0, "unexpected ambiguity");
    zassert_equal(ambiguous[3], 0, "unexpected ambiguity");
}

ZTEST(k380_ghost_filter, test_new_rectangle_corners_are_withheld) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0, 0,
    };
    const uint16_t expected[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0, 0,
    };
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    assert_rows_equal(filtered, expected);
    zassert_equal(ambiguous[0], BIT(0) | BIT(1), "row 0 ambiguity");
    zassert_equal(ambiguous[1], BIT(0) | BIT(1), "row 1 ambiguity");
}

ZTEST(k380_ghost_filter, test_release_is_not_blocked_by_ambiguity) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(1), BIT(0) | BIT(1), 0, 0, 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {
        BIT(0) | BIT(1), BIT(0), 0, 0, 0, 0, 0, 0,
    };
    const uint16_t expected[K380_GHOST_FILTER_ROWS] = {
        BIT(1), BIT(1), 0, 0, 0, 0, 0, 0,
    };
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    assert_rows_equal(filtered, expected);
}

ZTEST(k380_ghost_filter, test_bits_outside_the_15_column_matrix_are_ignored) {
    const uint16_t raw[K380_GHOST_FILTER_ROWS] = {
        BIT(15), 0, 0, 0, 0, 0, 0, 0,
    };
    const uint16_t accepted[K380_GHOST_FILTER_ROWS] = {0};
    uint16_t filtered[K380_GHOST_FILTER_ROWS];
    uint16_t ambiguous[K380_GHOST_FILTER_ROWS];

    k380_ghost_filter_apply(raw, accepted, filtered, ambiguous);

    zassert_equal(filtered[0], 0, "bit 15 must not enter the matrix");
    zassert_equal(ambiguous[0], 0, "bit 15 must not create ambiguity");
}
ZTEST_SUITE(k380_ghost_filter, NULL, NULL, NULL, NULL, NULL);
```

- [x] **步骤 3：运行测试并确认失败**

从 West 工作区根目录运行。当前 WSL 环境使用 host 工具链：

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host west twister -T zmk-keyboard-k380/tests/ghost-filter -p native_sim
```

预期：失败，原因是 `zmk_keyboard_k380/ghost_filter.h` 和 `ghost_filter.c` 尚不存在。

- [x] **步骤 4：提交测试骨架**

```bash
git add zmk-keyboard-k380/tests/ghost-filter
git commit -m "test(k380): 添加矩形歧义过滤器测试"
```

### Task 2: 实现纯矩阵过滤器

**文件：**

- 创建：`zmk-keyboard-k380/include/zmk_keyboard_k380/ghost_filter.h`
- 创建：`zmk-keyboard-k380/src/ghost_filter.c`
- 测试：`zmk-keyboard-k380/tests/ghost-filter/src/main.c`

- [x] **步骤 1：创建公开接口**

```c
#ifndef ZMK_KEYBOARD_K380_GHOST_FILTER_H_
#define ZMK_KEYBOARD_K380_GHOST_FILTER_H_

#include <stdint.h>

#define K380_GHOST_FILTER_ROWS 8U
#define K380_GHOST_FILTER_COLS 15U
#define K380_GHOST_FILTER_COL_MASK ((uint16_t)((1U << K380_GHOST_FILTER_COLS) - 1U))

void k380_ghost_filter_apply(const uint16_t raw[K380_GHOST_FILTER_ROWS],
                             const uint16_t accepted[K380_GHOST_FILTER_ROWS],
                             uint16_t filtered[K380_GHOST_FILTER_ROWS],
                             uint16_t ambiguous[K380_GHOST_FILTER_ROWS]);

#endif
```

- [x] **步骤 2：实现矩形检测和新按键抑制**

```c
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zmk_keyboard_k380/ghost_filter.h>

static bool has_at_least_two_bits(uint16_t value) {
    return (value & (uint16_t)(value - 1U)) != 0U;
}

void k380_ghost_filter_apply(const uint16_t raw[K380_GHOST_FILTER_ROWS],
                             const uint16_t accepted[K380_GHOST_FILTER_ROWS],
                             uint16_t filtered[K380_GHOST_FILTER_ROWS],
                             uint16_t ambiguous[K380_GHOST_FILTER_ROWS]) {
    memset(ambiguous, 0, sizeof(uint16_t) * K380_GHOST_FILTER_ROWS);

    for (size_t first_row = 0; first_row < K380_GHOST_FILTER_ROWS; first_row++) {
        const uint16_t first_active = raw[first_row] & K380_GHOST_FILTER_COL_MASK;

        for (size_t second_row = first_row + 1U; second_row < K380_GHOST_FILTER_ROWS;
             second_row++) {
            const uint16_t common_active =
                first_active & raw[second_row] & K380_GHOST_FILTER_COL_MASK;

            if (has_at_least_two_bits(common_active)) {
                ambiguous[first_row] |= common_active;
                ambiguous[second_row] |= common_active;
            }
        }
    }

    for (size_t row = 0; row < K380_GHOST_FILTER_ROWS; row++) {
        const uint16_t active = raw[row] & K380_GHOST_FILTER_COL_MASK;
        const uint16_t held = accepted[row] & active;
        const uint16_t new_unambiguous = active & (uint16_t)~accepted[row] &
                                         (uint16_t)~ambiguous[row];

        filtered[row] = (held | new_unambiguous) & K380_GHOST_FILTER_COL_MASK;
    }
}
```

- [x] **步骤 3：运行测试并确认通过**

从 West 工作区根目录运行。当前 WSL 环境使用 host 工具链：

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host west twister -T zmk-keyboard-k380/tests/ghost-filter -p native_sim
```

预期：`k380.ghost_filter` 通过，4 个测试均成功。

- [x] **步骤 4：提交过滤器实现**

```bash
git add include/zmk_keyboard_k380/ghost_filter.h src/ghost_filter.c
git commit -m "feat(k380): 添加矩形歧义过滤器"
```

### Task 3: 验证第一阶段隔离性

**文件：**

- 修改：无
- 测试：`zmk-keyboard-k380/tests/ghost-filter/`

- [x] **步骤 1：确认 ZMK 树内通用驱动未发生改动**

运行：

```bash
git diff --exit-code main...HEAD -- app/module/drivers/kscan/kscan_gpio_matrix.c app/module/drivers/kscan/Kconfig app/module/drivers/kscan/CMakeLists.txt app/module/dts/bindings/kscan/zmk,kscan-gpio-matrix.yaml
```

预期：退出码为 `0`。

- [x] **步骤 2：运行完整的过滤器测试目录**

从 West 工作区根目录运行。当前 WSL 环境使用 host 工具链：

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host west twister -T zmk-keyboard-k380/tests/ghost-filter -p native_sim
```

预期：所有 `k380.ghost_filter` 测试通过。

- [x] **步骤 3：记录阶段边界**

第一阶段交付物只包含纯过滤器和测试。不得创建 kscan driver、DTS binding、board DTS、UF2 配置或默认 keymap；这些内容属于第二阶段或 board 集成阶段。

- [x] **步骤 4：确认验证任务不产生额外提交**

运行：

```bash
git -C E:/project/k380-keyboard/zmk status --short
```

预期：仅显示用户已有的无关变更；本验证任务不修改 ZMK 树内文件，因此不创建提交。

## 计划自检

- 规格覆盖：本计划覆盖策略 2 的原始帧输入、矩形识别、已确认按键保持、新按键抑制、释放传播、列边界和 `native_sim` 回归测试。
- 范围控制：本计划不修改 ZMK 树内通用 kscan 文件，不创建 board 定义，也不要求未提供的物理键位坐标或 Bootloader 分区地址。
- 名称一致性：所有公开符号以 `K380_GHOST_FILTER_` 或 `k380_ghost_filter_` 开头；矩阵维度固定为 8 行、15 列。
- 后续门禁：第二阶段专用 kscan driver 必须调用 `k380_ghost_filter_apply()`，第三阶段 board 集成必须先获得完整物理键位表和精确应用分区起始地址。
