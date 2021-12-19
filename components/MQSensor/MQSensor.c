#include <stdio.h>
#include "MQSensor.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/adc.h"
//#include "esp_adc_cal.h"
#include "esp_log.h"

#define ADC1_TEST_CHANNEL (0)

//static esp_adc_cal_characteristics_t adc1_chars;

// R0 là điện trở của cảm biến tại 1000ppm lpg trong không khí sạch.
static const int R0 = 20;

static float ratio = 0;
static float RS = 0;
static int sensorValue = 0;
static float volts = 0;

static float ppm_co = 0;
static float ppm_ch4 = 0;
static float ppm_lpg = 0;
/*
    This library reference from https://www.teachmemicro.com/mq-135-air-quality-sensor-tutorial/
    and Datasheet
    Rs\RL = (Vc-VRL) / VRL
    -> Rs = Vc*RL/VRL - RL = RL(Vc/VRL - 1) = 10000(3.3/VRL - 1)

    _PPM =  a*ratio^b

    Exponential regression:
    GAS     | a      | b
    LPG     | 1000.5 | -2.186
    CH4     | 4269.6 | -2.648
    CO      | 599.65 | -2.244


    CH4 (125.89, 1.58) (10000, 0.5) => PPMch4 = (RS/R0 - 1.58)*(9874.12/-1.08) + 125.89 = 14571.36 - 9142.7*(RS/R0)

    CO (125.89 , 1.17) (1000, 0.63) => PPMco = (y - y1)*(x2 - x1)/(y2 - y1) + x1
                                    => PPMco = (RS/R0 - 1.17) * (874.11/-0.54) + 125.89 = 2019.8 - 1618.72*RS/R0

    LPG (125.89, 1.26) (10000, 0.15) => PPMpng = (RS/R0 - 1.58)*(9874.12/-1.11) + 125.89 = 14180.94 - 8895.6*RS/R0
*/

void config_mq_sensor (void)
{
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ADC1_TEST_CHANNEL, ADC_ATTEN_11db);
}

void read_mq_data_callback (void)
{
    sensorValue = adc1_get_raw(ADC1_TEST_CHANNEL);
    volts = sensorValue * 3.3;
    volts = volts / 4095;
    RS = 10*(3.6/volts - 1);
    ratio = RS / R0;
    printf("volts: %f, RS: %f, ratio: %f\n", volts, RS, ratio);
}

float get_ppm_ch4 (void)
{
    ppm_ch4 = 4269.6* pow(ratio, -2.648);
    if (ppm_ch4 < 0) ppm_ch4 = 0;
    ESP_LOGI("MQ SENSOR", "CH4: %f ppm", ppm_ch4);
    return ppm_ch4;
}

float get_ppm_co (void)
{

    ppm_co = 599.65* pow(ratio, -2.244);
    if (ppm_co < 0) ppm_co = 0;
    ESP_LOGI("MQ SENSOR", "C0: %f ppm", ppm_co);
    return ppm_co;
}

float get_ppm_lpg (void)
{
    ppm_lpg = 1000.5* pow(ratio, -2.186);
    if (ppm_lpg < 0) ppm_lpg = 0;
    ESP_LOGI("MQ SENSOR", "LPG: %f ppm", ppm_lpg);
    return ppm_lpg;
}
