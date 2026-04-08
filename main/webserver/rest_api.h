#pragma once
#include "esp_http_server.h"
#include "esp_err.h"

esp_err_t rest_api_register(httpd_handle_t server);

// Weryfikacja tokenu Bearer (używana przez file_server)
bool rest_auth_check(httpd_req_t *req);
