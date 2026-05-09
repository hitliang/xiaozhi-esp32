#pragma once

#include "../app.h"
#include <font_awesome.h>

class AttitudeView;
class AttitudeApp : public App {
public:
    const char* GetName() const override { return "Attitude"; }
    const char* GetIcon() const override { return FONT_AWESOME_COMPASS; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
    bool OnUpdate() override;

private:
    AttitudeView* view_ = nullptr;
};
