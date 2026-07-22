/* Thermal controller command table.
 *
 * Wire values are Celsius; the control loop works in Kelvin, so target
 * conversions happen at this boundary. Per-loop commands arrive as
 * loop/<loop_id>/<key>; system commands are single keys. */

#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "thermal_commands.h"

#include <config.h>
#include <control_loop.h>
#include <sensor_manager.h>
#include <heater_manager.h>
#include <coo_commons/json_utils.h>

#define KELVIN_OFFSET 273.15f

static int parse_loop_key(const struct coo_cmd_request *cmd, char *loop_id,
			  size_t loop_id_len, char *sub, size_t sub_len)
{
	return coo_cmd_key_suffix_pair_copy(cmd->key, "loop", loop_id, loop_id_len,
					    sub, sub_len);
}

static int loop_query(const struct coo_cmd_request *cmd, struct coo_cmd_response *out)
{
	char loop_id[MAX_ID_LENGTH];
	char sub[COO_CMD_KEY_MAX];
	char payload[128];

	if (parse_loop_key(cmd, loop_id, sizeof(loop_id), sub, sizeof(sub)) != 0) {
		return coo_cmd_invalid_response(out, cmd);
	}

	if (strcmp(sub, "target") == 0) {
		float target_k;

		if (control_loop_get_target(loop_id, &target_k) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		snprintf(payload, sizeof(payload), "{\"target\":%.2f}",
			 (double)(target_k - KELVIN_OFFSET));
		return coo_cmd_reply(out, cmd, COO_CMD_RESP_OK, payload);
	}

	if (strcmp(sub, "gains") == 0) {
		float kp, ki, kd;

		if (control_loop_get_gains(loop_id, &kp, &ki, &kd) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		snprintf(payload, sizeof(payload),
			 "{\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f}",
			 (double)kp, (double)ki, (double)kd);
		return coo_cmd_reply(out, cmd, COO_CMD_RESP_OK, payload);
	}

	if (strcmp(sub, "enable") == 0) {
		bool enabled;

		if (control_loop_get_enabled(loop_id, &enabled) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		snprintf(payload, sizeof(payload), "{\"enabled\":%s}",
			 enabled ? "true" : "false");
		return coo_cmd_reply(out, cmd, COO_CMD_RESP_OK, payload);
	}

	if (strcmp(sub, "status") == 0) {
		float target_k;
		loop_status_t status = control_loop_get_status(loop_id);

		if (status == LOOP_STATUS_NOT_INITIALIZED ||
		    control_loop_get_target(loop_id, &target_k) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		snprintf(payload, sizeof(payload),
			 "{\"setpoint\":%.2f,\"status\":%d}",
			 (double)(target_k - KELVIN_OFFSET), (int)status);
		return coo_cmd_reply(out, cmd, COO_CMD_RESP_OK, payload);
	}

	return coo_cmd_unknown_response(out, cmd);
}

static int loop_effect(const struct coo_cmd_request *cmd, struct coo_cmd_response *out)
{
	char loop_id[MAX_ID_LENGTH];
	char sub[COO_CMD_KEY_MAX];

	if (parse_loop_key(cmd, loop_id, sizeof(loop_id), sub, sizeof(sub)) != 0) {
		return coo_cmd_invalid_response(out, cmd);
	}

	if (strcmp(sub, "target") == 0) {
		double celsius;

		if (coo_json_extract_double(cmd->payload, "value", &celsius) !=
		    COO_JSON_EXTRACT_OK) {
			return coo_cmd_error(out, cmd, "value required");
		}
		if (control_loop_set_target(loop_id, (float)celsius + KELVIN_OFFSET) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		return coo_cmd_ok(out, cmd);
	}

	if (strcmp(sub, "gains") == 0) {
		double kp, ki, kd;

		if (coo_json_extract_double(cmd->payload, "kp", &kp) != COO_JSON_EXTRACT_OK ||
		    coo_json_extract_double(cmd->payload, "ki", &ki) != COO_JSON_EXTRACT_OK ||
		    coo_json_extract_double(cmd->payload, "kd", &kd) != COO_JSON_EXTRACT_OK) {
			return coo_cmd_error(out, cmd, "kp, ki, kd required");
		}
		if (control_loop_set_gains(loop_id, (float)kp, (float)ki, (float)kd) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		return coo_cmd_ok(out, cmd);
	}

	if (strcmp(sub, "enable") == 0) {
		bool enable;

		if (coo_json_extract_bool(cmd->payload, "value", &enable) != COO_JSON_EXTRACT_OK) {
			return coo_cmd_error(out, cmd, "value required");
		}
		if (control_loop_enable(loop_id, enable) != 0) {
			return coo_cmd_error(out, cmd, "unknown loop");
		}
		return coo_cmd_ok(out, cmd);
	}

	return coo_cmd_unknown_response(out, cmd);
}

/* Append {"<field>":[ "id0","id1",... ]} for an indexed id accessor */
static int reply_id_list(const struct coo_cmd_request *cmd, struct coo_cmd_response *out,
			 const char *field, int count,
			 const char *(*id_at)(int index))
{
	char payload[COO_CMD_PAYLOAD_MAX];
	int off = snprintf(payload, sizeof(payload), "{\"%s\":[", field);

	for (int i = 0; i < count && off > 0 && off < (int)sizeof(payload); i++) {
		off += snprintf(payload + off, sizeof(payload) - off, "%s\"%s\"",
				i ? "," : "", id_at(i));
	}
	if (off < 0 || off >= (int)sizeof(payload) - 2) {
		return coo_cmd_error(out, cmd, "response too large");
	}
	snprintf(payload + off, sizeof(payload) - off, "]}");
	return coo_cmd_reply(out, cmd, COO_CMD_RESP_OK, payload);
}

static int loops_list(const struct coo_cmd_request *cmd, struct coo_cmd_response *out)
{
	return reply_id_list(cmd, out, "loops", control_loop_get_count(),
			     control_loop_get_id_at);
}

static int sensors_list(const struct coo_cmd_request *cmd, struct coo_cmd_response *out)
{
	return reply_id_list(cmd, out, "sensors", sensor_manager_get_count(),
			     sensor_manager_get_id_at);
}

static int heaters_list(const struct coo_cmd_request *cmd, struct coo_cmd_response *out)
{
	return reply_id_list(cmd, out, "heaters", heater_manager_get_count(),
			     heater_manager_get_id_at);
}

static int estop_effect(const struct coo_cmd_request *cmd, struct coo_cmd_response *out)
{
	heater_manager_emergency_stop();
	control_loop_suspend_all();
	return coo_cmd_ok(out, cmd);
}

static const struct coo_cmd_spec thermal_specs[] = {
	{ .key = "loop", .query_handler = loop_query, .effect_handler = loop_effect,
	  .key_prefix_match = true, .class_policy = COO_CMD_CLASS_DEFAULT,
	  .allowed_payload_keys = "value,kp,ki,kd" },
	{ .key = "loops", .query_handler = loops_list,
	  .class_policy = COO_CMD_CLASS_ALWAYS_QUERY },
	{ .key = "sensors", .query_handler = sensors_list,
	  .class_policy = COO_CMD_CLASS_ALWAYS_QUERY },
	{ .key = "heaters", .query_handler = heaters_list,
	  .class_policy = COO_CMD_CLASS_ALWAYS_QUERY },
	{ .key = "estop", .effect_handler = estop_effect,
	  .class_policy = COO_CMD_CLASS_ALWAYS_EFFECT },
};

const struct coo_cmd_spec *thermal_commands_specs(size_t *count)
{
	*count = ARRAY_SIZE(thermal_specs);
	return thermal_specs;
}

int thermal_commands_dispatch(const struct coo_cmd_request *cmd,
			      struct coo_cmd_response *out)
{
	for (size_t i = 0; i < ARRAY_SIZE(thermal_specs); i++) {
		const struct coo_cmd_spec *spec = &thermal_specs[i];
		bool match = spec->key_prefix_match
				     ? coo_cmd_key_matches_prefix(cmd->key, spec->key)
				     : strcmp(cmd->key, spec->key) == 0;

		if (!match) {
			continue;
		}

		bool is_query;

		switch (spec->class_policy) {
		case COO_CMD_CLASS_ALWAYS_QUERY:
			is_query = true;
			break;
		case COO_CMD_CLASS_ALWAYS_EFFECT:
			is_query = false;
			break;
		default:
			is_query = coo_cmd_payload_empty(cmd);
			break;
		}

		coo_cmd_handler_fn handler = is_query ? spec->query_handler
						      : spec->effect_handler;
		if (handler == NULL) {
			return coo_cmd_unsupported_response(out, cmd);
		}
		return handler(cmd, out);
	}

	return coo_cmd_unknown_response(out, cmd);
}
