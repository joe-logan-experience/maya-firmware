#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <string.h>


/* GAP preset (modern) */
#define ADV_PRESET  BT_LE_ADV_CONN_FAST_2

/* LED0 node label from device tree */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* PWM device: ESP32 LEDC block 0 (matches &ledc0 in overlay) */
#define LEDC0_NODE DT_NODELABEL(ledc0)
static const struct device *pwm_ledc;

/* Channels: match overlay channel regs */
#define CH_PAN   0
#define CH_TILT  1

/* 50 Hz servo timing */
#define SERVO_PERIOD_NS  20000000ULL   /* 20 ms */
#define PULSE_MIN_NS      500000ULL    /* 0.5 ms (adjust to 1.0 ms if your servo expects 1–2 ms) */
#define PULSE_MAX_NS     2500000ULL    /* 2.5 ms */


/* Helpers */
static inline uint32_t deg_to_pulse_ns(uint8_t deg)
{
    uint32_t d = CLAMP(deg, 0, 180);
    uint64_t span = (PULSE_MAX_NS - PULSE_MIN_NS);
    return (uint32_t)(PULSE_MIN_NS + (span * d) / 180);
}
static inline uint8_t slew(uint8_t cur, uint8_t tgt, uint8_t step)
{
    if (cur < tgt) return (tgt - cur > step) ? cur + step : tgt;
    if (cur > tgt) return (cur - tgt > step) ? cur - step : tgt;
    return cur;
}


/* Adv payload: flags only */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/* Put the device name in SCAN RESPONSE, per 4.2 guidance */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME, strlen(CONFIG_BT_DEVICE_NAME)),
};

/* ===== Work item to safely restart advertising ===== */
static void adv_restart_work_handler(struct k_work *work)
{
    int rc = bt_le_adv_start(ADV_PRESET, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    printk("Adv %s (err %d)\n", rc ? "restart failed" : "restarted", rc);
}
K_WORK_DEFINE(adv_restart_work, adv_restart_work_handler);


/* ===== GATT: combined vector command (pan, tilt) via Write Without Response ===== */
/* 128-bit UUIDs (pick your own later) */
#define BT_UUID_MAYA_SVC   BT_UUID_DECLARE_128(0x12,0x34,0,0,0,0,0,0,0,0,0,0,0,0,0xA2,0x01)
#define BT_UUID_MAYA_VEC   BT_UUID_DECLARE_128(0x12,0x34,0,0,0,0,0,0,0,0,0,0,0,0,0xA2,0x02)

/* Targets updated by BLE writes; control loop slews toward them */
static atomic_t pan_target  = ATOMIC_INIT(90);
static atomic_t tilt_target = ATOMIC_INIT(90);

/* Expect exactly 2 bytes: [pan_deg, tilt_deg] (0–180) */
static ssize_t vec_write_wnr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0 || len != 2) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    const uint8_t *p = buf;
    atomic_set(&pan_target,  CLAMP(p[0], 0, 180));
    atomic_set(&tilt_target, CLAMP(p[1], 0, 180));
    return len;
}

BT_GATT_SERVICE_DEFINE(maya_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MAYA_SVC),
    BT_GATT_CHARACTERISTIC(BT_UUID_MAYA_VEC,
        BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, vec_write_wnr, NULL),
);


static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connect failed (%u)\n", err);
        return;
    }
    printk("Connected\n");

    /* Turn LED on */
    gpio_pin_set_dt(&led, 1);

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
    printk("Disconnected (0x%02x). Queuing adv restart...\n", reason);

    /* Turn LED off */
    gpio_pin_set_dt(&led, 0);

    /* Don’t call bt_le_adv_start() directly here; defer it a bit */
    k_work_submit(&adv_restart_work);              /* immediate */
}

BT_CONN_CB_DEFINE(conn_cbs) = {
    .connected = connected,
    .disconnected = disconnected,
};

void main(void)
{
    int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("Error configuring LED pin!\n");
        return 1;
    }

    /* PWM (LEDC) init */
    pwm_ledc = DEVICE_DT_GET(LEDC0_NODE);
    if (!device_is_ready(pwm_ledc)) {
        printk("LEDC not ready\n");
        return;
    }
    /* Center both servos initially */
    pwm_set(pwm_ledc, CH_PAN,  SERVO_PERIOD_NS, deg_to_pulse_ns(90), 0);
    pwm_set(pwm_ledc, CH_TILT, SERVO_PERIOD_NS, deg_to_pulse_ns(90), 0);

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

    /* Turn LED off */
    gpio_pin_set_dt(&led, 0);

    /* Control loop: 200 Hz — smooth slew to targets */
    uint8_t pan_cur = 90, tilt_cur = 90;
    while (1) {
        uint8_t pan_tgt  = (uint8_t)atomic_get(&pan_target);
        uint8_t tilt_tgt = (uint8_t)atomic_get(&tilt_target);

        pan_cur  = slew(pan_cur,  pan_tgt,  3);  /* ≤3° per 5 ms */
        tilt_cur = slew(tilt_cur, tilt_tgt, 3);

        pwm_set(pwm_ledc, CH_PAN,  SERVO_PERIOD_NS, deg_to_pulse_ns(pan_cur),  0);
        pwm_set(pwm_ledc, CH_TILT, SERVO_PERIOD_NS, deg_to_pulse_ns(tilt_cur), 0);

        k_sleep(K_MSEC(5));
    }
}
