#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include "device_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_BUTTON_1,
    INPUT_EVENT_BUTTON_2,
    INPUT_EVENT_ENCODER_PRESS,
    INPUT_EVENT_BUTTON_1_LONG,
    INPUT_EVENT_BUTTON_2_LONG,
    INPUT_EVENT_ENCODER_LONG,
    INPUT_EVENT_ENCODER_DELTA
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    int16_t value;
} input_event_t;

typedef void (*input_event_cb_t)(void *ctx, const input_event_t *event);

typedef struct {
    dev_gpio_t button1;
    dev_gpio_t button2;
    dev_gpio_t encoder_push;
    bool active_low;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t encoder_long_press_ms;
    input_event_cb_t callback;
    void *callback_ctx;
    uint8_t stable_mask;
    uint8_t raw_mask;
    uint8_t long_sent_mask;
    uint32_t raw_changed_ms[3];
    uint32_t pressed_ms[3];
} input_manager_t;

dev_status_t input_manager_init(input_manager_t *mgr,
                                const dev_gpio_t *button1,
                                const dev_gpio_t *button2,
                                const dev_gpio_t *encoder_push,
                                bool active_low,
                                uint32_t debounce_ms,
                                uint32_t long_press_ms,
                                uint32_t encoder_long_press_ms,
                                input_event_cb_t callback,
                                void *callback_ctx);
void input_manager_poll(input_manager_t *mgr, uint32_t now_ms);
void input_manager_encoder_delta(input_manager_t *mgr, int16_t delta);

#ifdef __cplusplus
}
#endif

#endif
