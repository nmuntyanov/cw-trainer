#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "wifi_manager.h"
#include "morse_logic.h"
#include "web_server.h"
#include "cw_trainer.h"
#include "nvs_flash.h"
#include "esp_system.h"

static const char *TAG = "web_server";
static httpd_handle_t server_handle = NULL;

/* WebSocket handler */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    return ESP_OK;
}

void web_server_broadcast(const char *msg)
{
    if (!server_handle) return;

    size_t clients = 4; // Max clients
    int client_fds[4];
    if (httpd_get_client_list(server_handle, &clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < clients; i++) {
            if (httpd_ws_get_fd_info(server_handle, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)msg;
                ws_pkt.len = strlen(msg);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;
                httpd_ws_send_frame_async(server_handle, client_fds[i], &ws_pkt);
            }
        }
    }
}

/* Embedded HTML binary data */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/* GET / - Serve index.html */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

/* GET /api/wifi/scan - Return WiFi scan results */
static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    uint16_t number = 10;
    wifi_ap_record_t ap_info[10];
    uint16_t ap_count = 0;

    ESP_LOGI(TAG, "Starting WiFi scan...");
    wifi_manager_scan_start(); // This now blocks until scan is complete
    
    esp_wifi_scan_get_ap_records(&number, ap_info);
    esp_wifi_scan_get_ap_num(&ap_count);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < number; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)ap_info[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_info[i].rssi);
        cJSON_AddItemToArray(root, item);
    }

    const char *sys_info = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);

    return ESP_OK;
}

/* POST /api/wifi/connect - Connect to WiFi */
static esp_err_t wifi_connect_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    const char *ssid = cJSON_GetObjectItem(root, "ssid")->valuestring;
    const char *pass = cJSON_GetObjectItem(root, "pass")->valuestring;

    wifi_manager_connect(ssid, pass);
    cJSON_Delete(root);

    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}");
    return ESP_OK;
}

/* POST /api/settings - Update volume/freq */
static esp_err_t settings_post_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *v = cJSON_GetObjectItem(root, "volume");
        cJSON *f = cJSON_GetObjectItem(root, "freq");
        cJSON *w = cJSON_GetObjectItem(root, "wpm");
        cJSON *n = cJSON_GetObjectItem(root, "noise");
        cJSON *cs = cJSON_GetObjectItem(root, "callsign");
        cJSON *qth = cJSON_GetObjectItem(root, "qth");
        
        uint8_t new_vol = (v && cJSON_IsNumber(v)) ? v->valueint : vol_val;
        uint32_t new_freq = (f && cJSON_IsNumber(f)) ? f->valueint : freq;
        uint32_t new_wpm = (w && cJSON_IsNumber(w)) ? w->valueint : wpm;
        uint8_t new_noise = (n && cJSON_IsNumber(n)) ? n->valueint : noise_level;
        const char *new_cs = (cs && cJSON_IsString(cs)) ? cs->valuestring : NULL;
        const char *new_qth = (qth && cJSON_IsString(qth)) ? qth->valuestring : NULL;

        ESP_LOGI("web_server", "Settings: V=%d F=%"PRIu32" W=%"PRIu32" N=%d CS=%s QTH=%s", new_vol, new_freq, new_wpm, new_noise, new_cs?new_cs:"NULL", new_qth?new_qth:"NULL");
        update_settings(new_freq, new_vol, new_wpm, new_noise, new_cs, new_qth);
        cJSON_Delete(root);
    }
    
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* GET /api/system/status - Return WiFi and system status */
static esp_err_t system_status_get_handler(httpd_req_t *req)
{
    char ssid[33] = {0};
    wifi_manager_get_sta_ssid(ssid, sizeof(ssid)-1);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "sta_ssid", ssid);
    cJSON_AddBoolToObject(root, "sta_connected", wifi_manager_is_connected());
    cJSON_AddBoolToObject(root, "ap_enabled", wifi_manager_is_ap_enabled());
    cJSON_AddNumberToObject(root, "volume", vol_val);
    cJSON_AddNumberToObject(root, "freq", freq);
    cJSON_AddNumberToObject(root, "wpm", wpm);
    cJSON_AddNumberToObject(root, "noise_level", noise_level);
    cJSON_AddStringToObject(root, "callsign", callsign);
    cJSON_AddStringToObject(root, "qth_locator", qth_locator);
    cJSON_AddStringToObject(root, "koch_chars", morse_logic_get_all_chars());
    
    const char *sys_info = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /api/system/ap_toggle - Enable/Disable AP */
static esp_err_t ap_toggle_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    if (enabled) {
        wifi_manager_set_ap_enabled(cJSON_IsTrue(enabled));
    }
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* POST /api/training/play - Play Morse for a character */
extern void trigger_playback(char c); // Defined in cw_trainer.c
static esp_err_t training_play_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *c_obj = cJSON_GetObjectItem(root, "char");
    if (c_obj && c_obj->valuestring) {
        trigger_playback(c_obj->valuestring[0]);
    }
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"playing\"}");
    return ESP_OK;
}

/* POST /api/training/play_sequence - Play a string of characters */
static esp_err_t training_play_sequence_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *s_obj = cJSON_GetObjectItem(root, "sequence");
    if (s_obj && s_obj->valuestring) {
        trigger_playback_string(s_obj->valuestring);
    }
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"playing_sequence\"}");
    return ESP_OK;
}

/* POST /api/reset - Wipe NVS and reboot */
static esp_err_t reset_post_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Factory reset requested!");
    nvs_flash_erase();
    httpd_resp_sendstr(req, "{\"status\":\"resetting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* POST /api/training/target - Set expected char for decoder */
static esp_err_t training_target_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *target = cJSON_GetObjectItem(root, "char");
    if (target && target->valuestring) {
        morse_logic_set_target(target->valuestring[0]);
    } else {
        morse_logic_set_target(0);
    }
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

esp_err_t web_server_init(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;        /* Increased for more headroom during header parsing */
    config.lru_purge_enable = true;  /* Better management of concurrent browser connections */
    config.max_uri_handlers = 16;   /* Accommodate new Phase 4/5 APIs */

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // ... (Register URIs)
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t wifi_scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get_handler };
        httpd_register_uri_handler(server, &wifi_scan);

        httpd_uri_t wifi_conn = { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_post_handler };
        httpd_register_uri_handler(server, &wifi_conn);

        httpd_uri_t status = { .uri = "/api/system/status", .method = HTTP_GET, .handler = system_status_get_handler };
        httpd_register_uri_handler(server, &status);

        httpd_uri_t ap_toggle = { .uri = "/api/system/ap_toggle", .method = HTTP_POST, .handler = ap_toggle_post_handler };
        httpd_register_uri_handler(server, &ap_toggle);

        httpd_uri_t tr_play = { .uri = "/api/training/play", .method = HTTP_POST, .handler = training_play_post_handler };
        httpd_register_uri_handler(server, &tr_play);

        httpd_uri_t tr_target = { .uri = "/api/training/target", .method = HTTP_POST, .handler = training_target_post_handler };
        httpd_register_uri_handler(server, &tr_target);

        httpd_uri_t tr_play_seq = { .uri = "/api/training/play_sequence", .method = HTTP_POST, .handler = training_play_sequence_post_handler };
        httpd_register_uri_handler(server, &tr_play_seq);

        httpd_uri_t settings = { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post_handler };
        httpd_register_uri_handler(server, &settings);

        httpd_uri_t reset = { .uri = "/api/reset", .method = HTTP_POST, .handler = reset_post_handler };
        httpd_register_uri_handler(server, &reset);

        httpd_uri_t ws = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws);

        server_handle = server;
        return ESP_OK;
    }
    return ESP_FAIL;
}
