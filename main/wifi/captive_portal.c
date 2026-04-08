#include "captive_portal.h"
#include "storage/nvs_storage.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "captive_portal";

static httpd_handle_t s_server    = NULL;
static bool           s_cfg_rcvd  = false;
static app_config_t  *s_cfg       = NULL;
static TaskHandle_t   s_dns_task  = NULL;

// --------------------------------------------------------------------------
// HTML strona konfiguracji
// --------------------------------------------------------------------------
static const char SETUP_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Konfiguracja nawadniania</title>"
"<style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 15px}"
"h2{color:#2a7c2a}label{display:block;margin-top:12px;font-weight:bold}"
"input{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ccc;border-radius:4px}"
"input[type=submit]{background:#2a7c2a;color:#fff;border:0;cursor:pointer;margin-top:20px}"
"input[type=submit]:hover{background:#1f5e1f}</style></head>"
"<body><h2>Konfiguracja sterownika</h2>"
"<form method='POST' action='/setup'>"
"<label>WiFi SSID</label><input name='ssid' required>"
"<label>WiFi Haslo</label><input name='pass' type='password'>"
"<label>MQTT URI (np. mqtt://192.168.1.10)</label><input name='mqtt_uri'>"
"<label>MQTT Uzytkownik</label><input name='mqtt_user'>"
"<label>MQTT Haslo</label><input name='mqtt_pass' type='password'>"
"<label>PHP URL (daily check)</label><input name='php_url'>"
"<label>Serwer NTP</label><input name='ntp_server' placeholder='pool.ntp.org'>"
"<label>Strefa czasowa (offset UTC, np. 2)</label><input name='tz' type='number' min='-12' max='14'>"
"<input type='submit' value='Zapisz i polacz'>"
"</form></body></html>";

static const char SUCCESS_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<title>OK</title></head><body>"
"<h2>Zapisano! ESP32 laczy sie z siecia...</h2>"
"<p>Mozesz zamknac te strone.</p></body></html>";

// --------------------------------------------------------------------------
// Pomocnik: pobierz parametr z URL-encoded body
// --------------------------------------------------------------------------
static void url_decode(char *out, const char *in, size_t max)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < max - 1; i++) {
        if (in[i] == '%' && in[i+1] && in[i+2]) {
            char h[3] = { in[i+1], in[i+2], 0 };
            out[j++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[j++] = ' ';
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

static bool get_param(const char *body, const char *key, char *out, size_t max)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { out[0] = '\0'; return false; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= max) len = max - 1;
    char raw[512];
    strncpy(raw, p, len);
    raw[len] = '\0';
    url_decode(out, raw, max);
    return true;
}

// --------------------------------------------------------------------------
// HTTP handlers
// --------------------------------------------------------------------------
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, strlen(SETUP_HTML));
    return ESP_OK;
}

static esp_err_t setup_post_handler(httpd_req_t *req)
{
    char body[1024] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }
    body[len] = '\0';

    char tz_str[8] = "1";
    get_param(body, "ssid",       s_cfg->wifi_ssid,   sizeof(s_cfg->wifi_ssid));
    get_param(body, "pass",       s_cfg->wifi_pass,   sizeof(s_cfg->wifi_pass));
    get_param(body, "mqtt_uri",   s_cfg->mqtt_uri,    sizeof(s_cfg->mqtt_uri));
    get_param(body, "mqtt_user",  s_cfg->mqtt_user,   sizeof(s_cfg->mqtt_user));
    get_param(body, "mqtt_pass",  s_cfg->mqtt_pass,   sizeof(s_cfg->mqtt_pass));
    get_param(body, "php_url",    s_cfg->php_url,     sizeof(s_cfg->php_url));
    get_param(body, "ntp_server", s_cfg->ntp_server,  sizeof(s_cfg->ntp_server));
    get_param(body, "tz",         tz_str,             sizeof(tz_str));

    if (s_cfg->ntp_server[0] == '\0')
        strncpy(s_cfg->ntp_server, DEFAULT_NTP_SERVER, sizeof(s_cfg->ntp_server) - 1);

    s_cfg->timezone_offset = (int8_t)atoi(tz_str);

    // Wygeneruj token jeśli pusty
    if (s_cfg->api_token[0] == '\0') {
        storage_generate_token(s_cfg->api_token, sizeof(s_cfg->api_token));
    }

    storage_save_config(s_cfg);
    s_cfg_rcvd = true;

    ESP_LOGI(TAG, "config saved via portal: ssid='%s'", s_cfg->wifi_ssid);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SUCCESS_HTML, strlen(SUCCESS_HTML));
    return ESP_OK;
}

// Redirect wszystkiego na portal (captive portal trick)
static esp_err_t catch_all_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP_ADDR "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// --------------------------------------------------------------------------
// DNS server (odpowiada na wszystko adresem AP)
// --------------------------------------------------------------------------
static void dns_server_task(void *arg)
{
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(53),
        .sin_addr   = { .s_addr = htonl(INADDR_ANY) },
    };
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    uint8_t buf[512];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &clen);
        if (n < 12) continue;

        // Odpowiedź DNS: skopiuj nagłówek, ustaw flagi i dodaj A record
        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] = 0x81; // QR=1, Opcode=0, AA=1, TC=0, RD=0
        resp[3] = 0x80; // RA=0, Z=0, RCODE=0
        resp[6] = 0;    // answers count high
        resp[7] = 1;    // answers count low = 1

        int pos = n;
        // Pointer do pytania (0xC00C)
        resp[pos++] = 0xC0; resp[pos++] = 0x0C;
        resp[pos++] = 0x00; resp[pos++] = 0x01; // TYPE A
        resp[pos++] = 0x00; resp[pos++] = 0x01; // CLASS IN
        resp[pos++] = 0x00; resp[pos++] = 0x00;
        resp[pos++] = 0x00; resp[pos++] = 0x3C; // TTL = 60s
        resp[pos++] = 0x00; resp[pos++] = 0x04; // RDLENGTH = 4
        // IP: 192.168.4.1
        resp[pos++] = 192; resp[pos++] = 168;
        resp[pos++] = 4;   resp[pos++] = 1;

        sendto(sock, resp, pos, 0, (struct sockaddr *)&client, clen);
    }
}

// --------------------------------------------------------------------------
// API
// --------------------------------------------------------------------------
esp_err_t captive_portal_start(app_config_t *cfg)
{
    s_cfg     = cfg;
    s_cfg_rcvd = false;

    // Skonfiguruj AP
    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, DEFAULT_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, DEFAULT_AP_PASS, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len       = strlen(DEFAULT_AP_SSID);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();
    ESP_LOGI(TAG, "AP started: SSID='%s' IP=%s", DEFAULT_AP_SSID, AP_IP_ADDR);

    // Uruchom serwer DNS
    xTaskCreate(dns_server_task, "dns", 4096, NULL, 3, &s_dns_task);

    // Uruchom HTTP server
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return ESP_FAIL;
    }

    httpd_uri_t root = { .uri = "/",      .method = HTTP_GET,  .handler = root_get_handler };
    httpd_uri_t post = { .uri = "/setup", .method = HTTP_POST, .handler = setup_post_handler };
    httpd_uri_t any  = { .uri = "/*",     .method = HTTP_GET,  .handler = catch_all_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &post);
    httpd_register_uri_handler(s_server, &any);

    ESP_LOGI(TAG, "portal started");
    return ESP_OK;
}

esp_err_t captive_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_dns_task) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
    }
    ESP_LOGI(TAG, "portal stopped");
    return ESP_OK;
}

bool captive_portal_config_received(void) { return s_cfg_rcvd; }
void captive_portal_reset_flag(void)      { s_cfg_rcvd = false; }
