idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        lcd
        lvgl_setup
        freertos
        log
)