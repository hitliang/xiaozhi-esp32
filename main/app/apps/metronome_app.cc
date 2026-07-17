#include "metronome_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "application.h"
#include "audio/audio_codec.h"
#include "audio/demuxer/ogg_demuxer.h"
#include "assets/lang_config.h"
#include "boards/common/board.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>

#define TAG "MetronomeApp"

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_48);

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kVoiceFirstPacket = 2;
constexpr int kVoicePacketCounts[] = {6, 5, 7, 7, 6, 6, 6, 6, 6};

std::string_view DigitSound(int beat) {
    switch (beat) {
        case 1: return Lang::Sounds::OGG_1;
        case 2: return Lang::Sounds::OGG_2;
        case 3: return Lang::Sounds::OGG_3;
        case 4: return Lang::Sounds::OGG_4;
        case 5: return Lang::Sounds::OGG_5;
        case 6: return Lang::Sounds::OGG_6;
        case 7: return Lang::Sounds::OGG_7;
        case 8: return Lang::Sounds::OGG_8;
        case 9: return Lang::Sounds::OGG_9;
        default: return {};
    }
}

int16_t ClampPcm(float sample) {
    const int value = static_cast<int>(std::lround(sample));
    return static_cast<int16_t>(std::clamp(value, -32767, 32767));
}

std::vector<int16_t> MakeClickSound(int sample_rate, int level) {
    static constexpr int kDurationMs[] = {14, 20, 28};
    static constexpr float kFrequency[] = {1050.0f, 1500.0f, 2100.0f};
    static constexpr float kAmplitude[] = {6500.0f, 10500.0f, 15000.0f};

    const int sample_count = std::max(1, sample_rate * kDurationMs[level] / 1000);
    const int attack_samples = std::max(1, sample_rate / 1000);
    std::vector<int16_t> pcm(sample_count);
    uint32_t noise_state = 0x9E3779B9u + static_cast<uint32_t>(level * 7919);

    for (int i = 0; i < sample_count; ++i) {
        noise_state ^= noise_state << 13;
        noise_state ^= noise_state >> 17;
        noise_state ^= noise_state << 5;

        const float progress = static_cast<float>(i) / sample_count;
        const float attack = std::min(1.0f, static_cast<float>(i) / attack_samples);
        const float decay = 1.0f - progress;
        const float envelope = attack * decay * decay;
        const float phase = 2.0f * kPi * kFrequency[level] * i / sample_rate;
        const float noise = (static_cast<int>(noise_state & 0xFFFFu) - 32768) / 32768.0f;
        const float wave = 0.78f * std::sin(phase) + 0.22f * noise;
        pcm[i] = ClampPcm(kAmplitude[level] * envelope * wave);
    }
    return pcm;
}

std::vector<int16_t> MakePianoSound(int sample_rate, int level) {
    static constexpr int kDurationMs[] = {34, 43, 50};
    static constexpr float kFrequency[] = {523.25f, 783.99f, 1046.50f};
    static constexpr float kAmplitude[] = {6500.0f, 9500.0f, 12500.0f};

    const int sample_count = std::max(1, sample_rate * kDurationMs[level] / 1000);
    const int attack_samples = std::max(1, sample_rate * 2 / 1000);
    std::vector<int16_t> pcm(sample_count);

    for (int i = 0; i < sample_count; ++i) {
        const float progress = static_cast<float>(i) / sample_count;
        const float attack = std::min(1.0f, static_cast<float>(i) / attack_samples);
        const float decay = 1.0f - progress;
        const float envelope = attack * decay * decay * decay;
        const float phase = 2.0f * kPi * kFrequency[level] * i / sample_rate;
        const float wave = std::sin(phase)
                         + 0.32f * std::sin(phase * 2.0f)
                         + 0.10f * std::sin(phase * 3.0f);
        pcm[i] = ClampPcm(kAmplitude[level] * envelope * wave);
    }
    return pcm;
}

}  // namespace

// ======================== Helpers ========================

uint64_t MetronomeApp::beatIntervalUs() const {
    const uint64_t denominator = static_cast<uint64_t>(bpm_) * steps_per_beat_;
    return (60000000ULL + denominator / 2) / denominator;
}

bool MetronomeApp::isMainBeat(int step) const {
    return step % steps_per_beat_ == 0;
}

int MetronomeApp::beatNumber(int step) const {
    return step / steps_per_beat_ + 1;
}

MetronomeApp::BeatLevel MetronomeApp::beatLevel(int step) const {
    if (!isMainBeat(step)) return BeatLevel::Subdivision;
    return beatNumber(step) == 1 ? BeatLevel::Downbeat : BeatLevel::Beat;
}

// ======================== Lifecycle ========================

void MetronomeApp::start() {
    if (is_playing_) return;
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }

    current_step_ = 0;
    beat_interval_us_ = beatIntervalUs();
    is_playing_ = true;
    updatePlayButton();

    const int64_t start_time = esp_timer_get_time();
    doBeat();
    next_beat_us_ = start_time + static_cast<int64_t>(beat_interval_us_);
    timer_ = lv_timer_create(onTimerTick, 1, this);
    scheduleNextTimer();
    enterFocusMode();
}

void MetronomeApp::stop() {
    is_playing_ = false;
    if (timer_) {
        lv_timer_del(timer_);
        timer_ = nullptr;
    }
    next_beat_us_ = 0;
    exitFocusMode();
    updatePlayButton();
    current_step_ = 0;
    for (auto* dot : beat_dots_) {
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x444444), 0);
    }
}

// ======================== Beat Logic ========================

void MetronomeApp::doBeat() {
    const int step = current_step_;
    playMetronomeSound(step);
    updateBeatVisual();

    current_step_ = (current_step_ + 1) % stepsPerMeasure();
}

void MetronomeApp::playMetronomeSound(int step) {
    const bool main_beat = isMainBeat(step);
    const BeatLevel level = beatLevel(step);
    switch (sound_type_) {
        case 0:
            if (main_beat && bpm_ <= kMaxVoiceBpm) {
                playVoiceCount(beatNumber(step));
            } else if (main_beat) {
                playSynthesizedSound(level, false);
            }
            break;
        case 1:
            playSynthesizedSound(level, false);
            break;
        case 2:
            playSynthesizedSound(level, true);
            break;
        default:
            break;
    }
}

bool MetronomeApp::playVoiceCount(int beat) {
    const auto sound = DigitSound(beat);
    if (sound.empty()) return false;

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.IsIdle()) {
        ESP_LOGD(TAG, "Dropping voice beat %d because audio is busy", beat);
        return false;
    }

    const int packet_count = (beat >= 1 && beat <= 9)
        ? kVoicePacketCounts[beat - 1]
        : kVoicePacketCounts[0];
    int packet_index = 0;
    int packets_queued = 0;
    bool queue_full = false;

    auto demuxer = std::make_unique<OggDemuxer>();
    demuxer->OnDemuxerFinished(
        [&audio, &packet_index, &packets_queued, &queue_full, packet_count]
        (const uint8_t* data, int sample_rate, size_t size) {
            const int index = packet_index++;
            if (queue_full || index < kVoiceFirstPacket ||
                index >= kVoiceFirstPacket + packet_count) {
                return;
            }

            auto packet = std::make_unique<AudioStreamPacket>();
            packet->sample_rate = sample_rate;
            packet->frame_duration = 60;
            packet->payload.resize(size);
            std::memcpy(packet->payload.data(), data, size);
            if (audio.PushPacketToDecodeQueue(std::move(packet), false)) {
                ++packets_queued;
            } else {
                queue_full = true;
            }
        });
    demuxer->Process(reinterpret_cast<const uint8_t*>(sound.data()), sound.size());
    return packets_queued > 0;
}

bool MetronomeApp::playSynthesizedSound(BeatLevel level, bool piano) {
    const size_t index = static_cast<size_t>(level);
    const auto& pcm = piano ? piano_sounds_[index] : click_sounds_[index];
    if (pcm.empty()) return false;

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.IsIdle()) {
        ESP_LOGD(TAG, "Dropping metronome tick because audio is busy");
        return false;
    }

    audio.PushPcmToPlaybackQueue(std::vector<int16_t>(pcm));
    return true;
}

void MetronomeApp::prepareSounds() {
    auto* codec = Board::GetInstance().GetAudioCodec();
    const int sample_rate = (codec && codec->output_sample_rate() > 0)
        ? codec->output_sample_rate()
        : 24000;
    if (sample_rate == sound_sample_rate_ && !click_sounds_[0].empty()) return;

    sound_sample_rate_ = sample_rate;
    for (int level = 0; level < 3; ++level) {
        click_sounds_[level] = MakeClickSound(sample_rate, level);
        piano_sounds_[level] = MakePianoSound(sample_rate, level);
    }
}

// ======================== Timer Callback ========================

void MetronomeApp::onTimerTick(lv_timer_t* timer) {
    auto* self = static_cast<MetronomeApp*>(lv_timer_get_user_data(timer));
    if (self) self->handleTimerTick();
}

void MetronomeApp::handleTimerTick() {
    if (!is_playing_ || !timer_ || beat_interval_us_ == 0) return;

    const int64_t now = esp_timer_get_time();
    if (now < next_beat_us_) {
        scheduleNextTimer();
        return;
    }

    const uint64_t late_us = static_cast<uint64_t>(now - next_beat_us_);
    const uint64_t skipped_steps = late_us / beat_interval_us_;
    if (skipped_steps > 0) {
        const int total_steps = stepsPerMeasure();
        current_step_ = (current_step_ + static_cast<int>(skipped_steps % total_steps)) % total_steps;
        ESP_LOGW(TAG, "Timer late by %llu us, skipped %llu beat steps",
                 static_cast<unsigned long long>(late_us),
                 static_cast<unsigned long long>(skipped_steps));
    }

    doBeat();
    next_beat_us_ += static_cast<int64_t>((skipped_steps + 1) * beat_interval_us_);
    scheduleNextTimer();
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
    if (idx < 0 || idx >= 4 || idx == selected_time_sig_) return;
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
    rebuildBeatDots(display->width() - 32);
    updateTimeSigButtons();
    setTimerPeriod();

    if (was_playing) start();
}

void MetronomeApp::onSubdivisionClicked(int idx) {
    if (idx < 0 || idx >= 4 || idx == selected_subdivision_) return;
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
    setBpm(bpm_ + delta, true);
}

void MetronomeApp::setBpm(int bpm, bool sync_slider) {
    bpm_ = std::clamp(bpm, kMinBpm, kMaxBpm);
    if (sound_type_ == 0 && bpm_ > kMaxVoiceBpm) {
        sound_type_ = 1;
    }

    updateBpmDisplay();
    updateSoundButton();
    if (sync_slider && bpm_slider_ && lv_slider_get_value(bpm_slider_) != bpm_) {
        lv_slider_set_value(bpm_slider_, bpm_, LV_ANIM_OFF);
    }
    setTimerPeriod();
}

void MetronomeApp::scheduleNextTimer() {
    if (!timer_ || !is_playing_) return;

    const int64_t remaining_us = next_beat_us_ - esp_timer_get_time();
    uint64_t delay_ms = remaining_us <= 0
        ? 1
        : (static_cast<uint64_t>(remaining_us) + 999) / 1000;
    delay_ms = std::clamp<uint64_t>(delay_ms, 1, UINT32_MAX);
    lv_timer_set_period(timer_, static_cast<uint32_t>(delay_ms));
    lv_timer_reset(timer_);
}

void MetronomeApp::setTimerPeriod() {
    const uint64_t new_interval_us = beatIntervalUs();
    if (!timer_ || !is_playing_) {
        beat_interval_us_ = new_interval_us;
        return;
    }

    const int64_t now = esp_timer_get_time();
    int64_t new_remaining_us = 1000;
    if (beat_interval_us_ > 0 && next_beat_us_ > now) {
        const uint64_t old_remaining_us = static_cast<uint64_t>(next_beat_us_ - now);
        new_remaining_us = static_cast<int64_t>(
            std::min<uint64_t>(new_interval_us,
                old_remaining_us * new_interval_us / beat_interval_us_));
        new_remaining_us = std::max<int64_t>(new_remaining_us, 1000);
    }

    beat_interval_us_ = new_interval_us;
    next_beat_us_ = now + new_remaining_us;
    scheduleNextTimer();
}

void MetronomeApp::updateBpmDisplay() {
    if (bpm_label_) {
        lv_label_set_text_fmt(bpm_label_, "%d", bpm_);
    }
}

void MetronomeApp::updatePlayButton() {
    if (play_icon_) {
        lv_label_set_text(play_icon_, is_playing_ ? FONT_AWESOME_STOP : FONT_AWESOME_PLAY);
    }
    if (play_label_) {
        lv_label_set_text(play_label_, is_playing_ ? "STOP" : "START");
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
    const char* names[] = {"Voice", "Click", "Piano"};
    if (sound_label_) {
        lv_label_set_text(sound_label_, names[std::clamp(sound_type_, 0, 2)]);
    }
    if (sound_btn_) {
        lv_obj_set_style_bg_color(sound_btn_,
            sound_type_ == 0 ? lv_color_hex(0x2E7D32) :
            sound_type_ == 1 ? lv_color_hex(0x1565C0) :
                               lv_color_hex(0x6A1B9A), 0);
    }
}

// ======================== Beat Dots ========================

void MetronomeApp::rebuildBeatDots(int available_w) {
    if (!beat_container_) return;
    beat_dots_.clear();
    lv_obj_clean(beat_container_);

    const int num = beats_per_measure_;
    const int dot_sz = num >= 6 ? 20 : 24;
    const int raw_gap = num > 1 ? (available_w - num * dot_sz) / (num - 1) : 0;
    const int gap = std::clamp(raw_gap, 8, 24);
    lv_obj_set_style_pad_column(beat_container_, gap, 0);

    for (int i = 0; i < num; i++) {
        lv_obj_t* dot = lv_obj_create(beat_container_);
        lv_obj_set_size(dot, dot_sz, dot_sz);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        beat_dots_.push_back(dot);
    }
}

void MetronomeApp::updateBeatVisual() {
    int main_beat = current_step_ / steps_per_beat_;
    int main_idx = main_beat;
    bool main = isMainBeat(current_step_);

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
    if (!self) return;
    self->setBpm(lv_slider_get_value(lv_event_get_target_obj(e)), false);
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
    if (!self) return;
    int next = (self->sound_type_ + 1) % 3;
    if (next == 0 && self->bpm_ > kMaxVoiceBpm) {
        next = 1;
    }
    self->sound_type_ = next;
    self->updateSoundButton();
}

void MetronomeApp::onScreenTapped(lv_event_t* e) {
    auto* self = static_cast<MetronomeApp*>(lv_event_get_user_data(e));
    if (!self || !self->focus_mode_) return;
    if (lv_event_get_target_obj(e) != self->screen_) return;
    self->exitFocusMode();
}

void MetronomeApp::enterFocusMode() {
    if (focus_mode_ || !is_playing_ || !beat_container_) return;

    focus_mode_ = true;
    for (auto* control : focus_controls_) {
        if (control) lv_obj_add_flag(control, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_height(beat_container_, 48);
    lv_obj_align(beat_container_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(beat_container_);
}

void MetronomeApp::exitFocusMode() {
    if (!focus_mode_) return;

    focus_mode_ = false;
    for (auto* control : focus_controls_) {
        if (control) lv_obj_clear_flag(control, LV_OBJ_FLAG_HIDDEN);
    }

    if (beat_container_) {
        lv_obj_set_height(beat_container_, 36);
        lv_obj_align(beat_container_, LV_ALIGN_TOP_MID, 0, 178);
    }
}

// ======================== OnEnter / OnExit ========================

void MetronomeApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    auto* icon_font = theme->icon_font()->font();
    auto fg = lv_color_white();
    int w = ctx.display->width();

    prepareSounds();
    beat_interval_us_ = beatIntervalUs();
    if (sound_type_ == 0 && bpm_ > kMaxVoiceBpm) sound_type_ = 1;
    screen_ = screen;
    focus_mode_ = false;
    focus_controls_.clear();

    // Disable scrolling so touch events reach buttons
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(screen, onScreenTapped, LV_EVENT_CLICKED, this);

    // Title
    lv_obj_t* title_row = lv_obj_create(screen);
    lv_obj_set_size(title_row, w - 32, 30);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_style_pad_column(title_row, 8, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(title_row, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t* title_icon = lv_label_create(title_row);
    lv_label_set_text(title_icon, FONT_AWESOME_CLOCK);
    lv_obj_set_style_text_font(title_icon, icon_font, 0);
    lv_obj_set_style_text_color(title_icon, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t* title = lv_label_create(title_row);
    lv_label_set_text(title, "Metronome");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xAAAAAA), 0);

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
    lv_slider_set_range(bpm_slider_, kMinBpm, kMaxBpm);
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
        lv_obj_set_size(btn, 56, 32);
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
    lv_obj_set_size(beat_container_, w - 32, 36);
    lv_obj_set_style_bg_opa(beat_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(beat_container_, 0, 0);
    lv_obj_set_style_pad_all(beat_container_, 0, 0);
    lv_obj_clear_flag(beat_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(beat_container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(beat_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(beat_container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(beat_container_, LV_ALIGN_TOP_MID, 0, 178);
    rebuildBeatDots(w - 32);

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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, onSubdivision, LV_EVENT_CLICKED, this);
        sub_btns_[i] = btn;
    }
    updateSubButtons();

    // Sound type selector row
    lv_obj_t* sound_row = lv_obj_create(screen);
    lv_obj_set_size(sound_row, w - 32, 36);
    lv_obj_set_style_bg_opa(sound_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sound_row, 0, 0);
    lv_obj_set_style_pad_all(sound_row, 0, 0);
    lv_obj_set_style_pad_column(sound_row, 8, 0);
    lv_obj_set_flex_flow(sound_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sound_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(sound_row, LV_ALIGN_TOP_MID, 0, 302);

    lv_obj_t* sound_icon = lv_label_create(sound_row);
    lv_label_set_text(sound_icon, FONT_AWESOME_VOLUME_HIGH);
    lv_obj_set_style_text_font(sound_icon, icon_font, 0);
    lv_obj_set_style_text_color(sound_icon, fg, 0);

    lv_obj_t* sound_text = lv_label_create(sound_row);
    lv_label_set_text(sound_text, "Sound");
    lv_obj_set_style_text_font(sound_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sound_text, fg, 0);

    sound_btn_ = lv_btn_create(sound_row);
    lv_obj_set_size(sound_btn_, 92, 34);
    lv_obj_set_style_radius(sound_btn_, 6, 0);
    lv_obj_set_style_border_width(sound_btn_, 0, 0);
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
    lv_obj_set_style_pad_all(play_btn_, 0, 0);
    lv_obj_set_style_pad_column(play_btn_, 10, 0);
    lv_obj_set_flex_flow(play_btn_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(play_btn_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(play_btn_, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(play_btn_, onPlayPause, LV_EVENT_CLICKED, this);

    play_icon_ = lv_label_create(play_btn_);
    lv_obj_set_style_text_font(play_icon_, icon_font, 0);
    lv_obj_set_style_text_color(play_icon_, fg, 0);
    play_label_ = lv_label_create(play_btn_);
    lv_obj_set_style_text_font(play_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(play_label_, fg, 0);
    updatePlayButton();

    focus_controls_ = {
        title_row,
        bpm_label_,
        bpm_text,
        bpm_slider_,
        btn_row,
        ts_row,
        sub_row,
        sound_row,
        play_btn_,
    };

    ESP_LOGI(TAG, "Metronome entered");
}

void MetronomeApp::OnExit() {
    stop();
    beat_dots_.clear();
    resetUiPointers();
    ESP_LOGI(TAG, "Metronome exited");
}

void MetronomeApp::resetUiPointers() {
    screen_ = nullptr;
    focus_mode_ = false;
    focus_controls_.clear();
    bpm_label_ = nullptr;
    bpm_slider_ = nullptr;
    play_btn_ = nullptr;
    play_icon_ = nullptr;
    play_label_ = nullptr;
    sound_btn_ = nullptr;
    sound_label_ = nullptr;
    beat_container_ = nullptr;
    for (auto& btn : time_sig_btns_) btn = nullptr;
    for (auto& btn : sub_btns_) btn = nullptr;
}
