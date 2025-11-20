#define DT_DRV_COMPAT ti_tps55287q1

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include <drivers/heater.h>
#include "tps55287q1.h"

LOG_MODULE_REGISTER(tps55287q1, CONFIG_TPS55287Q1_LOG_LEVEL);

static int tps55287q1_init(const struct device *dev) {
    const struct tps55287q1_config *cfg = dev->config;

    if(!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("%s device not found", cfg->i2c.bus->name);
        return -ENODEV;
    }

    // LOG_INF("%s device found", cfg->i2c.bus->name);

    return 0;
}

int tps55287q1_reg_write(const struct device *dev, uint8_t reg, uint8_t val) {
    const struct tps55287q1_config *cfg = dev->config;
    int ret = i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
    if (ret < 0) {
        LOG_ERR("reg_write failed: reg=0x%02x, err=%d", reg, ret);
    }

    return ret;
}

int tps55287q1_reg_read(const struct device *dev, uint8_t reg, uint8_t *val) {
    const struct tps55287q1_config *cfg = dev->config;
    int ret = i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
    if (ret < 0) {
        LOG_ERR("reg_read failed: reg=0x%02x, err=%d", reg, ret);
    }

    return ret;
}

static const struct heater_driver_api tps55287q1_driver_api = {
    .reg_write = tps55287q1_reg_write,
    .reg_read = tps55287q1_reg_read,
};

#define HEATER_TPS55287Q1_DEFINE(inst)                                          \
    static struct tps55287q1_data tps55287q1_data_##inst;                       \
    static const struct tps55287q1_config tps55287q1_config_##inst = {          \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                      \
    };                                                                          \
                                                                                \
    DEVICE_DT_INST_DEFINE(                                                      \
        inst,                                                                   \
        tps55287q1_init,                                                        \
        NULL,                                                                   \
        &tps55287q1_data_##inst,                                                \
        &tps55287q1_config_##inst,                                              \
        POST_KERNEL,                                                            \
        CONFIG_TPS55287Q1_INIT_PRIORITY,                                        \
        &tps55287q1_driver_api                                                  \
    );                                                                          \

DT_INST_FOREACH_STATUS_OKAY(HEATER_TPS55287Q1_DEFINE)