#include <stdint.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "relay.h"

#define MUTEX_TIMEOUT pdMS_TO_TICKS(3) // max time to wait for a lock (3 ms)
static const char *TAG = "relay";

struct relay_s
{
    gpio_num_t gpio_num;       /// the GPIO pin number
    relay_state_t state;       /// current state (on or off)
    int64_t timestamp;         /// the time (microseconds since boot) of the latest state change
    uint64_t time_in_state[2]; /// time (in microseconds) spent in each state (up to last change)
    uint32_t state_changes;    /// the number of state changes (/2 for off=>on; /2 + 1 for on=>off)
    SemaphoreHandle_t mutex;   /// synchronization mutex
};

esp_err_t relay_open(const gpio_num_t gpio_num, relay_t *relay_p)
{
    esp_err_t ret = ESP_FAIL;
    ESP_GOTO_ON_FALSE(relay_p != NULL, ESP_ERR_INVALID_ARG, handle_error, TAG, "null");
    const relay_t relay = calloc(1, sizeof(struct relay_s));
    ESP_GOTO_ON_FALSE(relay != NULL, ESP_ERR_NO_MEM, handle_error, TAG, "malloc relay");
    relay->gpio_num = gpio_num;
    relay->mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(relay->mutex != NULL, ESP_ERR_NO_MEM, free_relay, TAG,
                      "create semaphore on GPIO %d", gpio_num);
    ESP_GOTO_ON_ERROR(gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT), free_mutex, TAG,
                      "set direction on GPIO %d", gpio_num);
    ESP_GOTO_ON_ERROR(gpio_set_level(gpio_num, RELAY_OFF), reset_gpio, TAG,
                      "set level to 0 on GPIO %d", gpio_num);

    relay->timestamp = esp_timer_get_time();
    *relay_p = relay;
    ESP_LOGI(TAG, "Opened (GPIO: %d)", gpio_num);
    return ESP_OK;
reset_gpio:
    gpio_set_direction(gpio_num, GPIO_MODE_DISABLE);
free_mutex:
    vSemaphoreDelete(relay->mutex);
free_relay:
    free(relay);
handle_error:
    return ret;
}

esp_err_t relay_close(const relay_t relay)
{
    ESP_RETURN_ON_FALSE(relay, ESP_ERR_INVALID_ARG, TAG, "relay must not be NULL");
    const gpio_num_t gpio_num = relay->gpio_num;
    ESP_RETURN_ON_FALSE(xSemaphoreTake(relay->mutex, MUTEX_TIMEOUT), ESP_ERR_TIMEOUT, TAG, "acquire mutex");
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(gpio_num, 0));
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_direction(gpio_num, GPIO_MODE_DISABLE));
    vSemaphoreDelete(relay->mutex);
    free(relay);
    ESP_LOGI(TAG, "Closed (GPIO: %d)", gpio_num);
    return ESP_OK;
}

esp_err_t relay_set_state(const relay_t relay, relay_state_t state)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(relay, ESP_ERR_INVALID_ARG, TAG, "relay must not be NULL");
    ESP_RETURN_ON_FALSE(state == 0 || state == 1, ESP_ERR_INVALID_ARG, TAG, "invalid state %d", state);
    ESP_RETURN_ON_FALSE(xSemaphoreTake(relay->mutex, MUTEX_TIMEOUT), ESP_ERR_TIMEOUT, TAG, "acquire mutex");
    ESP_GOTO_ON_FALSE(relay->state != state, ESP_ERR_INVALID_STATE, release_mutex, TAG,
                      "state already set to %d", state);
    ESP_GOTO_ON_ERROR(gpio_set_level(relay->gpio_num, state), release_mutex, TAG, "set state to %d", state);
    const int64_t now = esp_timer_get_time();
    relay->time_in_state[relay->state] += now - relay->timestamp;
    relay->timestamp = now;
    relay->state = state;
    relay->state_changes++;
    ESP_LOGI(TAG, "Set state to %d (GPIO:%d)", state, relay->gpio_num);
release_mutex:
    ESP_RETURN_ON_FALSE(xSemaphoreGive(relay->mutex), ESP_ERR_NOT_FINISHED, TAG, "release mutex");
    return ret;
}

relay_state_t relay_get_state(const relay_t relay)
{
    ESP_RETURN_ON_FALSE(relay, -1, TAG, "relay must not be NULL");
    return relay->state;
}

esp_err_t relay_get_data(const relay_t relay, relay_data_t *data)
{
    ESP_RETURN_ON_FALSE(relay, ESP_ERR_INVALID_ARG, TAG, "relay must not be NULL");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(relay->mutex, MUTEX_TIMEOUT), ESP_ERR_TIMEOUT, TAG, "acquire mutex");
    data->current_state = relay->state;
    data->time_in_current_state = esp_timer_get_time() - relay->timestamp;
    memcpy(data->time_in_state, relay->time_in_state, 2 * sizeof(uint64_t));
    data->state_changes = relay->state_changes;
    ESP_RETURN_ON_FALSE(xSemaphoreGive(relay->mutex), ESP_ERR_NOT_FINISHED, TAG, "release mutex");
    return ESP_OK;
}

uint32_t relay_get_total_state_changes(const relay_data_t *data, const relay_state_t state)
{
    ESP_RETURN_ON_FALSE(data, 0, TAG, "NULL data");
    ESP_RETURN_ON_FALSE(state == 0 || state == 1, 0, TAG, "invalid state %d", state);
    const uint32_t state_changes = data->state_changes / 2;
    return state == RELAY_OFF ? state_changes + 1 : state_changes;
}

uint64_t relay_get_total_time_in_state(const relay_data_t *data, const relay_state_t state)
{
    ESP_RETURN_ON_FALSE(data, 0, TAG, "NULL data");
    ESP_RETURN_ON_FALSE(state == 0 || state == 1, 0, TAG, "invalid state %d", state);
    const uint64_t t = data->time_in_state[state];
    return data->current_state == state ? t + data->time_in_current_state : t;
}

uint64_t relay_get_average_time_in_state(const relay_data_t *data, const relay_state_t state)
{
    ESP_RETURN_ON_FALSE(data, 0, TAG, "NULL data");
    const uint32_t state_changes = relay_get_total_state_changes(data, state);
    return state_changes ? relay_get_total_time_in_state(data, state) / state_changes : 0;
}

float relay_get_fraction_of_time_in_state(const relay_data_t *data, const relay_state_t state)
{
    ESP_RETURN_ON_FALSE(data, 0, TAG, "NULL data");
    ESP_RETURN_ON_FALSE(state == 0 || state == 1, 0, TAG, "invalid state %d", state);
    const uint64_t t[2] = {relay_get_total_time_in_state(data, RELAY_OFF),
                           relay_get_total_time_in_state(data, RELAY_ON)};
    return (float)t[state] / (t[RELAY_OFF] + t[RELAY_ON]);
}
