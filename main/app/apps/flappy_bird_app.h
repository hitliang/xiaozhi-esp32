#pragma once

#include "../app.h"
#include <font_awesome.h>

class FlappyBirdView;
class FlappyBirdApp : public App {
public:
    const char* GetName() const override { return "Flappy Bird"; }
    const char* GetIcon() const override { return FONT_AWESOME_GAMEPAD; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;

private:
    FlappyBirdView* view_ = nullptr;
};
