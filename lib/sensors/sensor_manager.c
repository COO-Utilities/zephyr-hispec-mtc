/**
 * @file sensor_manager.c
 * @brief Multi-sensor management implementation
 */

#include "sensor_manager.h"
#include "adc_temp_sensor.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(sensor_manager, LOG_LEVEL_INF);

/* Maximum number of sensors we can manage */
#define MAX_MANAGED_SENSORS 16

/* Sensor reading cache */
static struct {
    char id[MAX_ID_LENGTH];
    sensor_reading_t reading;
    bool valid;
} sensor_cache[MAX_MANAGED_SENSORS];

static int num_sensors = 0;
static const thermal_config_t *config_ptr = NULL;

/* Thread-safe mutex for sensor readings */
K_MUTEX_DEFINE(sensor_mutex);

int sensor_manager_init(const thermal_config_t *config)
{
    if (config == NULL) {
        LOG_ERR("Config is NULL");
        return -1;
    }

    config_ptr = config;
    num_sensors = config->number_of_sensors;

    if (num_sensors > MAX_MANAGED_SENSORS) {
        LOG_ERR("Too many sensors: %d (max %d)", num_sensors, MAX_MANAGED_SENSORS);
        return -2;
    }

    /* Initialize sensor cache */
    memset(sensor_cache, 0, sizeof(sensor_cache));
    for (int i = 0; i < num_sensors; i++) {
        strncpy(sensor_cache[i].id, config->sensors[i].id, MAX_ID_LENGTH - 1);
        sensor_cache[i].valid = false;
    }

    /* Initialize hardware drivers */
    /* For now, we only have ADC temp sensor */
    int ret = adc_temp_sensor_init(config);
    if (ret != 0) {
        LOG_ERR("ADC temp sensor init failed: %d", ret);
        return ret;
    }

    LOG_INF("Sensor manager initialized with %d sensors", num_sensors);
    return 0;
}

int sensor_manager_read_all(void)
{
    int errors = 0;

    k_mutex_lock(&sensor_mutex, K_FOREVER);

    for (int i = 0; i < num_sensors; i++) {
        if (!config_ptr->sensors[i].enabled) {
            continue;
        }

        const char *sensor_id = config_ptr->sensors[i].id;
        float temp_k = 0.0f;

        /* Read from appropriate driver based on sensor type */
        /* For now, all sensors use ADC temp sensor */
        int ret = adc_temp_sensor_read(sensor_id, &temp_k);

        if (ret == 0) {
            sensor_cache[i].reading.temperature_kelvin = temp_k;
            sensor_cache[i].reading.timestamp_ms = k_uptime_get();
            sensor_cache[i].reading.status = SENSOR_STATUS_OK;
            sensor_cache[i].valid = true;
        } else {
            sensor_cache[i].reading.status = SENSOR_STATUS_READ_ERROR;
            sensor_cache[i].valid = false;
            errors++;
            LOG_WRN("Failed to read sensor %s: %d", sensor_id, ret);
        }
    }

    k_mutex_unlock(&sensor_mutex);

    return (errors > 0) ? -errors : 0;
}

int sensor_manager_get_reading(const char *sensor_id, sensor_reading_t *reading)
{
    if (sensor_id == NULL || reading == NULL) {
        return -1;
    }

    k_mutex_lock(&sensor_mutex, K_FOREVER);

    /* Find sensor in cache */
    int idx = -1;
    for (int i = 0; i < num_sensors; i++) {
        if (strcmp(sensor_cache[i].id, sensor_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&sensor_mutex);
        LOG_ERR("Sensor %s not found", sensor_id);
        return -2;
    }

    if (!sensor_cache[idx].valid) {
        k_mutex_unlock(&sensor_mutex);
        return -3;
    }

    /* Copy reading */
    memcpy(reading, &sensor_cache[idx].reading, sizeof(sensor_reading_t));

    k_mutex_unlock(&sensor_mutex);
    return 0;
}

int sensor_manager_get_average(const char *sensor_ids[], int num_sensors_to_avg, float *avg_temp)
{
    if (sensor_ids == NULL || avg_temp == NULL || num_sensors_to_avg <= 0) {
        return -1;
    }

    float sum = 0.0f;
    int valid_count = 0;

    k_mutex_lock(&sensor_mutex, K_FOREVER);

    for (int i = 0; i < num_sensors_to_avg; i++) {
        /* Find sensor in cache */
        int idx = -1;
        for (int j = 0; j < num_sensors; j++) {
            if (strcmp(sensor_cache[j].id, sensor_ids[i]) == 0) {
                idx = j;
                break;
            }
        }

        if (idx >= 0 && sensor_cache[idx].valid) {
            sum += sensor_cache[idx].reading.temperature_kelvin;
            valid_count++;
        }
    }

    k_mutex_unlock(&sensor_mutex);

    if (valid_count == 0) {
        LOG_WRN("No valid sensors for averaging");
        return -2;
    }

    *avg_temp = sum / (float)valid_count;
    return 0;
}

bool sensor_manager_is_valid(const char *sensor_id)
{
    if (sensor_id == NULL) {
        return false;
    }

    k_mutex_lock(&sensor_mutex, K_FOREVER);

    bool valid = false;
    for (int i = 0; i < num_sensors; i++) {
        if (strcmp(sensor_cache[i].id, sensor_id) == 0) {
            valid = sensor_cache[i].valid;
            break;
        }
    }

    k_mutex_unlock(&sensor_mutex);
    return valid;
}
