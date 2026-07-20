/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <config.h>
#include <heater_manager.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* PT1000 front-end calibration: 5.11k reference, 24-bit bipolar.
 * Gain caps the measurable resistance at R_REF/gain, so PT1000 needs 4x
 * (~1277 ohm ceiling); 8x would clip at ~638 ohm and 16x at ~319 ohm. */
#define RTD_REFERENCE_OHMS   5110.0f
#define ADC_GAIN             4.0f
#define ADC_RESOLUTION_BITS  24
#define RTD_NOMINAL_OHMS     1000.0f
#define RTD_TEMP_COEFFICIENT 3850.0f

#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_ALIAS(sensor_test));
#endif

int main(void)
{
	int ret;
	thermal_config_t *config = config_load_defaults();
    if (!config) {
        LOG_ERR("Failed to load default config");
        return -1;
    }

	strncpy(config->heaters[0].id, "high-power-1", MAX_ID_LENGTH - 1);
    config->heaters[0].type = HEATER_TYPE_HIGH_POWER;
    config->heaters[0].max_power_w = 40.0f;     // 40W max
    config->heaters[0].resistance_ohms = 30.0f;  // 30 Ohms
    config->heaters[0].regulator_dev = DEVICE_DT_GET(DT_ALIAS(heater_test));
    config->heaters[0].enabled = true;

	ret = heater_manager_init(config);
	if (ret < 0) {
		LOG_ERR("Failed to initialize heater manager (%d)", ret);
		return 0;
	}

	ret = heater_manager_set_power("high-power-1", 5.0f);
	if (ret < 0) {
		LOG_ERR("Failed to set heater power (%d)", ret);
		return 0;
	}


#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
	int32_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};


	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
		return 0;
	}

	ret = adc_channel_setup_dt(&adc_channel);
	if (ret < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", 0, ret);
		return 0;
	}

	ret = adc_sequence_init_dt(&adc_channel, &sequence);
	if (ret < 0) {
		LOG_ERR("Could not initialize sequence (%d)", ret);
		return 0;
	}


	printf("raw,resistance\n");

    // --- Read AD7124 Filter Register ---
    // SPI configuration matching the AD7124 driver
    // Speed: 1MHz, Operation: Word Size 8, Mode 3 (CPOL | CPHA)
    static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(DT_NODELABEL(ad7124_dev), SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8), 0);

    if (!spi_is_ready_dt(&spi_dev)) {
        LOG_ERR("SPI device not ready");
    } else {
        // --- Write 0x0607FF to Filter Register 0 (Address 0x21) ---
        // Command: Write (0x00) | Address (0x21) -> 0x21
        // Data: 0x06, 0x07, 0xFF
        uint8_t write_tx_buf[4] = {0x21, 0x06, 0x07, 0xFF};
        struct spi_buf write_tx_bufs[] = {{.buf = write_tx_buf, .len = 4}};
        struct spi_buf_set write_tx_set = {.buffers = write_tx_bufs, .count = 1};
        
        int ret_write = spi_transceive_dt(&spi_dev, &write_tx_set, NULL);
        if (ret_write < 0) {
            LOG_ERR("SPI Write failed: %d", ret_write);
        } else {
            LOG_INF("Wrote 0x0607FF to Filter Register 0");
        }
        
        k_sleep(K_MSEC(10)); // Short delay
        
        uint8_t tx_buf[2];
        
        // Read Filter Register 0 (Address 0x21)
        // Command: Read (0x40) | Address (0x21)
        
        tx_buf[0] = 0x40 | 0x21; // Read 0x21
        tx_buf[1] = 0x00;
        
        struct spi_buf tx_bufs[] = {
            { .buf = tx_buf, .len = 1 }
        };
        struct spi_buf_set tx_set_2 = { .buffers = tx_bufs, .count = 1 };
        
        uint8_t rx_data[4] = {0};
        struct spi_buf rx_bufs[] = {
            { .buf = rx_data, .len = 4 }
        };
        struct spi_buf_set rx_set = { .buffers = rx_bufs, .count = 1 };
        
        // Note: Zephyr SPI transceive might need matching lengths if not using specific flags.
        // But for many devices, we write 1 byte then read N bytes.
        // Let's use two structs if needed, or just 1 struct with sufficient length.
        // Actually, typically we send the command, and simultaneously read garbage, then send dummy and read data.
        
        // Let's try to match driver behavior:
        // Driver uses: tx_buf len 1. rx_buf len = 1 + data_len.
        
        ret = spi_transceive_dt(&spi_dev, &tx_set_2, &rx_set);
        if (ret < 0) {
            LOG_ERR("SPI Transceive failed: %d", ret);
        } else {
            // Data is in rx_data[1], rx_data[2], rx_data[3]
            uint32_t filter_val = (rx_data[1] << 16) | (rx_data[2] << 8) | rx_data[3];
            LOG_INF("Filter Register 0 (0x21) Value: 0x%06X (RX: %02X %02X %02X %02X)", filter_val, rx_data[0], rx_data[1], rx_data[2], rx_data[3]);
            printf("Filter Register 0: 0x%06X\n", filter_val);
        }
    }

    uint32_t start_time = k_uptime_get_32();
    int sample_count = 5000;

	for (int i = 0; i < sample_count; i++) {
		ret = adc_read(adc_channel.dev, &sequence);

		if (ret < 0) {
			LOG_ERR("Could not read (%d)", ret);
			continue;
		}
		
		int32_t val_mv = buf;
		
		ret = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
		if (ret < 0) {
			LOG_WRN("Call to adc_raw_to_millivolts_dt failed: %d", ret);
		} else {
             /* 
              * Note: The AD7124 driver in Zephyr might rely on the reference setup in DT.
              * We primarily trust the raw value and the library conversion.
              */
            
			const int32_t max_count = (1 << (ADC_RESOLUTION_BITS - 1)) - 1;
			float r_rtd = (((float)buf - (float)max_count) * RTD_REFERENCE_OHMS) /
			              (ADC_GAIN * (float)max_count);
            float temp_c = (r_rtd - RTD_NOMINAL_OHMS) / (RTD_TEMP_COEFFICIENT / RTD_NOMINAL_OHMS);
            float temp_k = temp_c + 273.15f;


			// Print to standard output for easy viewing
			printf("%6d,%.6f,%.6f,%.6f\n", buf, (double)r_rtd, (double)temp_c, (double)temp_k);

		}
	}

    uint32_t end_time = k_uptime_get_32();
    uint32_t duration = end_time - start_time;
    printf("Total Time: %u ms\n", duration);
    if(duration > 0) {
        printf("Frequency: %.2f Hz\n", (double)sample_count * 1000.0 / (double)duration);
    }

    
    // --- Read AD7124 Filter Register AGAIN after loop ---
    if (spi_is_ready_dt(&spi_dev)) {
         uint8_t tx_buf[2];
         tx_buf[0] = 0x40 | 0x21; // Read 0x21
         tx_buf[1] = 0x00;
         
         struct spi_buf tx_bufs[] = {
             { .buf = tx_buf, .len = 1 }
         };
         struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 1 };
         
         uint8_t rx_data[4] = {0};
         struct spi_buf rx_bufs[] = {
             { .buf = rx_data, .len = 4 }
         };
         struct spi_buf_set rx_set = { .buffers = rx_bufs, .count = 1 };
         
         int ret_read = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
         if (ret_read < 0) {
             LOG_ERR("SPI Transceive failed: %d", ret_read);
         } else {
             uint32_t filter_val = (rx_data[1] << 16) | (rx_data[2] << 8) | rx_data[3];
             LOG_INF("Filter Register 0 (0x21) Value (After Loop): 0x%06X", filter_val);
             printf("Filter Register 0 (After Loop): 0x%06X\n", filter_val);
         }
    }

	ret = heater_manager_set_power("high-power-1", 0.0f);
	if (ret < 0) {
		LOG_ERR("Failed to set heater power (%d)", ret);
		return 0;
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
