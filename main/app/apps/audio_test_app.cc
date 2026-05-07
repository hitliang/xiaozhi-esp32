#include "audio_test_app.h"
#include "application.h"
#include "audio/audio_service.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>

#define TAG "AudioTest"

void AudioTestApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* text_font = theme->text_font()->font();

    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "音频测试");
    lv_obj_set_style_text_font(title, theme->large_icon_font()->font(), 0);
    lv_obj_set_style_text_color(title, theme->text_color(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* desc = lv_label_create(screen);
    lv_label_set_text(desc, "播放 440Hz 标准音高测试\n\n功能待实现");
    lv_obj_set_style_text_font(desc, text_font, 0);
    lv_obj_set_style_text_color(desc, theme->text_color(), 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -20);

    ESP_LOGI(TAG, "Audio test entered");
}

void AudioTestApp::OnExit() {
    ESP_LOGI(TAG, "Audio test exited");
}
