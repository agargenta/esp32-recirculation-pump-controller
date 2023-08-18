#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <ds18x20.h>

#include "temperature_delta_sensor.h"

#define MUTEX_TIMEOUT pdMS_TO_TICKS(3) // max time to wait for a lock (3 ms)

static const char *TAG = "temperature_delta_sensor";

struct temperature_delta_sensor_s
{
    temperature_delta_sensor_config_t config; /// the config used to open this device
    ds18x20_addr_t sensors[2];
    temperature_delta_sensor_data_t data;
    TaskHandle_t task; /// internal task for reading from the sensor
    SemaphoreHandle_t mutex;
};

static void update_info(temperature_delta_sensor_info_t *info, uint32_t readings, float value)
{
    info->latest = value;
    info->min = readings ? fmin(info->min, value) : value;
    info->max = readings ? fmax(info->max, value) : value;
    info->average = info->average + (value - info->average) / (readings + 1);
}

esp_err_t temperature_delta_sensor_read(temperature_delta_sensor_t sensor)
{
    float temps[3];
    // this will block internally for ~750ms
    esp_err_t err = ds18x20_measure_and_read_multi(sensor->config.gpio_num, sensor->sensors, 2, temps);
    ESP_RETURN_ON_FALSE(xSemaphoreTake(sensor->mutex, MUTEX_TIMEOUT), ESP_ERR_TIMEOUT, TAG, "acquire mutex");
    if (err == ESP_OK)
    {
        temps[2] = fabs(temps[0] - temps[1]);
        for (int i = 0; i < 3; i++)
        {
            update_info(&sensor->data.info[i], sensor->data.readings, temps[i]);
        }
        sensor->data.readings++;
        sensor->data.latest_reading_timestamp = esp_timer_get_time();

        ESP_LOGD(TAG, "Completed reading %lu on GPIO %d: latest=%.3f/%.3f/%.3f, min=%.3f/%.3f/%.3f"
                      ", max=%.3f/%.3f/%.3f, average=%.3f/%.3f/%.3f",
                 sensor->data.readings, sensor->config.gpio_num,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_FIRST].latest,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_SECOND].latest,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_DELTA].latest,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_FIRST].min,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_SECOND].min,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_DELTA].min,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_FIRST].max,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_SECOND].max,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_DELTA].max,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_FIRST].average,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_SECOND].average,
                 sensor->data.info[TEMPERATURE_DELTA_SENSOR_DELTA].average);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read temperatures on GPIO %d: %d", sensor->config.gpio_num, err);
        sensor->data.faults++;
    }
    ESP_RETURN_ON_FALSE(xSemaphoreGive(sensor->mutex), ESP_ERR_NOT_FINISHED, TAG, "release mutex");
    return err;
}

static void temperature_delta_sensor_task(void *args)
{
    const temperature_delta_sensor_t sensor = (temperature_delta_sensor_t)args;
    const TickType_t delay = pdMS_TO_TICKS(sensor->config.sample_period);
    temperature_delta_sensor_notification_t msg = {
        .temperature_delta_sensor = sensor, .notification_arg = sensor->config.notification_arg};
    TickType_t t;
    while (true)
    {
        t = xTaskGetTickCount();
        if (temperature_delta_sensor_read(sensor) == ESP_OK && sensor->config.notification_queue)
        {
            msg.delta = sensor->data.info[TEMPERATURE_DELTA_SENSOR_DELTA].latest;
            const BaseType_t r = xQueueSendToBack(sensor->config.notification_queue,
                                                  (void *)&msg,
                                                  sensor->config.notification_timeout);
            if (r != pdTRUE)
            {
                ESP_LOGW(TAG, "Notification timeout on GPIO %d queue: %d", sensor->config.gpio_num, r);
            }
        }
        t = xTaskGetTickCount() - t;
        if (delay > t)
        {
            vTaskDelay(delay - t);
        }
    }
}

esp_err_t temperature_delta_sensor_open(const temperature_delta_sensor_config_t *config,
                                        temperature_delta_sensor_t *sensor_out)
{
    esp_err_t ret = ESP_FAIL;
    ESP_GOTO_ON_FALSE(sensor_out != NULL, ESP_ERR_INVALID_ARG, handle_error, TAG, "null");
    ESP_GOTO_ON_FALSE(config->sample_period == 0 ||
                          config->sample_period > TEMPERATURE_SENSOR_PAIR_SAMPLE_PERIOD_MIN,
                      ESP_ERR_INVALID_ARG, handle_error, TAG, "invalid sample_period");

    const temperature_delta_sensor_t sensor = calloc(1, sizeof(struct temperature_delta_sensor_s));
    ESP_GOTO_ON_FALSE(sensor != NULL, ESP_ERR_NO_MEM, handle_error, TAG, "malloc sensor");
    sensor->config = *config;

    size_t sensor_count = 0;
    ESP_GOTO_ON_ERROR(ds18x20_scan_devices(config->gpio_num, sensor->sensors, 2, &sensor_count),
                      free_sensor, TAG, "scanning for temperature sensors on GPIO %d", config->gpio_num);
    ESP_GOTO_ON_FALSE(sensor_count == 2, ESP_ERR_INVALID_STATE, free_sensor, TAG,
                      "expecting a pair of temperature sensors on GPIO %d, but got %d",
                      config->gpio_num, sensor_count);

    sensor->mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(sensor->mutex, ESP_ERR_NO_MEM, free_sensor, TAG,
                      "create mutex on GPIO %d", config->gpio_num);
    char name[64];
    snprintf(name, sizeof(name), "temperature sensor pair task on GPIO %d", config->gpio_num);
    const BaseType_t r = xTaskCreate(temperature_delta_sensor_task, name, 3072,
                                     (void *)sensor, 1, &(sensor->task));
    ESP_GOTO_ON_FALSE(r == pdPASS, ESP_ERR_NO_MEM, free_mutex, TAG, "create task: %d", r);
    *sensor_out = sensor;
    ESP_LOGI(TAG, "Opened on GPIO %d", config->gpio_num);
    return ESP_OK;
free_mutex:
    vSemaphoreDelete(sensor->mutex);
free_sensor:
    free(sensor);
handle_error:
    return ret;
}

esp_err_t temperature_delta_sensor_close(temperature_delta_sensor_t sensor)
{
    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_INVALID_ARG, TAG, "sensor must not be NULL");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(sensor->mutex, MUTEX_TIMEOUT), ESP_ERR_TIMEOUT, TAG, "acquire mutex");
    const gpio_num_t gpio_num = sensor->config.gpio_num;
    vTaskDelete(sensor->task);
    vSemaphoreDelete(sensor->mutex);
    free(sensor);
    ESP_LOGI(TAG, "Closed on GPIO %d", gpio_num);
    return ESP_OK;
}

esp_err_t temperature_delta_sensor_reset(temperature_delta_sensor_t sensor)
{
    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_INVALID_ARG, TAG, "sensor must not be NULL");
    sensor->data = (const temperature_delta_sensor_data_t){0};
    return ESP_OK;
}

esp_err_t temperature_delta_sensor_get_data(temperature_delta_sensor_t sensor,
                                            temperature_delta_sensor_data_t *data)
{
    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_INVALID_ARG, TAG, "sensor must not be NULL");
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "data must not be NULL");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(sensor->mutex, MUTEX_TIMEOUT), ESP_ERR_TIMEOUT, TAG, "acquire mutex");
    *data = sensor->data;
    ESP_RETURN_ON_FALSE(xSemaphoreGive(sensor->mutex), ESP_ERR_NOT_FINISHED, TAG, "release mutex");
    return ESP_OK;
}
