/**
 * @file control_loop.c
 * @brief Multi-loop PID temperature control implementation
 */

#include "control_loop.h"
#include "../sensors/sensor_manager.h"
#include "../heaters/heater_manager.h"
#include <coo_commons/pid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(control_loop, LOG_LEVEL_INF);

#define MAX_CONTROL_LOOPS 8

/* Control loop runtime state */
static struct {
    char id[MAX_ID_LENGTH];
    struct coo_pid pid;  /* coo_commons PID controller */

    /* Configuration */
    char sensor_ids[MAX_SENSORS_PER_LOOP][MAX_ID_LENGTH];
    int num_sensors;
    char heater_ids[MAX_HEATERS_PER_LOOP][MAX_ID_LENGTH];
    int num_heaters;

    /* Setpoint management */
    float target_temp_kelvin;
    float current_setpoint;  /* For ramping */

    /* Alarm thresholds */
    float alarm_min_temp;
    float alarm_max_temp;

    /* Power limits */
    float power_limit_min;
    float power_limit_max;

    /* Loop following */
    char follows_loop_id[MAX_ID_LENGTH];
    float follows_scalar;

    /* Status */
    bool enabled;
    bool suspended;
    loop_status_t status;
} loop_state[MAX_CONTROL_LOOPS];

static int num_loops = 0;
static const thermal_config_t *config_ptr = NULL;

/* Thread-safe mutex */
K_MUTEX_DEFINE(control_mutex);

int control_loop_init(const thermal_config_t *config)
{
    if (config == NULL) {
        LOG_ERR("Config is NULL");
        return -1;
    }

    config_ptr = config;
    num_loops = config->number_of_control_loops;

    if (num_loops > MAX_CONTROL_LOOPS) {
        LOG_ERR("Too many control loops: %d (max %d)", num_loops, MAX_CONTROL_LOOPS);
        return -2;
    }

    /* Initialize each control loop */
    for (int i = 0; i < num_loops; i++) {
        const control_loop_config_t *cfg = &config->control_loops[i];

        /* Copy basic info */
        strncpy(loop_state[i].id, cfg->id, MAX_ID_LENGTH - 1);
        loop_state[i].enabled = cfg->enabled && cfg->default_state_on;
        loop_state[i].suspended = false;
        loop_state[i].status = LOOP_STATUS_OK;

        /* Copy sensor/heater IDs */
        loop_state[i].num_sensors = cfg->num_sensors;
        for (int j = 0; j < cfg->num_sensors; j++) {
            strncpy(loop_state[i].sensor_ids[j], cfg->sensor_ids[j], MAX_ID_LENGTH - 1);
        }

        loop_state[i].num_heaters = cfg->num_heaters;
        for (int j = 0; j < cfg->num_heaters; j++) {
            strncpy(loop_state[i].heater_ids[j], cfg->heater_ids[j], MAX_ID_LENGTH - 1);
        }

        /* Setpoint */
        loop_state[i].target_temp_kelvin = cfg->default_target_temperature;
        loop_state[i].current_setpoint = cfg->default_target_temperature;

        /* Alarms */
        loop_state[i].alarm_min_temp = cfg->alarm_min_temp;
        loop_state[i].alarm_max_temp = cfg->alarm_max_temp;

        /* Power limits */
        loop_state[i].power_limit_min = cfg->heater_power_limit_min;
        loop_state[i].power_limit_max = cfg->heater_power_limit_max;

        /* Loop following */
        strncpy(loop_state[i].follows_loop_id, cfg->follows_loop_id, MAX_ID_LENGTH - 1);
        loop_state[i].follows_scalar = cfg->follows_loop_scalar;

        /* Initialize PID controller using coo_commons */
        if (cfg->control_algorithm == CONTROL_ALGO_PID) {
            coo_pid_init(&loop_state[i].pid,
                         cfg->p_gain,
                         cfg->i_gain,
                         cfg->d_gain,
                         cfg->heater_power_limit_min,
                         cfg->heater_power_limit_max);

            LOG_INF("Loop %s: PID initialized (P=%.2f, I=%.2f, D=%.2f)",
                    cfg->id, cfg->p_gain, cfg->i_gain, cfg->d_gain);
        } else {
            LOG_WRN("Loop %s: Only PID algorithm supported currently", cfg->id);
        }
    }

    LOG_INF("Control loop subsystem initialized with %d loops", num_loops);
    return 0;
}

int control_loop_update_all(float dt_seconds)
{
    int errors = 0;

    k_mutex_lock(&control_mutex, K_FOREVER);

    for (int i = 0; i < num_loops; i++) {
        if (!loop_state[i].enabled || loop_state[i].suspended) {
            continue;
        }

        /* Read sensors and calculate average */
        float measured_temp = 0.0f;
        const char *sensor_ids_ptr[MAX_SENSORS_PER_LOOP];
        for (int j = 0; j < loop_state[i].num_sensors; j++) {
            sensor_ids_ptr[j] = loop_state[i].sensor_ids[j];
        }

        int ret = sensor_manager_get_average(sensor_ids_ptr,
                                              loop_state[i].num_sensors,
                                              &measured_temp);
        if (ret != 0) {
            loop_state[i].status = LOOP_STATUS_SENSOR_ERROR;
            LOG_WRN("Loop %s: Sensor read error", loop_state[i].id);
            errors++;
            continue;
        }

        /* Check alarm conditions */
        if (measured_temp < loop_state[i].alarm_min_temp ||
            measured_temp > loop_state[i].alarm_max_temp) {
            loop_state[i].status = LOOP_STATUS_ALARM;
            LOG_ERR("Loop %s: ALARM - Temperature %.2f K out of range (%.2f - %.2f)",
                    loop_state[i].id, measured_temp,
                    loop_state[i].alarm_min_temp, loop_state[i].alarm_max_temp);
            errors++;
            /* Continue to allow controlled shutdown */
        }

        /* Determine setpoint (including loop following) */
        float setpoint = loop_state[i].current_setpoint;

        if (strlen(loop_state[i].follows_loop_id) > 0) {
            /* Find followed loop */
            for (int j = 0; j < num_loops; j++) {
                if (strcmp(loop_state[j].id, loop_state[i].follows_loop_id) == 0) {
                    setpoint = loop_state[j].current_setpoint * loop_state[i].follows_scalar;
                    break;
                }
            }
        }

        /* TODO: Apply setpoint ramping here */
        loop_state[i].current_setpoint = setpoint;

        /* Run PID controller using coo_commons */
        float output = coo_pid_update(&loop_state[i].pid,
                                      setpoint,
                                      measured_temp,
                                      dt_seconds);

        /* Distribute power to heaters */
        const char *heater_ids_ptr[MAX_HEATERS_PER_LOOP];
        for (int j = 0; j < loop_state[i].num_heaters; j++) {
            heater_ids_ptr[j] = loop_state[i].heater_ids[j];
        }

        ret = heater_manager_distribute_power(heater_ids_ptr,
                                               loop_state[i].num_heaters,
                                               output);
        if (ret != 0) {
            LOG_ERR("Loop %s: Failed to set heater power", loop_state[i].id);
            errors++;
        }

        LOG_INF("Loop %s: SP=%.2f, PV=%.2f, OUT=%.2f W",
                loop_state[i].id, setpoint, measured_temp, output);

        loop_state[i].status = LOOP_STATUS_OK;
    }

    k_mutex_unlock(&control_mutex);

    return (errors > 0) ? -errors : 0;
}

int control_loop_set_target(const char *loop_id, float target_kelvin)
{
    if (loop_id == NULL) {
        return -1;
    }

    k_mutex_lock(&control_mutex, K_FOREVER);

    int idx = -1;
    for (int i = 0; i < num_loops; i++) {
        if (strcmp(loop_state[i].id, loop_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&control_mutex);
        LOG_ERR("Loop %s not found", loop_id);
        return -2;
    }

    /* TODO: Validate against valid_setpoint_range */

    loop_state[idx].target_temp_kelvin = target_kelvin;
    LOG_INF("Loop %s: Target set to %.2f K", loop_id, target_kelvin);

    k_mutex_unlock(&control_mutex);
    return 0;
}

int control_loop_get_target(const char *loop_id, float *target_kelvin)
{
    if (loop_id == NULL || target_kelvin == NULL) {
        return -1;
    }

    k_mutex_lock(&control_mutex, K_FOREVER);

    int idx = -1;
    for (int i = 0; i < num_loops; i++) {
        if (strcmp(loop_state[i].id, loop_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&control_mutex);
        return -2;
    }

    *target_kelvin = loop_state[idx].target_temp_kelvin;

    k_mutex_unlock(&control_mutex);
    return 0;
}

int control_loop_enable(const char *loop_id, bool enable)
{
    if (loop_id == NULL) {
        return -1;
    }

    k_mutex_lock(&control_mutex, K_FOREVER);

    int idx = -1;
    for (int i = 0; i < num_loops; i++) {
        if (strcmp(loop_state[i].id, loop_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&control_mutex);
        return -2;
    }

    loop_state[idx].enabled = enable;

    if (enable) {
        /* Reset PID integral on re-enable */
        coo_pid_reset(&loop_state[idx].pid);
        LOG_INF("Loop %s enabled", loop_id);
    } else {
        LOG_INF("Loop %s disabled", loop_id);
    }

    k_mutex_unlock(&control_mutex);
    return 0;
}

int control_loop_suspend_all(void)
{
    LOG_WRN("Suspending all control loops");

    k_mutex_lock(&control_mutex, K_FOREVER);

    for (int i = 0; i < num_loops; i++) {
        loop_state[i].suspended = true;
    }

    k_mutex_unlock(&control_mutex);
    return 0;
}

int control_loop_resume_all(void)
{
    LOG_INF("Resuming all control loops");

    k_mutex_lock(&control_mutex, K_FOREVER);

    for (int i = 0; i < num_loops; i++) {
        loop_state[i].suspended = false;
        /* Reset PID to avoid integral windup */
        coo_pid_reset(&loop_state[i].pid);
    }

    k_mutex_unlock(&control_mutex);
    return 0;
}

loop_status_t control_loop_get_status(const char *loop_id)
{
    if (loop_id == NULL) {
        return LOOP_STATUS_NOT_INITIALIZED;
    }

    k_mutex_lock(&control_mutex, K_FOREVER);

    loop_status_t status = LOOP_STATUS_NOT_INITIALIZED;
    for (int i = 0; i < num_loops; i++) {
        if (strcmp(loop_state[i].id, loop_id) == 0) {
            status = loop_state[i].status;
            break;
        }
    }

    k_mutex_unlock(&control_mutex);
    return status;
}

int control_loop_set_gains(const char *loop_id, float kp, float ki, float kd)
{
    if (loop_id == NULL) {
        return -1;
    }

    k_mutex_lock(&control_mutex, K_FOREVER);

    int idx = -1;
    for (int i = 0; i < num_loops; i++) {
        if (strcmp(loop_state[i].id, loop_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        k_mutex_unlock(&control_mutex);
        return -2;
    }

    /* Update PID gains using coo_commons function */
    coo_pid_set_gains(&loop_state[idx].pid, kp, ki, kd);

    LOG_INF("Loop %s: Gains updated to P=%.2f, I=%.2f, D=%.2f", loop_id, kp, ki, kd);

    k_mutex_unlock(&control_mutex);
    return 0;
}
