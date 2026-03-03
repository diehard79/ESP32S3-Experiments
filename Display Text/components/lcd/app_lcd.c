#include "app_lcd.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "app_lcd";

#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_FREQ_HZ     5000
#define BL_LEDC_DUTY_RES    LEDC_TIMER_13_BIT
#define BL_LEDC_DUTY_MAX    8191

static esp_err_t _backlight_init(void)
{
    ledc_timer_config_t bl_timer = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "LEDC timer config failed");

    ledc_channel_config_t bl_channel = {
        .gpio_num   = LCD_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_channel), TAG, "LEDC channel config failed");
    ESP_LOGI(TAG, "Backlight PWM configured on GPIO %d", LCD_PIN_BL);
    return ESP_OK;
}

static esp_err_t _backlight_set(uint8_t percent)
{
    uint32_t duty = ((uint32_t)percent * BL_LEDC_DUTY_MAX) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty),
                        TAG, "ledc_set_duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL),
                        TAG, "ledc_update_duty failed");
    return ESP_OK;
}

esp_err_t app_lcd_init(esp_lcd_panel_io_handle_t *panel_io_handle,
                       esp_lcd_panel_handle_t    *panel_handle)
{
    /* Step 1: Backlight off during init */
    ESP_LOGI(TAG, "Configuring backlight (initially OFF)");
    ESP_RETURN_ON_ERROR(_backlight_init(), TAG, "Backlight init failed");

    /* Step 2: SPI bus */
    ESP_LOGI(TAG, "Initialising SPI2 bus");
    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = LCD_PIN_MISO,
        .sclk_io_num     = LCD_PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_DRAW_BUFFER_SIZE * 2,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
        TAG, "SPI bus init failed");

    /* Step 3: Panel IO */
    ESP_LOGI(TAG, "Installing panel IO (SPI)");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num         = LCD_PIN_DC,
        .cs_gpio_num         = LCD_PIN_CS,
        .pclk_hz             = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits        = LCD_CMD_BITS,
        .lcd_param_bits      = LCD_PARAM_BITS,
        .spi_mode            = 0,
        .trans_queue_depth   = 10,
        .on_color_trans_done = NULL,
        .user_ctx            = NULL,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                 &io_config, panel_io_handle),
        TAG, "Panel IO init failed");

    /* Step 4: ST7789V2 driver
     *
     * ENDIANNESS NOTE:
     * RGB_ENDIAN_RGB  -> panel shows correct colours (dark bg, coloured text)
     * RGB_ENDIAN_BGR  -> panel shows inverted/wrong colours (white bg)
     * Confirmed: this board needs LCD_RGB_ENDIAN_RGB.                       */
    ESP_LOGI(TAG, "Installing ST7789V2 panel driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(*panel_io_handle, &panel_config, panel_handle),
        TAG, "ST7789 panel init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel_handle), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_handle),  TAG, "Panel init failed");

    /* GRAM offset:
     * ST7789V2 GRAM = 240x320, visible panel = 240x280 (40 rows unused).
     * Unused rows are at the top of GRAM on this board.
     * gap(col=0, row=35) shifts the active window down past them.           */
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_set_gap(*panel_handle, 0, 20),
        TAG, "Panel set_gap failed");

    /* Orientation (portrait, correct for this board):
     * swap_xy=false, mirror_x=false, mirror_y=false
     * If text still appears mirrored after flash, toggle mirror_x only.    */
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_swap_xy(*panel_handle, false),
        TAG, "swap_xy failed");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_mirror(*panel_handle, false, false),
        TAG, "mirror failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel_handle, true),
                        TAG, "Display on failed");

    /* Step 5: Backlight ON */
    ESP_LOGI(TAG, "Turning backlight ON");
    ESP_RETURN_ON_ERROR(_backlight_set(100), TAG, "Backlight on failed");

    ESP_LOGI(TAG, "LCD init complete (%dx%d)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}