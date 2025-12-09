#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/regulator.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
    const struct device *tps = DEVICE_DT_GET(DT_ALIAS(tps55287q1_test));

    if (!device_is_ready(tps)) {
        LOG_ERR("TPS55287-Q1 device not ready; aborting.");
        return 0;
    }

    int ret;

    LOG_INF("Enabling TPS55287Q1...");
    ret = regulator_enable(tps);
    if (ret < 0) {
        LOG_ERR("regulator_enable() failed: %d", ret);
        return 0;
    }

    bool active_discharge;
    ret = regulator_get_active_discharge(tps, &active_discharge);
    if (ret < 0) {
        LOG_ERR("regulator_get_active_discharge() failed: %d", ret);
        return 0;
    }
    LOG_INF("Active discharge is %s", active_discharge ? "enabled" : "disabled");

    /* Example: set 5 V */
    ret = regulator_set_voltage(tps, 5000000, 5000000);
    if (ret < 0) {
        LOG_ERR("regulator_set_voltage() failed: %d", ret);
        return 0;
    }

    int32_t vout_uv;
    ret = regulator_get_voltage(tps, &vout_uv);
    if (ret < 0) {
        LOG_ERR("regulator_get_voltage() failed: %d", ret);
        return 0;
    }
    LOG_INF("VOUT set to ~%d mV", (int)(vout_uv / 1000));

    k_sleep(K_SECONDS(10));

    LOG_INF("Disabling TPS55287Q1...");
    ret = regulator_disable(tps);
    if (ret < 0) {
        LOG_ERR("regulator_disable() failed: %d", ret);
        return 0;
    }

    // int32_t v;
    // ret = regulator_get_voltage(reg, &v);
    // if (ret == 0) {
    //     LOG_INF("VOUT now ~%d mV", (int)(v / 1000));
    // }

    // while (1) {
    //     k_sleep(K_SECONDS(1));
    // }
}
