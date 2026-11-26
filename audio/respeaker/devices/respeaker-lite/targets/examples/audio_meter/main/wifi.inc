static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        if (!(atomic_load(&network_link) & 1u) || event->ip_changed)
            relay_link_publish(&network_link, true);
        xEventGroupSetBits(network, BIT0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        relay_link_publish(&network_link, false);
        xEventGroupClearBits(network, BIT0);
    } else if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        relay_link_publish(&network_link, false);
        xEventGroupClearBits(network, BIT0);
        esp_wifi_connect();
    }
}

static void start_wifi(const speaker_config_t *settings)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    configASSERT(esp_netif_create_default_wifi_sta() != NULL);
    network = xEventGroupCreate();
    configASSERT(network);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, network_event, NULL));
    wifi_config_t config = {0};
    const size_t ssid_size = strlen(settings->ssid);
    const size_t password_size = strlen(settings->password);
    if (ssid_size == 0 || ssid_size > sizeof config.sta.ssid || password_size >= sizeof config.sta.password) {
        ESP_LOGE(TAG, "Invalid Wi-Fi configuration");
        return;
    }
    memcpy(config.sta.ssid, settings->ssid, ssid_size);
    memcpy(config.sta.password, settings->password, password_size);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    memset(&config, 0, sizeof config);
    ESP_ERROR_CHECK(esp_wifi_start());
}

