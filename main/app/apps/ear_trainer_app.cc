#include "ear_trainer_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "application.h"

#include <esp_log.h>
#include <esp_random.h>
#include <algorithm>
#include <vector>
#include <string>

#define TAG "EarTrainer"

// ---- 36 piano notes: 3 octaves x 12 semitones ----
static const char* NOTE_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};
static const int OCTAVES = 3;       // 3, 4, 5
static const int SEMITONES = 12;
static const int TOTAL_NOTES = 36;
static const int DEFAULT_ROUNDS = 3;

// Binary OGG data embedded by ESP-IDF build system
#define DECL_NOTE(oct, n) \
    extern "C" const char _binary_piano_##oct##_##n##_ogg_start[] asm("_binary_piano_" #oct "_" #n "_ogg_start"); \
    extern "C" const char _binary_piano_##oct##_##n##_ogg_end[]   asm("_binary_piano_" #oct "_" #n "_ogg_end");

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

// Lookup table for all 36 notes
#define NOTE_ENTRY(oct, n) { \
    _binary_piano_##oct##_##n##_ogg_start, \
    _binary_piano_##oct##_##n##_ogg_end }

struct PianoSound {
    const char* start;
    const char* end;
};

static const PianoSound PIANO_SOUNDS[36] = {
    // Octave 3
    NOTE_ENTRY(3,c), NOTE_ENTRY(3,cs), NOTE_ENTRY(3,d), NOTE_ENTRY(3,ds),
    NOTE_ENTRY(3,e), NOTE_ENTRY(3,f),  NOTE_ENTRY(3,fs), NOTE_ENTRY(3,g),
    NOTE_ENTRY(3,gs),NOTE_ENTRY(3,a),  NOTE_ENTRY(3,as), NOTE_ENTRY(3,b),
    // Octave 4
    NOTE_ENTRY(4,c), NOTE_ENTRY(4,cs), NOTE_ENTRY(4,d), NOTE_ENTRY(4,ds),
    NOTE_ENTRY(4,e), NOTE_ENTRY(4,f),  NOTE_ENTRY(4,fs), NOTE_ENTRY(4,g),
    NOTE_ENTRY(4,gs),NOTE_ENTRY(4,a),  NOTE_ENTRY(4,as), NOTE_ENTRY(4,b),
    // Octave 5
    NOTE_ENTRY(5,c), NOTE_ENTRY(5,cs), NOTE_ENTRY(5,d), NOTE_ENTRY(5,ds),
    NOTE_ENTRY(5,e), NOTE_ENTRY(5,f),  NOTE_ENTRY(5,fs), NOTE_ENTRY(5,g),
    NOTE_ENTRY(5,gs),NOTE_ENTRY(5,a),  NOTE_ENTRY(5,as), NOTE_ENTRY(5,b),
};

#undef NOTE_ENTRY

static std::string_view GetNoteSound(int idx) {
    idx = std::clamp(idx, 0, TOTAL_NOTES - 1);
    auto& s = PIANO_SOUNDS[idx];
    return std::string_view(s.start, s.end - s.start);
}

static std::string GetNoteName(int idx) {
    int octave = idx / SEMITONES + 3;
    const char* name = NOTE_NAMES[idx % SEMITONES];
    char buf[12];
    snprintf(buf, sizeof(buf), "%s%d", name, octave);
    return std::string(buf);
}

LV_FONT_DECLARE(lv_font_montserrat_48);

class EarTrainerView {
public:
    EarTrainerView(lv_obj_t* parent, LvglTheme* theme, Display* display)
        : display_(display) {
        DisplayLockGuard lock(display_);
        auto* label_font = theme->text_font()->font();
        lv_color_t color = theme->text_color();

        // Notes count: just the number, with +/- buttons on sides
        count_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(count_label_, label_font, 0);
        lv_obj_set_style_text_color(count_label_, color, 0);
        lv_label_set_text(count_label_, "3");
        lv_obj_align(count_label_, LV_ALIGN_TOP_MID, 0, 36);

        minus_btn_ = lv_button_create(parent);
        lv_obj_set_size(minus_btn_, 44, 44);
        lv_obj_align(minus_btn_, LV_ALIGN_TOP_MID, -90, 30);
        lv_obj_set_style_bg_color(minus_btn_, lv_color_hex(0x333333), 0);
        lv_obj_t* ml = lv_label_create(minus_btn_);
        lv_label_set_text(ml, "-");
        lv_obj_center(ml);
        lv_obj_add_event_cb(minus_btn_, onMinusClick, LV_EVENT_CLICKED, this);

        plus_btn_ = lv_button_create(parent);
        lv_obj_set_size(plus_btn_, 44, 44);
        lv_obj_align(plus_btn_, LV_ALIGN_TOP_MID, 90, 30);
        lv_obj_set_style_bg_color(plus_btn_, lv_color_hex(0x333333), 0);
        lv_obj_t* pl = lv_label_create(plus_btn_);
        lv_label_set_text(pl, "+");
        lv_obj_center(pl);
        lv_obj_add_event_cb(plus_btn_, onPlusClick, LV_EVENT_CLICKED, this);

        // Play button
        play_btn_ = lv_button_create(parent);
        lv_obj_set_size(play_btn_, 200, 80);
        lv_obj_align(play_btn_, LV_ALIGN_CENTER, 0, -30);
        lv_obj_set_style_bg_color(play_btn_, lv_color_hex(0x333333), 0);
        play_btn_label_ = lv_label_create(play_btn_);
        lv_obj_set_style_text_font(play_btn_label_, label_font, 0);
        lv_label_set_text(play_btn_label_, "Play");
        lv_obj_center(play_btn_label_);
        lv_obj_add_event_cb(play_btn_, onPlayClick, LV_EVENT_CLICKED, this);

        // Answer area - two lines for >3 notes
        answer_line1_ = lv_label_create(parent);
        lv_obj_set_style_text_font(answer_line1_, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(answer_line1_, color, 0);
        lv_label_set_text(answer_line1_, "");
        lv_obj_align(answer_line1_, LV_ALIGN_CENTER, 0, 40);

        answer_line2_ = lv_label_create(parent);
        lv_obj_set_style_text_font(answer_line2_, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(answer_line2_, color, 0);
        lv_label_set_text(answer_line2_, "");
        lv_obj_align(answer_line2_, LV_ALIGN_CENTER, 0, 85);

        // Bottom hint
        hint_label_ = lv_label_create(parent);
        lv_obj_set_style_text_font(hint_label_, label_font, 0);
        lv_obj_set_style_text_color(hint_label_, color, 0);
        lv_label_set_text(hint_label_, "Press Play to start");
        lv_obj_align(hint_label_, LV_ALIGN_BOTTOM_MID, 0, -25);

        play_timer_ = lv_timer_create(onPlayTimer, 750, this);
        lv_timer_pause(play_timer_);

        ESP_LOGI(TAG, "Ear trainer view created (%d notes)", TOTAL_NOTES);
    }

    void SetNoteCount(int count) {
        note_count_ = std::clamp(count, 2, 6);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", note_count_);
        DisplayLockGuard lock(display_);
        lv_label_set_text(count_label_, buf);
    }

    int GetNoteCount() const { return note_count_; }

    void Reveal() {
        if (playing_) return;
        if (revealed_) return;
        if (sequence_.empty()) return;
        revealed_ = true;

        std::string line1, line2;
        int n = (int)sequence_.size();
        int split = (n > 3) ? (n + 1) / 2 : n;  // split roughly in half if >3
        for (int i = 0; i < n; i++) {
            if (i > 0) { if (i < split) line1 += " "; else line2 += " "; }
            if (i < split)
                line1 += GetNoteName(sequence_[i]);
            else
                line2 += GetNoteName(sequence_[i]);
        }

        DisplayLockGuard lock(display_);
        lv_label_set_text(answer_line1_, line1.c_str());
        lv_label_set_text(answer_line2_, line2.c_str());
        lv_label_set_text(hint_label_, "");
        lv_label_set_text(play_btn_label_, "Play");  // revealed -> new round
    }

private:
    Display* display_;
    lv_obj_t *count_label_ = nullptr;
    lv_obj_t *minus_btn_ = nullptr, *plus_btn_ = nullptr;
    lv_obj_t *play_btn_ = nullptr, *play_btn_label_ = nullptr;
    lv_obj_t *answer_line1_ = nullptr, *answer_line2_ = nullptr, *hint_label_ = nullptr;
    lv_timer_t* play_timer_ = nullptr;
    int note_count_ = DEFAULT_ROUNDS;

    std::vector<int> sequence_;
    int play_index_ = 0;
    bool playing_ = false;
    bool played_ = false;
    bool revealed_ = false;

    void StartPlay() {
        if (played_ && !revealed_) {
            play_index_ = 0;
            playing_ = true;
        } else {
            sequence_.clear();
            for (int i = 0; i < note_count_; i++) {
                sequence_.push_back(esp_random() % TOTAL_NOTES);
            }
            play_index_ = 0;
            playing_ = true;
            played_ = false;
            revealed_ = false;
        }

        DisplayLockGuard lock(display_);
        lv_label_set_text(play_btn_label_, "Playing...");
        lv_obj_add_state(play_btn_, LV_STATE_DISABLED);
        lv_label_set_text(answer_line1_, "");
        lv_label_set_text(answer_line2_, "");
        lv_label_set_text(hint_label_, "Listen...");
        lv_timer_resume(play_timer_);
        PlayCurrentNote();
    }

    void PlayCurrentNote() {
        if (play_index_ < (int)sequence_.size()) {
            Application::GetInstance().PlaySound(GetNoteSound(sequence_[play_index_]));
            play_index_++;
        } else {
            playing_ = false;
            played_ = true;
            lv_timer_pause(play_timer_);
            DisplayLockGuard lock(display_);
            lv_label_set_text(play_btn_label_, "Replay");
            lv_obj_remove_state(play_btn_, LV_STATE_DISABLED);
            lv_label_set_text(hint_label_, "Tap screen to reveal");
        }
    }

    static void onPlayTimer(lv_timer_t* timer) {
        auto* self = static_cast<EarTrainerView*>(lv_timer_get_user_data(timer));
        self->PlayCurrentNote();
    }

    static void onPlayClick(lv_event_t* e) {
        auto* self = static_cast<EarTrainerView*>(lv_event_get_user_data(e));
        if (!self->playing_) self->StartPlay();
    }

    static void onMinusClick(lv_event_t* e) {
        auto* self = static_cast<EarTrainerView*>(lv_event_get_user_data(e));
        if (!self->playing_) self->SetNoteCount(self->GetNoteCount() - 1);
    }

    static void onPlusClick(lv_event_t* e) {
        auto* self = static_cast<EarTrainerView*>(lv_event_get_user_data(e));
        if (!self->playing_) self->SetNoteCount(self->GetNoteCount() + 1);
    }

    friend class EarTrainerApp;
};

// ---- App lifecycle ----

void EarTrainerApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    view_ = new EarTrainerView(screen, theme, ctx.display);

    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, [](lv_event_t* e) {
        auto* self = static_cast<EarTrainerApp*>(lv_event_get_user_data(e));
        if (self->view_) self->view_->Reveal();
    }, LV_EVENT_CLICKED, this);

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
