#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <stdio.h>

#include <config.h>
#include <sensor_manager.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Static ADC specs */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
static const struct adc_dt_spec sensor_1_adc_spec = ADC_DT_SPEC_GET(DT_ALIAS(sensor_test));
#endif

int main(void)
{
    LOG_INF("Sensor Dev Demo Starting");

    /* Load configuration */
    thermal_config_t *config = config_load_defaults();
    if (!config) {
        LOG_ERR("Failed to load configuration");
        return -1;
    }

    /* Assign hardware drivers to config */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
    sensor_config_t *s1 = config_find_sensor(config, "sensor-1");
    if (s1) {
        s1->driver_data = &sensor_1_adc_spec;
    }
#endif

    /* Initialize Sensor Manager */
    int ret = sensor_manager_init(config);
    if (ret != 0) {
        LOG_ERR("Sensor manager init failed: %d", ret);
        return -1;
    }

    /* Main loop */
    while (1) {
        /* Read sensors */
        ret = sensor_manager_read_all();
        if (ret < 0) {
            LOG_WRN("Sensor read had %d errors", -ret);
        }

        /* Print readings */
        for (int i = 0; i < config->number_of_sensors; i++) {
            sensor_reading_t reading;
            if (sensor_manager_get_reading(config->sensors[i].id, &reading) == 0) {
                printf("Sensor %s: %.3f K (%.3f C)\n", 
                       config->sensors[i].id, 
                       reading.temperature_kelvin,
                       reading.temperature_kelvin - 273.15f);
            }
        }

        k_msleep(10000);
    }
    return 0;
}
