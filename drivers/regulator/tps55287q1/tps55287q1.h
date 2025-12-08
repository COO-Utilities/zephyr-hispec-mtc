#ifndef ZEPHYR_DRIVERS_REGULATOR_TPS55287Q1_H_
#define ZEPHYR_DRIVERS_REGULATOR_TPS55287Q1_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>

/* TPS55287-Q1 register maps */
#define TPS55287Q1_REG_VREF_LSB             0x00
#define TPS55287Q1_REG_VREF_MSB             0x01
#define TPS55287Q1_REG_IOUT_LIMIT           0x02
#define TPS55287Q1_REG_VOUT_SR              0x03
#define TPS55287Q1_REG_VOUT_FS              0x04
#define TPS55287Q1_REG_CDC                  0x05
#define TPS55287Q1_REG_MODE                 0x06
#define TPS55287Q1_REG_STATUS               0x07

/* VOUT_FS (04h) register bits */
#define TPS55287Q1_VOUT_FS_FB_BIT           BIT(7)
#define TPS55287Q1_VOUT_FS_INTFB_MASK       GENMASK(1, 0)

/*
 * Simple linear mapping of VOUT vs REF DAC for internal feedback
 * as given in Table 7-6:
 *
 *   INTFB = 0: 0.8 V .. 5 V   ,  2.5 mV step
 *   INTFB = 1: 0.8 V .. 10 V  ,  5.0 mV step
 *   INTFB = 2: 0.8 V .. 15 V  ,  7.5 mV step
 *   INTFB = 3: 0.8 V .. 20 V  , 10.0 mV step
 *
 * We model this as: VOUT = 0.8 V + code * step, code in [0, max_code].
 */

/* MODE (06h) register bits */
#define TPS55287Q1_MODE_OE_BIT              BIT(7)
#define TPS55287Q1_MODE_DISCHG_BIT          BIT(4)
#define TPS55287Q1_MODE_FORCE_DISCHG_BIT    BIT(3)
#define TPS55287Q1_MODE_FPWM_BIT            BIT(1)

struct tps55287q1_config {
	struct regulator_common_config common;
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec enable_gpio;
	bool has_enable_gpio;
	uint8_t intfb;
};

struct tps55287q1_data {
	struct regulator_common_data common;
	uint16_t vref_code_cached;
	uint8_t mode_cached;
};

#endif /* ZEPHYR_DRIVERS_REGULATOR_TPS55287Q1_H_ */
