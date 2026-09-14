idf_component_register(
    SRCS
        "main.c"
        "audio_echo.c"
        "fox_avatar.c"
        "tools.c"
        "llm_client.c"
        "config_store.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_http_client
        json
        driver
        esp_lcd
        bt
)
