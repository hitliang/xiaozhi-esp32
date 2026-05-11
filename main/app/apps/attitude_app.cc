#include "attitude_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <cmath>

#define TAG "AttitudeApp"

class AttitudeView {
public:
    AttitudeView(lv_obj_t* parent, LvglTheme* theme, Display* display)
        : display_(display) {
        DisplayLockGuard lock(display_);
        auto* text_font = theme->text_font()->font();
        auto* icon_font = theme->large_icon_font()->font();

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

        crosshair_ = lv_obj_create(parent);
        lv_obj_set_size(crosshair_, 160, 160);
        lv_obj_set_style_bg_opa(crosshair_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(crosshair_, 0, 0);
        lv_obj_align(crosshair_, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* circle = lv_obj_create(crosshair_);
        lv_obj_set_size(circle, 120, 120);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(circle, theme->text_color(), 0);
        lv_obj_set_style_border_width(circle, 2, 0);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_center(circle);

        dot_ = lv_obj_create(crosshair_);
        lv_obj_set_size(dot_, 12, 12);
        lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot_, theme->text_color(), 0);
        lv_obj_set_style_border_width(dot_, 0, 0);
        lv_obj_center(dot_);

        status_ = lv_label_create(parent);
        lv_obj_set_style_text_font(status_, text_font, 0);
        lv_obj_set_style_text_color(status_, theme->text_color(), 0);
        lv_label_set_text(status_, "IMU 未就绪");
        lv_obj_align(status_, LV_ALIGN_BOTTOM_MID, 0, -20);

        ESP_LOGI(TAG, "Attitude view created");
    }

    void Update(const ImuData& imu) {
        frame_count_++;
        if (frame_count_ % 3 != 0) return;
        frame_count_ = 0;

        DisplayLockGuard lock(display_);

        if (!imu.valid) {
            if (!shown_not_ready_) {
                lv_label_set_text(status_, "IMU 未就绪");
                shown_not_ready_ = true;
            }
            return;
        }
        shown_not_ready_ = false;

        char buf[32];
        int new_dx = (int)(imu.roll  * 55.0f / 45.0f);
        int new_dy = (int)(imu.pitch * 55.0f / 45.0f);
        if (new_dx > 55) new_dx = 55;
        if (new_dx < -55) new_dx = -55;
        if (new_dy > 55) new_dy = 55;
        if (new_dy < -55) new_dy = -55;

        if (new_dx != last_dx_ || new_dy != last_dy_) {
            lv_obj_set_pos(dot_, 80 + new_dx - 6, 80 + new_dy - 6);
            last_dx_ = new_dx;
            last_dy_ = new_dy;
        }

        int pitch_int = (int)(imu.pitch * 10.0f);
        int roll_int  = (int)(imu.roll  * 10.0f);
        if (pitch_int != last_pitch_) {
            snprintf(buf, sizeof(buf), "Pitch: %+.1f", imu.pitch);
            lv_label_set_text(pitch_label_, buf);
            last_pitch_ = pitch_int;
        }
        if (roll_int != last_roll_) {
            snprintf(buf, sizeof(buf), "Roll: %+.1f", imu.roll);
            lv_label_set_text(roll_label_, buf);
            last_roll_ = roll_int;
        }

        if (!shown_ok_) {
            lv_label_set_text(status_, "OK");
            shown_ok_ = true;
        }
    }

private:
    lv_obj_t* pitch_label_ = nullptr;
    lv_obj_t* roll_label_ = nullptr;
    lv_obj_t* crosshair_ = nullptr;
    lv_obj_t* dot_ = nullptr;
    lv_obj_t* status_ = nullptr;
    Display* display_ = nullptr;
    int frame_count_ = 0;
    int last_dx_ = 999, last_dy_ = 999;
    int last_pitch_ = 9999, last_roll_ = 9999;
    bool shown_not_ready_ = false, shown_ok_ = false;
};

void AttitudeApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    view_ = new AttitudeView(screen, theme, ctx.display);
    display_ = ctx.display;
    ESP_LOGI(TAG, "Attitude entered");
}

void AttitudeApp::OnExit() {
    delete view_;
    view_ = nullptr;
    display_ = nullptr;
    ESP_LOGI(TAG, "Attitude exited");
}

bool AttitudeApp::OnUpdate() {
    if (view_) {
        view_->Update(Board::GetInstance().GetImuData());
    }
    return false;
}
