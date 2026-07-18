#include "attitude_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "boards/common/board.h"

#include <algorithm>
#include <cmath>
#include <esp_log.h>

#define TAG "AttitudeApp"

LV_FONT_DECLARE(lv_font_montserrat_14);

namespace {

constexpr float kGravity = 9.80665f;
constexpr float kRadiansToDegrees = 57.2957795f;
constexpr float kLevelThresholdDegrees = 2.0f;
constexpr float kBallLimitDegrees = 30.0f;

lv_obj_t* CreateShape(lv_obj_t* parent, int x, int y, int width, int height,
                      uint32_t color, int radius) {
    auto* object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

}  // namespace

class AttitudeView {
public:
    AttitudeView(lv_obj_t* parent, LvglTheme* theme) {
        const int width = lv_obj_get_width(parent);
        const int height = lv_obj_get_height(parent);
        auto* text_font = theme->text_font()->font();

        auto* glow = CreateShape(parent, width - 150, -74, 220, 220,
                                 0x143A68, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

        title_ = lv_label_create(parent);
        lv_label_set_text(title_, "BUBBLE LEVEL");
        lv_obj_set_style_text_font(title_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title_, lv_color_hex(0x7890AA), 0);
        lv_obj_set_style_text_letter_space(title_, 2, 0);
        lv_obj_set_pos(title_, 18, 18);

        status_ = lv_label_create(parent);
        lv_label_set_text(status_, "CALIBRATING");
        lv_obj_set_style_text_font(status_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(status_, lv_color_hex(0xE7CB72), 0);
        lv_obj_set_pos(status_, width - 126, 18);
        lv_obj_set_size(status_, 108, 20);
        lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_RIGHT, 0);

        constexpr int level_size = 258;
        constexpr int level_y = 58;
        level_x_ = (width - level_size) / 2;
        level_y_ = level_y;
        level_radius_ = level_size / 2;

        level_ring_ = CreateShape(parent, level_x_, level_y_, level_size, level_size,
                                  0x07111D, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_grad_color(level_ring_, lv_color_hex(0x0E2034), 0);
        lv_obj_set_style_bg_grad_dir(level_ring_, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(level_ring_, 2, 0);
        lv_obj_set_style_border_color(level_ring_, lv_color_hex(0x31506D), 0);

        auto* horizontal = CreateShape(level_ring_, 24, level_radius_ - 1,
                                       level_size - 48, 2, 0x29445F, 1);
        auto* vertical = CreateShape(level_ring_, level_radius_ - 1, 24,
                                     2, level_size - 48, 0x29445F, 1);
        lv_obj_set_style_bg_opa(horizontal, LV_OPA_60, 0);
        lv_obj_set_style_bg_opa(vertical, LV_OPA_60, 0);

        target_ring_ = CreateShape(level_ring_, level_radius_ - 30,
                                   level_radius_ - 30, 60, 60,
                                   0x000000, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(target_ring_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(target_ring_, 2, 0);
        lv_obj_set_style_border_color(target_ring_, lv_color_hex(0x57D9B2), 0);
        lv_obj_set_style_border_opa(target_ring_, LV_OPA_60, 0);

        ball_ = CreateShape(level_ring_, level_radius_ - 16,
                            level_radius_ - 16, 32, 32,
                            0x66C7FF, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_color(ball_, lv_color_hex(0x66C7FF), 0);
        lv_obj_set_style_shadow_width(ball_, 16, 0);
        lv_obj_set_style_shadow_opa(ball_, LV_OPA_40, 0);

        info_panel_ = CreateShape(parent, 24, 330, width - 48, 46, 0x0D1826, 14);
        lv_obj_set_style_border_width(info_panel_, 1, 0);
        lv_obj_set_style_border_color(info_panel_, lv_color_hex(0x20364E), 0);

        info_label_ = lv_label_create(info_panel_);
        lv_label_set_text(info_label_, "ROLL  --.-     PITCH  --.-");
        lv_obj_set_style_text_font(info_label_, text_font, 0);
        lv_obj_set_style_text_color(info_label_, lv_color_hex(0xE5EDF7), 0);
        lv_obj_center(info_label_);

        zero_button_ = lv_button_create(parent);
        lv_obj_set_pos(zero_button_, (width - 132) / 2, height - 58);
        lv_obj_set_size(zero_button_, 132, 42);
        lv_obj_set_style_radius(zero_button_, 15, 0);
        lv_obj_set_style_bg_color(zero_button_, lv_color_hex(0x173553), 0);
        lv_obj_set_style_bg_color(zero_button_, lv_color_hex(0x24537E), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(zero_button_, 1, 0);
        lv_obj_set_style_border_color(zero_button_, lv_color_hex(0x3977A9), 0);
        auto* zero_label = lv_label_create(zero_button_);
        lv_label_set_text(zero_label, "SET ZERO");
        lv_obj_set_style_text_font(zero_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(zero_label, lv_color_hex(0xDCEEFF), 0);
        lv_obj_center(zero_label);
        lv_obj_add_event_cb(zero_button_, OnZeroClicked, LV_EVENT_CLICKED, this);

        timer_ = lv_timer_create(OnTimer, 50, this);
        ESP_LOGI(TAG, "Attitude view created at 20 fps");
    }

    ~AttitudeView() {
        if (timer_ != nullptr) {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }
    }

private:
    lv_obj_t* title_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* level_ring_ = nullptr;
    lv_obj_t* target_ring_ = nullptr;
    lv_obj_t* ball_ = nullptr;
    lv_obj_t* info_panel_ = nullptr;
    lv_obj_t* info_label_ = nullptr;
    lv_obj_t* zero_button_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    int level_x_ = 0;
    int level_y_ = 0;
    int level_radius_ = 0;
    bool initialized_ = false;
    bool zero_requested_ = false;
    float smooth_roll_ = 0.0f;
    float smooth_pitch_ = 0.0f;
    float zero_roll_ = 0.0f;
    float zero_pitch_ = 0.0f;
    uint32_t frame_count_ = 0;

    void Step() {
        const ImuData imu = Board::GetInstance().GetImuData();
        if (!imu.valid) {
            lv_label_set_text(status_, "IMU WAIT");
            lv_obj_set_style_text_color(status_, lv_color_hex(0xE7CB72), 0);
            lv_obj_add_flag(ball_, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        const float ax = imu.accel_x / kGravity;
        const float ay = imu.accel_y / kGravity;
        const float az = imu.accel_z / kGravity;
        const float raw_roll = std::atan2(ay, -az) * kRadiansToDegrees;
        const float raw_pitch = std::atan2(-ax, -az) * kRadiansToDegrees;

        if (!initialized_) {
            smooth_roll_ = raw_roll;
            smooth_pitch_ = raw_pitch;
            initialized_ = true;
            lv_obj_remove_flag(ball_, LV_OBJ_FLAG_HIDDEN);
        } else {
            constexpr float alpha = 0.16f;
            smooth_roll_ += alpha * (raw_roll - smooth_roll_);
            smooth_pitch_ += alpha * (raw_pitch - smooth_pitch_);
        }

        if (zero_requested_) {
            zero_roll_ = smooth_roll_;
            zero_pitch_ = smooth_pitch_;
            zero_requested_ = false;
            ESP_LOGI(TAG, "Attitude zero set: roll=%.2f pitch=%.2f",
                     (double)zero_roll_, (double)zero_pitch_);
        }

        float roll = smooth_roll_ - zero_roll_;
        float pitch = smooth_pitch_ - zero_pitch_;
        if (std::fabs(roll) < 0.15f) roll = 0.0f;
        if (std::fabs(pitch) < 0.15f) pitch = 0.0f;

        constexpr int ball_radius = 16;
        const int travel = level_radius_ - ball_radius - 15;
        const float normalized_x = std::clamp(roll / kBallLimitDegrees, -1.0f, 1.0f);
        const float normalized_y = std::clamp(pitch / kBallLimitDegrees, -1.0f, 1.0f);
        const float magnitude = std::sqrt(
            normalized_x * normalized_x + normalized_y * normalized_y);
        const float scale = magnitude > 1.0f ? 1.0f / magnitude : 1.0f;
        const int dx = static_cast<int>(normalized_x * scale * travel);
        const int dy = static_cast<int>(normalized_y * scale * travel);
        lv_obj_set_pos(ball_, level_radius_ + dx - ball_radius,
                       level_radius_ + dy - ball_radius);

        const bool level = std::hypot(roll, pitch) <= kLevelThresholdDegrees;
        const uint32_t accent = level ? 0x57D9B2 : 0x66C7FF;
        lv_obj_set_style_bg_color(ball_, lv_color_hex(accent), 0);
        lv_obj_set_style_shadow_color(ball_, lv_color_hex(accent), 0);
        lv_obj_set_style_border_color(target_ring_, lv_color_hex(accent), 0);
        lv_label_set_text(status_, level ? "LEVEL" : "TILT");
        lv_obj_set_style_text_color(
            status_, lv_color_hex(level ? 0x57D9B2 : 0x66C7FF), 0);

        if (++frame_count_ % 2 == 0) {
            char text[64];
            snprintf(text, sizeof(text), "ROLL %+.1f     PITCH %+.1f",
                     (double)roll, (double)pitch);
            lv_label_set_text(info_label_, text);
        }
    }

    static void OnTimer(lv_timer_t* timer) {
        auto* self = static_cast<AttitudeView*>(lv_timer_get_user_data(timer));
        if (self != nullptr) self->Step();
    }

    static void OnZeroClicked(lv_event_t* event) {
        auto* self = static_cast<AttitudeView*>(lv_event_get_user_data(event));
        if (self != nullptr && self->initialized_) {
            self->zero_requested_ = true;
        }
    }
};

void AttitudeApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x03070C), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
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
    return false;  // The LVGL timer keeps the level responsive.
}
