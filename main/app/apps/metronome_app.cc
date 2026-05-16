#include "metronome_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "application.h"
#include "assets/lang_config.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <algorithm>

#define TAG "MetronomeApp"

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_48);

// ======================== Helpers ========================

int MetronomeApp::timerPeriodMs() const {
    int ms = 60000 / bpm_ / steps_per_beat_;
    return std::max(ms, 20);
}

bool MetronomeApp::isMainBeat() const {
    return current_step_ % steps_per_beat_ == 0;
}

int MetronomeApp::beatNumber() const {
    return current_step_ / steps_per_beat_ + 1;
}

// ======================== Lifecycle ========================

void MetronomeApp::start() {
    if (timer_) return;
    current_step_ = 0;
    is_playing_ = true;
    timer_ = lv_timer_create(onTimerTick, timerPeriodMs(), this);
    updatePlayButton();
    doBeat();
}

void MetronomeApp::stop() {
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    is_playing_ = false;
    updatePlayButton();
    current_step_ = 0;
    for (auto* dot : beat_dots_) {
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x444444), 0);
    }
}

// ======================== Beat Logic ========================

void MetronomeApp::doBeat() {
    bool main = isMainBeat();
    playMetronomeSound(main);
    updateBeatVisual();

    current_step_++;
    if (current_step_ >= stepsPerMeasure()) {
        current_step_ = 0;
    }
}

void MetronomeApp::playMetronomeSound(bool main_beat) {
    auto& app = Application::GetInstance();

    switch (sound_type_) {
        case 0: { // Voice counting on main beats
            if (main_beat) {
                int bn = beatNumber();
                switch (bn) {
                    case 1: app.PlaySound(Lang::Sounds::OGG_1); break;
                    case 2: app.PlaySound(Lang::Sounds::OGG_2); break;
                    case 3: app.PlaySound(Lang::Sounds::OGG_3); break;
                    case 4: app.PlaySound(Lang::Sounds::OGG_4); break;
                    case 5: app.PlaySound(Lang::Sounds::OGG_5); break;
                    case 6: app.PlaySound(Lang::Sounds::OGG_6); break;
                    case 7: app.PlaySound(Lang::Sounds::OGG_7); break;
                    case 8: app.PlaySound(Lang::Sounds::OGG_8); break;
                    case 9: app.PlaySound(Lang::Sounds::OGG_9); break;
                    default: app.PlaySound(Lang::Sounds::OGG_POPUP); break;
                }
            }
            break;
        }
        case 1: { // Click — accent on main, light on subdivision
            app.PlaySound(main_beat ? Lang::Sounds::OGG_POPUP
                                    : Lang::Sounds::OGG_PIANO_5_C);
            break;
        }
        case 2: { // Piano — high note on main, low on subdivision
            app.PlaySound(main_beat ? Lang::Sounds::OGG_PIANO_5_C
                                    : Lang::Sounds::OGG_PIANO_4_C);
            break;
        }
    }
}

// ======================== Timer Callback ========================

void MetronomeApp::onTimerTick(lv_timer_t* timer) {
    auto* self = static_cast<MetronomeApp*>(lv_timer_get_user_data(timer));
    self->doBeat();
}

// ======================== State Updates ========================

void MetronomeApp::recalculateSteps() {
    switch (selected_subdivision_) {
        case 0: steps_per_beat_ = 1; break;
        case 1: steps_per_beat_ = 2; break;
        case 2: steps_per_beat_ = 3; break;
        case 3: steps_per_beat_ = 4; break;
        default: steps_per_beat_ = 1; break;
    }
}

void MetronomeApp::onTimeSigClicked(int idx) {
    bool was_playing = is_playing_;
    if (was_playing) stop();

    selected_time_sig_ = idx;
    switch (idx) {
        case 0: beats_per_measure_ = 2; break;
        case 1: beats_per_measure_ = 3; break;
        case 2: beats_per_measure_ = 4; break;
        case 3: beats_per_measure_ = 6; break;
    }
    recalculateSteps();
    current_step_ = 0;

    auto* display = Board::GetInstance().GetDisplay();
    rebuildBeatDots(display->width());
    updateTimeSigButtons();
    setTimerPeriod();

    if (was_playing) start();
}

void MetronomeApp::onSubdivisionClicked(int idx) {
    bool was_playing = is_playing_;
    if (was_playing) stop();

    selected_subdivision_ = idx;
    recalculateSteps();
    current_step_ = 0;
    updateSubButtons();
    setTimerPeriod();

    if (was_playing) start();
}

void MetronomeApp::adjustBpm(int delta) {
    bpm_ = std::clamp(bpm_ + delta, 40, 240);
    updateBpmDisplay();
    lv_slider_set_value(bpm_slider_, bpm_, LV_ANIM_OFF);
    setTimerPeriod();
}

void MetronomeApp::setTimerPeriod() {
    if (timer_) {
        // lv_timer_set_period does not reset the internal counter,
        // so the remaining old-period time would be added to the new beat.
        // Recreate the timer to guarantee exact new period.
        lv_timer_del(timer_);
        timer_ = lv_timer_create(onTimerTick, timerPeriodMs(), this);
    }
}

void MetronomeApp::updateBpmDisplay() {
    if (bpm_label_) {
        lv_label_set_text_fmt(bpm_label_, "%d", bpm_);
    }
}

void MetronomeApp::updatePlayButton() {
    if (play_label_) {
        lv_label_set_text(play_label_, is_playing_ ? "■ STOP" : "▶ PLAY");
    }
    if (play_btn_) {
        lv_obj_set_style_bg_color(play_btn_,
            is_playing_ ? lv_color_hex(0xAA3333) : lv_color_hex(0x2E7D32), 0);
    }
}

void MetronomeApp::updateTimeSigButtons() {
    for (int i = 0; i < 4; i++) {
        auto* btn = time_sig_btns_[i];
        if (!btn) continue;
        bool sel = (i == selected_time_sig_);
        lv_obj_set_style_bg_color(btn, sel ? lv_color_hex(0x2E7D32) : lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), sel ? lv_color_white() : lv_color_hex(0x888888), 0);
    }
}

void MetronomeApp::updateSubButtons() {
    for (int i = 0; i < 4; i++) {
        auto* btn = sub_btns_[i];
        if (!btn) continue;
        bool sel = (i == selected_subdivision_);
        lv_obj_set_style_bg_color(btn, sel ? lv_color_hex(0x2E7D32) : lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), sel ? lv_color_white() : lv_color_hex(0x888888), 0);
    }
}

void MetronomeApp::updateSoundButton() {
    if (!sound_btn_ || !sound_label_) return;
    const char* names[] = {"Voice", "Click", "Piano"};
    lv_label_set_text(sound_label_, names[sound_type_]);
    lv_obj_set_style_bg_color(sound_btn_,
        sound_type_ == 0 ? lv_color_hex(0x2E7D32) :
        sound_type_ == 1 ? lv_color_hex(0x1565C0) :
                           lv_color_hex(0x6A1B9A), 0);
}

// ======================== Beat Dots ========================

void MetronomeApp::rebuildBeatDots(int display_w) {
    if (!beat_container_) return;
    beat_dots_.clear();
    lv_obj_clean(beat_container_);

    int num = beats_per_measure_;
    int dot_sz = 22;
    int total_w = num * dot_sz;
    int gap = (display_w - 40 - total_w) / (num + 1);
    if (gap < 4) gap = 4;

    for (int i = 0; i < num; i++) {
        lv_obj_t* dot = lv_obj_create(beat_container_);
        lv_obj_set_size(dot, dot_sz, dot_sz);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_margin_left(dot, i == 0 ? gap : 0, 0);
        beat_dots_.push_back(dot);
    }
}

void MetronomeApp::updateBeatVisual() {
    int main_beat = current_step_ / steps_per_beat_;
    int main_idx = main_beat;
    bool main = isMainBeat();

    for (int i = 0; i < (int)beat_dots_.size(); i++) {
        auto* dot = beat_dots_[i];
        if (i == main_idx && main) {
            if (i == 0) {
                lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF9800), 0);
            } else {
                lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
            }
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, lv_color_white(), 0);
        } else if (i == main_idx) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x666666), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_50, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(0x666666), 0);
        } else if (i < main_idx) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(0x555555), 0);
        } else {
            lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(0x444444), 0);
        }
    }
}

// ======================== UI Events ========================

void MetronomeApp::onSliderChanged(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    self->bpm_ = lv_slider_get_value(lv_event_get_target_obj(e));
    self->updateBpmDisplay();
    self->setTimerPeriod();
}

void MetronomeApp::onBpmButton(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    int delta = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    self->adjustBpm(delta);
}

void MetronomeApp::onPlayPause(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    if (self->is_playing_) self->stop(); else self->start();
}

void MetronomeApp::onTimeSig(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    self->onTimeSigClicked(idx);
}

void MetronomeApp::onSubdivision(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    self->onSubdivisionClicked(idx);
}

void MetronomeApp::onSoundToggle(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    self->sound_type_ = (self->sound_type_ + 1) % 3;
    self->updateSoundButton();
}

// ======================== OnEnter / OnExit ========================

void MetronomeApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* text_font = theme->text_font()->font();
    auto fg = lv_color_white();
    int w = ctx.display->width();

    // Disable scrolling so touch events reach buttons
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    // Title
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "\xef\x80\x81  Metronome");
    lv_obj_set_style_text_font(title, text_font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // BPM Number
    bpm_label_ = lv_label_create(screen);
    lv_label_set_text_fmt(bpm_label_, "%d", bpm_);
    lv_obj_set_style_text_font(bpm_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(bpm_label_, fg, 0);
    lv_obj_align(bpm_label_, LV_ALIGN_TOP_MID, 0, 35);

    // BPM label
    lv_obj_t* bpm_text = lv_label_create(screen);
    lv_label_set_text(bpm_text, "BPM");
    lv_obj_set_style_text_font(bpm_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bpm_text, lv_color_hex(0x888888), 0);
    lv_obj_align(bpm_text, LV_ALIGN_TOP_MID, 0, 86);

    // BPM slider
    bpm_slider_ = lv_slider_create(screen);
    lv_obj_set_width(bpm_slider_, w - 48);
    lv_slider_set_range(bpm_slider_, 40, 240);
    lv_slider_set_value(bpm_slider_, bpm_, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bpm_slider_, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bpm_slider_, lv_color_hex(0x666666), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bpm_slider_, fg, LV_PART_KNOB);
    lv_obj_align(bpm_slider_, LV_ALIGN_TOP_MID, 0, 102);
    lv_obj_add_event_cb(bpm_slider_, onSliderChanged, LV_EVENT_VALUE_CHANGED, this);

    // BPM adjust buttons
    lv_obj_t* btn_row = lv_obj_create(screen);
    lv_obj_set_size(btn_row, w - 32, 32);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(btn_row, LV_ALIGN_TOP_MID, 0, 138);

    auto make_adj_btn = [&](const char* text, int delta) {
        lv_obj_t* btn = lv_btn_create(btn_row);
        lv_obj_set_size(btn, 56, 28);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, fg, 0);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void*)(intptr_t)delta);
        lv_obj_add_event_cb(btn, onBpmButton, LV_EVENT_CLICKED, this);
    };
    make_adj_btn("-10", -10);
    make_adj_btn("-1", -1);
    make_adj_btn("+1", +1);
    make_adj_btn("+10", +10);

    // Beat dots
    beat_container_ = lv_obj_create(screen);
    lv_obj_set_size(beat_container_, w, 36);
    lv_obj_set_style_bg_opa(beat_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(beat_container_, 0, 0);
    lv_obj_set_style_pad_all(beat_container_, 0, 0);
    lv_obj_set_flex_flow(beat_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(beat_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(beat_container_, LV_ALIGN_TOP_MID, 0, 178);
    rebuildBeatDots(w);

    // Time signature buttons
    lv_obj_t* ts_row = lv_obj_create(screen);
    lv_obj_set_size(ts_row, w - 32, 34);
    lv_obj_set_style_bg_opa(ts_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ts_row, 0, 0);
    lv_obj_set_style_pad_all(ts_row, 0, 0);
    lv_obj_set_flex_flow(ts_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ts_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(ts_row, LV_ALIGN_TOP_MID, 0, 220);

    const char* ts_labels[] = {"2/4", "3/4", "4/4", "6/8"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(ts_row);
        lv_obj_set_size(btn, 70, 30);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, ts_labels[i]);
        lv_obj_set_style_text_font(lbl, text_font, 0);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, onTimeSig, LV_EVENT_CLICKED, this);
        time_sig_btns_[i] = btn;
    }
    updateTimeSigButtons();

    // Subdivision buttons
    lv_obj_t* sub_row = lv_obj_create(screen);
    lv_obj_set_size(sub_row, w - 32, 34);
    lv_obj_set_style_bg_opa(sub_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sub_row, 0, 0);
    lv_obj_set_style_pad_all(sub_row, 0, 0);
    lv_obj_set_flex_flow(sub_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sub_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(sub_row, LV_ALIGN_TOP_MID, 0, 262);

    const char* sub_labels[] = {"Std", "8th", "Triplet", "16th"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(sub_row);
        lv_obj_set_size(btn, 70, 30);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, sub_labels[i]);
        lv_obj_set_style_text_font(lbl, text_font, 0);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, onSubdivision, LV_EVENT_CLICKED, this);
        sub_btns_[i] = btn;
    }
    updateSubButtons();

    // Sound type selector row
    lv_obj_t* sound_row = lv_obj_create(screen);
    lv_obj_set_size(sound_row, w - 32, 32);
    lv_obj_set_style_bg_opa(sound_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sound_row, 0, 0);
    lv_obj_set_style_pad_all(sound_row, 0, 0);
    lv_obj_set_flex_flow(sound_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sound_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(sound_row, LV_ALIGN_TOP_MID, 0, 302);

    lv_obj_t* sound_icon = lv_label_create(sound_row);
    lv_label_set_text(sound_icon, "\xef\x80\x81 Sound:");
    lv_obj_set_style_text_font(sound_icon, text_font, 0);
    lv_obj_set_style_text_color(sound_icon, fg, 0);

    sound_btn_ = lv_btn_create(sound_row);
    lv_obj_set_size(sound_btn_, 72, 28);
    lv_obj_set_style_radius(sound_btn_, 6, 0);
    lv_obj_set_style_border_width(sound_btn_, 0, 0);
    lv_obj_set_style_margin_left(sound_btn_, 12, 0);
    sound_label_ = lv_label_create(sound_btn_);
    lv_obj_set_style_text_font(sound_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sound_label_, fg, 0);
    lv_obj_center(sound_label_);
    lv_obj_add_event_cb(sound_btn_, onSoundToggle, LV_EVENT_CLICKED, this);
    updateSoundButton();

    // Play/Stop button
    play_btn_ = lv_btn_create(screen);
    lv_obj_set_size(play_btn_, 200, 54);
    lv_obj_set_style_radius(play_btn_, 12, 0);
    lv_obj_set_style_border_width(play_btn_, 0, 0);
    lv_obj_align(play_btn_, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(play_btn_, onPlayPause, LV_EVENT_CLICKED, this);
    play_label_ = lv_label_create(play_btn_);
    lv_obj_set_style_text_font(play_label_, text_font, 0);
    lv_obj_set_style_text_color(play_label_, fg, 0);
    lv_obj_center(play_label_);
    updatePlayButton();

    ESP_LOGI(TAG, "Metronome entered");
}

void MetronomeApp::OnExit() {
    stop();
    beat_dots_.clear();
    ESP_LOGI(TAG, "Metronome exited");
}
