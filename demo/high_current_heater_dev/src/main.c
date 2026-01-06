#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <math.h>

#include <config.h>
#include <heater_manager.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("Starting High Current Heater Demo");

    /* Load default configuration */
    thermal_config_t *config = config_load_defaults();
    if (!config) {
        LOG_ERR("Failed to load default config");
        return -1;
    }

    /* Configure Heater 1 as High Power for testing the regulator integration */
    /* This overrides the default low-power setting */
    strncpy(config->heaters[0].id, "high-power-1", MAX_ID_LENGTH - 1);
    config->heaters[0].type = HEATER_TYPE_HIGH_POWER;
    config->heaters[0].max_power_w = 40.0f;     // 40W max
    config->heaters[0].resistance_ohms = 30.0f;  // 30 Ohms
    config->heaters[0].regulator_dev = DEVICE_DT_GET(DT_ALIAS(high_current_heater_test));
    config->heaters[0].enabled = true;

    /* Initialize heater manager */
    int ret = heater_manager_init(config);
    if (ret < 0) {
        LOG_ERR("Failed to initialize heater manager: %d", ret);
        return -1;
    }

    LOG_INF("Heater manager initialized. Starting power cycle loop...");

    LOG_INF("Starting power ramp test: 0% to 35% with 1% increments");

    for (int i = 0; i <= 35; i++) {
        float p = (float)i;
        heater_manager_set_power("high-power-1", p);
        
        float current_p = 0.0f;
        heater_manager_get_power("high-power-1", &current_p);
        
        float expected_v = sqrtf(30.0f * 40.0f * p / 100.0f);
        LOG_INF("Set Power[%%]: %.1f%%, Readback[%%]: %.1f%%, Expected Voltage: %.3fV", 
                (double)p, (double)current_p, (double)expected_v);
        
        k_sleep(K_MSEC(6000));
    }

    heater_manager_set_power("high-power-1", 0.0f);

    while (1) {
        k_sleep(K_FOREVER);
        // Keep the main thread alive or loop indefinitely if needed
        // For this test, we just stop after the ramp.
    }
    return 0;
}
