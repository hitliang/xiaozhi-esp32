#pragma once

#include "../app.h"
#include <font_awesome.h>

class EarTrainerView;
class EarTrainerApp : public App {
public:
    const char* GetName() const override { return "Ear Train"; }
    const char* GetIcon() const override { return FONT_AWESOME_MUSIC; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
    bool OnUpdate() override;

private:
    EarTrainerView* view_ = nullptr;
};
