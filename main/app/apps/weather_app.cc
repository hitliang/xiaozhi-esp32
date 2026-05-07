#include "weather_app.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>

#define TAG "WeatherApp"

void WeatherApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* text_font = theme->text_font()->font();

    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    // Title
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "天气");
    lv_obj_set_style_text_font(title, theme->large_icon_font()->font(), 0);
    lv_obj_set_style_text_color(title, theme->text_color(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Weather icon
    lv_obj_t* icon = lv_label_create(screen);
    lv_label_set_text(icon, FONT_AWESOME_CLOUD_SUN);
    lv_obj_set_style_text_font(icon, theme->large_icon_font()->font(), 0);
    lv_obj_set_style_text_color(icon, theme->text_color(), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);

    // Status text
    lv_obj_t* status = lv_label_create(screen);
    lv_label_set_text(status, "天气功能开发中...\n需配置高德API Key");
    lv_obj_set_style_text_font(status, text_font, 0);
    lv_obj_set_style_text_color(status, theme->text_color(), 0);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 30);

    ESP_LOGI(TAG, "Weather entered (placeholder)");
}

void WeatherApp::OnExit() {
    ESP_LOGI(TAG, "Weather exited");
}
