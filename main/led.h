/*
 * led.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_LED_H_
#define MAIN_LED_H_


#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "esp_check_err.h"
#include "task_priorities_rtos.h"

#define GPIO_LED_ON 0   // define active level for led - 0 means the led is ON
#define GPIO_LED_OFF 1  // define inactive level for led - 1 means the led is OFF


// structure to store blink intervals for the led (on and off time in ms)
typedef struct {
    uint16_t blink_on_itv;
    uint16_t blink_off_itv;

} blink_itvs_t;

const char* g_tag_led = "LED";    // tag used in ESP_CHECK

uint8_t g_gpio_led_num;                 // gpio number for the led
bool led_is_initialised = false;        // flag to check if the led has been inited
TaskHandle_t blink_loop_hndl = NULL;    // handle for the blink task
blink_itvs_t blink_itvs;                // structure holding blink intervals

esp_err_t led_init(uint8_t gpio_led_num);
esp_err_t led_deinit();
esp_err_t led_turn_on();
esp_err_t led_turn_off();
esp_err_t led_start_blink(uint16_t blink_on_itv, uint16_t blink_off_itv);
esp_err_t led_stop_blink();
void blink_loop(void* arg);


// inits the led by configuring its gpio pin
esp_err_t led_init(uint8_t gpio_led_num)
{
    if (led_is_initialised) // check if led is already initialised
        return ESP_FAIL;

    led_is_initialised = true;      // mark led as initialised
    g_gpio_led_num = gpio_led_num;  // set gpio pin number for led

    // reset and configure the gpio pin for output mode
    ESP_CHECK(gpio_reset_pin(g_gpio_led_num), g_tag_led);
    ESP_CHECK(gpio_set_direction(g_gpio_led_num, GPIO_MODE_OUTPUT), g_tag_led);

    return ESP_OK;
}


// deinits the led by resetting its gpio pin
esp_err_t led_deinit()
{
    if (!led_is_initialised)     // check if led was not initialised
        return ESP_FAIL;

    // if a blink task is running, stop it before deiniting
    if (blink_loop_hndl != NULL)
        led_stop_blink();

    led_is_initialised = false;     // mark led as deinitialised
    ESP_CHECK(gpio_reset_pin(g_gpio_led_num), g_tag_led);   // reset the gpio pin
    return ESP_OK;
}

// turns the led on
esp_err_t led_turn_on()
{
    if (!led_is_initialised)     // check if led was not initialised
        return ESP_FAIL;

    if (blink_loop_hndl != NULL) // stop blinking if it's currently running
        led_stop_blink();

    // set the led gpio level to ON (active level)
    ESP_CHECK(gpio_set_level(g_gpio_led_num, GPIO_LED_ON), g_tag_led);
    return ESP_OK;
}

// turns the led off
esp_err_t led_turn_off()
{
    if (!led_is_initialised)     // check if led was not initialised
        return ESP_FAIL;

    if (blink_loop_hndl != NULL) // stop blinking if it's currently running
        led_stop_blink();

    // set the led gpio level to OFF (inactive level)
    ESP_CHECK(gpio_set_level(g_gpio_led_num, GPIO_LED_OFF), g_tag_led);
    return ESP_OK;
}

// starts blinking the led with specified on and off intervals
esp_err_t led_start_blink(uint16_t blink_on_itv, uint16_t blink_off_itv)    // in ms
{
    if (!led_is_initialised)     // check if led was not initialised
        return ESP_FAIL;

    // store the blink intervals
    blink_itvs.blink_on_itv = blink_on_itv;
    blink_itvs.blink_off_itv = blink_off_itv;

    if (blink_loop_hndl != NULL) // stop blinking if it's currently running
        led_stop_blink();

    // create a new task to handle the blinking loop
    BaseType_t res = xTaskCreate(blink_loop, "blink_loop", 2048, (void*)&blink_itvs, tskIDLE_PRIORITY + LOW_TASK_PRIORITY, &blink_loop_hndl);

    if (res != pdPASS)
        return ESP_FAIL;

    return ESP_OK;
}

// stops the blinking of the led
esp_err_t led_stop_blink()
{
    // check if led is initialised and blink task is running
    if (!led_is_initialised || blink_loop_hndl == NULL)
        return ESP_FAIL;

    // delete the blink task
    vTaskDelete(blink_loop_hndl);
    blink_loop_hndl = NULL; // reset task handle
    return ESP_OK;
}

// handles the blinking loop of the led
void blink_loop(void* arg)
{
    // get the blink intervals from arguments
    blink_itvs_t* blink_itvs = (blink_itvs_t*)arg;
    while (true)
    {
        ESP_CHECK(gpio_set_level(g_gpio_led_num, GPIO_LED_ON), g_tag_led);  // turn the led ON
        vTaskDelay(blink_itvs->blink_on_itv / portTICK_PERIOD_MS);      // wait for the specified ON interval
        ESP_CHECK(gpio_set_level(g_gpio_led_num, GPIO_LED_OFF), g_tag_led); // turn the led OFF
        vTaskDelay(blink_itvs->blink_off_itv / portTICK_PERIOD_MS);     // wait for the specified OFF interval
    }
}


#endif /* MAIN_LED_H_ */
