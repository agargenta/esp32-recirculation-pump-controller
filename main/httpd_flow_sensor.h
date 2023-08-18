#include <esp_err.h>
#include <esp_http_server.h>
#include "pulse_sensor.h"

esp_err_t httpd_flow_sensor_register_handlers(const httpd_handle_t httpd, const pulse_sensor_t sensor);
