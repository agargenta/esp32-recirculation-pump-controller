/* Async Request Handlers HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>
#include <assert.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_wifi.h>
#include "esp_check.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "driver/gpio.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_tls_crypto.h"
#include "sdkconfig.h"
#include "protocol_examples_common.h"
#include "pulse_sensor.h"
#include "temperature_delta_sensor.h"
#include "relay.h"
#include "httpd.h"

static const char *TAG = "main";

static pulse_sensor_t s_flow_sensor = NULL;
static temperature_delta_sensor_t s_temperature_delta_sensor = NULL;
static relay_t s_relay = NULL;
static httpd_handle_t s_httpd = NULL;

static void connect_handler(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_open((httpd_context_t *)event_data, &s_httpd));
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_close(s_httpd));
    s_httpd = NULL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing...");
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    pulse_sensor_config_t pulse_sensor_config = PULSE_SENSOR_CONFIG_DEFAULT();
    pulse_sensor_config.gpio_num = CONFIG_FLOW_METER_SENSOR_GPIO;
    pulse_sensor_config.min_cycle_pulses = CONFIG_FLOW_METER_SENSOR_MIN_PULSES;
    ESP_ERROR_CHECK(pulse_sensor_open(&pulse_sensor_config, &s_flow_sensor));

    temperature_delta_sensor_config_t temperature_delta_sensor_config =
        TEMPERATURE_SENSOR_PAIR_CONFIG_DEFAULT();
    temperature_delta_sensor_config.gpio_num = CONFIG_TEMPERATURE_SENSORS_GPIO;
    temperature_delta_sensor_config.sample_period = CONFIG_TEMPERATURE_SENSORS_SAMPLE_PERIOD;

    ESP_ERROR_CHECK(temperature_delta_sensor_open(&temperature_delta_sensor_config,
                                                  &s_temperature_delta_sensor));

    ESP_ERROR_CHECK(relay_open(CONFIG_RELAY_GPIO, &s_relay));

    static httpd_context_t httpd_context;
    httpd_context.temperature_delta_sensor = s_temperature_delta_sensor;
    httpd_context.relay = s_relay;
    httpd_context.flow_sensor = s_flow_sensor;

    // for (int i = 0; i < 100; i++)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(4000));
    //     ESP_LOGI(TAG, "Relay %d: on=%llu, off=%llu: %.3f", i,
    //              relay_get_average_time_in_state(relay, RELAY_ON),
    //              relay_get_average_time_in_state(relay, RELAY_OFF),
    //              relay_get_fraction_of_time_in_state(relay, RELAY_ON));
    //     relay_set_state(relay, RELAY_ON);
    //     vTaskDelay(pdMS_TO_TICKS(2000));
    //     relay_set_state(relay, RELAY_OFF);
    // }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &httpd_context));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, NULL));

    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_open(&httpd_context, &s_httpd));
    ESP_LOGI(TAG, "Done initializing");
}
