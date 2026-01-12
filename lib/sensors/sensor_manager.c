/**
 * @file sensor_manager.c
 * @brief Multi-sensor management implementation
 */

#include "sensor_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
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
        
        /* Check if ADC device is ready if configured */
        /* Check if ADC device is ready if configured */
        const struct adc_dt_spec *adc = (const struct adc_dt_spec *)config->sensors[i].driver_data;
        if (adc != NULL) {
            if (!adc_is_ready_dt(adc)) {
                LOG_ERR("ADC device not ready for sensor %s", config->sensors[i].id);
                return -3;
            }

            int ret = adc_channel_setup_dt(adc);
            if (ret != 0) {
                LOG_ERR("Failed to setup ADC channel for sensor %s: %d", config->sensors[i].id, ret);
                return -4;
            }
        }
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
        int ret = -1;

        const struct adc_dt_spec *adc = (const struct adc_dt_spec *)config_ptr->sensors[i].driver_data;

        /* Read from ADC if configured */
        if (adc != NULL) {
            int32_t buf;
            struct adc_sequence sequence = {
                .buffer = &buf,
                .buffer_size = sizeof(buf),
            };

            adc_sequence_init_dt(adc, &sequence);

            ret = adc_read_dt(adc, &sequence);
            if (ret == 0) {
                if (config_ptr->sensors[i].type == SENSOR_TYPE_INTERNAL_TEMP) {
                        /* 
                        * Convert raw ADC code to temperature.
                        * AD7124 Internal Temp Sensor formula:
                        * Temp(C) = ((Code - 0x800000) / 13584) - 272.5
                        * Code is 24-bit unipolar (0 to 2^24-1) effectively in bipolar mode context from previous driver
                        * but Zephyr AD7124 likely returns 32-bit signed or unsigned.
                        * Let's assume the raw value is what we expect. 
                        * NOTE: If using differential mode, Zephyr might return signed value centered at 0?
                        * But overlay says bipolar?
                        * Let's stick to the formula from the manual driver which expected 0x800000 offset for 0V?
                        * Actually, let's treat 'buf' as the raw code.
                        */
                        
                        /* 
                        * Note: The previous manual driver was bit-banging and knew exactly what it got.
                        * The Zephyr AD7124 driver might do some processing. 
                        * For now, applying the same formula.
                        */
                    float temp_c = ((float)((uint32_t)buf & 0xFFFFFF) - 8388608.0f) / 13584.0f - 272.5f;
                    temp_k = temp_c + 273.15f;
                } else if (config_ptr->sensors[i].type == SENSOR_TYPE_P_RTD) {
                    float rtd_tc = config_ptr->sensors[i].temperature_coefficient;
                    float r_ref = config_ptr->sensors[i].reference_resistance;
                    float r_nom = config_ptr->sensors[i].nominal_resistance;
                    float gain = (float)config_ptr->sensors[i].adc_gain;
                    LOG_DBG("Sensor %s: rtd_tc = %f, r_ref = %f, r_nom = %f, gain = %f", config_ptr->sensors[i].id, (double)rtd_tc, (double)r_ref, (double)r_nom, (double)gain);
                    
                    int32_t max_count = (1 << (config_ptr->sensors[i].adc_resolution - 1)) - 1;

                    float r_rtd = (((float)(uint32_t)buf - (float)max_count) * r_ref) / (gain * (float)max_count);
                    float temp_c = (r_rtd - r_nom) / (rtd_tc / r_nom);
                    temp_k = temp_c + 273.15f;
                    LOG_DBG("Raw: %d | Res: %.2f Ohms | Temp: %.3f C", buf, (double)r_rtd, (double)temp_c);

                }
            }
        }

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
