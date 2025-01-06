/*
 * button.h
 *
 *  2024
 *  Author: nemiv
 */

#ifndef MAIN_BUTTON_H_
#define MAIN_BUTTON_H_


#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_check_err.h"


// To ensure reliable operation of the button functionality, proper
// handling must be implemented. In mechanical buttons, contact bounce
// occurs when pressing and releasing, causing multiple transitions.
// This means the program will process all triggered interrupts,
// regardless of whether they are generated on the edge or level. There
// are many ways to solve this problem, and one of the simplest is to
// disable interrupt handling until the contact bouncing subsides, which
// is the solution used in this case.

// structure that describes button gpio
typedef struct {
    gpio_num_t gpio_num;                // gpio number for button
    esp_timer_handle_t glitching_timer; // callback for timer

} button_gpio_t;


// structure that describes button configuration
typedef struct {
    gpio_num_t gpio_num;                        // gpio id
    uint16_t short_button_press_period_ms;      // amount of ms for short button press period
    uint16_t medium_button_press_period_ms;     // amount of ms for medium button press period
    uint16_t long_button_press_period_ms;       // amount of ms for long button press period
    void (*on_short_button_press_cb)(void);     // callback for short button press period
    void (*on_medium_button_press_cb)(void);    // callback for medium button press period
    void (*on_long_button_press_cb)(void);      // callback for long button press period

} button_cnfg_t;

const char* g_tag_butt = "BUTT";    // tag used in ESP_CHECK

button_cnfg_t g_button_cnfg;        // button configuration
button_gpio_t g_button_gpio = {};   // button gpio
int64_t g_button_pressed_time;      // time point when button was pressed
int64_t g_button_released_time;     // time point when button was released


esp_err_t button_init(button_cnfg_t button_cnfg);
esp_err_t button_deinit();
static void glitching_timer_cb(void* arg);
static void IRAM_ATTR gpio_isr_handler(void* arg);


// inits button configuration and sets up gpio, interrupt, and timer for debouncing
// the button pin is configured to trigger interrupts on any edge
esp_err_t button_init(button_cnfg_t button_cnfg)
{
    esp_err_t init_status = ESP_OK; // TODO

    g_button_cnfg = button_cnfg;

    gpio_config_t gpio_button_cnfg = {};
    gpio_button_cnfg.pin_bit_mask = (1ULL << g_button_cnfg.gpio_num); // set the gpio pin mask for the button
    gpio_button_cnfg.mode = GPIO_MODE_INPUT;                // configure the pin as input
    gpio_button_cnfg.pull_up_en = GPIO_PULLUP_DISABLE;      // low level on release
    gpio_button_cnfg.pull_down_en = GPIO_PULLDOWN_ENABLE;   // high level on press
    gpio_button_cnfg.intr_type = GPIO_INTR_ANYEDGE;         // enable interrupt on both edges

    // configure gpio button pin, enable interrupt, enable deep sleep wakeup on high level
    ESP_CHECK(gpio_config(&gpio_button_cnfg), g_tag_butt);
    ESP_CHECK(gpio_intr_enable(g_button_cnfg.gpio_num), g_tag_butt);
    ESP_CHECK(gpio_deep_sleep_wakeup_enable(g_button_cnfg.gpio_num, GPIO_INTR_HIGH_LEVEL), g_tag_butt);

    ESP_CHECK(esp_deep_sleep_enable_gpio_wakeup(1ULL << g_button_cnfg.gpio_num, ESP_GPIO_WAKEUP_GPIO_HIGH), g_tag_butt);


    // init button_gpio_t structure for further passing to function
    g_button_gpio.gpio_num = g_button_cnfg.gpio_num;

    // configure the glitching timer for debouncing
    const esp_timer_create_args_t glitching_timer_args = {
        .name = "glitching timer",
        .callback = &glitching_timer_cb,    // callback for timer expiry
        .arg = (void*) &g_button_gpio,      // pass gpio data to the callback
        .skip_unhandled_events = false      // handle all timer events
    };

    // create the timer for debouncing with configured parameters
    ESP_CHECK(esp_timer_create(&glitching_timer_args, &g_button_gpio.glitching_timer), g_tag_butt);

    // install the interrupt handler for the button gpio
    ESP_CHECK(gpio_install_isr_service(0), g_tag_butt);
    ESP_CHECK(gpio_isr_handler_add(g_button_cnfg.gpio_num, gpio_isr_handler, (void*) &g_button_gpio), g_tag_butt);

    return init_status;
}


// deinit the button, including deleting the timer and clearing configuration
esp_err_t button_deinit()
{
    esp_err_t deinit_status = ESP_OK;

    esp_timer_delete(g_button_gpio.glitching_timer); // delete the glitching timer

    return deinit_status;
}


// interrupt service routine handler for the button gpio
// disables interrupts temporarily and starts a debounce timer.
static void gpio_isr_handler(void* arg)
{
    button_gpio_t* gpio = (button_gpio_t*) arg;
    // disable further interrupts for this GPIO pin
    gpio_intr_disable(gpio->gpio_num);
    // start a timer for debounce to prevent handling noisy signals
    esp_timer_start_once(gpio->glitching_timer,  10 * 1000);  // 10 ms
}


// callback for the glitching timer. once the debounce time
// is completed, the function checks the button state and triggers
// the appropriate actions based on the press duration (short, medium, or long).
static void glitching_timer_cb(void* arg)
{
    button_gpio_t* gpio = (button_gpio_t*) arg;
    static int prev_gpio_level = 0;
    int new_gpio_level = gpio_get_level(gpio->gpio_num);

    // re-enable the GPIO interrupt after the debounce period
    gpio_intr_enable(gpio->gpio_num);

    // if the state of the button has changed
    if(prev_gpio_level != new_gpio_level)
    {
        if (new_gpio_level == 1)        // BUTTON IS PRESSED
            g_button_pressed_time = esp_timer_get_time();   // record press time
        else if(new_gpio_level == 0)    // BUTTON IS RELEASED
        {
            g_button_released_time = esp_timer_get_time();  // record release time

            // calculate the duration of the button press
            int64_t button_pressed_period = g_button_released_time - g_button_pressed_time;
            if (button_pressed_period < 0)
                ESP_LOGE(g_tag_butt, "Button pressed period measurement error.");

            ESP_LOGI(g_tag_butt, "Button was pressed for %lld us = %f s", button_pressed_period, button_pressed_period/1000000.0);

            if (button_pressed_period/1000.0 < g_button_cnfg.short_button_press_period_ms)          // SHORT BUTTON PRESS
            {
                ESP_LOGI(g_tag_butt, "Short button pressed period.");
                if (g_button_cnfg.on_short_button_press_cb != NULL)
                    (*g_button_cnfg.on_short_button_press_cb)();
            }
            else if(button_pressed_period/1000.0 >= g_button_cnfg.short_button_press_period_ms &&
                    button_pressed_period/1000.0 <  g_button_cnfg.medium_button_press_period_ms)      // MEDIUM BUTTON PRESS
            {
                ESP_LOGI(g_tag_butt, "Medium button pressed period.");
                if (g_button_cnfg.on_medium_button_press_cb != NULL)
                    (*g_button_cnfg.on_medium_button_press_cb)();
            }
            else if(button_pressed_period/1000.0 >= g_button_cnfg.medium_button_press_period_ms)      // LONG BUTTON PRESS
            {
                ESP_LOGI(g_tag_butt, "Long button pressed period.");
                if (g_button_cnfg.on_long_button_press_cb != NULL)
                    (*g_button_cnfg.on_long_button_press_cb)();
            }
        }

        prev_gpio_level = new_gpio_level;   // update previous gpio state
    }
}


// force an interrupt for the button
void force_interupt()
{
    gpio_isr_handler(&g_button_gpio);   // trigger the ISR handler directly
}


#endif /* MAIN_BUTTON_H_ */
