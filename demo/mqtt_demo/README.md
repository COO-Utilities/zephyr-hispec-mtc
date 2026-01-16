# MQTT Demo

Demo the `coo_mqtt` client library with Ethernet connectivity on the NUCLEO-H563ZI board.

## Features

- Connects to an MQTT broker over Ethernet
- Subscribes to `coo/demo/cmd` for incoming commands
- Publishes status to `coo/demo/status`
- Echoes received commands back with "ACK: " prefix

## Build and Flash

```bash
west build -b nucleo_h563zi demo/mqtt_demo --pristine
west flash
```

## Configuration

### MQTT Broker

The MQTT broker hostname and port are configured via Kconfig options in `prj.conf`.

Edit `prj.conf` to set your broker:

```
CONFIG_COO_MQTT_BROKER_HOSTNAME="your-broker.example.com"
CONFIG_COO_MQTT_BROKER_PORT="1883"
```

After changing broker settings, rebuild with `--pristine` to ensure the new configuration is applied:

```bash
west build -b nucleo_h563zi demo/mqtt_demo --pristine
```

### Network Configuration

The demo supports both DHCP and static IP addressing.

#### DHCP (Default)

DHCP is enabled by default. The relevant settings in `prj.conf`:

```
CONFIG_NET_DHCPV4=y
```

#### Static IP

To use a static IP address, modify `prj.conf`:

1. Remove or comment out DHCP:
```
# CONFIG_NET_DHCPV4=y
```

2. Add static IP configuration:
```
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_NEED_IPV4=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.1.100"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
CONFIG_NET_CONFIG_MY_IPV4_GW="192.168.1.1"
```

## Testing

### Monitor Serial Output

Connect to the board's serial port at 115200 baud:

```bash
screen /dev/tty.usbmodem* 115200
```

### Subscribe to Status

```bash
mosquitto_sub -h your-broker.example.com -t "coo/demo/status" -v
```

You should see `coo/demo/status online` when the board connects.

### Send Commands

```bash
mosquitto_pub -h your-broker.example.com -t "coo/demo/cmd" -m "hello"
```

The board will echo back: `coo/demo/status ACK: hello`
