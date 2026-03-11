# MQTT + PID Control Demo

Closed-loop PID temperature control with MQTT remote monitoring and command interface.
Combines the `pid_loop` and `mqtt_demo` demos into a single application.

## What it does

- Reads temperature from an AD7124 ADC (RTD sensor) at 500 ms intervals
- Runs a PID controller to drive a TPS55287Q1 heater regulator toward a target temperature
- Publishes telemetry (temperature, setpoint, error, heater power) over MQTT every 2 seconds
- Accepts JSON commands over MQTT to change the setpoint, tune PID gains, or enable/disable the loop

## Architecture

Two threads run concurrently:

| Thread | Priority | Role |
|--------|----------|------|
| Main   | 0        | PID control loop: read sensors, update PID, apply heater output, publish telemetry |
| MQTT   | 8        | MQTT event processing: handle incoming messages and keepalives |

The `control_loop` APIs are mutex-protected, so MQTT command handlers can safely call into the
control subsystem from the MQTT thread while the main thread runs the PID loop.

## Hardware

- **Board:** NUCLEO-H563ZI
- **Sensor:** AD7124 ADC on SPI1 (RTD temperature measurement)
- **Heater:** TPS55287Q1 buck-boost regulator on I2C1
- **Network:** On-board Ethernet (DHCP)

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `coo/pid/telemetry` | publish | Periodic status every 2 s |
| `coo/pid/cmd` | subscribe | Incoming JSON commands |
| `coo/pid/resp` | publish | Command responses |

## Commands

All commands are JSON objects sent to `coo/pid/cmd`. Responses appear on `coo/pid/resp`.

### Set target temperature (Celsius)

```sh
mosquitto_pub -t "coo/pid/cmd" -m '{"cmd":"set_target","value":35.0}'
# {"status":"OK","target":35.00}
```

### Get current status

```sh
mosquitto_pub -t "coo/pid/cmd" -m '{"cmd":"get_status"}'
# {"temperature":28.50,"setpoint":35.00,"error":6.50,"power":42.3,"status":"OK"}
```

### Set PID gains

```sh
mosquitto_pub -t "coo/pid/cmd" -m '{"cmd":"set_gains","kp":8.0,"ki":0.2,"kd":1.5}'
# {"status":"OK","kp":8.00,"ki":0.20,"kd":1.50}
```

### Get PID gains

```sh
mosquitto_pub -t "coo/pid/cmd" -m '{"cmd":"get_gains"}'
# {"status":"OK","kp":5.00,"ki":0.10,"kd":1.00}
```

### Enable / disable the control loop

```sh
mosquitto_pub -t "coo/pid/cmd" -m '{"cmd":"enable","value":0}'
# {"status":"OK","enabled":false}
```

## Telemetry format

Published to `coo/pid/telemetry` every 2 seconds:

```json
{"temperature":28.50,"setpoint":35.00,"error":6.50,"power":42.3,"iteration":100}
```

Subscribe to watch it live:

```sh
mosquitto_sub -t "coo/pid/telemetry"
```

## Default parameters

| Parameter | Value |
|-----------|-------|
| Target temperature | 30.0 C |
| PID Kp | 5.0 |
| PID Ki | 0.1 |
| PID Kd | 1.0 |
| Power limit | 0 - 50 % |
| Alarm range | 0 - 80 C |
| Control loop period | 500 ms |
| MQTT broker | centaurus.caltech.edu:1883 |

## Build and flash

```sh
west build -b nucleo_h563zi demo/mqtt_pid --pristine
west flash
```

## Libraries used

- `lib/config` - hardware configuration
- `lib/sensors` - sensor manager (AD7124 ADC)
- `lib/heaters` - heater manager (TPS55287Q1)
- `lib/control` - PID control loop
- `lib/coo_commons` - PID algorithm, MQTT client, networking
