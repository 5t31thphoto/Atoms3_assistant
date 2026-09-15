idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_http_client json esp_timer driver m5unified m5gfx arduino-esp32 esp-sr picotts)

# Arduino-ESP32 3.3.x builds the BLE library as part of the Arduino component,
# but the BLE library include directory is not otherwise exposed to this IDF component.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${arduino-esp32_SOURCE_DIR}/libraries/BLE/src"
)
