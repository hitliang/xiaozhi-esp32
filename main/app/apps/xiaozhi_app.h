#pragma once

#include "../app.h"
#include <font_awesome.h>

class XiaozhiApp : public App {
public:
    const char* GetName() const override { return "小智AI"; }
    const char* GetIcon() const override { return FONT_AWESOME_MICROCHIP_AI; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
    bool OnUpdate() override;
};
