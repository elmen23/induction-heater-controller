#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <cstring>

static const char *TAG = "WiFi";

static esp_netif_t      *s_netif    = nullptr;
static char              s_ip_str[16] = "0.0.0.0";
static const char       *s_mode_str = "NONE";

/* ─── Event handler ─── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station connected to AP");
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "STA connected to router");
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA disconnected — will reconnect");
        esp_wifi_connect();
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
    }
}

/* ─── Init common (NVS, netif, event loop) ─── */
static esp_err_t wifi_common_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase required — erasing and re-init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    return ESP_OK;
}

/* ─── Soft AP mode ─── */
esp_err_t wifi_init_ap(void) {
    wifi_common_init();

    s_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t ap_cfg = {};
    memcpy(ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(WIFI_AP_SSID));
    memcpy(ap_cfg.ap.password, WIFI_AP_PASS, sizeof(WIFI_AP_PASS));
    ap_cfg.ap.ssid_len = 0;
    ap_cfg.ap.channel = WIFI_AP_CHANNEL;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_cfg.ap.max_connection = WIFI_AP_MAX_CONN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_mode_str = "AP";
    ESP_LOGI(TAG, "AP started  SSID:'%s'  IP:192.168.4.1", WIFI_AP_SSID);
    strcpy(s_ip_str, "192.168.4.1");
    return ESP_OK;
}

/* ─── Station mode ─── */
esp_err_t wifi_init_sta(const char *ssid, const char *pass) {
    wifi_common_init();

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (pass && strlen(pass) > 0) {
        strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    s_mode_str = "STA";
    ESP_LOGI(TAG, "STA connecting to '%s' ...", ssid);
    return ESP_OK;
}

bool wifi_is_connected(void) {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

const char *wifi_get_mode_str(void) { return s_mode_str; }
const char *wifi_get_ip_str(void)   { return s_ip_str; }

void wifi_get_ip(char *buf, size_t len) {
    strncpy(buf, s_ip_str, len - 1);
    buf[len - 1] = '\0';
}

void wifi_stop(void) {
    esp_wifi_stop();
    esp_wifi_deinit();
}
