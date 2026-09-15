#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* Product geometry / UI. HS20HS072RX is the 320x240 ST7789-class panel. */
#define APP_LCD_WIDTH                 320U
#define APP_LCD_HEIGHT                240U
#define APP_LCD_X_OFFSET              0U
#define APP_LCD_Y_OFFSET              0U

/* Australian FM broadcast band, represented in SI4705 10 kHz units. */
#define APP_FM_MIN_10KHZ              8750U
#define APP_FM_MAX_10KHZ              10800U
#define APP_FM_STEP_10KHZ             10U
#define APP_FM_DEFAULT_10KHZ          10170U
#define APP_FM_STATUS_POLL_MS           250U
#define APP_FM_RDS_POLL_MS               20U
#define APP_FM_SEEK_TIMEOUT_MS         12000U
#define APP_BT_METADATA_CORE_POLL_MS      5000U
#define APP_BT_METADATA_RESPONSE_TIMEOUT_MS 2200U
#define APP_BT_METADATA_RETRY_BACKOFF_MS  5000U
#define APP_BT_METADATA_STALE_DRAIN_MS     800U
#define APP_BT_METADATA_DETAIL_GAP_MS      500U
#define APP_BT_METADATA_POST_CONTROL_MS    800U
#define APP_BT_METADATA_IDENTITY_GUARD_MS 8000U
#define APP_BT_BROWSE_BUFFER_BYTES        512U
#define APP_BT_CONTROL_QUEUE_DEPTH           8U
#define APP_POWER_SAVE_BT_METADATA_CORE_POLL_MS 10000U
#define APP_POWER_SAVE_BRIGHTNESS_PERCENT   25U
#define APP_NORMAL_RENDER_PERIOD_MS          50U
#define APP_POWER_SAVE_RENDER_PERIOD_MS     250U

#define APP_POWER_POLL_MS             1000U
#define APP_USB_INPUT_LIMIT_MA          500U
#define APP_SETTINGS_SAVE_DELAY_MS    1500U
#define APP_INPUT_DEBOUNCE_MS         25U
#define APP_INPUT_LONG_PRESS_MS       900U
#define APP_ENCODER_POWER_HOLD_MS    4000U
#define APP_VOLUME_STEP_PERCENT         2U
#define APP_BOOT_ANIMATION_MS        1900U
#define APP_POWER_ANIMATION_MS       1850U
#define APP_POWER_AUDIO_FADE_MS      1650U
/* The renderer sends only changed screen regions during transitions, so the
 * small kiku mark can update at ~40 fps without a full-frame SPI transfer. */
#define APP_ANIMATION_FRAME_MS         20U
#define APP_THEME_GESTURE_WINDOW_MS  2200U
#define APP_LOW_BATTERY_CENTI_PERCENT 1000U
#define APP_CRITICAL_BATTERY_CENTI_PERCENT 400U

/*
 * Bench-only charging policy. Kept here (not in the BQ25895 driver) so these
 * values are obvious and can be replaced by a production battery/thermal
 * policy after the exact cell and enclosure are validated.
 */
#define APP_BENCH_CHARGE_INPUT_LIMIT_MA  500U
#define APP_BENCH_CHARGE_CURRENT_MA      500U
#define APP_BENCH_BQ_WATCHDOG_SECONDS     80U
#define APP_BENCH_CHARGE_POLICY_ENABLE      0U

#define APP_LOCAL_PCM_FRAMES_PER_CHUNK  1152U
#define APP_PATH_MAX                     192U

#endif
