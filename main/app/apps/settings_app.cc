#include "settings_app.h"
#include "boards/common/board.h"
#include "system_info.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <wifi_manager.h>
#include <esp_netif.h>
#include <ctime>

#define TAG "SettingsApp"

void SettingsApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* text_font = theme->text_font()->font();

    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    // Title
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "System Info");
    lv_obj_set_style_text_font(title, theme->large_icon_font()->font(), 0);
    lv_obj_set_style_text_color(title, theme->text_color(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Content area
    lv_obj_t* list = lv_obj_create(screen);
    lv_obj_set_size(list, ctx.display->width() - 32, ctx.display->height() - 80);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    auto& board = Board::GetInstance();
    auto& wifi = WifiManager::GetInstance();
    auto margin_top_val = 12;

    // WiFi SSID
    {
        std::string info = "WiFi: ";
        info += wifi.IsConnected() ? wifi.GetSsid() : "Disconnected";

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, info.c_str());
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
    }

    // IP
    {
        std::string info = "IP: ";
        info += wifi.IsConnected() ? wifi.GetIpAddress() : "--";

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, info.c_str());
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
    }

    // Signal
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Signal: %d dBm", wifi.GetRssi());

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
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

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, info.c_str());
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
    }

    // Firmware
    {
        std::string info = SystemInfo::GetUserAgent();

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, info.c_str());
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
    }

    // Board
    {
        std::string info = "Board: ";
        info += BOARD_NAME;

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, info.c_str());
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
    }

    // MAC
    {
        std::string info = "MAC: ";
        info += SystemInfo::GetMacAddress();

        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, info.c_str());
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, theme->text_color(), 0);
        lv_obj_set_style_margin_top(label, margin_top_val, 0);
    }

    ESP_LOGI(TAG, "Settings entered");
}

void SettingsApp::OnExit() {
    ESP_LOGI(TAG, "Settings exited");
}
