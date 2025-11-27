/**
 * @file sensor_manager.h
 * @brief Multi-sensor management and coordination
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "../config/config.h"
#include <stdint.h>

/**
 * Sensor reading status
 */
typedef enum {
    SENSOR_STATUS_OK = 0,
    SENSOR_STATUS_NOT_READY = -1,
    SENSOR_STATUS_READ_ERROR = -2,
    SENSOR_STATUS_OUT_OF_RANGE = -3,
    SENSOR_STATUS_DISCONNECTED = -4
} sensor_status_t;

/**
 * Sensor reading structure
 */
typedef struct {
    float temperature_kelvin;
    int64_t timestamp_ms;
    sensor_status_t status;
} sensor_reading_t;

/**
 * Initialize sensor manager
 * @param config Pointer to thermal configuration
 * @return 0 on success, negative error code on failure
 */
int sensor_manager_init(const thermal_config_t *config);

/**
 * Read all enabled sensors
 * Called periodically by sensor thread
 * @return 0 on success, negative if any sensor failed
 */
int sensor_manager_read_all(void);

/**
 * Get latest reading for a specific sensor
 * @param sensor_id Sensor ID string
 * @param reading Pointer to store reading
 * @return 0 on success, negative error code on failure
 */
int sensor_manager_get_reading(const char *sensor_id, sensor_reading_t *reading);

/**
 * Get average temperature from multiple sensors
 * @param sensor_ids Array of sensor ID strings
 * @param num_sensors Number of sensors to average
 * @param avg_temp Pointer to store average temperature
 * @return 0 on success, negative error code on failure
 */
int sensor_manager_get_average(const char *sensor_ids[], int num_sensors, float *avg_temp);

/**
 * Check if a sensor reading is valid
 * @param sensor_id Sensor ID string
 * @return true if valid, false otherwise
 */
bool sensor_manager_is_valid(const char *sensor_id);

#endif /* SENSOR_MANAGER_H */
