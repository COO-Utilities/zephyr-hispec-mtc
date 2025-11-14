#ifndef ZEPHYR_DRIVERS_HEATER_H_
#define ZEPHYR_DRIVERS_HEATER_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

__subsystem struct heater_driver_api {
    int (*reg_write)(const struct device *dev, uint8_t reg, uint8_t val);
    int (*reg_read)(const struct device *dev, uint8_t reg, uint8_t *val);
};

static inline int heater_reg_write(const struct device *dev, uint8_t reg, uint8_t val) {
    const struct heater_driver_api *api = (const struct heater_driver_api *)dev->api;

    return api->reg_write(dev, reg, val);
}

static inline int heater_reg_read(const struct device *dev, uint8_t reg, uint8_t *val) {
    const struct heater_driver_api *api = (const struct heater_driver_api *)dev->api;

    return api->reg_read(dev, reg, val);
}

#ifdef __cplusplus
}
#endif

#endif // ZEPHYR_DRIVERS_HEATER_H_