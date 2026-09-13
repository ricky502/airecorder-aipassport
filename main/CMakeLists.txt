idf_component_register(
    SRCS "main.c"
         "inspiration_adpcm.c"
         "inspiration_chunk_queue.c"
         "inspiration_recorder.c"
         "inspiration_ui.c"
         "inspiration_power.c"
         "meditation_hours_images.c"
         "inspiration_upload.c"
         "inspiration_wifi.c"
         "inspiration_storage.c"
         "inspiration_state.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_wifi.c"
         "demo_ble.c"
         "demo_low_power.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_http_client esp_http_server esp_hw_support esp_netif esp_timer esp_wifi nvs_flash spiffs
)

# Maintainers can provide an endpoint only in their local CMake invocation.
# Nothing is persisted in the public project files or defaults.
if(DEFINED INSPIRATION_LOCAL_ENDPOINT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        INSPIRATION_DEFAULT_BACKEND_ENDPOINT=\"${INSPIRATION_LOCAL_ENDPOINT}\")
endif()
