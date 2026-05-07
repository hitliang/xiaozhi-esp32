#pragma once

#include "../app.h"
#include <font_awesome.h>

class WeatherApp : public App {
public:
    const char* GetName() const override { return "天气"; }
    const char* GetIcon() const override { return FONT_AWESOME_CLOUD_SUN; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
};
