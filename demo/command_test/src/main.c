/* Exercise the thermal command table without MQTT: dispatch a sequence of
 * requests locally and print each response. */

#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <config.h>
#include <control_loop.h>
#include <sensor_manager.h>
#include <heater_manager.h>
#include <thermal_commands.h>

LOG_MODULE_REGISTER(command_test, LOG_LEVEL_INF);

static void run(const char *key, const char *payload)
{
	struct coo_cmd_request cmd = {0};
	struct coo_cmd_response out = {0};

	strncpy(cmd.key, key, sizeof(cmd.key) - 1);
	if (payload != NULL) {
		strncpy(cmd.payload, payload, sizeof(cmd.payload) - 1);
		cmd.payload_len = strlen(cmd.payload);
	}

	thermal_commands_dispatch(&cmd, &out);
	printf("%-24s %-22s -> %.*s\n", key, payload ? payload : "(query)",
	       (int)out.payload_len, out.payload);
}

int main(void)
{
	thermal_config_t *config = config_load_defaults();

	if (config == NULL) {
		LOG_ERR("config_load_defaults failed");
		return 0;
	}
	control_loop_init(config);
	sensor_manager_init(config);
	heater_manager_init(config);

	printf("=== thermal command dispatch ===\n");
	run("loops", NULL);
	run("sensors", NULL);
	run("heaters", NULL);
	run("loop/loop-1/target", "{\"value\":25.0}");
	run("loop/loop-1/target", NULL);
	run("loop/loop-1/gains", "{\"kp\":8.0,\"ki\":0.2,\"kd\":1.5}");
	run("loop/loop-1/gains", NULL);
	run("loop/loop-1/enable", "{\"value\":false}");
	run("loop/loop-1/enable", NULL);
	run("loop/loop-1/status", NULL);
	run("loop/nope/target", NULL);
	run("estop", "{}");
	run("bogus", NULL);
	printf("=== done ===\n");

	return 0;
}
