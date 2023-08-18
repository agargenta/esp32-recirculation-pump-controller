#pragma once

#include <esp_check.h>
#include <esp_http_server.h>
#include <cJSON.h>

esp_err_t httpd_util_send_json_object(const char *TAG, httpd_req_t *req, const cJSON *object);

esp_err_t httpd_util_register_handlers(const char *TAG,
                                       const httpd_handle_t httpd,
                                       const httpd_uri_t handlers[],
                                       const size_t len);