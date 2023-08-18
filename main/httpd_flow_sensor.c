#include <esp_check.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "httpd_util.h"
#include "pulse_sensor.h"
#include "httpd_flow_sensor.h"

#define USEC_IN_SEC (float)1000000

static const char *TAG = "httpd_pulse_sensor";

// TODO: add units (liters vs gallons vs pulses)

static esp_err_t add_current_cycle_attrs(cJSON *object, pulse_sensor_data_t *data)
{
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "null object");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_NO_MEM, TAG, "null data");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "pulses", data->current_cycle_pulses), ESP_ERR_NO_MEM, TAG, "add current_cycle_pulses");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "duration", pulse_sensor_get_current_cycle_duration(data) / USEC_IN_SEC), ESP_ERR_NO_MEM, TAG, "add current_cycle_duration");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "rate", pulse_sensor_get_current_cycle_rate(data)), ESP_ERR_NO_MEM, TAG, "add current_cycle_rate");
    return ESP_OK;
}

static esp_err_t add_totals_attrs(cJSON *object, pulse_sensor_data_t *data)
{
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "null object");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_NO_MEM, TAG, "null data");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "pulses", data->total_pulses), ESP_ERR_NO_MEM, TAG, "add total_pulses");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "duration", data->total_duration / USEC_IN_SEC), ESP_ERR_NO_MEM, TAG, "add total_duration");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "rate", pulse_sensor_get_total_rate(data)), ESP_ERR_NO_MEM, TAG, "add total_rate");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "cycles", data->cycles), ESP_ERR_NO_MEM, TAG, "add cycles");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(object, "partial_cycles", data->partial_cycles), ESP_ERR_NO_MEM, TAG, "add partial_cycles");
    return ESP_OK;
}

static esp_err_t get_all(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all");
    pulse_sensor_data_t data = {0};
    ESP_RETURN_ON_ERROR(pulse_sensor_get_data((pulse_sensor_t)req->user_ctx, &data), TAG, "get data");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(cJSON_AddNumberToObject(object, "current_rate", pulse_sensor_get_current_rate(&data)), ESP_ERR_NO_MEM, free_object, TAG, "add current_rate");
    ESP_GOTO_ON_ERROR(add_current_cycle_attrs(cJSON_AddObjectToObject(object, "current_cycle"), &data), free_object, TAG, "add current_cycle");
    ESP_GOTO_ON_ERROR(add_totals_attrs(cJSON_AddObjectToObject(object, "totals"), &data), free_object, TAG, "add totals");
    ESP_GOTO_ON_ERROR(httpd_util_send_json_object(TAG, req, object), free_object, TAG, "send JSON");
free_object:
    cJSON_Delete(object);
    return ret;
}

static esp_err_t get_current_cycle(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting current_cycle");
    pulse_sensor_data_t data = {0};
    ESP_RETURN_ON_ERROR(pulse_sensor_get_data((pulse_sensor_t)req->user_ctx, &data), TAG, "get data");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(add_current_cycle_attrs(object, &data), free_object, TAG, "add attrs");
    ESP_GOTO_ON_ERROR(httpd_util_send_json_object(TAG, req, object), free_object, TAG, "send JSON");
free_object:
    cJSON_Delete(object);
    return ret;
}

static esp_err_t get_totals(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting totals");
    pulse_sensor_data_t data = {0};
    ESP_RETURN_ON_ERROR(pulse_sensor_get_data((pulse_sensor_t)req->user_ctx, &data), TAG, "get data");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(add_totals_attrs(object, &data), free_object, TAG, "add attrs");
    ESP_GOTO_ON_ERROR(httpd_util_send_json_object(TAG, req, object), free_object, TAG, "send JSON");
free_object:
    cJSON_Delete(object);
    return ret;
}

esp_err_t httpd_flow_sensor_register_handlers(const httpd_handle_t httpd,
                                              const pulse_sensor_t sensor)
{
    httpd_uri_t handlers[] = {
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/flow", .handler = get_all},
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/flow/current_cycle", .handler = get_current_cycle},
        {.user_ctx = sensor, .method = HTTP_GET, .uri = "/flow/totals", .handler = get_totals},
    };
    return httpd_util_register_handlers(TAG, httpd, handlers, sizeof(handlers) / sizeof(httpd_uri_t));
}
