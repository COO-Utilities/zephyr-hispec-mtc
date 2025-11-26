#ifndef ZEPHYR_DRIVERS_HEATER_TI_TPS55287Q1_H_
#define ZEPHYR_DRIVERS_HEATER_TI_TPS55287Q1_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

// TPS55287-Q1 Register Definitions
#define TPS55287Q1_VREF_LSB                 0x00
#define TPS55287Q1_VREF_MSB                 0x01
#define TPS55287Q1_IOUT_LIMIT               0x02
#define TPS55287Q1_VOUT_SR                  0x03
#define TPS55287Q1_VOUT_FS                  0x04
#define TPS55287Q1_CDC                      0x05
#define TPS55287Q1_MODE                     0x06
#define TPS55287Q1_STATUS                   0x07

// MODE Register Bit Definitions
#define TPS55287Q1_MODE_OE                  BIT(7)
#define TPS55287Q1_MODE_FSWDBL              BIT(6)
#define TPS55287Q1_MODE_HICCUP              BIT(5)
#define TPS55287Q1_MODE_DISCHG              BIT(4)
#define TPS55287Q1_MODE_FORCE_DISCHG        BIT(3)
#define TPS55287Q1_MODE_FPWM                BIT(1)

// STATUS Register Bit Definitions
#define TPS55287Q1_STATUS_SCP               BIT(7)
#define TPS55287Q1_STATUS_OCP               BIT(6)
#define TPS55287Q1_STATUS_OVP               BIT(5)
#define TPS55287Q1_STATUS_MODE_MASK         GENMASK(1, 0)
#define TPS55287Q1_STATUS_MODE_BOOST        0x00
#define TPS55287Q1_STATUS_MODE_BUCK         0x01
#define TPS55287Q1_STATUS_MODE_BUCKBOOST    0x02

int tps55287q1_set_vref_uv(const struct device *dev, uint32_t vref_mv);
int tps55287q1_config_feedback(const struct device *dev, bool use_ext_fb, uint8_t intfb_code);
int tps55287q1_set_vout_mv(const struct device *dev, uint32_t vout_uv, uint8_t intfb_code);
int tps55287q1_set_current_limit(const struct device *dev, uint32_t limit_ma, uint32_t rsense_milliohm, bool enable);
int tps55287q1_enable_output(const struct device *dev, bool enable);
int tps55287q1_get_status(const struct device *dev, uint8_t *status);
int tps55287q1_check_faults(const struct device *dev);

struct tps55287q1_config {
    struct i2c_dt_spec i2c;
    bool enable_at_boot;
    uint8_t intfb;
    uint32_t default_vout_mv;
    uint32_t default_current_limit_ma;
    uint32_t rsense_milliohm;
};

struct tps55287q1_data {
    const struct device *i2c_dev;
    bool enabled;
    uint32_t cached_vref_uv;
    uint32_t cached_vout_uv;
    uint32_t cached_curr_limit_ua;
};

#ifdef __cplusplus
}
#endif

#endif  // ZEPHYR_DRIVERS_HEATER_TI_TPS55287Q1_H_