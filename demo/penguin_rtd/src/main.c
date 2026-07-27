/* Read the two Penguin PT1000s through sensor_manager (the SENSOR_TYPE_P_RTD
 * path). Ratiometric against R1 (5.1k); both excitation currents flow through
 * it, so reference_resistance is 2*R1. */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <config.h>
#include <sensor_manager.h>

LOG_MODULE_REGISTER(penguin_rtd, LOG_LEVEL_INF);

static const struct adc_dt_spec penguin_spec[] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_ALIAS(penguins), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_ALIAS(penguins), 1),
};

static void configure_penguin(sensor_config_t *sensor, const char *id,
			      const struct adc_dt_spec *spec)
{
	strncpy(sensor->id, id, MAX_ID_LENGTH - 1);
	sensor->type = SENSOR_TYPE_P_RTD;
	sensor->nominal_resistance = 1000.0f;       /* PT1000 */
	sensor->temperature_coefficient = 3850.0f;  /* r_nom * alpha * 1000 */
	sensor->reference_resistance = 10200.0f;    /* 2 * R1; both IOUT share it */
	sensor->adc_gain = 1;
	sensor->adc_resolution = 24;
	sensor->driver_data = spec;
	sensor->enabled = true;
}

int main(void)
{
	thermal_config_t *config = config_load_defaults();

	if (config == NULL) {
		LOG_ERR("config_load_defaults failed");
		return 0;
	}

	config->number_of_sensors = 2;
	configure_penguin(&config->sensors[0], "penguin-1", &penguin_spec[0]);
	configure_penguin(&config->sensors[1], "penguin-2", &penguin_spec[1]);

	if (sensor_manager_init(config) != 0) {
		LOG_ERR("sensor_manager_init failed");
		return 0;
	}

	while (1) {
		sensor_manager_read_all();

		for (int i = 0; i < 2; i++) {
			sensor_reading_t r;

			if (sensor_manager_get_reading(config->sensors[i].id, &r) == 0) {
				printf("%s: %.2f C (status %d)   ", config->sensors[i].id,
				       (double)(r.temperature_kelvin - 273.15f), r.status);
			}
		}
		printf("\n");
		k_sleep(K_MSEC(500));
	}
	return 0;
}
