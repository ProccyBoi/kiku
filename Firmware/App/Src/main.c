#include "app.h"
#include "audio_stream.h"
#include "main.h"
#include "platform.h"
#include "ui_renderer.h"

#include "FreeRTOS.h"
#include "task.h"

static platform_devices_t s_platform;
static kiku_app_t s_app;

/* SWD bench mailbox: write a frequency in 10 kHz units to request FM from
 * the application task, where HAL delays and audio routing are safe. Zero
 * is idle. Normal boot behaviour is unchanged; result is a dev_status_t. */
volatile uint32_t g_bench_fm_request_10khz;
volatile uint32_t g_bench_volume_percent = 25U;
volatile int32_t g_bench_fm_result = DEV_ENOTREADY;
/* -1 seeks down, +1 seeks up, 0 idle. */
volatile int32_t g_bench_fm_seek_request;
volatile int32_t g_bench_fm_seek_result = DEV_ENOTREADY;
volatile uint32_t g_bench_power_toggle_request;

/* GCC -fstack-usage shows the deepest MP3 decode path at roughly 21.5 KiB
 * (minimp3 frame decoder + local playback frame). 8K Cortex-M33 stack words
 * gives a 32 KiB task stack with useful margin for HAL/FatFs call depth. */
#define APP_TASK_STACK_WORDS 8192U
#define LOCAL_PREFILL_FRAMES 6144U /* 128 ms at 48 kHz */

static uint16_t animation_ease(uint16_t progress)
{
    uint64_t p = progress > 1000U ? 1000U : progress;
    uint64_t p2 = p * p;
    uint64_t p3 = p2 * p;
    uint64_t eased = (p3 * (10000000ULL + p * (6ULL * p - 15000ULL)) +
                       500000000000ULL) / 1000000000000ULL;
    if (eased > 1000ULL) eased = 1000ULL;
    return (uint16_t)eased;
}

static uint16_t transition_backlight_level(const kiku_app_t *app)
{
    uint32_t full = (uint32_t)app->ui.brightness_percent * 100U;
    uint32_t visible = 1000U;
    if (app->ui.screen == UI_SCREEN_BOOT || app->ui.screen == UI_SCREEN_POWERING_ON) {
        /* Start with a deliberate low glow after the first animation frame has
         * reached GRAM. A completely black 0% first half looked like there was
         * no boot animation on the physical unit, especially at low brightness. */
        uint32_t eased = animation_ease(app->ui.animation_progress);
        visible = 160U + ((840U * eased + 500U) / 1000U);
    } else if (app->ui.screen == UI_SCREEN_POWERING_OFF) {
        /* Fade for the whole transition rather than cramming the luminance
         * change into its second half. With 0.01%-resolution PWM this removes
         * the visible staircase near black. */
        visible = 1000U - animation_ease(app->ui.animation_progress);
    }
    return (uint16_t)((full * visible + 500U) / 1000U);
}

static void prefill_local_audio(void)
{
    if (!s_app.initialised || s_app.audio.source != AUDIO_SOURCE_LOCAL ||
        !s_app.local.playing || s_app.local.file == NULL) return;

    /* A full 320x240 software-rendered SPI frame can occupy the foreground
     * task for tens of milliseconds. Keep ~128 ms queued before rendering so
     * the circular I2S DMA never depends on foreground/UI latency. */
    for (unsigned guard = 0U; guard < 16U &&
         audio_stream_queued_frames() < LOCAL_PREFILL_FRAMES; ++guard) {
        dev_status_t st = local_playback_process(&s_app.local);
        if (st == DEV_EBUSY) break;
        if (st != DEV_OK || s_app.local.eof) break;
    }
}

static void app_task(void *argument)
{
    (void)argument;
    dev_status_t st = platform_devices_init(&s_platform);
    if (st == DEV_OK) st = kiku_app_init(&s_app, &s_platform.deps);
    if (st == DEV_OK) s_app.ui.storage_mounted = storage_is_mounted(&s_platform.storage);

    uint32_t last_render = 0U;
    bool was_soft_powered_off = false;
    if (st == DEV_OK && s_app.ui.screen == UI_SCREEN_BOOT) {
        /* Prime the panel synchronously and start the boot timer only after the
         * first visible animation frame is physically present. This makes cold
         * boot deterministic instead of allowing init/display latency to eat
         * the beginning of the animation. */
        s_app.ui.animation_progress = 0U;
        ui_renderer_invalidate();
        (void)ui_renderer_draw(&s_platform.display, &s_app.ui);
        s_app.transition_started_ms = HAL_GetTick();
        last_render = s_app.transition_started_ms;
        platform_set_backlight_level(transition_backlight_level(&s_app));
    }
    for (;;) {
        if (st == DEV_OK) {
            if (g_bench_power_toggle_request != 0U) {
                g_bench_power_toggle_request = 0U;
                kiku_app_toggle_soft_power(&s_app);
            }
            uint32_t requested_fm = g_bench_fm_request_10khz;
            if (requested_fm != 0U) {
                g_bench_fm_request_10khz = 0U;
                if (requested_fm < APP_FM_MIN_10KHZ || requested_fm > APP_FM_MAX_10KHZ) {
                    g_bench_fm_result = DEV_EINVAL;
                } else {
                    uint8_t volume = (uint8_t)(g_bench_volume_percent > 100U ?
                                               100U : g_bench_volume_percent);
                    s_app.settings.value.volume_percent = volume;
                    s_app.ui.volume_percent = volume;
                    g_bench_fm_result = audio_router_set_volume(&s_app.audio, volume);
                    if (g_bench_fm_result == DEV_OK) {
                        g_bench_fm_result = kiku_app_tune_fm(&s_app, (uint16_t)requested_fm);
                    }
                }
            }
            int32_t requested_seek = g_bench_fm_seek_request;
            if (requested_seek != 0) {
                g_bench_fm_seek_request = 0;
                if (requested_seek != -1 && requested_seek != 1) {
                    g_bench_fm_seek_result = DEV_EINVAL;
                } else {
                    g_bench_fm_seek_result = kiku_app_seek_fm(&s_app, requested_seek > 0);
                }
            }
            int16_t encoder = platform_encoder_delta();
            if (encoder != 0) kiku_app_encoder_delta(&s_app, encoder);
            platform_poll_bm83(&s_app);
            (void)kiku_app_tick(&s_app);
            if (was_soft_powered_off && !s_app.soft_powered_off) {
                /* The LCD controller may retain GRAM across SLPIN/SLPOUT, but
                 * do not depend on that for a product transition. Force the
                 * first wake frame to be sent before the backlight is raised. */
                ui_renderer_invalidate();
                if (s_app.ui.screen == UI_SCREEN_POWERING_ON) {
                    s_app.ui.animation_progress = 0U;
                    (void)ui_renderer_draw(&s_platform.display, &s_app.ui);
                    s_app.transition_started_ms = HAL_GetTick();
                    last_render = s_app.transition_started_ms;
                    platform_set_backlight_level(transition_backlight_level(&s_app));
                } else {
                    last_render = 0U;
                }
            }
            if (s_app.soft_powered_off) {
                platform_set_backlight(0U);
                platform_watchdog_refresh();
                was_soft_powered_off = true;
                vTaskDelay(pdMS_TO_TICKS(20U));
                continue;
            }
            platform_service_stream_audio(&s_app);
            uint32_t now = HAL_GetTick();
            bool animating = s_app.ui.screen == UI_SCREEN_BOOT ||
                             s_app.ui.screen == UI_SCREEN_POWERING_OFF ||
                             s_app.ui.screen == UI_SCREEN_POWERING_ON;
            uint32_t render_period = animating ? APP_ANIMATION_FRAME_MS :
                                     (s_app.settings.value.power_save_enabled != 0U ?
                                      APP_POWER_SAVE_RENDER_PERIOD_MS : APP_NORMAL_RENDER_PERIOD_MS);
            if ((uint32_t)(now - last_render) >= render_period) {
                prefill_local_audio();
                (void)ui_renderer_draw(&s_platform.display, &s_app.ui);
                /* Rendering is the longest normal foreground operation. Drain
                 * BM83 immediately afterwards instead of adding another task
                 * period of Command/Event_ACK latency. */
                platform_poll_bm83(&s_app);
                last_render = now;
            }
            /* Update luminance only after any due frame has reached GRAM. This
             * prevents wake-up from exposing a stale pre-sleep frame for even a
             * single visible refresh. */
            platform_set_backlight_level(transition_backlight_level(&s_app));
            was_soft_powered_off = false;
            platform_watchdog_refresh();
        }
        /* On a fatal platform/application initialisation error deliberately do
         * not refresh IWDG. The board will reset instead of hanging forever in
         * an unusable state while appearing healthy to the watchdog. */
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

int main(void)
{
    Board_Init();
    if (xTaskCreate(app_task, "App", APP_TASK_STACK_WORDS, NULL,
                    tskIDLE_PRIORITY + 3U, NULL) != pdPASS) {
        Error_Handler();
    }
    vTaskStartScheduler();
    Error_Handler();
    return 0;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task; (void)task_name;
    taskDISABLE_INTERRUPTS();
    Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    Error_Handler();
}

void vApplicationIdleHook(void)
{
    /* The application task deliberately sleeps between 10 ms service passes.
     * Without an idle hook FreeRTOS spins at 160 MHz for all of that time,
     * which dominates both awake-idle and soft-off battery current. Keep the
     * normal clocks/peripherals running, but halt the Cortex-M33 core until the
     * next SysTick/HAL/peripheral interrupt. Audio DMA, UART, timers and the
     * watchdog therefore keep their existing behaviour while CPU dynamic power
     * drops whenever there is no useful work to do. */
    __DSB();
    __WFI();
    __ISB();
}
