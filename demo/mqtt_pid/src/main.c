/**
 * @file main.c
 * @brief MQTT + PID Control Demo
 *
 * MQTT Topics:
 *   coo/pid/telemetry  (publish)  - periodic status
 *   coo/pid/cmd        (subscribe) - incoming commands
 *   coo/pid/resp       (publish)  - command responses
 *
 * Commands (JSON):
 *   {"cmd":"set_target","value":35.0}
 *   {"cmd":"get_status"}
 *   {"cmd":"set_gains","kp":5.0,"ki":0.1,"kd":1.0}
 *   {"cmd":"get_gains"}
 *   {"cmd":"enable","value":1}
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/mqtt.h>
#include <coo_commons/mqtt_client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <config.h>
#include <sensor_manager.h>
#include <heater_manager.h>
#include <control_loop.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Control loop timing */
#define CONTROL_LOOP_PERIOD_MS  500
#define CONTROL_LOOP_DT_SECONDS (CONTROL_LOOP_PERIOD_MS / 1000.0f)
#define TELEMETRY_INTERVAL      4 /* publish every 4th iteration = 2s */

/* PID gains - tune these for your system */
#define PID_KP  5.0f
#define PID_KI  0.1f
#define PID_KD  1.0f

/* Target temperature in Celsius */
#define TARGET_TEMP_C  30.0f

/* Power limits (0-100%) */
#define POWER_LIMIT_MIN  0.0f
#define POWER_LIMIT_MAX  50.0f

/* Alarm thresholds in Celsius */
#define ALARM_MIN_TEMP_C  0.0f
#define ALARM_MAX_TEMP_C  80.0f

/* Conversion helper */
#define C_TO_K(c) ((c) + 273.15f)
#define K_TO_C(k) ((k) - 273.15f)

/* Hardware IDs */
#define SENSOR_ID    "sensor-1"
#define HEATER_ID    "high-power-1"
#define LOOP_ID      "pid-loop-1"

/* MQTT topics */
#define TOPIC_TELEMETRY  "coo/pid/telemetry"
#define TOPIC_CMD        "coo/pid/cmd"
#define TOPIC_RESP       "coo/pid/resp"

/* MQTT client instance */
static struct mqtt_client client;
static uint16_t pub_msg_id = 1;

/* MQTT thread */
#define MQTT_THREAD_STACK_SIZE 4096
#define MQTT_THREAD_PRIORITY   8
K_THREAD_STACK_DEFINE(mqtt_stack, MQTT_THREAD_STACK_SIZE);
static struct k_thread mqtt_thread_data;

/* Flag to indicate MQTT is ready for publishing */
static volatile bool mqtt_ready;

/* ADC spec for sensor */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
static const struct adc_dt_spec sensor_adc_spec = ADC_DT_SPEC_GET(DT_ALIAS(sensor_test));
#endif

/*
 * Publish message to an MQTT topic
 */
static int publish(const char *topic, const char *payload)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	param.message.topic.topic.utf8 = topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = strlen(payload);
	param.message_id = pub_msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	return mqtt_publish(&client, &param);
}

/*
 * Handle "set_target" command: {"cmd":"set_target","value":35.0}
 */
static void handle_set_target(float value_c)
{
	float target_k = C_TO_K(value_c);
	int ret = control_loop_set_target(LOOP_ID, target_k);

	char resp[128];

	if (ret == 0) {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"OK\",\"target\":%.2f}",
			 (double)value_c);
	} else {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"ERROR\",\"code\":%d}",
			 ret);
	}
	publish(TOPIC_RESP, resp);
}

/*
 * Handle "get_status" command: {"cmd":"get_status"}
 */
static void handle_get_status(void)
{
	sensor_reading_t reading;
	float heater_power = 0.0f;
	float target_k = 0.0f;

	sensor_manager_get_reading(SENSOR_ID, &reading);
	heater_manager_get_power(HEATER_ID, &heater_power);
	control_loop_get_target(LOOP_ID, &target_k);

	float temp_c = K_TO_C(reading.temperature_kelvin);
	float setpoint_c = K_TO_C(target_k);
	float error_c = setpoint_c - temp_c;

	char resp[256];

	snprintf(resp, sizeof(resp),
		 "{\"temperature\":%.2f,\"setpoint\":%.2f,"
		 "\"error\":%.2f,\"power\":%.1f,\"status\":\"OK\"}",
		 (double)temp_c, (double)setpoint_c,
		 (double)error_c, (double)heater_power);
	publish(TOPIC_RESP, resp);
}

/*
 * Handle "set_gains" command: {"cmd":"set_gains","kp":5.0,"ki":0.1,"kd":1.0}
 */
static void handle_set_gains(const char *payload)
{
	float kp = PID_KP, ki = PID_KI, kd = PID_KD;
	const char *p;

	p = strstr(payload, "\"kp\":");
	if (p) {
		kp = strtof(p + 5, NULL);
	}

	p = strstr(payload, "\"ki\":");
	if (p) {
		ki = strtof(p + 5, NULL);
	}

	p = strstr(payload, "\"kd\":");
	if (p) {
		kd = strtof(p + 5, NULL);
	}

	int ret = control_loop_set_gains(LOOP_ID, kp, ki, kd);

	char resp[128];

	if (ret == 0) {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"OK\",\"kp\":%.2f,\"ki\":%.2f,\"kd\":%.2f}",
			 (double)kp, (double)ki, (double)kd);
	} else {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"ERROR\",\"code\":%d}",
			 ret);
	}
	publish(TOPIC_RESP, resp);
}

/*
 * Handle "get_gains" command: {"cmd":"get_gains"}
 */
static void handle_get_gains(void)
{
	float kp, ki, kd;
	int ret = control_loop_get_gains(LOOP_ID, &kp, &ki, &kd);

	char resp[128];

	if (ret == 0) {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"OK\",\"kp\":%.2f,\"ki\":%.2f,\"kd\":%.2f}",
			 (double)kp, (double)ki, (double)kd);
	} else {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"ERROR\",\"code\":%d}",
			 ret);
	}
	publish(TOPIC_RESP, resp);
}

/*
 * Handle "enable" command: {"cmd":"enable","value":1}
 */
static void handle_enable(float value)
{
	bool enable = (value != 0.0f);
	int ret = control_loop_enable(LOOP_ID, enable);

	char resp[128];

	if (ret == 0) {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"OK\",\"enabled\":%s}",
			 enable ? "true" : "false");
	} else {
		snprintf(resp, sizeof(resp),
			 "{\"status\":\"ERROR\",\"code\":%d}",
			 ret);
	}
	publish(TOPIC_RESP, resp);
}

/*
 * MQTT message callback
 */
static void on_message(const struct mqtt_publish_param *pub)
{
	const char *payload = (const char *)pub->message.payload.data;
	size_t payload_len = pub->message.payload.len;

	LOG_INF("MQTT cmd: '%.*s'", (int)payload_len, payload);

	char cmd[32];
	float value = 0.0f;
	char payload_copy[256];

	/* null-terminate for parsing */
	size_t copy_len = (payload_len < sizeof(payload_copy) - 1)
			  ? payload_len : sizeof(payload_copy) - 1;
	memcpy(payload_copy, payload, copy_len);
	payload_copy[copy_len] = '\0';

	const char *cmd_start = strstr(payload_copy, "\"cmd\":\"");

	if (!cmd_start) {
		publish(TOPIC_RESP, "{\"error\":\"Missing cmd field\"}");
		return;
	}
	cmd_start += 7;
	const char *cmd_end = strchr(cmd_start, '\"');

	if (!cmd_end || (size_t)(cmd_end - cmd_start) >= sizeof(cmd)) {
		publish(TOPIC_RESP, "{\"error\":\"Invalid cmd\"}");
		return;
	}
	memcpy(cmd, cmd_start, cmd_end - cmd_start);
	cmd[cmd_end - cmd_start] = '\0';

	/* Find value field */
	const char *val_start = strstr(payload_copy, "\"value\":");

	if (val_start) {
		value = strtof(val_start + 8, NULL);
	}

	/* Dispatch */
	if (strcmp(cmd, "set_target") == 0) {
		handle_set_target(value);
	} else if (strcmp(cmd, "get_status") == 0) {
		handle_get_status();
	} else if (strcmp(cmd, "set_gains") == 0) {
		handle_set_gains(payload_copy);
	} else if (strcmp(cmd, "get_gains") == 0) {
		handle_get_gains();
	} else if (strcmp(cmd, "enable") == 0) {
		handle_enable(value);
	} else {
		char resp[128];

		snprintf(resp, sizeof(resp),
			 "{\"error\":\"Unknown command: %s\"}", cmd);
		publish(TOPIC_RESP, resp);
	}
}

static void mqtt_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("MQTT thread started");
	mqtt_ready = true;
	coo_mqtt_run(&client);
	mqtt_ready = false;
	LOG_WRN("MQTT thread exiting (disconnected)");
}

/*
 * Wait for network interface to be ready
 */
static void wait_for_network(void)
{
	struct net_if *iface = net_if_get_default();
	int wait_count = 0;

	LOG_INF("Waiting for network interface...");

	while (!net_if_is_up(iface)) {
		if (wait_count % 10 == 0) {
			LOG_INF("Interface not up yet... (%d s)", wait_count / 10);
		}
		k_msleep(100);
		wait_count++;
	}
	LOG_INF("Network interface is up");

#if defined(CONFIG_NET_DHCPV4)
	LOG_INF("Starting DHCP client...");
	net_dhcpv4_start(iface);

	wait_count = 0;
	LOG_INF("Waiting for DHCP...");
	while (true) {
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		if (ipv4 && ipv4->unicast[0].ipv4.is_used) {
			char addr_str[NET_IPV4_ADDR_LEN];

			net_addr_ntop(AF_INET,
				      &ipv4->unicast[0].ipv4.address.in_addr,
				      addr_str, sizeof(addr_str));
			LOG_INF("Got IP address: %s", addr_str);
			break;
		}
		if (wait_count % 2 == 0) {
			LOG_INF("Waiting for DHCP... (%d s)", wait_count / 2);
		}
		k_msleep(500);
		wait_count++;
	}
#else
	k_msleep(100);

	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

	if (ipv4 && ipv4->unicast[0].ipv4.is_used) {
		char addr_str[NET_IPV4_ADDR_LEN];

		net_addr_ntop(AF_INET,
			      &ipv4->unicast[0].ipv4.address.in_addr,
			      addr_str, sizeof(addr_str));
		LOG_INF("Using static IP: %s", addr_str);
	} else {
		LOG_WRN("No IP address configured");
	}
#endif
}

/*
 * Publish telemetry
 */
static void publish_telemetry(int iteration)
{
	sensor_reading_t reading;
	float heater_power = 0.0f;
	float target_k = 0.0f;

	sensor_manager_get_reading(SENSOR_ID, &reading);
	heater_manager_get_power(HEATER_ID, &heater_power);
	control_loop_get_target(LOOP_ID, &target_k);

	float temp_c = K_TO_C(reading.temperature_kelvin);
	float setpoint_c = K_TO_C(target_k);
	float error_c = setpoint_c - temp_c;

	char payload[256];

	snprintf(payload, sizeof(payload),
		 "{\"temperature\":%.2f,\"setpoint\":%.2f,"
		 "\"error\":%.2f,\"power\":%.1f,\"iteration\":%d}",
		 (double)temp_c, (double)setpoint_c,
		 (double)error_c, (double)heater_power, iteration);

	publish(TOPIC_TELEMETRY, payload);
}

int main(void)
{
	int ret;

	LOG_INF("===========================================");
	LOG_INF("MQTT + PID Control Demo Starting");
	LOG_INF("===========================================");

	/*
	 * Load settings
	 */
	thermal_config_t *config = config_load_defaults();

	if (!config) {
		LOG_ERR("Failed to load configuration");
		return -1;
	}

	/* Configure sensor ADC */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
	sensor_config_t *sensor = config_find_sensor(config, SENSOR_ID);

	if (sensor) {
		sensor->driver_data = &sensor_adc_spec;
	} else {
		LOG_ERR("Sensor %s not found in config", SENSOR_ID);
		return -1;
	}
#endif

	/* Configure heater */
	config->number_of_heaters = 1;
	strncpy(config->heaters[0].id, HEATER_ID, MAX_ID_LENGTH - 1);
	config->heaters[0].type = HEATER_TYPE_HIGH_POWER;
	config->heaters[0].max_power_w = 40.0f;
	config->heaters[0].resistance_ohms = 30.0f;
	config->heaters[0].regulator_dev =
		DEVICE_DT_GET(DT_ALIAS(high_current_heater_test));
	config->heaters[0].enabled = true;

	/* Configure control loop */
	config->number_of_control_loops = 1;
	strncpy(config->control_loops[0].id, LOOP_ID, MAX_ID_LENGTH - 1);

	config->control_loops[0].num_sensors = 1;
	strncpy(config->control_loops[0].sensor_ids[0], SENSOR_ID,
		MAX_ID_LENGTH - 1);

	config->control_loops[0].num_heaters = 1;
	strncpy(config->control_loops[0].heater_ids[0], HEATER_ID,
		MAX_ID_LENGTH - 1);

	config->control_loops[0].control_algorithm = CONTROL_ALGO_PID;
	config->control_loops[0].p_gain = PID_KP;
	config->control_loops[0].i_gain = PID_KI;
	config->control_loops[0].d_gain = PID_KD;

	config->control_loops[0].default_target_temperature = C_TO_K(TARGET_TEMP_C);
	config->control_loops[0].default_state_on = true;
	config->control_loops[0].enabled = true;

	config->control_loops[0].heater_power_limit_min = POWER_LIMIT_MIN;
	config->control_loops[0].heater_power_limit_max = POWER_LIMIT_MAX;

	config->control_loops[0].alarm_min_temp = C_TO_K(ALARM_MIN_TEMP_C);
	config->control_loops[0].alarm_max_temp = C_TO_K(ALARM_MAX_TEMP_C);

	config->control_loops[0].follows_loop_id[0] = '\0';
	config->control_loops[0].follows_loop_scalar = 1.0f;

	config->control_loops[0].error_condition = ERROR_CONDITION_ALARM;

	/*
	 * Initialize managers and control loop
	 */
	LOG_INF("Initializing heater manager...");
	ret = heater_manager_init(config);
	if (ret < 0) {
		LOG_ERR("Failed to initialize heater manager: %d", ret);
		return -1;
	}
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

	/*
	 * Initialize MQTT
	 */
	wait_for_network();

	ret = coo_mqtt_init(&client, "coo-mqtt-pid");
	if (ret != 0) {
		LOG_ERR("Failed to initialize MQTT client: %d", ret);
		return -1;
	}

	coo_mqtt_add_subscription(TOPIC_CMD, MQTT_QOS_1_AT_LEAST_ONCE);
	coo_mqtt_set_message_callback(on_message);

	LOG_INF("Connecting to MQTT broker...");
	coo_mqtt_connect(&client);

	/*
	 * Start MQTT thread
	 */
	k_thread_create(&mqtt_thread_data, mqtt_stack,
			K_THREAD_STACK_SIZEOF(mqtt_stack),
			mqtt_thread_entry, NULL, NULL, NULL,
			MQTT_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&mqtt_thread_data, "mqtt");

	/*
	 * Start PID loop
	 */
	LOG_INF("-------------------------------------------");
	LOG_INF("Configuration:");
	LOG_INF("  Target Temperature: %.2f C", (double)TARGET_TEMP_C);
	LOG_INF("  PID Gains: Kp=%.2f, Ki=%.2f, Kd=%.2f",
		(double)PID_KP, (double)PID_KI, (double)PID_KD);
	LOG_INF("  Power Limits: %.1f%% - %.1f%%",
		(double)POWER_LIMIT_MIN, (double)POWER_LIMIT_MAX);
	LOG_INF("  Loop Period: %d ms", CONTROL_LOOP_PERIOD_MS);
	LOG_INF("  MQTT Telemetry: %s (every %d s)",
		TOPIC_TELEMETRY, (TELEMETRY_INTERVAL * CONTROL_LOOP_PERIOD_MS) / 1000);
	LOG_INF("  MQTT Commands:  %s", TOPIC_CMD);
	LOG_INF("-------------------------------------------");
	LOG_INF("Starting PID control loop...");

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

		/* Log + publish telemetry every TELEMETRY_INTERVAL iterations */
		if (iteration % TELEMETRY_INTERVAL == 0) {
			sensor_reading_t reading;
			float heater_power = 0.0f;
			float target = 0.0f;

			sensor_manager_get_reading(SENSOR_ID, &reading);
			heater_manager_get_power(HEATER_ID, &heater_power);
			control_loop_get_target(LOOP_ID, &target);

			float error = target - reading.temperature_kelvin;

			printf("[%6d] T=%.2f K (%.2f C) | SP=%.2f K | "
			       "Err=%.2f | Pwr=%.1f%%\n",
			       iteration,
			       (double)reading.temperature_kelvin,
			       (double)(reading.temperature_kelvin - 273.15f),
			       (double)target,
			       (double)error,
			       (double)heater_power);

			if (mqtt_ready) {
				publish_telemetry(iteration);
			}
		}

		iteration++;
		k_msleep(CONTROL_LOOP_PERIOD_MS);
	}

	return 0;
}
