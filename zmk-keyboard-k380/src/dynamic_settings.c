#include <errno.h>
#include <stddef.h>

#include <zephyr/settings/settings.h>

#include <zmk_keyboard_k380/dynamic_settings.h>

#define K380_DYNAMIC_SETTINGS_BLOB K380_DYNAMIC_SETTINGS_ROOT "/blob"

static struct k380_dynamic_config *load_target;

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

int k380_dynamic_settings_load(struct k380_dynamic_config *cfg)
{
    if (cfg == NULL) {
        return -EINVAL;
    }

    struct k380_dynamic_config loaded;
    k380_dynamic_config_init_defaults(&loaded);

    load_target = &loaded;
    const int err = settings_load_subtree_direct(
        K380_DYNAMIC_SETTINGS_ROOT, k380_dynamic_settings_load_blob, NULL);
    load_target = NULL;
    if (err == -ENOENT) {
        *cfg = loaded;
        return 0;
    }
    if (err != 0) {
        return err;
    }

    if (k380_dynamic_config_validate(&loaded) != 0) {
        return -EINVAL;
    }

    *cfg = loaded;
    return 0;
}

int k380_dynamic_settings_save(const struct k380_dynamic_config *cfg)
{
    if (cfg == NULL || k380_dynamic_config_validate(cfg) != 0) {
        return -EINVAL;
    }

    return settings_save_one(K380_DYNAMIC_SETTINGS_BLOB, cfg, sizeof(*cfg));
}

int k380_dynamic_settings_restore_all(void)
{
    return settings_delete(K380_DYNAMIC_SETTINGS_BLOB);
}
