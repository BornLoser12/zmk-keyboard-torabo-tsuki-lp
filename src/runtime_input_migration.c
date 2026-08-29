// SPDX-License-Identifier: MIT

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <cormoran/zmk/custom_settings.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zmk/event_manager.h>
#include <zmk/pointing/input_processor_runtime.h>

LOG_MODULE_REGISTER(torabo_runtime_input_migration, CONFIG_ZMK_LOG_LEVEL);

#define MIGRATION_SETTINGS_ROOT "torabo_tsuki"
#define MOUSE_DEFAULTS_MIGRATION_KEY "mouse_defaults_v2"
#define MOUSE_DEFAULTS_MIGRATION_PATH                                                            \
    MIGRATION_SETTINGS_ROOT "/" MOUSE_DEFAULTS_MIGRATION_KEY
#define MOUSE_DEFAULTS_MIGRATION_VERSION 1

static bool mouse_defaults_migrated;

static int migration_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                  void *cb_arg) {
    if (strcmp(name, MOUSE_DEFAULTS_MIGRATION_KEY) != 0) {
        return -ENOENT;
    }

    if (len != sizeof(uint8_t)) {
        return -EINVAL;
    }

    uint8_t version = 0;
    ssize_t read_len = read_cb(cb_arg, &version, sizeof(version));
    if (read_len < 0) {
        return (int)read_len;
    }
    if (read_len != sizeof(version)) {
        return -EINVAL;
    }

    mouse_defaults_migrated = version >= MOUSE_DEFAULTS_MIGRATION_VERSION;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(torabo_runtime_input_migration, MIGRATION_SETTINGS_ROOT, NULL,
                               migration_settings_set, NULL, NULL);

static int migrate_mouse_defaults(const zmk_event_t *eh) {
    if (as_zmk_custom_settings_initialized(eh) == NULL || mouse_defaults_migrated) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct device *mouse = zmk_input_processor_runtime_find_by_name("mouse");
    if (mouse == NULL) {
        LOG_ERR("Cannot migrate runtime input defaults: mouse processor not found");
        return ZMK_EV_EVENT_BUBBLE;
    }

    int ret = zmk_input_processor_runtime_reset(mouse);
    if (ret < 0) {
        LOG_ERR("Failed to reset mouse processor defaults: %d", ret);
        return ZMK_EV_EVENT_BUBBLE;
    }

    ret = zmk_input_processor_runtime_save_all();
    if (ret < 0) {
        LOG_ERR("Failed to save migrated runtime input defaults: %d", ret);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint8_t version = MOUSE_DEFAULTS_MIGRATION_VERSION;
    ret = settings_save_one(MOUSE_DEFAULTS_MIGRATION_PATH, &version, sizeof(version));
    if (ret < 0) {
        LOG_ERR("Failed to save runtime input migration marker: %d", ret);
        return ZMK_EV_EVENT_BUBBLE;
    }

    mouse_defaults_migrated = true;
    LOG_INF("Reset legacy mouse settings to stable defaults");
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(torabo_runtime_input_migration, migrate_mouse_defaults);
ZMK_SUBSCRIPTION(torabo_runtime_input_migration, zmk_custom_settings_initialized);
