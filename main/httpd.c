#include <freertos/FreeRTOS.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "sdkconfig.h"
#include "httpd.h"
#include "httpd_relay.h"
#include "httpd_temperature_delta_sensor.h"
#include "httpd_flow_sensor.h"

#define USEC_IN_SEC (double)1000000

static const char *TAG = "httpd";

static esp_err_t index_handler(httpd_req_t *req)
{

    ESP_LOGI(TAG, "uri: /");
    // TODO: implement
    const char *html = "<div><a href=\"/long\">long</a></div>"
                       "<div><a href=\"/quick\">quick</a></div>";
    return httpd_resp_sendstr(req, html);
}

esp_err_t httpd_open(const httpd_context_t *context, httpd_handle_t *httpd_out)
{
    esp_err_t ret = ESP_FAIL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 14;
    config.lru_purge_enable = true;
    config.server_port = CONFIG_HTTPD_PORT;
    config.max_open_sockets = CONFIG_HTTPD_MAX_OPEN_SOCKETS;

    httpd_handle_t httpd = NULL;
    ESP_GOTO_ON_ERROR(httpd_start(&httpd, &config), handle_error, TAG,
                      "start httpd on port %d", config.server_port);

    const httpd_uri_t uri_handler = {.method = HTTP_GET, .uri = "/", .handler = index_handler};
    ESP_GOTO_ON_ERROR(httpd_register_uri_handler(httpd, &uri_handler), stop_httpd, TAG, "register GET /");

    httpd_relay_register_handlers(httpd, context->relay);
    httpd_temperature_delta_sensor_register_handlers(httpd, context->temperature_delta_sensor);
    httpd_flow_sensor_register_handlers(httpd, context->flow_sensor);

    ESP_LOGI(TAG, "Opened httpd on port: %d", config.server_port);
    *httpd_out = httpd;
    return ESP_OK;
stop_httpd:
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_stop(httpd));
    *httpd_out = NULL;
handle_error:
    return ret;
}

esp_err_t httpd_close(const httpd_handle_t httpd)
{
    ESP_RETURN_ON_FALSE(httpd, ESP_ERR_INVALID_ARG, TAG, "httpd not defined");
    ESP_RETURN_ON_ERROR(httpd_stop(httpd), TAG, "stop httpd");
    ESP_LOGI(TAG, "Closed httpd");
    return ESP_OK;
}