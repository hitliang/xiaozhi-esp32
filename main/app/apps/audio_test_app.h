#pragma once

#include "../app.h"
#include <font_awesome.h>

class AudioTestApp : public App {
public:
    const char* GetName() const override { return "音频测试"; }
    const char* GetIcon() const override { return FONT_AWESOME_MUSIC; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
};
