/**
 * @file control_loop.h
 * @brief Multi-loop PID temperature control
 */

#ifndef CONTROL_LOOP_H
#define CONTROL_LOOP_H

#include "../config/config.h"
#include <stdbool.h>

/**
 * Control loop status
 */
typedef enum {
    LOOP_STATUS_OK = 0,
    LOOP_STATUS_DISABLED = -1,
    LOOP_STATUS_SENSOR_ERROR = -2,
    LOOP_STATUS_ALARM = -3,
    LOOP_STATUS_NOT_INITIALIZED = -4
} loop_status_t;

/**
 * Initialize control loop subsystem
 * Creates PID controllers for each configured loop
 * @param config Pointer to thermal configuration
 * @return 0 on success, negative error code on failure
 */
int control_loop_init(const thermal_config_t *config);

/**
 * Update all control loops
 * Called periodically by control thread
 * Reads sensors, runs PID, outputs to heaters
 * @param dt_seconds Time delta since last update (seconds)
 * @return 0 on success, negative error code on failure
 */
int control_loop_update_all(float dt_seconds);

/**
 * Set target temperature for a loop
 * @param loop_id Loop ID string
 * @param target_kelvin Target temperature in Kelvin
 * @return 0 on success, negative error code on failure
 */
int control_loop_set_target(const char *loop_id, float target_kelvin);

/**
 * Get target temperature for a loop
 * @param loop_id Loop ID string
 * @param target_kelvin Pointer to store target temperature
 * @return 0 on success, negative error code on failure
 */
int control_loop_get_target(const char *loop_id, float *target_kelvin);

/**
 * Enable/disable a control loop
 * @param loop_id Loop ID string
 * @param enable true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int control_loop_enable(const char *loop_id, bool enable);

/**
 * Suspend all control loops (for emergency/alarm conditions)
 * @return 0 on success, negative error code on failure
 */
int control_loop_suspend_all(void);

/**
 * Resume all control loops
 * @return 0 on success, negative error code on failure
 */
int control_loop_resume_all(void);

/**
 * Get loop status
 * @param loop_id Loop ID string
 * @return loop_status_t status code
 */
loop_status_t control_loop_get_status(const char *loop_id);

/**
 * Update PID gains for a loop at runtime
 * @param loop_id Loop ID string
 * @param kp Proportional gain
 * @param ki Integral gain
 * @param kd Derivative gain
 * @return 0 on success, negative error code on failure
 */
int control_loop_set_gains(const char *loop_id, float kp, float ki, float kd);

#endif /* CONTROL_LOOP_H */
