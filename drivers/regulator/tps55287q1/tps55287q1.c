#define DT_DRV_COMPAT ti_tps55287q1

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>

#include "tps55287q1.h"

LOG_MODULE_REGISTER(tps55287q1, CONFIG_REGULATOR_TPS55287Q1_LOG_LEVEL);

static int tps55287q1_reg_read(const struct device *dev, uint8_t reg, uint8_t *val) {
	const struct tps55287q1_config *cfg = dev->config;

	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static int tps55287q1_reg_write(const struct device *dev, uint8_t reg, uint8_t val){
	const struct tps55287q1_config *cfg = dev->config;

	return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

static int tps55287q1_update_bits(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t value) {
    const struct tps55287q1_config *cfg = dev->config;

    return i2c_reg_update_byte_dt(&cfg->i2c, reg, mask, value);
}

static int tps55287q1_voltage_to_adc(const struct tps55287q1_config *cfg, int32_t vout_uv, uint16_t *adc) {
	const double intfb_ratio_table[] = {0.2256, 0.1128, 0.0752, 0.0564};
	const double intfb_ratio = intfb_ratio_table[cfg->intfb & 0x03];

	const double vref_min_uv = 45000.0;   /* 45 mV */
	const double vref_max_uv = 1200000.0; /* 1.200 V */
	const double vref_step_uv = 564.5;    /* 0.5645 mV */

	double vref_uv = (double)vout_uv * intfb_ratio;
	if (vref_uv < vref_min_uv) {
		vref_uv = vref_min_uv;
	} else if (vref_uv > vref_max_uv) {
		vref_uv = vref_max_uv;
	}

	double adc_double = (vref_uv - vref_min_uv) / vref_step_uv;
	if (adc_double < 0.0) {
		adc_double = 0.0;
	}
	if (adc_double > 2047.0) {
		adc_double = 2047.0;
	}

	uint16_t adc_int = (uint16_t)(adc_double);

	*adc = adc_int;

	return 0;
}

static int tps55287q1_adc_to_voltage(const struct tps55287q1_config *cfg, uint16_t adc, int32_t *vout_uv) {
	const double intfb_ratio_table[] = {0.2256, 0.1128, 0.0752, 0.0564};
	const double intfb_ratio = intfb_ratio_table[cfg->intfb & 0x03];

	const double vref_min_uv = 45000.0;   /* 45 mV */
	const double vref_step_uv = 564.5;    /* 0.5645 mV */

	double vref_uv = vref_min_uv + ((double)adc * vref_step_uv);
	double vout_uv_double = vref_uv / intfb_ratio;

	*vout_uv = (int32_t)(vout_uv_double);

	return 0;
}

/* --- regulator API callbacks --- */
static int regulator_tps55287q1_enable(const struct device *dev) {
    int ret;

    ret = tps55287q1_update_bits(dev, TPS55287Q1_REG_MODE, TPS55287Q1_MODE_OE, TPS55287Q1_MODE_OE);
    if (ret < 0) {
        LOG_ERR("Failed to enable regulator: %d", ret);
		return ret;
    }

    return 0;
}

static int regulator_tps55287q1_disable(const struct device *dev) {	
    int ret;

    ret = tps55287q1_update_bits(dev, TPS55287Q1_REG_MODE, TPS55287Q1_MODE_OE, 0);
    if (ret < 0) {
		LOG_ERR("Failed to disable regulator: %d", ret);
        return ret;
    }

    return 0;
}

static int regulator_tps55287q1_set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv) {
	const struct tps55287q1_config *cfg = dev->config;

	uint16_t adc;
	int ret;

	ret = tps55287q1_voltage_to_adc(cfg, min_uv, &adc);
	if (ret < 0) {
		LOG_ERR("Failed to convert VOUT to code: %d", ret);
		return ret;
	}

	uint8_t lsb = adc & 0xFF;
	uint8_t msb = (adc >> 8) & 0x07;

	ret = tps55287q1_reg_write(dev, TPS55287Q1_REG_VREF_LSB, lsb);
	if (ret < 0) {
		LOG_ERR("Failed to write LSB: %d", ret);
		return ret;
	}

	ret = tps55287q1_reg_write(dev, TPS55287Q1_REG_VREF_MSB, msb);
	if (ret < 0) {
		LOG_ERR("Failed to write MSB: %d", ret);
		return ret;
	}

	return 0;
}

static int regulator_tps55287q1_get_voltage(const struct device *dev, int32_t *volt_uv) {
	const struct tps55287q1_config *cfg = dev->config;

	uint8_t lsb, msb;
	uint16_t code;
	int ret;

	ret = tps55287q1_reg_read(dev, TPS55287Q1_REG_VREF_LSB, &lsb);
	if (ret < 0) {
		LOG_ERR("Failed to read LSB: %d", ret);
		return ret;
	}
	ret = tps55287q1_reg_read(dev, TPS55287Q1_REG_VREF_MSB, &msb);
	if (ret < 0) {
		LOG_ERR("Failed to read MSB: %d", ret);
		return ret;
	}

	code = ((uint16_t)(msb & 0x07) << 8) | lsb;

	ret = tps55287q1_adc_to_voltage(cfg, code, volt_uv);
	if (ret < 0) {
		LOG_ERR("Failed to convert code to VOUT: %d", ret);	
		return ret;
	}

	return 0;
}

static int regulator_tps55287q1_set_active_discharge(const struct device *dev, bool active_discharge) {
	const struct tps55287q1_config *cfg = dev->config;

	uint8_t mask, val;

	if (active_discharge) {
		LOG_DBG("Enabling active discharge");
		if (cfg->force_discharge) {
			LOG_DBG("force_discharge config set; active discharge will be forced on disable");
			mask = TPS55287Q1_MODE_DISCHG | TPS55287Q1_MODE_FORCE_DISCHG;
			val = TPS55287Q1_MODE_DISCHG | TPS55287Q1_MODE_FORCE_DISCHG;
		} else {
			mask = TPS55287Q1_MODE_DISCHG;
			val = TPS55287Q1_MODE_DISCHG;
		}
	} else {
		LOG_DBG("Disabling active discharge");
		mask = TPS55287Q1_MODE_DISCHG;
		val = 0;
	}

	return tps55287q1_update_bits(dev, TPS55287Q1_REG_MODE, mask, val);
}

static int regulator_tps55287q1_get_active_discharge(const struct device *dev, bool *active_discharge) {
	uint8_t mode;
	int ret;

	ret = tps55287q1_reg_read(dev, TPS55287Q1_REG_MODE, &mode);
	if (ret < 0) {
		LOG_ERR("Failed to read MODE register: %d", ret);
		return ret;
	}

	*active_discharge = (mode & TPS55287Q1_MODE_DISCHG) != 0U;

	return 0;
}

static const struct regulator_driver_api tps55287q1_regulator_api = {
	.enable              = regulator_tps55287q1_enable,
	.disable             = regulator_tps55287q1_disable,
	.set_voltage         = regulator_tps55287q1_set_voltage,
	.get_voltage		 = regulator_tps55287q1_get_voltage,
	.set_active_discharge = regulator_tps55287q1_set_active_discharge,
	.get_active_discharge = regulator_tps55287q1_get_active_discharge,
};

static int tps55287q1_init(const struct device *dev) {
	const struct tps55287q1_config *cfg = dev->config;

	int ret;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	regulator_common_data_init(dev);

	uint8_t fs_val = cfg->intfb & TPS55287Q1_VOUT_FS_INTFB;
	ret = tps55287q1_update_bits(dev, TPS55287Q1_REG_VOUT_FS, TPS55287Q1_VOUT_FS_FB | TPS55287Q1_VOUT_FS_INTFB, fs_val);
	if (ret < 0) {
		LOG_ERR("%s: Failed to configure VOUT_FS register: %d", dev->name, ret);
		return ret;
	}

	ret = regulator_common_init(dev, false);
	if (ret < 0) {
		LOG_ERR("%s: Failed to initialize regulator: %d", dev->name, ret);
		return ret;
	}

	return ret;
}

#define REGULATOR_TPS55287Q1_DEFINE(inst)                                           \
    static struct tps55287q1_data tps55287q1_data_##inst = {                 		\
    };                                                                       		\
    static const struct tps55287q1_config tps55287q1_config_##inst = {            	\
        .common      = REGULATOR_DT_INST_COMMON_CONFIG_INIT(inst),               	\
        .i2c         = I2C_DT_SPEC_INST_GET(inst),                               	\
        .intfb       = DT_INST_PROP_OR(inst, intfb, 3),                       		\
        .force_discharge = DT_INST_PROP_OR(inst, force_discharge, false),       	\
    };                                                                            	\
                                                                                  	\
    DEVICE_DT_INST_DEFINE(inst,                                                   	\
                          tps55287q1_init,                                        	\
                          NULL,                                                   	\
                          &tps55287q1_data_##inst,                                	\
                          &tps55287q1_config_##inst,                              	\
                          POST_KERNEL,                                            	\
                          CONFIG_REGULATOR_TPS55287Q1_INIT_PRIORITY,             	\
                          &tps55287q1_regulator_api);

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_TPS55287Q1_DEFINE);
