idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_http_client json esp_timer driver esp_system esp_hw_support heap m5unified m5gfx arduino-esp32 arduinojson esp-sr picotts
)

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${esp-sr_SOURCE_DIR}/include/esp32"
    "${esp-sr_SOURCE_DIR}/src/include"
)
