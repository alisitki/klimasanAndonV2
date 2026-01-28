idf_component_register(
    SRCS 
        "main.c"
        "andon_display.c"
        "led_strip.c"
        "led_strip_encoder.c"
        "rtc_ds1307.c"
        "ir_remote.c"
        "button_handler.c"
        "nvs_storage.c"
    INCLUDE_DIRS "."
)
