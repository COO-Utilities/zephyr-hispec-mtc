#define DT_DRV_COMPAT ti_tps55287q1

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>

#include "tps55287q1.h"

LOG_MODULE_REGISTER(tps55287q1, CONFIG_REGULATOR_LOG_LEVEL);

static int tps55287q1_reg_read(const struct tps55287q1_config *cfg, uint8_t reg, uint8_t *val) {
	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static int tps55287q1_reg_write(const struct tps55287q1_config *cfg,uint8_t reg, uint8_t val) {
	return i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
}

static int tps55287q1_reg_update(const struct tps55287q1_config *cfg, uint8_t reg, uint8_t mask, uint8_t val) {
	int ret;
	uint8_t tmp;

	ret = tps55287q1_reg_read(cfg, reg, &tmp);
	if (ret < 0) {
		return ret;
	}

	tmp = (tmp & ~mask) | (val & mask);

	return tps55287q1_reg_write(cfg, reg, tmp);
}

static int tps55287q1_get_range(const struct tps55287q1_config *cfg, int32_t *vmin_uv, int32_t *vmax_uv, int32_t *step_uv, uint16_t *max_code) {
	const int32_t base_uv = 800000; /* 0.8 V */

	switch (cfg->intfb & 0x3) {
	case 0:
		/* 0.8 V .. 5 V, 2.5 mV step */
		*vmin_uv = base_uv;
		*vmax_uv = 5000000;
		*step_uv = 2500;
		break;
	case 1:
		/* 0.8 V .. 10 V, 5 mV step */
		*vmin_uv = base_uv;
		*vmax_uv = 10000000;
		*step_uv = 5000;
		break;
	case 2:
		/* 0.8 V .. 15 V, 7.5 mV step */
		*vmin_uv = base_uv;
		*vmax_uv = 15000000;
		*step_uv = 7500;
		break;
	case 3:
	default:
		/* 0.8 V .. 20 V, 10 mV step (default in datasheet) */
		*vmin_uv = base_uv;
		*vmax_uv = 20000000;
		*step_uv = 10000;
		break;
	}

	/* max_code such that vmin + max_code * step <= vmax */
	*max_code = (uint16_t)((*vmax_uv - *vmin_uv) / *step_uv);

	return 0;
}

static int tps55287q1_write_ref_code(const struct device *dev, uint16_t code) {
	const struct tps55287q1_config *cfg = dev->config;
	struct tps55287q1_data *data = dev->data;
	int ret;

	code &= 0x07FFu; /* 11 bits */

	uint8_t lsb = code & 0xFFu;
	uint8_t msb = (code >> 8) & 0x07u;

	ret = tps55287q1_reg_write(cfg, TPS55287Q1_REG_VREF_LSB, lsb);
	if (ret < 0) {
		return ret;
	}

	ret = tps55287q1_reg_write(cfg, TPS55287Q1_REG_VREF_MSB, msb);
	if (ret < 0) {
		return ret;
	}

	data->vref_code_cached = code;

	return 0;
}

static int tps55287q1_read_ref_code(const struct device *dev, uint16_t *code) {
	const struct tps55287q1_config *cfg = dev->config;
	struct tps55287q1_data *data = dev->data;
	int ret;
	uint8_t lsb, msb;

	ret = tps55287q1_reg_read(cfg, TPS55287Q1_REG_VREF_LSB, &lsb);
	if (ret < 0) {
		return ret;
	}

	ret = tps55287q1_reg_read(cfg, TPS55287Q1_REG_VREF_MSB, &msb);
	if (ret < 0) {
		return ret;
	}

	data->vref_code_cached = ((uint16_t)(msb & 0x07u) << 8) | lsb;
	*code = data->vref_code_cached;

	return 0;
}

static int tps55287q1_write_mode(const struct device *dev, uint8_t mode) {
	const struct tps55287q1_config *cfg = dev->config;
	struct tps55287q1_data *data = dev->data;
	int ret;

	ret = tps55287q1_reg_write(cfg, TPS55287Q1_REG_MODE, mode);
	if (ret < 0) {
		return ret;
	}

	data->mode_cached = mode;

	return 0;
}

static int tps55287q1_read_mode(const struct device *dev, uint8_t *mode) {
	const struct tps55287q1_config *cfg = dev->config;
	struct tps55287q1_data *data = dev->data;
	int ret;
	uint8_t tmp;

	ret = tps55287q1_reg_read(cfg, TPS55287Q1_REG_MODE, &tmp);
	if (ret < 0) {
		return ret;
	}

	data->mode_cached = tmp;
	*mode = tmp;

	return 0;
}

/* Regulator API callbacks */
static int tps55287q1_enable(const struct device *dev) {
	const struct tps55287q1_config *cfg = dev->config;
	uint8_t mode;
	int ret;

	if (cfg->has_enable_gpio) {
		ret = gpio_pin_set_dt(&cfg->enable_gpio, 1);
		if (ret < 0) {
			return ret;
		}
	}

	ret = tps55287q1_read_mode(dev, &mode);
	if (ret < 0) {
		return ret;
	}

	mode |= TPS55287Q1_MODE_OE_BIT;

	return tps55287q1_write_mode(dev, mode);
}

static int tps55287q1_disable(const struct device *dev) {
	const struct tps55287q1_config *cfg = dev->config;
	uint8_t mode;
	int ret;

	ret = tps55287q1_read_mode(dev, &mode);
	if (ret < 0) {
		return ret;
	}

	mode &= ~TPS55287Q1_MODE_OE_BIT;

	ret = tps55287q1_write_mode(dev, mode);
	if (ret < 0) {
		return ret;
	}

	if (cfg->has_enable_gpio) {
		(void)gpio_pin_set_dt(&cfg->enable_gpio, 0);
	}

	return 0;
}

static unsigned int tps55287q1_count_voltages(const struct device *dev) {
	const struct tps55287q1_config *cfg = dev->config;
	int32_t vmin_uv, vmax_uv, step_uv;
	uint16_t max_code;

	(void)tps55287q1_get_range(cfg, &vmin_uv, &vmax_uv, &step_uv, &max_code);

	return (unsigned int)max_code + 1U;
}

static int tps55287q1_list_voltage(const struct device *dev, unsigned int idx, int32_t *volt_uv) {
	const struct tps55287q1_config *cfg = dev->config;
	int32_t vmin_uv, vmax_uv, step_uv;
	uint16_t max_code;

	(void)tps55287q1_get_range(cfg, &vmin_uv, &vmax_uv, &step_uv, &max_code);

	if (idx > max_code) {
		return -EINVAL;
	}

	*volt_uv = vmin_uv + (int32_t)idx * step_uv;

	return 0;
}

static int tps55287q1_set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv) {
	const struct tps55287q1_config *cfg = dev->config;
	int32_t vmin_uv, vmax_uv, step_uv;
	uint16_t max_code;
	int32_t target_uv;
	uint16_t code;

	tps55287q1_get_range(cfg, &vmin_uv, &vmax_uv, &step_uv, &max_code);

	if (max_uv < vmin_uv || min_uv > vmax_uv) {
		return -EINVAL;
	}

	if (min_uv < vmin_uv) {
		min_uv = vmin_uv;
	}
	if (max_uv > vmax_uv) {
		max_uv = vmax_uv;
	}

	target_uv = min_uv;

	int64_t tmp = (int64_t)(target_uv - vmin_uv) + (step_uv / 2);
	if (tmp < 0) {
		tmp = 0;
	}
	code = (uint16_t)(tmp / step_uv);
	if (code > max_code) {
		code = max_code;
	}

	return tps55287q1_write_ref_code(dev, code);
}

static int tps55287q1_get_voltage(const struct device *dev, int32_t *volt_uv) {
	const struct tps55287q1_config *cfg = dev->config;
	int32_t vmin_uv, vmax_uv, step_uv;
	uint16_t max_code, code;
	int ret;

	tps55287q1_get_range(cfg, &vmin_uv, &vmax_uv, &step_uv, &max_code);

	ret = tps55287q1_read_ref_code(dev, &code);
	if (ret < 0) {
		return ret;
	}

	if (code > max_code) {
		code = max_code;
	}

	*volt_uv = vmin_uv + (int32_t)code * step_uv;

	return 0;
}

static int tps55287q1_set_active_discharge(const struct device *dev, bool active_discharge) {
	const struct tps55287q1_config *cfg = dev->config;
	uint8_t val = active_discharge ? TPS55287Q1_MODE_DISCHG_BIT : 0U;

	return tps55287q1_reg_update(cfg, TPS55287Q1_REG_MODE, TPS55287Q1_MODE_DISCHG_BIT, val);
}

static int tps55287q1_get_active_discharge(const struct device *dev, bool *active_discharge) {
	const struct tps55287q1_config *cfg = dev->config;
	uint8_t mode;
	int ret;

	ret = tps55287q1_reg_read(cfg, TPS55287Q1_REG_MODE, &mode);
	if (ret < 0) {
		return ret;
	}

	*active_discharge = (mode & TPS55287Q1_MODE_DISCHG_BIT) != 0U;

	return 0;
}

static const struct regulator_driver_api tps55287q1_regulator_api = {
	.enable = tps55287q1_enable,
	.disable = tps55287q1_disable,
	.count_voltages = tps55287q1_count_voltages,
	.list_voltage = tps55287q1_list_voltage,
	.set_voltage = tps55287q1_set_voltage,
	.get_voltage = tps55287q1_get_voltage,
	.set_active_discharge = tps55287q1_set_active_discharge,
	.get_active_discharge = tps55287q1_get_active_discharge,
};

static int tps55287q1_init(const struct device *dev) {
	const struct tps55287q1_config *cfg = dev->config;
	struct tps55287q1_data *data = dev->data;
	int ret;

	regulator_common_data_init(dev);

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	if (cfg->has_enable_gpio) {
		if (!device_is_ready(cfg->enable_gpio.port)) {
			LOG_ERR("Enable GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to config enable GPIO: %d", ret);
			return ret;
		}
	}

	uint8_t vout_fs;
	ret = tps55287q1_reg_read(cfg, TPS55287Q1_REG_VOUT_FS, &vout_fs);
	if (ret < 0) {
		return ret;
	}

	vout_fs &= ~(TPS55287Q1_VOUT_FS_FB_BIT | TPS55287Q1_VOUT_FS_INTFB_MASK);
	vout_fs |= (cfg->intfb & 0x3u);

	ret = tps55287q1_reg_write(cfg, TPS55287Q1_REG_VOUT_FS, vout_fs);
	if (ret < 0) {
		return ret;
	}

#if DT_INST_NODE_HAS_PROP(0, regulator_active_discharge)
	bool active_discharge = true;
	(void)tps55287q1_set_active_discharge(dev, active_discharge);
#endif

	data->vref_code_cached = 0;
	data->mode_cached = 0;

	bool init_enabled = regulator_common_is_init_enabled(dev);

	ret = regulator_common_init(dev, init_enabled);
	if (ret < 0) {
		LOG_ERR("regulator_common_init failed: %d", ret);
		return ret;
	}

	return 0;
}

#define TPS55287Q1_CONFIG(inst)                                                 \
	static const struct tps55287q1_config tps55287q1_config_##inst = {          \
		.common = REGULATOR_DT_INST_COMMON_CONFIG_INIT(inst),                   \
		.i2c = I2C_DT_SPEC_GET(DT_DRV_INST(inst)),                              \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}),       \
		.has_enable_gpio = DT_INST_NODE_HAS_PROP(inst, enable_gpios),           \
		.intfb = DT_INST_PROP_OR(inst, ti_intfb, 3),                            \
	}

#define TPS55287Q1_DATA(inst)                                                   \
	static struct tps55287q1_data tps55287q1_data_##inst                        \

#define TPS55287Q1_INIT(inst)                                                   \
	TPS55287Q1_DATA(inst);                                                      \
	TPS55287Q1_CONFIG(inst);                                                    \
	DEVICE_DT_INST_DEFINE(inst,                                                 \
			      tps55287q1_init, NULL,                                        \
			      &tps55287q1_data_##inst,                                      \
			      &tps55287q1_config_##inst,                                    \
			      POST_KERNEL,                                                  \
                  CONFIG_REGULATOR_TPS55287Q1_INIT_PRIORITY,                    \
			      &tps55287q1_regulator_api);

DT_INST_FOREACH_STATUS_OKAY(TPS55287Q1_INIT);
