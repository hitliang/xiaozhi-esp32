#pragma once

#include "../app.h"
#include <font_awesome.h>
#include <vector>

class MetronomeApp : public App {
public:
    const char* GetName() const override { return "Metronome"; }
    const char* GetIcon() const override { return FONT_AWESOME_CLOCK; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;

private:
    // --- State ---
    bool is_playing_ = false;
    int bpm_ = 120;
    int beats_per_measure_ = 4;
    int steps_per_beat_ = 1;
    int current_step_ = 0;
    int selected_time_sig_ = 2;       // 0=2/4, 1=3/4, 2=4/4, 3=6/8
    int selected_subdivision_ = 0;    // 0=Std, 1=8th, 2=Triplet, 3=16th
    bool voice_count_enabled_ = true;

    // --- LVGL objects ---
    lv_obj_t* bpm_label_ = nullptr;
    lv_obj_t* bpm_slider_ = nullptr;
    lv_obj_t* play_btn_ = nullptr;
    lv_obj_t* play_label_ = nullptr;
    lv_obj_t* voice_btn_ = nullptr;
    lv_obj_t* voice_label_ = nullptr;
    lv_obj_t* beat_container_ = nullptr;
    lv_obj_t* time_sig_btns_[4] = {};
    lv_obj_t* sub_btns_[4] = {};
    std::vector<lv_obj_t*> beat_dots_;

    lv_timer_t* timer_ = nullptr;

    // --- Helpers ---
    int stepsPerMeasure() const { return beats_per_measure_ * steps_per_beat_; }
    int timerPeriodMs() const;
    bool isMainBeat() const;
    int beatNumber() const;

    void start();
    void stop();
    void doBeat();
    void playDigitSound(int beat);
    void updateBpmDisplay();
    void updateBeatVisual();
    void updatePlayButton();
    void updateTimeSigButtons();
    void updateSubButtons();
    void updateVoiceToggle();
    void rebuildBeatDots(int display_w);
    void setTimerPeriod();
    void recalculateSteps();
    void onTimeSigClicked(int idx);
    void onSubdivisionClicked(int idx);
    void adjustBpm(int delta);

    static void onTimerTick(lv_timer_t* timer);
    static void onSliderChanged(lv_event_t* e);
    static void onBpmButton(lv_event_t* e);
    static void onPlayPause(lv_event_t* e);
    static void onTimeSig(lv_event_t* e);
    static void onSubdivision(lv_event_t* e);
    static void onVoiceToggle(lv_event_t* e);
};
