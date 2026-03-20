#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mdns.h"
#include "wifi_manager.h"

static const char *TAG = "wifi_mgr";
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static bool connected = false;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
        connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        connected = true;
    }
}

static esp_err_t init_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "mDNS init failed: %d", err);
        return err;
    }
    mdns_hostname_set("cw-trainer");
    mdns_instance_name_set("CW Trainer Web Interface");
    ESP_LOGI(TAG, "mDNS initialized: cw-trainer.local");
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config_sta = {0};
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        size_t s_len = sizeof(wifi_config_sta.sta.ssid);
        size_t p_len = sizeof(wifi_config_sta.sta.password);
        if (nvs_get_str(nvs, "wifi_ssid", (char*)wifi_config_sta.sta.ssid, &s_len) == ESP_OK &&
            nvs_get_str(nvs, "wifi_pass", (char*)wifi_config_sta.sta.password, &p_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found stored credentials for SSID: %s", wifi_config_sta.sta.ssid);
        }
        nvs_close(nvs);
    }

    wifi_config_t wifi_config_ap = {
        .ap = {
            .ssid = "CW-Trainer",
            .ssid_len = strlen("CW-Trainer"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    /* Always use APSTA mode to allow scanning even if no credentials exist */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    if (strlen((char*)wifi_config_sta.sta.ssid) > 0) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config_ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    init_mdns();

    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password)-1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "wifi_ssid", ssid);
        nvs_set_str(nvs, "wifi_pass", password);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return connected;
}

esp_err_t wifi_manager_scan_start(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };
    /* Block for 3 seconds max to get results */
    esp_wifi_scan_start(&scan_config, true);
    return ESP_OK;
}

esp_err_t wifi_manager_get_sta_ssid(char *ssid, size_t max_len)
{
    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK) {
        strncpy(ssid, (char*)config.sta.ssid, max_len);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t wifi_manager_set_ap_enabled(bool enabled)
{
    if (enabled) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    } else {
        /* Only disable if we are connected to STA */
        if (connected) {
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        } else {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

bool wifi_manager_is_ap_enabled(void)
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}
