#include <esp_err.h>
#include <esp_http_server.h>
#include "temperature_delta_sensor.h"

esp_err_t httpd_temperature_delta_sensor_register_handlers(const httpd_handle_t httpd,
                                                           const temperature_delta_sensor_t sensor);
