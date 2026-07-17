#include "settings_app.h"

#include "audio/audio_codec.h"
#include "boards/common/board.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "system_info.h"

#include <esp_log.h>
#include <wifi_manager.h>

#include <cstdio>
#include <string>

#define TAG "SettingsApp"

LV_FONT_DECLARE(lv_font_montserrat_14);

namespace {

constexpr uint32_t kBackground = 0x05080D;
constexpr uint32_t kPanel = 0x101A29;
constexpr uint32_t kPanelDark = 0x09111D;
constexpr uint32_t kBorder = 0x1C2D43;
constexpr uint32_t kText = 0xF4F7FC;
constexpr uint32_t kMuted = 0x788BA5;
constexpr uint32_t kBlue = 0x67C7FF;
constexpr uint32_t kPurple = 0xB695FF;
constexpr uint32_t kGreen = 0x69E5AD;
constexpr uint32_t kAmber = 0xFFD27A;
constexpr uint32_t kRed = 0xFF857A;

struct DeviceSnapshot {
    std::string wifi;
    std::string ip;
    std::string signal;
    std::string battery;
    uint32_t wifi_color = kMuted;
    uint32_t signal_color = kMuted;
    uint32_t battery_color = kMuted;
};

struct ControlRefs {
    lv_obj_t* slider = nullptr;
    lv_obj_t* value_label = nullptr;
};

lv_obj_t* CreatePanel(lv_obj_t* parent, int x, int y, int width, int height,
                      uint32_t color, int radius) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    return panel;
}

void SetLabel(lv_obj_t* label, const char* text, const lv_font_t* font, uint32_t color) {
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void CreateSectionLabel(lv_obj_t* parent, int y, const char* text) {
    auto* marker = CreatePanel(parent, 18, y + 5, 18, 3, kBlue, 2);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);

    auto* label = lv_label_create(parent);
    SetLabel(label, text, &lv_font_montserrat_14, kMuted);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_set_pos(label, 44, y - 2);
}

ControlRefs CreateControlCard(lv_obj_t* parent, int y, int width,
                              const char* icon, const char* title, const char* subtitle,
                              int minimum, int maximum, int value, uint32_t accent,
                              const lv_font_t* icon_font) {
    auto* card = CreatePanel(parent, 14, y, width, 112, kPanel, 24);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(kPanelDark), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kBorder), 0);

    auto* icon_bubble = CreatePanel(card, 16, 14, 44, 44, accent, 16);
    lv_obj_set_style_bg_opa(icon_bubble, LV_OPA_20, 0);
    lv_obj_set_style_border_width(icon_bubble, 1, 0);
    lv_obj_set_style_border_color(icon_bubble, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(icon_bubble, LV_OPA_30, 0);

    auto* icon_label = lv_label_create(icon_bubble);
    SetLabel(icon_label, icon, icon_font, accent);
    lv_obj_center(icon_label);

    auto* title_label = lv_label_create(card);
    SetLabel(title_label, title, &lv_font_montserrat_14, kText);
    lv_obj_set_pos(title_label, 72, 13);

    auto* subtitle_label = lv_label_create(card);
    SetLabel(subtitle_label, subtitle, &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(subtitle_label, 72, 38);

    auto* value_pill = CreatePanel(card, width - 76, 16, 58, 30, 0x17253A, 15);
    lv_obj_set_style_border_width(value_pill, 1, 0);
    lv_obj_set_style_border_color(value_pill, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(value_pill, LV_OPA_30, 0);

    auto* value_label = lv_label_create(value_pill);
    char percentage[8];
    snprintf(percentage, sizeof(percentage), "%d%%", value);
    SetLabel(value_label, percentage, &lv_font_montserrat_14, accent);
    lv_obj_center(value_label);

    auto* slider = lv_slider_create(card);
    lv_obj_set_pos(slider, 18, 82);
    lv_obj_set_size(slider, width - 36, 8);
    lv_slider_set_range(slider, minimum, maximum);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(slider, 14);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x26364C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_width(slider, 24, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 24, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xF7FAFF), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(accent), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 12, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, lv_color_hex(accent), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_30, LV_PART_KNOB);

    return {.slider = slider, .value_label = value_label};
}

lv_obj_t* CreateInfoRow(lv_obj_t* card, int y, int card_width, const char* icon,
                        const char* title, const char* value, uint32_t accent,
                        const lv_font_t* icon_font, bool divider = true) {
    auto* icon_bubble = CreatePanel(card, 14, y + 9, 34, 34, 0x152337, 12);
    lv_obj_set_style_border_width(icon_bubble, 1, 0);
    lv_obj_set_style_border_color(icon_bubble, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(icon_bubble, LV_OPA_30, 0);

    auto* icon_label = lv_label_create(icon_bubble);
    SetLabel(icon_label, icon, icon_font, accent);
    lv_obj_center(icon_label);

    auto* title_label = lv_label_create(card);
    SetLabel(title_label, title, &lv_font_montserrat_14, kMuted);
    lv_obj_set_pos(title_label, 60, y + 17);

    auto* value_label = lv_label_create(card);
    SetLabel(value_label, value, &lv_font_montserrat_14, accent);
    lv_obj_set_pos(value_label, 148, y + 17);
    lv_obj_set_size(value_label, card_width - 164, 20);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);

    if (divider) {
        auto* line = CreatePanel(card, 60, y + 51, card_width - 76, 1, 0x1A2A3F, 1);
        lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    }
    return value_label;
}

DeviceSnapshot ReadDeviceSnapshot() {
    DeviceSnapshot snapshot;
    auto& wifi = WifiManager::GetInstance();
    auto& board = Board::GetInstance();

    if (wifi.IsConnected()) {
        snapshot.wifi = wifi.GetSsid();
        if (snapshot.wifi.empty()) snapshot.wifi = "Connected";
        snapshot.ip = wifi.GetIpAddress();
        if (snapshot.ip.empty()) snapshot.ip = "--";

        const int rssi = wifi.GetRssi();
        char signal[24];
        snprintf(signal, sizeof(signal), "%d dBm", rssi);
        snapshot.signal = signal;
        snapshot.wifi_color = kGreen;
        snapshot.signal_color = rssi >= -60 ? kGreen : (rssi >= -75 ? kAmber : kRed);
    } else {
        snapshot.wifi = "Not connected";
        snapshot.ip = "--";
        snapshot.signal = "--";
        snapshot.wifi_color = kRed;
        snapshot.signal_color = kMuted;
    }

    int level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(level, charging, discharging)) {
        if (level < 0) level = 0;
        if (level > 100) level = 100;
        char battery[32];
        if (charging) {
            snprintf(battery, sizeof(battery), "%d%%  CHARGING", level);
            snapshot.battery_color = kGreen;
        } else {
            snprintf(battery, sizeof(battery), "%d%%", level);
            snapshot.battery_color = level <= 20 ? kRed : (level <= 40 ? kAmber : kBlue);
        }
        snapshot.battery = battery;
    } else {
        snapshot.battery = "Unavailable";
        snapshot.battery_color = kMuted;
    }

    return snapshot;
}

std::string FirmwareVersion() {
    const std::string user_agent = SystemInfo::GetUserAgent();
    const auto separator = user_agent.find_last_of('/');
    std::string version = separator == std::string::npos ? user_agent : user_agent.substr(separator + 1);
    if (!version.empty() && version.front() != 'v') version.insert(version.begin(), 'v');
    return version.empty() ? "--" : version;
}

}  // namespace

void SettingsApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    display_ = ctx.display;
    refresh_ticks_ = 0;

    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* text_font = theme->text_font()->font();
    auto* icon_font = theme->icon_font()->font();
    const int width = ctx.display->width();
    const int height = ctx.display->height();
    const int card_width = width - 28;
    const DeviceSnapshot snapshot = ReadDeviceSnapshot();

    auto& board = Board::GetInstance();
    auto* codec = board.GetAudioCodec();
    auto* backlight = board.GetBacklight();
    committed_volume_ = codec ? codec->output_volume() : 50;
    committed_brightness_ = backlight ? backlight->brightness() : 75;

    lv_obj_set_size(screen, width, height);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x07111D), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    auto* glow = CreatePanel(screen, width - 124, -74, 204, 204, 0x174E7E, 102);
    lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

    root_ = lv_obj_create(screen);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, width, height);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_scroll_dir(root_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(root_, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBlue), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(root_, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(root_, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);

    auto* title = lv_label_create(root_);
    SetLabel(title, "Settings", text_font, kText);
    lv_obj_set_pos(title, 18, 14);

    auto* subtitle = lv_label_create(root_);
    SetLabel(subtitle, "DEVICE CONTROL", &lv_font_montserrat_14, kBlue);
    lv_obj_set_style_text_letter_space(subtitle, 2, 0);
    lv_obj_set_pos(subtitle, 20, 53);

    auto* status_pill = CreatePanel(root_, width - 102, 20, 84, 28, 0x10283A, 14);
    lv_obj_set_style_border_width(status_pill, 1, 0);
    lv_obj_set_style_border_color(status_pill, lv_color_hex(kGreen), 0);
    lv_obj_set_style_border_opa(status_pill, LV_OPA_30, 0);
    auto* status = lv_label_create(status_pill);
    SetLabel(status, "READY", &lv_font_montserrat_14, kGreen);
    lv_obj_center(status);

    CreateSectionLabel(root_, 84, "SOUND & DISPLAY");

    const ControlRefs volume = CreateControlCard(root_, 106, card_width,
                                                  FONT_AWESOME_VOLUME_HIGH,
                                                  "Volume", "Speaker output",
                                                  0, 100, committed_volume_, kBlue, icon_font);
    volume_slider_ = volume.slider;
    volume_value_label_ = volume.value_label;
    lv_obj_add_event_cb(volume_slider_, VolumeEvent, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(volume_slider_, VolumeEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(volume_slider_, VolumeEvent, LV_EVENT_PRESS_LOST, this);

    const ControlRefs brightness = CreateControlCard(root_, 230, card_width,
                                                      FONT_AWESOME_BRIGHTNESS,
                                                      "Brightness", "AMOLED intensity",
                                                      10, 100, committed_brightness_, kPurple, icon_font);
    brightness_slider_ = brightness.slider;
    brightness_value_label_ = brightness.value_label;
    lv_obj_add_event_cb(brightness_slider_, BrightnessEvent, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(brightness_slider_, BrightnessEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(brightness_slider_, BrightnessEvent, LV_EVENT_PRESS_LOST, this);

    CreateSectionLabel(root_, 366, "CONNECTIVITY");
    auto* network_card = CreatePanel(root_, 14, 388, card_width, 166, kPanelDark, 24);
    lv_obj_set_style_border_width(network_card, 1, 0);
    lv_obj_set_style_border_color(network_card, lv_color_hex(kBorder), 0);
    wifi_value_label_ = CreateInfoRow(network_card, 0, card_width, FONT_AWESOME_WIFI,
                                      "Wi-Fi", snapshot.wifi.c_str(), snapshot.wifi_color,
                                      icon_font);
    ip_value_label_ = CreateInfoRow(network_card, 52, card_width, FONT_AWESOME_GLOBE,
                                    "IP address", snapshot.ip.c_str(), kBlue,
                                    icon_font);
    signal_value_label_ = CreateInfoRow(network_card, 104, card_width, FONT_AWESOME_SIGNAL,
                                        "Signal", snapshot.signal.c_str(), snapshot.signal_color,
                                        icon_font, false);

    CreateSectionLabel(root_, 582, "DEVICE");
    auto* device_card = CreatePanel(root_, 14, 604, card_width, 218, kPanelDark, 24);
    lv_obj_set_style_border_width(device_card, 1, 0);
    lv_obj_set_style_border_color(device_card, lv_color_hex(kBorder), 0);
    battery_value_label_ = CreateInfoRow(device_card, 0, card_width, FONT_AWESOME_BATTERY_FULL,
                                         "Battery", snapshot.battery.c_str(), snapshot.battery_color,
                                         icon_font);
    CreateInfoRow(device_card, 52, card_width, FONT_AWESOME_MICROCHIP_AI,
                  "Hardware", BOARD_NAME, kPurple, icon_font);
    const std::string firmware = FirmwareVersion();
    CreateInfoRow(device_card, 104, card_width, FONT_AWESOME_CIRCLE_INFO,
                  "Firmware", firmware.c_str(), kBlue, icon_font);
    const std::string mac = SystemInfo::GetMacAddress();
    CreateInfoRow(device_card, 156, card_width, FONT_AWESOME_KEY,
                  "Device ID", mac.c_str(), kMuted, icon_font, false);

    auto* footer = lv_label_create(root_);
    SetLabel(footer, "Swipe to explore  /  BOOT to go back", &lv_font_montserrat_14, 0x53667F);
    lv_obj_set_pos(footer, 18, 844);
    lv_obj_set_size(footer, width - 36, 24);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);

    auto* scroll_end = CreatePanel(root_, width / 2, 882, 1, 1, kBackground, 0);
    lv_obj_set_style_bg_opa(scroll_end, LV_OPA_TRANSP, 0);

    ESP_LOGI(TAG, "Settings entered: volume=%d brightness=%d", committed_volume_, committed_brightness_);
}

void SettingsApp::VolumeEvent(lv_event_t* event) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
    if (!self || !self->volume_slider_) return;

    const int value = lv_slider_get_value(self->volume_slider_);
    self->UpdatePercentLabel(self->volume_value_label_, value);

    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;

    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec && value != self->committed_volume_) {
        codec->SetOutputVolume(value);
        self->committed_volume_ = value;
    }
}

void SettingsApp::BrightnessEvent(lv_event_t* event) {
    auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
    if (!self || !self->brightness_slider_) return;

    const int value = lv_slider_get_value(self->brightness_slider_);
    self->UpdatePercentLabel(self->brightness_value_label_, value);

    auto* backlight = Board::GetInstance().GetBacklight();
    if (!backlight) return;

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        backlight->SetBrightness(value, false);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
               value != self->committed_brightness_) {
        backlight->SetBrightness(value, true);
        self->committed_brightness_ = value;
    }
}

void SettingsApp::UpdatePercentLabel(lv_obj_t* label, int value) {
    if (!label) return;
    char percentage[8];
    snprintf(percentage, sizeof(percentage), "%d%%", value);
    lv_label_set_text(label, percentage);
}

void SettingsApp::RefreshDynamicInfo() {
    if (!display_ || !root_) return;
    const DeviceSnapshot snapshot = ReadDeviceSnapshot();

    DisplayLockGuard lock(display_);
    if (!root_) return;

    if (wifi_value_label_) {
        lv_label_set_text(wifi_value_label_, snapshot.wifi.c_str());
        lv_obj_set_style_text_color(wifi_value_label_, lv_color_hex(snapshot.wifi_color), 0);
    }
    if (ip_value_label_) {
        lv_label_set_text(ip_value_label_, snapshot.ip.c_str());
    }
    if (signal_value_label_) {
        lv_label_set_text(signal_value_label_, snapshot.signal.c_str());
        lv_obj_set_style_text_color(signal_value_label_, lv_color_hex(snapshot.signal_color), 0);
    }
    if (battery_value_label_) {
        lv_label_set_text(battery_value_label_, snapshot.battery.c_str());
        lv_obj_set_style_text_color(battery_value_label_, lv_color_hex(snapshot.battery_color), 0);
    }
}

void SettingsApp::CommitPendingValues() {
    auto& board = Board::GetInstance();
    if (volume_slider_) {
        const int value = lv_slider_get_value(volume_slider_);
        auto* codec = board.GetAudioCodec();
        if (codec && value != committed_volume_) {
            codec->SetOutputVolume(value);
            committed_volume_ = value;
        }
    }
    if (brightness_slider_) {
        const int value = lv_slider_get_value(brightness_slider_);
        auto* backlight = board.GetBacklight();
        if (backlight && value != committed_brightness_) {
            backlight->SetBrightness(value, true);
            committed_brightness_ = value;
        }
    }
}

bool SettingsApp::OnUpdate() {
    if (!root_) return false;
    if (++refresh_ticks_ >= 2) {
        refresh_ticks_ = 0;
        RefreshDynamicInfo();
    }
    return false;
}

void SettingsApp::OnExit() {
    CommitPendingValues();

    display_ = nullptr;
    root_ = nullptr;
    volume_slider_ = nullptr;
    brightness_slider_ = nullptr;
    volume_value_label_ = nullptr;
    brightness_value_label_ = nullptr;
    wifi_value_label_ = nullptr;
    ip_value_label_ = nullptr;
    signal_value_label_ = nullptr;
    battery_value_label_ = nullptr;

    ESP_LOGI(TAG, "Settings exited");
}
