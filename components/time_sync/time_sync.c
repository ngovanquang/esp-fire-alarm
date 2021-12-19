#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sntp.h"

#include "time_sync.h"


static const char *TAG = "TIME";

void obtain_time(void)
{
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);

}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

// void sync_time_callback(char strftime_buf[64])
// {
//     time_t now;
//     struct tm timeinfo;
    
 
//     time(&now);
//     localtime_r(&now, &timeinfo);
//     // Is time set? If not, tm_year will be (1970 -1900)
//     if (timeinfo.tm_year < (2016 - 1900)) {
//         ESP_LOGI(TAG, "Time is not set yet. Connecting to Wifi and getting time over NTP");
//         obtain_time();
//         // update 'now' variable with current time
//         time(&now);
//     }
    
//     // set timezone to Easten standrd time
//     setenv("TZ", "CST-7", 1);
//     tzset();
//     localtime_r(&now, &timeinfo);
//     strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
//     ESP_LOGI(TAG, "The current date/time in Hanoi is: %s", strftime_buf);
// }