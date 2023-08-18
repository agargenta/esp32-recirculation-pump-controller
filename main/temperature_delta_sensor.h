#pragma once

#include <stdint.h>
#include <esp_check.h>
#include <driver/gpio.h>

#define TEMPERATURE_SENSOR_PAIR_SAMPLE_PERIOD_MIN 750

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        gpio_num_t gpio_num;              /// GPIO number for this sensor (*required)
        uint64_t sample_period;           /// Time (in milliseconds) between temperature sensor readings. Cannot be less than 750ms.
        TickType_t notification_timeout;  /// max time (in ticks) to wait to send a notification (defaults to 1 ms)
        QueueHandle_t notification_queue; /// notification queue to which to send events when the latest readings are available.
        void *notification_arg;           /// an argument to pass in each notification message (optional)
    } temperature_delta_sensor_config_t;

#define TEMPERATURE_SENSOR_PAIR_CONFIG_DEFAULT()  \
    {                                             \
        .sample_period = 10000,                   \
        .notification_timeout = pdMS_TO_TICKS(1), \
    }

    typedef struct temperature_delta_sensor_s *temperature_delta_sensor_t;

    typedef struct
    {
        float delta;                                         /// the latest absolute delta (in C)
        temperature_delta_sensor_t temperature_delta_sensor; /// the temperature delta sensor
        void *notification_arg;                              /// the configured argument
    } temperature_delta_sensor_notification_t;

    typedef struct
    {
        float latest;  /// the latest temperature (in C)
        float min;     /// the minimum temperature (in C)
        float max;     /// the maximum temperature (in C)
        float average; /// the average temperature (in C)
    } temperature_delta_sensor_info_t;

    typedef enum
    {
        TEMPERATURE_DELTA_SENSOR_FIRST = 0,
        TEMPERATURE_DELTA_SENSOR_SECOND = 1,
        TEMPERATURE_DELTA_SENSOR_DELTA = 2
    } temperature_delta_sensor_position_t;

    typedef struct
    {
        temperature_delta_sensor_info_t info[3]; /// temperature sensor info
        uint32_t readings;                       /// the number of (successful) readings
        uint32_t faults;                         /// the number of (unsuccessful) readings
        uint64_t latest_reading_timestamp;       /// microseconds since boot of the latest reading
    } temperature_delta_sensor_data_t;

    esp_err_t temperature_delta_sensor_open(const temperature_delta_sensor_config_t *config,
                                            temperature_delta_sensor_t *sensor_p);

    esp_err_t temperature_delta_sensor_close(temperature_delta_sensor_t sensor);

    esp_err_t temperature_delta_sensor_reset(temperature_delta_sensor_t sensor);

    esp_err_t temperature_delta_sensor_get_data(temperature_delta_sensor_t sensor,
                                                temperature_delta_sensor_data_t *data);
#ifdef __cplusplus
}
#endif
