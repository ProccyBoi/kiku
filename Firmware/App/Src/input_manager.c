#include "input_manager.h"

#include <string.h>

static bool read_pressed(const dev_gpio_t *gpio, bool active_low)
{
    bool raw = gpio->read(gpio->ctx);
    return active_low ? !raw : raw;
}

static void emit(input_manager_t *mgr, input_event_type_t type, int16_t value)
{
    if (mgr->callback == NULL) return;
    input_event_t ev = { type, value };
    mgr->callback(mgr->callback_ctx, &ev);
}

dev_status_t input_manager_init(input_manager_t *mgr,
                                const dev_gpio_t *button1,
                                const dev_gpio_t *button2,
                                const dev_gpio_t *encoder_push,
                                bool active_low,
                                uint32_t debounce_ms,
                                uint32_t long_press_ms,
                                uint32_t encoder_long_press_ms,
                                input_event_cb_t callback,
                                void *callback_ctx)
{
    if ((mgr == NULL) || (button1 == NULL) || (button2 == NULL) ||
        (encoder_push == NULL) || (button1->read == NULL) ||
        (button2->read == NULL) || (encoder_push->read == NULL)) return DEV_EINVAL;
    memset(mgr, 0, sizeof(*mgr));
    mgr->button1 = *button1;
    mgr->button2 = *button2;
    mgr->encoder_push = *encoder_push;
    mgr->active_low = active_low;
    mgr->debounce_ms = debounce_ms;
    mgr->long_press_ms = long_press_ms;
    mgr->encoder_long_press_ms = encoder_long_press_ms;
    mgr->callback = callback;
    mgr->callback_ctx = callback_ctx;
    return DEV_OK;
}

void input_manager_poll(input_manager_t *mgr, uint32_t now_ms)
{
    if (mgr == NULL) return;
    const dev_gpio_t *gpios[3] = { &mgr->button1, &mgr->button2, &mgr->encoder_push };
    const input_event_type_t short_ev[3] = {
        INPUT_EVENT_BUTTON_1, INPUT_EVENT_BUTTON_2, INPUT_EVENT_ENCODER_PRESS
    };
    const input_event_type_t long_ev[3] = {
        INPUT_EVENT_BUTTON_1_LONG, INPUT_EVENT_BUTTON_2_LONG, INPUT_EVENT_ENCODER_LONG
    };

    for (uint8_t i = 0U; i < 3U; ++i) {
        uint8_t bit = (uint8_t)(1U << i);
        bool raw_pressed = read_pressed(gpios[i], mgr->active_low);
        bool previous_raw = (mgr->raw_mask & bit) != 0U;
        if (raw_pressed != previous_raw) {
            if (raw_pressed) mgr->raw_mask |= bit;
            else mgr->raw_mask &= (uint8_t)~bit;
            mgr->raw_changed_ms[i] = now_ms;
        }

        bool stable_pressed = (mgr->stable_mask & bit) != 0U;
        if ((raw_pressed != stable_pressed) &&
            ((uint32_t)(now_ms - mgr->raw_changed_ms[i]) >= mgr->debounce_ms)) {
            if (raw_pressed) {
                mgr->stable_mask |= bit;
                mgr->pressed_ms[i] = now_ms;
                mgr->long_sent_mask &= (uint8_t)~bit;
            } else {
                mgr->stable_mask &= (uint8_t)~bit;
                if ((mgr->long_sent_mask & bit) == 0U) emit(mgr, short_ev[i], 1);
                mgr->long_sent_mask &= (uint8_t)~bit;
            }
            stable_pressed = raw_pressed;
        }

        uint32_t hold_ms = (i == 2U) ? mgr->encoder_long_press_ms : mgr->long_press_ms;
        if (stable_pressed && ((mgr->long_sent_mask & bit) == 0U) &&
            ((uint32_t)(now_ms - mgr->pressed_ms[i]) >= hold_ms)) {
            mgr->long_sent_mask |= bit;
            emit(mgr, long_ev[i], 1);
        }
    }

}

void input_manager_encoder_delta(input_manager_t *mgr, int16_t delta)
{
    if ((mgr == NULL) || (delta == 0)) return;
    emit(mgr, INPUT_EVENT_ENCODER_DELTA, delta);
}
