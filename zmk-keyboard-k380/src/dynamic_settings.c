#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zmk_keyboard_k380/dynamic_settings.h>

#define K380_DYNAMIC_SETTINGS_ACTIVE K380_DYNAMIC_SETTINGS_ROOT "/active"
#define K380_DYNAMIC_SETTINGS_LEGACY_BLOB K380_DYNAMIC_SETTINGS_ROOT "/blob"

static struct k380_dynamic_config shared_config;
static struct k380_dynamic_config baseline_config;
static struct k380_dynamic_config *load_target;
static int load_status;
K_MUTEX_DEFINE(shared_config_lock);

static bool is_zeroed(const void *data, size_t len)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < len; i++) {
        if (bytes[i] != 0U) {
            return false;
        }
    }

    return true;
}

static int check_formatted_len(int written, size_t key_len)
{
    return written < 0 || (size_t)written >= key_len ? -ENAMETOOLONG : 0;
}

static int format_preset_key(char *key, size_t key_len, uint8_t preset,
                             const char *leaf)
{
    return check_formatted_len(
        snprintf(key, key_len, K380_DYNAMIC_SETTINGS_ROOT "/p/%u/%s", preset,
                 leaf),
        key_len);
}

static int format_macro_key(char *key, size_t key_len, uint8_t preset,
                            uint8_t slot)
{
    return check_formatted_len(
        snprintf(key, key_len, K380_DYNAMIC_SETTINGS_ROOT "/p/%u/m/%u", preset,
                 slot),
        key_len);
}

static int parse_index(const char *name, uint8_t limit, const char **next)
{
    uint16_t value = 0U;
    bool has_digit = false;

    while (*name >= '0' && *name <= '9') {
        has_digit = true;
        value = (uint16_t)(value * 10U + (uint16_t)(*name - '0'));
        if (value >= limit) {
            return -EINVAL;
        }
        name++;
    }

    if (!has_digit) {
        return -EINVAL;
    }

    if (*name == '/') {
        if (next != NULL) {
            *next = name + 1;
        }
        return value;
    }

    if (*name == '\0' || *name == '=') {
        if (next != NULL) {
            *next = NULL;
        }
        return value;
    }

    return -EINVAL;
}

static int read_record(size_t len, settings_read_cb read_cb, void *cb_arg,
                       void *target, size_t target_len)
{
    if (load_target == NULL || len != target_len) {
        return -EMSGSIZE;
    }

    const int read_len = read_cb(cb_arg, target, target_len);
    if (read_len < 0) {
        return read_len;
    }

    return read_len == target_len ? 0 : -EMSGSIZE;
}

static int load_record(const char *name, size_t len, settings_read_cb read_cb,
                       void *cb_arg)
{
    const char *next;
    int err = 0;

    if (settings_name_steq(name, "active", NULL)) {
        err = read_record(len, read_cb, cb_arg, &load_target->active_preset,
                          sizeof(load_target->active_preset));
        goto done;
    }

    if (!settings_name_steq(name, "p", &next)) {
        return 0;
    }

    const int preset = parse_index(next, K380_DYNAMIC_PRESET_COUNT, &next);
    if (preset < 0 || next == NULL) {
        return 0;
    }

    struct k380_dynamic_preset *target = &load_target->presets[preset];
    if (settings_name_steq(next, "name", NULL)) {
        err = read_record(len, read_cb, cb_arg, target->name,
                          sizeof(target->name));
        goto done;
    }
    if (settings_name_steq(next, "bindings", NULL)) {
        err = read_record(len, read_cb, cb_arg, target->bindings,
                          sizeof(target->bindings));
        goto done;
    }
    if (!settings_name_steq(next, "m", &next)) {
        return 0;
    }

    const int slot = parse_index(next, K380_DYNAMIC_MACRO_SLOT_COUNT, &next);
    if (slot < 0 || next != NULL) {
        return 0;
    }

    err = read_record(len, read_cb, cb_arg, &target->macros[slot],
                      sizeof(target->macros[slot]));

done:
    if (err != 0 && load_status == 0) {
        load_status = err;
    }
    return err;
}

static int load_unlocked(struct k380_dynamic_config *cfg)
{
    k380_dynamic_config_init_defaults(cfg);

    load_target = cfg;
    load_status = 0;
    const int err = settings_load_subtree_direct(K380_DYNAMIC_SETTINGS_ROOT,
                                                 load_record, NULL);
    load_target = NULL;
    if (err != 0) {
        return err;
    }
    if (load_status != 0) {
        return load_status;
    }

    if (k380_dynamic_config_validate(cfg) != 0) {
        return -EINVAL;
    }

    return 0;
}

static int save_or_delete(const char *key, const void *value, size_t value_len)
{
    return is_zeroed(value, value_len) ? settings_delete(key)
                                      : settings_save_one(key, value, value_len);
}

static int save_preset_key(uint8_t preset, const char *leaf, const void *value,
                           size_t value_len)
{
    char key[64];
    int err = format_preset_key(key, sizeof(key), preset, leaf);

    if (err != 0) {
        return err;
    }

    return save_or_delete(key, value, value_len);
}

static int save_macro_key(uint8_t preset, uint8_t slot, const void *value,
                          size_t value_len)
{
    char key[64];
    int err = format_macro_key(key, sizeof(key), preset, slot);

    if (err != 0) {
        return err;
    }

    return save_or_delete(key, value, value_len);
}

static int delete_preset_key(uint8_t preset, const char *leaf)
{
    char key[64];
    int err = format_preset_key(key, sizeof(key), preset, leaf);

    if (err != 0) {
        return err;
    }

    return settings_delete(key);
}

static int delete_macro_key(uint8_t preset, uint8_t slot)
{
    char key[64];
    int err = format_macro_key(key, sizeof(key), preset, slot);

    if (err != 0) {
        return err;
    }

    return settings_delete(key);
}

static int save_changed_unlocked(const struct k380_dynamic_config *cfg,
                                 const struct k380_dynamic_config *baseline)
{
    int err;

    if (cfg->active_preset != baseline->active_preset) {
        err = cfg->active_preset == 0U
                  ? settings_delete(K380_DYNAMIC_SETTINGS_ACTIVE)
                  : settings_save_one(K380_DYNAMIC_SETTINGS_ACTIVE,
                                      &cfg->active_preset,
                                      sizeof(cfg->active_preset));
        if (err != 0) {
            return err;
        }
    }

    for (uint8_t preset = 0U; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        const struct k380_dynamic_preset *current = &cfg->presets[preset];
        const struct k380_dynamic_preset *previous = &baseline->presets[preset];

        if (memcmp(current->name, previous->name, sizeof(current->name)) != 0) {
            err = save_preset_key(preset, "name", current->name,
                                  sizeof(current->name));
            if (err != 0) {
                return err;
            }
        }

        if (memcmp(current->bindings, previous->bindings,
                   sizeof(current->bindings)) != 0) {
            err = save_preset_key(preset, "bindings", current->bindings,
                                  sizeof(current->bindings));
            if (err != 0) {
                return err;
            }
        }

        for (uint8_t slot = 0U; slot < K380_DYNAMIC_MACRO_SLOT_COUNT; slot++) {
            if (memcmp(&current->macros[slot], &previous->macros[slot],
                       sizeof(current->macros[slot])) == 0) {
                continue;
            }
            err = save_macro_key(preset, slot, &current->macros[slot],
                                 sizeof(current->macros[slot]));
            if (err != 0) {
                return err;
            }
        }
    }

    return 0;
}

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg)
{
    int err;

    if (cfg == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&shared_config_lock, K_FOREVER);
    err = load_unlocked(&shared_config);
    if (err == 0) {
        *cfg = shared_config;
    }
    k_mutex_unlock(&shared_config_lock);

    return err;
}

int k380_dynamic_settings_save(const struct k380_dynamic_config *cfg)
{
    int err;

    if (cfg == NULL || k380_dynamic_config_validate(cfg) != 0) {
        return -EINVAL;
    }

    k_mutex_lock(&shared_config_lock, K_FOREVER);
    err = load_unlocked(&baseline_config);
    if (err == 0) {
        err = save_changed_unlocked(cfg, &baseline_config);
    }
    if (err == 0) {
        shared_config = *cfg;
    }
    k_mutex_unlock(&shared_config_lock);

    return err;
}

int k380_dynamic_settings_with_config(k380_dynamic_settings_config_cb_t callback,
                                      void *user_data)
{
    int err;

    if (callback == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&shared_config_lock, K_FOREVER);
    err = load_unlocked(&shared_config);
    if (err == 0) {
        baseline_config = shared_config;
    }
    if (err == 0) {
        err = callback(&shared_config, user_data);
    }
    k_mutex_unlock(&shared_config_lock);

    return err;
}

int k380_dynamic_settings_update(k380_dynamic_settings_config_cb_t callback,
                                 void *user_data)
{
    int err;

    if (callback == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&shared_config_lock, K_FOREVER);
    err = load_unlocked(&shared_config);
    if (err == 0) {
        baseline_config = shared_config;
    }
    if (err == 0) {
        err = callback(&shared_config, user_data);
    }
    if (err == 0) {
        err = k380_dynamic_config_validate(&shared_config);
    }
    if (err == 0) {
        err = save_changed_unlocked(&shared_config, &baseline_config);
    }
    k_mutex_unlock(&shared_config_lock);

    return err;
}

int k380_dynamic_settings_restore_all(void)
{
    int err = settings_delete(K380_DYNAMIC_SETTINGS_ACTIVE);
    int first_err = err;

    err = settings_delete(K380_DYNAMIC_SETTINGS_LEGACY_BLOB);
    if (first_err == 0 && err != 0) {
        first_err = err;
    }

    for (uint8_t preset = 0U; preset < K380_DYNAMIC_PRESET_COUNT; preset++) {
        err = delete_preset_key(preset, "name");
        if (first_err == 0 && err != 0) {
            first_err = err;
        }

        err = delete_preset_key(preset, "bindings");
        if (first_err == 0 && err != 0) {
            first_err = err;
        }

        for (uint8_t slot = 0U; slot < K380_DYNAMIC_MACRO_SLOT_COUNT; slot++) {
            err = delete_macro_key(preset, slot);
            if (first_err == 0 && err != 0) {
                first_err = err;
            }
        }
    }

    return first_err;
}
