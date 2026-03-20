idf_component_register(SRCS "cw_trainer.c" "wifi_manager.c" "web_server.c" "morse_logic.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "index.html"
                    REQUIRES mdns nvs_flash driver esp_wifi esp_event esp_netif esp_http_server json esp_timer)
