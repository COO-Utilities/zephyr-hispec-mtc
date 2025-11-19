#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(main_app, LOG_LEVEL_INF);

/* DT nodes */
#define AD7124_NODE DT_NODELABEL(ad7124)

/* SPI spec from DT (8-bit, MSB, MODE3, driver-managed CS) */
static const struct spi_dt_spec bus =
    SPI_DT_SPEC_GET(AD7124_NODE,
                    SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA, 0);

/* AD7124 registers used */
#define REG_COMMS        0x00
#define REG_ADC_CONTROL  0x01
#define REG_DATA         0x02
#define REG_IO_CONTROL_2 0x04
#define REG_ID           0x05
#define REG_CHANNEL_0    0x09
#define REG_CONFIG_0     0x19
#define REG_FILTER0      0x21

/* ADI "no-shift" read command (R/W=1 at bit6, 6-bit addr) */
static inline uint8_t cmd_read(uint8_t addr)
{
    return (uint8_t)(0x40u | (addr & 0x3Fu));
}

/* ---- SPI helpers ---- */
static bool read(uint8_t reg, uint8_t *dst, size_t n) {
    uint8_t tx[4] = { cmd_read(reg), 0xFF, 0xFF, 0xFF };
    uint8_t rx[4] = { 0 };
    struct spi_buf txb = { .buf = tx, .len = (uint32_t)(1 + n) };
    struct spi_buf rxb = { .buf = rx, .len = (uint32_t)(1 + n) };
    struct spi_buf_set TX = { .buffers = &txb, .count = 1 };
    struct spi_buf_set RX = { .buffers = &rxb, .count = 1 };
    if (spi_transceive_dt(&bus, &TX, &RX) != 0) return false;
    memcpy(dst, &rx[1], n);
    return true;
}

static bool read8(uint8_t reg, uint8_t *v)
{
    return read(reg, v, 1);
}

static bool read16(uint8_t reg, uint16_t *v) {
    uint8_t b[2] = {0,0};
    if (!read(reg, b, 2)) return false;
    *v = ((uint16_t)b[0] << 8) | b[1];  /* device sends MSB first */
    return true;
}

static bool read24(uint8_t reg, uint32_t *v) {
    uint8_t b[3] = {0,0,0};
    if (!read(reg, b, 3)) return false;
    *v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return true;
}

static bool write(uint8_t reg, const uint8_t* data, size_t n) {
    uint8_t hdr = (uint8_t)(0x00u | (reg & 0x7Fu)); /* write, no-shift */
    struct spi_buf seg[2] = {
        { .buf = &hdr,         .len = 1 },
        { .buf = (void*)data,  .len = (uint32_t)n }
    };
    struct spi_buf_set TX = { .buffers = seg, .count = 2 };
    return spi_write_dt(&bus, &TX) == 0;
}

static bool write16(uint8_t reg, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return write(reg, b, 2);
}

static bool write24(uint8_t reg, uint32_t v) {
    uint8_t b[3] = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    return write(reg, b, 3);
}

/* soft reset = 64 ones */
static void soft_reset(void) {
    uint8_t ff[8];
    memset(ff, 0xFF, sizeof ff);
    struct spi_buf txb = { .buf = ff, .len = sizeof ff };
    struct spi_buf_set TX = { .buffers = &txb, .count = 1 };
    (void)spi_write_dt(&bus, &TX);
}

/* poll RDY bit in STATUS (bit7 == 0 => ready) */
static bool wait_ready_ms(int timeout_ms) {
    uint8_t st = 0xFF;
    while (timeout_ms-- > 0) {
        if (read8(REG_COMMS, &st) && ((st & 0x80u) == 0)) return true;
        k_msleep(1);
    }
    return false;
}

/* ---- configuration for on-chip temperature sensor ---- */
/* Channel 0: AINP=16 (Temp sensor), AINM=17 (AVSS)
 * CONFIG_0:
 *   bipolar=1, input & reference buffers ON, REF_SEL=internal 2.5V, PGA=1
 * FILTER0: SINC3 setting (works, can tweak later for speed/noise)
 */
static bool config_temp(void) {
    if (!write16(REG_IO_CONTROL_2, 0x0000)) return false;  /* reset */

    /* ADC_CONTROL: REF_EN=1 (bit8), POWER_MODE=full (7:6=0), MODE=continuous (5:2=0) */
    const uint16_t ADC_CTRL = (1u<<8) | (0u<<6) | (0u<<2);
    if (!write16(REG_ADC_CONTROL, ADC_CTRL)) return false;

    /* CONFIG_0 fields (datasheet-defined positions) */
    const uint16_t CONFIG0 =
        (1u<<11) |               /* bipolar = 1 */
        (1u<<8)  | (1u<<7) |     /* REF buffer pos/neg */
        (1u<<6)  | (1u<<5) |     /* AIN buffer pos/neg */
        (0b10u<<3) |             /* REF_SEL = 0b10 (internal 2.5V) */
        (0u);                    /* PGA = 1 (gain=1) */
    if (!write16(REG_CONFIG_0, CONFIG0)) return false;

    /* FILTER0 (setup0): SINC3 with a reasonable FS */
    (void)write24(REG_FILTER0, 0x060180);

    /* CHANNEL_0: enable, setup0, AINP=16 (Temp), AINM=17 (AVSS) */
    const uint16_t CH0 = (1u<<15) | (0u<<12) | (16u<<5) | 17u;
    if (!write16(REG_CHANNEL_0, CH0)) return false;

    /* let the digital filter settle one frame */
    k_msleep(5);
    return true;
}


/* raw offset-binary and subtract mid-scale */
static inline double code_to_celsius(uint32_t code24) {
    uint32_t code = (code24 & 0xFFFFFFu);
    int32_t delta = (int32_t)code - 8388608;   /* 0x800000 */
    return ((double)delta / 13584.0) - 272.5;
}

int main(void) {
    LOG_INF("AD7124 Temperature Sensor Test");

    /* Check SPI bus for AD7124 */
    if (!device_is_ready(bus.bus)) {
        LOG_ERR("SPI bus not ready");
        return 0;
    }
    if (bus.config.cs.gpio.port && !device_is_ready(bus.config.cs.gpio.port)) {
        LOG_ERR("CS GPIO not ready");
        return 0;
    }

    soft_reset();
    k_msleep(3);
    if (!config_temp()) {
        LOG_ERR("Temp-path config failed");
        return 0;
    }

    /* read back key regs to verify what latched */
    uint16_t cfg = 0, ch0 = 0, adc = 0;
    (void)read16(REG_CONFIG_0, &cfg);
    (void)read16(REG_CHANNEL_0, &ch0);
    (void)read16(REG_ADC_CONTROL, &adc);
    LOG_INF("CFG0=0x%04x CH0=0x%04x ADC_CTRL=0x%04x",
            (unsigned)cfg, (unsigned)ch0, (unsigned)adc);

    LOG_INF("--- Starting temperature readings ---");

    while (1) {
        if (!wait_ready_ms(500)) {
            LOG_WRN("ADC not ready");
            k_msleep(100);
            continue;
        }

        uint32_t raw = 0;
        if (!read24(REG_DATA, &raw)) {
            LOG_ERR("Failed to read data");
            k_msleep(100);
            continue;
        }

        double temp_c = code_to_celsius(raw);
        LOG_INF("Raw=0x%06x => %.2f C", (unsigned)raw, temp_c);

        k_msleep(1000);
    }
}
