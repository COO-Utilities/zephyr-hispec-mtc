/**
 * @file main.c
 * @brief Thermal controller main application
 *
 * Modular multi-heater, multi-sensor thermal controller with PID loops
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Application modules */
#include "../../lib/config/config.h"
#include "../../lib/sensors/sensor_manager.h"
#include "../../lib/heaters/heater_manager.h"
#include "../../lib/control/control_loop.h"

LOG_MODULE_REGISTER(main_app, LOG_LEVEL_INF);

/* Thread stacks */
#define SENSOR_STACK_SIZE 2048
#define CONTROL_STACK_SIZE 4096
#define SUPERVISOR_STACK_SIZE 2048

K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_STACK_SIZE);
K_THREAD_STACK_DEFINE(control_stack, CONTROL_STACK_SIZE);

struct k_thread sensor_thread;
struct k_thread control_thread;

/* Global configuration */
static thermal_config_t *g_config = NULL;

/* Thread synchronization */
static bool system_running = true;
static bool alarm_triggered = false;

/* ========== Sensor Thread ========== */

void sensor_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Sensor thread started (period: 500ms)");

    while (system_running) {
        int ret = sensor_manager_read_all();
        if (ret != 0) {
            LOG_WRN("Sensor read errors: %d", -ret);
        }

        k_sleep(K_MSEC(500));  /* 2 Hz - matches control loop */
    }

    LOG_INF("Sensor thread exiting");
}

/* ========== Control Thread ========== */

void control_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Control thread started (period: 500ms)");

    const float dt = 0.5f;  /* 500ms = 0.5 seconds */

    while (system_running) {
        if (!alarm_triggered) {
            int ret = control_loop_update_all(dt);
            if (ret != 0) {
                LOG_WRN("Control loop errors: %d", -ret);
            }
        } else {
            /* In alarm state, keep loops suspended */
            LOG_DBG("Control loops suspended due to alarm");
        }

        k_sleep(K_MSEC(500));  /* 2 Hz */
    }

    LOG_INF("Control thread exiting");
}

/* ========== Helper Functions ========== */

/**
 * Monitor system health and handle alarms
 */
static void monitor_system_health(void)
{
    /* Check all control loops for alarm conditions */
    for (int i = 0; i < g_config->number_of_control_loops; i++) {
        if (!g_config->control_loops[i].enabled) {
            continue;
        }

        loop_status_t status = control_loop_get_status(g_config->control_loops[i].id);

        if (status == LOOP_STATUS_ALARM && !alarm_triggered) {
            LOG_ERR("ALARM: Loop %s in alarm state!", g_config->control_loops[i].id);
            alarm_triggered = true;

            /* Handle based on error_condition */
            if (g_config->control_loops[i].error_condition == ERROR_CONDITION_STOP) {
                LOG_ERR("EMERGENCY STOP triggered");
                heater_manager_emergency_stop();
                control_loop_suspend_all();
            }
        }
    }

    /* TODO: Add watchdog timer monitoring */
    /* TODO: Add timeout handling */
}

/**
 * Handle mode changes (future implementation)
 */
static void handle_mode_changes(void)
{
    /* TODO: Implement mode switching (auto/manual/off) */
}

/* ========== Main Entry Point ========== */

int main(void)
{
    LOG_INF("====================================");
    LOG_INF("  Thermal Controller v1.0");
    LOG_INF("====================================");

    /* ========== 1. Load Configuration ========== */
    g_config = config_load_defaults();
    if (g_config == NULL) {
        LOG_ERR("Failed to load configuration");
        return -1;
    }

    LOG_INF("Configuration loaded:");
    LOG_INF("  Controller ID: %s", g_config->id);
    LOG_INF("  Sensors: %d", g_config->number_of_sensors);
    LOG_INF("  Heaters: %d", g_config->number_of_heaters);
    LOG_INF("  Control Loops: %d", g_config->number_of_control_loops);

    /* ========== 2. Validate Configuration ========== */
    int ret = config_validate(g_config);
    if (ret != 0) {
        LOG_ERR("Configuration validation failed: %d", ret);
        return ret;
    }

    LOG_INF("Configuration validated successfully");

    /* ========== 3. Initialize Hardware Subsystems ========== */

    LOG_INF("Initializing sensor manager...");
    ret = sensor_manager_init(g_config);
    if (ret != 0) {
        LOG_ERR("Sensor manager initialization failed: %d", ret);
        return ret;
    }

    LOG_INF("Initializing heater manager...");
    ret = heater_manager_init(g_config);
    if (ret != 0) {
        LOG_ERR("Heater manager initialization failed: %d", ret);
        return ret;
    }

    /* ========== 4. Initialize Control Loops ========== */

    LOG_INF("Initializing control loops...");
    ret = control_loop_init(g_config);
    if (ret != 0) {
        LOG_ERR("Control loop initialization failed: %d", ret);
        return ret;
    }

    /* ========== 5. Create Worker Threads ========== */

    LOG_INF("Creating worker threads...");

    k_thread_create(&sensor_thread, sensor_stack,
                    K_THREAD_STACK_SIZEOF(sensor_stack),
                    sensor_thread_entry, NULL, NULL, NULL,
                    5,  /* Priority 5 (high) */
                    0, K_NO_WAIT);

    k_thread_name_set(&sensor_thread, "sensor");

    k_thread_create(&control_thread, control_stack,
                    K_THREAD_STACK_SIZEOF(control_stack),
                    control_thread_entry, NULL, NULL, NULL,
                    7,  /* Priority 7 (medium) */
                    0, K_NO_WAIT);

    k_thread_name_set(&control_thread, "control");

    LOG_INF("All threads started");

    /* ========== 6. Optional: Network and Telemetry ========== */

#ifdef CONFIG_NETWORKING
    LOG_INF("Network support not yet enabled");
    /* TODO: Initialize coo_commons network */
    /* TODO: Initialize MQTT telemetry */
#endif

    /* ========== 7. Supervisor Loop ========== */

    LOG_INF("====================================");
    LOG_INF("System initialized - entering supervisor loop");
    LOG_INF("====================================");

    while (system_running) {
        /* Monitor system health */
        monitor_system_health();

        /* Handle mode changes */
        handle_mode_changes();

        /* Sleep 100ms */
        k_sleep(K_MSEC(100));
    }

    /* ========== 8. Cleanup on Exit ========== */

    LOG_INF("Shutting down system...");

    /* Stop all heaters */
    heater_manager_emergency_stop();

    /* Wait for threads to exit */
    k_thread_join(&sensor_thread, K_FOREVER);
    k_thread_join(&control_thread, K_FOREVER);

    LOG_INF("System shutdown complete");
    return 0;
}