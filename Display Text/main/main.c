/**
 * @file   main.c
 * @brief  Experiment 1 - Display "Welcome Saeed !" on the ESP32-S3-Touch-LCD-1.69.
 *
 * Font:    lv_font_montserrat_36
 * Layout:  vertically centred on 240x280
 *
 * COLOUR COMPENSATION:
 * This panel physically swaps the R and B channels of every pixel it renders.
 * We pre-swap all colour hex values so the displayed colour is correct:
 *
 *   Desired colour        Pre-swapped hex     What panel renders
 *   Dark navy  #0D0D1A -> #1A0D0D          -> #0D0D1A (correct dark bg)
 *   Dodger blu #1E90FF -> #FF901E          -> #1E90FF (correct blue)
 *   White      #FFFFFF -> #FFFFFF          -> #FFFFFF (symmetric)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_lcd.h"
#include "app_lvgl.h"

static const char *TAG = "main";

/* ── Pre-swapped colours (R<->B) for this panel ─────────────────────────── */
#define COL_BG       0x1A0D0D   /* renders as dark navy  #0D0D1A */
#define COL_BLUE     0xFF901E   /* renders as dodger blue #1E90FF */

/* ─── UI ─────────────────────────────────────────────────────────────────── */

static void ui_create_welcome_screen(void)
{
    if (!app_lvgl_lock(0)) {
        ESP_LOGE(TAG, "Could not acquire LVGL lock");
        return;
    }

    /* ── Screen ───────────────────────────────────────────────────────────── */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* ── "Welcome" (white, Montserrat 36) ────────────────────────────────── */
    lv_obj_t *lbl_welcome = lv_label_create(scr);
    lv_label_set_text(lbl_welcome, "Welcome");
    lv_obj_set_style_text_font(lbl_welcome, &lv_font_montserrat_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_welcome, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(lbl_welcome, LV_ALIGN_CENTER, 0, -30);

    /* ── Divider (renders blue) ───────────────────────────────────────────── */
    lv_obj_t *divider = lv_obj_create(scr);
    lv_obj_set_size(divider, 190, 2);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COL_BLUE), LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(divider, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
    lv_obj_align(divider, LV_ALIGN_CENTER, 0, 0);

    /* ── "Saeed !" (renders dodger blue, Montserrat 36) ─────────────────── */
    lv_obj_t *lbl_name = lv_label_create(scr);
    lv_label_set_text(lbl_name, "Saeed !");
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(COL_BLUE), LV_PART_MAIN);
    lv_obj_align(lbl_name, LV_ALIGN_CENTER, 0, +32);

    app_lvgl_unlock();
    ESP_LOGI(TAG, "Welcome screen created");
}

/* ─── Entry point ─────────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Experiment 1 : Display Text ===");

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t    panel    = NULL;

    ESP_LOGI(TAG, "Initialising LCD ...");
    ESP_ERROR_CHECK(app_lcd_init(&panel_io, &panel));

    ESP_LOGI(TAG, "Initialising LVGL ...");
    ESP_ERROR_CHECK(app_lvgl_init(panel_io, panel));

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Drawing welcome screen ...");
    ui_create_welcome_screen();

    ESP_LOGI(TAG, "app_main done - LVGL task is running.");
}