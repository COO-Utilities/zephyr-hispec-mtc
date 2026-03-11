# Interface Control Document: Temperature Controller MQTT API

| Field           | Value                              |
|-----------------|------------------------------------|
| Document ID     | ICD-TC-001                         |
| Version         | 2.0                                |
| Date            | 2026-03-11                         |
| System          | COO Thermal Controller (Zephyr)    |
| Interface       | MQTT over TCP                      |

---

## 1. Overview

This document defines the MQTT interface for the COO Temperature Controller. The system manages multiple independent PID control loops, each driving one or more heaters from many sensor inputs. External clients interact with the controller via MQTT to command setpoints, tune PID gains, configure ramp rates, and receive per-loop telemetry. All resource limits are configurable at build time (see Section 1.2).

### 1.1 System Context

```
                           MQTT / TCP
+-----------+         (configurable broker)      +----------------------+
|  External |  <------------------------------->  |  Temperature         |
|  Client   |                                    |  Controller (Zephyr) |
+-----------+                                    +----------------------+
                                                   | Multiple PID loops
+-----------+                                      | Up to 100 sensors
|  Config   |  --- build-time / runtime --->       | Up to 20 heaters
|  Source   |      (Kconfig, MQTT, JSON)           | Ethernet (DHCP/static)
+-----------+                                    +----------------------+
```

### 1.2 System Limits

All resource limits are configurable at build time via Kconfig. There are no hard-coded ceilings — the values below are the defaults.

| Resource               | Kconfig Option                    | Default |
|------------------------|-----------------------------------|---------|
| Sensors                | `CONFIG_COO_MAX_SENSORS`          | 100     |
| Heaters                | `CONFIG_COO_MAX_HEATERS`          | 20      |
| Control loops          | `CONFIG_COO_MAX_CONTROL_LOOPS`    | 8       |
| Sensors per loop       | `CONFIG_COO_MAX_SENSORS_PER_LOOP` | 20      |
| Heaters per loop       | `CONFIG_COO_MAX_HEATERS_PER_LOOP` | 4       |
| MQTT subscriptions     | _(compile-time)_                  | 4       |
| MQTT payload size      | `CONFIG_COO_MQTT_PAYLOAD_SIZE`    | 512 B   |

The only constraint on these values is available RAM. Each sensor config entry is ~260 bytes and each heater config entry is ~120 bytes, so the memory cost scales linearly.

---

## 2. Network Configuration

### 2.1 IP Network

The controller uses Zephyr's network stack over Ethernet. IP configuration is determined at build time via Kconfig and can be extended at runtime.

| Parameter                    | Kconfig Option                     | Default          |
|------------------------------|------------------------------------|------------------|
| IPv4 support                 | `CONFIG_NET_IPV4`                  | `y`              |
| DHCP client                  | `CONFIG_NET_DHCPV4`                | `y`              |
| DNS resolver                 | `CONFIG_DNS_RESOLVER`              | `y`              |
| Ethernet driver              | `CONFIG_ETH_STM32_HAL`            | `y`              |
| Static IP address            | `CONFIG_NET_CONFIG_MY_IPV4_ADDR`  | _(unset, DHCP)_  |
| Static netmask               | `CONFIG_NET_CONFIG_MY_IPV4_NETMASK`| _(unset, DHCP)_ |
| Static gateway               | `CONFIG_NET_CONFIG_MY_IPV4_GW`    | _(unset, DHCP)_  |

**Startup sequence:**
1. `coo_network_init()` brings up the Ethernet interface
2. DHCP lease is requested (or static IP applied)
3. Controller waits for IP assignment before connecting to broker

### 2.2 MQTT Broker Connection

| Parameter                    | Kconfig Option                      | Default                    |
|------------------------------|-------------------------------------|----------------------------|
| Broker hostname              | `CONFIG_COO_MQTT_BROKER_HOSTNAME`   | `"centaurus.caltech.edu"`  |
| Broker port                  | `CONFIG_COO_MQTT_BROKER_PORT`       | `"1883"`                   |
| Payload buffer size          | `CONFIG_COO_MQTT_PAYLOAD_SIZE`      | `512`                      |

| Runtime Parameter      | Value                        |
|------------------------|------------------------------|
| Transport              | TCP (non-TLS)                |
| Reconnect timeout      | 5000 ms (exponential backoff)|
| Socket poll timeout    | 30000 ms                     |

### 2.3 MQTT Network Configuration Commands

Clients can query and update broker settings at runtime. Changes take effect on the next reconnect.

#### 2.3.1 Get Network Status

**Topic:** `coo/system/cmd`

**Request:**
```json
{"cmd": "get_network"}
```

**Response (Topic: `coo/system/resp`):**
```json
{
  "status": "OK",
  "ip": "192.168.1.42",
  "netmask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "broker": "centaurus.caltech.edu",
  "broker_port": 1883,
  "mqtt_connected": true
}
```

| Field            | Type    | Description                          |
|------------------|---------|--------------------------------------|
| `ip`             | string  | Current IPv4 address                 |
| `netmask`        | string  | Current subnet mask                  |
| `gateway`        | string  | Current default gateway              |
| `broker`         | string  | MQTT broker hostname                 |
| `broker_port`    | integer | MQTT broker port                     |
| `mqtt_connected` | boolean | `true` if MQTT session is active     |

#### 2.3.2 Set Broker

**Topic:** `coo/system/cmd`

**Request:**
```json
{"cmd": "set_broker", "hostname": "10.0.0.5", "port": 1883}
```

| Field      | Type    | Required | Description                      |
|------------|---------|----------|----------------------------------|
| `cmd`      | string  | Yes      | Must be `"set_broker"`           |
| `hostname` | string  | Yes      | Broker hostname or IP            |
| `port`     | integer | No       | Broker port (default: 1883)      |

**Response (Topic: `coo/system/resp`):**
```json
{"status": "OK", "broker": "10.0.0.5", "port": 1883}
```

---

## 3. MQTT Topic Map

Topics are organized into a hierarchy. Loop-specific topics are parameterized by `{loop_id}`.

### 3.1 System Topics

| Topic                  | Direction            | QoS | Description                        |
|------------------------|----------------------|-----|------------------------------------|
| `coo/system/cmd`       | Client -> Controller | 1   | System-level commands              |
| `coo/system/resp`      | Controller -> Client | 0   | System-level responses             |
| `coo/system/status`    | Controller -> Client | 0   | System health / network heartbeat  |

### 3.2 Per-Loop Topics

| Topic                              | Direction            | QoS | Description                     |
|------------------------------------|----------------------|-----|---------------------------------|
| `coo/{loop_id}/cmd`               | Client -> Controller | 1   | Loop commands                   |
| `coo/{loop_id}/resp`              | Controller -> Client | 0   | Loop command responses          |
| `coo/{loop_id}/telemetry`         | Controller -> Client | 0   | Periodic loop telemetry         |

**Example for loop `loop-1`:**
- `coo/loop-1/cmd`
- `coo/loop-1/resp`
- `coo/loop-1/telemetry`

### 3.3 Wildcard Subscriptions

Clients can subscribe to all loops using MQTT wildcards:
- `coo/+/telemetry` — all loop telemetry
- `coo/+/resp` — all loop responses
- `coo/#` — everything

---

## 4. Per-Loop Telemetry

**Topic:** `coo/{loop_id}/telemetry`
**Direction:** Controller -> Client
**Rate:** Configurable (default: every 2 seconds)
**QoS:** 0

```json
{
  "loop_id": "loop-1",
  "temperature": 28.50,
  "setpoint": 35.00,
  "target_setpoint": 40.00,
  "ramp_active": true,
  "error": 6.50,
  "power": 42.3,
  "iteration": 100,
  "status": 0,
  "sensors": [
    {"id": "sensor-1", "temperature": 28.45, "status": 0},
    {"id": "sensor-2", "temperature": 28.55, "status": 0}
  ],
  "heaters": [
    {"id": "heater-1", "power": 30.2},
    {"id": "heater-2", "power": 12.1}
  ]
}
```

| Field              | Type    | Unit | Description                                           |
|--------------------|---------|------|-------------------------------------------------------|
| `loop_id`          | string  | -    | Control loop identifier                               |
| `temperature`      | float   | C    | Averaged temperature across loop sensors              |
| `setpoint`         | float   | C    | Current effective setpoint (may be mid-ramp)          |
| `target_setpoint`  | float   | C    | Final target setpoint (equals `setpoint` when stable) |
| `ramp_active`      | boolean | -    | `true` if setpoint is currently ramping               |
| `error`            | float   | C    | Setpoint minus averaged measured temperature          |
| `power`            | float   | %    | Total heater power output for this loop               |
| `iteration`        | integer | -    | Control loop iteration counter                        |
| `status`           | integer | -    | Loop status code (see Section 9)                      |
| `sensors[]`        | array   | -    | Per-sensor readings (see below)                       |
| `heaters[]`        | array   | -    | Per-heater power levels (see below)                   |

**Sensor element:**

| Field         | Type    | Unit | Description                  |
|---------------|---------|------|------------------------------|
| `id`          | string  | -    | Sensor identifier            |
| `temperature` | float   | C    | Individual sensor reading    |
| `status`      | integer | -    | Sensor status (0 = OK)       |

**Heater element:**

| Field   | Type   | Unit | Description                         |
|---------|--------|------|-------------------------------------|
| `id`    | string | -    | Heater identifier                   |
| `power` | float  | %    | Power output for this heater        |

---

## 5. Per-Loop Commands

All per-loop commands are sent to `coo/{loop_id}/cmd` and responses arrive on `coo/{loop_id}/resp`.

### 5.1 Set Target Temperature

**Request:**
```json
{"cmd": "set_target", "value": 35.0}
```

| Field   | Type  | Unit | Range               | Required | Description                |
|---------|-------|------|---------------------|----------|----------------------------|
| `cmd`   | string| -    | -                   | Yes      | Must be `"set_target"`     |
| `value` | float | C    | `valid_setpoint_range` | Yes   | Desired target temperature |

When a ramp rate is configured, the setpoint transitions gradually toward the target. The telemetry `setpoint` field reflects the current ramped value, while `target_setpoint` shows the final target.

**Response:**
```json
{"status": "OK", "target": 35.00, "ramp_rate": 2.0}
```

| Field       | Type   | Unit   | Description                                    |
|-------------|--------|--------|------------------------------------------------|
| `status`    | string | -      | `"OK"` on success                              |
| `target`    | float  | C      | Confirmed target temperature                   |
| `ramp_rate` | float  | C/min  | Active ramp rate (`0` = instant step change)   |

---

### 5.2 Set Ramp Rate

Controls how fast the setpoint moves toward a new target. A value of `0` disables ramping (instantaneous step change).

**Request:**
```json
{"cmd": "set_ramp_rate", "value": 2.0}
```

| Field   | Type   | Unit  | Range    | Required | Description                                  |
|---------|--------|-------|----------|----------|----------------------------------------------|
| `cmd`   | string | -     | -        | Yes      | Must be `"set_ramp_rate"`                    |
| `value` | float  | C/min | >= 0     | Yes      | Setpoint change rate limit. `0` = no ramp.   |

**Response:**
```json
{"status": "OK", "ramp_rate": 2.00}
```

---

### 5.3 Get Ramp Rate

**Request:**
```json
{"cmd": "get_ramp_rate"}
```

**Response:**
```json
{"status": "OK", "ramp_rate": 2.00}
```

---

### 5.4 Get Status

**Request:**
```json
{"cmd": "get_status"}
```

**Response:**
```json
{
  "status": "OK",
  "loop_id": "loop-1",
  "temperature": 28.50,
  "setpoint": 30.00,
  "target_setpoint": 35.00,
  "ramp_active": true,
  "ramp_rate": 2.00,
  "error": 1.50,
  "power": 42.3,
  "enabled": true,
  "sensors": [
    {"id": "sensor-1", "temperature": 28.45, "status": 0},
    {"id": "sensor-2", "temperature": 28.55, "status": 0}
  ],
  "heaters": [
    {"id": "heater-1", "power": 30.2, "max_power_w": 100.0},
    {"id": "heater-2", "power": 12.1, "max_power_w": 50.0}
  ]
}
```

---

### 5.5 Set PID Gains

**Request:**
```json
{"cmd": "set_gains", "kp": 8.0, "ki": 0.2, "kd": 1.5}
```

| Field | Type   | Default | Required | Description         |
|-------|--------|---------|----------|---------------------|
| `cmd` | string | -       | Yes      | Must be `"set_gains"` |
| `kp`  | float  | 5.0     | Yes      | Proportional gain   |
| `ki`  | float  | 0.1     | Yes      | Integral gain       |
| `kd`  | float  | 1.0     | Yes      | Derivative gain     |

**Response:**
```json
{"status": "OK", "kp": 8.00, "ki": 0.20, "kd": 1.50}
```

---

### 5.6 Get PID Gains

**Request:**
```json
{"cmd": "get_gains"}
```

**Response:**
```json
{"status": "OK", "kp": 5.00, "ki": 0.10, "kd": 1.00}
```

---

### 5.7 Enable / Disable Control Loop

**Request:**
```json
{"cmd": "enable", "value": 1}
```

| Field   | Type   | Range | Required | Description                  |
|---------|--------|-------|----------|------------------------------|
| `cmd`   | string | -     | Yes      | Must be `"enable"`           |
| `value` | int    | 0, 1  | Yes      | `1` = enable, `0` = disable |

When a loop is disabled, all associated heaters are set to 0% power and the PID integrator is reset.

**Response:**
```json
{"status": "OK", "enabled": true}
```

---

### 5.8 Set Telemetry Rate

Controls how often telemetry is published for this loop.

**Request:**
```json
{"cmd": "set_telemetry_rate", "value": 5000}
```

| Field   | Type    | Unit | Range         | Required | Description                     |
|---------|---------|------|---------------|----------|---------------------------------|
| `cmd`   | string  | -    | -             | Yes      | Must be `"set_telemetry_rate"`  |
| `value` | integer | ms   | 500 - 60000   | Yes      | Telemetry publish interval      |

**Response:**
```json
{"status": "OK", "telemetry_rate_ms": 5000}
```

---

## 6. System Commands

System commands are sent to `coo/system/cmd` with responses on `coo/system/resp`.

### 6.1 List Loops

**Request:**
```json
{"cmd": "list_loops"}
```

**Response:**
```json
{
  "status": "OK",
  "loops": [
    {"id": "loop-1", "enabled": true, "status": 0},
    {"id": "loop-2", "enabled": false, "status": -1}
  ]
}
```

### 6.2 List Sensors

**Request:**
```json
{"cmd": "list_sensors"}
```

**Response:**
```json
{
  "status": "OK",
  "sensors": [
    {"id": "sensor-1", "type": "rtd", "location": "zone-a", "temperature": 28.5, "status": 0},
    {"id": "sensor-2", "type": "rtd", "location": "zone-b", "temperature": 29.1, "status": 0}
  ]
}
```

### 6.3 List Heaters

**Request:**
```json
{"cmd": "list_heaters"}
```

**Response:**
```json
{
  "status": "OK",
  "heaters": [
    {"id": "heater-1", "type": "buck_boost", "location": "zone-a", "power": 30.2, "max_power_w": 100.0, "enabled": true},
    {"id": "heater-2", "type": "buck_boost", "location": "zone-b", "power": 12.1, "max_power_w": 50.0, "enabled": true}
  ]
}
```

### 6.4 Emergency Stop

Immediately sets all heaters to 0% power and disables all control loops.

**Request:**
```json
{"cmd": "emergency_stop"}
```

**Response:**
```json
{"status": "OK", "all_heaters": "off", "all_loops": "disabled"}
```

### 6.5 Get / Set Network (see Section 2.3)

---

## 7. Command Summary

### 7.1 Per-Loop Commands (`coo/{loop_id}/cmd`)

| Command              | Key Request Fields           | Key Response Fields                                   |
|----------------------|------------------------------|-------------------------------------------------------|
| `set_target`         | `value` (C)                  | `target`, `ramp_rate`                                 |
| `get_status`         | _(none)_                     | `temperature`, `setpoint`, `target_setpoint`, `ramp_active`, `ramp_rate`, `error`, `power`, `sensors[]`, `heaters[]` |
| `set_gains`          | `kp`, `ki`, `kd`            | `kp`, `ki`, `kd`                                     |
| `get_gains`          | _(none)_                     | `kp`, `ki`, `kd`                                     |
| `enable`             | `value` (0/1)                | `enabled`                                             |
| `set_ramp_rate`      | `value` (C/min)              | `ramp_rate`                                           |
| `get_ramp_rate`      | _(none)_                     | `ramp_rate`                                           |
| `set_telemetry_rate` | `value` (ms)                 | `telemetry_rate_ms`                                   |

### 7.2 System Commands (`coo/system/cmd`)

| Command          | Key Request Fields         | Key Response Fields                              |
|------------------|----------------------------|--------------------------------------------------|
| `list_loops`     | _(none)_                   | `loops[]`                                        |
| `list_sensors`   | _(none)_                   | `sensors[]`                                      |
| `list_heaters`   | _(none)_                   | `heaters[]`                                      |
| `emergency_stop` | _(none)_                   | `all_heaters`, `all_loops`                       |
| `get_network`    | _(none)_                   | `ip`, `netmask`, `gateway`, `broker`, `broker_port`, `mqtt_connected` |
| `set_broker`     | `hostname`, `port`         | `broker`, `port`                                 |

---

## 8. PID Controller Details

### 8.1 Algorithm

Discrete PID executed at a fixed 2 Hz rate (500 ms period):

```
error     = setpoint - measured
P_term    = Kp * error
integral += error * dt
I_term    = Ki * clamp(integral, integral_min, integral_max)
D_term    = Kd * (error - prev_error) / dt
output    = clamp(P_term + I_term + D_term, output_min, output_max)
```

### 8.2 Setpoint Ramping

When a ramp rate `R` (C/min) is configured and a new target `T_target` is set:

```
Each control iteration (dt = 0.5s):
  step      = R * (dt / 60)          // max change per iteration
  if |T_target - setpoint| <= step:
      setpoint = T_target            // arrived
  else:
      setpoint += sign(T_target - setpoint) * step
```

The PID always controls against the current `setpoint` (the ramped value), not the final target. This prevents thermal shock and allows smooth transitions. Setting `R = 0` bypasses ramping entirely.

### 8.3 Multi-Sensor Averaging

When a loop has multiple sensors, the measured temperature is the **arithmetic mean** of all valid readings:

```
temperature = sum(valid_sensor_temps) / count(valid_sensors)
```

- Invalid or disabled sensors are excluded from the average
- If all sensors are invalid, the loop enters `SENSOR_ERROR` status and heater output is set to 0%

### 8.4 Multi-Heater Power Distribution

The PID output is a total power demand in watts. This is distributed **proportionally** across heaters based on their maximum rated power:

```
For each heater h in the loop:
  heater_power_w  = total_power * (h.max_power_w / sum_all_max_power_w)
  heater_percent  = (heater_power_w / h.max_power_w) * 100%
```

This ensures each heater operates at the same percentage of its capacity, preventing under- or over-driving mismatched heaters.

### 8.5 Default Parameters

| Parameter                       | Value     | Unit  |
|---------------------------------|-----------|-------|
| Default target temperature      | 30.0      | C     |
| Kp (proportional gain)          | 5.0       | -     |
| Ki (integral gain)              | 0.1       | -     |
| Kd (derivative gain)            | 1.0       | -     |
| Output minimum                  | 0         | %     |
| Output maximum                  | 50        | %     |
| Setpoint ramp rate              | 1.0       | C/min |
| Valid setpoint range             | 0 - 80   | C     |
| Alarm minimum temperature       | 0         | C     |
| Alarm maximum temperature       | 80        | C     |
| Control loop period             | 500       | ms    |
| Default telemetry interval      | 2000      | ms    |

### 8.6 Anti-Windup

The integral accumulator is clamped to `[integral_min, integral_max]` to prevent windup during output saturation. The final output is also clamped to `[output_min, output_max]`.

---

## 9. Status and Error Codes

### 9.1 Loop Status Codes

| Code | Name                     | Description                                     |
|------|--------------------------|-------------------------------------------------|
| 0    | `LOOP_STATUS_OK`         | Operating normally                              |
| -1   | `LOOP_STATUS_DISABLED`   | Loop is disabled, heaters at 0%                 |
| -2   | `LOOP_STATUS_SENSOR_ERROR` | All sensors invalid, heaters at 0%            |
| -3   | `LOOP_STATUS_ALARM`      | Temperature outside alarm thresholds            |
| -4   | `LOOP_STATUS_NOT_INITIALIZED` | Loop not yet initialized                   |

### 9.2 Sensor Status Codes

| Code | Description        |
|------|--------------------|
| 0    | Valid reading      |
| -1   | Sensor disabled    |
| -2   | Read error / stale |
| -3   | Out of range       |

### 9.3 Error Responses

All error responses include a `status` field:

```json
{"status": "ERROR", "code": -2, "message": "sensor error"}
```

Unrecognized commands:
```json
{"error": "unknown command"}
```

---

## 10. Build-Time Configuration

### 10.1 Kconfig Options

| Option                              | Type   | Default                  | Description                    |
|-------------------------------------|--------|--------------------------|--------------------------------|
| `CONFIG_COO_COMMONS`                | bool   | `y`                      | Enable common libraries        |
| `CONFIG_COO_MQTT`                   | bool   | `y`                      | Enable MQTT wrapper            |
| `CONFIG_COO_MQTT_BROKER_HOSTNAME`   | string | `"centaurus.caltech.edu"`| Default broker hostname        |
| `CONFIG_COO_MQTT_BROKER_PORT`       | string | `"1883"`                 | Default broker port            |
| `CONFIG_COO_MQTT_PAYLOAD_SIZE`      | int    | `512`                    | RX/TX buffer size (bytes)      |
| `CONFIG_COO_CONFIG_LIB`            | bool   | `y`                      | Configuration library          |
| `CONFIG_COO_MAX_SENSORS`            | int    | `100`                    | Max sensors system-wide        |
| `CONFIG_COO_MAX_HEATERS`            | int    | `20`                     | Max heaters system-wide        |
| `CONFIG_COO_MAX_CONTROL_LOOPS`      | int    | `8`                      | Max control loops              |
| `CONFIG_COO_MAX_SENSORS_PER_LOOP`   | int    | `20`                     | Max sensors per loop           |
| `CONFIG_COO_MAX_HEATERS_PER_LOOP`   | int    | `4`                      | Max heaters per loop           |
| `CONFIG_COO_SENSORS_LIB`           | bool   | `y`                      | Sensor manager library         |
| `CONFIG_COO_HEATERS_LIB`           | bool   | `y`                      | Heater manager library         |
| `CONFIG_COO_CONTROL_LIB`           | bool   | `y`                      | Control loop library           |
| `CONFIG_NET_DHCPV4`                 | bool   | `y`                      | Enable DHCP                    |
| `CONFIG_DNS_RESOLVER`               | bool   | `y`                      | Enable DNS resolution          |

### 10.2 Configuration File (Future)

The `thermal_config_t` struct supports a full declarative configuration. Currently defaults are hardcoded in `config.c`. A future JSON or YAML configuration file will allow build-time customization of:

- Number and IDs of sensors, heaters, and control loops
- Sensor-to-loop and heater-to-loop assignments
- PID gains, power limits, alarm thresholds, ramp rates per loop
- Setpoint range validation per loop

---

## 11. Hardware Interface

### 11.1 Sensors

| Parameter        | Sensor 1           | Sensor 2           |
|------------------|--------------------|--------------------|
| Sensor ID        | `sensor-1`         | `sensor-2`         |
| Device           | AD7124 ADC         | AD7124 ADC         |
| Interface        | SPI1 @ 1 MHz       | SPI1 @ 1 MHz       |
| Resolution       | 24-bit             | 24-bit             |
| Gain             | 4x                 | 4x                 |
| Reference        | External           | External           |
| RTD input pins   | AIN2 / AIN3        | AIN4 / AIN5        |
| Read rate        | 2 Hz               | 2 Hz               |

### 11.2 Heaters

| Parameter        | Heater 1                        | Heater 2                        |
|------------------|---------------------------------|---------------------------------|
| Heater ID        | `heater-1`                      | `heater-2`                      |
| Device           | TPS55287Q1 Buck-Boost           | TPS55287Q1 Buck-Boost           |
| Interface        | I2C1 @ 0x74                     | I2C1 @ 0x75                     |
| Sense resistor   | 20 mOhm                         | 20 mOhm                         |
| Max power        | 100 W                           | 50 W                            |
| Power range      | 0 - 50%                         | 0 - 50%                         |

### 11.3 Target Board

| Parameter        | Value                               |
|------------------|-------------------------------------|
| Board            | NUCLEO-H563ZI                       |
| MCU              | STM32H563ZI                         |
| Networking       | Ethernet (STM32 HAL), IPv4, DHCP    |

---

## 12. Threading Model

| Thread         | Priority | Period  | Role                                                     |
|----------------|----------|---------|----------------------------------------------------------|
| Sensor         | 5        | 500 ms  | Read all sensors, update cache                           |
| Control        | 7        | 500 ms  | Run all PID loops, distribute heater power, ramp setpoints |
| MQTT           | 8        | event   | Socket polling, command dispatch, telemetry publish      |
| Main           | 0        | 100 ms  | System health monitoring, alarm checking                 |

All control loop and sensor/heater state is protected by mutexes. MQTT command callbacks acquire the `control_mutex` before modifying loop parameters, ensuring thread-safe access between the MQTT event thread and the control thread.

---

## 13. Sequence Diagrams

### 13.1 Set Target with Ramping (Multi-Heater)

```
Client                    Broker                  Controller
  |                         |                         |
  |-- PUBLISH ------------->|                         |
  |   coo/loop-1/cmd        |-- DELIVER ------------->|
  |   {"cmd":"set_target",  |                         |
  |    "value":50.0}        |                   [mutex lock]
  |                         |                   [set target_setpoint=50.0]
  |                         |                   [ramp_active=true]
  |                         |                   [mutex unlock]
  |                         |<-- PUBLISH -------------|
  |<-- DELIVER -------------|   coo/loop-1/resp       |
  |                         |   {"status":"OK",       |
  |                         |    "target":50.0,       |
  |                         |    "ramp_rate":2.0}     |
  |                         |                         |
  :   (control loop runs every 500ms)                 :
  :                         :                         :
  |                         |   [ramp: sp 30.0->30.017]
  |                         |   [PID: error=0.017]    |
  |                         |   [distribute power to  |
  |                         |    heater-1 and heater-2]|
  :         ...ramp continues...                      :
  |                         |                         |
  |                         |<-- PUBLISH -------------|
  |<-- DELIVER -------------|   coo/loop-1/telemetry  |
  |  {"loop_id":"loop-1",  |                         |
  |   "temperature":31.2,  |                         |
  |   "setpoint":32.0,     |                         |
  |   "target_setpoint":50.0,                        |
  |   "ramp_active":true,  |                         |
  |   "sensors":[...],     |                         |
  |   "heaters":[...]}     |                         |
```

### 13.2 Multi-Sensor Averaging

```
Control Thread (every 500ms per loop)
  |
  |-- sensor_manager_get_average(["sensor-1","sensor-2"])
  |     |-- get_reading("sensor-1") -> 28.45 C [OK]
  |     |-- get_reading("sensor-2") -> 28.55 C [OK]
  |     |-- average = (28.45 + 28.55) / 2 = 28.50 C
  |     '-- return 28.50 C
  |
  |-- pid_update(setpoint=35.0, measured=28.50, dt=0.5)
  |     '-- return power = 42.3%
  |
  |-- heater_manager_distribute_power(["heater-1","heater-2"], 42.3W)
  |     |-- heater-1: max=100W -> 100/150 * 42.3 = 28.2W -> 28.2%
  |     |-- heater-2: max=50W  ->  50/150 * 42.3 = 14.1W -> 28.2%
  |     '-- (both run at same % of capacity)
  |
  '-- done
```

### 13.3 Sensor Failure Handling

```
Control Thread
  |
  |-- sensor_manager_get_average(["sensor-1","sensor-2"])
  |     |-- get_reading("sensor-1") -> 28.45 C [OK]
  |     |-- get_reading("sensor-2") -> ERROR [status=-2]
  |     |-- average = 28.45 / 1 = 28.45 C  (skip invalid)
  |     '-- return 28.45 C
  |
  |-- PID continues with single sensor
  |
  === If ALL sensors fail: ===
  |
  |-- sensor_manager_get_average(...) -> ERROR
  |-- loop status = LOOP_STATUS_SENSOR_ERROR (-2)
  |-- all heaters set to 0%
  '-- telemetry reports status: -2
```

---

## Revision History

| Version | Date       | Author | Description                                                    |
|---------|------------|--------|----------------------------------------------------------------|
| 1.0     | 2026-03-11 | -      | Initial release (single loop)                                  |
| 2.0     | 2026-03-11 | -      | Multi-loop/sensor/heater support, ramp rate, network config, system commands |
