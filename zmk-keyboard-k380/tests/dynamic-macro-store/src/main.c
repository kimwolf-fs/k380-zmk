#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <zmk_keyboard_k380/dynamic_macro_record.h>
#include <zmk_keyboard_k380/dynamic_macro_store.h>

#include "fake_settings.h"

static const uint8_t canonical_package[] = {
    0x4b, 0x56, 0x4d, 0x31, 0x01, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x61, 0x55, 0x24, 0x9c,
    0x01, 0x04, 0x00, 0x10, 0xc0, 0x27, 0x09, 0x00,
    0x02, 0x04, 0x00, 0x00,
};

static void make_record(struct k380_dynamic_macro_record *record)
{
    memset(record, 0, sizeof(*record));
    record->record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    record->trigger = K380_DYNAMIC_MACRO_TRIGGER_COUNT;
    record->name_len = 4U;
    memcpy(record->name, "test", 4U);
    record->repeat_count = 3U;
    record->package_len = sizeof(canonical_package);
    record->package_crc32 = 0x9c245561U;
    memcpy(record->package, canonical_package, sizeof(canonical_package));
}

static void make_empty_record(struct k380_dynamic_macro_record *record)
{
    memset(record, 0, sizeof(*record));
    record->record_version = K380_DYNAMIC_MACRO_RECORD_VERSION;
    record->trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE;
    record->repeat_count = 1U;
}

static void put_record(const char *key,
                       const struct k380_dynamic_macro_record *record)
{
    uint8_t wire[K380_DYNAMIC_MACRO_RECORD_MAX_BYTES];
    size_t wire_len = 0U;

    zassert_ok(k380_dynamic_macro_record_encode(record, wire, sizeof(wire),
                                                &wire_len));
    k380_dynamic_macro_test_settings_put(key, wire, wire_len);
}

static void make_legacy(struct k380_dynamic_macro *legacy)
{
    memset(legacy, 0, sizeof(*legacy));
    legacy->trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE;
    legacy->name[0] = 'v';
    legacy->name[1] = '1';
    legacy->count = 1U;
    legacy->step_count = 3U;
    legacy->steps[0].type = K380_DYNAMIC_MACRO_PRESS_KEY;
    legacy->steps[0].value.key_usage = 4U;
    legacy->steps[1].type = K380_DYNAMIC_MACRO_WAIT_MS;
    legacy->steps[1].value.wait_ms = 60000U;
    legacy->steps[2].type = K380_DYNAMIC_MACRO_RELEASE_KEY;
    legacy->steps[2].value.key_usage = 4U;
}

ZTEST(dynamic_macro_store, test_record_wire_encoding_is_explicit_and_exact)
{
    struct k380_dynamic_macro_record record;
    uint8_t wire[K380_DYNAMIC_MACRO_RECORD_MAX_BYTES];
    size_t wire_len = 0U;

    make_record(&record);
    zassert_ok(k380_dynamic_macro_record_encode(&record, wire, sizeof(wire),
                                                &wire_len));
    zassert_equal(48U + sizeof(canonical_package), wire_len);
    zassert_equal(2U, wire[0]);
    zassert_equal(K380_DYNAMIC_MACRO_TRIGGER_COUNT, wire[1]);
    zassert_equal(4U, wire[2]);
    zassert_equal(3U, sys_get_le32(&wire[4]));
    zassert_equal(sizeof(canonical_package), sys_get_le16(&wire[8]));
    zassert_equal(0x9c245561U, sys_get_le32(&wire[12]));
    zassert_mem_equal(&wire[16], "test", 4U);
    zassert_mem_equal(&wire[48], canonical_package, sizeof(canonical_package));
}

ZTEST(dynamic_macro_store, test_record_round_trip_and_crc_validation)
{
    struct k380_dynamic_macro_record source;
    struct k380_dynamic_macro_record decoded;
    uint8_t wire[K380_DYNAMIC_MACRO_RECORD_MAX_BYTES];
    size_t wire_len = 0U;

    make_record(&source);
    zassert_ok(k380_dynamic_macro_record_encode(&source, wire, sizeof(wire),
                                                &wire_len));
    zassert_ok(k380_dynamic_macro_record_decode(&decoded, wire, wire_len));
    zassert_mem_equal(&source, &decoded, sizeof(source));

    wire[48] ^= 0x80U;
    zassert_not_equal(0, k380_dynamic_macro_record_decode(&decoded, wire,
                                                          wire_len));
}

ZTEST(dynamic_macro_store, test_legacy_flat_macro_converts_to_vm)
{
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record record;

    make_legacy(&legacy);

    zassert_ok(k380_dynamic_macro_record_from_legacy(&legacy, &record));
    zassert_equal(K380_DYNAMIC_MACRO_RECORD_VERSION, record.record_version);
    zassert_equal(K380_DYNAMIC_MACRO_TRIGGER_ONCE, record.trigger);
    zassert_true(record.package_len > 0U);
    zassert_ok(k380_dynamic_macro_record_validate(&record));
    zassert_equal(K380_MACRO_VM_OP_PRESS, record.package[16]);
    zassert_equal(0x60U, record.package[20]);
    zassert_equal(0xeaU, record.package[21]);
    zassert_equal(K380_MACRO_VM_OP_RELEASE, record.package[24]);
}

ZTEST(dynamic_macro_store, test_legacy_fixed_wait_keeps_60000_ms_ceiling)
{
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record record;

    make_legacy(&legacy);
    legacy.step_count = 1U;
    legacy.steps[0].type = K380_DYNAMIC_MACRO_WAIT_MS;
    legacy.steps[0].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS;
    zassert_ok(k380_dynamic_macro_record_from_legacy(&legacy, &record));

    legacy.steps[0].value.wait_ms = K380_DYNAMIC_WAIT_MAX_MS + 1U;
    zassert_equal(-EINVAL,
                  k380_dynamic_macro_record_from_legacy(&legacy, &record));
}

ZTEST(dynamic_macro_store, test_legacy_random_wait_keeps_60000_ms_ceiling)
{
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record record;

    make_legacy(&legacy);
    legacy.step_count = 1U;
    legacy.steps[0].type = K380_DYNAMIC_MACRO_WAIT_RANDOM;
    legacy.steps[0].value.wait_random.min_ms = K380_DYNAMIC_WAIT_MAX_MS;
    legacy.steps[0].value.wait_random.max_ms = K380_DYNAMIC_WAIT_MAX_MS;
    zassert_ok(k380_dynamic_macro_record_from_legacy(&legacy, &record));

    legacy.steps[0].value.wait_random.max_ms =
        K380_DYNAMIC_WAIT_MAX_MS + 1U;
    zassert_equal(-EINVAL,
                  k380_dynamic_macro_record_from_legacy(&legacy, &record));
}

ZTEST(dynamic_macro_store, test_empty_legacy_macro_converts_to_empty_record)
{
    struct k380_dynamic_macro legacy = {
        .trigger = K380_DYNAMIC_MACRO_TRIGGER_ONCE,
        .count = 1U,
    };
    struct k380_dynamic_macro_record record;

    zassert_ok(k380_dynamic_macro_record_from_legacy(&legacy, &record));
    zassert_equal(0U, record.package_len);
    zassert_equal(0U, record.package_crc32);
    zassert_ok(k380_dynamic_macro_record_validate(&record));
}

ZTEST(dynamic_macro_store, test_save_load_and_slots_are_independent)
{
    struct k380_dynamic_macro_record source;
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    make_record(&source);
    zassert_ok(k380_dynamic_macro_store_save(0U, 0U, &source));
    source.name[0] = 'b';
    zassert_ok(k380_dynamic_macro_store_save(0U, 1U, &source));

    memset(&loaded, 0, sizeof(loaded));
    zassert_ok(k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_equal('t', loaded.name[0]);
    zassert_ok(k380_dynamic_macro_store_load(0U, 1U, &loaded));
    zassert_equal('b', loaded.name[0]);
}

ZTEST(dynamic_macro_store, test_non_empty_save_attempts_canonical_cleanup)
{
    struct k380_dynamic_macro_record record;

    k380_dynamic_macro_test_settings_reset();
    make_record(&record);

    zassert_ok(k380_dynamic_macro_store_save(0U, 0U, &record));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_equal(3U, k380_dynamic_macro_test_settings_operation_count());
    zassert_equal(0, strcmp("save:k380/dynamic_config/p/0/vm/0",
                            k380_dynamic_macro_test_settings_operation(0U)));
    zassert_equal(0,
                  strcmp("delete:k380/dynamic_config/v2/p/0/m/0",
                         k380_dynamic_macro_test_settings_operation(1U)));
    zassert_equal(0, strcmp("delete:k380/dynamic_config/p/0/m/0",
                            k380_dynamic_macro_test_settings_operation(2U)));
}

ZTEST(dynamic_macro_store, test_empty_save_cleans_both_old_paths)
{
    struct k380_dynamic_macro_record empty;
    struct k380_dynamic_macro_record temporary;
    struct k380_dynamic_macro legacy;

    k380_dynamic_macro_test_settings_reset();
    make_empty_record(&empty);
    make_record(&temporary);
    make_legacy(&legacy);
    put_record("k380/dynamic_config/v2/p/0/m/0", &temporary);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));

    zassert_ok(k380_dynamic_macro_store_save(0U, 0U, &empty));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
}

ZTEST(dynamic_macro_store, test_failed_final_save_preserves_both_old_paths)
{
    struct k380_dynamic_macro_record record;
    struct k380_dynamic_macro legacy;

    k380_dynamic_macro_test_settings_reset();
    make_record(&record);
    make_legacy(&legacy);
    put_record("k380/dynamic_config/v2/p/0/m/0", &record);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));
    k380_dynamic_macro_test_settings_fail_next_save();

    zassert_equal(-EIO, k380_dynamic_macro_store_save(0U, 0U, &record));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_equal(1U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store,
      test_cleanup_failure_returns_first_error_and_retries_on_next_save)
{
    struct k380_dynamic_macro_record record;
    struct k380_dynamic_macro legacy;

    k380_dynamic_macro_test_settings_reset();
    make_record(&record);
    make_legacy(&legacy);
    put_record("k380/dynamic_config/v2/p/0/m/0", &record);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));
    k380_dynamic_macro_test_settings_fail_next_delete();

    zassert_equal(-EIO, k380_dynamic_macro_store_save(0U, 0U, &record));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_equal(3U, k380_dynamic_macro_test_settings_operation_count());

    zassert_ok(k380_dynamic_macro_store_save(0U, 0U, &record));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_equal(6U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store, test_loading_one_slot_ignores_other_slot_corruption)
{
    struct k380_dynamic_macro_record source;
    struct k380_dynamic_macro_record loaded;
    uint8_t corrupt[48] = {0};

    k380_dynamic_macro_test_settings_reset();
    make_record(&source);
    zassert_ok(k380_dynamic_macro_store_save(0U, 0U, &source));
    corrupt[0] = 0xffU;
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/v2/p/0/m/1", corrupt, sizeof(corrupt));

    memset(&loaded, 0, sizeof(loaded));
    zassert_ok(k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_equal('t', loaded.name[0]);
}

ZTEST(dynamic_macro_store, test_missing_slot_is_canonical_empty)
{
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    memset(&loaded, 0xff, sizeof(loaded));
    zassert_ok(k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_mem_equal(&loaded, &(struct k380_dynamic_macro_record){0},
                      sizeof(loaded));
}

ZTEST(dynamic_macro_store, test_final_record_wins_over_temporary_and_legacy)
{
    struct k380_dynamic_macro_record final_record;
    struct k380_dynamic_macro_record temporary_record;
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    make_record(&final_record);
    make_record(&temporary_record);
    temporary_record.name[0] = 'x';
    make_legacy(&legacy);
    put_record("k380/dynamic_config/p/0/vm/0", &final_record);
    put_record("k380/dynamic_config/v2/p/0/m/0", &temporary_record);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));

    zassert_ok(k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_equal('t', loaded.name[0]);
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_equal(0U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store,
      test_temporary_migration_saves_final_before_deleting_temporary)
{
    struct k380_dynamic_macro_record temporary_record;
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    make_record(&temporary_record);
    make_legacy(&legacy);
    legacy.name[0] = 'l';
    put_record("k380/dynamic_config/v2/p/0/m/0", &temporary_record);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));

    zassert_ok(k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_equal('t', loaded.name[0]);
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_equal(2U, k380_dynamic_macro_test_settings_operation_count());
    zassert_equal(0, strcmp("save:k380/dynamic_config/p/0/vm/0",
                             k380_dynamic_macro_test_settings_operation(0U)));
    zassert_equal(0, strcmp("delete:k380/dynamic_config/v2/p/0/m/0",
                             k380_dynamic_macro_test_settings_operation(1U)));
}

ZTEST(dynamic_macro_store, test_failed_temporary_migration_preserves_source)
{
    struct k380_dynamic_macro_record temporary_record;
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    make_record(&temporary_record);
    put_record("k380/dynamic_config/v2/p/0/m/0", &temporary_record);
    k380_dynamic_macro_test_settings_fail_next_save();

    zassert_equal(-EIO,
                  k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_equal(1U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store, test_legacy_migration_saves_final_before_deleting_v1)
{
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    make_legacy(&legacy);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));

    zassert_ok(k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_equal(2U, k380_dynamic_macro_test_settings_operation_count());
    zassert_equal(0, strcmp("save:k380/dynamic_config/p/0/vm/0",
                             k380_dynamic_macro_test_settings_operation(0U)));
    zassert_equal(0, strcmp("delete:k380/dynamic_config/p/0/m/0",
                             k380_dynamic_macro_test_settings_operation(1U)));
}

ZTEST(dynamic_macro_store, test_failed_migration_write_preserves_v1)
{
    struct k380_dynamic_macro legacy;
    struct k380_dynamic_macro_record loaded;

    k380_dynamic_macro_test_settings_reset();
    make_legacy(&legacy);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));
    k380_dynamic_macro_test_settings_fail_next_save();

    zassert_not_equal(0, k380_dynamic_macro_store_load(0U, 0U, &loaded));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_equal(1U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store, test_restore_slot_deletes_all_three_paths)
{
    struct k380_dynamic_macro_record record;
    struct k380_dynamic_macro legacy;

    k380_dynamic_macro_test_settings_reset();
    make_record(&record);
    make_legacy(&legacy);
    put_record("k380/dynamic_config/p/0/vm/0", &record);
    put_record("k380/dynamic_config/v2/p/0/m/0", &record);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));

    zassert_ok(k380_dynamic_macro_store_restore_slot(0U, 0U));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
                  "k380/dynamic_config/p/0/m/0"));
    zassert_equal(3U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store,
      test_restore_slot_attempts_remaining_deletes_after_final_failure)
{
    struct k380_dynamic_macro_record record;
    struct k380_dynamic_macro legacy;

    k380_dynamic_macro_test_settings_reset();
    make_record(&record);
    make_legacy(&legacy);
    put_record("k380/dynamic_config/p/0/vm/0", &record);
    put_record("k380/dynamic_config/v2/p/0/m/0", &record);
    k380_dynamic_macro_test_settings_put(
        "k380/dynamic_config/p/0/m/0", &legacy, sizeof(legacy));
    k380_dynamic_macro_test_settings_fail_next_delete();

    zassert_equal(-EIO, k380_dynamic_macro_store_restore_slot(0U, 0U));
    zassert_true(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/vm/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/v2/p/0/m/0"));
    zassert_false(k380_dynamic_macro_test_settings_has(
        "k380/dynamic_config/p/0/m/0"));
    zassert_equal(3U, k380_dynamic_macro_test_settings_operation_count());
}

ZTEST(dynamic_macro_store, test_invalid_indexes_are_rejected)
{
    struct k380_dynamic_macro_record record;

    k380_dynamic_macro_test_settings_reset();
    make_record(&record);
    zassert_equal(-EINVAL, k380_dynamic_macro_store_save(
                                K380_DYNAMIC_PRESET_COUNT, 0U, &record));
    zassert_equal(-EINVAL, k380_dynamic_macro_store_load(
                                0U, K380_DYNAMIC_MACRO_SLOT_COUNT, &record));
    zassert_equal(-EINVAL, k380_dynamic_macro_store_restore_preset(
                                K380_DYNAMIC_PRESET_COUNT));
}

ZTEST(dynamic_macro_store, test_all_maximum_records_fit_raw_budget)
{
    zassert_equal(73728U, K380_DYNAMIC_PRESET_COUNT *
                              K380_DYNAMIC_MACRO_SLOT_COUNT *
                              K380_DYNAMIC_MACRO_RECORD_MAX_BYTES);
}

ZTEST_SUITE(dynamic_macro_store, NULL, NULL, NULL, NULL, NULL);
