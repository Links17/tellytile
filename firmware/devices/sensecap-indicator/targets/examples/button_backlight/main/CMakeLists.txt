idf_component_register(SRCS "main.c" "setup_portal.c" "setup_portal_config.c" "session_connection.c" "channel_menu.c" "stream_status.c" "playback_profile.c"
    "reconnect_view.c" "av_picture.c" "ui/ui_text.c"
    "fav1/av_protocol.c" "fav1/av_adpcm.c" "fav1/av_channel_policy.c"
    "fav1/pairing_client.c" "fav1/tv_config.c" "cjson/cJSON.c"
    INCLUDE_DIRS "." "fav1" "cjson" "ui"
    EMBED_FILES "ui/ui_font_cjk.bin"
    REQUIRES bsp esp_psram esp_rom esp_timer esp_wifi esp_event esp_driver_ledc esp-tls mbedtls http_parser esp_http_server bootloader_support esp_netif nvs_flash lwip)
