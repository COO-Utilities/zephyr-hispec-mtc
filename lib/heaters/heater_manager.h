/**
 * @file heater_manager.h
 * @brief Multi-heater management and control
 */

#ifndef HEATER_MANAGER_H
#define HEATER_MANAGER_H

#include "../config/config.h"
#include <stdbool.h>

/**
 * Heater status
 */
typedef enum {
    HEATER_STATUS_OK = 0,
    HEATER_STATUS_NOT_READY = -1,
    HEATER_STATUS_ERROR = -2,
    HEATER_STATUS_DISABLED = -3,
    HEATER_STATUS_OVER_LIMIT = -4
} heater_status_t;

/**
 * Initialize heater manager
 * @param config Pointer to thermal configuration
 * @return 0 on success, negative error code on failure
 */
int heater_manager_init(const thermal_config_t *config);

/**
 * Set power level for a specific heater
 * @param heater_id Heater ID string
 * @param power_percent Power level 0.0-100.0%
 * @return 0 on success, negative error code on failure
 */
int heater_manager_set_power(const char *heater_id, float power_percent);

/**
 * Distribute power across multiple heaters
 * Proportionally distributes total power among heaters
 * @param heater_ids Array of heater ID strings
 * @param num_heaters Number of heaters
 * @param total_power_watts Total power to distribute in watts
 * @return 0 on success, negative error code on failure
 */
int heater_manager_distribute_power(const char *heater_ids[], int num_heaters,
                                     float total_power_watts);

/**
 * Emergency stop: turn off all heaters immediately
 * @return 0 on success, negative error code on failure
 */
int heater_manager_emergency_stop(void);

/**
 * Get current power level for a heater
 * @param heater_id Heater ID string
 * @param power_percent Pointer to store power level
 * @return 0 on success, negative error code on failure
 */
int heater_manager_get_power(const char *heater_id, float *power_percent);

/**
 * Get heater status
 * @param heater_id Heater ID string
 * @return heater_status_t status code
 */
heater_status_t heater_manager_get_status(const char *heater_id);

#endif /* HEATER_MANAGER_H */
