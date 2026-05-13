#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "hal/ledc_types.h"
#include "soc/gpio_num.h"

#include "MFRC522.h"

static _Noreturn void
halt (void)
{
    for (;;)
    {
        vTaskDelay (pdMS_TO_TICKS (1000));
    }
}

static int
buzzer_init (void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2400,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    int rc;

    if ((rc = ledc_timer_config (&timer_cfg)) != ESP_OK)
        return rc;

    ledc_channel_config_t channel_cfg = {
        .gpio_num = GPIO_NUM_22,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    if ((rc = ledc_channel_config (&channel_cfg)) != ESP_OK)
        return rc;

    return ESP_OK;
}

static void
buzzer_beep (uint32_t duration_ms)
{
    ledc_set_duty (LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 256);
    ledc_update_duty (LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay (pdMS_TO_TICKS (duration_ms));
    ledc_set_duty (LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty (LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void
spi_init (void)
{
    const uint32_t PIN_MOSI = RC522_SPI_BUS_GPIO_MOSI;
    const uint32_t PIN_MISO = RC522_SPI_BUS_GPIO_MISO;
    const uint32_t PIN_SCK = RC522_SPI_BUS_GPIO_SCLK;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_bus_initialize (SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
}

static spi_device_handle_t
rc522_init (void)
{
    const uint32_t PIN_CS = RC522_SPI_SCANNER_GPIO_SDA;

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 500000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 7,
    };

    spi_device_handle_t rc522;
    spi_bus_add_device (SPI2_HOST, &dev_cfg, &rc522);

    return rc522;
}

void
app_main (void)
{
    int rc;

    if ((rc = buzzer_init ()) != ESP_OK)
    {
        fprintf (stderr, "buzzer_init() failed with code %d\n", rc);
        fflush (stderr);
        halt ();
    }

    spi_init ();
    spi_device_handle_t rc522 = rc522_init ();
    PCD_Init (rc522);

    gpio_set_direction (RC522_SCANNER_GPIO_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction (GPIO_NUM_25, GPIO_MODE_OUTPUT);
    gpio_set_direction (GPIO_NUM_26, GPIO_MODE_OUTPUT);
    gpio_set_direction (GPIO_NUM_27, GPIO_MODE_OUTPUT);

    gpio_set_level (RC522_SCANNER_GPIO_RST, 1);
    gpio_set_level (GPIO_NUM_26, 1);
    gpio_set_level (GPIO_NUM_25, 0);
    gpio_set_level (GPIO_NUM_27, 0);

    for (;;)
    {
        if (!PICC_IsNewCardPresent (rc522))
        {
            vTaskDelay (pdMS_TO_TICKS (250));
            continue;
        }

        buzzer_beep (80);

        printf ("Card detected\n");
        bool success = false;

        for (int i = 0; i < 3; i++)
        {
            if (!PICC_ReadCardSerial (rc522))
                continue;

            printf("Read tried %d times\n", i);
            success = true;
            break;
        }

        if (!success)
            continue;

        printf ("UID: ");

        for (uint8_t i = 0; i < uid.size; i++)
        {
            printf ("%x ", uid.uidByte[i]);
        }

        printf ("\n");

        gpio_set_level (GPIO_NUM_26, 0);
        gpio_set_level (GPIO_NUM_25, 0);

        bool denied = uid.uidByte[0] == 0xee;

        gpio_num_t led_gpio = denied ? GPIO_NUM_25 : GPIO_NUM_27;

        for (int i = 0; i < 5; i++)
        {
            gpio_set_level (led_gpio, 1);
            vTaskDelay (pdMS_TO_TICKS (100));

            if (denied)
                buzzer_beep (80);

            gpio_set_level (led_gpio, 0);
            vTaskDelay (pdMS_TO_TICKS (100));

            if (denied)
                buzzer_beep (80);
        }

        PICC_HaltA (rc522);
        PCD_StopCrypto1 (rc522);

        gpio_set_level (GPIO_NUM_26, 1);
        gpio_set_level (GPIO_NUM_25, 0);
        gpio_set_level (GPIO_NUM_27, 0);
    }
}

/**
 * 13 -> RST
 * 19 -> MISO
 * 25 -> Buzzer
 * 34 -> SS
 * 35 -> SCK
 * 23 -> MOSI
 */
