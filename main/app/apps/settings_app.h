#pragma once

#include "../app.h"
#include <font_awesome.h>

class Display;

class SettingsApp : public App {
public:
    const char* GetName() const override { return "Settings"; }
    const char* GetIcon() const override { return FONT_AWESOME_GEAR; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
    bool OnUpdate() override;

private:
    Display* display_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* volume_value_label_ = nullptr;
    lv_obj_t* brightness_value_label_ = nullptr;
    lv_obj_t* wifi_value_label_ = nullptr;
    lv_obj_t* ip_value_label_ = nullptr;
    lv_obj_t* signal_value_label_ = nullptr;
    lv_obj_t* battery_value_label_ = nullptr;

    int committed_volume_ = 50;
    int committed_brightness_ = 75;
    int refresh_ticks_ = 0;

    static void VolumeEvent(lv_event_t* event);
    static void BrightnessEvent(lv_event_t* event);

    void UpdatePercentLabel(lv_obj_t* label, int value);
    void RefreshDynamicInfo();
    void CommitPendingValues();
};
