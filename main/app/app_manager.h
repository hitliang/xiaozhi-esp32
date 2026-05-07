#pragma once

#include "app.h"
#include <memory>
#include <vector>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

class AppManager {
public:
    static AppManager& GetInstance();

    // Register an app. Called once at boot.
    void RegisterApp(const char* id, std::unique_ptr<App> app);

    // Create home screen and grid screen (called once after display SetupUI)
    void InitializeLauncher(AppContext& ctx);

    // Called from the main event loop clock tick (~1Hz)
    void OnClockTick();

    // Handle BOOT button single click (global behavior)
    void HandleBootClick();

    // Check if a screen switch is pending
    bool HasPendingSwitch() const;

    // Process a queued app switch (called from main event loop)
    void ProcessPendingSwitch();

    // Get current active app ID (empty = on home/grid)
    const std::string& GetCurrentAppId() const { return current_app_id_; }

    // Is the user currently in an app (not on home/grid)?
    bool IsInApp() const { return !current_app_id_.empty(); }

    // Is the screen currently in black/sleep mode?
    bool IsScreenOff() const { return screen_off_; }

    // Get list of registered apps (for launcher grid building)
    const std::vector<std::pair<std::string, App*>>& GetApps() const { return apps_; }

    // Switch to the given app ID (async, via main loop)
    void SwitchToApp(const char* app_id);

    // Load the default screen (screen 0, where XiaoZhi UI lives)
    void LoadDefaultScreen();

private:
    AppManager() = default;

    struct RegisteredApp {
        std::string id;
        std::unique_ptr<App> app;
    };

    std::vector<std::pair<std::string, App*>> apps_;  // Flat list for grid iteration
    std::vector<RegisteredApp> registered_apps_;       // Owns the App objects

    App* current_app_ = nullptr;
    std::string current_app_id_;
    std::string pending_switch_id_;
    bool pending_switch_ = false;

    AppContext* app_context_ = nullptr;

    // LVGL screen objects
    lv_obj_t* home_screen_ = nullptr;
    lv_obj_t* grid_screen_ = nullptr;
    lv_obj_t* app_screen_ = nullptr;    // Current app's screen
    lv_obj_t* black_screen_ = nullptr;  // Black sleep screen
    lv_obj_t* default_screen_ = nullptr; // Screen 0 - XiaoZhi UI lives here

    // Home screen widgets
    lv_obj_t* home_time_label_ = nullptr;
    lv_obj_t* home_date_label_ = nullptr;
    lv_obj_t* home_wifi_label_ = nullptr;
    lv_obj_t* home_battery_label_ = nullptr;
    lv_obj_t* home_mute_label_ = nullptr;

    // Grid screen widgets
    lv_obj_t* grid_tiles_[9] = {nullptr};
    lv_obj_t* grid_wifi_label_ = nullptr;
    lv_obj_t* grid_battery_label_ = nullptr;
    lv_obj_t* grid_mute_label_ = nullptr;

    bool screen_off_ = false;
    bool home_active_ = true;  // true=home visible, false=grid visible

    void BuildHomeScreen();
    void BuildGridScreen();
    void BuildBlackScreen();
    void UpdateStatusBarIcons(lv_obj_t* wifi_label, lv_obj_t* battery_label, lv_obj_t* mute_label);
    void ShowHome();
    void ShowGrid();
    void EnterBlackScreen();
    void ExitBlackScreen();
    void OnSwipeLeft();
    void OnSwipeRight();
    void OpenApp(int grid_index);
};
