# maya-firmware

## Prerequisites

- Install and follow the [Zephyr Project Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

## Building

To build the firmware for ESP32 (PRO CPU), run:

```sh
west build -p always -b esp32_devkitc/esp32/procpu
```

## Flashing

To flash the device, run:

```sh
west flash
```
