#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"
#include "hal/adc_types.h"

/**
 * ESP32 Audio Kit V2.2 A404 (ESP32-A1S) Pin Definitions
 */

// I2S Interface
#define I2S_BCK_IO      (GPIO_NUM_27)
#define I2S_WS_IO       (GPIO_NUM_25)
#define I2S_DO_IO       (GPIO_NUM_26)
#define I2S_DI_IO       (GPIO_NUM_35)
#define I2S_MCLK_IO     (GPIO_NUM_0)

// I2C Interface (Audio Codec AC101 / ES8388)
#define I2C_SDA_IO      (GPIO_NUM_33)
#define I2C_SCL_IO      (GPIO_NUM_32)

// Peripherals
#define BUTTON_1_IO     (GPIO_NUM_36)
#define BUTTON_2_IO     (GPIO_NUM_13)
#define BUTTON_3_IO     (GPIO_NUM_19)
#define BUTTON_4_IO     (GPIO_NUM_23)
#define BUTTON_5_IO     (GPIO_NUM_18)
#define BUTTON_6_IO     (GPIO_NUM_5)

#define LED_1_IO        (GPIO_NUM_22)
#define LED_2_IO        (GPIO_NUM_19)

#define EXT_KEY_IO      (GPIO_NUM_22) /* Expansion header pin (safer than 21) */

// SD Card (SPI mode or SDMMC)
#define SD_MMC_CLK      (GPIO_NUM_14)
#define SD_MMC_CMD      (GPIO_NUM_15)
#define SD_MMC_D0       (GPIO_NUM_2)
#define SD_MMC_D1       (GPIO_NUM_4)
#define SD_MMC_D2       (GPIO_NUM_12)
#define SD_MMC_D3       (GPIO_NUM_13)

#endif // BOARD_PINS_H
