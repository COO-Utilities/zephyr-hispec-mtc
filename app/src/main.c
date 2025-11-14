#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

#include <drivers/heater.h>

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

int main(void) {
    LOG_INF("TPS55287-Q1 Heater Chip Test");
    const struct device *tps = DEVICE_DT_GET(DT_ALIAS(tps55287q1_test));

    if (!device_is_ready(tps)) {
        LOG_ERR("HEATER device %s is not ready\n", tps->name);
        return 0;
    }

    int ret = heater_reg_write(tps, 0x00, 0x10);
    if (ret != 0) {
        LOG_ERR("dac_write_value() failed with code %d\n", ret);
        return 0;
    }

    uint8_t val;
    heater_reg_read(tps, 0x00, &val);
    printk("LSB = 0x%02x\n", val);

    LOG_INF("End of main function");

    return 0;
}
