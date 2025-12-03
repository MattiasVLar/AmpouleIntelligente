/* app_priv.h */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_rmaker_core.h>

#define DEFAULT_POWER true
#define DEFAULT_BRIGHTNESS 10

/* Hardware GPIOs */
#define PWM_GPIO_1 14
#define PWM_GPIO_2 15

/* Names for the parameters in the App */
#define PARAM_BRIGHTNESS_1 "Brightness 1"
#define PARAM_BRIGHTNESS_2 "Brightness 2"

void app_driver_init(void);
esp_err_t app_light_set_power(bool power);
esp_err_t app_light_set_brightness(int channel, int brightness);