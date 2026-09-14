#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/settings/settings.h>

#include <zmk_keyboard_k380/dynamic_macro_record.h>
#include <zmk_keyboard_k380/dynamic_settings.h>

#define FAKE_SETTINGS_MAX_ENTRIES 128U
#define FAKE_SETTINGS_MAX_KEY_BYTES 96U
#define FAKE_SETTINGS_MAX_DATA_BYTES \
    K380_DYNAMIC_MACRO_RECORD_MAX_BYTES
#define FAKE_SETTINGS_MAX_OPERATIONS 256U

struct fake_settings_entry {
    bool used;
    char key[FAKE_SETTINGS_MAX_KEY_BYTES];
    uint8_t data[FAKE_SETTINGS_MAX_DATA_BYTES];
    size_t len;
};

static struct fake_settings_entry entries[FAKE_SETTINGS_MAX_ENTRIES];
static char operations[FAKE_SETTINGS_MAX_OPERATIONS][FAKE_SETTINGS_MAX_KEY_BYTES + 5U];
static size_t operation_count;
static bool fail_next_save;

void k380_dynamic_settings_lock(void)
{
}

void k380_dynamic_settings_unlock(void)
{
}

static struct fake_settings_entry *find_entry(const char *key)
{
    for (size_t index = 0U; index < FAKE_SETTINGS_MAX_ENTRIES; index++) {
        if (entries[index].used && strcmp(entries[index].key, key) == 0) {
            return &entries[index];
        }
    }
    return NULL;
}

static struct fake_settings_entry *allocate_entry(void)
{
    for (size_t index = 0U; index < FAKE_SETTINGS_MAX_ENTRIES; index++) {
        if (!entries[index].used) {
            entries[index].used = true;
            return &entries[index];
        }
    }
    return NULL;
}

static void record_operation(const char *prefix, const char *key)
{
    if (operation_count >= FAKE_SETTINGS_MAX_OPERATIONS) {
        return;
    }
    (void)snprintf(operations[operation_count],
                   sizeof(operations[operation_count]), "%s%s", prefix, key);
    operation_count++;
}

void k380_dynamic_macro_test_settings_reset(void)
{
    memset(entries, 0, sizeof(entries));
    memset(operations, 0, sizeof(operations));
    operation_count = 0U;
    fail_next_save = false;
}

void k380_dynamic_macro_test_settings_put(const char *key, const void *data,
                                           size_t len)
{
    struct fake_settings_entry *entry = find_entry(key);
    if (entry == NULL) {
        entry = allocate_entry();
    }
    if (entry == NULL || strlen(key) >= sizeof(entry->key) ||
        len > sizeof(entry->data)) {
        return;
    }
    strcpy(entry->key, key);
    memcpy(entry->data, data, len);
    entry->len = len;
}

bool k380_dynamic_macro_test_settings_has(const char *key)
{
    return find_entry(key) != NULL;
}

void k380_dynamic_macro_test_settings_fail_next_save(void)
{
    fail_next_save = true;
}

size_t k380_dynamic_macro_test_settings_operation_count(void)
{
    return operation_count;
}

const char *k380_dynamic_macro_test_settings_operation(size_t index)
{
    return index < operation_count ? operations[index] : NULL;
}

static int fake_read_cb(void *cb_arg, void *data, size_t len)
{
    const struct fake_settings_entry *entry = cb_arg;
    if (entry == NULL || len > entry->len) {
        return -EMSGSIZE;
    }
    memcpy(data, entry->data, len);
    return (int)len;
}

static bool key_in_subtree(const char *key, const char *subtree,
                           const char **relative)
{
    size_t subtree_len = strlen(subtree);
    if (strncmp(key, subtree, subtree_len) != 0 ||
        key[subtree_len] != '/') {
        return false;
    }
    *relative = &key[subtree_len + 1U];
    return **relative != '\0';
}

int k380_dynamic_macro_test_settings_save_one(const char *key,
                                              const void *value, size_t len)
{
    record_operation("save:", key);
    if (fail_next_save) {
        fail_next_save = false;
        return -EIO;
    }
    k380_dynamic_macro_test_settings_put(key, value, len);
    return 0;
}

int k380_dynamic_macro_test_settings_delete(const char *key)
{
    record_operation("delete:", key);
    struct fake_settings_entry *entry = find_entry(key);
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    }
    return 0;
}

int k380_dynamic_macro_test_settings_load_subtree_direct(
    const char *subtree, settings_load_direct_cb callback, void *cb_arg)
{
    if (subtree == NULL || callback == NULL) {
        return -EINVAL;
    }
    for (size_t index = 0U; index < FAKE_SETTINGS_MAX_ENTRIES; index++) {
        if (!entries[index].used) {
            continue;
        }
        const char *relative = NULL;
        if (!key_in_subtree(entries[index].key, subtree, &relative)) {
            continue;
        }
        int err = callback(relative, entries[index].len, fake_read_cb,
                           &entries[index], cb_arg);
        if (err != 0) {
            return err;
        }
    }
    return 0;
}
