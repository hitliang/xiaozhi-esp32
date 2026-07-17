#include "xiaozhi_app.h"
#include "../app_manager.h"
#include "application.h"
#include "boards/common/board.h"
#include "display.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "XiaozhiApp"

void XiaozhiApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    // Load the default screen where the XiaoZhi UI already exists
    auto& mgr = AppManager::GetInstance();
    mgr.LoadDefaultScreen();

    // Clear chat and reset emotion for a fresh start
    auto* display = ctx.display;
    if (display) {
        display->ClearChatMessages();
        // Enter the dedicated text-free animated face mode before setting the first expression.
        display->SetHideSubtitle(true);
        display->SetEmotionLarge(true);
        display->SetEmotion("neutral");
    }

    // Start voice interaction (same path as BOOT button in original code)
    auto& app = Application::GetInstance();
    next_reconnect_at_us_ = 0;
    if (app.GetDeviceState() == kDeviceStateIdle) {
        app.ToggleChatState();
    }

    ESP_LOGI(TAG, "Entered XiaoZhi app");
}

void XiaozhiApp::OnExit() {
    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    next_reconnect_at_us_ = 0;

    if (state == kDeviceStateSpeaking) {
        app.AbortSpeaking(kAbortReasonNone);
    }

    // Close immediately while still in the app. Posting StopListening here can
    // race with the state change below and leave the WebSocket alive in the
    // background after the user returns to the launcher.
    app.CloseAudioChannel();
    app.SetDeviceState(kDeviceStateIdle);

    // Restore subtitle and normal emotion display
    auto* display = Board::GetInstance().GetDisplay();
    if (display) {
        display->SetEmotionLarge(false);
        display->SetHideSubtitle(false);
    }

    ESP_LOGI(TAG, "Exited XiaoZhi app");
}

bool XiaozhiApp::OnUpdate() {
    auto& app = Application::GetInstance();
    const auto state = app.GetDeviceState();
    const int64_t now = esp_timer_get_time();

    if (state == kDeviceStateIdle) {
        if (now >= next_reconnect_at_us_) {
            // Network interruptions shouldn't strand the full-screen face in
            // idle. Retry at a bounded rate while this app remains active.
            next_reconnect_at_us_ = now + 5 * 1000 * 1000;
            ESP_LOGI(TAG, "Voice channel idle; reconnecting");
            app.ToggleChatState();
        }
    } else {
        // Once a live session drops, retry on the next app update (about 1 s).
        next_reconnect_at_us_ = now + 1000 * 1000;
    }

    return false;
}
