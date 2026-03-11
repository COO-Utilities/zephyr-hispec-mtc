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

#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_ALIAS(sensor_test));
#endif

int main(void)
{
	int ret;
    // printk("=== ADC Noise Measurement Demo Booting ===\n");
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

	// k_sleep(K_MSEC(5000));

#if DT_NODE_HAS_STATUS(DT_ALIAS(sensor_test), okay)
	// int ret;
	int32_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};

	// LOG_INF("ADC Noise Measurement Demo Starting");

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

	// LOG_INF("Starting ADC sampling loop...");

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
        // uint8_t write_tx_buf[4] = {0x21, 0x06, 0x03, 0xC0};
        // uint8_t write_tx_buf[4] = {0x21, 0x06, 0x00, 0x01};
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
        // uint8_t rx_buf[4]; // Status + 3 bytes data sent down below.
        
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
    // int sample_count = 1000;
    // int sample_count = 100;
    // int sample_count = 60;

	for (int i = 0; i < sample_count; i++) {
	// for (int i = 0; i < 1000; i++) {
        // LOG_INF("Calling adc_read...");
		ret = adc_read(adc_channel.dev, &sequence);
        // LOG_INF("adc_read returned: %d", ret);

		if (ret < 0) {
			LOG_ERR("Could not read (%d)", ret);
			continue;
		}
		
		int32_t val_mv = buf;
		// LOG_INF("Raw: %d", val_mv);
		
		ret = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
		if (ret < 0) {
			LOG_WRN("Call to adc_raw_to_millivolts_dt failed: %d", ret);
		} else {
             /* 
              * Note: The AD7124 driver in Zephyr might rely on the reference setup in DT.
              * We primarily trust the raw value and the library conversion.
              */
            
			// Calculate Resistance
			// R_sensor = (Raw / Max_Count) * R_REF / Gain
			// Assuming 24-bit resolution and R_REF of 5110.0 Ohms (5.11k)
			// Note: Gain limits max measurable resistance (R_max = R_REF / Gain).
			// Gain 16 => Max ~319 Ohms. Gain 8 => Max ~638 Ohms.
			// For PT1000 (1000 Ohms), we need Gain 4 (Max ~1277 Ohms).
			#define R_REF 5110.0f
			// #define ADC_GAIN 1.0f
			#define ADC_GAIN 4.0f
			// #define ADC_GAIN 16.0f
			#define ADC_RESOLUTION_BITS 24
            #define RTD_TC 3850.0f
            #define R_NOM 1000.0f
			
			int32_t max_count = (1 << (ADC_RESOLUTION_BITS - 1)) - 1;
			// float r_rtd = ((float)buf * R_REF) / (ADC_GAIN * (float)max_count);
			float r_rtd = (((float)buf - (float)max_count) * R_REF) / (ADC_GAIN * (float)max_count);
            float temp_c = (r_rtd - R_NOM) / (RTD_TC / R_NOM);
            float temp_k = temp_c + 273.15f;


			// Print to standard output for easy viewing
			// printf("ADC Channel %d | Raw: %6d | Resistance: %.5f Ohms\n", adc_channel.channel_id, buf, (double)r_rtd);
			printf("%6d,%.6f,%.6f,%.6f\n", buf, (double)r_rtd, (double)temp_c, (double)temp_k);

			// k_sleep(K_MSEC(100));
		}
	}

    uint32_t end_time = k_uptime_get_32();
    uint32_t duration = end_time - start_time;
    printf("Total Time: %u ms\n", duration);
    if(duration > 0) {
        printf("Frequency: %.2f Hz\n", (float)sample_count * 1000.0f / (float)duration);
    }

	// 	return 0;
	// }
    
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
