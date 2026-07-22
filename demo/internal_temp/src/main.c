/* Read the AD7124 on-die temperature sensor -- no external sensor wiring. */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <config.h>
#include <adc_temp_sensor.h>

LOG_MODULE_REGISTER(internal_temp, LOG_LEVEL_INF);

int main(void)
{
	thermal_config_t *config = config_load_defaults();
	if (config == NULL) {
		LOG_ERR("config_load_defaults failed");
		return 0;
	}

	int ret = adc_temp_sensor_init(config);
	if (ret != 0) {
		LOG_ERR("adc_temp_sensor_init failed: %d", ret);
		return 0;
	}

	while (1) {
		float kelvin = 0.0f;

		ret = adc_temp_sensor_read("sensor-1", &kelvin);
		if (ret == 0) {
			printf("internal die temp: %.2f C\n", (double)(kelvin - 273.15f));
		} else {
			printf("read failed: %d\n", ret);
		}
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
