// src/main.c

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <app_version.h>


/* LED0 node label from device tree */
#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
    printk("Maya firmware running...\n");
    printk("App version: %s\n", APP_VERSION_STRING);      // "0.1.0-unstable"

    if (!device_is_ready(led.port)) {
        printk("Error: LED device not ready.\n");
        return 1;
    }

    int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("Error configuring LED pin!\n");
        return 1;
    }

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }
}

