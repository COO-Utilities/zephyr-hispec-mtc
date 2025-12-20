#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtd_dev, LOG_LEVEL_INF);

#define R_REF               5110.0f      /* Reference Resistance */
#define RTD_TC              3850.0f    /* Temperature Coefficient */
#define RTD_R0              1000.0f      /* Nominal Resistance at 0°C */

#define RTD_NODE DT_ALIAS(rtd_test)

static const struct adc_dt_spec rtd_channel = ADC_DT_SPEC_GET(RTD_NODE);

int main(void)
{
    int err;
    int32_t buf;

    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    err = adc_is_ready_dt(&rtd_channel);
    if (err < 0) {
        LOG_ERR("ADC device not ready (%d)", err);
        return -1;
    }

    /* Setup Channel 0 (RTD) */
    err = adc_channel_setup_dt(&rtd_channel);
    if (err < 0) {
        LOG_ERR("RTD Channel Setup failed (%d)", err);
        return -1;
    }

    LOG_INF("Starting RTD Measurement...");

    while (1) {
        adc_sequence_init_dt(&rtd_channel, &sequence);
        err = adc_read(rtd_channel.dev, &sequence);
        if (err < 0) {
            LOG_ERR("ADC read failed (%d)", err);
        } else {
            int32_t raw_value = buf;
            int32_t max_count = (1 << (rtd_channel.resolution - 1)) - 1; 
            float gain = 4.0f; 

            float r_rtd = (((float)raw_value - (float)max_count) * R_REF) / (gain * (float)max_count);
            float temp_c = (r_rtd - RTD_R0) / (RTD_TC / RTD_R0);

            LOG_INF("Raw: %d | Res: %.2f Ohms | Temp: %.3f C", raw_value, (double)r_rtd, (double)temp_c);
        }

        printk("------------------------------------------------------------------------------------\n");
        k_sleep(K_MSEC(1000));
    }

    return 0;
}