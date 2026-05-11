#pragma once

#include "../app.h"
#include <font_awesome.h>

class WeatherApp : public App {
public:
    const char* GetName() const override { return "Weather"; }
    const char* GetIcon() const override { return FONT_AWESOME_CLOUD_SUN; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;

    // Accessed by fetch task
    int fetchState_ = 0;  // 0=loading, 1=ok, -1=error
    char todayWeather_[64] = {};
    char todayTemp_[32] = {};
    char todayWind_[64] = {};
    char forecast_[3][64] = {};

private:
    void BuildUI();
    Display* display_ = nullptr;
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* loadingLabel_ = nullptr;
    lv_timer_t* pollTimer_ = nullptr;
};
