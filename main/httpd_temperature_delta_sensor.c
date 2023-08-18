#include <esp_check.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "httpd_temperature_delta_sensor.h"

#define USEC_IN_SEC (float)1000000

static const char *TAG = "httpd_temperature_delta_sensor";

// TODO: add units (C vs F)

static esp_err_t add_info_attrs(cJSON *object, temperature_delta_sensor_info_t *info)
{
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "null object");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "latest", info->latest), ESP_ERR_NO_MEM, TAG, "add latest");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "min", info->min), ESP_ERR_NO_MEM, TAG, "add min");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "max", info->max), ESP_ERR_NO_MEM, TAG, "add max");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "average", info->average), ESP_ERR_NO_MEM, TAG, "add average");
    return ESP_OK;
}

static esp_err_t get_all(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all data");
    temperature_delta_sensor_data_t data;
    ESP_RETURN_ON_ERROR(
        temperature_delta_sensor_get_data((const temperature_delta_sensor_t)req->user_ctx, &data),
        TAG, "get data");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTPD_TYPE_JSON), TAG, "send content type");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(
        cJSON_AddNumberToObject(object, "readings", data.readings),
        ESP_ERR_NO_MEM, free_object, TAG, "add readings");
    ESP_GOTO_ON_FALSE(
        cJSON_AddNumberToObject(object, "latest_reading_timestamp", data.latest_reading_timestamp),
        ESP_ERR_NO_MEM, free_object, TAG, "add latest_reading_timestamp");
    ESP_GOTO_ON_ERROR(add_info_attrs(cJSON_AddObjectToObject(object, "1"), &data.info[TEMPERATURE_DELTA_SENSOR_FIRST]), free_object, TAG, "add first");
    ESP_GOTO_ON_ERROR(add_info_attrs(cJSON_AddObjectToObject(object, "2"), &data.info[TEMPERATURE_DELTA_SENSOR_SECOND]), free_object, TAG, "add second");
    ESP_GOTO_ON_ERROR(add_info_attrs(cJSON_AddObjectToObject(object, "delta"), &data.info[TEMPERATURE_DELTA_SENSOR_DELTA]), free_object, TAG, "add delta");
    char *json = cJSON_Print(object);
    ESP_GOTO_ON_FALSE(json, ESP_ERR_NO_MEM, free_object, TAG, "json to string");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, json), free_json, TAG, "send response");
free_json:
    cJSON_free(json);
free_object:
    cJSON_Delete(object);
    return ret;
}

static esp_err_t get_position(httpd_req_t *req, temperature_delta_sensor_position_t position)
{
    ESP_LOGI(TAG, "Getting info for position %d", position);
    temperature_delta_sensor_data_t data;
    ESP_RETURN_ON_ERROR(
        temperature_delta_sensor_get_data((const temperature_delta_sensor_t)req->user_ctx, &data),
        TAG, "get data");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTPD_TYPE_JSON), TAG, "send content type");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(add_info_attrs(object, &data.info[position]), free_object, TAG, "add info");
    char *json = cJSON_Print(object);
    ESP_GOTO_ON_FALSE(json, ESP_ERR_NO_MEM, free_object, TAG, "json to string");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, json), free_json, TAG, "send response");
free_json:
    cJSON_free(json);
free_object:
    cJSON_Delete(object);
    return ret;
}

static esp_err_t get_first(httpd_req_t *req)
{
    return get_position(req, TEMPERATURE_DELTA_SENSOR_FIRST);
}

static esp_err_t get_second(httpd_req_t *req)
{
    return get_position(req, TEMPERATURE_DELTA_SENSOR_SECOND);
}

static esp_err_t get_delta(httpd_req_t *req)
{
    return get_position(req, TEMPERATURE_DELTA_SENSOR_DELTA);
}

static esp_err_t reset(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Resetting readings");
    ESP_RETURN_ON_ERROR(temperature_delta_sensor_reset((const temperature_delta_sensor_t)req->user_ctx), TAG, "reset");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "204 No Content"), TAG, "send 204");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t httpd_temperature_delta_sensor_register_handlers(const httpd_handle_t httpd,
                                                           const temperature_delta_sensor_t sensor)
{
    const httpd_uri_t handlers[] = {
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/temperature", .handler = get_all},
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/temperature/1", .handler = get_first},
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/temperature/2", .handler = get_second},
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/temperature/delta", .handler = get_delta},
        {.user_ctx = sensor, .method = HTTP_DELETE, .uri = "/temperature/readings", .handler = reset},
    };
    for (int i = 0, len = sizeof(handlers) / sizeof(httpd_uri_t); i < len; i++)
    {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &handlers[i]), TAG,
                            "register handler: %s", handlers[i].uri);
    }
    return ESP_OK;
}
