#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

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

    float set_power_percent = 0.0f;
    heater_manager_set_power("high-power-1", set_power_percent);
    // heater_manager_emergency_stop();

    float power_percent = 0.0f;
    heater_manager_get_power("high-power-1", &power_percent);
    LOG_INF("Heater power: %.1lf%%", power_percent);
    

    /* Test loop: Cycle through power levels */
    // while (1) {
    //     /* 0% Power */
    //     heater_manager_set_power("high-power-1", 0.0f);
    //     k_sleep(K_SECONDS(5));

    //     /* 10% Power */
    //     heater_manager_set_power("high-power-1", 10.0f);
    //     k_sleep(K_SECONDS(5));

    //     /* 50% Power */
    //     heater_manager_set_power("high-power-1", 50.0f);
    //     k_sleep(K_SECONDS(5));
        
    //     /* 100% Power */
    //     heater_manager_set_power("high-power-1", 100.0f);
    //     k_sleep(K_SECONDS(5));
    // }
    return 0;
}
