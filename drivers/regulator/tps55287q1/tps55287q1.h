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

/* IOUT_LIMIT (02h) register bits */
#define TPS55287Q1_IOUT_LIMIT_CURRENT_LIMIT_EN     		BIT(7)
#define TPS55287Q1_IOUT_LIMIT_CURRENT_LIMIT_SETTINGS	GENMASK(6, 0)

/* VOUT_SR (03h) register bits */
#define TPS55287Q1_VOUT_SR_OCP_DELAY		GENMASK(5, 4)
#define TPS55287Q1_VOUT_SR_SR				GENMASK(1, 0)

/* VOUT_FS (04h) register bits */
#define TPS55287Q1_VOUT_FS_FB           	BIT(7)
#define TPS55287Q1_VOUT_FS_INTFB			GENMASK(1, 0)

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

/* CDC (05h) register bits */
#define TPS55287Q1_CDC_SC_MASK		    	BIT(7)
#define TPS55287Q1_CDC_OCP_MASK		    	BIT(6)
#define TPS55287Q1_CDC_OVP_MASK		    	BIT(5)
#define TPS55287Q1_CDC_CDC_OPTION		    BIT(3)
#define TPS55287Q1_CDC_CDC		    		GENMASK(2, 0)

/* MODE (06h) register bits */
#define TPS55287Q1_MODE_OE					BIT(7)
#define TPS55287Q1_MODE_FSWDBL        		BIT(6)
#define TPS55287Q1_MODE_HICCUP        		BIT(5)
#define TPS55287Q1_MODE_DISCHG				BIT(4)
#define TPS55287Q1_MODE_FORCE_DISCHG		BIT(3)
#define TPS55287Q1_MODE_FPWM				BIT(1)

/* STATUS (07h) register bits */
#define TPS55287Q1_STATUS_SCP		    	BIT(7)
#define TPS55287Q1_STATUS_OCP		    	BIT(6)
#define TPS55287Q1_STATUS_OVP		    	BIT(5)
#define TPS55287Q1_STATUS_STATUS		    GENMASK(1, 0)

struct tps55287q1_config {
	struct regulator_common_config common;
	struct i2c_dt_spec i2c;
	uint8_t intfb;
};

struct tps55287q1_data {
	struct regulator_common_data common;
	uint16_t vref_code_cached;
	uint8_t mode_cached;
};

#endif /* ZEPHYR_DRIVERS_REGULATOR_TPS55287Q1_H_ */
