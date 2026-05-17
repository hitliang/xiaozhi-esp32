#include "flappy_bird_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "settings.h"

#include <esp_log.h>
#include <cmath>
#include <cstdlib>
#include <ctime>

#define TAG "FlappyBird"

#define NVS_NS         "flappy"
#define KEY_HIGH_SCORE "high_score"
#define KEY_PLAY_COUNT "play_count"
#define KEY_PLAY_DATE  "play_date"
#define DAILY_LIMIT    10

static int GetTodayInt() {
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    return (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
}

bool FlappyBirdApp::CanEnter() const {
    Settings nvs(NVS_NS, true);
    int play_date = nvs.GetInt(KEY_PLAY_DATE, 0);
    int play_count = nvs.GetInt(KEY_PLAY_COUNT, 0);
    int today = GetTodayInt();

    if (play_date != today) return true;  // new day, reset
    return play_count < DAILY_LIMIT;
}

// Game constants
#define GRAVITY      1100.0f
#define FLAP_VEL     -340.0f
#define PIPE_SPEED    160.0f
#define PIPE_WIDTH    52
#define PIPE_GAP      135
#define BIRD_RADIUS   14
#define BIRD_X        80
#define GROUND_H      60
#define CEILING_Y     20
#define FPS           30
#define FRAME_DT      (1.0f / FPS)
#define SPAWN_DIST    280
#define PIPE_COUNT    4

class FlappyBirdView {
public:
    FlappyBirdView(lv_obj_t* parent, Display* display, LvglTheme* theme)
        : display_(display), scr_w_(display->width()), scr_h_(display->height()) {
        auto* text_font = theme->text_font()->font();
        auto fg = lv_color_white();

        srand((unsigned)esp_log_timestamp());

        ESP_LOGI(TAG, "Screen: %dx%d", scr_w_, scr_h_);

        int ground_y = scr_h_ - GROUND_H;

        // Ground
        ground_ = lv_obj_create(parent);
        lv_obj_set_size(ground_, scr_w_, GROUND_H);
        lv_obj_set_pos(ground_, 0, ground_y);
        lv_obj_set_style_bg_color(ground_, lv_color_hex(0xD4A017), 0);
        lv_obj_set_style_bg_opa(ground_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ground_, 0, 0);
        lv_obj_set_style_radius(ground_, 0, 0);
        lv_obj_clear_flag(ground_, LV_OBJ_FLAG_CLICKABLE);

        // Ground stripe
        lv_obj_t* stripe = lv_obj_create(parent);
        lv_obj_set_size(stripe, scr_w_, 6);
        lv_obj_set_pos(stripe, 0, ground_y);
        lv_obj_set_style_bg_color(stripe, lv_color_hex(0xE8C547), 0);
        lv_obj_set_style_border_width(stripe, 0, 0);
        lv_obj_set_style_radius(stripe, 0, 0);
        lv_obj_clear_flag(stripe, LV_OBJ_FLAG_CLICKABLE);

        // Bird
        bird_ = lv_obj_create(parent);
        lv_obj_set_size(bird_, BIRD_RADIUS * 2, BIRD_RADIUS * 2);
        lv_obj_set_style_bg_color(bird_, lv_color_hex(0xFFD600), 0);
        lv_obj_set_style_radius(bird_, BIRD_RADIUS, 0);
        lv_obj_set_style_border_width(bird_, 2, 0);
        lv_obj_set_style_border_color(bird_, lv_color_hex(0xFF8F00), 0);
        lv_obj_clear_flag(bird_, LV_OBJ_FLAG_CLICKABLE);

        // Bird eye
        bird_eye_ = lv_obj_create(parent);
        lv_obj_set_size(bird_eye_, 7, 7);
        lv_obj_set_style_bg_color(bird_eye_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_radius(bird_eye_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(bird_eye_, 0, 0);
        lv_obj_clear_flag(bird_eye_, LV_OBJ_FLAG_CLICKABLE);

        // Score label
        score_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(score_label_, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(score_label_, fg, 0);
        lv_obj_set_style_text_align(score_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(score_label_, "0");
        lv_obj_align(score_label_, LV_ALIGN_TOP_MID, 0, 8);

        // Message label
        msg_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(msg_label_, text_font, 0);
        lv_obj_set_style_text_color(msg_label_, fg, 0);
        lv_obj_set_style_text_align(msg_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(msg_label_, "Tap to Start");
        lv_obj_align(msg_label_, LV_ALIGN_CENTER, 0, -80);

        // Pipe pool
        for (int i = 0; i < PIPE_COUNT; i++) {
            pipes_top_[i] = lv_obj_create(parent);
            lv_obj_set_size(pipes_top_[i], PIPE_WIDTH, 0);
            lv_obj_set_style_bg_color(pipes_top_[i], lv_color_hex(0x4CAF50), 0);
            lv_obj_set_style_border_width(pipes_top_[i], 0, 0);
            lv_obj_set_style_radius(pipes_top_[i], 4, 0);
            lv_obj_clear_flag(pipes_top_[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(pipes_top_[i], LV_OBJ_FLAG_HIDDEN);

            pipes_bot_[i] = lv_obj_create(parent);
            lv_obj_set_size(pipes_bot_[i], PIPE_WIDTH, 0);
            lv_obj_set_style_bg_color(pipes_bot_[i], lv_color_hex(0x4CAF50), 0);
            lv_obj_set_style_border_width(pipes_bot_[i], 0, 0);
            lv_obj_set_style_radius(pipes_bot_[i], 4, 0);
            lv_obj_clear_flag(pipes_bot_[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(pipes_bot_[i], LV_OBJ_FLAG_HIDDEN);

            // Pipe caps
            pipe_caps_top_[i] = lv_obj_create(parent);
            lv_obj_set_size(pipe_caps_top_[i], PIPE_WIDTH + 8, 8);
            lv_obj_set_style_bg_color(pipe_caps_top_[i], lv_color_hex(0x388E3C), 0);
            lv_obj_set_style_border_width(pipe_caps_top_[i], 0, 0);
            lv_obj_set_style_radius(pipe_caps_top_[i], 3, 0);
            lv_obj_clear_flag(pipe_caps_top_[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(pipe_caps_top_[i], LV_OBJ_FLAG_HIDDEN);

            pipe_caps_bot_[i] = lv_obj_create(parent);
            lv_obj_set_size(pipe_caps_bot_[i], PIPE_WIDTH + 8, 8);
            lv_obj_set_style_bg_color(pipe_caps_bot_[i], lv_color_hex(0x388E3C), 0);
            lv_obj_set_style_border_width(pipe_caps_bot_[i], 0, 0);
            lv_obj_set_style_radius(pipe_caps_bot_[i], 3, 0);
            lv_obj_clear_flag(pipe_caps_bot_[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(pipe_caps_bot_[i], LV_OBJ_FLAG_HIDDEN);

            pipe_gap_y_[i] = 0;
            pipe_active_[i] = false;
        }

        // Touch: use PRESSED for instant response
        lv_obj_add_event_cb(parent, onTap, LV_EVENT_PRESSED, this);

        // Game loop at 30fps
        timer_ = lv_timer_create(onTimer, (uint32_t)(FRAME_DT * 1000), this);

        // Load persisted high score
        {
            Settings nvs(NVS_NS, true);
            high_score_ = nvs.GetInt(KEY_HIGH_SCORE, 0);
        }

        ResetGame();
    }

    ~FlappyBirdView() {
        if (timer_) { lv_timer_del(timer_); timer_ = nullptr; }
    }

private:
    Display* display_;
    int scr_w_, scr_h_;

    lv_obj_t* bird_ = nullptr;
    lv_obj_t* bird_eye_ = nullptr;
    lv_obj_t* ground_ = nullptr;
    lv_obj_t* score_label_ = nullptr;
    lv_obj_t* msg_label_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    lv_obj_t* pipes_top_[PIPE_COUNT] = {};
    lv_obj_t* pipes_bot_[PIPE_COUNT] = {};
    lv_obj_t* pipe_caps_top_[PIPE_COUNT] = {};
    lv_obj_t* pipe_caps_bot_[PIPE_COUNT] = {};
    float pipe_x_[PIPE_COUNT] = {};
    int pipe_gap_y_[PIPE_COUNT] = {};
    bool pipe_active_[PIPE_COUNT] = {};
    bool pipe_scored_[PIPE_COUNT] = {};

    float bird_y_ = 0;
    float bird_vy_ = 0;

    enum State { IDLE, PLAYING, DEAD };
    State state_ = IDLE;
    int score_ = 0;
    int high_score_ = 0;
    float spawn_counter_ = 0;

    int GroundY() const { return scr_h_ - GROUND_H; }

    void ResetGame() {
        bird_y_ = (float)scr_h_ / 2;
        bird_vy_ = 0;
        score_ = 0;
        spawn_counter_ = 0;
        state_ = IDLE;

        for (int i = 0; i < PIPE_COUNT; i++) {
            pipe_active_[i] = false;
            pipe_scored_[i] = false;
            pipe_x_[i] = 0;
            lv_obj_add_flag(pipes_top_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pipes_bot_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pipe_caps_top_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pipe_caps_bot_[i], LV_OBJ_FLAG_HIDDEN);
        }

        UpdateBirdVis();
        lv_label_set_text(score_label_, "0");
        if (high_score_ > 0) {
            lv_label_set_text_fmt(msg_label_, "Tap to Start\nBest: %d", high_score_);
        } else {
            lv_label_set_text(msg_label_, "Tap to Start");
        }
        lv_obj_clear_flag(msg_label_, LV_OBJ_FLAG_HIDDEN);
    }

    void StartGame() {
        bird_y_ = (float)scr_h_ / 3;
        bird_vy_ = FLAP_VEL;
        score_ = 0;
        spawn_counter_ = SPAWN_DIST; // Spawn first pipe immediately
        state_ = PLAYING;
        lv_obj_add_flag(msg_label_, LV_OBJ_FLAG_HIDDEN);
        Flap();

        // Increment daily play count
        {
            Settings nvs(NVS_NS, true);
            int today = GetTodayInt();
            int date = nvs.GetInt(KEY_PLAY_DATE, 0);
            int count = (date == today) ? nvs.GetInt(KEY_PLAY_COUNT, 0) : 0;
            nvs.SetInt(KEY_PLAY_DATE, today);
            nvs.SetInt(KEY_PLAY_COUNT, count + 1);
        }

        ESP_LOGI(TAG, "Game started");
    }

    void GameOver() {
        state_ = DEAD;

        // Check high score
        if (score_ > high_score_) {
            high_score_ = score_;
            Settings nvs(NVS_NS, true);
            nvs.SetInt(KEY_HIGH_SCORE, high_score_);
            lv_label_set_text_fmt(msg_label_, "New High Score!\n%d\nTap to Retry", score_);
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
            ESP_LOGI(TAG, "New high score: %d", score_);
        } else {
            lv_label_set_text_fmt(msg_label_, "Game Over\nScore: %d  Best: %d\nTap to Retry", score_, high_score_);
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_VIBRATION);
        }
        lv_obj_clear_flag(msg_label_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Game over, score=%d, high=%d", score_, high_score_);
    }

    void Flap() {
        bird_vy_ = FLAP_VEL;
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_PIANO_5_C);
    }

    void UpdateBirdVis() {
        int by = (int)bird_y_;
        lv_obj_set_pos(bird_, BIRD_X - BIRD_RADIUS, by - BIRD_RADIUS);
        float tilt = bird_vy_ < -200 ? -2.0f : (bird_vy_ > 200 ? 4.0f : 0.0f);
        lv_obj_set_pos(bird_eye_, BIRD_X + 2, by - 4 + (int)tilt);
    }

    void SpawnPipe() {
        for (int i = 0; i < PIPE_COUNT; i++) {
            if (!pipe_active_[i]) {
                pipe_x_[i] = (float)scr_w_;
                pipe_gap_y_[i] = 80 + rand() % (GroundY() - PIPE_GAP - 80);
                pipe_active_[i] = true;
                pipe_scored_[i] = false;
                ShowPipe(i);
                ESP_LOGI(TAG, "Pipe spawned at x=%d gap_y=%d", (int)pipe_x_[i], pipe_gap_y_[i]);
                return;
            }
        }
    }

    void ShowPipe(int i) {
        int gap_top = pipe_gap_y_[i] - PIPE_GAP / 2;
        int gap_bot = pipe_gap_y_[i] + PIPE_GAP / 2;
        int top_h = gap_top;
        int bot_h = scr_h_ - gap_bot;

        lv_obj_set_pos(pipes_top_[i], (int)pipe_x_[i], 0);
        lv_obj_set_size(pipes_top_[i], PIPE_WIDTH, top_h);
        lv_obj_set_pos(pipe_caps_top_[i], (int)pipe_x_[i] - 4, top_h - 8);

        lv_obj_set_pos(pipes_bot_[i], (int)pipe_x_[i], gap_bot);
        lv_obj_set_size(pipes_bot_[i], PIPE_WIDTH, bot_h);
        lv_obj_set_pos(pipe_caps_bot_[i], (int)pipe_x_[i] - 4, gap_bot);

        lv_obj_clear_flag(pipes_top_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pipes_bot_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pipe_caps_top_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pipe_caps_bot_[i], LV_OBJ_FLAG_HIDDEN);
    }

    void UpdatePipes() {
        for (int i = 0; i < PIPE_COUNT; i++) {
            if (!pipe_active_[i]) continue;

            pipe_x_[i] -= PIPE_SPEED * FRAME_DT;
            int px = (int)pipe_x_[i];
            int gap_top = pipe_gap_y_[i] - PIPE_GAP / 2;
            int gap_bot = pipe_gap_y_[i] + PIPE_GAP / 2;

            lv_obj_set_pos(pipes_top_[i], px, 0);
            lv_obj_set_pos(pipe_caps_top_[i], px - 4, gap_top - 8);

            lv_obj_set_pos(pipes_bot_[i], px, gap_bot);
            lv_obj_set_pos(pipe_caps_bot_[i], px - 4, gap_bot);

            if (!pipe_scored_[i] && px + PIPE_WIDTH < BIRD_X) {
                pipe_scored_[i] = true;
                score_++;
                lv_label_set_text_fmt(score_label_, "%d", score_);
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
            }

            if (px < -PIPE_WIDTH - 20) {
                pipe_active_[i] = false;
                lv_obj_add_flag(pipes_top_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pipes_bot_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pipe_caps_top_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(pipe_caps_bot_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    bool CheckCollision() {
        int bx = BIRD_X;
        int by = (int)bird_y_;
        int br = BIRD_RADIUS - 3;

        if (by + br >= GroundY() || by - br <= CEILING_Y)
            return true;

        for (int i = 0; i < PIPE_COUNT; i++) {
            if (!pipe_active_[i]) continue;

            int px = (int)pipe_x_[i];
            int gap_top = pipe_gap_y_[i] - PIPE_GAP / 2;
            int gap_bot = pipe_gap_y_[i] + PIPE_GAP / 2;

            if (bx + br < px || bx - br > px + PIPE_WIDTH)
                continue;

            if (by - br < gap_top || by + br > gap_bot)
                return true;
        }
        return false;
    }

    void Step() {
        if (state_ == IDLE) {
            bird_y_ = (float)scr_h_ / 3 + sinf((float)lv_tick_get() / 400.0f) * 8.0f;
            UpdateBirdVis();
            return;
        }

        if (state_ == DEAD) {
            bird_vy_ += GRAVITY * FRAME_DT;
            bird_y_ += bird_vy_ * FRAME_DT;
            if (bird_y_ + BIRD_RADIUS >= GroundY()) {
                bird_y_ = (float)(GroundY() - BIRD_RADIUS);
                bird_vy_ = 0;
            }
            UpdateBirdVis();
            return;
        }

        // === PLAYING ===
        bird_vy_ += GRAVITY * FRAME_DT;
        bird_y_ += bird_vy_ * FRAME_DT;
        if (bird_vy_ > 600.0f) bird_vy_ = 600.0f;
        if (bird_vy_ < -500.0f) bird_vy_ = -500.0f;

        spawn_counter_ += PIPE_SPEED * FRAME_DT;
        if (spawn_counter_ >= SPAWN_DIST) {
            spawn_counter_ -= SPAWN_DIST;
            SpawnPipe();
        }

        UpdatePipes();

        if (CheckCollision()) {
            GameOver();
        }

        UpdateBirdVis();
    }

    static void onTimer(lv_timer_t* timer) {
        auto* self = static_cast<FlappyBirdView*>(lv_timer_get_user_data(timer));
        self->Step();
    }

    static void onTap(lv_event_t* e) {
        auto* self = static_cast<FlappyBirdView*>(lv_event_get_user_data(e));
        switch (self->state_) {
            case IDLE:
                self->StartGame();
                break;
            case PLAYING:
                self->Flap();
                break;
            case DEAD:
                self->ResetGame();
                break;
        }
    }
};

void FlappyBirdApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x4EC0CA), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    view_ = new FlappyBirdView(screen, ctx.display, theme);
    ESP_LOGI(TAG, "Flappy Bird entered");
}

void FlappyBirdApp::OnExit() {
    delete view_;
    view_ = nullptr;
    ESP_LOGI(TAG, "Flappy Bird exited");
}
