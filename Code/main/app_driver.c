/* app_driver.c */
#include <sdkconfig.h>
#include <esp_log.h>
#include <driver/ledc.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include "app_priv.h"

static const char *TAG = "app_driver";

/* Global state */
static bool g_power = DEFAULT_POWER;
static uint16_t g_bright_1 = DEFAULT_BRIGHTNESS;
static uint16_t g_bright_2 = DEFAULT_BRIGHTNESS;

/* PWM Configuration */
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY 5000

static void app_pwm_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Channel 0 -> GPIO 14
    ledc_channel_config_t ledc_channel_0 = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_GPIO_1,
        .duty = 0,
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_0));

    // Channel 1 -> GPIO 15
    ledc_channel_config_t ledc_channel_1 = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_GPIO_2,
        .duty = 0,
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_1));
}

/* Helper to write to hardware */
static void update_hardware()
{
    uint32_t duty_1 = 0;
    uint32_t duty_2 = 0;

    duty_1 = (g_bright_1 * 8191) / 100;
    duty_2 = (g_bright_2 * 8191) / 100;

    ESP_LOGI(TAG, "HW Update -> Ch1: %lu | Ch2: %lu", duty_1, duty_2);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, duty_1);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, duty_2);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
}

esp_err_t app_light_set_power(bool power)
{
    g_power = power;
    return ESP_OK;
}

esp_err_t app_light_set_brightness(int channel, int brightness)
{
    if (channel == 1)
    {
        g_bright_1 = brightness;
    }
    else if (channel == 2)
    {
        g_bright_2 = brightness;
    }
    update_hardware();
    return ESP_OK;
}

void app_driver_init()
{
    app_pwm_init();
    update_hardware();
}