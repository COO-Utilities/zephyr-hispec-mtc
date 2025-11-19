/**
 * @file adc_temp_sensor.c
 * @brief AD7124 ADC temperature sensor driver implementation
 *
 * Currently configured to read the AD7124's internal chip temperature sensor
 * for testing/development purposes.
 *
 * In production, this will be reconfigured to read external Penguin RTD sensors
 * connected to the AD7124's analog input channels.
 */

#include "adc_temp_sensor.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(adc_temp_sensor, LOG_LEVEL_INF);

/* DT node */
#define AD7124_NODE DT_NODELABEL(ad7124)

/* SPI spec from DT (8-bit, MSB, MODE3, driver-managed CS) */
static const struct spi_dt_spec bus =
    SPI_DT_SPEC_GET(AD7124_NODE,
                    SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA, 0);

/* AD7124 register addresses */
#define REG_COMMS        0x00
#define REG_ADC_CONTROL  0x01
#define REG_DATA         0x02
#define REG_IO_CONTROL_2 0x04
#define REG_ID           0x05
#define REG_CHANNEL_0    0x09
#define REG_CONFIG_0     0x19
#define REG_FILTER0      0x21

/* Module state */
static bool initialized = false;

/* ========== Low-level SPI helpers ========== */

/* ADI "no-shift" read command (R/W=1 at bit6, 6-bit addr) */
static inline uint8_t cmd_read(uint8_t addr)
{
    return (uint8_t)(0x40u | (addr & 0x3Fu));
}

/**
 * Generic SPI read
 */
static bool ad7124_read(uint8_t reg, uint8_t *dst, size_t n)
{
    uint8_t tx[4] = { cmd_read(reg), 0xFF, 0xFF, 0xFF };
    uint8_t rx[4] = { 0 };
    struct spi_buf txb = { .buf = tx, .len = (uint32_t)(1 + n) };
    struct spi_buf rxb = { .buf = rx, .len = (uint32_t)(1 + n) };
    struct spi_buf_set TX = { .buffers = &txb, .count = 1 };
    struct spi_buf_set RX = { .buffers = &rxb, .count = 1 };

    if (spi_transceive_dt(&bus, &TX, &RX) != 0) {
        return false;
    }

    memcpy(dst, &rx[1], n);
    return true;
}

static bool ad7124_read8(uint8_t reg, uint8_t *v)
{
    return ad7124_read(reg, v, 1);
}

static bool ad7124_read16(uint8_t reg, uint16_t *v)
{
    uint8_t b[2] = {0, 0};
    if (!ad7124_read(reg, b, 2)) {
        return false;
    }
    *v = ((uint16_t)b[0] << 8) | b[1];  /* MSB first */
    return true;
}

static bool ad7124_read24(uint8_t reg, uint32_t *v)
{
    uint8_t b[3] = {0, 0, 0};
    if (!ad7124_read(reg, b, 3)) {
        return false;
    }
    *v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return true;
}

/**
 * Generic SPI write
 */
static bool ad7124_write(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t hdr = (uint8_t)(0x00u | (reg & 0x7Fu)); /* write, no-shift */
    struct spi_buf seg[2] = {
        { .buf = &hdr,         .len = 1 },
        { .buf = (void*)data,  .len = (uint32_t)n }
    };
    struct spi_buf_set TX = { .buffers = seg, .count = 2 };
    return spi_write_dt(&bus, &TX) == 0;
}

static bool ad7124_write16(uint8_t reg, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return ad7124_write(reg, b, 2);
}

static bool ad7124_write24(uint8_t reg, uint32_t v)
{
    uint8_t b[3] = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    return ad7124_write(reg, b, 3);
}

/**
 * Soft reset: send 64 ones
 */
static void ad7124_soft_reset(void)
{
    uint8_t ff[8];
    memset(ff, 0xFF, sizeof(ff));
    struct spi_buf txb = { .buf = ff, .len = sizeof(ff) };
    struct spi_buf_set TX = { .buffers = &txb, .count = 1 };
    (void)spi_write_dt(&bus, &TX);
}

/**
 * Poll RDY bit in STATUS register (bit7 == 0 => ready)
 */
static bool ad7124_wait_ready_ms(int timeout_ms)
{
    uint8_t st = 0xFF;
    while (timeout_ms-- > 0) {
        if (ad7124_read8(REG_COMMS, &st) && ((st & 0x80u) == 0)) {
            return true;
        }
        k_msleep(1);
    }
    return false;
}

/* ========== Temperature sensor configuration ========== */

/**
 * Configure AD7124 for on-chip temperature sensor reading
 * Channel 0: AINP=16 (Temp sensor), AINM=17 (AVSS)
 * CONFIG_0: bipolar, input & reference buffers ON, REF_SEL=internal 2.5V, PGA=1
 * FILTER0: SINC3 setting
 */
static bool ad7124_config_temp_sensor(void)
{
    /* Reset IO control */
    if (!ad7124_write16(REG_IO_CONTROL_2, 0x0000)) {
        return false;
    }

    /* ADC_CONTROL: REF_EN=1 (bit8), POWER_MODE=full (7:6=0), MODE=continuous (5:2=0) */
    const uint16_t ADC_CTRL = (1u << 8) | (0u << 6) | (0u << 2);
    if (!ad7124_write16(REG_ADC_CONTROL, ADC_CTRL)) {
        return false;
    }

    /* CONFIG_0 fields (datasheet-defined positions) */
    const uint16_t CONFIG0 =
        (1u << 11) |               /* bipolar = 1 */
        (1u << 8)  | (1u << 7) |   /* REF buffer pos/neg */
        (1u << 6)  | (1u << 5) |   /* AIN buffer pos/neg */
        (0b10u << 3) |             /* REF_SEL = 0b10 (internal 2.5V) */
        (0u);                      /* PGA = 1 (gain=1) */
    if (!ad7124_write16(REG_CONFIG_0, CONFIG0)) {
        return false;
    }

    /* FILTER0 (setup0): SINC3 with a reasonable FS */
    (void)ad7124_write24(REG_FILTER0, 0x060180);

    /* CHANNEL_0: enable, setup0, AINP=16 (Temp), AINM=17 (AVSS) */
    const uint16_t CH0 = (1u << 15) | (0u << 12) | (16u << 5) | 17u;
    if (!ad7124_write16(REG_CHANNEL_0, CH0)) {
        return false;
    }

    /* Let the digital filter settle */
    k_msleep(5);
    return true;
}

/* ========== Temperature conversion ========== */

/**
 * Convert 24-bit ADC code to Celsius
 * Raw offset-binary: subtract mid-scale
 */
static inline float ad7124_code_to_celsius(uint32_t code24)
{
    uint32_t code = (code24 & 0xFFFFFFu);
    int32_t delta = (int32_t)code - 8388608;   /* 0x800000 */
    return ((float)delta / 13584.0f) - 272.5f;
}

/**
 * Convert Celsius to Kelvin
 */
static inline float celsius_to_kelvin(float celsius)
{
    return celsius + 273.15f;
}

/* ========== Public API ========== */

int adc_temp_sensor_init(const thermal_config_t *config)
{
    if (initialized) {
        LOG_WRN("ADC temp sensor already initialized");
        return 0;
    }

    LOG_INF("Initializing AD7124 temperature sensor");

    /* Check SPI bus */
    if (!device_is_ready(bus.bus)) {
        LOG_ERR("SPI bus not ready");
        return -1;
    }

    /* Check CS GPIO */
    if (bus.config.cs.gpio.port && !device_is_ready(bus.config.cs.gpio.port)) {
        LOG_ERR("CS GPIO not ready");
        return -2;
    }

    /* Reset and configure */
    ad7124_soft_reset();
    k_msleep(3);

    if (!ad7124_config_temp_sensor()) {
        LOG_ERR("Temperature sensor configuration failed");
        return -3;
    }

    /* Verify configuration by reading back registers */
    uint16_t cfg = 0, ch0 = 0, adc = 0;
    (void)ad7124_read16(REG_CONFIG_0, &cfg);
    (void)ad7124_read16(REG_CHANNEL_0, &ch0);
    (void)ad7124_read16(REG_ADC_CONTROL, &adc);
    LOG_INF("AD7124 configured: CFG0=0x%04x CH0=0x%04x ADC_CTRL=0x%04x",
            (unsigned)cfg, (unsigned)ch0, (unsigned)adc);

    initialized = true;
    LOG_INF("AD7124 temperature sensor initialized successfully");
    return 0;
}

int adc_temp_sensor_read(const char *sensor_id, float *temp_kelvin)
{
    if (!initialized) {
        LOG_ERR("ADC temp sensor not initialized");
        return -1;
    }

    if (temp_kelvin == NULL) {
        return -2;
    }

    /* Wait for ADC ready */
    if (!ad7124_wait_ready_ms(500)) {
        LOG_WRN("ADC not ready (sensor: %s)", sensor_id);
        return -3;
    }

    /* Read 24-bit data register */
    uint32_t raw = 0;
    if (!ad7124_read24(REG_DATA, &raw)) {
        LOG_ERR("Failed to read ADC data (sensor: %s)", sensor_id);
        return -4;
    }

    /* Convert to temperature */
    float temp_c = ad7124_code_to_celsius(raw);
    *temp_kelvin = celsius_to_kelvin(temp_c);

    LOG_INF("Sensor %s: Raw=0x%06x => %.2f C (%.2f K)",
            sensor_id, (unsigned)raw, temp_c, *temp_kelvin);

    return 0;
}

int adc_temp_sensor_configure_channel(const char *sensor_id,
                                      const adc_channel_config_t *channel_config)
{
    if (!initialized) {
        LOG_ERR("ADC temp sensor not initialized");
        return -1;
    }

    if (channel_config == NULL) {
        return -2;
    }

    /* TODO: Implement custom channel configuration for external sensors */
    /* This would allow configuring different AINP/AINM pairs for RTDs, thermocouples, etc. */

    LOG_WRN("Custom channel configuration not yet implemented");
    return -ENOSYS;
}

bool adc_temp_sensor_is_ready(const char *sensor_id)
{
    if (!initialized) {
        return false;
    }

    uint8_t st = 0xFF;
    if (ad7124_read8(REG_COMMS, &st) && ((st & 0x80u) == 0)) {
        return true;
    }

    return false;
}
