#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── LVGL Port Task Parameters ───────────────────────────────────────────── */
#define LVGL_TASK_PRIORITY      2
#define LVGL_TASK_STACK_SIZE    (6 * 1024)   /* bytes */
#define LVGL_TICK_PERIOD_MS     2            /* LVGL internal tick interval   */
#define LVGL_TASK_MAX_DELAY_MS  500          /* Max sleep between lv_timer_handler calls */

/**
 * @brief  Initialise the LVGL library and register the LCD as its display.
 *
 * Steps performed internally:
 *  1. Call lv_init().
 *  2. Install a 2 ms periodic esp_timer that feeds lv_tick_inc().
 *  3. Allocate two DMA-capable draw buffers (double-buffering).
 *  4. Create and register an lv_display_t bound to the ST7789 panel.
 *  5. Register the flush callback that routes LVGL pixels → esp_lcd.
 *  6. Spawn a FreeRTOS task that calls lv_timer_handler() in a loop.
 *
 * @param  panel_io_handle  Panel-IO handle from app_lcd_init().
 * @param  panel_handle     Panel handle from app_lcd_init().
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t app_lvgl_init(esp_lcd_panel_io_handle_t panel_io_handle,
                        esp_lcd_panel_handle_t    panel_handle);

/**
 * @brief  Acquire the LVGL mutex before making any lv_* API calls from
 *         application code outside the LVGL task.
 *
 * @param  timeout_ms  Milliseconds to wait; 0 = wait forever.
 * @return true if lock acquired, false on timeout.
 */
bool app_lvgl_lock(uint32_t timeout_ms);

/**
 * @brief  Release the LVGL mutex acquired by app_lvgl_lock().
 */
void app_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif
