#include "ui_renderer.h"

#include <stdio.h>
#include <string.h>

#define UI_MAX_W 320U
#define UI_MAX_H 240U
#define UI_FB_BYTES ((UI_MAX_W * UI_MAX_H) / 8U)
#define RGB565(r,g,b) ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))

static uint8_t s_fb[UI_FB_BYTES];
static uint8_t s_prev_fb[UI_FB_BYTES];
static uint32_t s_last_hash;
static bool s_have_frame;
static bool s_prev_valid;
static uint16_t s_w;
static uint16_t s_h;
static ui_theme_t s_last_theme = UI_THEME_KIKU;

static uint32_t hash_bytes(uint32_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len-- != 0U) { h ^= *p++; h *= 16777619UL; }
    return h;
}

static uint32_t ui_hash(const ui_state_t *ui)
{
    uint32_t h = 2166136261UL;
    h = hash_bytes(h, &ui->screen, sizeof(ui->screen));
    h = hash_bytes(h, &ui->source, sizeof(ui->source));
    h = hash_bytes(h, &ui->output, sizeof(ui->output));
    h = hash_bytes(h, &ui->menu_index, sizeof(ui->menu_index));
    h = hash_bytes(h, &ui->playing, sizeof(ui->playing));
    h = hash_bytes(h, &ui->volume_percent, sizeof(ui->volume_percent));
    h = hash_bytes(h, &ui->battery_centi_percent, sizeof(ui->battery_centi_percent));
    h = hash_bytes(h, &ui->battery_mv, sizeof(ui->battery_mv));
    h = hash_bytes(h, &ui->charging, sizeof(ui->charging));
    h = hash_bytes(h, &ui->headphones_present, sizeof(ui->headphones_present));
    h = hash_bytes(h, &ui->bluetooth_connected, sizeof(ui->bluetooth_connected));
    h = hash_bytes(h, &ui->bt_avrcp_connected, sizeof(ui->bt_avrcp_connected));
    h = hash_bytes(h, &ui->fm_seeking, sizeof(ui->fm_seeking));
    h = hash_bytes(h, &ui->storage_mounted, sizeof(ui->storage_mounted));
    h = hash_bytes(h, &ui->usb_vbus_mv, sizeof(ui->usb_vbus_mv));
    h = hash_bytes(h, &ui->charge_current_ma, sizeof(ui->charge_current_ma));
    h = hash_bytes(h, &ui->charger_status_raw, sizeof(ui->charger_status_raw));
    h = hash_bytes(h, &ui->charger_fault_raw, sizeof(ui->charger_fault_raw));
    h = hash_bytes(h, &ui->usb_power_good, sizeof(ui->usb_power_good));
    h = hash_bytes(h, &ui->power_save_enabled, sizeof(ui->power_save_enabled));
    h = hash_bytes(h, &ui->shuffle_enabled, sizeof(ui->shuffle_enabled));
    h = hash_bytes(h, &ui->theme, sizeof(ui->theme));
    h = hash_bytes(h, &ui->fm_control_mode, sizeof(ui->fm_control_mode));
    h = hash_bytes(h, &ui->fm_preset_index, sizeof(ui->fm_preset_index));
    h = hash_bytes(h, &ui->animation_progress, sizeof(ui->animation_progress));
    h = hash_bytes(h, &ui->error_flags, sizeof(ui->error_flags));
    /* Only hash FM state that is actually rendered. RDS assembly masks and
     * partial groups change much more often than the user-visible text; hashing
     * the whole parser state caused pointless display traffic while text was
     * still being assembled. */
    h = hash_bytes(h, &ui->fm.frequency_10khz, sizeof(ui->fm.frequency_10khz));
    h = hash_bytes(h, &ui->fm.pty, sizeof(ui->fm.pty));
    h = hash_bytes(h, &ui->fm.stereo, sizeof(ui->fm.stereo));
    h = hash_bytes(h, &ui->fm.rssi_dbuv, sizeof(ui->fm.rssi_dbuv));
    h = hash_bytes(h, &ui->fm.rds_sync, sizeof(ui->fm.rds_sync));
    bool ps_valid = fm_state_has_program_service(&ui->fm);
    bool ps_partial = fm_state_has_partial_program_service(&ui->fm);
    bool rt_valid = fm_state_has_radio_text(&ui->fm);
    bool rt_title_valid = fm_state_has_rtplus_title(&ui->fm);
    bool rt_artist_valid = fm_state_has_rtplus_artist(&ui->fm);
    h = hash_bytes(h, &ps_valid, sizeof(ps_valid));
    h = hash_bytes(h, &ps_partial, sizeof(ps_partial));
    h = hash_bytes(h, &rt_valid, sizeof(rt_valid));
    h = hash_bytes(h, &rt_title_valid, sizeof(rt_title_valid));
    h = hash_bytes(h, &rt_artist_valid, sizeof(rt_artist_valid));
    if (ps_valid || ps_partial)
        h = hash_bytes(h, ui->fm.program_service, sizeof(ui->fm.program_service));
    if (rt_valid) h = hash_bytes(h, ui->fm.radio_text, sizeof(ui->fm.radio_text));
    if (rt_title_valid) h = hash_bytes(h, ui->fm.rtplus_title, sizeof(ui->fm.rtplus_title));
    if (rt_artist_valid) h = hash_bytes(h, ui->fm.rtplus_artist, sizeof(ui->fm.rtplus_artist));
    h = hash_bytes(h, ui->fm_preset_name, sizeof(ui->fm_preset_name));
    h = hash_bytes(h, ui->track_title, sizeof(ui->track_title));
    h = hash_bytes(h, ui->track_artist, sizeof(ui->track_artist));
    h = hash_bytes(h, ui->track_album, sizeof(ui->track_album));
    h = hash_bytes(h, ui->track_genre, sizeof(ui->track_genre));
    h = hash_bytes(h, &ui->track_number, sizeof(ui->track_number));
    h = hash_bytes(h, &ui->track_total, sizeof(ui->track_total));
    h = hash_bytes(h, &ui->track_duration_ms, sizeof(ui->track_duration_ms));
    return h;
}

static void px(int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= (int)s_w || y >= (int)s_h) return;
    size_t n = (size_t)y * UI_MAX_W + (size_t)x;
    uint8_t mask = (uint8_t)(1U << (7U - (n & 7U)));
    if (on) s_fb[n >> 3] |= mask;
    else s_fb[n >> 3] &= (uint8_t)~mask;
}

static void fill_rect(int x, int y, int w, int h, bool on)
{
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) px(x + xx, y + yy, on);
}

static void line_h(int x, int y, int w, bool on) { fill_rect(x, y, w, 1, on); }
static void line_v(int x, int y, int h, bool on) { fill_rect(x, y, 1, h, on); }

static void draw_box(int x, int y, int w, int h, bool on)
{
    if (w <= 1 || h <= 1) return;
    line_h(x, y, w, on);
    line_h(x, y + h - 1, w, on);
    line_v(x, y, h, on);
    line_v(x + w - 1, y, h, on);
}

static void glyph(char c, uint8_t out[5])
{
    memset(out, 0, 5U);
    switch (c) {
    case 'A': { uint8_t v[5]={0x7E,0x11,0x11,0x11,0x7E}; memcpy(out,v,5); } break;
    case 'B': { uint8_t v[5]={0x7F,0x49,0x49,0x49,0x36}; memcpy(out,v,5); } break;
    case 'C': { uint8_t v[5]={0x3E,0x41,0x41,0x41,0x22}; memcpy(out,v,5); } break;
    case 'D': { uint8_t v[5]={0x7F,0x41,0x41,0x22,0x1C}; memcpy(out,v,5); } break;
    case 'E': { uint8_t v[5]={0x7F,0x49,0x49,0x49,0x41}; memcpy(out,v,5); } break;
    case 'F': { uint8_t v[5]={0x7F,0x09,0x09,0x09,0x01}; memcpy(out,v,5); } break;
    case 'G': { uint8_t v[5]={0x3E,0x41,0x49,0x49,0x7A}; memcpy(out,v,5); } break;
    case 'H': { uint8_t v[5]={0x7F,0x08,0x08,0x08,0x7F}; memcpy(out,v,5); } break;
    case 'I': { uint8_t v[5]={0x00,0x41,0x7F,0x41,0x00}; memcpy(out,v,5); } break;
    case 'J': { uint8_t v[5]={0x20,0x40,0x41,0x3F,0x01}; memcpy(out,v,5); } break;
    case 'K': { uint8_t v[5]={0x7F,0x08,0x14,0x22,0x41}; memcpy(out,v,5); } break;
    case 'L': { uint8_t v[5]={0x7F,0x40,0x40,0x40,0x40}; memcpy(out,v,5); } break;
    case 'M': { uint8_t v[5]={0x7F,0x02,0x0C,0x02,0x7F}; memcpy(out,v,5); } break;
    case 'N': { uint8_t v[5]={0x7F,0x04,0x08,0x10,0x7F}; memcpy(out,v,5); } break;
    case 'O': { uint8_t v[5]={0x3E,0x41,0x41,0x41,0x3E}; memcpy(out,v,5); } break;
    case 'P': { uint8_t v[5]={0x7F,0x09,0x09,0x09,0x06}; memcpy(out,v,5); } break;
    case 'Q': { uint8_t v[5]={0x3E,0x41,0x51,0x21,0x5E}; memcpy(out,v,5); } break;
    case 'R': { uint8_t v[5]={0x7F,0x09,0x19,0x29,0x46}; memcpy(out,v,5); } break;
    case 'S': { uint8_t v[5]={0x46,0x49,0x49,0x49,0x31}; memcpy(out,v,5); } break;
    case 'T': { uint8_t v[5]={0x01,0x01,0x7F,0x01,0x01}; memcpy(out,v,5); } break;
    case 'U': { uint8_t v[5]={0x3F,0x40,0x40,0x40,0x3F}; memcpy(out,v,5); } break;
    case 'V': { uint8_t v[5]={0x1F,0x20,0x40,0x20,0x1F}; memcpy(out,v,5); } break;
    case 'W': { uint8_t v[5]={0x7F,0x20,0x18,0x20,0x7F}; memcpy(out,v,5); } break;
    case 'X': { uint8_t v[5]={0x63,0x14,0x08,0x14,0x63}; memcpy(out,v,5); } break;
    case 'Y': { uint8_t v[5]={0x03,0x04,0x78,0x04,0x03}; memcpy(out,v,5); } break;
    case 'Z': { uint8_t v[5]={0x61,0x51,0x49,0x45,0x43}; memcpy(out,v,5); } break;
    case '0': { uint8_t v[5]={0x3E,0x51,0x49,0x45,0x3E}; memcpy(out,v,5); } break;
    case '1': { uint8_t v[5]={0x00,0x42,0x7F,0x40,0x00}; memcpy(out,v,5); } break;
    case '2': { uint8_t v[5]={0x42,0x61,0x51,0x49,0x46}; memcpy(out,v,5); } break;
    case '3': { uint8_t v[5]={0x21,0x41,0x45,0x4B,0x31}; memcpy(out,v,5); } break;
    case '4': { uint8_t v[5]={0x18,0x14,0x12,0x7F,0x10}; memcpy(out,v,5); } break;
    case '5': { uint8_t v[5]={0x27,0x45,0x45,0x45,0x39}; memcpy(out,v,5); } break;
    case '6': { uint8_t v[5]={0x3C,0x4A,0x49,0x49,0x30}; memcpy(out,v,5); } break;
    case '7': { uint8_t v[5]={0x01,0x71,0x09,0x05,0x03}; memcpy(out,v,5); } break;
    case '8': { uint8_t v[5]={0x36,0x49,0x49,0x49,0x36}; memcpy(out,v,5); } break;
    case '9': { uint8_t v[5]={0x06,0x49,0x49,0x29,0x1E}; memcpy(out,v,5); } break;
    case '-': { uint8_t v[5]={0x08,0x08,0x08,0x08,0x08}; memcpy(out,v,5); } break;
    case '.': out[2]=0x60; break;
    case ':': out[2]=0x24; break;
    case '/': { uint8_t v[5]={0x60,0x18,0x06,0x01,0x00}; memcpy(out,v,5); } break;
    case '\'': out[2]=0x03; break;
    case '%': { uint8_t v[5]={0x63,0x13,0x08,0x64,0x63}; memcpy(out,v,5); } break;
    case '?': { uint8_t v[5]={0x02,0x01,0x51,0x09,0x06}; memcpy(out,v,5); } break;
    case '!': { uint8_t v[5]={0x00,0x00,0x5F,0x00,0x00}; memcpy(out,v,5); } break;
    case '>': { uint8_t v[5]={0x00,0x41,0x22,0x14,0x08}; memcpy(out,v,5); } break;
    case '<': { uint8_t v[5]={0x08,0x14,0x22,0x41,0x00}; memcpy(out,v,5); } break;
    case '+': { uint8_t v[5]={0x08,0x08,0x3E,0x08,0x08}; memcpy(out,v,5); } break;
    case '_': { uint8_t v[5]={0x40,0x40,0x40,0x40,0x40}; memcpy(out,v,5); } break;
    case '=': { uint8_t v[5]={0x14,0x14,0x14,0x14,0x14}; memcpy(out,v,5); } break;
    case ' ': break;
    case ',': out[1]=0x40; out[2]=0x20; break;
    case '"': { uint8_t v[5]={0x00,0x07,0x00,0x07,0x00}; memcpy(out,v,5); } break;
    case '&': { uint8_t v[5]={0x36,0x49,0x55,0x22,0x50}; memcpy(out,v,5); } break;
    case '(': { uint8_t v[5]={0x00,0x1C,0x22,0x41,0x00}; memcpy(out,v,5); } break;
    case ')': { uint8_t v[5]={0x00,0x41,0x22,0x1C,0x00}; memcpy(out,v,5); } break;
    case '#': { uint8_t v[5]={0x14,0x7F,0x14,0x7F,0x14}; memcpy(out,v,5); } break;
    /* Mixed-case 5x7 face for song/station metadata. Keeping lowercase
     * distinct from the compact all-caps navigation face makes the UI read
     * like a small music player rather than a terminal/dashboard. */
    case 'a': { uint8_t v[5]={0x20,0x54,0x54,0x54,0x78}; memcpy(out,v,5); } break;
    case 'b': { uint8_t v[5]={0x7F,0x48,0x44,0x44,0x38}; memcpy(out,v,5); } break;
    case 'c': { uint8_t v[5]={0x38,0x44,0x44,0x44,0x20}; memcpy(out,v,5); } break;
    case 'd': { uint8_t v[5]={0x38,0x44,0x44,0x48,0x7F}; memcpy(out,v,5); } break;
    case 'e': { uint8_t v[5]={0x38,0x54,0x54,0x54,0x18}; memcpy(out,v,5); } break;
    case 'f': { uint8_t v[5]={0x08,0x7E,0x09,0x01,0x02}; memcpy(out,v,5); } break;
    case 'g': { uint8_t v[5]={0x0C,0x52,0x52,0x52,0x3E}; memcpy(out,v,5); } break;
    case 'h': { uint8_t v[5]={0x7F,0x08,0x04,0x04,0x78}; memcpy(out,v,5); } break;
    case 'i': { uint8_t v[5]={0x00,0x44,0x7D,0x40,0x00}; memcpy(out,v,5); } break;
    case 'j': { uint8_t v[5]={0x20,0x40,0x44,0x3D,0x00}; memcpy(out,v,5); } break;
    case 'k': { uint8_t v[5]={0x7F,0x10,0x28,0x44,0x00}; memcpy(out,v,5); } break;
    case 'l': { uint8_t v[5]={0x00,0x41,0x7F,0x40,0x00}; memcpy(out,v,5); } break;
    case 'm': { uint8_t v[5]={0x7C,0x04,0x18,0x04,0x78}; memcpy(out,v,5); } break;
    case 'n': { uint8_t v[5]={0x7C,0x08,0x04,0x04,0x78}; memcpy(out,v,5); } break;
    case 'o': { uint8_t v[5]={0x38,0x44,0x44,0x44,0x38}; memcpy(out,v,5); } break;
    case 'p': { uint8_t v[5]={0x7C,0x14,0x14,0x14,0x08}; memcpy(out,v,5); } break;
    case 'q': { uint8_t v[5]={0x08,0x14,0x14,0x18,0x7C}; memcpy(out,v,5); } break;
    case 'r': { uint8_t v[5]={0x7C,0x08,0x04,0x04,0x08}; memcpy(out,v,5); } break;
    case 's': { uint8_t v[5]={0x48,0x54,0x54,0x54,0x20}; memcpy(out,v,5); } break;
    case 't': { uint8_t v[5]={0x04,0x3F,0x44,0x40,0x20}; memcpy(out,v,5); } break;
    case 'u': { uint8_t v[5]={0x3C,0x40,0x40,0x20,0x7C}; memcpy(out,v,5); } break;
    case 'v': { uint8_t v[5]={0x1C,0x20,0x40,0x20,0x1C}; memcpy(out,v,5); } break;
    case 'w': { uint8_t v[5]={0x3C,0x40,0x30,0x40,0x3C}; memcpy(out,v,5); } break;
    case 'x': { uint8_t v[5]={0x44,0x28,0x10,0x28,0x44}; memcpy(out,v,5); } break;
    case 'y': { uint8_t v[5]={0x0C,0x50,0x50,0x50,0x3C}; memcpy(out,v,5); } break;
    case 'z': { uint8_t v[5]={0x44,0x64,0x54,0x4C,0x44}; memcpy(out,v,5); } break;
    default: { uint8_t v[5]={0x7F,0x41,0x5D,0x41,0x7F}; memcpy(out,v,5); } break;
    }
}

static int text_width(const char *s, int scale)
{
    return s == NULL ? 0 : (int)strlen(s) * 6 * scale;
}

static void draw_text_colour(int x, int y, const char *s, int scale, bool on)
{
    if (s == NULL || scale <= 0) return;
    while (*s != '\0' && x + 5 * scale < (int)s_w) {
        uint8_t g[5]; glyph(*s++, g);
        for (int col=0; col<5; ++col)
            for (int row=0; row<7; ++row)
                if ((g[col] & (1U << row)) != 0U)
                    fill_rect(x + col*scale, y + row*scale, scale, scale, on);
        x += 6 * scale;
    }
}

static void draw_text(int x, int y, const char *s, int scale)
{
    draw_text_colour(x, y, s, scale, true);
}

/* The PCB mark is lower-case Ubuntu Mono Bold. The display face is deliberately
 * small, but using the same lower-case monospaced construction is much closer
 * to the silkscreen than the previous geometric pseudo-logo. A one-pixel second
 * pass gives the mark the heavier silkscreen weight without adding another font. */
static void draw_kiku_wordmark_scaled(int x, int y, int scale, bool on)
{
    draw_text_colour(x, y, "kiku", scale, on);
    draw_text_colour(x + 1, y, "kiku", scale, on);
}

static void draw_centered(int y, const char *s, int scale)
{
    int x = ((int)s_w - text_width(s, scale)) / 2;
    if (x < 4) x = 4;
    draw_text(x, y, s, scale);
}

static void draw_text_fit_colour(int x, int y, int width, const char *s, int scale,
                                 bool on)
{
    if (s == NULL || width <= 0 || scale <= 0) return;
    size_t cap = (size_t)(width / (6 * scale));
    if (cap == 0U) return;
    char line[64];
    size_t len = strlen(s);
    size_t n = len < cap ? len : cap;
    if (n >= sizeof(line)) n = sizeof(line) - 1U;
    memcpy(line, s, n);
    line[n] = '\0';
    if (len > n && n >= 3U) {
        line[n-3U] = '.'; line[n-2U] = '.'; line[n-1U] = '.';
    }
    draw_text_colour(x, y, line, scale, on);
}

static void draw_text_fit(int x, int y, int width, const char *s, int scale)
{
    draw_text_fit_colour(x, y, width, s, scale, true);
}

static void draw_title_two_lines(int x, int y, int width, const char *s)
{
    if (s == NULL || s[0] == '\0') return;
    size_t max_chars = (size_t)(width / 12); /* 6 px glyph pitch * scale 2 */
    if (max_chars < 4U) return;
    size_t len = strlen(s);
    if (len <= max_chars) { draw_text(x, y, s, 2); return; }

    size_t split = max_chars;
    while (split > max_chars / 2U && s[split] != ' ') --split;
    if (split <= max_chars / 2U) split = max_chars;
    char first[32];
    size_t n = split < sizeof(first)-1U ? split : sizeof(first)-1U;
    memcpy(first, s, n); first[n] = '\0';
    draw_text(x, y, first, 2);
    const char *second = s + split;
    while (*second == ' ') ++second;
    draw_text_fit(x, y + 18, width, second, 2);
}

static void draw_play_icon(int x, int y)
{
    for (int row=0; row<15; ++row) {
        int half = row < 8 ? row : 14-row;
        fill_rect(x, y+row, half+1, 1, true);
    }
}

static uint16_t ease_smootherstep(uint16_t progress)
{
    uint64_t p = progress > 1000U ? 1000U : progress;
    /* Quintic smootherstep: zero velocity and zero acceleration at both ends.
     * The physical backlight supplies the luminance fade; this curve makes the
     * small geometric reveal settle without the mechanical-looking stop/start
     * of the old progress animation. */
    uint64_t p2 = p * p;
    uint64_t p3 = p2 * p;
    uint64_t eased = (p3 * (10000000ULL + p * (6ULL * p - 15000ULL)) +
                       500000000000ULL) / 1000000000000ULL;
    if (eased > 1000ULL) eased = 1000ULL;
    return (uint16_t)eased;
}

static void draw_transition_mark(const ui_state_t *ui, bool reverse)
{
    /* Product transitions deliberately contain only the kiku mark. The mark
     * reveals from its centre while settling vertically; unlike the previous
     * 10-pixel drift, this produces obvious motion even on a small 320x240 LCD
     * without turning the boot screen into a progress indicator. */
    memset(s_fb, 0xFF, sizeof(s_fb));

    uint16_t eased = ease_smootherstep(ui->animation_progress);
    uint16_t visible = reverse ? (uint16_t)(1000U - eased) : eased;

    const int logo_scale = 4;
    const int logo_w = text_width("kiku", logo_scale);
    const int logo_x = ((int)s_w - logo_w) / 2;
    const int logo_h = 7 * logo_scale;
    const int logo_rest_y = 94;
    int travel = (int)((1000U - visible) * 18U / 1000U);
    int logo_y = reverse ? logo_rest_y - travel : logo_rest_y + travel;
    draw_kiku_wordmark_scaled(logo_x, logo_y, logo_scale, false);

    int reveal_w = 4 + (int)(((uint32_t)(logo_w - 4) * visible) / 1000U);
    int reveal_x0 = ((int)s_w - reveal_w) / 2;
    int reveal_x1 = reveal_x0 + reveal_w;
    if (reveal_x0 > logo_x) fill_rect(logo_x, logo_y, reveal_x0 - logo_x, logo_h, true);
    if (reveal_x1 < logo_x + logo_w)
        fill_rect(reveal_x1, logo_y, logo_x + logo_w - reveal_x1, logo_h, true);

    /* A hairline shares the same centre-out motion and gives the eye a smooth
     * sub-character-width cue between glyph changes. */
    int line_w = (int)(((uint32_t)logo_w * visible) / 1000U);
    if (line_w > 0) line_h(((int)s_w - line_w) / 2, logo_y + logo_h + 10, line_w, false);
}

static void render_boot(const ui_state_t *ui)
{
    draw_transition_mark(ui, false);
}

static void render_powering_off(const ui_state_t *ui)
{
    draw_transition_mark(ui, true);
}

static void render_powering_on(const ui_state_t *ui)
{
    draw_transition_mark(ui, false);
}

static void draw_header(const ui_state_t *ui, const char *page)
{
    /* Hidden themes change physical information architecture rather than merely
     * recolouring the same chrome. Content renderers can therefore stay shared
     * while every page still reads as a genuinely different product UI. */
    if (ui->theme == UI_THEME_POLAROID) {
        draw_kiku_wordmark_scaled(14, 6, 2, true);
        if (page != NULL) draw_text_fit(14, 24, 190, page, 1);
    } else if (ui->theme == UI_THEME_WALKMAN) {
        draw_kiku_wordmark_scaled(14, 7, 1, true);
        draw_text(68, 7, "PORTABLE AUDIO", 1);
        if (page != NULL) draw_text_fit(14, 23, 210, page, 1);
    } else if (ui->theme == UI_THEME_INSTRUMENT) {
        draw_box(9, 4, 54, 22, true);
        draw_text(18, 11, "KIKU", 1);
        if (page != NULL) draw_text_fit(75, 8, 150, page, 2);
        line_h(9, 31, (int)s_w - 18, true);
        line_h(9, 34, (int)s_w - 18, true);
    } else if (ui->theme == UI_THEME_MINIDISC) {
        fill_rect(10, 5, 29, 21, true);
        draw_text_colour(16, 12, "MD", 1, false);
        if (page != NULL) draw_text_fit(51, 9, 170, page, 1);
        draw_kiku_wordmark_scaled((int)s_w - 63, 7, 1, true);
        line_h(10, 30, (int)s_w - 20, true);
        line_v(44, 4, 22, true);
    } else if (ui->theme == UI_THEME_TERMINAL) {
        draw_text(10, 7, "KIKU SYS", 1);
        if (page != NULL) {
            draw_text(72, 7, "/", 1);
            draw_text_fit(84, 7, 156, page, 1);
        }
        draw_text((int)s_w - 46, 7, "01", 1);
        line_h(10, 22, (int)s_w - 20, true);
        line_h(10, 25, 62, true);
    } else if (ui->theme == UI_THEME_AQUA) {
        draw_box(9, 4, (int)s_w - 18, 25, true);
        draw_kiku_wordmark_scaled(17, 9, 1, true);
        if (page != NULL) draw_text_fit(78, 9, 165, page, 1);
        fill_rect((int)s_w - 44, 10, 24, 12, true);
        draw_text_colour((int)s_w - 39, 13, "UI", 1, false);
        line_h(18, 33, (int)s_w - 36, true);
    } else {
        /* Shipping kiku UI: one quiet identity row and no dashboard chrome.
         * The display should feel like a listening product, not a status panel. */
        draw_kiku_wordmark_scaled(14, 7, 2, true);
        if (page != NULL) draw_text_fit(78, 11, 145, page, 1);
    }
    if (ui->battery_mv != 0U) {
        char bat[16];
        snprintf(bat, sizeof(bat), "%u%%%s", ui->battery_centi_percent / 100U,
                 ui->charging ? "+" : "");
        int bx = (int)s_w - text_width(bat, 1) - 10;
        if (ui->theme != UI_THEME_MINIDISC && ui->theme != UI_THEME_TERMINAL &&
            ui->theme != UI_THEME_AQUA)
            draw_text(bx, 9, bat, 1);
        else
            draw_text(bx, 34, bat, 1);
    }
}

static void draw_controls(const ui_state_t *ui, const char *left,
                          const char *centre, const char *right,
                          bool left_hold_back)
{
    if (ui->theme == UI_THEME_POLAROID) {
        draw_box(10, 213, 94, 23, true);
        draw_box(113, 213, 94, 23, true);
        draw_box(216, 213, 94, 23, true);
    } else if (ui->theme == UI_THEME_WALKMAN) {
        line_h(10, 212, (int)s_w - 20, true);
        line_v(106, 212, 24, true);
        line_v(213, 212, 24, true);
    } else if (ui->theme == UI_THEME_INSTRUMENT) {
        line_h(9, 213, (int)s_w - 18, true);
        line_h(9, 216, (int)s_w - 18, true);
        line_v(108, 216, 24, true);
        line_v(211, 216, 24, true);
    } else if (ui->theme == UI_THEME_MINIDISC) {
        fill_rect(10, 215, (int)s_w - 20, 2, true);
        fill_rect(10, 220, 36, 15, true);
    } else if (ui->theme == UI_THEME_TERMINAL) {
        draw_text(10, 211, ">", 1);
        line_h(22, 216, (int)s_w - 32, true);
    } else if (ui->theme == UI_THEME_AQUA) {
        draw_box(9, 215, (int)s_w - 18, 21, true);
        line_v(108, 215, 21, true);
        line_v(211, 215, 21, true);
    } else {
        /* Three short rails map directly to the physical left button, encoder
         * and right button without printing hardware names. */
        line_h(12, 215, 82, true);
        line_h(119, 215, 82, true);
        line_h(226, 215, 82, true);
    }
    if (left_hold_back) draw_text_fit(12, 203, 94, "HOLD BACK", 1);
    if (left != NULL && left[0] != '\0') {
        if (ui->theme == UI_THEME_MINIDISC) {
            /* The MiniDisc footer uses a filled ink tab for the left action.
             * Render its label in paper colour so PREV/BACK/SEEK- cannot vanish
             * into the tab itself. The 32 px fit stays inside the 36 px fill. */
            draw_text_fit_colour(12, 224, 32, left, 1, false);
        } else {
            draw_text_fit(12, 224, 94, left, 1);
        }
    }
    if (centre != NULL && centre[0] != '\0') {
        int width = text_width(centre, 1);
        int x = 160 - width / 2;
        if (x < 112) x = 112;
        draw_text_fit(x, 224, 96, centre, 1);
    }
    if (right != NULL && right[0] != '\0') {
        int width = text_width(right, 1);
        if (width > 94) width = 94;
        draw_text_fit((int)s_w - 12 - width, 224, 94, right, 1);
    }
}

static void draw_signal_bars(int x, int y, uint8_t rssi)
{
    /* Quiet four-bar RF indicator for the listening screen. Raw RSSI/SNR remain
     * available on Diagnostics; the main FM page should communicate quality,
     * not look like laboratory firmware. */
    static const uint8_t thresholds[4] = { 5U, 12U, 20U, 30U };
    for (unsigned i = 0U; i < 4U; ++i) {
        int h = 4 + (int)i * 3;
        if (rssi >= thresholds[i]) fill_rect(x + (int)i * 6, y + 13 - h, 3, h, true);
        else {
            line_h(x + (int)i * 6, y + 12, 3, true);
        }
    }
}

static void draw_menu_row(int y, const char *label, bool selected)
{
    if (selected) {
        fill_rect(14, y + 5, 5, 5, true);
        draw_text_fit(30, y, (int)s_w - 48, label, 2);
    } else {
        draw_text_fit(30, y + 4, (int)s_w - 48, label, 1);
    }
}

static void format_duration(uint32_t ms, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    if (ms == 0U) { out[0] = '\0'; return; }
    uint32_t sec = ms / 1000U;
    snprintf(out, out_size, "%lu:%02lu", (unsigned long)(sec / 60U),
             (unsigned long)(sec % 60U));
}

static void draw_track_details(const ui_state_t *ui, int x, int y, int width)
{
    char line[48] = {0};
    char duration[16] = {0};
    format_duration(ui->track_duration_ms, duration, sizeof(duration));
    if (ui->track_number != 0U && ui->track_total != 0U && duration[0] != '\0') {
        snprintf(line, sizeof(line), "%u/%u  %s", ui->track_number, ui->track_total, duration);
    } else if (ui->track_number != 0U && ui->track_total != 0U) {
        snprintf(line, sizeof(line), "%u/%u", ui->track_number, ui->track_total);
    } else if (duration[0] != '\0') {
        snprintf(line, sizeof(line), "%s", duration);
    }
    if (line[0] != '\0') draw_text_fit(x, y, width, line, 1);
    if (ui->track_genre[0] != '\0') draw_text_fit(x, y + 14, width, ui->track_genre, 1);
}

static void render_home(const ui_state_t *ui)
{
    static const char *const names[] = { "Music", "FM radio", "Bluetooth", "Output", "Settings" };
    uint8_t selected = ui->menu_index < 5U ? ui->menu_index : 0U;

    if (ui->theme == UI_THEME_POLAROID) {
        draw_header(ui, NULL);
        draw_box(18, 50, 202, 132, true);
        char slot[8];
        snprintf(slot, sizeof(slot), "%02u / 05", (unsigned)(selected + 1U));
        draw_text(31, 64, slot, 1);
        draw_text_fit(31, 96, 174, names[selected], 3);
        line_h(31, 137, 58, true);
        draw_text(238, 62, "SOURCE", 1);
        for (uint8_t i = 0U; i < 5U; ++i) {
            int y = 88 + (int)i * 20;
            if (i == selected) fill_rect(238, y + 2, 5, 5, true);
            char n[4];
            snprintf(n, sizeof(n), "%02u", (unsigned)(i + 1U));
            draw_text(251, y, n, 1);
        }
        draw_controls(ui, NULL, "OPEN", NULL, false);
        return;
    }

    if (ui->theme == UI_THEME_WALKMAN) {
        draw_header(ui, "SOURCE");
        draw_box(14, 52, 96, 142, true);
        for (uint8_t i = 0U; i < 5U; ++i) {
            int y = 66 + (int)i * 25;
            if (i == selected) fill_rect(23, y - 2, 7, 14, true);
            draw_text_fit(40, y, 58, names[i], 1);
        }
        draw_text(132, 61, "SELECTED", 1);
        draw_text_fit(132, 88, 174, names[selected], 3);
        char slot[8];
        snprintf(slot, sizeof(slot), "%u OF 5", (unsigned)(selected + 1U));
        draw_text(132, 139, slot, 1);
        line_h(132, 159, 146, true);
        draw_controls(ui, NULL, "OPEN", NULL, false);
        return;
    }

    if (ui->theme == UI_THEME_INSTRUMENT) {
        draw_header(ui, "SOURCE BANK");
        draw_text(14, 48, "INPUT", 1);
        draw_box(12, 62, 82, 126, true);
        for (uint8_t i = 0U; i < 5U; ++i) {
            char n[4];
            snprintf(n, sizeof(n), "%02u", (unsigned)(i + 1U));
            int y = 73 + (int)i * 22;
            if (ui->menu_index == i) fill_rect(20, y - 4, 28, 16, true);
            draw_text_colour(26, y, n, 1, ui->menu_index != i);
            draw_text_fit(112, y, 180, names[i], ui->menu_index == i ? 2 : 1);
        }
        line_v(104, 62, 126, true);
        draw_controls(ui, NULL, "OPEN", NULL, false);
        return;
    }

    if (ui->theme == UI_THEME_MINIDISC) {
        draw_header(ui, "SOURCE");
        char slot[8];
        snprintf(slot, sizeof(slot), "%02u", (unsigned)(ui->menu_index + 1U));
        draw_text(14, 55, slot, 4);
        line_v(72, 47, 143, true);
        draw_text_fit(91, 57, 205, names[ui->menu_index < 5U ? ui->menu_index : 0U], 3);
        draw_text(91, 91, "SELECTED SOURCE", 1);
        line_h(91, 108, 198, true);
        for (uint8_t i = 0U; i < 5U; ++i) {
            int y = 121 + (int)i * 15;
            if (i == ui->menu_index) draw_text(91, y, ">", 1);
            draw_text_fit(105, y, 175, names[i], 1);
        }
        draw_controls(ui, NULL, "OPEN", NULL, false);
        return;
    }

    if (ui->theme == UI_THEME_TERMINAL) {
        draw_header(ui, "SOURCE SELECT");
        draw_text(12, 42, "READY. CHOOSE INPUT", 1);
        for (uint8_t i = 0U; i < 5U; ++i) {
            char row[32];
            snprintf(row, sizeof(row), "%c %02u  %s", ui->menu_index == i ? '>' : ' ',
                     (unsigned)(i + 1U), names[i]);
            draw_text(18, 68 + (int)i * 25, row, ui->menu_index == i ? 2 : 1);
        }
        draw_controls(ui, NULL, "EXEC", NULL, false);
        return;
    }

    if (ui->theme == UI_THEME_AQUA) {
        draw_header(ui, "SOURCES");
        static const int x[5] = { 14, 164, 14, 164, 89 };
        static const int y[5] = { 48, 48, 105, 105, 162 };
        for (uint8_t i = 0U; i < 5U; ++i) {
            int w = 142;
            int h = 46;
            draw_box(x[i], y[i], w, h, true);
            if (ui->menu_index == i) draw_box(x[i] + 3, y[i] + 3, w - 6, h - 6, true);
            int tx = x[i] + (w - text_width(names[i], 1)) / 2;
            draw_text(tx, y[i] + 19, names[i], 1);
        }
        draw_controls(ui, NULL, "OPEN", NULL, false);
        return;
    }

    /* Default home is a source carousel, not a settings-style list. One source
     * owns the visual hierarchy while the five tiny index labels preserve
     * orientation during encoder movement. */
    draw_header(ui, NULL);
    draw_text(16, 49, "SOURCE", 1);
    char slot[8];
    snprintf(slot, sizeof(slot), "%02u / 05", (unsigned)(selected + 1U));
    int sx = (int)s_w - text_width(slot, 1) - 16;
    draw_text(sx, 49, slot, 1);
    draw_text_fit(16, 78, (int)s_w - 32, names[selected], 4);
    int hero_w = text_width(names[selected], 4);
    if (hero_w > (int)s_w - 32) hero_w = (int)s_w - 32;
    line_h(16, 116, hero_w, true);

    static const char *const tabs[] = { "MUS", "FM", "BT", "OUT", "SET" };
    for (uint8_t i = 0U; i < 5U; ++i) {
        int cell_x = (int)i * 64;
        int tx = cell_x + (64 - text_width(tabs[i], 1)) / 2;
        draw_text(tx, 164, tabs[i], 1);
        if (i == selected) fill_rect(cell_x + 20, 180, 24, 2, true);
    }
    draw_controls(ui, NULL, "OPEN", NULL, false);
}

static void render_library(const ui_state_t *ui)
{
    draw_header(ui, "MUSIC OPTIONS");
    if (!ui->storage_mounted) {
        draw_text(16, 52, "No SD card", 2);
        draw_text(16, 80, "Insert microSD", 1);
    } else {
        draw_text(16, 49, "LOCAL MUSIC", 1);
        if (ui->track_title[0]) draw_text_fit(16, 66, (int)s_w - 32, ui->track_title, 1);
    }
    draw_menu_row(108, "Now playing", ui->menu_index == 0U);
    char shuffle[32];
    snprintf(shuffle, sizeof(shuffle), "Shuffle  %s", ui->shuffle_enabled ? "On" : "Off");
    draw_menu_row(154, shuffle, ui->menu_index == 1U);
    draw_controls(ui, "BACK", "OPEN", NULL, false);
}

static void render_music(const ui_state_t *ui)
{
    draw_header(ui, "MUSIC");
    if (!ui->storage_mounted) {
        draw_centered(89, "Insert SD card", 2);
        draw_centered(122, "MP3 / WAV", 1);
    } else {
        int mx = 16, mw = (int)s_w - 32;
        draw_text(mx, 48, ui->playing ? "PLAYING" : "PAUSED", 1);
        if (ui->shuffle_enabled) {
            int sw = text_width("SHUFFLE", 1);
            draw_text((int)s_w - sw - 16, 48, "SHUFFLE", 1);
        }
        if (ui->track_title[0]) draw_title_two_lines(mx, 72, mw, ui->track_title);
        if (ui->track_artist[0]) draw_text_fit(mx, 119, mw, ui->track_artist, 2);
        if (ui->track_album[0]) draw_text_fit(mx, 145, mw, ui->track_album, 1);
        /* The glyph shows the action a centre press will perform. */
        if (ui->playing) { fill_rect(16, 174, 4, 15, true); fill_rect(25, 174, 4, 15, true); }
        else draw_play_icon(16, 174);
        char volume[16];
        snprintf(volume, sizeof(volume), "VOL %u", ui->volume_percent);
        int vx = (int)s_w - text_width(volume, 1) - 16;
        /* Reserve the lower-right corner for volume. Genre shares the y=191
         * information row but is clipped before the volume label, so a long
         * genre can never overprint VOL xxx. Track number/duration stays on the
         * independent row above. */
        int detail_x = 47;
        int detail_w = vx - detail_x - 8;
        if (detail_w < 0) detail_w = 0;
        draw_track_details(ui, detail_x, 177, detail_w);
        draw_text(vx, 191, volume, 1);
    }
    draw_controls(ui, "PREV", ui->playing ? "PAUSE" : "PLAY", "NEXT", true);
}

static void render_bluetooth(const ui_state_t *ui)
{
    draw_header(ui, "BLUETOOTH");
    if (!ui->bluetooth_connected) {
        draw_text(16, 70, "Not connected", 2);
        draw_text(16, 100, "Bluetooth audio", 1);
    } else {
        int mx = 16, mw = (int)s_w - 32;
        draw_text(mx, 48, ui->bt_avrcp_connected ? "PHONE AUDIO" : "CONNECTED", 1);
        if (ui->track_title[0]) draw_title_two_lines(mx, 72, mw, ui->track_title);
        if (ui->track_artist[0]) draw_text_fit(mx, 119, mw, ui->track_artist, 2);
        if (ui->track_album[0]) draw_text_fit(mx, 145, mw, ui->track_album, 1);
        char volume[16];
        snprintf(volume, sizeof(volume), "VOL %u", ui->volume_percent);
        int vx = (int)s_w - text_width(volume, 1) - 16;
        int detail_w = vx - mx - 8;
        if (detail_w < 0) detail_w = 0;
        draw_track_details(ui, mx, 177, detail_w);
        draw_text(vx, 191, volume, 1);
    }
    draw_controls(ui, ui->bluetooth_connected ? "PREV" : "BACK",
                  ui->bluetooth_connected ? "PLAY / PAUSE" : NULL,
                  ui->bluetooth_connected ? "NEXT" : "HOLD PAIR",
                  ui->bluetooth_connected);
}

static void render_fm(const ui_state_t *ui)
{
    draw_header(ui, "FM RADIO");
    char f[16];
    snprintf(f, sizeof(f), "%u.%u", ui->fm.frequency_10khz / 100U,
             (ui->fm.frequency_10khz % 100U) / 10U);
    draw_text(16, 65, f, 5);
    int unit_x = 16 + text_width(f, 5) + 8;
    if (unit_x < (int)s_w - 42) draw_text(unit_x, 91, "MHz", 1);

    char volume[16];
    snprintf(volume, sizeof(volume), "VOL %u", ui->volume_percent);
    draw_text(16, 47, volume, 1);
    draw_signal_bars((int)s_w - 42, 47, ui->fm.rssi_dbuv);

    const char *station = fm_state_has_partial_program_service(&ui->fm) ? ui->fm.program_service :
                          (ui->fm_preset_name[0] != '\0' ? ui->fm_preset_name : NULL);
    if (station != NULL) draw_text_fit(16, 116, (int)s_w - 32, station, 2);

    if (fm_state_has_rtplus_title(&ui->fm)) {
        draw_text_fit(16, 150, (int)s_w - 32, ui->fm.rtplus_title, 2);
        if (fm_state_has_rtplus_artist(&ui->fm))
            draw_text_fit(16, 178, (int)s_w - 32, ui->fm.rtplus_artist, 1);
    } else if (fm_state_has_radio_text(&ui->fm)) {
        draw_text_fit(16, 158, (int)s_w - 32, ui->fm.radio_text, 1);
    }
    draw_controls(ui, "SEEK-",
                  ui->fm_seeking ? "SEEKING" :
                  (ui->fm_control_mode == UI_FM_CONTROL_VOLUME ? "VOL" : "FREQ"),
                  "SEEK+", true);
}

static void render_output(const ui_state_t *ui)
{
    static const char *const names[] = { "Auto", "Headphones", "Speaker", "Both" };
    draw_header(ui, "OUTPUT");
    draw_text(16, 49, "AUDIO ROUTING", 1);
    for (uint8_t i = 0U; i < 4U; ++i) draw_menu_row(72 + (int)i * 34, names[i], ui->menu_index == i);
    draw_controls(ui, "BACK", "CHANGE", NULL, false);
}

static void render_system(const ui_state_t *ui)
{
    draw_header(ui, "DIAGNOSTICS");
    char line[64];
    draw_text(14, 41, "STATUS", 1);
    line_h(14, 54, (int)s_w - 28, true);
    snprintf(line, sizeof(line), "Battery   %u%%  %u.%02uV%s",
             ui->battery_centi_percent / 100U, ui->battery_mv / 1000U,
             (ui->battery_mv % 1000U) / 10U, ui->charging ? " CHG" : "");
    draw_text(14, 67, line, 1);
    snprintf(line, sizeof(line), "USB       %s  %u.%02uV %umA",
             ui->usb_power_good ? "PG" : "--", ui->usb_vbus_mv / 1000U,
             (ui->usb_vbus_mv % 1000U) / 10U, ui->charge_current_ma);
    draw_text(14, 88, line, 1);
    snprintf(line, sizeof(line), "BQ        ST %02X  FLT %02X",
             ui->charger_status_raw, ui->charger_fault_raw);
    draw_text(14, 109, line, 1);
    snprintf(line, sizeof(line), "Library   %s", ui->storage_mounted ? "SD mounted" : "No card");
    draw_text(14, 130, line, 1);
    snprintf(line, sizeof(line), "Bluetooth %s", ui->bluetooth_connected ? "Connected" : "Offline");
    draw_text(14, 151, line, 1);
    snprintf(line, sizeof(line), "Errors    %08lX", (unsigned long)ui->error_flags);
    draw_text(14, 172, line, 1);
    draw_controls(ui, "BACK", NULL, NULL, false);
}

static void render_settings(const ui_state_t *ui)
{
    draw_header(ui, "SETTINGS");
    char line[48];
    snprintf(line, sizeof(line), "Power save  %s", ui->power_save_enabled ? "On" : "Off");
    draw_menu_row(78, line, ui->menu_index == 0U);
    draw_menu_row(130, "Status", ui->menu_index == 1U);
    draw_controls(ui, "BACK", "OPEN", NULL, false);
}

static void render_error(const ui_state_t *ui)
{
    draw_header(ui, "ERROR");
    char line[32];
    snprintf(line, sizeof(line), "CODE %08lX", (unsigned long)ui->error_flags);
    draw_centered(91, line, 2);
    draw_controls(ui, "BACK", NULL, NULL, false);
}

static void render_screen(const ui_state_t *ui)
{
    memset(s_fb, 0, sizeof(s_fb));
    switch (ui->screen) {
    case UI_SCREEN_HOME: render_home(ui); break;
    case UI_SCREEN_BOOT: render_boot(ui); break;
    case UI_SCREEN_NOW_PLAYING: render_music(ui); break;
    case UI_SCREEN_LIBRARY: render_library(ui); break;
    case UI_SCREEN_FM: render_fm(ui); break;
    case UI_SCREEN_BLUETOOTH: render_bluetooth(ui); break;
    case UI_SCREEN_OUTPUT: render_output(ui); break;
    case UI_SCREEN_SETTINGS: render_settings(ui); break;
    case UI_SCREEN_DIAGNOSTICS: render_system(ui); break;
    case UI_SCREEN_POWERING_OFF: render_powering_off(ui); break;
    case UI_SCREEN_POWERING_ON: render_powering_on(ui); break;
    case UI_SCREEN_SLEEP: break;
    case UI_SCREEN_ERROR:
    default: render_error(ui); break;
    }
}

static bool fb_ink_at(const uint8_t *fb, uint16_t x, uint16_t y)
{
    size_t n = (size_t)y * UI_MAX_W + x;
    return (fb[n >> 3] & (uint8_t)(1U << (7U - (n & 7U)))) != 0U;
}

static uint16_t theme_paper(ui_theme_t theme)
{
    switch (theme) {
    case UI_THEME_POLAROID: return RGB565(247U, 241U, 225U);
    case UI_THEME_WALKMAN:  return RGB565(232U, 233U, 226U);
    case UI_THEME_INSTRUMENT: return RGB565(235U, 231U, 216U);
    case UI_THEME_MINIDISC: return RGB565(21U, 25U, 31U);
    case UI_THEME_TERMINAL: return RGB565(2U, 12U, 5U);
    case UI_THEME_AQUA: return RGB565(218U, 247U, 248U);
    case UI_THEME_KIKU:
    default: return 0xFFFFU;
    }
}

static uint16_t theme_ink(ui_theme_t theme)
{
    switch (theme) {
    case UI_THEME_POLAROID: return RGB565(38U, 33U, 30U);
    case UI_THEME_WALKMAN:  return RGB565(21U, 31U, 42U);
    case UI_THEME_INSTRUMENT: return RGB565(39U, 39U, 34U);
    case UI_THEME_MINIDISC: return RGB565(218U, 230U, 235U);
    case UI_THEME_TERMINAL: return RGB565(90U, 255U, 120U);
    case UI_THEME_AQUA: return RGB565(15U, 47U, 70U);
    case UI_THEME_KIKU:
    default: return 0x0000U;
    }
}

static bool theme_accent_at(ui_theme_t theme, uint16_t x, uint16_t y,
                            uint16_t *colour)
{
    if (colour == NULL) return false;
    if (theme == UI_THEME_POLAROID && y >= 32U && y <= 35U && x >= 12U && x < 132U) {
        static const uint16_t rainbow[5] = {
            RGB565(230U, 63U, 55U), RGB565(242U, 132U, 45U),
            RGB565(244U, 199U, 55U), RGB565(77U, 153U, 83U),
            RGB565(52U, 112U, 176U)
        };
        uint16_t band = (uint16_t)((x - 12U) / 24U);
        if (band > 4U) band = 4U;
        *colour = rainbow[band];
        return true;
    }
    if (theme == UI_THEME_WALKMAN && x >= 12U && x < 132U) {
        if (y >= 32U && y <= 33U) {
            *colour = RGB565(238U, 103U, 42U);
            return true;
        }
        if (y >= 34U && y <= 35U) {
            *colour = RGB565(54U, 126U, 174U);
            return true;
        }
    }
    if (theme == UI_THEME_INSTRUMENT && y >= 31U && y <= 34U && x >= 9U && x < s_w - 9U) {
        *colour = RGB565(224U, 86U, 43U);
        return true;
    }
    if (theme == UI_THEME_MINIDISC && y >= 30U && y <= 32U && x >= 10U && x < s_w - 10U) {
        *colour = RGB565(55U, 190U, 214U);
        return true;
    }
    if (theme == UI_THEME_TERMINAL && y >= 22U && y <= 25U && x >= 10U && x < 72U) {
        *colour = RGB565(52U, 178U, 83U);
        return true;
    }
    if (theme == UI_THEME_AQUA && y >= 33U && y <= 34U && x >= 18U && x < s_w - 18U) {
        *colour = RGB565(57U, 168U, 214U);
        return true;
    }
    return false;
}

static dev_status_t flush_mono_region(st7789_t *lcd, const ui_state_t *ui,
                                      uint16_t x0, uint16_t y0,
                                      uint16_t x1, uint16_t y1)
{
    dev_status_t st = st7789_set_window(lcd, x0, y0, x1, y1);
    if (st != DEV_OK) return st;
    uint16_t line[UI_MAX_W];
    uint16_t width = (uint16_t)(x1 - x0 + 1U);
    uint16_t paper = theme_paper(ui->theme);
    uint16_t ink = theme_ink(ui->theme);
    for (uint16_t y = y0; y <= y1; ++y) {
        for (uint16_t x = x0; x <= x1; ++x) {
            if (fb_ink_at(s_fb, x, y)) {
                line[x - x0] = ink;
            } else {
                uint16_t accent = 0U;
                line[x - x0] = theme_accent_at(ui->theme, x, y, &accent) ? accent : paper;
            }
        }
        st = st7789_write_pixels_rgb565(lcd, line, width);
        if (st != DEV_OK) return st;
    }
    return DEV_OK;
}

dev_status_t ui_renderer_draw(st7789_t *lcd, const ui_state_t *ui)
{
    if (lcd == NULL || ui == NULL || lcd->width == 0U || lcd->height == 0U ||
        lcd->width > UI_MAX_W || lcd->height > UI_MAX_H) return DEV_EINVAL;

    uint32_t h = ui_hash(ui);
    bool dimensions_changed = s_w != lcd->width || s_h != lcd->height;
    if (s_have_frame && h == s_last_hash && !dimensions_changed) return DEV_OK;
    s_w = lcd->width;
    s_h = lcd->height;
    render_screen(ui);

    uint16_t min_x = 0U, min_y = 0U;
    uint16_t max_x = (uint16_t)(s_w - 1U), max_y = (uint16_t)(s_h - 1U);
    bool have_dirty = true;
    bool theme_changed = s_prev_valid && ui->theme != s_last_theme;
    if (s_prev_valid && !dimensions_changed && !theme_changed) {
        min_x = s_w;
        min_y = s_h;
        max_x = 0U;
        max_y = 0U;
        have_dirty = false;
        for (uint16_t y = 0U; y < s_h; ++y) {
            for (uint16_t x = 0U; x < s_w; ++x) {
                if (fb_ink_at(s_fb, x, y) == fb_ink_at(s_prev_fb, x, y)) continue;
                if (!have_dirty) {
                    min_x = max_x = x;
                    min_y = max_y = y;
                    have_dirty = true;
                } else {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
    }

    dev_status_t st = DEV_OK;
    if (have_dirty) st = flush_mono_region(lcd, ui, min_x, min_y, max_x, max_y);
    if (st == DEV_OK) {
        memcpy(s_prev_fb, s_fb, sizeof(s_prev_fb));
        s_prev_valid = true;
        s_last_hash = h;
        s_last_theme = ui->theme;
        s_have_frame = true;
    }
    return st;
}

void ui_renderer_invalidate(void)
{
    s_have_frame = false;
    s_prev_valid = false;
}
