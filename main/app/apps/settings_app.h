#pragma once

#include "../app.h"
#include <font_awesome.h>

class SettingsApp : public App {
public:
    const char* GetName() const override { return "设置"; }
    const char* GetIcon() const override { return FONT_AWESOME_GEAR; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
};
