#include "file_server.h"
#include "rest_api.h"
#include "config.h"
#include "esp_spiffs.h"
#include "esp_log.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "file_server";
static httpd_handle_t s_server = NULL;

#define SPIFFS_BASE "/spiffs"
#define SCRATCH_BUFSIZE 4096

esp_err_t file_server_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = SPIFFS_BASE,
        .partition_label        = "spiffs",
        .max_files              = 8,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %zu/%zu bytes used", used, total);
    return ESP_OK;
}

static const char *get_content_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".js"))   return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png"))  return "image/png";
    if (strstr(path, ".ico"))  return "image/x-icon";
    if (strstr(path, ".svg"))  return "image/svg+xml";
    if (strstr(path, ".woff2")) return "font/woff2";
    return "text/plain";
}

static esp_err_t serve_file(httpd_req_t *req, const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) return ESP_ERR_NOT_FOUND;

    httpd_resp_set_type(req, get_content_type(filepath));
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");

    char *buf = malloc(SCRATCH_BUFSIZE);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t n;
    while ((n = fread(buf, 1, SCRATCH_BUFSIZE, f)) > 0) {
        httpd_resp_send_chunk(req, buf, n);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    free(buf);
    fclose(f);
    return ESP_OK;
}

static esp_err_t serve_index(httpd_req_t *req)
{
    const char *path = SPIFFS_BASE "/index.html";
    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Not Found", 9);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    // index.html nigdy nie cachować — odwołuje się do hashowanych assetów
    // i po aktualizacji SPIFFS przeglądarka musi pobrać nową wersję
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char *buf = malloc(SCRATCH_BUFSIZE);
    if (buf) {
        size_t n;
        while ((n = fread(buf, 1, SCRATCH_BUFSIZE, f)) > 0)
            httpd_resp_send_chunk(req, buf, n);
        httpd_resp_send_chunk(req, NULL, 0);
        free(buf);
    }
    fclose(f);
    return ESP_OK;
}

static esp_err_t static_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    // Strona główna i wszystkie ścieżki SPA bez rozszerzenia → index.html
    if (strcmp(uri, "/") == 0 || strrchr(uri, '.') == NULL) {
        return serve_index(req);
    }

    // Pliki statyczne (assets z rozszerzeniem)
    char filepath[520];
    snprintf(filepath, sizeof(filepath), SPIFFS_BASE "%.511s", uri);

    esp_err_t err = serve_file(req, filepath);
    if (err == ESP_ERR_NOT_FOUND) {
        // Ostatni fallback — może to SPA route z kropką w nazwie
        return serve_index(req);
    }
    return ESP_OK;
}

httpd_handle_t file_server_get_handle(void) { return s_server; }

esp_err_t file_server_register(httpd_handle_t server)
{
    httpd_uri_t static_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = static_handler,
    };
    httpd_register_uri_handler(server, &static_uri);
    ESP_LOGI(TAG, "static file handler registered");
    return ESP_OK;
}

void file_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers      = 32;
    cfg.stack_size            = TASK_STACK_WEBSERVER;
    cfg.uri_match_fn          = httpd_uri_match_wildcard;
    cfg.lru_purge_enable      = true;
    cfg.recv_wait_timeout     = 30;   // 30s — wymagane dla dużych plików OTA

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    // REST API rejestrowane PRZED statycznym handlerem (bardziej specyficzne)
    rest_api_register(s_server);
    file_server_register(s_server);

    ESP_LOGI(TAG, "HTTP server started");
}
