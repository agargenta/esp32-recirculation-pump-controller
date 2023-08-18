#include <esp_check.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "httpd_util.h"

esp_err_t httpd_util_send_json_object(const char *TAG, httpd_req_t *req, const cJSON *object)
{
    ESP_RETURN_ON_FALSE(TAG, ESP_ERR_INVALID_ARG, TAG, "null TAG");
    ESP_RETURN_ON_FALSE(req, ESP_ERR_INVALID_ARG, TAG, "null req");
    ESP_RETURN_ON_FALSE(object, ESP_ERR_INVALID_ARG, TAG, "null object");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTPD_TYPE_JSON), TAG, "send content type");
    esp_err_t ret = ESP_OK;
    char *json = cJSON_Print(object);
    ESP_GOTO_ON_FALSE(json, ESP_ERR_NO_MEM, handle_error, TAG, "json to string");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, json), free_json, TAG, "send response");
free_json:
    cJSON_free(json);
handle_error:
    return ret;
}

esp_err_t httpd_util_register_handlers(const char *TAG,
                                       const httpd_handle_t httpd,
                                       const httpd_uri_t handlers[],
                                       const size_t len)
{
    for (int i = 0; i < len; i++)
    {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &handlers[i]), TAG,
                            "register handler: %s", handlers[i].uri);
    }
    return ESP_OK;
}