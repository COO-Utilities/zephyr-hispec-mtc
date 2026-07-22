/* MQTT command + telemetry interface: the coo_commons dispatcher bound to the
 * thermal command table over a live broker, plus periodic per-loop telemetry. */

#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>

#include <control_loop.h>
#include <thermal_commands.h>
#include <coo_commons/mqtt_client.h>
#include <coo_commons/command_dispatch.h>

#include "mqtt_interface.h"

LOG_MODULE_REGISTER(mqtt_interface, LOG_LEVEL_INF);

#define DEVICE_ID            "hstempctrl"
#define MAX_PENDING_COMMANDS 8
#define TELEMETRY_PERIOD_MS  2000
#define EXEC_STACK_SIZE      4096
#define MQTT_STACK_SIZE      4096

K_MSGQ_DEFINE(inbound_queue, sizeof(struct coo_cmd_request), MAX_PENDING_COMMANDS, 4);
K_MSGQ_DEFINE(outbound_queue, sizeof(struct coo_cmd_response), 8, 4);

static struct coo_cmd_runtime runtime;
static struct mqtt_client client;
static uint16_t mqtt_msg_id = 1;

static K_THREAD_STACK_DEFINE(exec_stack, EXEC_STACK_SIZE);
static K_THREAD_STACK_DEFINE(mqtt_stack, MQTT_STACK_SIZE);
static struct k_thread exec_thread;
static struct k_thread mqtt_thread;

static void wait_for_network(void)
{
	struct net_if *iface = net_if_get_default();

	while (!net_if_is_up(iface)) {
		k_msleep(100);
	}
	LOG_INF("Network interface up");

	net_dhcpv4_start(iface);
	while (true) {
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		if (ipv4 && ipv4->unicast[0].ipv4.is_used) {
			char addr[NET_IPV4_ADDR_LEN];

			net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
				      addr, sizeof(addr));
			LOG_INF("Got IP: %s", addr);
			return;
		}
		k_msleep(500);
	}
}

static void publish_telemetry(void)
{
	int count = control_loop_get_count();

	for (int i = 0; i < count; i++) {
		const char *id = control_loop_get_id_at(i);
		float target_k = 0.0f;
		bool enabled = false;
		struct coo_cmd_response out = {0};
		char suffix[COO_CMD_TOPIC_MAX];

		control_loop_get_target(id, &target_k);
		control_loop_get_enabled(id, &enabled);
		loop_status_t status = control_loop_get_status(id);

		snprintk(suffix, sizeof(suffix), "loop/%s/telemetry", id);
		out.payload_len = snprintk(
			out.payload, sizeof(out.payload),
			"{\"loop_id\":\"%s\",\"setpoint\":%.2f,\"enabled\":%s,\"status\":%d}",
			id, (double)(target_k - 273.15f), enabled ? "true" : "false",
			(int)status);

		const struct coo_cmd_runtime_emit_args args = {
			.type = COO_CMD_RUNTIME_EMIT_DATA,
			.delivery = COO_CMD_RUNTIME_EMIT_BEST_EFFORT,
			.suffix = suffix,
			.out = &out,
		};
		coo_cmd_runtime_emit(&runtime, &args);
	}
}

static void mqtt_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	wait_for_network();

	if (coo_mqtt_init(&client, DEVICE_ID) != 0) {
		LOG_ERR("MQTT init failed");
		return;
	}

	struct coo_mqtt_broker_config broker = {0};

	strncpy(broker.host, CONFIG_COO_MQTT_BROKER_HOSTNAME, sizeof(broker.host) - 1);
	broker.port = (uint16_t)atoi(CONFIG_COO_MQTT_BROKER_PORT);
	if (coo_mqtt_set_broker_config(&broker) != 0) {
		LOG_ERR("broker config invalid: %s:%u", broker.host, broker.port);
		return;
	}
	LOG_INF("Broker %s:%u", broker.host, broker.port);

	coo_mqtt_set_message_callback(coo_cmd_runtime_mqtt_callback, &runtime);

	char subscription[COO_CMD_TOPIC_MAX];

	snprintk(subscription, sizeof(subscription), "%s#", runtime.request_prefix);
	coo_mqtt_add_subscription(subscription, MQTT_QOS_1_AT_LEAST_ONCE);
	LOG_INF("Subscribing to %s", subscription);

	bool subscribed = false;
	int64_t next_telemetry = 0;

	while (true) {
		coo_cmd_runtime_serial_poll(&runtime);

		if (!coo_mqtt_is_connected()) {
			subscribed = false;
			if (coo_mqtt_connect(&client) != 0) {
				k_msleep(2000);
				continue;
			}
		}
		if (!subscribed) {
			coo_mqtt_subscribe(&client);
			subscribed = true;
		}

		if (k_uptime_get() >= next_telemetry) {
			publish_telemetry();
			next_telemetry = k_uptime_get() + TELEMETRY_PERIOD_MS;
		}

		coo_cmd_runtime_drain_outbound(&runtime, &client, coo_mqtt_is_connected());
		coo_mqtt_process(&client);
		k_msleep(20);
	}
}

void mqtt_interface_start(void)
{
	size_t spec_count;
	const struct coo_cmd_spec *specs = thermal_commands_specs(&spec_count);
	const struct coo_cmd_runtime_config cfg = {
		.device_id = DEVICE_ID,
		.inbound_queue = &inbound_queue,
		.outbound_queue = &outbound_queue,
		.mqtt_msg_id = &mqtt_msg_id,
		.serial_wrap_column = COO_CMD_SERIAL_WRAP_COLUMN,
		.command_specs = specs,
		.command_spec_count = spec_count,
	};

	if (coo_cmd_runtime_configure(&runtime, &cfg) != 0) {
		LOG_ERR("runtime configure failed");
		return;
	}

	k_thread_create(&exec_thread, exec_stack, K_THREAD_STACK_SIZEOF(exec_stack),
			coo_cmd_runtime_executor_thread, &runtime, NULL, NULL,
			8, 0, K_NO_WAIT);
	k_thread_name_set(&exec_thread, "cmd_exec");

	k_thread_create(&mqtt_thread, mqtt_stack, K_THREAD_STACK_SIZEOF(mqtt_stack),
			mqtt_thread_entry, NULL, NULL, NULL,
			8, 0, K_NO_WAIT);
	k_thread_name_set(&mqtt_thread, "mqtt");
}
