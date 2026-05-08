#include "settings_app.h"
#include "boards/common/board.h"
#include "system_info.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "audio/audio_codec.h"

#include <esp_log.h>
#include <wifi_manager.h>
#include <esp_netif.h>
#include <ctime>

#define TAG "SettingsApp"

// Callback data for sliders
struct SliderCtx {
    lv_obj_t* label;
    const char* prefix;
};

static void volume_event_cb(lv_event_t* e) {
    auto* slider = lv_event_get_target_obj(e);
    int value = lv_slider_get_value(slider);
    auto* ctx = static_cast<SliderCtx*>(lv_event_get_user_data(e));

    char buf[32];
    snprintf(buf, sizeof(buf), "%s: %d", ctx->prefix, value);
    lv_label_set_text(ctx->label, buf);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        codec->SetOutputVolume(value);
    }
}

static void brightness_event_cb(lv_event_t* e) {
    auto* slider = lv_event_get_target_obj(e);
    int value = lv_slider_get_value(slider);
    auto* ctx = static_cast<SliderCtx*>(lv_event_get_user_data(e));

    char buf[32];
    snprintf(buf, sizeof(buf), "%s: %d", ctx->prefix, value);
    lv_label_set_text(ctx->label, buf);

    auto* backlight = Board::GetInstance().GetBacklight();
    if (backlight) {
        backlight->SetBrightness(value, true);
    }
}

void SettingsApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* text_font = theme->text_font()->font();
    auto fg_color = lv_color_white();

    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    // Scrollable main container
    lv_obj_t* cont = lv_obj_create(screen);
    lv_obj_set_size(cont, ctx.display->width(), ctx.display->height());
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 16, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    auto& board = Board::GetInstance();
    auto& wifi = WifiManager::GetInstance();
    int margin = 12;

    // === Volume ===
    {
        auto codec = board.GetAudioCodec();
        int vol = codec ? codec->output_volume() : 50;

        auto* sctx = new SliderCtx{nullptr, "Volume"};
        lv_obj_t* label = lv_label_create(cont);
        char buf[32];
        snprintf(buf, sizeof(buf), "Volume: %d", vol);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, fg_color, 0);
        sctx->label = label;

        lv_obj_t* slider = lv_slider_create(cont);
        lv_obj_set_width(slider, ctx.display->width() - 32);
        lv_slider_set_range(slider, 0, 100);
        lv_slider_set_value(slider, vol, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, volume_event_cb, LV_EVENT_VALUE_CHANGED, sctx);
        lv_obj_set_style_margin_top(slider, 4, 0);
        lv_obj_set_style_margin_bottom(slider, margin, 0);
        // Dark theme styling
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x666666), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    }

    // === Brightness ===
    {
        auto* bl = board.GetBacklight();
        int br = bl ? bl->brightness() : 75;

        auto* sctx = new SliderCtx{nullptr, "Brightness"};
        lv_obj_t* label = lv_label_create(cont);
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %d", br);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, fg_color, 0);
        sctx->label = label;

        lv_obj_t* slider = lv_slider_create(cont);
        lv_obj_set_width(slider, ctx.display->width() - 32);
        lv_slider_set_range(slider, 10, 100);
        lv_slider_set_value(slider, br, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, brightness_event_cb, LV_EVENT_VALUE_CHANGED, sctx);
        lv_obj_set_style_margin_top(slider, 4, 0);
        lv_obj_set_style_margin_bottom(slider, margin * 2, 0);
        // Dark theme styling
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x666666), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    }

    // === Divider ===
    {
        lv_obj_t* line = lv_obj_create(cont);
        lv_obj_set_size(line, ctx.display->width() - 32, 1);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_style_margin_bottom(line, margin, 0);
    }

    // === System Info ===
    auto make_label = [&](const char* text) {
        lv_obj_t* l = lv_label_create(cont);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, text_font, 0);
        lv_obj_set_style_text_color(l, fg_color, 0);
        lv_obj_set_style_margin_top(l, margin, 0);
        return l;
    };

    // WiFi
    {
        std::string info = "WiFi: ";
        info += wifi.IsConnected() ? wifi.GetSsid() : "Disconnected";
        make_label(info.c_str());
    }

    // IP
    {
        std::string info = "IP: ";
        info += wifi.IsConnected() ? wifi.GetIpAddress() : "--";
        make_label(info.c_str());
    }

    // Signal
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Signal: %d dBm", wifi.GetRssi());
        make_label(buf);
    }

    // Battery
    {
        int level;
        bool charging, discharging;
        std::string info = "Battery: ";
        if (board.GetBatteryLevel(level, charging, discharging)) {
            info += std::to_string(level) + "%";
            info += charging ? " (Charging)" : (discharging ? " (Discharging)" : "");
        } else {
            info += "N/A";
        }
        make_label(info.c_str());
    }

    // Firmware
    make_label(SystemInfo::GetUserAgent().c_str());

    // Board
    {
        std::string info = "Board: ";
        info += BOARD_NAME;
        make_label(info.c_str());
    }

    // MAC
    {
        std::string info = "MAC: ";
        info += SystemInfo::GetMacAddress();
        make_label(info.c_str());
    }

    ESP_LOGI(TAG, "Settings entered");
}

void SettingsApp::OnExit() {
    ESP_LOGI(TAG, "Settings exited");
}
