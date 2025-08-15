// src/main.c
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <app_version.h>

int main(void)
{
    printk("Maya firmware running...\n");
    printk("App version: %s\n", APP_VERSION_STRING);      // "0.1.0-unstable"
    printk("Build: " APP_BUILD_VERSION "\n");              // from `git describe`
    while (1) {
        k_msleep(1000);
    }
}

