#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Display Physical Dimensions ─────────────────────────────────────────── */
#define LCD_H_RES               240
#define LCD_V_RES               280

/* ─── ST7789V2 SPI GPIO (from schematic) ──────────────────────────────────── */
#define LCD_PIN_DC              4
#define LCD_PIN_CS              5
#define LCD_PIN_CLK             6
#define LCD_PIN_MOSI            7
#define LCD_PIN_RST             8
#define LCD_PIN_BL              15   /* Backlight – active HIGH */
#define LCD_PIN_MISO            (-1) /* Not used for write-only display */

/* ─── SPI Bus Configuration ───────────────────────────────────────────────── */
#define LCD_SPI_HOST            SPI2_HOST
#define LCD_SPI_CLK_HZ          (40 * 1000 * 1000)  /* 40 MHz */
#define LCD_CMD_BITS            8
#define LCD_PARAM_BITS          8

/* ─── LVGL draw-buffer size (in pixels) ──────────────────────────────────── */
/* Using 1/10 of the full frame – a good balance between RAM and performance   */
#define LCD_DRAW_BUFFER_SIZE    (LCD_H_RES * LCD_V_RES / 10)

/**
 * @brief  Initialise the ST7789V2 LCD panel.
 *
 * Steps performed internally:
 *  1. Configure & enable the PWM backlight (off initially).
 *  2. Install SPI bus (SPI2_HOST).
 *  3. Create esp_lcd panel-IO object (SPI).
 *  4. Install ST7789 panel driver and run hw-reset + init sequence.
 *  5. Apply mirror / swap settings for correct portrait orientation.
 *  6. Turn the backlight on.
 *
 * @param[out] panel_io_handle   Pointer that receives the panel-IO handle.
 * @param[out] panel_handle      Pointer that receives the panel handle.
 * @return     ESP_OK on success, error code otherwise.
 */
esp_err_t app_lcd_init(esp_lcd_panel_io_handle_t *panel_io_handle,
                       esp_lcd_panel_handle_t    *panel_handle);

#ifdef __cplusplus
}
#endif
