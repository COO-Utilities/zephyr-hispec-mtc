/**
 * @file adc_temp_sensor.h
 * @brief AD7124 ADC temperature sensor driver
 *
 * This module handles communication with AD7124 ADCs for temperature measurement
 * Supports on-chip temperature sensor and external RTD/thermocouple inputs
 */

#ifndef ADC_TEMP_SENSOR_H
#define ADC_TEMP_SENSOR_H

#include <zephyr/drivers/spi.h>
#include "../config/config.h"

/**
 * AD7124 channel configuration
 */
typedef struct {
    uint8_t ainp;  // Positive analog input (0-15 for external, 16 for temp sensor)
    uint8_t ainm;  // Negative analog input (0-15 for external, 17 for AVSS)
    uint8_t pga;   // Programmable gain (1, 2, 4, 8, 16, 32, 64, 128)
} adc_channel_config_t;

/**
 * Initialize ADC temperature sensor subsystem
 * @param config Thermal configuration with sensor definitions
 * @return 0 on success, negative error code on failure
 */
int adc_temp_sensor_init(const thermal_config_t *config);

/**
 * Read temperature from a specific sensor
 * @param sensor_id Sensor ID string
 * @param temp_kelvin Pointer to store temperature in Kelvin
 * @return 0 on success, negative error code on failure
 */
int adc_temp_sensor_read(const char *sensor_id, float *temp_kelvin);

/**
 * Configure an ADC channel (advanced use)
 * @param sensor_id Sensor ID string
 * @param channel_config Channel configuration
 * @return 0 on success, negative error code on failure
 */
int adc_temp_sensor_configure_channel(const char *sensor_id,
                                      const adc_channel_config_t *channel_config);

/**
 * Check if ADC is ready for reading
 * @param sensor_id Sensor ID string
 * @return true if ready, false otherwise
 */
bool adc_temp_sensor_is_ready(const char *sensor_id);

#endif /* ADC_TEMP_SENSOR_H */
