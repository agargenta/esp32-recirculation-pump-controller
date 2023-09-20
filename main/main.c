#include <sys/param.h>
#include <assert.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
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

#define MAX_TEMP_DIFF 6                 //  (°C)
#define MIN_TEMP_DIFF 3                 //  (°C)
#define MIN_PUMP_OFF_DURATION 180000000 //  3 minutes
#define MIN_PUMP_ON_DURATION 60000000   //  1 minute
#define MAX_PUMP_ON_DURATION 600000000  // 10 minutes
typedef enum
{
    FLOW_STARTED,
    TEMPERATURE_MEASURED,
    PUMP_TIMEOUT,
} pump_pump_control_message_type_t;
typedef struct
{
    pump_pump_control_message_type_t type;
    float temperature_delta;
} pump_control_message_t;

static const char *TAG = "main";

static QueueHandle_t flow_reporting_queue;
static QueueHandle_t temperature_reporting_queue;
static QueueHandle_t pump_control_queue;

static pulse_sensor_t s_flow_sensor;
static temperature_delta_sensor_t s_temperature_delta_sensor;
static relay_t s_relay;
static httpd_handle_t s_httpd;

static void flow_reporting_task_handler(void *args)
{
    pulse_sensor_notification_t pulse_sensor_notification;
    const pump_control_message_t ctrl_msg = {.type = FLOW_STARTED};

    while (true)
    {
        if (!xQueueReceive(flow_reporting_queue, &pulse_sensor_notification, portMAX_DELAY))
        {
            ESP_LOGW(TAG, "Pulse sensor notification receive timeout. Bailing out.");
            break;
        }
        switch (pulse_sensor_notification.type)
        {
        case PULSE_SENSOR_CYCLE_STARTED:
            if (!xQueueSend(pump_control_queue, (void *)&(ctrl_msg), pdMS_TO_TICKS(10)))
            {
                ESP_LOGW(TAG, "Flow started message send timeout. Ignoring.");
            }
            break;
        default:
            break;
        }
    }
}

static void temperature_reporting_task_handler(void *args)
{
    temperature_delta_sensor_notification_t temperature_delta_sensor_notification;
    pump_control_message_t ctrl_msg = {.type = TEMPERATURE_MEASURED};

    while (true)
    {
        if (!xQueueReceive(temperature_reporting_queue, &temperature_delta_sensor_notification, portMAX_DELAY))
        {
            ESP_LOGW(TAG, "Temperature sensor notification receive timeout. Bailing out.");
            break;
        }
        ctrl_msg.temperature_delta = temperature_delta_sensor_notification.delta;
        if (!xQueueSend(pump_control_queue, (void *)&(ctrl_msg), pdMS_TO_TICKS(10)))
        {
            ESP_LOGW(TAG, "Temperature measured message send timeout. Ignoring.");
        }
    }
}

static void pump_timeout_handler(void *args)
{
    const pump_control_message_t ctrl_msg = {.type = PUMP_TIMEOUT};
    if (!xQueueSend(pump_control_queue, (void *)&(ctrl_msg), pdMS_TO_TICKS(10)))
    {
        ESP_LOGW(TAG, "Timer tick message send timeout. Ignoring");
    }
}

static void pump_control_task_handler(void *args)
{
    esp_timer_handle_t timeout_timer;
    pump_control_message_t msg;
    temperature_delta_sensor_data_t t_data;
    relay_data_t r_data;

    const esp_timer_create_args_t timer_args = {.callback = &pump_timeout_handler,
                                                .name = "Pump timeout timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timeout_timer));

    while (true)
    {
        if (!xQueueReceive(pump_control_queue, &msg, portMAX_DELAY))
        {
            ESP_LOGW(TAG, "Pump control message receive timeout. Bailing out.");
            break;
        }
        ESP_LOGI(TAG, "Pump control got message %d", msg.type);
        switch (msg.type)
        {
        case FLOW_STARTED:
            ESP_ERROR_CHECK(temperature_delta_sensor_get_data(s_temperature_delta_sensor, &t_data));
            ESP_ERROR_CHECK(relay_get_data(s_relay, &r_data));
            if (t_data.info[TEMPERATURE_DELTA_SENSOR_DELTA].latest >= MAX_TEMP_DIFF &&
                r_data.current_state == RELAY_OFF &&
                (r_data.time_in_current_state >= MIN_PUMP_OFF_DURATION || !r_data.state_changes))
            {
                ESP_LOGI(TAG, "Turning pump ON");
                ESP_ERROR_CHECK(esp_timer_start_once(timeout_timer, MAX_PUMP_ON_DURATION));
                ESP_ERROR_CHECK(relay_set_state(s_relay, RELAY_ON));
            }
            break;
        case TEMPERATURE_MEASURED:
            if (msg.temperature_delta <= MIN_TEMP_DIFF)
            {
                ESP_ERROR_CHECK(relay_get_data(s_relay, &r_data));
                if (r_data.current_state == RELAY_ON &&
                    r_data.time_in_current_state >= MIN_PUMP_ON_DURATION)
                {
                    ESP_LOGI(TAG, "Turning pump OFF (temperature reached)");
                    ESP_ERROR_CHECK(relay_set_state(s_relay, RELAY_OFF));
                    ESP_ERROR_CHECK(esp_timer_stop(timeout_timer));
                }
            }
            break;
        case PUMP_TIMEOUT:
            ESP_LOGI(TAG, "Turning pump OFF (timeout)");
            ESP_ERROR_CHECK(relay_set_state(s_relay, RELAY_OFF));
            break;
        default:
            ESP_LOGW(TAG, "Unexpected pump control message type: %d. Ignoring", msg.type);
            break;
        }
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_delete(timeout_timer));
}

static void wifi_connect_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_open((httpd_context_t *)event_data, &s_httpd));
}

static void wifi_disconnect_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_close(s_httpd));
    s_httpd = NULL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing...");
    flow_reporting_queue = xQueueCreate(8, sizeof(pulse_sensor_notification_t));
    ESP_ERROR_CHECK(flow_reporting_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(xTaskCreate(&flow_reporting_task_handler, "Flow reporting task",
                                2048, NULL, 1, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    temperature_reporting_queue = xQueueCreate(8, sizeof(temperature_delta_sensor_notification_t));
    ESP_ERROR_CHECK(temperature_reporting_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(xTaskCreate(&temperature_reporting_task_handler, "Temperature reporting task",
                                2048, NULL, 1, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    pump_control_queue = xQueueCreate(16, sizeof(pump_control_message_t));
    ESP_ERROR_CHECK(pump_control_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    ESP_ERROR_CHECK(xTaskCreate(&pump_control_task_handler, "Pump control task",
                                2048, NULL, 1, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    pulse_sensor_config_t pulse_sensor_config = PULSE_SENSOR_CONFIG_DEFAULT();
    pulse_sensor_config.gpio_num = CONFIG_FLOW_METER_SENSOR_GPIO;
    pulse_sensor_config.min_cycle_pulses = CONFIG_FLOW_METER_SENSOR_MIN_PULSES;
    pulse_sensor_config.notification_queue = flow_reporting_queue;
    ESP_ERROR_CHECK(pulse_sensor_open(&pulse_sensor_config, &s_flow_sensor));

    temperature_delta_sensor_config_t temperature_delta_sensor_config =
        TEMPERATURE_SENSOR_PAIR_CONFIG_DEFAULT();
    temperature_delta_sensor_config.gpio_num = CONFIG_TEMPERATURE_SENSORS_GPIO;
    temperature_delta_sensor_config.sample_period = CONFIG_TEMPERATURE_SENSORS_SAMPLE_PERIOD;
    temperature_delta_sensor_config.notification_queue = temperature_reporting_queue;

    ESP_ERROR_CHECK(temperature_delta_sensor_open(&temperature_delta_sensor_config,
                                                  &s_temperature_delta_sensor));

    ESP_ERROR_CHECK(relay_open(CONFIG_RELAY_GPIO, &s_relay));

    static httpd_context_t httpd_context;
    httpd_context.temperature_delta_sensor = s_temperature_delta_sensor;
    httpd_context.relay = s_relay;
    httpd_context.flow_sensor = s_flow_sensor;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_connect_handler, &httpd_context));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_disconnect_handler, NULL));

    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_open(&httpd_context, &s_httpd));
    ESP_LOGI(TAG, "Done initializing");
}
