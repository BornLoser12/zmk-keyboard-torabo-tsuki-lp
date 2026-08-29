// SPDX-License-Identifier: MIT

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>

LOG_MODULE_REGISTER(ble_reconnect_diag, LOG_LEVEL_INF);

struct bond_dump_context {
    const bt_addr_le_t *dst;
    const bt_addr_le_t *remote;
    uint8_t count;
};

static void log_address(const char *label, const bt_addr_le_t *addr) {
    if (!addr) {
        LOG_INF("BLE_DIAG addr %s=<null>", label);
        return;
    }

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    LOG_INF("BLE_DIAG addr %s=%s type=%u raw=%02x:%02x:%02x:%02x:%02x:%02x", label,
            addr_str, addr->type, addr->a.val[5], addr->a.val[4], addr->a.val[3],
            addr->a.val[2], addr->a.val[1], addr->a.val[0]);
}

static void dump_profiles(const bt_addr_le_t *dst, const bt_addr_le_t *remote) {
    int active = zmk_ble_active_profile_index();
    int dst_index = dst ? zmk_ble_profile_index(dst) : -1;
    int remote_index = remote ? zmk_ble_profile_index(remote) : -1;

    LOG_INF("BLE_DIAG profiles active=%d dst_index=%d remote_index=%d count=%d", active,
            dst_index, remote_index, ZMK_BLE_PROFILE_COUNT);

    for (uint8_t i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        const bt_addr_le_t *profile = zmk_ble_profile_address(i);
        char addr_str[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(profile, addr_str, sizeof(addr_str));
        LOG_INF("BLE_DIAG profile[%u] addr=%s open=%u active=%u cmp_dst=%d cmp_remote=%d", i,
                addr_str, zmk_ble_profile_is_open(i), i == active,
                dst ? bt_addr_le_cmp(profile, dst) : -1,
                remote ? bt_addr_le_cmp(profile, remote) : -1);
    }
}

static void dump_bond(const struct bt_bond_info *info, void *user_data) {
    struct bond_dump_context *context = user_data;
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));
    LOG_INF("BLE_DIAG bond[%u] addr=%s cmp_dst=%d cmp_remote=%d", context->count, addr_str,
            context->dst ? bt_addr_le_cmp(&info->addr, context->dst) : -1,
            context->remote ? bt_addr_le_cmp(&info->addr, context->remote) : -1);
    context->count++;
}

static void dump_bonds(const bt_addr_le_t *dst, const bt_addr_le_t *remote) {
    struct bond_dump_context context = {
        .dst = dst,
        .remote = remote,
    };

    bt_foreach_bond(BT_ID_DEFAULT, dump_bond, &context);
    LOG_INF("BLE_DIAG bonds total=%u", context.count);
}

static void dump_connection(const char *event, struct bt_conn *conn) {
    struct bt_conn_info info;
    const bt_addr_le_t *dst = bt_conn_get_dst(conn);
    int err = bt_conn_get_info(conn, &info);

    if (err) {
        LOG_ERR("BLE_DIAG event=%s conn=%u get_info_err=%d", event, bt_conn_index(conn), err);
        log_address("bt_conn_get_dst", dst);
        dump_profiles(dst, NULL);
        dump_bonds(dst, NULL);
        return;
    }

    LOG_INF("BLE_DIAG event=%s conn=%u role=%u state=%u id=%u security=%u key_size=%u "
            "interval=%u latency=%u timeout=%u",
            event, bt_conn_index(conn), info.role, info.state, info.id, info.security.level,
            info.security.enc_key_size, info.le.interval, info.le.latency, info.le.timeout);
    log_address("bt_conn_get_dst", dst);
    log_address("info.le.dst", info.le.dst);
    log_address("info.le.remote", info.le.remote);
    log_address("info.le.src", info.le.src);
    log_address("info.le.local", info.le.local);
    dump_profiles(dst, info.le.remote);
    dump_bonds(dst, info.le.remote);
}

static void snapshot_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    LOG_INF("BLE_DIAG snapshot begin uptime_ms=%lld", k_uptime_get());
    dump_profiles(NULL, NULL);
    dump_bonds(NULL, NULL);
    LOG_INF("BLE_DIAG snapshot end");
}

static K_WORK_DELAYABLE_DEFINE(snapshot_work, snapshot_work_handler);

static void diagnostics_connected(struct bt_conn *conn, uint8_t err) {
    LOG_INF("BLE_DIAG callback connected err=0x%02x", err);
    dump_connection("connected", conn);
}

static void diagnostics_disconnected(struct bt_conn *conn, uint8_t reason) {
    LOG_INF("BLE_DIAG callback disconnected reason=0x%02x", reason);
    dump_connection("disconnected", conn);
}

static void diagnostics_identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
                                          const bt_addr_le_t *identity) {
    LOG_INF("BLE_DIAG callback identity_resolved");
    log_address("resolved.rpa", rpa);
    log_address("resolved.identity", identity);
    dump_connection("identity_resolved", conn);
}

static void diagnostics_security_changed(struct bt_conn *conn, bt_security_t level,
                                         enum bt_security_err err) {
    LOG_INF("BLE_DIAG callback security_changed level=%u err=%u", level, err);
    dump_connection("security_changed", conn);
}

static struct bt_conn_cb diagnostics_conn_callbacks = {
    .connected = diagnostics_connected,
    .disconnected = diagnostics_disconnected,
    .identity_resolved = diagnostics_identity_resolved,
    .security_changed = diagnostics_security_changed,
};

static void diagnostics_pairing_complete(struct bt_conn *conn, bool bonded) {
    LOG_INF("BLE_DIAG callback pairing_complete bonded=%u", bonded);
    dump_connection("pairing_complete", conn);
    k_work_reschedule(&snapshot_work, K_MSEC(250));
}

static void diagnostics_pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
    LOG_INF("BLE_DIAG callback pairing_failed reason=%u", reason);
    dump_connection("pairing_failed", conn);
}

static void diagnostics_bond_deleted(uint8_t id, const bt_addr_le_t *peer) {
    LOG_INF("BLE_DIAG callback bond_deleted id=%u", id);
    log_address("deleted.peer", peer);
    k_work_reschedule(&snapshot_work, K_MSEC(250));
}

static struct bt_conn_auth_info_cb diagnostics_auth_callbacks = {
    .pairing_complete = diagnostics_pairing_complete,
    .pairing_failed = diagnostics_pairing_failed,
    .bond_deleted = diagnostics_bond_deleted,
};

static int ble_reconnect_diagnostics_init(void) {
    int conn_err = bt_conn_cb_register(&diagnostics_conn_callbacks);
    int auth_err = bt_conn_auth_info_cb_register(&diagnostics_auth_callbacks);

    LOG_INF("BLE_DIAG init conn_cb=%d auth_cb=%d", conn_err, auth_err);
    k_work_schedule(&snapshot_work, K_MSEC(1500));
    return conn_err ? conn_err : auth_err;
}

SYS_INIT(ble_reconnect_diagnostics_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
