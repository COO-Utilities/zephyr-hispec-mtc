/* MQTT command and telemetry interface for the thermal controller app.
 * Runs the coo_commons dispatcher over MQTT in its own threads. */

#ifndef MQTT_INTERFACE_H
#define MQTT_INTERFACE_H

/* Start the network/MQTT command runtime and telemetry publisher.
 * Call after the sensor/heater/control managers are initialized. */
void mqtt_interface_start(void);

#endif /* MQTT_INTERFACE_H */
