/* Thermal controller command table: ICD command handlers over the coo_commons
 * dispatcher and the sensor/heater/control managers. */

#ifndef THERMAL_COMMANDS_H
#define THERMAL_COMMANDS_H

#include <stddef.h>

#include <coo_commons/command_dispatch.h>

/** Command specs for coo_cmd_runtime_configure(). */
const struct coo_cmd_spec *thermal_commands_specs(size_t *count);

/**
 * Route one request to its handler without the MQTT runtime.
 * Matches the key, classifies query vs effect by payload presence, and invokes
 * the handler. Intended for tests and serial bring-up.
 * @return handler result, or a built error response for an unknown key
 */
int thermal_commands_dispatch(const struct coo_cmd_request *cmd,
			      struct coo_cmd_response *out);

#endif /* THERMAL_COMMANDS_H */
