#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
// #include <zephyr/drivers/regulator.h>

#include <drivers/tps55287q1.h>

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

static const int32_t test_vout_table_mv[] = {
    1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000,
    5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500,
    10000,
};

int main(void)
{
    const struct device *tps = DEVICE_DT_GET(DT_ALIAS(tps55287q1_test));

    if (!device_is_ready(tps)) {
        LOG_ERR("TPS55287-Q1 device not ready");
        return 0;
    }

    LOG_INF("TPS55287-Q1 VOUT sweep demo starting");

#if DT_NODE_HAS_PROP(TPS_NODE, intfb)
    uint8_t intfb_code = DT_PROP(TPS_NODE, intfb);
#else
    uint8_t intfb_code = 3;
#endif

    int ret = tps55287q1_config_feedback(tps, false, intfb_code);
    if (ret) {
        LOG_ERR("config_feedback failed (%d)", ret);
        return 0;
    }

    ret = tps55287q1_enable_output(tps, true);
    if (ret) {
        LOG_ERR("enable_output failed (%d)", ret);
        return 0;
    }

    LOG_INF("Output enabled, entering VOUT sweep loop");

    while (1) {
        for (size_t i = 0; i < ARRAY_SIZE(test_vout_table_mv); i++) {
            uint32_t mv = test_vout_table_mv[i];

            LOG_INF("Setting VOUT to %u mV", mv);

            ret = tps55287q1_set_vout_mv(tps, mv, intfb_code);
            if (ret) {
                LOG_ERR("tps55287q1_set_vout_mv(%u mV) failed (%d)", mv, ret);
            }

            uint8_t status;
            if (!tps55287q1_get_status(tps, &status)) {
                LOG_INF("STATUS = 0x%02x", status);
            }

            k_sleep(K_SECONDS(10));
        }
    }

    return 0;
}