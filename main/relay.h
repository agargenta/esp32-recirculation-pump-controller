#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct relay_s *relay_t;
    typedef enum
    {
        RELAY_OFF = 0,
        RELAY_ON = 1
    } relay_state_t;

    typedef struct
    {
        relay_state_t current_state;
        uint64_t time_in_current_state; // microseconds in current state
        uint64_t time_in_state[2];
        uint32_t state_changes;
    } relay_data_t;

    esp_err_t relay_open(const gpio_num_t gpio_num, relay_t *relay_out);
    esp_err_t relay_close(const relay_t relay);
    esp_err_t relay_set_state(const relay_t relay, relay_state_t state);
    relay_state_t relay_get_state(const relay_t relay);
    esp_err_t relay_get_data(const relay_t relay, relay_data_t *data);

    uint32_t relay_get_total_state_changes(const relay_data_t *data, const relay_state_t state);
    uint64_t relay_get_total_time_in_state(const relay_data_t *data, const relay_state_t state);
    uint64_t relay_get_average_time_in_state(const relay_data_t *data, const relay_state_t state);
    float relay_get_fraction_of_time_in_state(const relay_data_t *data, const relay_state_t state);

#ifdef __cplusplus
}
#endif
