idf_component_register(
    SRCS "app_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lcd               # our LCD component (for pin / size macros)
        esp_lcd
        esp_timer
        freertos
        log
        lvgl              # managed via idf_component.yml
)