#pragma once

#include "../app.h"
#include <font_awesome.h>
#include <array>
#include <cstdint>
#include <vector>

class MetronomeApp : public App {
public:
    const char* GetName() const override { return "Metronome"; }
    const char* GetIcon() const override { return FONT_AWESOME_CLOCK; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;

private:
    enum class BeatLevel : uint8_t {
        Subdivision = 0,
        Beat,
        Downbeat,
    };

    static constexpr int kMinBpm = 40;
    static constexpr int kMaxBpm = 240;
    static constexpr int kMaxVoiceBpm = 140;

    // --- State ---
    bool is_playing_ = false;
    int bpm_ = 120;
    int beats_per_measure_ = 4;
    int steps_per_beat_ = 1;
    int current_step_ = 0;
    int selected_time_sig_ = 2;       // 0=2/4, 1=3/4, 2=4/4, 3=6/8
    int selected_subdivision_ = 0;    // 0=Std, 1=8th, 2=Triplet, 3=16th
    int sound_type_ = 0;              // 0=Voice, 1=Click, 2=Piano
    bool focus_mode_ = false;

    // --- LVGL objects ---
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* bpm_label_ = nullptr;
    lv_obj_t* bpm_slider_ = nullptr;
    lv_obj_t* play_btn_ = nullptr;
    lv_obj_t* play_icon_ = nullptr;
    lv_obj_t* play_label_ = nullptr;
    lv_obj_t* sound_btn_ = nullptr;
    lv_obj_t* sound_label_ = nullptr;
    lv_obj_t* beat_container_ = nullptr;
    lv_obj_t* time_sig_btns_[4] = {};
    lv_obj_t* sub_btns_[4] = {};
    std::vector<lv_obj_t*> beat_dots_;
    std::vector<lv_obj_t*> focus_controls_;

    lv_timer_t* timer_ = nullptr;
    uint64_t beat_interval_us_ = 500000;
    int64_t next_beat_us_ = 0;

    int sound_sample_rate_ = 0;
    std::array<std::vector<int16_t>, 3> click_sounds_;
    std::array<std::vector<int16_t>, 3> piano_sounds_;

    // --- Helpers ---
    int stepsPerMeasure() const { return beats_per_measure_ * steps_per_beat_; }
    uint64_t beatIntervalUs() const;
    bool isMainBeat(int step) const;
    int beatNumber(int step) const;
    BeatLevel beatLevel(int step) const;

    void start();
    void stop();
    void doBeat();
    void handleTimerTick();
    void playMetronomeSound(int step);
    bool playVoiceCount(int beat);
    bool playSynthesizedSound(BeatLevel level, bool piano);
    void prepareSounds();
    void updateBpmDisplay();
    void updateBeatVisual();
    void updatePlayButton();
    void updateTimeSigButtons();
    void updateSubButtons();
    void updateSoundButton();
    void rebuildBeatDots(int available_w);
    void scheduleNextTimer();
    void setTimerPeriod();
    void recalculateSteps();
    void onTimeSigClicked(int idx);
    void onSubdivisionClicked(int idx);
    void setBpm(int bpm, bool sync_slider);
    void adjustBpm(int delta);
    void enterFocusMode();
    void exitFocusMode();
    void resetUiPointers();

    static void onTimerTick(lv_timer_t* timer);
    static void onSliderChanged(lv_event_t* e);
    static void onBpmButton(lv_event_t* e);
    static void onPlayPause(lv_event_t* e);
    static void onTimeSig(lv_event_t* e);
    static void onSubdivision(lv_event_t* e);
    static void onSoundToggle(lv_event_t* e);
    static void onScreenTapped(lv_event_t* e);
};
