#include "xiaozhi_app.h"
#include "../app_manager.h"
#include "application.h"
#include "boards/common/board.h"
#include "display.h"

#include <esp_log.h>

#define TAG "XiaozhiApp"

void XiaozhiApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    // Load the default screen where the XiaoZhi UI already exists
    auto& mgr = AppManager::GetInstance();
    mgr.LoadDefaultScreen();

    // Clear chat and reset emotion for a fresh start
    auto* display = ctx.display;
    if (display) {
        display->ClearChatMessages();
        display->SetEmotion("neutral");
    }

    // Start voice interaction (same path as BOOT button in original code)
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateIdle) {
        app.ToggleChatState();
    }

    ESP_LOGI(TAG, "Entered XiaoZhi app");
}

void XiaozhiApp::OnExit() {
    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();

    if (state == kDeviceStateSpeaking) {
        app.AbortSpeaking(kAbortReasonNone);
    }

    if (state == kDeviceStateListening || state == kDeviceStateConnecting || state == kDeviceStateSpeaking) {
        app.StopListening();
    }

    // Return to idle
    app.SetDeviceState(kDeviceStateIdle);

    ESP_LOGI(TAG, "Exited XiaoZhi app");
}

bool XiaozhiApp::OnUpdate() {
    return false;
}
