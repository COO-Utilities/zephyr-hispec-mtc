/**
 * @file heater_manager.c
 * @brief Multi-heater management implementation (skeleton)
 */

#include "heater_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(heater_manager, LOG_LEVEL_INF);

#define MAX_MANAGED_HEATERS 16

/* Heater state */
static struct {
    char id[MAX_ID_LENGTH];
    float power_percent;
    float max_power_watts;
    heater_status_t status;
    bool enabled;
} heater_state[MAX_MANAGED_HEATERS];

static int num_heaters = 0;
static const thermal_config_t *config_ptr = NULL;

/* Thread-safe mutex for heater control */
K_MUTEX_DEFINE(heater_mutex);

int heater_manager_init(const thermal_config_t *config)
{
    if (config == NULL) {
        LOG_ERR("Config is NULL");
        return -1;
    }

    config_ptr = config;
    num_heaters = config->number_of_heaters;

    if (num_heaters > MAX_MANAGED_HEATERS) {
        LOG_ERR("Too many heaters: %d (max %d)", num_heaters, MAX_MANAGED_HEATERS);
        return -2;
    }

    /* Initialize heater state */
    memset(heater_state, 0, sizeof(heater_state));
    for (int i = 0; i < num_heaters; i++) {
        strncpy(heater_state[i].id, config->heaters[i].id, MAX_ID_LENGTH - 1);
        heater_state[i].power_percent = 0.0f;
        heater_state[i].max_power_watts = config->heaters[i].max_power_w;
        heater_state[i].enabled = config->heaters[i].enabled;
        heater_state[i].status = config->heaters[i].enabled ?
                                  HEATER_STATUS_OK : HEATER_STATUS_DISABLED;
    }

    /* TODO: Initialize heater hardware drivers */
    /* Start with:
     * 1. TPS55287Q1 I2C driver for high-power heaters
     * 2. Low-power heater driver
     * 3. Setting initial output to 0%
     */

    LOG_INF("Heater manager initialized with %d heaters (hardware drivers not yet implemented)",
            num_heaters);
    return 0;
}

int heater_manager_set_power(const char *heater_id, float power_percent)
{
    if (heater_id == NULL) {
        return -1;
    }

    /* Clamp to valid range */
    if (power_percent < 0.0f) {
        power_percent = 0.0f;
    }
    if (power_percent > 100.0f) {
        power_percent = 100.0f;
    }

    k_mutex_lock(&heater_mutex, K_FOREVER);

    /* Find heater */
    int idx = -1;
    for (int i = 0; i < num_heaters; i++) {
        if (strcmp(heater_state[i].id, heater_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&heater_mutex);
        LOG_ERR("Heater %s not found", heater_id);
        return -2;
    }

    if (!heater_state[idx].enabled) {
        k_mutex_unlock(&heater_mutex);
        LOG_WRN("Heater %s is disabled", heater_id);
        return -3;
    }

    /* Update power level */
    heater_state[idx].power_percent = power_percent;

    /* TODO: Set hardware output based on power_percent */
    /* For now, just log */
    LOG_DBG("Heater %s power set to %.1f%%", heater_id, power_percent);

    k_mutex_unlock(&heater_mutex);
    return 0;
}

int heater_manager_distribute_power(const char *heater_ids[], int num_heaters_to_use,
                                     float total_power_watts)
{
    if (heater_ids == NULL || num_heaters_to_use <= 0) {
        return -1;
    }

    /* Calculate total max power available */
    float total_max_power = 0.0f;
    for (int i = 0; i < num_heaters_to_use; i++) {
        for (int j = 0; j < num_heaters; j++) {
            if (strcmp(heater_state[j].id, heater_ids[i]) == 0) {
                total_max_power += heater_state[j].max_power_watts;
                break;
            }
        }
    }

    if (total_max_power <= 0.0f) {
        LOG_ERR("No heater capacity available");
        return -2;
    }

    /* Clamp total power */
    if (total_power_watts > total_max_power) {
        LOG_WRN("Requested power %.1fW exceeds max %.1fW, clamping",
                total_power_watts, total_max_power);
        total_power_watts = total_max_power;
    }
    if (total_power_watts < 0.0f) {
        total_power_watts = 0.0f;
    }

    /* Distribute proportionally */
    for (int i = 0; i < num_heaters_to_use; i++) {
        for (int j = 0; j < num_heaters; j++) {
            if (strcmp(heater_state[j].id, heater_ids[i]) == 0) {
                float fraction = heater_state[j].max_power_watts / total_max_power;
                float heater_power = total_power_watts * fraction;
                float power_percent = (heater_power / heater_state[j].max_power_watts) * 100.0f;
                heater_manager_set_power(heater_ids[i], power_percent);
                break;
            }
        }
    }

    return 0;
}

int heater_manager_emergency_stop(void)
{
    LOG_WRN("EMERGENCY STOP - Disabling all heaters!");

    k_mutex_lock(&heater_mutex, K_FOREVER);

    for (int i = 0; i < num_heaters; i++) {
        heater_state[i].power_percent = 0.0f;
        /* TODO: Set hardware output to 0% immediately */
    }

    k_mutex_unlock(&heater_mutex);

    LOG_INF("All heaters stopped");
    return 0;
}

int heater_manager_get_power(const char *heater_id, float *power_percent)
{
    if (heater_id == NULL || power_percent == NULL) {
        return -1;
    }

    k_mutex_lock(&heater_mutex, K_FOREVER);

    int idx = -1;
    for (int i = 0; i < num_heaters; i++) {
        if (strcmp(heater_state[i].id, heater_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&heater_mutex);
        return -2;
    }

    *power_percent = heater_state[idx].power_percent;

    k_mutex_unlock(&heater_mutex);
    return 0;
}

heater_status_t heater_manager_get_status(const char *heater_id)
{
    if (heater_id == NULL) {
        return HEATER_STATUS_ERROR;
    }

    k_mutex_lock(&heater_mutex, K_FOREVER);

    heater_status_t status = HEATER_STATUS_ERROR;
    for (int i = 0; i < num_heaters; i++) {
        if (strcmp(heater_state[i].id, heater_id) == 0) {
            status = heater_state[i].status;
            break;
        }
    }

    k_mutex_unlock(&heater_mutex);
    return status;
}
