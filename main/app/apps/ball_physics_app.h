#pragma once

#include "../app.h"
#include <font_awesome.h>

class BallPhysicsView;
class BallPhysicsApp : public App {
public:
    const char* GetName() const override { return "Ball"; }
    const char* GetIcon() const override { return FONT_AWESOME_GAMEPAD; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
    bool OnUpdate() override;

private:
    BallPhysicsView* view_ = nullptr;
};
