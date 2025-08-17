#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <string.h>


/* GAP preset (modern) */
#define ADV_PRESET  BT_LE_ADV_CONN_FAST_2

/* Adv payload: flags only */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/* Put the device name in SCAN RESPONSE, per 4.2 guidance */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME, strlen(CONFIG_BT_DEVICE_NAME)),
};


static void adv_restart_work_handler(struct k_work *work)
{
    int rc = bt_le_adv_start(ADV_PRESET, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    printk("Adv %s (err %d)\n", rc ? "restart failed" : "restarted", rc);
}
K_WORK_DEFINE(adv_restart_work, adv_restart_work_handler);

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connect failed (%u)\n", err);
        return;
    }
    printk("Connected\n");

    const struct bt_le_conn_param param = {
        .interval_min = 6,   /* 7.5 ms */
        .interval_max = 12,  /* 15  ms */
        .latency      = 0,
        .timeout      = 400, /* 4.0 s */
    };
    bt_conn_le_param_update(conn, &param);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (0x%02x). Restarting adv...\n", reason);
    printk("Disconnected (0x%02x). Queuing adv restart...\n", reason);
    /* Don’t call bt_le_adv_start() directly here; defer it a bit */
    k_work_submit(&adv_restart_work);              /* immediate */
}

BT_CONN_CB_DEFINE(conn_cbs) = {
    .connected = connected,
    .disconnected = disconnected,
};

void main(void)
{
    int err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }
    printk("Bluetooth initialized\n");

    /* Use GAP preset: FAST_1 (faster, meant for user-initiated ble advertising) or FAST_2 (still fast) */
    err = bt_le_adv_start(ADV_PRESET, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    printk("%s advertising as \"%s\" (err %d)\n",
           err ? "Failed to start" : "Started",
           CONFIG_BT_DEVICE_NAME, err);

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
