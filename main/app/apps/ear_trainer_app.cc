#include "ear_trainer_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "application.h"

#include <algorithm>
#include <array>
#include <esp_log.h>
#include <esp_random.h>
#include <string>

#define TAG "EarTrainer"

namespace {

constexpr int kSemitones = 12;
constexpr int kTotalNotes = 36;
constexpr int kFirstOctave = 3;
constexpr int kPlaybackGapMs = 950;

const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B",
};

#define DECL_NOTE(oct, note) \
    extern "C" const char _binary_piano_##oct##_##note##_ogg_start[] \
        asm("_binary_piano_" #oct "_" #note "_ogg_start"); \
    extern "C" const char _binary_piano_##oct##_##note##_ogg_end[] \
        asm("_binary_piano_" #oct "_" #note "_ogg_end");

DECL_NOTE(3, c)  DECL_NOTE(3, cs) DECL_NOTE(3, d)  DECL_NOTE(3, ds)
DECL_NOTE(3, e)  DECL_NOTE(3, f)  DECL_NOTE(3, fs) DECL_NOTE(3, g)
DECL_NOTE(3, gs) DECL_NOTE(3, a)  DECL_NOTE(3, as) DECL_NOTE(3, b)
DECL_NOTE(4, c)  DECL_NOTE(4, cs) DECL_NOTE(4, d)  DECL_NOTE(4, ds)
DECL_NOTE(4, e)  DECL_NOTE(4, f)  DECL_NOTE(4, fs) DECL_NOTE(4, g)
DECL_NOTE(4, gs) DECL_NOTE(4, a)  DECL_NOTE(4, as) DECL_NOTE(4, b)
DECL_NOTE(5, c)  DECL_NOTE(5, cs) DECL_NOTE(5, d)  DECL_NOTE(5, ds)
DECL_NOTE(5, e)  DECL_NOTE(5, f)  DECL_NOTE(5, fs) DECL_NOTE(5, g)
DECL_NOTE(5, gs) DECL_NOTE(5, a)  DECL_NOTE(5, as) DECL_NOTE(5, b)

#undef DECL_NOTE

struct PianoSound {
    const char* start;
    const char* end;
};

#define NOTE_ENTRY(oct, note) \
    {_binary_piano_##oct##_##note##_ogg_start, \
     _binary_piano_##oct##_##note##_ogg_end}

const PianoSound kPianoSounds[kTotalNotes] = {
    NOTE_ENTRY(3, c),  NOTE_ENTRY(3, cs), NOTE_ENTRY(3, d),  NOTE_ENTRY(3, ds),
    NOTE_ENTRY(3, e),  NOTE_ENTRY(3, f),  NOTE_ENTRY(3, fs), NOTE_ENTRY(3, g),
    NOTE_ENTRY(3, gs), NOTE_ENTRY(3, a),  NOTE_ENTRY(3, as), NOTE_ENTRY(3, b),
    NOTE_ENTRY(4, c),  NOTE_ENTRY(4, cs), NOTE_ENTRY(4, d),  NOTE_ENTRY(4, ds),
    NOTE_ENTRY(4, e),  NOTE_ENTRY(4, f),  NOTE_ENTRY(4, fs), NOTE_ENTRY(4, g),
    NOTE_ENTRY(4, gs), NOTE_ENTRY(4, a),  NOTE_ENTRY(4, as), NOTE_ENTRY(4, b),
    NOTE_ENTRY(5, c),  NOTE_ENTRY(5, cs), NOTE_ENTRY(5, d),  NOTE_ENTRY(5, ds),
    NOTE_ENTRY(5, e),  NOTE_ENTRY(5, f),  NOTE_ENTRY(5, fs), NOTE_ENTRY(5, g),
    NOTE_ENTRY(5, gs), NOTE_ENTRY(5, a),  NOTE_ENTRY(5, as), NOTE_ENTRY(5, b),
};

#undef NOTE_ENTRY

std::string_view GetNoteSound(int index) {
    index = std::clamp(index, 0, kTotalNotes - 1);
    const auto& sound = kPianoSounds[index];
    return std::string_view(sound.start, sound.end - sound.start);
}

std::string GetNoteName(int index) {
    index = std::clamp(index, 0, kTotalNotes - 1);
    const int octave = index / kSemitones + kFirstOctave;
    char text[10];
    snprintf(text, sizeof(text), "%s%d", kNoteNames[index % kSemitones], octave);
    return text;
}

lv_obj_t* CreatePanel(lv_obj_t* parent, int x, int y, int width, int height,
                      uint32_t color, int radius) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

const char* DirectionName(int direction) {
    if (direction < 0) return "lower";
    if (direction > 0) return "higher";
    return "the same";
}

}  // namespace

LV_FONT_DECLARE(lv_font_montserrat_14);

class EarTrainerView {
public:
    EarTrainerView(lv_obj_t* parent, LvglTheme* theme) {
        const int width = lv_obj_get_width(parent);
        auto* text_font = theme->text_font()->font();

        auto* glow = CreatePanel(parent, -82, -108, 230, 230,
                                 0x3B276D, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);
        lv_obj_clear_flag(glow, LV_OBJ_FLAG_CLICKABLE);

        auto* title = lv_label_create(parent);
        lv_label_set_text(title, "PITCH DIRECTION");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xA98BFF), 0);
        lv_obj_set_style_text_letter_space(title, 2, 0);
        lv_obj_set_pos(title, 18, 18);

        level_label_ = lv_label_create(parent);
        lv_label_set_text(level_label_, "LEVEL 1");
        lv_obj_set_style_text_font(level_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(level_label_, lv_color_hex(0x7E91AA), 0);
        lv_obj_set_pos(level_label_, width - 91, 18);

        auto* question_panel = CreatePanel(parent, 18, 54, width - 36, 116,
                                           0x10192A, 24);
        lv_obj_set_style_bg_grad_color(question_panel, lv_color_hex(0x191332), 0);
        lv_obj_set_style_bg_grad_dir(question_panel, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(question_panel, 1, 0);
        lv_obj_set_style_border_color(question_panel, lv_color_hex(0x30254E), 0);

        question_label_ = lv_label_create(question_panel);
        lv_label_set_text(question_label_, "Listen to two notes");
        lv_obj_set_style_text_font(question_label_, text_font, 0);
        lv_obj_set_style_text_color(question_label_, lv_color_hex(0xF2F5FA), 0);
        lv_obj_set_pos(question_label_, 12, 18);
        lv_obj_set_size(question_label_, width - 60, 28);
        lv_obj_set_style_text_align(question_label_, LV_TEXT_ALIGN_CENTER, 0);

        result_label_ = lv_label_create(question_panel);
        lv_label_set_text(result_label_, "Does note 2 go lower, stay same, or go higher?");
        lv_obj_set_style_text_font(result_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(result_label_, lv_color_hex(0x91A1B7), 0);
        lv_obj_set_pos(result_label_, 16, 58);
        lv_obj_set_size(result_label_, width - 68, 40);
        lv_label_set_long_mode(result_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(result_label_, LV_TEXT_ALIGN_CENTER, 0);

        play_button_ = lv_button_create(parent);
        lv_obj_set_pos(play_button_, 74, 186);
        lv_obj_set_size(play_button_, width - 148, 60);
        lv_obj_set_style_radius(play_button_, 20, 0);
        lv_obj_set_style_bg_color(play_button_, lv_color_hex(0x563AA0), 0);
        lv_obj_set_style_bg_color(play_button_, lv_color_hex(0x704FC1), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(play_button_, lv_color_hex(0x29233A), LV_STATE_DISABLED);
        play_button_label_ = lv_label_create(play_button_);
        lv_label_set_text(play_button_label_, "PLAY QUESTION");
        lv_obj_set_style_text_font(play_button_label_, text_font, 0);
        lv_obj_set_style_text_color(play_button_label_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(play_button_label_);
        lv_obj_add_event_cb(play_button_, OnPlayClicked, LV_EVENT_CLICKED, this);

        auto* answer_row = lv_obj_create(parent);
        lv_obj_set_pos(answer_row, 12, 266);
        lv_obj_set_size(answer_row, width - 24, 74);
        lv_obj_set_style_bg_opa(answer_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(answer_row, 0, 0);
        lv_obj_set_style_pad_all(answer_row, 0, 0);
        lv_obj_set_style_pad_column(answer_row, 8, 0);
        lv_obj_set_flex_flow(answer_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(answer_row, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(answer_row, LV_OBJ_FLAG_SCROLLABLE);

        const char* labels[] = {"LOWER", "SAME", "HIGHER"};
        const int directions[] = {-1, 0, 1};
        for (int index = 0; index < 3; ++index) {
            auto* button = lv_button_create(answer_row);
            lv_obj_set_size(button, 106, 62);
            lv_obj_set_style_radius(button, 16, 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x172434), 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x223850), LV_STATE_PRESSED);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x101720), LV_STATE_DISABLED);
            lv_obj_set_style_border_width(button, 1, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(0x2C4158), 0);
            lv_obj_set_user_data(button, reinterpret_cast<void*>(
                static_cast<intptr_t>(directions[index])));
            auto* label = lv_label_create(button);
            lv_label_set_text(label, labels[index]);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0xD5E0EC), 0);
            lv_obj_center(label);
            lv_obj_add_event_cb(button, OnAnswerClicked, LV_EVENT_CLICKED, this);
            answer_buttons_[index] = button;
        }

        score_panel_ = CreatePanel(parent, 24, 360, width - 48, 46, 0x0C1520, 14);
        lv_obj_set_style_border_width(score_panel_, 1, 0);
        lv_obj_set_style_border_color(score_panel_, lv_color_hex(0x1D3044), 0);
        score_label_ = lv_label_create(score_panel_);
        lv_label_set_text(score_label_, "SCORE 0/0     STREAK 0");
        lv_obj_set_style_text_font(score_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(score_label_, lv_color_hex(0x91A5BB), 0);
        lv_obj_center(score_label_);

        hint_label_ = lv_label_create(parent);
        lv_label_set_text(hint_label_, "Three correct answers unlock a harder level");
        lv_obj_set_style_text_font(hint_label_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x5F738B), 0);
        lv_obj_set_pos(hint_label_, 18, 416);
        lv_obj_set_size(hint_label_, width - 36, 20);
        lv_obj_set_style_text_align(hint_label_, LV_TEXT_ALIGN_CENTER, 0);

        playback_timer_ = lv_timer_create(OnPlaybackTimer, kPlaybackGapMs, this);
        lv_timer_pause(playback_timer_);
        SetAnswerButtonsEnabled(false);
        ESP_LOGI(TAG, "Adaptive pitch-direction trainer created");
    }

    ~EarTrainerView() {
        if (playback_timer_ != nullptr) {
            lv_timer_delete(playback_timer_);
            playback_timer_ = nullptr;
        }
    }

private:
    lv_obj_t* level_label_ = nullptr;
    lv_obj_t* question_label_ = nullptr;
    lv_obj_t* result_label_ = nullptr;
    lv_obj_t* play_button_ = nullptr;
    lv_obj_t* play_button_label_ = nullptr;
    std::array<lv_obj_t*, 3> answer_buttons_{};
    lv_obj_t* score_panel_ = nullptr;
    lv_obj_t* score_label_ = nullptr;
    lv_obj_t* hint_label_ = nullptr;
    lv_timer_t* playback_timer_ = nullptr;

    int first_note_ = -1;
    int second_note_ = -1;
    int correct_direction_ = 0;
    int previous_first_note_ = -1;
    int previous_second_note_ = -1;
    int playback_index_ = 0;
    int score_ = 0;
    int attempts_ = 0;
    int streak_ = 0;
    int wrong_streak_ = 0;
    int level_ = 1;
    bool has_question_ = false;
    bool playing_ = false;
    bool answered_ = false;

    void GenerateQuestion() {
        static constexpr int kMinInterval[] = {7, 3, 1};
        static constexpr int kMaxInterval[] = {12, 6, 2};

        for (int attempt = 0; attempt < 12; ++attempt) {
            const uint32_t random = esp_random();
            const bool same = random % 5 == 0;
            correct_direction_ = same ? 0 : ((random & 1U) ? 1 : -1);

            int interval = 0;
            if (!same) {
                const int min_interval = kMinInterval[level_ - 1];
                const int max_interval = kMaxInterval[level_ - 1];
                interval = min_interval +
                    static_cast<int>((random >> 8) %
                    static_cast<uint32_t>(max_interval - min_interval + 1));
            }

            if (correct_direction_ > 0) {
                first_note_ = static_cast<int>((random >> 16) %
                    static_cast<uint32_t>(kTotalNotes - interval));
                second_note_ = first_note_ + interval;
            } else if (correct_direction_ < 0) {
                second_note_ = static_cast<int>((random >> 16) %
                    static_cast<uint32_t>(kTotalNotes - interval));
                first_note_ = second_note_ + interval;
            } else {
                first_note_ = static_cast<int>((random >> 16) % kTotalNotes);
                second_note_ = first_note_;
            }

            if (first_note_ != previous_first_note_ ||
                second_note_ != previous_second_note_) {
                break;
            }
        }

        previous_first_note_ = first_note_;
        previous_second_note_ = second_note_;
        has_question_ = true;
        answered_ = false;
        ResetAnswerButtonStyles();
        lv_label_set_text(question_label_, "Listen to two notes");
        lv_label_set_text(
            result_label_,
            "Does note 2 go lower, stay same, or go higher?");
    }

    void StartPlayback(bool new_question) {
        if (playing_) return;
        if (new_question || !has_question_) GenerateQuestion();

        playback_index_ = 0;
        playing_ = true;
        SetAnswerButtonsEnabled(false);
        lv_obj_add_state(play_button_, LV_STATE_DISABLED);
        lv_label_set_text(play_button_label_, "LISTEN...");
        lv_label_set_text(question_label_, "Note 1");
        lv_label_set_text(result_label_, "Listen carefully...");
        lv_timer_resume(playback_timer_);
        PlayNext();
    }

    void PlayNext() {
        if (!playing_) return;

        if (playback_index_ == 0) {
            Application::GetInstance().PlaySound(GetNoteSound(first_note_));
            playback_index_ = 1;
            return;
        }
        if (playback_index_ == 1) {
            lv_label_set_text(question_label_, "Note 2");
            Application::GetInstance().PlaySound(GetNoteSound(second_note_));
            playback_index_ = 2;
            return;
        }

        playing_ = false;
        lv_timer_pause(playback_timer_);
        lv_obj_remove_state(play_button_, LV_STATE_DISABLED);
        lv_label_set_text(play_button_label_, "REPLAY");
        lv_label_set_text(question_label_, "Your answer?");
        lv_label_set_text(result_label_, "Choose LOWER, SAME, or HIGHER");
        if (!answered_) SetAnswerButtonsEnabled(true);
    }

    void SubmitAnswer(int selected_direction, lv_obj_t* selected_button) {
        if (playing_ || answered_ || !has_question_) return;
        answered_ = true;
        attempts_++;
        SetAnswerButtonsEnabled(false);

        const bool correct = selected_direction == correct_direction_;
        if (correct) {
            score_++;
            streak_++;
            wrong_streak_ = 0;
            lv_obj_set_style_bg_color(selected_button, lv_color_hex(0x1F765E), 0);
            lv_obj_set_style_border_color(selected_button, lv_color_hex(0x57D9B2), 0);
        } else {
            streak_ = 0;
            wrong_streak_++;
            lv_obj_set_style_bg_color(selected_button, lv_color_hex(0x7A2F3A), 0);
            lv_obj_set_style_border_color(selected_button, lv_color_hex(0xFF7E8D), 0);
            const int correct_index = correct_direction_ + 1;
            lv_obj_set_style_bg_color(
                answer_buttons_[correct_index], lv_color_hex(0x1F765E), 0);
            lv_obj_set_style_border_color(
                answer_buttons_[correct_index], lv_color_hex(0x57D9B2), 0);
        }

        const int old_level = level_;
        if (streak_ >= 3 && level_ < 3) {
            level_++;
            streak_ = 0;
        } else if (wrong_streak_ >= 2 && level_ > 1) {
            level_--;
            wrong_streak_ = 0;
        }

        char question[64];
        snprintf(question, sizeof(question), "%s  ->  %s",
                 GetNoteName(first_note_).c_str(),
                 GetNoteName(second_note_).c_str());
        lv_label_set_text(question_label_, question);

        char result[96];
        if (correct) {
            snprintf(result, sizeof(result), "Correct! Note 2 went %s.",
                     DirectionName(correct_direction_));
        } else {
            snprintf(result, sizeof(result), "Good try. Note 2 went %s.",
                     DirectionName(correct_direction_));
        }
        lv_label_set_text(result_label_, result);
        lv_obj_set_style_text_color(
            result_label_, lv_color_hex(correct ? 0x57D9B2 : 0xFF9A82), 0);
        lv_label_set_text(play_button_label_, "NEXT QUESTION");

        char score[64];
        snprintf(score, sizeof(score), "SCORE %d/%d     STREAK %d",
                 score_, attempts_, streak_);
        lv_label_set_text(score_label_, score);

        char level[24];
        snprintf(level, sizeof(level), "LEVEL %d", level_);
        lv_label_set_text(level_label_, level);
        if (level_ > old_level) {
            lv_label_set_text(hint_label_, "Level up! The notes are closer now.");
            lv_obj_set_style_text_color(hint_label_, lv_color_hex(0xA98BFF), 0);
        } else if (level_ < old_level) {
            lv_label_set_text(hint_label_, "Wider note gaps for a little practice.");
            lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x66C7FF), 0);
        } else {
            lv_label_set_text(hint_label_, "Tap NEXT QUESTION when you are ready");
            lv_obj_set_style_text_color(hint_label_, lv_color_hex(0x5F738B), 0);
        }
    }

    void SetAnswerButtonsEnabled(bool enabled) {
        for (auto* button : answer_buttons_) {
            if (enabled) {
                lv_obj_remove_state(button, LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(button, LV_STATE_DISABLED);
            }
        }
    }

    void ResetAnswerButtonStyles() {
        for (auto* button : answer_buttons_) {
            lv_obj_set_style_bg_color(button, lv_color_hex(0x172434), 0);
            lv_obj_set_style_border_color(button, lv_color_hex(0x2C4158), 0);
        }
        lv_obj_set_style_text_color(result_label_, lv_color_hex(0x91A1B7), 0);
    }

    static void OnPlaybackTimer(lv_timer_t* timer) {
        auto* self = static_cast<EarTrainerView*>(lv_timer_get_user_data(timer));
        if (self != nullptr) self->PlayNext();
    }

    static void OnPlayClicked(lv_event_t* event) {
        auto* self = static_cast<EarTrainerView*>(lv_event_get_user_data(event));
        if (self == nullptr || self->playing_) return;
        self->StartPlayback(self->answered_ || !self->has_question_);
    }

    static void OnAnswerClicked(lv_event_t* event) {
        auto* self = static_cast<EarTrainerView*>(lv_event_get_user_data(event));
        auto* button = lv_event_get_current_target_obj(event);
        if (self == nullptr || button == nullptr) return;
        const int direction = static_cast<int>(
            reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
        self->SubmitAnswer(direction, button);
    }
};

void EarTrainerApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x03070C), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    view_ = new EarTrainerView(screen, theme);
    ESP_LOGI(TAG, "Ear trainer entered");
}

void EarTrainerApp::OnExit() {
    delete view_;
    view_ = nullptr;
    ESP_LOGI(TAG, "Ear trainer exited");
}

bool EarTrainerApp::OnUpdate() {
    return false;
}
