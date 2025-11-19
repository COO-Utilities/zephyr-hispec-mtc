/**
 * @file config.h
 * @brief Thermal controller configuration structures
 *
 * These structures match the ThermalControllerConfig.yaml schema
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* These numbers are not permanent */
#define MAX_SENSORS 16
#define MAX_HEATERS 16
#define MAX_CONTROL_LOOPS 8
#define MAX_SENSORS_PER_LOOP 4
#define MAX_HEATERS_PER_LOOP 4
#define MAX_ID_LENGTH 32
#define MAX_LOCATION_LENGTH 64
#define MAX_PATH_LENGTH 128

/**
 * Controller operating mode
 */
typedef enum {
    CONTROLLER_MODE_AUTO,
    CONTROLLER_MODE_MANUAL,
    CONTROLLER_MODE_OFF
} controller_mode_t;

/**
 * Temperature units
 */
typedef enum {
    UNIT_CELSIUS,
    UNIT_FAHRENHEIT,
    UNIT_KELVIN
} temp_unit_t;

/**
 * Error condition handling
 */
typedef enum {
    ERROR_CONDITION_STOP,
    ERROR_CONDITION_ALARM,
    ERROR_CONDITION_IGNORE_INVALID_SENSORS,
    ERROR_CONDITION_CONTINUE_LAST_GOOD
} error_condition_t;

/**
 * Sensor types
 */
typedef enum {
    SENSOR_TYPE_P_RTD       // Penguin RTD sensors
} sensor_type_t;

/**
 * Heater types
 */
typedef enum {
    HEATER_TYPE_LOW_POWER,
    HEATER_TYPE_HIGH_POWER
} heater_type_t;

/**
 * Control algorithms
 */
typedef enum {
    CONTROL_ALGO_PID,
    CONTROL_ALGO_ON_OFF,
    CONTROL_ALGO_POWER_LEVEL
} control_algo_t;

/**
 * Calibration extrapolation methods
 */
typedef enum {
    EXTRAP_NONE,
    EXTRAP_POLY,
    EXTRAP_LINEAR
} extrap_method_t;

/**
 * Sensor configuration
 */
typedef struct {
    char id[MAX_ID_LENGTH];
    sensor_type_t type;
    char location[MAX_LOCATION_LENGTH];
    float default_value;              // Resistance (RTD) or Voltage (diode/TC)
    float temperature_at_default;     // Temperature in Kelvin
    float temperature_coefficient;    // dR/dT or dV/dT
    char calibration_file[MAX_PATH_LENGTH];
    extrap_method_t extrapolate_method;
    bool enabled;
} sensor_config_t;

/**
 * Heater configuration
 */
typedef struct {
    char id[MAX_ID_LENGTH];
    heater_type_t type;
    char location[MAX_LOCATION_LENGTH];
    float max_power_w;
    bool enabled;
} heater_config_t;

/**
 * Control loop configuration
 */
typedef struct {
    char id[MAX_ID_LENGTH];
    char sensor_ids[MAX_SENSORS_PER_LOOP][MAX_ID_LENGTH];
    int num_sensors;
    char heater_ids[MAX_HEATERS_PER_LOOP][MAX_ID_LENGTH];
    int num_heaters;

    float default_target_temperature;  // In Kelvin
    bool default_state_on;

    control_algo_t control_algorithm;
    float p_gain;
    float i_gain;
    float d_gain;

    error_condition_t error_condition;
    float threshold_for_invalid_sensors;  // Degrees

    float alarm_min_temp;  // Kelvin
    float alarm_max_temp;  // Kelvin

    float valid_setpoint_range_min;  // Kelvin
    float valid_setpoint_range_max;  // Kelvin
    float setpoint_change_rate_limit;  // Degrees per minute

    float heater_power_limit_min;  // Watts
    float heater_power_limit_max;  // Watts

    char follows_loop_id[MAX_ID_LENGTH];
    float follows_loop_scalar;

    bool enabled;
} control_loop_config_t;

/**
 * Main controller configuration
 */
typedef struct {
    char id[MAX_ID_LENGTH];
    controller_mode_t mode;
    temp_unit_t units;

    int number_of_sensors;
    int number_of_heaters;
    int number_of_control_loops;

    uint32_t timeout_seconds;
    error_condition_t timeout_error_condition;

    sensor_config_t sensors[MAX_SENSORS];
    heater_config_t heaters[MAX_HEATERS];
    control_loop_config_t control_loops[MAX_CONTROL_LOOPS];
} thermal_config_t;

/**
 * Load default configuration
 * @return Pointer to static config structure
 */
thermal_config_t* config_load_defaults(void);

/**
 * Validate configuration for consistency
 * @param config Configuration to validate
 * @return 0 on success, negative error code on failure
 */
int config_validate(const thermal_config_t *config);

/**
 * Find sensor by ID
 * @param config Configuration
 * @param id Sensor ID to find
 * @return Pointer to sensor config, or NULL if not found
 */
sensor_config_t* config_find_sensor(thermal_config_t *config, const char *id);

/**
 * Find heater by ID
 * @param config Configuration
 * @param id Heater ID to find
 * @return Pointer to heater config, or NULL if not found
 */
heater_config_t* config_find_heater(thermal_config_t *config, const char *id);

/**
 * Find control loop by ID
 * @param config Configuration
 * @param id Loop ID to find
 * @return Pointer to loop config, or NULL if not found
 */
control_loop_config_t* config_find_loop(thermal_config_t *config, const char *id);

#endif /* CONFIG_H */
