#pragma once

#include "esp_err.h"

/* ─── WiFi Configuration ─── */
#define WIFI_AP_SSID        "IH-2000"
#define WIFI_AP_PASS        "induction2000"
#define WIFI_AP_CHANNEL      1
#define WIFI_AP_MAX_CONN     4

/* ─── Public API ─── */
esp_err_t wifi_init_ap(void);
esp_err_t wifi_init_sta(const char *ssid, const char *pass);
bool      wifi_is_connected(void);
const char *wifi_get_mode_str(void);   // "AP" or "STA"
const char *wifi_get_ip_str(void);
void      wifi_get_ip(char *buf, size_t len);
void      wifi_stop(void);
