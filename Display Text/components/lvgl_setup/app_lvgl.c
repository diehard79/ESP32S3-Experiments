#include "app_lvgl.h"
#include "app_lcd.h"           /* for LCD_H_RES, LCD_V_RES, LCD_DRAW_BUFFER_SIZE */

#include "esp_log.h"
#include "esp_check.h"          /* For ESP_RETURN_ON_ERROR */
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

static const char *TAG = "app_lvgl";

/* ─── Module-level state ─────────────────────────────────────────────────── */
static SemaphoreHandle_t s_lvgl_mutex  = NULL;
static lv_display_t     *s_disp        = NULL;

/* ─── Flush callback  ────────────────────────────────────────────────────────
 * Called by LVGL when a rectangular region of the frame-buffer is ready.
 * We forward it straight to esp_lcd which DMA-copies it to the panel.        */
static void _lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                            uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    /* px_map points to LVGL's internal draw buffer already filled with
     * RGB565 pixels; hand it directly to esp_lcd.                          */
    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);

    /* Tell LVGL the flush is complete so it can reuse the buffer.           */
    lv_display_flush_ready(disp);
}

/* ─── LVGL tick source  ──────────────────────────────────────────────────────
 * esp_timer fires every LVGL_TICK_PERIOD_MS ms and advances LVGL's clock.   */
static void _lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ─── LVGL handler task  ─────────────────────────────────────────────────────
 * Runs on Core 1 (PRO_CPU); calls lv_timer_handler() which processes all
 * LVGL timers, animations, and redraws.                                      */
static void _lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started on core %d", xPortGetCoreID());

    for (;;) {
        uint32_t sleep_ms = 10;

        /* All LVGL API calls must be enclosed in lock / unlock. */
        if (app_lvgl_lock(10)) {
            sleep_ms = lv_timer_handler();
            app_lvgl_unlock();
        }

        /* Clamp the sleep so FreeRTOS does not starve other tasks. */
        if (sleep_ms > LVGL_TASK_MAX_DELAY_MS) {
            sleep_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

esp_err_t app_lvgl_init(esp_lcd_panel_io_handle_t panel_io_handle,
                        esp_lcd_panel_handle_t    panel_handle)
{
    /* ── Step 1: Create mutex for thread-safe LVGL access ─────────────────*/
    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    /* ── Step 2: Initialise LVGL core library ─────────────────────────────*/
    ESP_LOGI(TAG, "Initialising LVGL %d.%d.%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_init();

    /* ── Step 3: Install periodic tick timer ──────────────────────────────*/
    const esp_timer_create_args_t tick_timer_args = {
        .callback        = _lvgl_tick_cb,
        .name            = "lvgl_tick",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_timer_handle_t tick_timer;
    ESP_RETURN_ON_ERROR(
        esp_timer_create(&tick_timer_args, &tick_timer),
        TAG, "Failed to create tick timer");
    ESP_RETURN_ON_ERROR(
        esp_timer_start_periodic(tick_timer,
                                 LVGL_TICK_PERIOD_MS * 1000 /* µs */),
        TAG, "Failed to start tick timer");

    /* ── Step 4: Allocate draw buffers (double-buffering, DMA-capable) ────*/
    /* Each buffer holds LCD_DRAW_BUFFER_SIZE pixels × 2 bytes (RGB565).    */
    size_t buf_bytes = LCD_DRAW_BUFFER_SIZE * sizeof(lv_color_t);

    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Not enough DMA memory for draw buffers (%u bytes each)",
                 (unsigned)buf_bytes);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Draw buffers: 2 × %u bytes in DMA-capable SRAM", (unsigned)buf_bytes);

    /* ── Step 5: Create and configure LVGL display ────────────────────────*/
    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!s_disp) {
        ESP_LOGE(TAG, "lv_display_create failed");
        return ESP_FAIL;
    }

    /* Attach double draw-buffers */
    lv_display_set_buffers(s_disp, buf1, buf2,
                           buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Store panel handle in display user-data so the flush callback can
     * retrieve it without needing a global variable.                       */
    lv_display_set_user_data(s_disp, (void *)panel_handle);

    /* Register the flush callback */
    lv_display_set_flush_cb(s_disp, _lvgl_flush_cb);

    ESP_LOGI(TAG, "LVGL display registered  (%dx%d)", LCD_H_RES, LCD_V_RES);

    /* ── Step 6: Spawn LVGL handler task ──────────────────────────────────*/
    BaseType_t rc = xTaskCreatePinnedToCore(
        _lvgl_task,
        "lvgl",
        LVGL_TASK_STACK_SIZE,
        NULL,
        LVGL_TASK_PRIORITY,
        NULL,
        1  /* Pin to Core 1 (APP_CPU) so Core 0 handles Wi-Fi / BT freely) */
    );
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL init complete");
    return ESP_OK;
}

bool app_lvgl_lock(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mutex, ticks) == pdTRUE;
}

void app_lvgl_unlock(void)
{
    xSemaphoreGive(s_lvgl_mutex);
}
