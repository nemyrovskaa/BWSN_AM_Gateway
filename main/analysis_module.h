/*
 * analysis_module.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_ANALYSIS_MODULE_H_
#define MAIN_ANALYSIS_MODULE_H_


#include <stdio.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_check_err.h"

// maximum possible score for the temperature classification
#define TEMP_MAX_SCORE  3

//variable to store the temperature data, stored in RTC memory
float RTC_DATA_ATTR temp_data;

// enum to define different life rate states
typedef enum {
    UNDEFINED = -1,
    NORMAL = 0,
    CRITICAL = 1,
    VERY_CRITICAL = 2
} liferate_t;


esp_err_t push_temp_data(float temp);
int8_t get_temp_score(float temp);
liferate_t start_analysis();
float convert_temp_data_to_float(uint8_t temp_msb, uint8_t temp_lsb);


// pushes temperature data to store
esp_err_t push_temp_data(float temp)
{
    temp_data = temp;
    return ESP_OK;
}

// calculates the temperature score based on given temperature value
int8_t get_temp_score(float temp)
{
    if (temp > 36.0 && temp <= 38.0)
        return 0;

    if (temp > 35.0 && temp <= 39.0)
        return 1;

    if (temp > 39.0)
        return 2;

    if (temp <= 35.0)
        return 3;

    return -1;
}

// analyses the temperature data and classify it into life rate categories
liferate_t start_analysis()
{
    int8_t critical_meas_score = get_temp_score(temp_data);
    int8_t critical_max_score = TEMP_MAX_SCORE;

    // calculate the result score as a ratio between measured score and maximum score
    float res_score = (float)critical_meas_score/(float)critical_max_score;
    ESP_LOGI("AM", "Temp data: %f", temp_data);
    ESP_LOGI("AM", "Score is:  %f", res_score);
    if(res_score >= 0.0 && res_score < 0.3)
        return NORMAL;
    if(res_score >= 0.3 && res_score < 0.7)
        return CRITICAL;
    if(res_score >= 0.7 && res_score <= 1.0)
        return VERY_CRITICAL;

    return UNDEFINED;
}

// converts raw temperature data (from two bytes) to a float value
float convert_temp_data_to_float(uint8_t temp_msb, uint8_t temp_lsb)
{
    // extract the most significant byte (MSB) and the least significant byte (LSB)
    float ret_val = (float)(temp_msb & 0b01111111);

    // add the fractional part by shifting the LSB and dividing by powers of 2
    for (int i = 0; i < 8; i++)
        ret_val += ((temp_lsb >> (7-i)) & 1) / (float)(2 << i);

    // adjust for the sign based on the MSB (negative if the MSB's sign bit is 1)
    return ret_val * ((temp_msb>>7) & 1 ? -1.0 : 1.0);
}


#endif /* MAIN_ANALYSIS_MODULE_H_ */
