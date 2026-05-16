#include "attitude_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <cmath>
#include <lvgl.h>

#define TAG "AttitudeApp"

class AttitudeView {
public:
    AttitudeView(lv_obj_t* parent, LvglTheme* theme)
        : scr_w_(lv_obj_get_width(parent)),
          scr_h_(lv_obj_get_height(parent)) {
        auto* text_font = theme->text_font()->font();

        // Ball that rolls with gravity (bubble-level style)
        int half = 30;
        ball_ = lv_obj_create(parent);
        lv_obj_set_size(ball_, half, half);
        lv_obj_set_style_radius(ball_, half / 2, 0);
        lv_obj_set_style_bg_color(ball_, theme->text_color(), 0);
        lv_obj_set_style_border_width(ball_, 0, 0);
        lv_obj_set_pos(ball_, scr_w_ / 2 - half / 2, scr_h_ / 2 - half / 2);

        // Pitch/Roll readout
        info_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(info_label_, text_font, 0);
        lv_obj_set_style_text_color(info_label_, theme->text_color(), 0);
        lv_label_set_text(info_label_, "P: --  R: --");
        lv_obj_align(info_label_, LV_ALIGN_TOP_LEFT, 8, 8);

        // LVGL timer for stable 30fps update
        timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<AttitudeView*>(lv_timer_get_user_data(t))->Step();
        }, 33, this);

        ESP_LOGI(TAG, "Attitude view created");
    }

    ~AttitudeView() {
        if (timer_) { lv_timer_del(timer_); timer_ = nullptr; }
    }

    void Step() {
        auto imu = Board::GetInstance().GetImuData();

        float ax = imu.accel_x / 9.80665f;
        float ay = imu.accel_y / 9.80665f;
        float az = imu.accel_z / 9.80665f;

        // Heavy low-pass on accel angles (reference: a=0.05)
        float raw_r = atan2f(ay, -az) * 57.29578f;
        float raw_p = atan2f(-ax, -az) * 57.29578f;
        float a = 0.05f;
        smooth_roll_  = smooth_roll_  * (1.0f - a) + raw_r * a;
        smooth_pitch_ = smooth_pitch_ * (1.0f - a) + raw_p * a;
        if (fabsf(smooth_roll_)  < 0.5f) smooth_roll_  = 0;
        if (fabsf(smooth_pitch_) < 0.5f) smooth_pitch_ = 0;

        // Ball position from raw accel (bubble-level: ball goes to HIGH side)
        int ball_half = 15;
        int range_x = scr_w_ / 2 - ball_half;
        int range_y = scr_h_ / 2 - ball_half;
        int dx = (int)( ay * range_x);
        int dy = (int)(-ax * range_y);
        if (dx > range_x)  dx = range_x;
        if (dx < -range_x) dx = -range_x;
        if (dy > range_y)  dy = range_y;
        if (dy < -range_y) dy = -range_y;
        lv_obj_set_pos(ball_, scr_w_ / 2 + dx - ball_half, scr_h_ / 2 + dy - ball_half);

        // Update labels
        frame_count_++;
        if (frame_count_ % 3 == 0) {
            char buf[48];
            snprintf(buf, sizeof(buf), "R:%+.1f  P:%+.1f",
                     (double)smooth_roll_, (double)smooth_pitch_);
            lv_label_set_text(info_label_, buf);
        }
    }

private:
    lv_obj_t* ball_ = nullptr;
    lv_obj_t* info_label_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    int scr_w_, scr_h_;
    int frame_count_ = 0;
    float smooth_roll_ = 0, smooth_pitch_ = 0;
};

void AttitudeApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    view_ = new AttitudeView(screen, theme);
    ESP_LOGI(TAG, "Attitude entered");
}

void AttitudeApp::OnExit() {
    delete view_;
    view_ = nullptr;
    ESP_LOGI(TAG, "Attitude exited");
}

bool AttitudeApp::OnUpdate() {
    return false;  // LVGL timer drives updates
}
