/**
 * @file main.c
 * @brief PID Closed-Loop Control Demo
 *
 * Demonstrates closed-loop temperature control using:
 * - AD7124 ADC for temperature sensing (RTD)
 * - TPS55287Q1 regulator for heater control
 * - PID control algorithm from lib/control
 *
 * The demo:
 * 1. Initializes sensor, heater, and control loop subsystems
 * 2. Sets a target temperature (setpoint)
 * 3. Runs the PID control loop to maintain temperature
 * 4. Logs sensor readings, heater power, and PID state
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <stdio.h>

#include <config.h>
#include <sensor_manager.h>
#include <heater_manager.h>
#include <control_loop.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Control loop timing */
#define CONTROL_LOOP_PERIOD_MS  500
#define CONTROL_LOOP_DT_SECONDS (CONTROL_LOOP_PERIOD_MS / 1000.0f)

/* PID gains - tune these for your system */
#define PID_KP  5.0f   /* Proportional gain */
#define PID_KI  0.1f   /* Integral gain */
#define PID_KD  1.0f   /* Derivative gain */

/* Target temperature in Celsius */
#define TARGET_TEMP_C  30.0f

/* Power limits (0-100%) */
#define POWER_LIMIT_MIN  0.0f
#define POWER_LIMIT_MAX  50.0f  /* Limit to 50% for safety during testing */

/* Alarm thresholds in Celsius */
#define ALARM_MIN_TEMP_C  0.0f
#define ALARM_MAX_TEMP_C  80.0f

/* Conversion helper */
#define C_TO_K(c) ((c) + 273.15f)

/* ADC spec for sensor */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
static const struct adc_dt_spec sensor_adc_spec = ADC_DT_SPEC_GET(DT_ALIAS(sensor_test));
#endif

/* IDs used in this demo */
#define SENSOR_ID    "sensor-1"
#define HEATER_ID    "high-power-1"
#define LOOP_ID      "pid-loop-1"

int main(void)
{
    int ret;

    LOG_INF("===========================================");
    LOG_INF("PID Closed-Loop Control Demo Starting");
    LOG_INF("===========================================");

    /* Load default configuration */
    thermal_config_t *config = config_load_defaults();
    if (!config) {
        LOG_ERR("Failed to load configuration");
        return -1;
    }

    /*
     * Configure Sensor
     * Use defaults (sensor-1 is already configured as P_RTD with correct params)
     * Just assign the ADC driver
     */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
    sensor_config_t *sensor = config_find_sensor(config, SENSOR_ID);
    if (sensor) {
        sensor->driver_data = &sensor_adc_spec;
    } else {
        LOG_ERR("Sensor %s not found in config", SENSOR_ID);
        return -1;
    }
#endif

    /*
     * Configure Heater
     */
    config->number_of_heaters = 1;
    strncpy(config->heaters[0].id, HEATER_ID, MAX_ID_LENGTH - 1);
    config->heaters[0].type = HEATER_TYPE_HIGH_POWER;
    config->heaters[0].max_power_w = 40.0f;
    config->heaters[0].resistance_ohms = 30.0f;
    config->heaters[0].regulator_dev = DEVICE_DT_GET(DT_ALIAS(high_current_heater_test));
    config->heaters[0].enabled = true;

    /*
     * Configure Control Loop
     */
    config->number_of_control_loops = 1;
    strncpy(config->control_loops[0].id, LOOP_ID, MAX_ID_LENGTH - 1);

    /* Assign sensor to loop */
    config->control_loops[0].num_sensors = 1;
    strncpy(config->control_loops[0].sensor_ids[0], SENSOR_ID, MAX_ID_LENGTH - 1);

    /* Assign heater to loop */
    config->control_loops[0].num_heaters = 1;
    strncpy(config->control_loops[0].heater_ids[0], HEATER_ID, MAX_ID_LENGTH - 1);

    /* PID parameters */
    config->control_loops[0].control_algorithm = CONTROL_ALGO_PID;
    config->control_loops[0].p_gain = PID_KP;
    config->control_loops[0].i_gain = PID_KI;
    config->control_loops[0].d_gain = PID_KD;

    /* Setpoint and state */
    config->control_loops[0].default_target_temperature = C_TO_K(TARGET_TEMP_C);
    config->control_loops[0].default_state_on = true;
    config->control_loops[0].enabled = true;

    /* Power limits */
    config->control_loops[0].heater_power_limit_min = POWER_LIMIT_MIN;
    config->control_loops[0].heater_power_limit_max = POWER_LIMIT_MAX;

    /* Alarm thresholds */
    config->control_loops[0].alarm_min_temp = C_TO_K(ALARM_MIN_TEMP_C);
    config->control_loops[0].alarm_max_temp = C_TO_K(ALARM_MAX_TEMP_C);

    /* No loop following */
    config->control_loops[0].follows_loop_id[0] = '\0';
    config->control_loops[0].follows_loop_scalar = 1.0f;

    /* Error handling */
    config->control_loops[0].error_condition = ERROR_CONDITION_ALARM;

    /*
     * Initialize Subsystems
     */
    LOG_INF("Initializing heater manager...");
    ret = heater_manager_init(config);
    if (ret < 0) {
        LOG_ERR("Failed to initialize heater manager: %d", ret);
        return -1;
    }

    /* Start with heater off */
    heater_manager_set_power(HEATER_ID, 0.0f);

    LOG_INF("Initializing sensor manager...");
    ret = sensor_manager_init(config);
    if (ret != 0) {
        LOG_ERR("Failed to initialize sensor manager: %d", ret);
        return -1;
    }

    LOG_INF("Initializing control loop...");
    ret = control_loop_init(config);
    if (ret != 0) {
        LOG_ERR("Failed to initialize control loop: %d", ret);
        return -1;
    }

    LOG_INF("-------------------------------------------");
    LOG_INF("Configuration:");
    LOG_INF("  Target Temperature: %.2f C",
            (double)TARGET_TEMP_C);
    LOG_INF("  PID Gains: Kp=%.2f, Ki=%.2f, Kd=%.2f",
            (double)PID_KP, (double)PID_KI, (double)PID_KD);
    LOG_INF("  Power Limits: %.1f%% - %.1f%%",
            (double)POWER_LIMIT_MIN, (double)POWER_LIMIT_MAX);
    LOG_INF("  Loop Period: %d ms", CONTROL_LOOP_PERIOD_MS);
    LOG_INF("-------------------------------------------");

    /*
     * Main Control Loop
     */
    LOG_INF("Starting PID control loop...");
    LOG_INF("Press reset to stop.");
    LOG_INF("");

    int iteration = 0;

    while (1) {
        /* Read all sensors */
        ret = sensor_manager_read_all();
        if (ret < 0) {
            LOG_WRN("Sensor read errors: %d", -ret);
        }

        /* Run PID control loop update */
        ret = control_loop_update_all(CONTROL_LOOP_DT_SECONDS);
        if (ret < 0) {
            LOG_WRN("Control loop errors: %d", -ret);
        }

        /* Log status every 2 seconds (4 iterations at 500ms) */
        if (iteration % 4 == 0) {
            sensor_reading_t reading;
            float heater_power = 0.0f;
            float target = 0.0f;

            sensor_manager_get_reading(SENSOR_ID, &reading);
            heater_manager_get_power(HEATER_ID, &heater_power);
            control_loop_get_target(LOOP_ID, &target);

            float error = target - reading.temperature_kelvin;

            printf("[%6d] T=%.2f K (%.2f C) | SP=%.2f K | Err=%.2f | Pwr=%.1f%%\n",
                   iteration,
                   (double)reading.temperature_kelvin,
                   (double)(reading.temperature_kelvin - 273.15f),
                   (double)target,
                   (double)error,
                   (double)heater_power);
        }

        iteration++;
        k_msleep(CONTROL_LOOP_PERIOD_MS);
    }

    return 0;
}
