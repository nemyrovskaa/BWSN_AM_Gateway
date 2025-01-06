/*
 * esp_check_err.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_ESP_CHECK_ERR_H_
#define MAIN_ESP_CHECK_ERR_H_

// helper macro to check and output info message

#ifdef DEBUGGING
#define ESP_CHECK(func, tag) \
    { \
    esp_err_t err = func; \
    if (err != ESP_OK) \
        ESP_LOGE(tag, "%s failed! Error: %s [%d]", #func, esp_err_to_name(err), __LINE__); \
    else \
        ESP_LOGI(tag, "%s succeeded!", #func); \
    }
#else
    #define ESP_CHECK(func, tag) \
        func;
#endif


#endif /* MAIN_ESP_CHECK_ERR_H_ */
