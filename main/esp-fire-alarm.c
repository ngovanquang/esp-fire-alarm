#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "DHT22.h"
#include "time_sync.h"
#include "MQSensor.h"

/*

json format

{
    "deviceId":"3e31d3bd-e0a6-4497-8b48-cef5c6f3547b",
    "deviceType":"ESP FIRE ALARM",
    "data": {
        "temperature": 22.4,
        "humidity": 63.8,
        "location":{    
            "latitude":"21.027763",
            "longitude":"105.834160"
        },
        "time":"2021-20-12T00:42:23",
        "ch4":47,
        "co":13,
        "lpg":24
    }
}

*/

static const char *TAG = "MQTT";
static const char *APP_TAG = "ESP_FIRE_ALARM";
static const char *TIME_TAG = "TIME";
static const char *deviceId = "3e31d3bd-e0a6-4497-8b48-cef5c6f3547b";
static const char *deviceType = "ESP FIRE ALARM";
static const char *latitude = "21.027763";
static const char *longitude = "105.834160";
static char strftime_buf[64];
static const int DelayMS = 3000;
static int msg_id;
static bool led_status = 0;

esp_mqtt_client_handle_t mqtt_client = NULL;
TaskHandle_t publishMessageHandle = NULL;
TaskHandle_t dhtTaskHandle = NULL;
TaskHandle_t mqTaskHandle = NULL;
TaskHandle_t syncTimeHandle = NULL;
TaskHandle_t turnOnWarningHandle = NULL;
TaskHandle_t detectFireHandle = NULL;
QueueHandle_t queue1; // store dht22 data
QueueHandle_t queue2; // store gas sensor data
/**
 * @brief sync time using Network time server
 * 
 * @param args 
 */
static void sync_time(void *args)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
        localtime_r(&now, &timeinfo);
        // Is time set? If not, tm_year will be (1970 -1900)
        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGI(TIME_TAG, "Time is not set yet. Connecting to Wifi and getting time over NTP");
            obtain_time();
            // update 'now' variable with current time
            time(&now);
        }
    while (1)
    {
        // set timezone to Easten standrd time
        setenv("TZ", "CST-7", 1);
        tzset();
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%d-%mT%H:%M:%S", &timeinfo);
        ESP_LOGI(TIME_TAG, "The current date/time in Hanoi is: %s", strftime_buf);
        vTaskDelay(DelayMS / portTICK_PERIOD_MS);
    }
    
}

static void read_mq_data(void* args)
{
    char txbuff[50];
    queue2 = xQueueCreate(5, sizeof(txbuff));
    if (queue2 == 0)
    {
        ESP_LOGW("QUEUE", "failed to create queue2 = %p", queue2);
    }
    config_mq_sensor();
    while (1)
    {
        read_mq_data_callback();
        sprintf(txbuff, "\"ch4\":%.f,\"co\":%.f,\"lpg\":%.f", get_ppm_ch4(), get_ppm_co(), get_ppm_lpg());
        if (get_ppm_ch4() > 5000) vTaskResume(turnOnWarningHandle);
        if (xQueueSend(queue2, (void*)txbuff, (TickType_t)0) != 1)
        {
            printf("could not sended this message = %s \n", txbuff);
        }
        vTaskDelay(pdMS_TO_TICKS(DelayMS));
    }
    
}

static void recv_dht22_data(void* arg)
{
    char txbuff[50];
    queue1 = xQueueCreate(5, sizeof(txbuff));
    if (queue1 == 0)
    {
        ESP_LOGW("QUEUE", "failed to create queue1 = %p", queue1);
    }

    setDHTgpio(4);
    int ret = 0;
    while (1)
    {
        ret = readDHT();
        errorHandler(ret);
        sprintf(txbuff, "\"temperature\": %.1f,\"humidity\": %.1f", getTemperature(), getHumidity());
        
        if (xQueueSend(queue1, (void*)txbuff, (TickType_t)0) != 1)
        {
            printf("could not sended this message = %s \n", txbuff);
        }
        vTaskDelay(pdMS_TO_TICKS(DelayMS));
    }
    
}

/**
 * @brief ham thuc hien chuc nang canh bao
 * 
 */
static void turn_on_warning_task(void) {
    int cnt = 0;
    vTaskSuspend(NULL);
    while(1) {
        cnt++;
        if (cnt/2 == 10){
            cnt = 0;
            gpio_set_level(2, 0);
            gpio_set_level(5, 0);
            gpio_set_level(18, 0);
            vTaskSuspend(turnOnWarningHandle);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        led_status = !led_status;
        gpio_set_level(2, led_status);
        gpio_set_level(5, led_status);
        gpio_set_level(18, 1);
    }
}

/**
 * @brief phát hiện lửa cháy
 * 
 */
static void detect_fire_task(void) {
    vTaskSuspend(NULL);
    while(1) {
        if(gpio_get_level(19) == 0) {
            printf("fire is detecting...\n");
            vTaskResume(turnOnWarningHandle);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief xu ly data nhan duoc tu subscribe
 * 
 *
*/
static void process_msg_from_subscribe (char *msg, int data_len) {
    char str[data_len];
    strncpy(str, msg, data_len);
    str[data_len] = '\0';
    if (strcmp(str,"off") == 0) {
        printf("%s\n", str);
        printf("led is off\n");
        gpio_set_level(2, 0);
    } else if (strcmp(str,"on") == 0) {
        vTaskResume(turnOnWarningHandle);
    }

}

/**
 * @brief Publish message to broker
 * 
 * @param args 
 */
void publish_message_task(char *args)
{
    char rxbuff1[50];
    char rxbuff2[50];
    char buff[1024];
    while (1)
    {
        
        if (xQueueReceive(queue1, &(rxbuff1), (TickType_t)5))
        {
            printf("got a data from queue1 === %s \n", rxbuff1);
        }
        if (xQueueReceive(queue2, &(rxbuff2), (TickType_t)5))
        {
            printf("got a data from queue2 === %s \n", rxbuff2);
        }
        sprintf(buff, "{\"deviceId\":\"%s\",\"deviceType\":\"%s\",\"data\":{%s,\"location\":{\"latitude\":\"%s\",\"longitude\":\"%s\"},\"time\":\"%s\",%s}}", deviceId, deviceType, rxbuff1, latitude, longitude, strftime_buf, rxbuff2);
        msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/firealarm/01", buff, 0, 1, 0);
        vTaskDelay(DelayMS / portTICK_PERIOD_MS);
    }
}

/*
* log function from mqtt error
*/
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}
/* handler event from mqtt
*
*
*/
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(mqtt_client, "/topic/firealarm/command/01", 2);
        vTaskResume(publishMessageHandle);
        vTaskResume(dhtTaskHandle);
        vTaskResume(detectFireHandle);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        vTaskSuspend(publishMessageHandle);
        vTaskSuspend(dhtTaskHandle);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        process_msg_from_subscribe(event->data, event->data_len);
        memset(event->data, 0, event->data_len);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if(event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event_id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    // The last argument may be used to pass data to the event handler, in this example mqtt_event_handler
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void app_main(void)
{
    ESP_LOGI(APP_TAG, "[APP] Startup...");
    ESP_LOGI(APP_TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(APP_TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    gpio_set_direction(2, GPIO_MODE_OUTPUT);
    gpio_set_direction(5, GPIO_MODE_OUTPUT);
    gpio_set_direction(18, GPIO_MODE_OUTPUT);
    gpio_set_direction(19, GPIO_MODE_INPUT);
    // Connect to wifi
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();

    xTaskCreate(sync_time, "sync time", 4096, NULL, 10, &syncTimeHandle);
    xTaskCreate(recv_dht22_data, "dht data task", 4096, NULL, 10, &dhtTaskHandle);
    xTaskCreate(publish_message_task, "publish message", 4096, NULL, 10, &publishMessageHandle);
    xTaskCreate(read_mq_data, "read mq data", 2048, NULL, 10, &mqTaskHandle);
    xTaskCreate(turn_on_warning_task, "turn on warning", 2048, NULL, 10, &turnOnWarningHandle);
    xTaskCreate(detect_fire_task, "detect fire", 1024, NULL, 10, &detectFireHandle);
}
