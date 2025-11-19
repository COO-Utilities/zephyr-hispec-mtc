# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# Add include path for coo_commons headers
zephyr_include_directories(../../include)

# Always include PID controller
zephyr_library_sources(pid.c)

# Network utilities (requires networking support)
# Includes both low-level sockets and high-level connection manager
zephyr_library_sources_ifdef(CONFIG_COO_NETWORK network.c)

# JSON utilities (requires JSON support)
zephyr_library_sources_ifdef(CONFIG_COO_JSON json_utils.c)

# MQTT client wrapper (requires MQTT library)
zephyr_library_sources_ifdef(CONFIG_COO_MQTT mqtt_client.c)
