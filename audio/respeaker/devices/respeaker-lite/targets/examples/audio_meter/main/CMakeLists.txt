idf_component_register(SRCS "main.c" "audio_playout.c" "speaker_config.c" "speaker_setup.c"
    "fav1/av_protocol.c" "fav1/av_adpcm.c"
    INCLUDE_DIRS "." "fav1"
    REQUIRES bsp esp_psram esp_wifi esp_event esp_netif esp_timer esp_system
    esp_driver_usb_serial_jtag nvs_flash lwip)
