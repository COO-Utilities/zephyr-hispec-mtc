#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <config.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* PT1000 front-end calibration: 5.11k reference, AD7124 PGA at 4x, 24-bit bipolar.
 * Temperature is a linear approximation and drifts outside roughly 0-100 C. */
#define RTD_REFERENCE_OHMS   5110.0f
#define ADC_GAIN             4.0f
#define ADC_RESOLUTION_BITS  24
#define RTD_NOMINAL_OHMS     1000.0f
#define RTD_TEMP_COEFFICIENT 3850.0f

#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
// Define channels from the overlay
static const struct adc_dt_spec adc_channels[] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_ALIAS(sensor_test), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_ALIAS(sensor_test), 1),
    ADC_DT_SPEC_GET_BY_IDX(DT_ALIAS(sensor_test), 2),
    ADC_DT_SPEC_GET_BY_IDX(DT_ALIAS(sensor_test), 3),
};
#endif

int main(void)
{
	int ret;

#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
	int32_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

    for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
        if (!adc_is_ready_dt(&adc_channels[i])) {
            LOG_ERR("ADC controller device %s not ready", adc_channels[i].dev->name);
            return 0;
        }
    }

	LOG_INF("Starting ADC sampling loop...");

    uint32_t start_time = k_uptime_get_32();
    int sample_count = 1000;

    for (size_t ch = 0; ch < ARRAY_SIZE(adc_channels); ch++) {
        ret = adc_channel_setup_dt(&adc_channels[ch]);
        if (ret < 0) {
            LOG_ERR("Could not setup channel #%zu (%d)", ch, ret);
            return 0;
        }
    }

	for (int i = 0; i < sample_count; i++) {

        for (size_t ch = 0; ch < ARRAY_SIZE(adc_channels); ch++) {
            (void)adc_sequence_init_dt(&adc_channels[ch], &sequence);

            ret = adc_read(adc_channels[ch].dev, &sequence);

            if (ret < 0) {
                LOG_ERR("Could not read channel %zu (%d)", ch, ret);
                continue;
            }

            int32_t val_mv = buf;

            ret = adc_raw_to_millivolts_dt(&adc_channels[ch], &val_mv);
            if (ret < 0) {
                LOG_WRN("Call to adc_raw_to_millivolts_dt failed: %d", ret);
            } else {
                /* Ratiometric: excitation current cancels, so resistance follows
                 * directly from the bipolar code offset against the reference. */
                const int32_t max_count = (1 << (ADC_RESOLUTION_BITS - 1)) - 1;
                float r_rtd = (((float)buf - (float)max_count) * RTD_REFERENCE_OHMS) /
                              (ADC_GAIN * (float)max_count);
                float temp_c = (r_rtd - RTD_NOMINAL_OHMS) / (RTD_TEMP_COEFFICIENT / RTD_NOMINAL_OHMS);
                float temp_k = temp_c + 273.15f;

                printf("%zu,%6d,%.6f,%.6f,%.6f\n", ch, buf, (double)r_rtd, (double)temp_c, (double)temp_k);
            }
        }
        printf("--------------------------------------------\n");
	}

    uint32_t end_time = k_uptime_get_32();
    uint32_t duration = end_time - start_time;
    printf("Total Time: %u ms\n", duration);
    if(duration > 0) {
        printf("Frequency: %.2f Hz\n", (float)(sample_count * ARRAY_SIZE(adc_channels)) * 1000.0f / (float)duration);
    }

    while(1) {
        k_sleep(K_FOREVER);
    }
#else
    LOG_WRN("ADC Device (sensor_test) is DISABLED in Devicetree");
    while(1) {
        k_sleep(K_SECONDS(1));
    }
#endif

	return 0;
}
