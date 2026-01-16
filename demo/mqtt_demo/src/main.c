/*
 * Copyright (c) 2025 Caltech Optical Observatories
 * SPDX-License-Identifier: Apache-2.0
 *
 * MQTT Demo - demonstrates using the coo_mqtt client library
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <coo_commons/mqtt_client.h>
#include <string.h>

LOG_MODULE_REGISTER(mqtt_demo, LOG_LEVEL_DBG);

/* MQTT client instance */
static struct mqtt_client client;

/* Topics */
#define TOPIC_CMD    "coo/demo/cmd"
#define TOPIC_STATUS "coo/demo/status"

/* Message ID for publishing */
static uint16_t pub_msg_id = 1;

/**
 * Publish a message to a topic
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

/**
 * Callback for received MQTT messages
 */
static void on_message(const struct mqtt_publish_param *pub)
{
	const char *topic = pub->message.topic.topic.utf8;
	const char *payload = pub->message.payload.data;
	size_t topic_len = pub->message.topic.topic.size;
	size_t payload_len = pub->message.payload.len;

	LOG_INF("Received: topic='%.*s' payload='%.*s'",
		(int)topic_len, topic, (int)payload_len, payload);

	/* Echo back on status topic */
	char response[128];
	snprintf(response, sizeof(response), "ACK: %.*s", (int)payload_len, payload);
	publish(TOPIC_STATUS, response);
}

/**
 * Wait for network interface to be ready
 * Supports both DHCP (CONFIG_NET_DHCPV4) and static IP (CONFIG_NET_CONFIG_SETTINGS)
 */
static void wait_for_network(void)
{
	struct net_if *iface = net_if_get_default();
	int wait_count = 0;

	LOG_INF("Waiting for network interface...");

	/* Wait for interface to be up */
	while (!net_if_is_up(iface)) {
		if (wait_count % 10 == 0) {
			LOG_INF("Interface not up yet... (%d s)", wait_count / 10);
		}
		k_msleep(100);
		wait_count++;
	}
	LOG_INF("Network interface is up");

#if defined(CONFIG_NET_DHCPV4)
	/* DHCP mode: start client and wait for address */
	LOG_INF("Starting DHCP client...");
	net_dhcpv4_start(iface);

	wait_count = 0;
	LOG_INF("Waiting for DHCP...");
	while (true) {
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
		if (ipv4 && ipv4->unicast[0].ipv4.is_used) {
			char addr_str[NET_IPV4_ADDR_LEN];
			net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
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
	/* Static IP mode: address is configured at boot via CONFIG_NET_CONFIG_* */
	/* Just wait briefly for the stack to be ready */
	k_msleep(100);

	struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
	if (ipv4 && ipv4->unicast[0].ipv4.is_used) {
		char addr_str[NET_IPV4_ADDR_LEN];
		net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
			      addr_str, sizeof(addr_str));
		LOG_INF("Using static IP: %s", addr_str);
	} else {
		LOG_WRN("No IP address configured");
	}
#endif
}

int main(void)
{
	int rc;

	LOG_INF("MQTT Demo starting");

	/* Wait for network connectivity */
	wait_for_network();

	/* Initialize MQTT client */
	rc = coo_mqtt_init(&client, "coo-mqtt-demo");
	if (rc != 0) {
		LOG_ERR("Failed to initialize MQTT client: %d", rc);
		return rc;
	}

	/* Set up subscription and callback */
	coo_mqtt_add_subscription(TOPIC_CMD, MQTT_QOS_0_AT_MOST_ONCE);
	coo_mqtt_set_message_callback(on_message);

	/* Connect to broker (blocks until connected) */
	LOG_INF("Connecting to MQTT broker...");
	coo_mqtt_connect(&client);

	/* Publish startup message */
	publish(TOPIC_STATUS, "online");

	/* Run MQTT event loop (blocks) */
	LOG_INF("Entering MQTT event loop");
	coo_mqtt_run(&client);

	LOG_INF("MQTT Demo exiting");
	return 0;
}
