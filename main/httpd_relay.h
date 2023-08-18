#include <esp_err.h>
#include <esp_http_server.h>
#include "relay.h"

esp_err_t httpd_relay_register_handlers(const httpd_handle_t httpd, const relay_t relay);
