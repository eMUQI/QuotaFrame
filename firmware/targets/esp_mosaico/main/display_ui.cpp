#include "display_ui.hpp"

#include <cstdio>
#include <initializer_list>

#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "usage_fonts/usage_fonts.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "usage_panel_state/usage_bar_animation.hpp"
#include "usage_ota/presentation.hpp"

namespace usage_panel::mosaico {
namespace {
constexpr char TAG[] = "mosaico_ui";
constexpr uint32_t COLOR_BG     = 0x0A0C10;
// True black keeps background pixels unlit during prolonged screensaver use.
constexpr uint32_t COLOR_SAVER_BG = 0x000000;
constexpr uint32_t COLOR_PANEL  = 0x171B22;
constexpr uint32_t COLOR_BORDER = 0x272C35;
constexpr uint32_t COLOR_LINE   = 0x1D2128;
constexpr uint32_t COLOR_TRACK  = 0x252A33;
constexpr uint32_t COLOR_TEXT   = 0xF2F4F7;
constexpr uint32_t COLOR_MUTED  = 0x8B93A1;
constexpr uint32_t COLOR_DIM    = 0x565D68;
constexpr uint32_t COLOR_RESET  = 0xC7CCD4;
constexpr uint32_t COLOR_GOOD   = 0x3ECF8E;
constexpr uint32_t COLOR_WARN   = 0xF5B84C;
constexpr uint32_t COLOR_BAD    = 0xF26D6D;
constexpr uint32_t COLOR_BATTERY_OFF = 0x2C313B;
constexpr uint32_t COLOR_LOW_DIM = 0x8A6A2E;
// Screensaver usage underline colours. A thin underline needs more contrast
// than the 120 px digits to stay legible at viewing distance, so the base fill
// matches COLOR_DIM (the dimmest reliably legible element) while the track is
// dimmer still to keep a visible reference length.
constexpr uint32_t COLOR_SAVER_TRACK = 0x2C313B;
constexpr uint32_t COLOR_SAVER_BASE  = COLOR_DIM;
constexpr uint32_t COLOR_SAVER_WARN  = 0x737B86;
constexpr uint32_t COLOR_SAVER_BAD   = COLOR_MUTED;
constexpr int MARGIN      = 24;
constexpr int CARD_W      = 432;
constexpr int CARD_PAD    = 20;
constexpr int OVERVIEW_PAD = 18;
constexpr int CARD_RADIUS = 16;
constexpr int HEADER_H    = 50;
constexpr int BAR_H_OVERVIEW = 12;
constexpr int BAR_H_DETAIL   = 16;
constexpr int NAV_H       = 34;
constexpr int NAV_Y       = 436;
constexpr int NAV_GAP     = 6;
constexpr int NAV_BAR_W   = 26;
constexpr int NAV_BAR_H   = 2;
constexpr int NAV_LABEL_GAP = 7;
// The three tabs share the row with flex-grow 1, so their width is exact only
// while the remaining space divides evenly. The shared underline is placed from
// these numbers rather than from resolved geometry, which keeps it independent
// of when the flex pass runs; the assert is what makes that safe.
static_assert((CARD_W - 2 * NAV_GAP) % 3 == 0,
              "navigation tabs must divide the row evenly");
constexpr int NAV_TAB_W   = (CARD_W - 2 * NAV_GAP) / 3;
constexpr int NAV_TAB_PITCH = NAV_TAB_W + NAV_GAP;
constexpr int NAV_BAR_X   = (NAV_TAB_W - NAV_BAR_W) / 2;
constexpr int PAIRING_BOTTOM = 52;
constexpr int SCREEN_W    = 480;
constexpr int SCREEN_H    = 480;
constexpr int BATTERY_GROUP_W = 44;
constexpr int BATTERY_BODY_W = 30;
constexpr int BATTERY_BODY_H = 14;
constexpr int CONTENT_Y   = HEADER_H + 1;
constexpr int CONTENT_H   = NAV_Y - CONTENT_Y;
constexpr int OTA_CARD_Y         = 98;
constexpr int OTA_CARD_H         = 212;
constexpr int OTA_CARD_ROW_Y     = CARD_PAD;
constexpr int OTA_CARD_VALUE_Y   = 74;
constexpr int OTA_CARD_THIRD_ROW_Y = 142;
constexpr int OTA_CARD_HINT_Y    = 172;
constexpr int OTA_BUTTON_Y       = OTA_CARD_Y + OTA_CARD_H + MARGIN;
constexpr int OTA_BUTTON_W       = 204;
constexpr int OTA_BUTTON_H       = 66;
constexpr int OTA_WAIT_TITLE_Y   = 210;
constexpr int OTA_WAIT_DETAIL_Y  = 258;
constexpr int OTA_FAIL_ROW_Y     = 200;
constexpr int OTA_FAIL_DETAIL_Y  = 254;
constexpr int SAVER_CLOCK_Y      = 133;
constexpr int SAVER_CLOCK_H      = 88;   // montserrat_bold_120 line height
constexpr int SAVER_DATE_Y       = 271;
constexpr int SAVER_DATE_H       = 28;
constexpr int SAVER_USAGE_Y      = 320;
constexpr int SAVER_ROW_H        = 16;
constexpr int SAVER_ROW_GAP      = 2;
constexpr int SAVER_BAR_W        = 220;
// A hairline underline loses to the 120 px clock at viewing distance no matter
// how its colour is tuned.
constexpr int SAVER_BAR_H        = 3;
constexpr int SAVER_PCT_GAP      = 12;
// Fits a three-digit percentage without wrapping.
constexpr int SAVER_PCT_W        = 40;
// Both rows share the same centred bar and label positions.
constexpr int SAVER_PCT_SLOT     = SAVER_PCT_GAP + SAVER_PCT_W;
static_assert(SAVER_PCT_SLOT % 2 == 0);  // Halved below, so it must be even.
constexpr int SAVER_BAR_X        = SAVER_PCT_SLOT;
constexpr int SAVER_BAR_ALERT_X  = SAVER_PCT_SLOT / 2;
constexpr int SAVER_PCT_X        = SAVER_BAR_ALERT_X + SAVER_BAR_W + SAVER_PCT_GAP;
constexpr int SAVER_PCT_Y        = 2;
constexpr int SAVER_USAGE_W      = SAVER_BAR_X + SAVER_BAR_W + SAVER_PCT_SLOT;
constexpr int SAVER_USAGE_X      = (SCREEN_W - SAVER_USAGE_W) / 2;
// The row is centred, the resting bar is centred within it, and so is the
// alerting bar-plus-number. The group therefore reads as an underline of the
// clock rather than as a component of its own, in either state.
static_assert(2 * SAVER_USAGE_X + SAVER_USAGE_W == SCREEN_W);
static_assert(2 * (SAVER_USAGE_X + SAVER_BAR_X) + SAVER_BAR_W == SCREEN_W);
static_assert(
    2 * (SAVER_USAGE_X + SAVER_BAR_ALERT_X) + SAVER_BAR_W + SAVER_PCT_GAP +
        SAVER_PCT_W ==
    SCREEN_W);
static_assert(SAVER_PCT_X + SAVER_PCT_W <= SAVER_USAGE_W);
static_assert(SAVER_PCT_Y + 12 <= SAVER_ROW_H);  // 12 px is the font's line height.
// Ink runs from the top of the clock to the middle of the lower bar. Equal
// margins top and bottom mean the block is centred; the wake hint is excluded
// because it fades out and is not part of the resting composition.
constexpr int SAVER_INK_TOP    = SAVER_CLOCK_Y;
constexpr int SAVER_INK_BOTTOM =
    SAVER_USAGE_Y + SAVER_ROW_H + SAVER_ROW_GAP + (SAVER_ROW_H + SAVER_BAR_H) / 2;
static_assert(SAVER_INK_TOP + SAVER_INK_BOTTOM == SCREEN_H);
static_assert(SAVER_CLOCK_Y + SAVER_CLOCK_H < SAVER_DATE_Y);
static_assert(SAVER_DATE_Y + SAVER_DATE_H <= SAVER_USAGE_Y);

bsp_display_rotation_t to_bsp_rotation(ScreenRotation rotation)
{
    switch (rotation) {
    case ScreenRotation::Deg90:  return BSP_DISPLAY_ROTATE_90;
    case ScreenRotation::Deg180: return BSP_DISPLAY_ROTATE_180;
    case ScreenRotation::Deg270: return BSP_DISPLAY_ROTATE_270;
    default:                     return BSP_DISPLAY_ROTATE_0;
    }
}

lv_display_t* start_display(
    ScreenRotation initial_rotation, lv_indev_t** ret_input_device)
{
    bsp_display_config_t config = BSP_DISPLAY_DEFAULT_CONFIG();
    config.rotation = to_bsp_rotation(initial_rotation);
    lv_display_t* display = bsp_display_start_with_config(&config);
    if (display == nullptr) {
        ESP_LOGE(TAG, "display initialization failed");
        return nullptr;
    }
    *ret_input_device = bsp_display_get_input_dev();
    if (*ret_input_device == nullptr) {
        ESP_LOGE(TAG, "touch initialization failed");
        return nullptr;
    }
    const esp_err_t error = bsp_display_brightness_set(
        CONFIG_MOSAICO_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display brightness setting failed: %s",
                 esp_err_to_name(error));
        return nullptr;
    }
    return display;
}

class DisplayLock {
public:
    DisplayLock() : locked_(bsp_display_lock(-1)) {}

    ~DisplayLock()
    {
        if (locked_) {
            bsp_display_unlock();
        }
    }

    explicit operator bool() const { return locked_; }

    DisplayLock(const DisplayLock&) = delete;
    DisplayLock& operator=(const DisplayLock&) = delete;

private:
    bool locked_;
};

size_t provider_index(Provider provider)
{
    return provider == Provider::Codex ? 0 : 1;
}

constexpr uint32_t usage_color_hex(uint8_t percent)
{
    return percent >= 95 ? COLOR_BAD : percent >= 80 ? COLOR_WARN : COLOR_TEXT;
}

static_assert(usage_color_hex(0) == COLOR_TEXT);
static_assert(usage_color_hex(79) == COLOR_TEXT);
static_assert(usage_color_hex(80) == COLOR_WARN);
static_assert(usage_color_hex(94) == COLOR_WARN);
static_assert(usage_color_hex(95) == COLOR_BAD);
static_assert(usage_color_hex(100) == COLOR_BAD);

lv_color_t usage_color(uint8_t percent)
{
    return lv_color_hex(usage_color_hex(percent));
}

constexpr uint32_t saver_usage_color_hex(ScreensaverUsageLevel level)
{
    return level == ScreensaverUsageLevel::Bad    ? COLOR_SAVER_BAD
         : level == ScreensaverUsageLevel::Warn   ? COLOR_SAVER_WARN
                                                  : COLOR_SAVER_BASE;
}

// The resting tiers stay below the clock, so the clock keeps being the subject.
// The top tier is allowed to reach it, but never exceed it: at 95% the row is
// showing its number as well, and being noticed is the whole point.
static_assert(saver_usage_color_hex(ScreensaverUsageLevel::Base) < COLOR_MUTED);
static_assert(saver_usage_color_hex(ScreensaverUsageLevel::Warn) < COLOR_MUTED);
static_assert(saver_usage_color_hex(ScreensaverUsageLevel::Bad) <= COLOR_MUTED);

int saver_fill_width(uint8_t percent)
{
    return (int(percent) * SAVER_BAR_W + 50) / 100;
}

lv_color_t state_color(DisplayState state)
{
    switch (state) {
        case DisplayState::Online:
        case DisplayState::Partial:
            return lv_color_hex(COLOR_MUTED);
        case DisplayState::NoData:
        case DisplayState::Offline:
        default:
            return lv_color_hex(COLOR_DIM);
    }
}

void style_panel(lv_obj_t* panel, uint32_t background = COLOR_PANEL,
                 int radius = CARD_RADIUS)
{
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, int x, int y,
                     const lv_font_t* font, uint32_t color = COLOR_TEXT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void style_bar(lv_obj_t* bar, int height)
{
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, height / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, height / 2, LV_PART_INDICATOR);
}

/** Animation callback for the shared navigation underline. */
void nav_indicator_slide(void* target, int32_t translate_x)
{
    lv_obj_set_style_translate_x(
        static_cast<lv_obj_t*>(target), translate_x, 0);
}

void build_compact_metric(lv_obj_t* parent, const char* name,
                           int label_y, int value_y, int bar_y,
                           DisplayUi::MetricWidgets& widgets)
{
    lv_obj_t* label = make_label(
        parent, name, OVERVIEW_PAD, label_y,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    widgets.value =
        make_label(parent, "--", OVERVIEW_PAD, value_y,
                   &lv_font_montserrat_30);
    lv_obj_set_width(widgets.value, CARD_W - 2 * OVERVIEW_PAD);
    lv_obj_set_style_text_align(widgets.value, LV_TEXT_ALIGN_RIGHT, 0);
    widgets.bar = lv_bar_create(parent);
    lv_obj_set_pos(widgets.bar, OVERVIEW_PAD, bar_y);
    lv_obj_set_size(
        widgets.bar, CARD_W - 2 * OVERVIEW_PAD, BAR_H_OVERVIEW);
    style_bar(widgets.bar, BAR_H_OVERVIEW);
    lv_bar_set_value(widgets.bar, 0, LV_ANIM_OFF);
}

void build_large_metric(lv_obj_t* parent, const char* name, int y,
                        DisplayUi::MetricWidgets& widgets)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, MARGIN, y);
    lv_obj_set_size(card, CARD_W, 158);
    style_panel(card);
    lv_obj_t* label = make_label(
        card, name, CARD_PAD, 16, &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    widgets.reset =
        make_label(card, "", 220, 16, &lv_font_montserrat_16, COLOR_RESET);
    lv_obj_set_width(widgets.reset, CARD_W - 220 - CARD_PAD);
    lv_obj_set_style_text_align(widgets.reset, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(widgets.reset, 1, 0);
    widgets.value =
        make_label(card, "--", CARD_PAD, 43, &lv_font_montserrat_48);
    widgets.bar = lv_bar_create(card);
    lv_obj_set_pos(widgets.bar, CARD_PAD, 124);
    lv_obj_set_size(
        widgets.bar, CARD_W - 2 * CARD_PAD, BAR_H_DETAIL);
    style_bar(widgets.bar, BAR_H_DETAIL);
    lv_bar_set_value(widgets.bar, 0, LV_ANIM_OFF);
}
}  // namespace

bool DisplayUi::begin(ScreenRotation initial_rotation)
{
    display_ = start_display(initial_rotation, &input_device_);
    if (display_ == nullptr) {
        return false;
    }
    panel_io_ = bsp_display_get_panel_io();
    rotation_ = initial_rotation;
    brightness_percent_ = CONFIG_MOSAICO_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT;
    DisplayLock lock;
    if (!lock) {
        ESP_LOGE(TAG, "failed to acquire display lock");
        return false;
    }

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, touch_down_event, LV_EVENT_PRESSED, this);
    // Listen at the input-device level as well: a press targeting a card,
    // label, or navigation button is not guaranteed to bubble to the screen.
    lv_indev_add_event_cb(
        input_device_, touch_down_event, LV_EVENT_PRESSED, this);

    lv_indev_set_long_press_time(input_device_, 800);
    lv_indev_add_event_cb(input_device_, long_press_event, LV_EVENT_LONG_PRESSED, this);
    build_header(screen);
    build_overview(screen);
    build_provider_page(screen, Provider::Codex);
    build_provider_page(screen, Provider::Claude);
    build_navigation(screen);
    build_screensaver(screen);
    build_settings(screen);
    build_pairing_overlay(screen);
    build_ota_overlay(screen);
    show_page(Page::Overview, /*animate=*/false);
    lv_display_add_event_cb(
        display_, transition_refresh_ready, LV_EVENT_REFR_READY, this);

    return true;
}

bool DisplayUi::set_rotation(ScreenRotation rotation)
{
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "cannot rotate before display initialization");
        return false;
    }
    if (rotation == rotation_) {
        return true;
    }

    // Called without the LVGL lock: bsp_display_set_rotation() drains the LVGL
    // worker through esp_lv_adapter_pause() and waits for its acknowledgement,
    // which the worker can only send from the top of its loop. Holding the lock
    // here strands the worker inside esp_lv_adapter_lock() and both tasks wait
    // forever. The BSP takes the lock itself for the invalidate that follows.
    const esp_err_t error = bsp_display_set_rotation(to_bsp_rotation(rotation));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display rotation failed: %s", esp_err_to_name(error));
        return false;
    }

    rotation_ = rotation;
    return true;
}


void DisplayUi::build_header(lv_obj_t* screen)
{
    clock_label_ = make_label(
        screen, "--:--", MARGIN, 13,
        &lv_font_montserrat_22, COLOR_TEXT);

    lv_obj_t* status_group = lv_obj_create(screen);
    lv_obj_set_pos(status_group, 311, 13);
    lv_obj_set_size(status_group, 91, 28);
    lv_obj_set_style_bg_opa(status_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_group, 0, 0);
    lv_obj_set_style_pad_all(status_group, 0, 0);
    lv_obj_remove_flag(status_group, LV_OBJ_FLAG_SCROLLABLE);

    status_dot_ = lv_obj_create(status_group);
    lv_obj_set_pos(status_dot_, 0, 10);
    lv_obj_set_size(status_dot_, 8, 8);
    lv_obj_set_style_radius(status_dot_, 4, 0);
    lv_obj_set_style_bg_color(status_dot_, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(status_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot_, 0, 0);
    lv_obj_set_style_pad_all(status_dot_, 0, 0);
    lv_obj_remove_flag(status_dot_, LV_OBJ_FLAG_SCROLLABLE);

    connection_label_ = make_label(
        status_group, "WAITING", 16, 6,
        &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(connection_label_, 75);
    lv_obj_set_style_text_align(connection_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(connection_label_, 1, 0);

    battery_group_ = lv_obj_create(screen);
    lv_obj_set_pos(battery_group_, SCREEN_W - MARGIN - BATTERY_GROUP_W, 13);
    lv_obj_set_size(battery_group_, BATTERY_GROUP_W, 28);
    lv_obj_set_style_bg_opa(battery_group_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_group_, 0, 0);
    lv_obj_set_style_pad_all(battery_group_, 0, 0);
    lv_obj_remove_flag(battery_group_, LV_OBJ_FLAG_SCROLLABLE);

    charge_symbol_ = make_label(
        battery_group_, LV_SYMBOL_CHARGE, 0, 6,
        &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(charge_symbol_, 7);
    lv_obj_set_style_text_align(charge_symbol_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(charge_symbol_, LV_OPA_TRANSP, 0);

    lv_obj_t* body = lv_obj_create(battery_group_);
    lv_obj_set_pos(body, 12, 7);
    lv_obj_set_size(body, BATTERY_BODY_W, BATTERY_BODY_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; ++i) {
        battery_segments_[i] = lv_obj_create(body);
        lv_obj_set_pos(battery_segments_[i], 2 + i * 6, 2);
        lv_obj_set_size(battery_segments_[i], 5, 8);
        lv_obj_set_style_border_width(battery_segments_[i], 0, 0);
        lv_obj_set_style_radius(battery_segments_[i], 1, 0);
        lv_obj_set_style_pad_all(battery_segments_[i], 0, 0);
        lv_obj_remove_flag(battery_segments_[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t* terminal = lv_obj_create(battery_group_);
    lv_obj_set_pos(terminal, 42, 11);
    lv_obj_set_size(terminal, 2, 6);
    lv_obj_set_style_bg_color(terminal, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(terminal, 0, 0);
    lv_obj_set_style_radius(terminal, 1, 0);
    lv_obj_set_style_pad_all(terminal, 0, 0);
    lv_obj_remove_flag(terminal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* divider = lv_obj_create(screen);
    lv_obj_set_pos(divider, MARGIN, HEADER_H);
    lv_obj_set_size(divider, CARD_W, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
}

void DisplayUi::build_screensaver(lv_obj_t* screen)
{
    screensaver_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(screensaver_panel_, 0, 0);
    lv_obj_set_size(screensaver_panel_, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(
        screensaver_panel_, lv_color_hex(COLOR_SAVER_BG), 0);
    lv_obj_set_style_bg_opa(screensaver_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screensaver_panel_, 0, 0);
    lv_obj_set_style_pad_all(screensaver_panel_, 0, 0);
    lv_obj_remove_flag(screensaver_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        screensaver_panel_, touch_down_event, LV_EVENT_PRESSED, this);

    screensaver_content_ = lv_obj_create(screensaver_panel_);
    lv_obj_set_pos(screensaver_content_, 0, 0);
    lv_obj_set_size(screensaver_content_, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(screensaver_content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screensaver_content_, 0, 0);
    lv_obj_set_style_pad_all(screensaver_content_, 0, 0);
    lv_obj_remove_flag(screensaver_content_, LV_OBJ_FLAG_SCROLLABLE);

    screensaver_time_ = make_label(
        screensaver_content_, "--:--", 0, SAVER_CLOCK_Y,
        &montserrat_bold_120, COLOR_MUTED);
    lv_obj_set_width(screensaver_time_, SCREEN_W);
    lv_obj_set_style_text_align(screensaver_time_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* date_row = lv_obj_create(screensaver_content_);
    lv_obj_set_pos(date_row, 0, SAVER_DATE_Y);
    lv_obj_set_size(date_row, SCREEN_W, SAVER_DATE_H);
    lv_obj_set_style_bg_opa(date_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(date_row, 0, 0);
    lv_obj_set_style_pad_all(date_row, 0, 0);
    lv_obj_set_style_pad_column(date_row, 12, 0);
    lv_obj_set_flex_flow(date_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        date_row, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(date_row, LV_OBJ_FLAG_SCROLLABLE);
    screensaver_weekday_ = make_label(
        date_row, "---", 0, 0, &montserrat_semibold_20, COLOR_DIM);
    lv_obj_set_style_text_letter_space(screensaver_weekday_, 2, 0);
    lv_obj_t* date_dot = lv_obj_create(date_row);
    lv_obj_set_size(date_dot, 4, 4);
    lv_obj_set_style_radius(date_dot, 2, 0);
    lv_obj_set_style_bg_color(date_dot, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_bg_opa(date_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(date_dot, 0, 0);
    lv_obj_set_style_pad_all(date_dot, 0, 0);
    screensaver_month_day_ = make_label(
        date_row, "-- --", 0, 0, &montserrat_semibold_20, COLOR_DIM);
    lv_obj_set_style_text_letter_space(screensaver_month_day_, 2, 0);

    for (int i = 0; i < 2; ++i) {
        lv_obj_t* row = lv_obj_create(screensaver_content_);
        lv_obj_set_pos(
            row, SAVER_USAGE_X,
            SAVER_USAGE_Y + i * (SAVER_ROW_H + SAVER_ROW_GAP));
        lv_obj_set_size(row, SAVER_USAGE_W, SAVER_ROW_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* track = lv_obj_create(row);
        lv_obj_set_pos(track, SAVER_BAR_X, (SAVER_ROW_H - SAVER_BAR_H) / 2);
        lv_obj_set_size(track, SAVER_BAR_W, SAVER_BAR_H);
        lv_obj_set_style_bg_color(track, lv_color_hex(COLOR_SAVER_TRACK), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_radius(track, SAVER_BAR_H / 2, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        screensaver_usage_[i].track = track;

        lv_obj_t* fill = lv_obj_create(row);
        lv_obj_set_pos(fill, SAVER_BAR_X, (SAVER_ROW_H - SAVER_BAR_H) / 2);
        lv_obj_set_size(fill, SAVER_BAR_W, SAVER_BAR_H);
        lv_obj_set_style_bg_color(fill, lv_color_hex(COLOR_SAVER_BASE), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, SAVER_BAR_H / 2, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
        screensaver_usage_[i].fill = fill;

        lv_obj_t* percent = make_label(
            row, "", SAVER_PCT_X, SAVER_PCT_Y,
            &montserrat_semibold_13, COLOR_SAVER_BASE);
        lv_obj_set_width(percent, SAVER_PCT_W);
        // Clip rather than wrap, so no future glyph change can push a second
        // line into the row below.
        lv_label_set_long_mode(percent, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_letter_space(percent, 0, 0);
        lv_obj_add_flag(percent, LV_OBJ_FLAG_HIDDEN);
        screensaver_usage_[i].percent = percent;
    }

    screensaver_hint_ = make_label(
        screensaver_content_, "TAP OR SHAKE TO WAKE", 0, 433,
        &montserrat_semibold_13, COLOR_BATTERY_OFF);
    lv_obj_set_width(screensaver_hint_, SCREEN_W);
    lv_obj_set_style_text_align(screensaver_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(screensaver_hint_, 1, 0);

    screensaver_low_group_ = lv_obj_create(screensaver_content_);
    lv_obj_set_pos(screensaver_low_group_, 382, 18);
    lv_obj_set_size(screensaver_low_group_, 74, 22);
    lv_obj_set_style_bg_opa(screensaver_low_group_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screensaver_low_group_, 0, 0);
    lv_obj_set_style_pad_all(screensaver_low_group_, 0, 0);
    lv_obj_remove_flag(screensaver_low_group_, LV_OBJ_FLAG_SCROLLABLE);
    make_label(
        screensaver_low_group_, "LOW", 0, 4,
        &montserrat_semibold_13, COLOR_LOW_DIM);
    lv_obj_t* low_body = lv_obj_create(screensaver_low_group_);
    lv_obj_set_pos(low_body, 42, 4);
    lv_obj_set_size(low_body, 30, 14);
    lv_obj_set_style_bg_opa(low_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(low_body, 1, 0);
    lv_obj_set_style_border_color(low_body, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_radius(low_body, 2, 0);
    lv_obj_set_style_pad_all(low_body, 0, 0);
    for (int i = 0; i < 4; ++i) {
        screensaver_low_segments_[i] = lv_obj_create(low_body);
        lv_obj_set_pos(screensaver_low_segments_[i], 2 + i * 6, 2);
        lv_obj_set_size(screensaver_low_segments_[i], 5, 8);
        lv_obj_set_style_border_width(screensaver_low_segments_[i], 0, 0);
        lv_obj_set_style_radius(screensaver_low_segments_[i], 1, 0);
        lv_obj_set_style_pad_all(screensaver_low_segments_[i], 0, 0);
    }
    lv_obj_t* low_terminal = lv_obj_create(screensaver_low_group_);
    lv_obj_set_pos(low_terminal, 72, 8);
    lv_obj_set_size(low_terminal, 2, 6);
    lv_obj_set_style_bg_color(low_terminal, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_bg_opa(low_terminal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(low_terminal, 0, 0);
    lv_obj_set_style_radius(low_terminal, 1, 0);
    lv_obj_set_style_pad_all(low_terminal, 0, 0);
    lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
}

void DisplayUi::build_overview(lv_obj_t* screen)
{
    overview_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(overview_panel_, 0, CONTENT_Y);
    lv_obj_set_size(overview_panel_, SCREEN_W, CONTENT_H);
    lv_obj_set_style_bg_opa(overview_panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overview_panel_, 0, 0);
    lv_obj_set_style_pad_all(overview_panel_, 0, 0);
    lv_obj_remove_flag(overview_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(overview_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        overview_panel_, gesture_event, LV_EVENT_GESTURE, this);

    const char* names[] = {"CODEX", "CLAUDE"};
    for (size_t i = 0; i < 2; ++i) {
        ProviderWidgets& widgets = overview_[i];
        widgets.panel = lv_obj_create(overview_panel_);
        lv_obj_set_pos(widgets.panel, MARGIN, i == 0 ? 17 : 195);
        lv_obj_set_size(widgets.panel, CARD_W, 166);
        style_panel(widgets.panel);
        lv_obj_t* title = make_label(
            widgets.panel, names[i], OVERVIEW_PAD, 14,
            &lv_font_montserrat_20);
        lv_obj_set_style_text_letter_space(title, 1, 0);
        widgets.compact = true;
        widgets.status = make_label(
            widgets.panel, "NO DATA", 220, 17,
            &lv_font_montserrat_14, COLOR_DIM);
        lv_obj_set_width(widgets.status, 194);
        lv_obj_set_style_text_align(widgets.status, LV_TEXT_ALIGN_RIGHT, 0);
        build_compact_metric(
            widgets.panel, "SHORT", 50, 42, 78,
            widgets.short_window);
        build_compact_metric(
            widgets.panel, "WEEK", 108, 100, 136,
            widgets.week_window);
    }
}

void DisplayUi::build_provider_page(lv_obj_t* screen, Provider provider)
{
    const size_t index = provider_index(provider);
    ProviderWidgets& widgets = provider_pages_[index];
    widgets.panel = lv_obj_create(screen);
    lv_obj_set_pos(widgets.panel, 0, CONTENT_Y);
    lv_obj_set_size(widgets.panel, SCREEN_W, CONTENT_H);
    lv_obj_set_style_bg_opa(widgets.panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(widgets.panel, 0, 0);
    lv_obj_set_style_pad_all(widgets.panel, 0, 0);
    lv_obj_remove_flag(widgets.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(widgets.panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        widgets.panel, gesture_event, LV_EVENT_GESTURE, this);

    lv_obj_t* title = make_label(
        widgets.panel, provider == Provider::Codex ? "CODEX" : "CLAUDE",
        MARGIN, 11, &lv_font_montserrat_26);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    widgets.status =
        make_label(
            widgets.panel, "NO DATA", 260, 19,
            &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_width(widgets.status, SCREEN_W - MARGIN - 260);
    lv_obj_set_style_text_align(widgets.status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(widgets.status, 1, 0);
    build_large_metric(
        widgets.panel, "SHORT WINDOW", 49, widgets.short_window);
    build_large_metric(
        widgets.panel, "WEEK WINDOW", 217, widgets.week_window);
}

void DisplayUi::build_navigation(lv_obj_t* screen)
{
    lv_obj_t* navigation = lv_obj_create(screen);
    lv_obj_set_pos(navigation, MARGIN, NAV_Y);
    lv_obj_set_size(navigation, CARD_W, NAV_H);
    lv_obj_set_style_bg_opa(navigation, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(navigation, 0, 0);
    lv_obj_set_style_pad_all(navigation, 0, 0);
    lv_obj_set_style_pad_column(navigation, NAV_GAP, 0);
    lv_obj_set_flex_flow(navigation, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(navigation, LV_OBJ_FLAG_SCROLLABLE);

    const char* labels[] = {"OVERVIEW", "CODEX", "CLAUDE"};
    for (size_t i = 0; i < 3; ++i) {
        tab_bindings_[i] =
            TabBinding{this, static_cast<uint8_t>(i)};
        lv_obj_t* tab = lv_button_create(navigation);
        lv_obj_set_size(tab, 0, NAV_H);
        lv_obj_set_flex_grow(tab, 1);
        lv_obj_set_style_radius(tab, 0, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
        // Keep the theme's pressed fill off too, so a touch never paints a
        // block behind the label.
        lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_style_pad_all(tab, 0, 0);
        // Reserve the shared underline's height plus its label gap as bottom
        // padding so the label baseline stays a fixed distance above it.
        lv_obj_set_style_pad_bottom(tab, NAV_LABEL_GAP + NAV_BAR_H, 0);
        lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            tab, LV_FLEX_ALIGN_END,
            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            tab, tab_event, LV_EVENT_CLICKED, &tab_bindings_[i]);

        nav_labels_[i] =
            make_label(
                tab, labels[i], 0, 0,
                &montserrat_semibold_13, COLOR_DIM);
        lv_obj_set_style_text_letter_space(nav_labels_[i], 1, 0);
    }

    // Selection is shown by an underline, never by a filled block: the
    // navigation must not outshine the data above it. One underline is shared
    // by all three tabs so a page change can slide it instead of blinking it
    // from one tab to the next. It ignores the row layout and is placed by
    // hand; only its translation is animated, which keeps the transform out of
    // the flex pass.
    nav_indicator_ = lv_obj_create(navigation);
    lv_obj_add_flag(nav_indicator_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(nav_indicator_, NAV_BAR_W, NAV_BAR_H);
    lv_obj_set_style_radius(nav_indicator_, NAV_BAR_H / 2, 0);
    lv_obj_set_style_bg_color(nav_indicator_, lv_color_hex(COLOR_RESET), 0);
    lv_obj_set_style_bg_opa(nav_indicator_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_indicator_, 0, 0);
    lv_obj_set_style_pad_all(nav_indicator_, 0, 0);
    lv_obj_remove_flag(nav_indicator_, LV_OBJ_FLAG_SCROLLABLE);

    // Parked under the first tab; every other page is reached by translating
    // one whole tab pitch, so Page::Overview needs no transform at all.
    lv_obj_set_pos(nav_indicator_, NAV_BAR_X, NAV_H - NAV_BAR_H);
}

void DisplayUi::build_settings(lv_obj_t* screen)
{
    settings_panel_ = lv_obj_create(screen);
    lv_obj_remove_style_all(settings_panel_);
    lv_obj_set_pos(settings_panel_, 0, HEADER_H + 1);
    lv_obj_set_size(settings_panel_, SCREEN_W, SCREEN_H - HEADER_H - 1);
    lv_obj_set_style_bg_color(settings_panel_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_SCROLLABLE);
    auto* title = make_label(settings_panel_, "BRIGHTNESS", MARGIN, 24,
                            &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    settings_brightness_ = make_label(settings_panel_, "", MARGIN, 58, &lv_font_montserrat_48);
    lv_obj_t* divider = lv_obj_create(settings_panel_);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, MARGIN, 158);
    lv_obj_set_size(divider, CARD_W, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    title = make_label(settings_panel_, "AUTO CLOCK", MARGIN, 180,
                       &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    settings_timeout_ = make_label(settings_panel_, "AFTER IDLE / MIN", MARGIN, 207,
                                   &lv_font_montserrat_14, COLOR_MUTED);
    settings_hint_ = make_label(settings_panel_, "", MARGIN, 322,
                               &montserrat_semibold_13, COLOR_TEXT);
    const char* labels[] = {"-", "+", "OFF", "1", "5", "10", "30", "SAVE", "CANCEL"};
    const SettingsAction actions[] = {SettingsAction::Dimmer, SettingsAction::Brighter,
        SettingsAction::TimeoutOff, SettingsAction::Timeout1, SettingsAction::Timeout5,
        SettingsAction::Timeout10, SettingsAction::Timeout30, SettingsAction::Save, SettingsAction::Cancel};
    const int x[] = {288, 376, 24, 112, 200, 288, 376, 250, MARGIN};
    const int y[] = {58, 58, 244, 244, 244, 244, 244, 349, 349};
    for (size_t i = 0; i < 9; ++i) {
        lv_obj_t* button = lv_button_create(settings_panel_);
        settings_buttons_[i] = button;
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, x[i], y[i]);
        lv_obj_set_size(button, i < 7 ? 80 : 206, i < 2 ? 80 : 64);
        style_panel(button);
        lv_obj_set_style_text_color(button, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(button, i < 2 ? &lv_font_montserrat_30 : &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_letter_space(button, 1, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_TRACK), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button, lv_color_hex(COLOR_MUTED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_TEXT), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(button, lv_color_hex(COLOR_BG), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_RESET), LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(button, lv_color_hex(COLOR_DIM), LV_STATE_DISABLED);
        if (i == 7) lv_obj_add_state(button, LV_STATE_CHECKED);
        settings_bindings_[i] = {this, actions[i]};
        lv_obj_add_event_cb(button, settings_event, LV_EVENT_CLICKED, &settings_bindings_[i]);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, labels[i]);
        lv_obj_center(label);
    }
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
}

bool DisplayUi::apply_settings(const DisplaySettings& settings, bool visible, bool save_error, bool modified)
{
    DisplayLock lock;
    if (!lock) return false;
    const bool brightness_changed = brightness_percent_ != settings.brightness;
    if (brightness_changed) {
        lv_anim_delete(this, transition_brightness);
        if (!transition_refresh_pending_ && !set_transition_brightness(settings.brightness))
            return false;
        brightness_percent_ = settings.brightness;
    }
    const bool was_visible = settings_visible_.exchange(visible, std::memory_order_relaxed);
    lv_label_set_text_fmt(settings_brightness_, "%u%%", settings.brightness);
    lv_obj_set_state(settings_buttons_[0], LV_STATE_DISABLED, settings.brightness <= 1);
    lv_obj_set_state(settings_buttons_[1], LV_STATE_DISABLED, settings.brightness >= 100);
    lv_obj_set_state(settings_buttons_[7], LV_STATE_DISABLED, !modified && !save_error);
    bool standard_timeout = false;
    for (size_t i = 0; i < 5; ++i) {
        const bool selected = settings.clock_timeout_seconds == CLOCK_TIMEOUT_CHOICES[i];
        lv_obj_set_state(settings_buttons_[i + 2], LV_STATE_CHECKED, selected);
        standard_timeout = standard_timeout || selected;
    }
    if (standard_timeout) lv_label_set_text(settings_timeout_, "AFTER IDLE / MIN");
    else lv_label_set_text_fmt(settings_timeout_, "AFTER IDLE / MIN - CURRENT %lu SEC",
                              static_cast<unsigned long>(settings.clock_timeout_seconds));
    lv_label_set_text(settings_hint_, save_error ? "SAVE FAILED - TRY AGAIN" : "");
    if (visible && !was_visible) {
        lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_panel_);
    } else if (!visible) lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void DisplayUi::settings_event(lv_event_t* event)
{
    auto* binding = static_cast<SettingsBinding*>(lv_event_get_user_data(event));
    if (binding->ui->settings_visible_.load(std::memory_order_relaxed))
        binding->ui->settings_action_.store(binding->action, std::memory_order_relaxed);
}

void DisplayUi::long_press_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (!ui->settings_visible_.load(std::memory_order_relaxed) &&
        !ui->ota_active_.load(std::memory_order_relaxed) &&
        ui->input_enabled_.load(std::memory_order_relaxed)) {
        ui->settings_action_.store(SettingsAction::Open, std::memory_order_relaxed);
        lv_indev_wait_release(ui->input_device_);
    }
}

void DisplayUi::build_pairing_overlay(lv_obj_t* screen)
{
    pairing_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(pairing_panel_, 0, HEADER_H + 1);
    lv_obj_set_size(
        pairing_panel_, SCREEN_W,
        SCREEN_H - (HEADER_H + 1) - PAIRING_BOTTOM);
    lv_obj_set_style_bg_color(pairing_panel_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(pairing_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pairing_panel_, 0, 0);
    lv_obj_set_style_pad_all(pairing_panel_, 0, 0);
    lv_obj_remove_flag(pairing_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* pairing_card = lv_obj_create(pairing_panel_);
    lv_obj_set_size(pairing_card, 380, 200);
    style_panel(pairing_card, COLOR_PANEL, 18);
    lv_obj_set_style_border_color(
        pairing_card, lv_color_hex(0x2C313B), 0);
    lv_obj_center(pairing_card);

    lv_obj_t* title =
        make_label(
            pairing_card, "PAIRING", 0, 22,
            &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_width(title, 380);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    pairing_code_ =
        make_label(
            pairing_card, "000000", 0, 56,
            &lv_font_montserrat_30, COLOR_TEXT);
    lv_obj_set_width(pairing_code_, 380);
    lv_obj_set_style_text_align(pairing_code_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(pairing_code_, 10, 0);
    lv_obj_t* instruction =
        make_label(
            pairing_card, "Enter this code on your computer", 0, 119,
            &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(instruction, 380);
    lv_obj_set_style_text_align(instruction, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* supporting =
        make_label(
            pairing_card, "The panel will connect after confirmation", 0, 157,
            &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_width(supporting, 380);
    lv_obj_set_style_text_align(supporting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
}

void DisplayUi::build_ota_overlay(lv_obj_t* screen)
{
    ota_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(ota_panel_, 0, 0);
    lv_obj_set_size(ota_panel_, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(ota_panel_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(ota_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ota_panel_, 0, 0);
    lv_obj_set_style_pad_all(ota_panel_, 0, 0);
    lv_obj_remove_flag(ota_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = make_label(
        ota_panel_, "FIRMWARE", MARGIN, 18,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(header, 2, 0);
    lv_obj_t* divider = lv_obj_create(ota_panel_);
    lv_obj_set_pos(divider, MARGIN, HEADER_H);
    lv_obj_set_size(divider, CARD_W, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    // Confirming and receiving share one card: only the text inside and
    // which secondary row (countdown/note vs. progress bar/hint) is shown
    // changes between the two phases.
    ota_card_ = lv_obj_create(ota_panel_);
    lv_obj_set_pos(ota_card_, MARGIN, OTA_CARD_Y);
    lv_obj_set_size(ota_card_, CARD_W, OTA_CARD_H);
    style_panel(ota_card_);

    ota_card_label_ = make_label(
        ota_card_, "", CARD_PAD, OTA_CARD_ROW_Y,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(ota_card_label_, 1, 0);

    ota_card_countdown_ = make_label(
        ota_card_, "", CARD_PAD, OTA_CARD_ROW_Y,
        &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(ota_card_countdown_, CARD_W - 2 * CARD_PAD);
    lv_obj_set_style_text_align(ota_card_countdown_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(ota_card_countdown_, 1, 0);

    ota_card_value_ = make_label(
        ota_card_, "", 0, OTA_CARD_VALUE_Y,
        &lv_font_montserrat_48, COLOR_TEXT);
    lv_obj_set_width(ota_card_value_, CARD_W);
    lv_label_set_long_mode(ota_card_value_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ota_card_value_, LV_TEXT_ALIGN_CENTER, 0);

    ota_card_note_ = make_label(
        ota_card_, "AUTO-CANCELS", 0, OTA_CARD_THIRD_ROW_Y,
        &lv_font_montserrat_12, COLOR_DIM);
    lv_obj_set_width(ota_card_note_, CARD_W);
    lv_obj_set_style_text_align(ota_card_note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ota_card_note_, 1, 0);

    ota_progress_ = lv_bar_create(ota_card_);
    lv_obj_set_pos(ota_progress_, CARD_PAD, OTA_CARD_THIRD_ROW_Y);
    lv_obj_set_size(ota_progress_, CARD_W - 2 * CARD_PAD, BAR_H_DETAIL);
    style_bar(ota_progress_, BAR_H_DETAIL);
    lv_bar_set_value(ota_progress_, 0, LV_ANIM_OFF);

    ota_card_hint_ = make_label(
        ota_card_, "DO NOT POWER OFF", 0, OTA_CARD_HINT_Y,
        &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_set_width(ota_card_hint_, CARD_W);
    lv_obj_set_style_text_align(ota_card_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ota_card_hint_, 1, 0);

    ota_confirm_button_ = lv_button_create(ota_panel_);
    lv_obj_set_pos(ota_confirm_button_, MARGIN, OTA_BUTTON_Y);
    lv_obj_set_size(ota_confirm_button_, OTA_BUTTON_W, OTA_BUTTON_H);
    lv_obj_set_style_bg_color(
        ota_confirm_button_, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_radius(ota_confirm_button_, 14, 0);
    lv_obj_add_event_cb(
        ota_confirm_button_, ota_confirm_event, LV_EVENT_CLICKED, this);
    lv_obj_t* label = make_label(
        ota_confirm_button_, "CONFIRM", 0, 0,
        &lv_font_montserrat_16, COLOR_BG);
    lv_obj_center(label);

    ota_deny_button_ = lv_button_create(ota_panel_);
    lv_obj_set_pos(
        ota_deny_button_, MARGIN + OTA_BUTTON_W + MARGIN, OTA_BUTTON_Y);
    lv_obj_set_size(ota_deny_button_, OTA_BUTTON_W, OTA_BUTTON_H);
    lv_obj_set_style_bg_color(ota_deny_button_, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_width(ota_deny_button_, 1, 0);
    lv_obj_set_style_border_color(
        ota_deny_button_, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(ota_deny_button_, 14, 0);
    lv_obj_add_event_cb(
        ota_deny_button_, ota_deny_event, LV_EVENT_CLICKED, this);
    label = make_label(
        ota_deny_button_, "CANCEL", 0, 0,
        &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_center(label);

    // Verifying/rebooting: plain centered status text, no card.
    ota_title_ = make_label(
        ota_panel_, "", 0, OTA_WAIT_TITLE_Y,
        &lv_font_montserrat_30, COLOR_TEXT);
    lv_obj_set_width(ota_title_, SCREEN_W);
    lv_obj_set_style_text_align(ota_title_, LV_TEXT_ALIGN_CENTER, 0);

    ota_detail_ = make_label(
        ota_panel_, "", 0, OTA_WAIT_DETAIL_Y,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_width(ota_detail_, SCREEN_W);
    lv_obj_set_style_text_align(ota_detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ota_detail_, 1, 0);

    // Failed: a status dot beside the headline, sized to its content and
    // centered as a unit rather than assuming a fixed text width.
    ota_fail_row_ = lv_obj_create(ota_panel_);
    lv_obj_set_size(ota_fail_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ota_fail_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ota_fail_row_, 0, 0);
    lv_obj_set_style_pad_all(ota_fail_row_, 0, 0);
    lv_obj_set_style_pad_column(ota_fail_row_, 12, 0);
    lv_obj_set_flex_flow(ota_fail_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        ota_fail_row_, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ota_fail_row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ota_fail_row_, LV_ALIGN_TOP_MID, 0, OTA_FAIL_ROW_Y);

    lv_obj_t* fail_dot = lv_obj_create(ota_fail_row_);
    lv_obj_set_size(fail_dot, 14, 14);
    lv_obj_set_style_radius(fail_dot, 7, 0);
    lv_obj_set_style_bg_color(fail_dot, lv_color_hex(COLOR_BAD), 0);
    lv_obj_set_style_bg_opa(fail_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fail_dot, 0, 0);
    lv_obj_remove_flag(fail_dot, LV_OBJ_FLAG_SCROLLABLE);

    ota_fail_title_ = make_label(
        ota_fail_row_, "UPDATE FAILED", 0, 0,
        &lv_font_montserrat_34, COLOR_BAD);

    lv_obj_add_flag(ota_panel_, LV_OBJ_FLAG_HIDDEN);
}

void DisplayUi::show_page(Page page, bool animate)
{
    lv_obj_add_flag(overview_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(provider_pages_[0].panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(provider_pages_[1].panel, LV_OBJ_FLAG_HIDDEN);
    if (page == Page::Overview) {
        lv_obj_remove_flag(overview_panel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        const Provider provider =
            page == Page::Codex ? Provider::Codex : Provider::Claude;
        lv_obj_remove_flag(
            provider_pages_[provider_index(provider)].panel,
            LV_OBJ_FLAG_HIDDEN);
    }

    const size_t selected = static_cast<size_t>(page);
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_set_style_text_color(
            nav_labels_[i],
            lv_color_hex(i == selected ? COLOR_RESET : COLOR_DIM), 0);
    }

    // render() calls this on every refresh, so the underline only moves when
    // the page actually changed; otherwise a steady screen would restart the
    // slide once per second.
    if (page != shown_page_) {
        const int32_t target =
            static_cast<int32_t>(selected) * NAV_TAB_PITCH;
        lv_anim_delete(nav_indicator_, nav_indicator_slide);
        if (animate) {
            lv_anim_t slide;
            lv_anim_init(&slide);
            lv_anim_set_var(&slide, nav_indicator_);
            lv_anim_set_exec_cb(&slide, nav_indicator_slide);
            lv_anim_set_values(
                &slide,
                lv_obj_get_style_translate_x(nav_indicator_, LV_PART_MAIN),
                target);
            lv_anim_set_duration(&slide, UI_ANIM_MS);
            lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
            lv_anim_start(&slide);
        } else {
            nav_indicator_slide(nav_indicator_, target);
        }
        shown_page_ = page;
    }
}

void DisplayUi::update_metric(
    MetricWidgets& widgets, const UsageWindow& window,
    bool has_data, uint32_t epoch, bool fresh)
{
    if (!has_data || !window.present) {
        lv_label_set_text(widgets.value, "--");
        lv_obj_set_style_text_color(
            widgets.value, lv_color_hex(COLOR_DIM), 0);
        if (widgets.reset != nullptr) {
            lv_label_set_text(widgets.reset, "");
        }
        set_usage_bar_value_immediately(widgets.bar, 0);
        lv_obj_set_style_bg_color(
            widgets.bar, lv_color_hex(COLOR_DIM), LV_PART_INDICATOR);
        return;
    }

    lv_label_set_text_fmt(widgets.value, "%u%%", window.used_percent);
    const lv_color_t color = fresh ? usage_color(window.used_percent)
                                  : lv_color_hex(COLOR_DIM);
    lv_obj_set_style_text_color(widgets.value, color, 0);
    lv_obj_set_style_bg_color(widgets.bar, color, LV_PART_INDICATOR);
    animate_usage_bar(widgets.bar, window.used_percent);
    if (widgets.reset != nullptr) {
        char countdown[16]{};
        format_countdown(
            window.has_reset, window.reset_at, epoch,
            countdown, sizeof(countdown));
        lv_label_set_text_fmt(widgets.reset, "RESET %s", countdown);
        lv_obj_set_style_text_color(
            widgets.reset, lv_color_hex(fresh ? COLOR_RESET : COLOR_DIM), 0);
    }
}

void DisplayUi::update_screensaver_usage(const ScreensaverUsageView& usage)
{
    // Both rows share a horizontal position when either label is visible.
    const bool any_percent =
        usage.rows[0].show_percent || usage.rows[1].show_percent;
    const int bar_x = any_percent ? SAVER_BAR_ALERT_X : SAVER_BAR_X;

    for (int i = 0; i < 2; ++i) {
        const ScreensaverUsageRow& row = usage.rows[i];
        const ScreensaverUsageWidgets& widgets = screensaver_usage_[i];
        const lv_color_t color =
            lv_color_hex(saver_usage_color_hex(row.level));
        lv_obj_set_x(widgets.track, bar_x);
        lv_obj_set_x(widgets.fill, bar_x);
        const int width = row.has_data ? saver_fill_width(row.percent) : 0;
        if (width > 0) {
            lv_obj_set_width(widgets.fill, width);
            lv_obj_set_style_bg_color(widgets.fill, color, 0);
            lv_obj_remove_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);
        }
        if (row.show_percent) {
            lv_label_set_text_fmt(widgets.percent, "%u%%", row.percent);
            lv_obj_set_style_text_color(widgets.percent, color, 0);
            lv_obj_remove_flag(widgets.percent, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.percent, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void DisplayUi::update_provider(
    ProviderWidgets& widgets, const UsageModel& model,
    Provider provider, bool connected, uint64_t now_ms)
{
    const auto state = model.display_state(provider, connected);
    const auto& snapshot = model.snapshot(provider);
    if (widgets.status != nullptr) {
        if (state == DisplayState::Online && widgets.compact) {
            char countdown[16]{};
            format_countdown(snapshot.short_window.has_reset,
                snapshot.short_window.reset_at, model.estimated_epoch(provider, now_ms),
                countdown, sizeof(countdown));
            lv_label_set_text_fmt(widgets.status, "SHORT RESET %s", countdown);
        } else if (!widgets.compact && state == DisplayState::Online) {
            const uint32_t epoch = model.estimated_epoch(provider, now_ms);
            const uint32_t age = epoch > snapshot.sampled_at
                ? epoch - snapshot.sampled_at : 0;
            lv_label_set_text_fmt(widgets.status, "LAST %luS AGO",
                static_cast<unsigned long>(age));
        } else {
            lv_label_set_text(widgets.status, display_state_name(state));
        }
        lv_obj_set_style_text_color(widgets.status, state_color(state), 0);
    }

    const uint32_t epoch =
        snapshot.has_valid_data ? model.estimated_epoch(provider, now_ms) : 0;
    const bool fresh = state == DisplayState::Online || state == DisplayState::Partial;
    const bool partial = snapshot.source_state == SourceState::Partial;
    update_metric(
        widgets.short_window, snapshot.short_window,
        snapshot.has_valid_data && (!partial || snapshot.latest_short_present), epoch, fresh);
    update_metric(
        widgets.week_window, snapshot.week_window,
        snapshot.has_valid_data && (!partial || snapshot.latest_week_present), epoch, fresh);
}

bool DisplayUi::set_transition_brightness(int32_t percent)
{
    const uint8_t level = static_cast<uint8_t>(percent * 255 / 100);
    // CO5300 QSPI encodes brightness register 0x51 with opcode 0x02
    // and an 8-bit address shift.
    constexpr int brightness_command = (0x02 << 24) | (0x51 << 8);
    const esp_err_t error = esp_lcd_panel_io_tx_param(
        panel_io_, brightness_command, &level, sizeof(level));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "transition brightness failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

void DisplayUi::transition_brightness(void* target, int32_t percent)
{
    static_cast<DisplayUi*>(target)->set_transition_brightness(percent);
}

void DisplayUi::transition_refresh_ready(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (!ui->transition_refresh_pending_) return;
    ui->transition_refresh_pending_ = false;

    // REFR_READY can precede the final DMA completion. lv_anim_start() applies
    // zero brightness synchronously; the SPI parameter command waits for queued
    // pixel transfers to finish before the fade can illuminate the new frame.
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ui);
    lv_anim_set_exec_cb(&fade, transition_brightness);
    lv_anim_set_values(&fade, 0, ui->brightness_percent_);
    lv_anim_set_duration(&fade, 120);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    if (lv_anim_start(&fade) == nullptr) {
        ui->set_transition_brightness(ui->brightness_percent_);
    }
}

void DisplayUi::render(
    const UsageModel& model, Page page, bool connected,
    bool has_passkey, uint32_t passkey, uint64_t now_ms,
    const OtaSnapshot& ota, uint32_t confirm_seconds,
    bool show_ota_error, const PanelPresentation& presentation,
    bool input_enabled)
{
    DisplayLock lock;
    if (!lock) {
        ESP_LOGE(TAG, "failed to acquire display lock for render");
        return;
    }

    const bool ota_visible = ota.phase != OtaPhase::Idle || show_ota_error;
    const bool screensaver_visible =
        presentation.screensaver.active && !has_passkey && !ota_visible;
    const bool was_screensaver_visible =
        !lv_obj_has_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
    if (screensaver_visible != was_screensaver_visible) {
        // Cancel an active fade before concealing the next screensaver repaint.
        lv_anim_delete(this, transition_brightness);
        transition_refresh_pending_ = set_transition_brightness(0);
    }
    input_enabled_.store(input_enabled && !has_passkey, std::memory_order_relaxed);
    ota_active_.store(ota.phase != OtaPhase::Idle, std::memory_order_relaxed);
    ota_confirming_.store(
        ota.phase == OtaPhase::Confirming, std::memory_order_relaxed);
    if (ota_visible) {
        lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ota_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ota_panel_);

        lv_obj_add_flag(ota_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_card_countdown_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_card_note_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_progress_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_card_hint_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_confirm_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_deny_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_fail_row_, LV_OBJ_FLAG_HIDDEN);

        if (show_ota_error) {
            const OtaErrorPresentation message =
                ota_error_presentation(ota.error);
            lv_obj_remove_flag(ota_fail_row_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_detail_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ota_detail_, OTA_FAIL_DETAIL_Y);
            lv_label_set_text(ota_fail_title_, message.title);
            lv_label_set_text(ota_detail_, message.detail);
        } else if (ota.phase == OtaPhase::Confirming) {
            lv_obj_remove_flag(ota_card_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_card_countdown_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_card_note_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_confirm_button_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_deny_button_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ota_card_label_, "NEW VERSION AVAILABLE");
            lv_label_set_text_fmt(
                ota_card_countdown_, "%luS",
                static_cast<unsigned long>(confirm_seconds));
            lv_label_set_text(ota_card_value_, ota.version.c_str());
        } else if (ota.phase == OtaPhase::Receiving) {
            lv_obj_remove_flag(ota_card_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_progress_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_card_hint_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ota_card_label_, "UPDATING FIRMWARE");
            const uint8_t percent = ota_progress_percent(ota.offset, ota.size);
            lv_label_set_text_fmt(ota_card_value_, "%u%%", percent);
            lv_bar_set_value(ota_progress_, percent, LV_ANIM_OFF);
        } else {
            lv_obj_remove_flag(ota_title_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_detail_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ota_detail_, OTA_WAIT_DETAIL_Y);
            lv_label_set_text(
                ota_title_, ota.phase == OtaPhase::Verifying
                    ? "VERIFYING" : "REBOOTING");
            lv_label_set_text(ota_detail_, "PLEASE WAIT");
        }
        return;
    }
    lv_obj_add_flag(ota_panel_, LV_OBJ_FLAG_HIDDEN);
    const bool settings_visible = settings_visible_.load(std::memory_order_relaxed) && !has_passkey;
    lv_label_set_text(clock_label_, settings_visible ? "DISPLAY" : presentation.clock.time);
    lv_obj_set_style_text_color(
        clock_label_,
        lv_color_hex(settings_visible || presentation.clock.available ? COLOR_TEXT : COLOR_DIM), 0);

    const bool battery_available = presentation.battery.available;
    const int visible_segments = battery_available
        ? static_cast<int>(presentation.battery.band) : 0;
    const uint32_t battery_color =
        presentation.battery.low_on_battery ? COLOR_WARN : COLOR_RESET;
    lv_obj_set_style_opa(
        charge_symbol_, presentation.battery.charging
            ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    for (int i = 0; i < 4; ++i) {
        lv_obj_set_style_bg_color(
            battery_segments_[i],
            lv_color_hex(i < visible_segments
                ? battery_color : COLOR_BATTERY_OFF), 0);
        lv_obj_set_style_bg_opa(battery_segments_[i], LV_OPA_COVER, 0);
    }

    if (settings_visible) {
        lv_label_set_text(connection_label_, connected ? "LINKED" : "WAITING");
        lv_obj_set_style_bg_color(status_dot_, lv_color_hex(connected ? COLOR_GOOD : COLOR_MUTED), 0);
        lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (screensaver_visible) {
        lv_label_set_text(screensaver_time_, presentation.clock.time);
        lv_label_set_text(
            screensaver_weekday_, presentation.clock.available
                ? presentation.clock.weekday : "---");
        lv_label_set_text(
            screensaver_month_day_, presentation.clock.available
                ? presentation.clock.month_day : "-- --");
        lv_obj_set_style_text_color(
            screensaver_time_,
            lv_color_hex(presentation.clock.available ? COLOR_MUTED : COLOR_DIM),
            0);
        const ScreensaverUsageView usage = make_screensaver_usage_view(model, connected);
        update_screensaver_usage(usage);
        lv_label_set_text(
            screensaver_hint_,
            usage.offline ? (connected ? "DATA UNAVAILABLE" : "OFFLINE")
                          : "TAP OR SHAKE TO WAKE");
        lv_obj_set_style_opa(
            screensaver_hint_, usage.offline ? static_cast<lv_opa_t>(LV_OPA_COVER)
                : presentation.screensaver.hint_opacity, 0);
        lv_obj_set_style_translate_x(
            screensaver_content_, presentation.screensaver.offset_x, 0);
        lv_obj_set_style_translate_y(
            screensaver_content_, presentation.screensaver.offset_y, 0);
        if (presentation.battery.low_on_battery) {
            lv_obj_remove_flag(screensaver_low_group_, LV_OBJ_FLAG_HIDDEN);
            const int low_segments = static_cast<int>(presentation.battery.band);
            for (int i = 0; i < 4; ++i) {
                lv_obj_set_style_bg_color(
                    screensaver_low_segments_[i],
                    lv_color_hex(i < low_segments
                        ? COLOR_LOW_DIM : COLOR_LINE), 0);
                lv_obj_set_style_bg_opa(
                    screensaver_low_segments_[i], LV_OPA_COVER, 0);
            }
        } else {
            lv_obj_add_flag(screensaver_low_group_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(screensaver_panel_);
        return;
    }
    lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
    show_page(page, /*animate=*/true);
    for (Provider provider : {Provider::Codex, Provider::Claude}) {
        const size_t index = provider_index(provider);
        update_provider(
            overview_[index], model, provider, connected, now_ms);
        update_provider(
            provider_pages_[index], model, provider, connected, now_ms);
    }

    if (has_passkey) {
        lv_label_set_text(connection_label_, "PAIRING");
        lv_label_set_text_fmt(
            pairing_code_, "%06lu", static_cast<unsigned long>(passkey));
        lv_obj_remove_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(pairing_panel_);
    } else {
        lv_label_set_text(connection_label_, connected ? "LINKED" : "WAITING");
        lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(
        status_dot_,
        lv_color_hex(
            has_passkey ? COLOR_WARN : connected ? COLOR_GOOD : COLOR_MUTED),
        0);
}

void DisplayUi::tab_event(lv_event_t* event)
{
    auto* binding =
        static_cast<TabBinding*>(lv_event_get_user_data(event));
    if (binding->ui->settings_visible_.load(std::memory_order_relaxed) ||
        binding->ui->ota_active_.load(std::memory_order_relaxed) ||
        !binding->ui->input_enabled_.load(std::memory_order_relaxed)) return;
    binding->ui->requested_page_.store(
        page_from_tab(
            binding->tab_index,
            binding->ui->requested_page_.load(std::memory_order_relaxed)),
        std::memory_order_relaxed);
}

void DisplayUi::gesture_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (ui->settings_visible_.load(std::memory_order_relaxed) ||
        ui->ota_active_.load(std::memory_order_relaxed) ||
        !ui->input_enabled_.load(std::memory_order_relaxed)) return;
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    TouchDirection detected_direction;
    switch (direction) {
    case LV_DIR_LEFT:
        detected_direction = TouchDirection::Left;
        break;
    case LV_DIR_RIGHT:
        detected_direction = TouchDirection::Right;
        break;
    case LV_DIR_TOP:
        detected_direction = TouchDirection::Up;
        break;
    case LV_DIR_BOTTOM:
        detected_direction = TouchDirection::Down;
        break;
    default:
        return;
    }

    const auto swipe = navigation_swipe(detected_direction);
    if (!swipe.has_value()) {
        return;
    }

    const Page current =
        ui->requested_page_.load(std::memory_order_relaxed);
    ui->requested_page_.store(
        page_after_swipe(current, *swipe), std::memory_order_relaxed);
    lv_indev_wait_release(indev);
}

void DisplayUi::touch_down_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    ui->touch_down_.store(true, std::memory_order_relaxed);
}

void DisplayUi::ota_confirm_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (ui->ota_confirming_.load(std::memory_order_relaxed)) {
        ui->ota_confirmation_requested_.store(true, std::memory_order_relaxed);
    }
}

void DisplayUi::ota_deny_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (ui->ota_confirming_.load(std::memory_order_relaxed)) {
        ui->ota_denial_requested_.store(true, std::memory_order_relaxed);
    }
}

}  // namespace usage_panel::mosaico
