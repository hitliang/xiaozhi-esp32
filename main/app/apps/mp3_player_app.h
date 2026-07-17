#pragma once

#include "../app.h"
#include <font_awesome.h>

class Mp3PlayerView;

class Mp3PlayerApp : public App {
public:
    const char* GetName() const override { return "MP3 Player"; }
    const char* GetIcon() const override { return FONT_AWESOME_MUSIC; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;

private:
    Mp3PlayerView* view_ = nullptr;
};
