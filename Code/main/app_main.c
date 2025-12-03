/* app_main.c - Dual PWM Light Example */
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "sdkconfig.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_console.h>

#include <app_network.h>
#include <app_insights.h>

#include "app_priv.h"

static const char *TAG = "app_main";

// --- NEW: Store the current state of the light ---
static int g_power = 1;       // 1 = ON, 0 = OFF
static int g_brightness = 10; // 0 to 100
static int g_cct = 50;        // 0 (Warm) to 100 (Cool)
// -------------------------------------------------

esp_rmaker_device_t *light_device;

static void update_led_output()
{
    // If power is OFF, kill both channels
    if (g_power == 0)
    {
        app_light_set_brightness(1, 0);
        app_light_set_brightness(2, 0);
        return;
    }

    // --- The Mixing Math (Linear Blend) ---
    // If CCT is 0 (Warmest): Cool = 0%, Warm = 100% of Brightness
    // If CCT is 100 (Coolest): Cool = 100% of Brightness, Warm = 0%

    int cool_val = (g_brightness * g_cct) / 100;
    int warm_val = (g_brightness * (100 - g_cct)) / 100;

    ESP_LOGI(TAG, "Updating LEDs -> Cool: %d, Warm: %d", cool_val, warm_val);

    app_light_set_brightness(1, cool_val);
    app_light_set_brightness(2, warm_val);
}

/* Callback to handle param updates */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
                          uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx)
    {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }

    for (int i = 0; i < count; i++)
    {
        const esp_rmaker_param_t *param = write_req[i].param;
        esp_rmaker_param_val_t val = write_req[i].val;
        const char *param_name = esp_rmaker_param_get_name(param);

        // 1. Handle Power
        if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0)
        {
            g_power = val.val.b ? 1 : 0;
            app_light_set_power(g_power); // Keep standard power toggle if you have it
            update_led_output();          // Recalculate LEDs
        }
        // 2. Handle Brightness
        else if (strcmp(param_name, PARAM_BRIGHTNESS_1) == 0)
        {
            g_brightness = val.val.i;
            update_led_output();
        }
        // 3. Handle CCT (Warm/Cool Mix)
        else if (strcmp(param_name, ESP_RMAKER_DEF_CCT_NAME) == 0)
        {
            g_cct = val.val.i;
            update_led_output();
        }

        esp_rmaker_param_update(param, val);
    }
    return ESP_OK;
}

void app_main()
{
    /* Initialize Hardware */
    app_driver_init();
    esp_rmaker_console_init();

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialize Network */
    app_network_init();

    /* Initialize RainMaker Node */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Dual PWM Light");
    if (!node)
    {
        abort();
    }

    /* --- Create Device --- */
    light_device = esp_rmaker_lightbulb_device_create("Dual LED", NULL, DEFAULT_POWER);
    esp_rmaker_device_add_bulk_cb(light_device, write_cb, NULL);

    /* --- Add Parameter 1 (Brightness) --- */
    esp_rmaker_param_t *bright1_param = esp_rmaker_brightness_param_create(PARAM_BRIGHTNESS_1, DEFAULT_BRIGHTNESS);
    esp_rmaker_device_add_param(light_device, bright1_param);

    /* --- Add Parameter 2 (Custom Slider) --- */
    // Create standard CCT param with default value (e.g., 2700 Kelvin)
    esp_rmaker_param_t *cct_param = esp_rmaker_cct_param_create(ESP_RMAKER_DEF_CCT_NAME, 2700);
    esp_rmaker_device_add_param(light_device, cct_param);
    esp_rmaker_param_add_bounds(cct_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    // Add the device to the node
    esp_rmaker_node_add_device(node, light_device);

    /* Enable Services */
    esp_rmaker_ota_enable_default();
    esp_rmaker_timezone_service_enable();
    esp_rmaker_schedule_enable();
    esp_rmaker_scenes_enable();

    // --- FIX: Define config and pass pointer instead of NULL ---
    esp_rmaker_system_serv_config_t system_serv_config = {
        .flags = SYSTEM_SERV_FLAGS_ALL,
        .reboot_seconds = 2,
        .reset_seconds = 2,
        .reset_reboot_seconds = 2,
    };
    esp_rmaker_system_service_enable(&system_serv_config);
    // ----------------------------------------------------------

    app_insights_enable();

    /* Start */
    esp_rmaker_start();

    err = app_network_set_custom_mfg_data(MGF_DATA_DEVICE_TYPE_LIGHT, MFG_DATA_DEVICE_SUBTYPE_LIGHT);
    err = app_network_start((app_network_pop_type_t)CONFIG_APP_POP_TYPE);
}