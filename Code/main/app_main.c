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

// =========================================================
//  CALIBRATION SETTINGS (Based on your measurements)
// =========================================================

// 1. COOL CHANNEL SCALING
// Previous ratio was ~0.856.
// You measured Cool=4630 vs Warm=4576. Cool is 1.2% too bright.
// New Factor = 0.856 * (4576 / 4630) = ~0.846
#define COOL_CHANNEL_SCALE 0.846f

// 2. MIDPOINT ATTENUATION
// You measured Middle=4758 vs Target=4576.
// The middle is ~4% too bright.
// We will dip the brightness by 0.04 at the center.
#define MIDPOINT_DROP 0.04f

static void update_led_output()
{
    if (g_power == 0)
    {
        app_light_set_brightness(1, 0);
        app_light_set_brightness(2, 0);
        return;
    }

    // --- 1. Calculate the Midpoint "Dip" ---
    // Distance from center (0 to 50 scale normalized to 0.0 - 1.0)
    // If CCT is 50, dist is 0.0. If CCT is 0 or 100, dist is 1.0.
    float dist_from_center = (float)abs(g_cct - 50) / 50.0f;

    // Correction Factor:
    // Center (dist=0) -> 1.0 - 0.04 = 0.96 (96% brightness)
    // Edge (dist=1)   -> 1.0 - 0.00 = 1.00 (100% brightness)
    float midpoint_correction = 1.0f - (MIDPOINT_DROP * (1.0f - dist_from_center));

    // --- 2. Calculate Ratios ---
    float ratio_cool = (float)g_cct / 100.0f;
    float ratio_warm = 1.0f - ratio_cool;

    // --- 3. Apply Factors ---
    // Cool: Global_Bright * Ratio * Hard_Scale * Midpoint_Dip
    float cool_pwm = (float)g_brightness * ratio_cool * COOL_CHANNEL_SCALE * midpoint_correction;

    // Warm: Global_Bright * Ratio * Midpoint_Dip
    float warm_pwm = (float)g_brightness * ratio_warm * midpoint_correction;

    // --- 4. Hardware Output (Swap Channels: 1=Warm, 2=Cool) ---
    int ch1_warm_val = (int)warm_pwm;
    int ch2_cool_val = (int)cool_pwm;

    ESP_LOGI(TAG, "CCT:%d | Comp:%.3f | Out -> Warm: %d, Cool: %d",
             g_cct, midpoint_correction, ch1_warm_val, ch2_cool_val);

    app_light_set_brightness(1, ch1_warm_val);
    app_light_set_brightness(2, ch2_cool_val);
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
    esp_rmaker_param_t *cct_param = esp_rmaker_cct_param_create(ESP_RMAKER_DEF_CCT_NAME, 50);
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