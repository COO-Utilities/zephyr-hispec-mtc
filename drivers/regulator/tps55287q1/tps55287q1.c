#define DT_DRV_COMPAT ti_tps55287q1

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>

#include "tps55287q1.h"

LOG_MODULE_REGISTER(tps55287q1, CONFIG_TPS55287Q1_LOG_LEVEL);

static int tps55287q1_reg_write(const struct device *dev, uint8_t reg, uint8_t val) {
    const struct tps55287q1_config *cfg = dev->config;
    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("%s device not ready", cfg->i2c.bus->name);
        return -ENODEV;
    }

    return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

static int tps55287q1_reg_read(const struct device *dev, uint8_t reg, uint8_t *val) {
    const struct tps55287q1_config *cfg = dev->config;
    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("%s device not ready", cfg->i2c.bus->name);
        return -ENODEV;
    }

    return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

int tps55287q1_set_vref_uv(const struct device *dev, uint32_t vref_uv) {
    struct tps55287q1_data *data = dev->data;

    double vref_mv = vref_uv / 1000.0;

    if (vref_mv < 45.0) {
        vref_mv = 45.0;
    }
    if (vref_mv > 1200.0) {
        vref_mv = 1200.0;
    }

    double code_f = (vref_mv - 45.0) / 0.5645;
    int32_t code = (int32_t)llround(code_f);

    if (code < 0) {
        code = 0;
    }
    if (code > 0x7FF) {
        code = 0x7FF;
    }

    uint8_t lsb = code & 0xFF;
    uint8_t msb = (code >> 8) & 0x07;

    int ret = tps55287q1_reg_write(dev, TPS55287Q1_VREF_LSB, lsb);
    if (ret) {
        return ret;
    }

    ret = tps55287q1_reg_write(dev, TPS55287Q1_VREF_MSB, msb);
    if (ret) {
        return ret;
    }

    data->cached_vref_uv = vref_uv;

    return 0;
}

int tps55287q1_config_feedback(const struct device *dev, bool use_ext_fb, uint8_t intfb_code) {
    uint8_t val = 0;

    if (intfb_code > 3) {
        return -EINVAL;
    }

    val |= (use_ext_fb ? BIT(7) : 0);
    val |= (intfb_code & 0x3);

    return tps55287q1_reg_write(dev, TPS55287Q1_VOUT_FS, val);
}

int tps55287q1_set_vout_mv(const struct device *dev, uint32_t vout_mv, uint8_t intfb_code) {
    double intfb;

    switch (intfb_code & 0x3) {
        case 0: intfb = 0.2256; break;
        case 1: intfb = 0.1128; break;
        case 2: intfb = 0.0752; break;
        case 3: intfb = 0.0564; break;
        default:
            return -EINVAL;
    }

    double vout_v = vout_mv / 1000.0;
    double vref_v = vout_v * intfb;
    uint32_t vref_uv = (uint32_t)llround(vref_v * 1e6);

    return tps55287q1_set_vref_uv(dev, vref_uv);
}

int tps55287q1_set_current_limit(const struct device *dev, uint32_t limit_ma, uint32_t rsense_milliohm, bool enable) {
    if (rsense_milliohm == 0U) {
        return -EINVAL;
    }

    double limit_a = limit_ma / 1000.0;
    double vsense_mv = (limit_a * rsense_milliohm) / 1000.0;
    double code_f = vsense_mv / 0.5;
    int32_t code = (int32_t)llround(code_f);

    if (code < 0) {
        code = 0;
    }
    if (code > 0x7F) {
        code = 0x7F;
    }

    uint8_t reg = (uint8_t)(code & 0x7F);
    if (enable) {
        reg |= BIT(7);
    }

    return tps55287q1_reg_write(dev, TPS55287Q1_IOUT_LIMIT, reg);
}

int tps55287q1_enable_output(const struct device *dev, bool enable) {
    struct tps55287q1_data *data = dev->data;
    uint8_t mode;
    int ret = tps55287q1_reg_read(dev, TPS55287Q1_MODE, &mode);
    if (ret) {
        return ret;
    }

    if (enable) {
        mode |= TPS55287Q1_MODE_OE;
    } else {
        mode &= ~TPS55287Q1_MODE_OE;
    }

    ret = tps55287q1_reg_write(dev, TPS55287Q1_MODE, mode);
    if (!ret) {
        data->enabled = enable;
    }

    return ret;
}

int tps55287q1_get_status(const struct device *dev, uint8_t *status) {
    if (!status) {
        return -EINVAL;
    }

    return tps55287q1_reg_read(dev, TPS55287Q1_STATUS, status);
}

int tps55287q1_check_faults(const struct device *dev) {
    uint8_t status;
    int ret = tps55287q1_get_status(dev, &status);
    if (ret) {
        return ret;
    }

    if (status & (TPS55287Q1_STATUS_SCP |
             TPS55287Q1_STATUS_OCP |
             TPS55287Q1_STATUS_OVP)) {
        return -EIO;
    }

    return 0;
}

static int tps55287q1_init(const struct device *dev) {
    const struct tps55287q1_config *cfg = dev->config;
    struct tps55287q1_data *data = dev->data;

    data->i2c_dev = dev;

    if(!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("%s is not ready", cfg->i2c.bus->name);
        return -ENODEV;
    }

    uint8_t status;
    int ret = tps55287q1_get_status(dev, &status);
    if (ret) {
        LOG_ERR("TPS55287-Q1 not responding on I2C (%d)", ret);
        return ret;
    }
    LOG_INF("TPS55287-Q1 detected, STATUS=0x%02x", status);

    if (cfg->intfb <= 3) {
        ret = tps55287q1_config_feedback(dev, false, cfg->intfb);
        if (ret) {
            LOG_ERR("config_feedback failed (%d)", ret);
            return ret;
        }
    }

    if (cfg->default_vout_mv != 0U && cfg->intfb <= 3) {
        ret = tps55287q1_set_vout_mv(dev, cfg->default_vout_mv, cfg->intfb);
        if (ret) {
            LOG_ERR("set_vout_mv(%u mV) failed (%d)", cfg->default_vout_mv, ret);
            return ret;
        }
        data->cached_vout_uv = cfg->default_vout_mv * 1000U;
    }

    if (cfg->rsense_milliohm != 0U && cfg->default_current_limit_ma != 0U) {
        ret = tps55287q1_set_current_limit(dev, cfg->default_current_limit_ma, cfg->rsense_milliohm, true);
        if (ret) {
            LOG_WRN("set_current_limit(%u mA) failed (%d)", cfg->default_current_limit_ma, ret);
        } else {
            data->cached_curr_limit_ua = cfg->default_current_limit_ma * 1000U;
        }
    }

    if (cfg->enable_at_boot) {
        ret = tps55287q1_enable_output(dev, true);
        if (ret) {
            LOG_ERR("enable_at_boot failed (%d)", ret);
            return ret;
        }
    }

    return 0;
}

// Regulator API wrappers
static int regulator_tps55287q1_enable(const struct device *dev) {
    return tps55287q1_enable_output(dev, true);
}

static int regulator_tps55287q1_disable(const struct device *dev) {
    // return tps55287q1_enable_output(dev, false);
    ARG_UNUSED(dev);
    LOG_INF("tps55287q1_reg_disable() stub called");
    return 0;
}

static int regulator_tps55287q1_set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv) {
    const struct tps55287q1_config *cfg = dev->config;
    struct tps55287q1_data *data = dev->data;

    if (min_uv <= 0 || max_uv <= 0 || min_uv > max_uv) {
        return -EINVAL;
    }

    int32_t target_uv = min_uv;
    if (target_uv < 45000) {
        target_uv = 45000;
    }

    uint32_t target_mv = (uint32_t)(target_uv / 1000);

    int ret = tps55287q1_set_vout_mv(dev, target_mv, cfg->intfb);
    if (!ret) {
        data->cached_vout_uv = target_mv * 1000U;
    }

    return ret;
}

static int regulator_tps55287q1_get_voltage(const struct device *dev, int32_t *volt_uv) {
    const struct tps55287q1_config *cfg = dev->config;
    struct tps55287q1_data *data = dev->data;

    if (!volt_uv) {
        return -EINVAL;
    }

    if (data->cached_vout_uv != 0U) {
        *volt_uv = (int32_t)data->cached_vout_uv;
        return 0;
    }

    double intfb;
    switch (cfg->intfb & 0x3) {
        case 0: intfb = 0.2256; break;
        case 1: intfb = 0.1128; break;
        case 2: intfb = 0.0752; break;
        case 3: intfb = 0.0564; break;
        default:
            return -EINVAL;
    }

    if (data->cached_vref_uv == 0U) {
        uint8_t lsb, msb;
        int ret = tps55287q1_reg_read(dev, TPS55287Q1_VREF_LSB, &lsb);
        if (ret) {
            return ret;
        }
        ret = tps55287q1_reg_read(dev, TPS55287Q1_VREF_MSB, &msb);
        if (ret) {
            return ret;
        }

        int32_t code = ((msb & 0x07) << 8) | lsb;
        double vref_mv = 45.0 + 0.5645 * code;
        data->cached_vref_uv = (uint32_t)llround(vref_mv * 1000.0);
    }

    double vref_v = data->cached_vref_uv / 1e6;
    double vout_v = vref_v / intfb;
    *volt_uv = (int32_t)llround(vout_v * 1e6);

    return 0;
}

static int regulator_tps55287q1_set_current_limit(const struct device *dev, int32_t min_ua, int32_t max_ua) {
    const struct tps55287q1_config *cfg = dev->config;
    struct tps55287q1_data *data = dev->data;

    if (cfg->rsense_milliohm == 0U) {
        return -ENOSYS;
    }
    if (min_ua <= 0 || max_ua <= 0 || min_ua > max_ua) {
        return -EINVAL;
    }

    int32_t target_ua = min_ua;
    uint32_t target_ma = (uint32_t)(target_ua / 1000);

    int ret = tps55287q1_set_current_limit(dev, target_ma, cfg->rsense_milliohm, true);
    if (!ret) {
        data->cached_curr_limit_ua = (uint32_t)target_ua;
    }

    return ret;
}

static int regulator_tps55287q1_get_current_limit(const struct device *dev, int32_t *curr_ua) {
    struct tps55287q1_data *data = dev->data;

    if (!curr_ua) {
        return -EINVAL;
    }

    if (data->cached_curr_limit_ua == 0U) {
        return -ENOSYS;
    }

    *curr_ua = (int32_t)data->cached_curr_limit_ua;

    return 0;
}

static int regulator_tps55287q1_get_error_flags(const struct device *dev, regulator_error_flags_t *flags) {
    if (!flags) {
        return -EINVAL;
    }

    uint8_t status;
    int ret = tps55287q1_get_status(dev, &status);
    if (ret) {
        return ret;
    }

    regulator_error_flags_t f = 0;

    if (status & TPS55287Q1_STATUS_OVP) {
        f |= REGULATOR_ERROR_OVER_VOLTAGE;
    }
    if (status & (TPS55287Q1_STATUS_OCP | TPS55287Q1_STATUS_SCP)) {
        f |= REGULATOR_ERROR_OVER_CURRENT;
    }

    *flags = f;

    return 0;
}

static const struct regulator_driver_api tps55287q1_regulator_api = {
    .enable            = regulator_tps55287q1_enable,
    .disable           = regulator_tps55287q1_disable,
    .set_voltage       = regulator_tps55287q1_set_voltage,
    .get_voltage       = regulator_tps55287q1_get_voltage,
    .set_current_limit = regulator_tps55287q1_set_current_limit,
    .get_current_limit = regulator_tps55287q1_get_current_limit,
    .get_error_flags   = regulator_tps55287q1_get_error_flags,
};

#define REGULATROR_TPS55287Q1_DEFINE(inst)                                                      \
    static struct tps55287q1_data tps55287q1_data_##inst;                                   \
    static const struct tps55287q1_config tps55287q1_config_##inst = {                      \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                                  \
        .intfb = DT_INST_PROP_OR(inst, ti_intfb_code, 0xFF),                                \
        .default_vout_mv = DT_INST_PROP_OR(inst, ti_default_vout_mv, 0),                    \
        .rsense_milliohm = DT_INST_PROP_OR(inst, rsense_milliohm, 0),                       \
        .default_current_limit_ma = DT_INST_PROP_OR(inst, ti_default_current_limit_ma, 0),  \
        .enable_at_boot = DT_INST_PROP_OR(inst, ti_enable_at_boot, false),                  \
    };                                                                                      \
                                                                                            \
    DEVICE_DT_INST_DEFINE(                                                                  \
        inst,                                                                               \
        tps55287q1_init,                                                                    \
        NULL,                                                                               \
        &tps55287q1_data_##inst,                                                            \
        &tps55287q1_config_##inst,                                                          \
        POST_KERNEL,                                                                        \
        CONFIG_TPS55287Q1_INIT_PRIORITY,                                                    \
        &tps55287q1_regulator_api                                                           \
    );                                                                                      \

DT_INST_FOREACH_STATUS_OKAY(REGULATROR_TPS55287Q1_DEFINE)