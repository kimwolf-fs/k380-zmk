#include <zephyr/ztest.h>

/* Compile the unchanged production owner here so a test utility can observe its
 * private delayed-work state without exposing test hooks in the production API. */
#include "../../../../app/src/behaviors/behavior_hold_tap.c"

void shutdown_test_wait_timer_running(uint32_t position) {
    struct active_hold_tap *hold_tap = find_hold_tap(position);
    zassert_not_null(hold_tap);
    for (int i = 0; i < 200; i++) {
        if (k_work_busy_get(&hold_tap->work.work) & K_WORK_RUNNING) {
            return;
        }
        k_msleep(5);
    }
    zassert_unreachable("Hold-tap timer never became running while dispatch was locked");
}
