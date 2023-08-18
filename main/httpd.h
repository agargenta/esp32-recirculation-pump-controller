#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include "relay.h"
#include "temperature_delta_sensor.h"
#include "pulse_sensor.h"

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        temperature_delta_sensor_t temperature_delta_sensor;
        relay_t relay;
        pulse_sensor_t flow_sensor;
    } httpd_context_t;

    esp_err_t httpd_open(const httpd_context_t *context, httpd_handle_t *httpd_out);
    esp_err_t httpd_close(const httpd_handle_t httpd);
#ifdef __cplusplus
}
#endif
