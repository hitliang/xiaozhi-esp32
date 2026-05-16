#include "ball_physics_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <cmath>
#include <cstdlib>
#include <lvgl.h>

#define TAG "BallPhysics"

#define NUM_BALLS   7
#define G_SCALE     2000.0f
#define DAMPING     0.990f
#define WALL_BOUNCE 0.6f

static const uint32_t colors[] = {
    0xff4444, 0x44cc44, 0x4488ff, 0xffaa00, 0xcc44cc, 0x00cccc, 0xff8888
};
static const float radii[] = { 18, 22, 16, 24, 14, 20, 16 };

class BallPhysicsView {
public:
    BallPhysicsView(lv_obj_t* parent, LvglTheme* theme, Display* display) {
        scr_w_ = lv_obj_get_width(parent);
        scr_h_ = lv_obj_get_height(parent);

        srand(42);
        for (int i = 0; i < NUM_BALLS; i++) {
            r_[i] = radii[i];
            x_[i] = r_[i] + (scr_w_ - 2 * r_[i]) * (float)rand() / RAND_MAX;
            y_[i] = r_[i] + (scr_h_ - 2 * r_[i]) * (float)rand() / RAND_MAX;
            vx_[i] = 0;
            vy_[i] = 0;

            balls_[i] = lv_obj_create(parent);
            lv_obj_set_size(balls_[i], (int)(r_[i] * 2), (int)(r_[i] * 2));
            lv_obj_set_pos(balls_[i], (int)(x_[i] - r_[i]), (int)(y_[i] - r_[i]));
            lv_obj_set_style_bg_color(balls_[i], lv_color_hex(colors[i]), 0);
            lv_obj_set_style_radius(balls_[i], (int)r_[i], 0);
            lv_obj_set_style_border_width(balls_[i], 0, 0);
            lv_obj_set_style_shadow_color(balls_[i], lv_color_hex(colors[i]), 0);
            lv_obj_set_style_shadow_width(balls_[i], 8, 0);
            lv_obj_clear_flag(balls_[i], LV_OBJ_FLAG_CLICKABLE);
        }

        // Drive physics at ~62fps (16ms) via LVGL timer
        timer_ = lv_timer_create([](lv_timer_t* t) {
            auto* self = (BallPhysicsView*)lv_timer_get_user_data(t);
            self->Step();
        }, 16, this);

        ESP_LOGI(TAG, "Ball physics view created (%dx%d)", scr_w_, scr_h_);
    }

    ~BallPhysicsView() {
        if (timer_) { lv_timer_del(timer_); timer_ = nullptr; }
    }

    void Step() {
        auto imu = Board::GetInstance().GetImuData();

        float ax = imu.accel_x / 9.80665f;
        float ay = imu.accel_y / 9.80665f;
        float gx = -ay * G_SCALE;
        float gy =  ax * G_SCALE;
        float dt = 0.016f;

        for (int i = 0; i < NUM_BALLS; i++) {
            vx_[i] += gx * dt;
            vy_[i] += gy * dt;
            vx_[i] *= DAMPING;
            vy_[i] *= DAMPING;
            x_[i] += vx_[i] * dt;
            y_[i] += vy_[i] * dt;

            float margin = r_[i];
            if (x_[i] < margin)      { x_[i] = margin;      vx_[i] = -vx_[i] * WALL_BOUNCE; }
            if (x_[i] > scr_w_ - margin) { x_[i] = scr_w_ - margin; vx_[i] = -vx_[i] * WALL_BOUNCE; }
            if (y_[i] < margin)      { y_[i] = margin;      vy_[i] = -vy_[i] * WALL_BOUNCE; }
            if (y_[i] > scr_h_ - margin) { y_[i] = scr_h_ - margin; vy_[i] = -vy_[i] * WALL_BOUNCE; }

            float max_v = 600.0f;
            if (vx_[i] > max_v) vx_[i] = max_v;
            if (vx_[i] < -max_v) vx_[i] = -max_v;
            if (vy_[i] > max_v) vy_[i] = max_v;
            if (vy_[i] < -max_v) vy_[i] = -max_v;
        }

        // Ball-ball collisions
        for (int i = 0; i < NUM_BALLS; i++) {
            for (int j = i + 1; j < NUM_BALLS; j++) {
                float dx = x_[j] - x_[i];
                float dy = y_[j] - y_[i];
                float dist = sqrtf(dx * dx + dy * dy);
                float min_dist = r_[i] + r_[j];
                if (dist < min_dist && dist > 0.001f) {
                    float overlap = min_dist - dist;
                    float nx = dx / dist;
                    float ny = dy / dist;
                    x_[i] -= nx * overlap * 0.5f;
                    y_[i] -= ny * overlap * 0.5f;
                    x_[j] += nx * overlap * 0.5f;
                    y_[j] += ny * overlap * 0.5f;

                    float dvx = vx_[i] - vx_[j];
                    float dvy = vy_[i] - vy_[j];
                    float dvn = dvx * nx + dvy * ny;
                    if (dvn > 0) {
                        float impulse = dvn * 0.8f;
                        vx_[i] -= impulse * nx;
                        vy_[i] -= impulse * ny;
                        vx_[j] += impulse * nx;
                        vy_[j] += impulse * ny;
                    }
                }
            }
        }

        // Update positions
        for (int i = 0; i < NUM_BALLS; i++) {
            lv_obj_set_pos(balls_[i], (int)(x_[i] - r_[i]), (int)(y_[i] - r_[i]));
        }
    }

private:
    lv_obj_t* balls_[NUM_BALLS] = {};
    lv_timer_t* timer_ = nullptr;
    int scr_w_, scr_h_;
    float x_[NUM_BALLS], y_[NUM_BALLS];
    float vx_[NUM_BALLS], vy_[NUM_BALLS];
    float r_[NUM_BALLS];
};

void BallPhysicsApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    view_ = new BallPhysicsView(screen, theme, ctx.display);
    ESP_LOGI(TAG, "Ball physics entered");
}

void BallPhysicsApp::OnExit() {
    delete view_;
    view_ = nullptr;
    ESP_LOGI(TAG, "Ball physics exited");
}

bool BallPhysicsApp::OnUpdate() {
    return false;  // LVGL timer drives physics, no polling needed
}
