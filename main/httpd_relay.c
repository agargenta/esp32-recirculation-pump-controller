#include <esp_check.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "httpd_relay.h"

#define USEC_IN_SEC (float)1000000

static const char *TAG = "httpd_relay";

static esp_err_t add_state_attrs(cJSON *o, const relay_data_t *data, const relay_state_t state)
{
    ESP_RETURN_ON_FALSE(o, ESP_ERR_NO_MEM, TAG, "create object");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(o, "count", relay_get_total_state_changes(data, state)), ESP_ERR_NO_MEM, TAG, "add count");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(o, "total_duration", relay_get_total_time_in_state(data, state) / USEC_IN_SEC), ESP_ERR_NO_MEM, TAG, "add total_duration");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(o, "average_duration", relay_get_average_time_in_state(data, state) / USEC_IN_SEC), ESP_ERR_NO_MEM, TAG, "add average_duration");
    ESP_RETURN_ON_FALSE(cJSON_AddNumberToObject(o, "fraction_of_time", relay_get_fraction_of_time_in_state(data, state)), ESP_ERR_NO_MEM, TAG, "add fraction_of_time");
    return ESP_OK;
}

static esp_err_t get_all(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Getting all data");
    relay_data_t data = {};
    ESP_RETURN_ON_ERROR(relay_get_data((relay_t)req->user_ctx, &data), TAG, "get data");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTPD_TYPE_JSON), TAG, "send content type");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(
        cJSON_AddStringToObject(object, "state", data.current_state ? "on" : "off"),
        ESP_ERR_NO_MEM, free_object, TAG, "add json attribute");
    ESP_GOTO_ON_ERROR(add_state_attrs(cJSON_AddObjectToObject(object, "off"), &data, RELAY_OFF), free_object, TAG, "add 'off' data");
    ESP_GOTO_ON_ERROR(add_state_attrs(cJSON_AddObjectToObject(object, "on"), &data, RELAY_ON), free_object, TAG, "add 'on' data");
    char *json = cJSON_Print(object);
    ESP_GOTO_ON_FALSE(json, ESP_ERR_NO_MEM, free_object, TAG, "json to string");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, json), free_json, TAG, "send response");
free_json:
    cJSON_free(json);
free_object:
    cJSON_Delete(object);
    return ret;
}

static esp_err_t get_for_state(httpd_req_t *req, const relay_state_t state)
{
    ESP_LOGI(TAG, "Getting data for '%s' state", state ? "on" : "off");
    relay_data_t data = {};
    ESP_RETURN_ON_ERROR(relay_get_data((relay_t)req->user_ctx, &data), TAG, "get data");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTPD_TYPE_JSON), TAG, "send content type");
    cJSON *object = cJSON_CreateObject();
    ESP_RETURN_ON_FALSE(object, ESP_ERR_NO_MEM, TAG, "create json object");
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(add_state_attrs(object, &data, state), free_object, TAG, "add data");
    char *json = cJSON_Print(object);
    ESP_GOTO_ON_FALSE(json, ESP_ERR_NO_MEM, free_object, TAG, "json to string");
    ESP_GOTO_ON_ERROR(httpd_resp_sendstr(req, json), free_json, TAG, "send response");
free_json:
    cJSON_free(json);
free_object:
    cJSON_Delete(object);
    return ret;
}

static esp_err_t get_off(httpd_req_t *req)
{
    return get_for_state(req, RELAY_OFF);
}

static esp_err_t get_on(httpd_req_t *req)
{
    return get_for_state(req, RELAY_ON);
}

static esp_err_t set_state(httpd_req_t *req, const relay_state_t state)
{
    ESP_LOGI(TAG, "Changing state to %d", state);
    esp_err_t err = relay_set_state((relay_t)req->user_ctx, state);
    if (err == ESP_OK)
    {
        ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "204 No Content"), TAG, "send 204");
    }
    else if (err == ESP_ERR_INVALID_STATE)
    {
        ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "409 Conflict"), TAG, "send 409");
    }
    else
    {
        return err;
    }
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t turn_off(httpd_req_t *req)
{
    return set_state(req, RELAY_OFF);
}

static esp_err_t turn_on(httpd_req_t *req)
{
    return set_state(req, RELAY_ON);
}

esp_err_t httpd_relay_register_handlers(const httpd_handle_t httpd, const relay_t relay)
{
    const httpd_uri_t handlers[] = {
        {.user_ctx = relay, .method = HTTP_GET, .uri = "/relay", .handler = get_all},
        {.user_ctx = relay, .method = HTTP_GET, .uri = "/relay/off", .handler = get_off},
        {.user_ctx = relay, .method = HTTP_PUT, .uri = "/relay/off", .handler = turn_off},
        {.user_ctx = relay, .method = HTTP_GET, .uri = "/relay/on", .handler = get_on},
        {.user_ctx = relay, .method = HTTP_PUT, .uri = "/relay/on", .handler = turn_on},
    };
    for (int i = 0, len = sizeof(handlers) / sizeof(httpd_uri_t); i < len; i++)
    {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &handlers[i]), TAG,
                            "register handler: %s", handlers[i].uri);
    }
    return ESP_OK;
}