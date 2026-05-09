#include "attitude_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <cmath>

#define TAG "AttitudeApp"

class AttitudeView {
public:
    AttitudeView(lv_obj_t* parent, LvglTheme* theme) {
        auto* text_font = theme->text_font()->font();
        auto* icon_font = theme->large_icon_font()->font();

        // Data labels: pitch, roll
        pitch_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(pitch_label_, icon_font, 0);
        lv_obj_set_style_text_color(pitch_label_, theme->text_color(), 0);
        lv_label_set_text(pitch_label_, "P: --");
        lv_obj_align(pitch_label_, LV_ALIGN_TOP_MID, 0, 20);

        roll_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(roll_label_, icon_font, 0);
        lv_obj_set_style_text_color(roll_label_, theme->text_color(), 0);
        lv_label_set_text(roll_label_, "R: --");
        lv_obj_align(roll_label_, LV_ALIGN_TOP_MID, 0, 80);

        // Crosshair: a container with lines showing tilt
        crosshair_ = lv_obj_create(parent);
        lv_obj_set_size(crosshair_, 160, 160);
        lv_obj_set_style_bg_opa(crosshair_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(crosshair_, 0, 0);
        lv_obj_align(crosshair_, LV_ALIGN_CENTER, 0, 0);

        // Outer circle
        lv_obj_t* circle = lv_obj_create(crosshair_);
        lv_obj_set_size(circle, 120, 120);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(circle, theme->text_color(), 0);
        lv_obj_set_style_border_width(circle, 2, 0);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_center(circle);

        // Inner dot
        dot_ = lv_obj_create(crosshair_);
        lv_obj_set_size(dot_, 12, 12);
        lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot_, theme->text_color(), 0);
        lv_obj_set_style_border_width(dot_, 0, 0);
        lv_obj_center(dot_);

        // Status
        status_ = lv_label_create(parent);
        lv_obj_set_style_text_font(status_, text_font, 0);
        lv_obj_set_style_text_color(status_, theme->text_color(), 0);
        lv_label_set_text(status_, "IMU 未就绪");
        lv_obj_align(status_, LV_ALIGN_BOTTOM_MID, 0, -20);

        ESP_LOGI(TAG, "Attitude view created");
    }

    void Update(const ImuData& imu) {
        if (!imu.valid) {
            lv_label_set_text(status_, "IMU 未就绪");
            return;
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "Pitch: %+.1f°", imu.pitch);
        lv_label_set_text(pitch_label_, buf);

        snprintf(buf, sizeof(buf), "Roll: %+.1f°", imu.roll);
        lv_label_set_text(roll_label_, buf);

        // Move dot: max displacement ±55px for ±45° tilt
        int dx = (int)(imu.roll  * 55.0f / 45.0f);
        int dy = (int)(imu.pitch * 55.0f / 45.0f);
        if (dx > 55) dx = 55;
        if (dx < -55) dx = -55;
        if (dy > 55) dy = 55;
        if (dy < -55) dy = -55;
        lv_obj_align(dot_, LV_ALIGN_CENTER, dx, dy);

        lv_label_set_text(status_, "OK");
    }

private:
    lv_obj_t* pitch_label_ = nullptr;
    lv_obj_t* roll_label_ = nullptr;
    lv_obj_t* crosshair_ = nullptr;
    lv_obj_t* dot_ = nullptr;
    lv_obj_t* status_ = nullptr;
};

void AttitudeApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    view_ = new AttitudeView(screen, theme);
    ESP_LOGI(TAG, "Attitude entered");
}

void AttitudeApp::OnExit() {
    delete view_;
    view_ = nullptr;
    ESP_LOGI(TAG, "Attitude exited");
}

bool AttitudeApp::OnUpdate() {
    if (view_) {
        view_->Update(Board::GetInstance().GetImuData());
    }
    return false;
}
