/**
 * @file config.c
 * @brief Configuration management implementation
 */

#include "config.h"
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(config, LOG_LEVEL_INF);

/* Static configuration instance */
static thermal_config_t default_config;

thermal_config_t* config_load_defaults(void)
{
    memset(&default_config, 0, sizeof(thermal_config_t));

    /* Controller settings */
    strncpy(default_config.id, "tc-01", MAX_ID_LENGTH - 1);
    default_config.mode = CONTROLLER_MODE_AUTO;
    default_config.units = UNIT_KELVIN;
    default_config.number_of_sensors = 1;
    default_config.number_of_heaters = 2;
    default_config.number_of_control_loops = 2;
    default_config.timeout_seconds = 10;
    default_config.timeout_error_condition = ERROR_CONDITION_ALARM;

    /* Sensor 1: Penguin RTD (currently using AD7124 internal temp for testing) */
    strncpy(default_config.sensors[0].id, "sensor-1", MAX_ID_LENGTH - 1);
    default_config.sensors[0].type = SENSOR_TYPE_P_RTD;
    strncpy(default_config.sensors[0].location, "test", MAX_LOCATION_LENGTH - 1);
    default_config.sensors[0].default_value = 1000.0f;
    default_config.sensors[0].temperature_at_default = 273.15f;
    default_config.sensors[0].temperature_coefficient = 0.00385f;
    strncpy(default_config.sensors[0].calibration_file, "null", MAX_PATH_LENGTH - 1);
    default_config.sensors[0].extrapolate_method = EXTRAP_NONE;
    default_config.sensors[0].enabled = true;

    /* Heater 1: High power */
    strncpy(default_config.heaters[0].id, "heater-1", MAX_ID_LENGTH - 1);
    default_config.heaters[0].type = HEATER_TYPE_HIGH_POWER;
    strncpy(default_config.heaters[0].location, "inlet", MAX_LOCATION_LENGTH - 1);
    default_config.heaters[0].max_power_w = 50.0f;
    default_config.heaters[0].resistance_ohms = 30.0f;
    default_config.heaters[0].enabled = true;

    /* Heater 2: Low power */
    strncpy(default_config.heaters[1].id, "heater-2", MAX_ID_LENGTH - 1);
    default_config.heaters[1].type = HEATER_TYPE_LOW_POWER;
    strncpy(default_config.heaters[1].location, "outlet", MAX_LOCATION_LENGTH - 1);
    default_config.heaters[1].max_power_w = 50.0f;
    default_config.heaters[1].resistance_ohms = 10.0f;
    default_config.heaters[1].enabled = true;

    /* Control Loop 1 */
    strncpy(default_config.control_loops[0].id, "loop-1", MAX_ID_LENGTH - 1);
    strncpy(default_config.control_loops[0].sensor_ids[0], "sensor-2", MAX_ID_LENGTH - 1);
    default_config.control_loops[0].num_sensors = 1;
    strncpy(default_config.control_loops[0].heater_ids[0], "heater-2", MAX_ID_LENGTH - 1);
    default_config.control_loops[0].num_heaters = 1;
    default_config.control_loops[0].default_target_temperature = 308.15f;  // 35°C
    default_config.control_loops[0].default_state_on = true;
    default_config.control_loops[0].control_algorithm = CONTROL_ALGO_PID;
    default_config.control_loops[0].p_gain = 2.0f;
    default_config.control_loops[0].i_gain = 0.5f;
    default_config.control_loops[0].d_gain = 0.1f;
    default_config.control_loops[0].error_condition = ERROR_CONDITION_STOP;
    default_config.control_loops[0].threshold_for_invalid_sensors = 50.0f;
    default_config.control_loops[0].alarm_min_temp = 273.15f;  // 0°C
    default_config.control_loops[0].alarm_max_temp = 353.15f;  // 80°C
    default_config.control_loops[0].valid_setpoint_range_min = 293.15f;  // 20°C
    default_config.control_loops[0].valid_setpoint_range_max = 303.15f;  // 30°C
    default_config.control_loops[0].setpoint_change_rate_limit = 1.0f;  // K/min
    default_config.control_loops[0].heater_power_limit_min = 0.0f;
    default_config.control_loops[0].heater_power_limit_max = 50.0f;
    default_config.control_loops[0].follows_loop_id[0] = '\0';  // Not following
    default_config.control_loops[0].follows_loop_scalar = 1.0f;
    default_config.control_loops[0].enabled = false;

    /* Control Loop 2: Follows Loop 1 */
    strncpy(default_config.control_loops[1].id, "loop-2", MAX_ID_LENGTH - 1);
    strncpy(default_config.control_loops[1].sensor_ids[0], "sensor-1", MAX_ID_LENGTH - 1);
    default_config.control_loops[1].num_sensors = 1;
    strncpy(default_config.control_loops[1].heater_ids[0], "heater-1", MAX_ID_LENGTH - 1);
    default_config.control_loops[1].num_heaters = 1;
    default_config.control_loops[1].default_target_temperature = 313.15f;  // 40°C
    default_config.control_loops[1].default_state_on = true;
    default_config.control_loops[1].control_algorithm = CONTROL_ALGO_PID;
    default_config.control_loops[1].p_gain = 2.0f;
    default_config.control_loops[1].i_gain = 0.5f;
    default_config.control_loops[1].d_gain = 0.1f;
    default_config.control_loops[1].error_condition = ERROR_CONDITION_STOP;
    default_config.control_loops[1].threshold_for_invalid_sensors = 50.0f;
    default_config.control_loops[1].alarm_min_temp = 273.15f;  // 0°C
    default_config.control_loops[1].alarm_max_temp = 353.15f;  // 80°C
    default_config.control_loops[1].valid_setpoint_range_min = 293.15f;
    default_config.control_loops[1].valid_setpoint_range_max = 303.15f;
    default_config.control_loops[1].setpoint_change_rate_limit = 1.0f;
    default_config.control_loops[1].heater_power_limit_min = 0.0f;
    default_config.control_loops[1].heater_power_limit_max = 50.0f;
    default_config.control_loops[1].follows_loop_id[0] = '\0';  // Not following
    default_config.control_loops[1].follows_loop_scalar = 1.0f;
    default_config.control_loops[1].enabled = true;

    LOG_INF("Loaded default configuration");
    return &default_config;
}

int config_validate(const thermal_config_t *config)
{
    if (config == NULL) {
        LOG_ERR("Config is NULL");
        return -1;
    }

    /* Validate sensor count */
    if (config->number_of_sensors > MAX_SENSORS) {
        LOG_ERR("Too many sensors: %d (max %d)", config->number_of_sensors, MAX_SENSORS);
        return -2;
    }

    /* Validate heater count */
    if (config->number_of_heaters > MAX_HEATERS) {
        LOG_ERR("Too many heaters: %d (max %d)", config->number_of_heaters, MAX_HEATERS);
        return -3;
    }

    /* Validate control loop count */
    if (config->number_of_control_loops > MAX_CONTROL_LOOPS) {
        LOG_ERR("Too many control loops: %d (max %d)",
                config->number_of_control_loops, MAX_CONTROL_LOOPS);
        return -4;
    }

    /* Validate that all loop sensor/heater IDs exist */
    for (int i = 0; i < config->number_of_control_loops; i++) {
        const control_loop_config_t *loop = &config->control_loops[i];

        if (!loop->enabled) {
            continue;
        }

        /* Check sensor IDs */
        for (int j = 0; j < loop->num_sensors; j++) {
            bool found = false;
            for (int k = 0; k < config->number_of_sensors; k++) {
                if (strcmp(loop->sensor_ids[j], config->sensors[k].id) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                LOG_ERR("Loop %s references unknown sensor %s", loop->id, loop->sensor_ids[j]);
                return -5;
            }
        }

        /* Check heater IDs */
        for (int j = 0; j < loop->num_heaters; j++) {
            bool found = false;
            for (int k = 0; k < config->number_of_heaters; k++) {
                if (strcmp(loop->heater_ids[j], config->heaters[k].id) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                LOG_ERR("Loop %s references unknown heater %s", loop->id, loop->heater_ids[j]);
                return -6;
            }
        }

        /* Check for circular follows dependencies (simple check) */
        if (strlen(loop->follows_loop_id) > 0) {
            if (strcmp(loop->follows_loop_id, loop->id) == 0) {
                LOG_ERR("Loop %s follows itself", loop->id);
                return -7;
            }
        }
    }

    LOG_INF("Configuration validated successfully");
    return 0;
}

sensor_config_t* config_find_sensor(thermal_config_t *config, const char *id)
{
    for (int i = 0; i < config->number_of_sensors; i++) {
        if (strcmp(config->sensors[i].id, id) == 0) {
            return &config->sensors[i];
        }
    }
    return NULL;
}

heater_config_t* config_find_heater(thermal_config_t *config, const char *id)
{
    for (int i = 0; i < config->number_of_heaters; i++) {
        if (strcmp(config->heaters[i].id, id) == 0) {
            return &config->heaters[i];
        }
    }
    return NULL;
}

control_loop_config_t* config_find_loop(thermal_config_t *config, const char *id)
{
    for (int i = 0; i < config->number_of_control_loops; i++) {
        if (strcmp(config->control_loops[i].id, id) == 0) {
            return &config->control_loops[i];
        }
    }
    return NULL;
}
