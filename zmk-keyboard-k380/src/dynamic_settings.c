#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zmk_keyboard_k380/dynamic_settings.h>

#define K380_DYNAMIC_SETTINGS_BLOB K380_DYNAMIC_SETTINGS_ROOT "/blob"

static struct k380_dynamic_config shared_config;
static struct k380_dynamic_config *load_target;
K_MUTEX_DEFINE(shared_config_lock);

static int k380_dynamic_settings_load_blob(const char *name, size_t len,
                                           settings_read_cb read_cb,
                                           void *cb_arg)
{
    if (!settings_name_steq(name, "blob", NULL)) {
        return -EINVAL;
    }

    if (load_target == NULL || len != sizeof(*load_target)) {
        return -EMSGSIZE;
    }

    const int read_len = read_cb(cb_arg, load_target, sizeof(*load_target));
    if (read_len < 0) {
        return read_len;
    }

    return read_len == sizeof(*load_target) ? 0 : -EMSGSIZE;
}

static int load_unlocked(struct k380_dynamic_config *cfg)
{
    k380_dynamic_config_init_defaults(cfg);

    load_target = cfg;
    const int err = settings_load_subtree_direct(
        K380_DYNAMIC_SETTINGS_ROOT, k380_dynamic_settings_load_blob, NULL);
    load_target = NULL;
    if (err == -ENOENT) {
        return 0;
    }
    if (err != 0) {
        return err;
    }

    if (k380_dynamic_config_validate(cfg) != 0) {
        return -EINVAL;
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
    shared_config = *cfg;
    err = settings_save_one(K380_DYNAMIC_SETTINGS_BLOB, &shared_config,
                            sizeof(shared_config));
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
        err = callback(&shared_config, user_data);
    }
    if (err == 0) {
        err = k380_dynamic_config_validate(&shared_config);
    }
    if (err == 0) {
        err = settings_save_one(K380_DYNAMIC_SETTINGS_BLOB, &shared_config,
                                sizeof(shared_config));
    }
    k_mutex_unlock(&shared_config_lock);

    return err;
}

int k380_dynamic_settings_restore_all(void)
{
    return settings_delete(K380_DYNAMIC_SETTINGS_BLOB);
}
