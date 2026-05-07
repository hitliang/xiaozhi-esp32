#include "app_manager.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "boards/common/board.h"
#include "application.h"
#include "settings.h"

#include <esp_log.h>
#include <font_awesome.h>
#include <ctime>

#define TAG "AppManager"

AppManager& AppManager::GetInstance() {
    static AppManager instance;
    return instance;
}

void AppManager::RegisterApp(const char* id, std::unique_ptr<App> app) {
    apps_.push_back({id, app.get()});
    registered_apps_.push_back({id, std::move(app)});
    ESP_LOGI(TAG, "Registered app: %s", id);
}

static LvglTheme* GetLvglTheme() {
    auto* theme = Board::GetInstance().GetDisplay()->GetTheme();
    return static_cast<LvglTheme*>(theme);
}

void AppManager::InitializeLauncher(AppContext& ctx) {
    app_context_ = ctx;

    // Save the default screen (screen 0) where XiaoZhi UI lives
    auto* display = Board::GetInstance().GetDisplay();
    {
        DisplayLockGuard lock(display);
        default_screen_ = lv_screen_active();
    }

    BuildHomeScreen();
    BuildGridScreen();
    BuildBlackScreen();

    // Start on home screen
    ShowHome();
    ESP_LOGI(TAG, "Launcher initialized");
}

void AppManager::BuildHomeScreen() {
    auto* theme = GetLvglTheme();
    auto* display = Board::GetInstance().GetDisplay();
    auto* text_font = theme->text_font()->font();
    auto* icon_font = theme->icon_font()->font();
    DisplayLockGuard lock(display);

    auto bg_color = lv_color_black();
    auto fg_color = lv_color_hex(0xFFFFFF);

    home_screen_ = lv_obj_create(nullptr);
    lv_obj_set_size(home_screen_, display->width(), display->height());
    lv_obj_set_style_bg_color(home_screen_, bg_color, 0);
    lv_obj_set_style_bg_opa(home_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_screen_, 0, 0);
    lv_obj_set_style_pad_all(home_screen_, 0, 0);

    // Top status bar (transparent on black)
    lv_obj_t* top_bar = lv_obj_create(home_screen_);
    lv_obj_set_size(top_bar, display->width(), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_hor(top_bar, 16, 0);
    lv_obj_set_style_pad_ver(top_bar, 8, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF);

    // WiFi icon (left)
    home_wifi_label_ = lv_label_create(top_bar);
    lv_label_set_text(home_wifi_label_, "");
    lv_obj_set_style_text_font(home_wifi_label_, icon_font, 0);
    lv_obj_set_style_text_color(home_wifi_label_, fg_color, 0);

    // Right icons: mute + battery
    lv_obj_t* right_icons = lv_obj_create(top_bar);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    home_mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(home_mute_label_, "");
    lv_obj_set_style_text_font(home_mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(home_mute_label_, fg_color, 0);

    home_battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(home_battery_label_, "");
    lv_obj_set_style_text_font(home_battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(home_battery_label_, fg_color, 0);
    lv_obj_set_style_margin_left(home_battery_label_, 8, 0);

    // Center content: time + date
    lv_obj_t* center = lv_obj_create(home_screen_);
    lv_obj_set_size(center, display->width(), display->height() - 60);
    lv_obj_align(center, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Time (use text_font, not icon_font - icon fonts don't have digits)
    home_time_label_ = lv_label_create(center);
    lv_obj_set_style_text_font(home_time_label_, text_font, 0);
    lv_obj_set_style_text_color(home_time_label_, fg_color, 0);
    lv_label_set_text(home_time_label_, "00:00");

    // Date
    home_date_label_ = lv_label_create(center);
    lv_obj_set_style_text_font(home_date_label_, text_font, 0);
    lv_obj_set_style_text_color(home_date_label_, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(home_date_label_, "");
    lv_obj_set_style_margin_top(home_date_label_, 4, 0);

    // Swipe hint at bottom
    lv_obj_t* hint = lv_label_create(home_screen_);
    lv_obj_set_style_text_font(hint, text_font, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_label_set_text(hint, "← Swipe for Menu →");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Swipe gesture detection
    lv_obj_add_event_cb(home_screen_, [](lv_event_t* e) {
        auto* self = static_cast<AppManager*>(lv_event_get_user_data(e));
        auto gesture = lv_indev_get_gesture_dir(lv_indev_active());
        if (gesture == LV_DIR_LEFT) {
            self->OnSwipeLeft();
        } else if (gesture == LV_DIR_RIGHT) {
            self->OnSwipeRight();
        }
    }, LV_EVENT_GESTURE, this);

    UpdateStatusBarIcons(home_wifi_label_, home_battery_label_, home_mute_label_);
}

void AppManager::BuildGridScreen() {
    auto* theme = GetLvglTheme();
    auto* display = Board::GetInstance().GetDisplay();
    auto* text_font = theme->text_font()->font();
    auto* icon_font = theme->icon_font()->font();
    DisplayLockGuard lock(display);

    auto bg_color = lv_color_black();
    auto fg_color = lv_color_hex(0xFFFFFF);
    auto tile_bg = lv_color_hex(0x000000);
    auto tile_border = lv_color_hex(0x000000);

    grid_screen_ = lv_obj_create(nullptr);
    lv_obj_set_size(grid_screen_, display->width(), display->height());
    lv_obj_set_style_bg_color(grid_screen_, bg_color, 0);
    lv_obj_set_style_bg_opa(grid_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid_screen_, 0, 0);
    lv_obj_set_style_pad_all(grid_screen_, 0, 0);

    // Top status bar (transparent)
    lv_obj_t* top_bar = lv_obj_create(grid_screen_);
    lv_obj_set_size(top_bar, display->width(), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_hor(top_bar, 16, 0);
    lv_obj_set_style_pad_ver(top_bar, 8, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF);

    grid_wifi_label_ = lv_label_create(top_bar);
    lv_label_set_text(grid_wifi_label_, "");
    lv_obj_set_style_text_font(grid_wifi_label_, icon_font, 0);
    lv_obj_set_style_text_color(grid_wifi_label_, fg_color, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);

    grid_mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(grid_mute_label_, "");
    lv_obj_set_style_text_font(grid_mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(grid_mute_label_, fg_color, 0);

    grid_battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(grid_battery_label_, "");
    lv_obj_set_style_text_font(grid_battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(grid_battery_label_, fg_color, 0);
    lv_obj_set_style_margin_left(grid_battery_label_, 8, 0);

    // Grid area
    lv_obj_t* grid_area = lv_obj_create(grid_screen_);
    lv_obj_set_size(grid_area, display->width(), display->height() - 50);
    lv_obj_align(grid_area, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(grid_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid_area, 0, 0);
    lv_obj_set_style_pad_all(grid_area, 8, 0);

    lv_obj_set_flex_flow(grid_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid_area, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int tile_w = (display->width() - 32) / 3;
    int tile_h = (display->height() - 90) / 3;

    for (int row = 0; row < 3; row++) {
        lv_obj_t* row_cont = lv_obj_create(grid_area);
        lv_obj_set_size(row_cont, display->width() - 16, tile_h);
        lv_obj_set_style_bg_opa(row_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row_cont, 0, 0);
        lv_obj_set_style_pad_all(row_cont, 0, 0);
        lv_obj_set_flex_flow(row_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            lv_obj_t* tile = lv_obj_create(row_cont);
            lv_obj_set_size(tile, tile_w - 4, tile_h - 4);
            lv_obj_set_style_radius(tile, 16, 0);
            lv_obj_set_style_bg_color(tile, tile_bg, 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_border_color(tile, tile_border, 0);
            lv_obj_set_style_pad_all(tile, 6, 0);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Icon - use icon_font for compact grid tiles
            lv_obj_t* icon = lv_label_create(tile);
            lv_obj_set_style_text_font(icon, icon_font, 0);
            lv_obj_set_style_text_color(icon, fg_color, 0);
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(icon, LV_PCT(100));
            lv_label_set_text(icon, "");

            // Name - use proportional Montserrat font for compact English text
            lv_obj_t* name = lv_label_create(tile);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(name, fg_color, 0);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(name, LV_PCT(100));
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_label_set_text(name, "");
            lv_obj_set_style_margin_top(name, 2, 0);

            lv_obj_set_user_data(tile, icon);

            lv_obj_add_event_cb(tile, [](lv_event_t* e) {
                lv_obj_t* tile = lv_event_get_target_obj(e);
                auto* self = static_cast<AppManager*>(lv_event_get_user_data(e));
                for (int i = 0; i < 9; i++) {
                    if (self->grid_tiles_[i] == tile) {
                        self->OpenApp(i);
                        return;
                    }
                }
            }, LV_EVENT_CLICKED, this);

            grid_tiles_[idx] = tile;
        }
    }

    // Swipe gesture to go back to home
    lv_obj_add_event_cb(grid_screen_, [](lv_event_t* e) {
        auto* self = static_cast<AppManager*>(lv_event_get_user_data(e));
        auto gesture = lv_indev_get_gesture_dir(lv_indev_active());
        if (gesture == LV_DIR_RIGHT) {
            self->OnSwipeRight();
        }
    }, LV_EVENT_GESTURE, this);
}

void AppManager::PopulateGridTiles() {
    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);

    int count = apps_.size();
    if (count > 9) count = 9;
    for (int i = 0; i < count; i++) {
        auto* tile = grid_tiles_[i];
        if (!tile) continue;
        uint32_t child_cnt = lv_obj_get_child_cnt(tile);
        if (child_cnt >= 2) {
            lv_obj_t* icon_label = lv_obj_get_child(tile, 0);
            lv_obj_t* name_label = lv_obj_get_child(tile, 1);
            if (icon_label) lv_label_set_text(icon_label, apps_[i].second->GetIcon());
            if (name_label) lv_label_set_text(name_label, apps_[i].second->GetName());
        }
    }
    ESP_LOGI(TAG, "Grid populated with %d apps", count);
}

void AppManager::BuildBlackScreen() {
    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);

    black_screen_ = lv_obj_create(nullptr);
    lv_obj_set_size(black_screen_, display->width(), display->height());
    lv_obj_set_style_bg_color(black_screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(black_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(black_screen_, 0, 0);
    lv_obj_set_style_pad_all(black_screen_, 0, 0);

    // Touch anywhere on black screen to wake up
    lv_obj_add_event_cb(black_screen_, [](lv_event_t* e) {
        auto* self = static_cast<AppManager*>(lv_event_get_user_data(e));
        self->ExitBlackScreen();
    }, LV_EVENT_CLICKED, this);
}

void AppManager::UpdateStatusBarIcons(lv_obj_t* wifi_label, lv_obj_t* battery_label, lv_obj_t* mute_label) {
    auto& board = Board::GetInstance();

    // WiFi icon
    if (wifi_label) {
        const char* wifi_icon = board.GetNetworkStateIcon();
        if (wifi_icon) {
            lv_label_set_text(wifi_label, wifi_icon);
        }
    }

    // Battery icon
    if (battery_label) {
        int level;
        bool charging, discharging;
        if (board.GetBatteryLevel(level, charging, discharging)) {
            const char* icon;
            if (charging) {
                icon = FONT_AWESOME_BATTERY_BOLT;
            } else {
                const char* levels[] = {
                    FONT_AWESOME_BATTERY_EMPTY,
                    FONT_AWESOME_BATTERY_QUARTER,
                    FONT_AWESOME_BATTERY_HALF,
                    FONT_AWESOME_BATTERY_THREE_QUARTERS,
                    FONT_AWESOME_BATTERY_FULL,
                    FONT_AWESOME_BATTERY_FULL,
                };
                icon = levels[level / 20];
            }
            lv_label_set_text(battery_label, icon);
        }
    }

    // Mute icon
    if (mute_label) {
        auto codec = board.GetAudioCodec();
        if (codec && codec->output_volume() == 0) {
            lv_label_set_text(mute_label, FONT_AWESOME_VOLUME_XMARK);
        } else {
            lv_label_set_text(mute_label, "");
        }
    }
}

void AppManager::ShowHome() {
    if (screen_off_) return;
    home_active_ = true;

    // Cancel any stale pending app switch (e.g. tile click during swipe gesture)
    pending_switch_ = false;
    pending_switch_id_.clear();

    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    lv_screen_load(home_screen_);
    ESP_LOGI(TAG, "Showing home screen");
}

void AppManager::ShowGrid() {
    if (screen_off_) return;
    home_active_ = false;

    // Cancel any stale pending app switch
    pending_switch_ = false;
    pending_switch_id_.clear();

    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    lv_screen_load(grid_screen_);
    ESP_LOGI(TAG, "Showing grid screen");
}

void AppManager::LoadDefaultScreen() {
    if (default_screen_) {
        lv_screen_load(default_screen_);
        ESP_LOGI(TAG, "Loaded default screen (XiaoZhi)");
    }
}

void AppManager::EnterBlackScreen() {
    if (screen_off_) return;
    screen_off_ = true;

    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    lv_screen_load(black_screen_);

    // Turn off backlight
    auto* backlight = Board::GetInstance().GetBacklight();
    if (backlight) {
        backlight->SetBrightness(0);
    }
    ESP_LOGI(TAG, "Screen off");
}

void AppManager::ExitBlackScreen() {
    if (!screen_off_) return;
    screen_off_ = false;

    // Restore backlight
    auto* backlight = Board::GetInstance().GetBacklight();
    if (backlight) {
        backlight->RestoreBrightness();
    }

    // Return to home
    ShowHome();
    ESP_LOGI(TAG, "Screen on");
}

void AppManager::OnSwipeLeft() {
    if (screen_off_) return;
    if (home_active_ && !swipe_pending_) {
        swipe_pending_ = true;
        pending_switch_ = false;
        pending_switch_id_.clear();
        Application::GetInstance().Schedule([this]() {
            if (home_active_ && !screen_off_) {
                ShowGrid();
            }
            swipe_pending_ = false;
        });
    }
}

void AppManager::OnSwipeRight() {
    if (screen_off_) return;
    if (!home_active_ && !swipe_pending_) {
        swipe_pending_ = true;
        pending_switch_ = false;
        pending_switch_id_.clear();
        Application::GetInstance().Schedule([this]() {
            if (!home_active_ && !screen_off_) {
                ShowHome();
            }
            swipe_pending_ = false;
        });
    }
}

void AppManager::OpenApp(int grid_index) {
    if (home_active_ || screen_off_) return;
    if (grid_index < 0 || grid_index >= (int)apps_.size()) return;

    const auto& id_str = apps_[grid_index].first;
    SwitchToApp(id_str.c_str());
}

void AppManager::SwitchToApp(const char* app_id) {
    pending_switch_id_ = app_id;
    pending_switch_ = true;

    auto& app = Application::GetInstance();
    app.Schedule([]() {
        AppManager::GetInstance().ProcessPendingSwitch();
    });
}

void AppManager::HandleBootClick() {
    if (screen_off_) {
        ExitBlackScreen();
        return;
    }

    if (IsInApp()) {
        // Exit current app, return to home
        ESP_LOGI(TAG, "Boot click: exiting app '%s'", current_app_id_.c_str());
        SwitchToApp("");  // Empty = go home
    } else {
        // On home/grid: enter black screen
        EnterBlackScreen();
    }
}

bool AppManager::HasPendingSwitch() const {
    return pending_switch_;
}

void AppManager::ProcessPendingSwitch() {
    if (!pending_switch_) return;
    pending_switch_ = false;

    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);

    // Exit current app if any
    if (current_app_ && app_screen_) {
        current_app_->OnExit();

        // Delete app screen
        lv_obj_del(app_screen_);
        app_screen_ = nullptr;
        current_app_ = nullptr;
        current_app_id_.clear();
    }

    // Enter target app or go home
    if (pending_switch_id_.empty()) {
        // Go to home screen
        ShowHome();
        return;
    }

    // Find the app
    App* target_app = nullptr;
    for (auto& [id, app_ptr] : apps_) {
        if (id == pending_switch_id_) {
            target_app = app_ptr;
            break;
        }
    }

    if (!target_app) {
        ESP_LOGW(TAG, "App not found: %s", pending_switch_id_.c_str());
        ShowHome();
        return;
    }

    // Create a new screen for the app
    app_screen_ = lv_obj_create(nullptr);
    lv_obj_set_size(app_screen_, display->width(), display->height());
    lv_obj_set_style_bg_color(app_screen_, lv_color_black(), 0);
    lv_obj_set_style_border_width(app_screen_, 0, 0);
    lv_obj_set_style_pad_all(app_screen_, 0, 0);

    // Load and enter the app
    lv_screen_load(app_screen_);
    target_app->OnEnter(app_context_, app_screen_);

    current_app_ = target_app;
    current_app_id_ = pending_switch_id_;
    home_active_ = false;
    pending_switch_id_.clear();

    ESP_LOGI(TAG, "Entered app: %s", current_app_id_.c_str());
}

void AppManager::OnClockTick() {
    // Don't update if screen is off
    if (screen_off_) return;

    auto* display = Board::GetInstance().GetDisplay();

    // Update home screen time
    if (home_active_) {
        time_t now = time(nullptr);
        struct tm* tm = localtime(&now);
        if (tm && tm->tm_year >= 2025 - 1900) {
            DisplayLockGuard lock(display);
            if (home_time_label_) {
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", tm);
                lv_label_set_text(home_time_label_, time_str);
            }
            if (home_date_label_) {
                char date_str[32];
                strftime(date_str, sizeof(date_str), "%Y年%m月%d日", tm);
                lv_label_set_text(home_date_label_, date_str);
            }
        }

        // Update status bar every 10 clock ticks
        static int home_tick = 0;
        if (home_tick++ % 10 == 0) {
            DisplayLockGuard lock(display);
            UpdateStatusBarIcons(home_wifi_label_, home_battery_label_, home_mute_label_);
            if (home_tick >= 1000) home_tick = 0;
        }
    } else if (!home_active_ && !IsInApp()) {
        // On grid screen
        static int grid_tick = 0;
        if (grid_tick++ % 10 == 0) {
            DisplayLockGuard lock(display);
            UpdateStatusBarIcons(grid_wifi_label_, grid_battery_label_, grid_mute_label_);
            if (grid_tick >= 1000) grid_tick = 0;
        }
    }

    // Update active app
    if (current_app_) {
        current_app_->OnUpdate();
    }
}
