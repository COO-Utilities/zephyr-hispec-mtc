#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/regulator.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
    const struct device *reg = DEVICE_DT_GET(DT_ALIAS(tps55287q1_test));

    if (!device_is_ready(reg)) {
        LOG_ERR("TPS55287-Q1 device not ready; aborting.");
        return 0;
    }

    int ret;

    LOG_INF("Enabling TPS55287Q1...");
    ret = regulator_enable(reg);
    if (ret < 0) {
        LOG_ERR("regulator_enable() failed: %d", ret);
        return 0;
    }

    /* Example: set 12 V */
    ret = regulator_set_voltage(reg, 12000000, 12000000);
    if (ret < 0) {
        LOG_ERR("regulator_set_voltage() failed: %d", ret);
        return 0;
    }

    int32_t v;
    ret = regulator_get_voltage(reg, &v);
    if (ret == 0) {
        LOG_INF("VOUT now ~%d mV", (int)(v / 1000));
    }

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
