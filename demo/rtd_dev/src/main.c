#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ad7124_app, LOG_LEVEL_INF);


int main(void)
{
    const struct device *adc = DEVICE_DT_GET(DT_ALIAS(ad7124_test));
    if (!device_is_ready(adc)) {
        LOG_ERR("ADC device not found or not ready");
        return 0;
    }

    LOG_INF("ADC device is ready");

    struct adc_channel_cfg chnnel_0_cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(ad7124_test), channel_0));
    if (adc_channel_setup(adc, &chnnel_0_cfg) != 0) {
        LOG_ERR("Failed to configure ADC channel 0");
        return 0;
    }

    int32_t sample_buffer[1];
    
    struct adc_sequence sequence = {
        .channels = BIT(0),
        .buffer = sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution = 24,
    };

    LOG_INF("Starting AD7124 reading loop...");

    while (1) {
        int ret = adc_read(adc, &sequence);
        if (ret == 0) {
            int32_t raw_code = sample_buffer[0];
            
            double temp_c = ((double)(raw_code - 0x800000) / 13584.0) - 272.5;

            printf("Raw: 0x%06X | Temp: %.2f C\n", raw_code, temp_c);
            
        } else {
            LOG_ERR("ADC read failed: %d", ret);
        }

        k_sleep(K_MSEC(2000));
    }
    return 0;
}