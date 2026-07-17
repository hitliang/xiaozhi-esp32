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

LV_FONT_DECLARE(font_noto_60_4);

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

static lv_obj_t* CreateLauncherPanel(lv_obj_t* parent, int x, int y, int width, int height,
                                     uint32_t color, int radius) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    return panel;
}

static void ConfigureLauncherScreen(lv_obj_t* screen, int width, int height) {
    lv_obj_set_size(screen, width, height);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
}

static void BuildLauncherStatusBar(lv_obj_t* screen, const lv_font_t* icon_font,
                                   lv_obj_t*& wifi_label, lv_obj_t*& mute_label,
                                   lv_obj_t*& battery_label, lv_obj_t*& battery_percent_label) {
    auto* bar = CreateLauncherPanel(screen, 12, 8, 344, 44, 0x0A111C, 22);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x172538), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0x070C13), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);

    wifi_label = lv_label_create(bar);
    lv_label_set_text(wifi_label, "");
    lv_obj_set_style_text_font(wifi_label, icon_font, 0);
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x77C7FF), 0);
    lv_obj_set_pos(wifi_label, 14, 7);

    mute_label = lv_label_create(bar);
    lv_label_set_text(mute_label, "");
    lv_obj_set_style_text_font(mute_label, icon_font, 0);
    lv_obj_set_style_text_color(mute_label, lv_color_hex(0xFF8A7A), 0);
    lv_obj_set_pos(mute_label, 214, 7);

    auto* battery_pill = CreateLauncherPanel(bar, 252, 5, 84, 34, 0x111B29, 17);
    lv_obj_set_style_border_width(battery_pill, 1, 0);
    lv_obj_set_style_border_color(battery_pill, lv_color_hex(0x203149), 0);

    battery_label = lv_label_create(battery_pill);
    lv_label_set_text(battery_label, FONT_AWESOME_BATTERY_FULL);
    lv_obj_set_style_text_font(battery_label, icon_font, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0xDCE7F5), 0);
    lv_obj_set_pos(battery_label, 7, 2);

    battery_percent_label = lv_label_create(battery_pill);
    lv_label_set_text(battery_percent_label, "--");
    lv_obj_set_style_text_font(battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(battery_percent_label, lv_color_hex(0xB8C6D8), 0);
    lv_obj_set_pos(battery_percent_label, 40, 9);
    lv_obj_set_size(battery_percent_label, 39, 18);
    lv_obj_set_style_text_align(battery_percent_label, LV_TEXT_ALIGN_CENTER, 0);
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

    home_screen_ = lv_obj_create(nullptr);
    ConfigureLauncherScreen(home_screen_, display->width(), display->height());

    auto* glow = CreateLauncherPanel(home_screen_, 226, -72, 220, 220, 0x143A68, 110);
    lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

    BuildLauncherStatusBar(home_screen_, icon_font,
                           home_wifi_label_, home_mute_label_,
                           home_battery_label_, home_battery_percent_label_);

    auto* time_card = CreateLauncherPanel(home_screen_, 16, 76, 336, 278, 0x101C31, 34);
    lv_obj_set_style_bg_grad_color(time_card, lv_color_hex(0x05090F), 0);
    lv_obj_set_style_bg_grad_dir(time_card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(time_card, 1, 0);
    lv_obj_set_style_border_color(time_card, lv_color_hex(0x213451), 0);

    auto* accent = CreateLauncherPanel(time_card, 24, 26, 34, 5, 0x5DB8FF, 3);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

    auto* overline = lv_label_create(time_card);
    lv_label_set_text(overline, "LOCAL TIME");
    lv_obj_set_style_text_font(overline, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(overline, lv_color_hex(0x7E91AA), 0);
    lv_obj_set_style_text_letter_space(overline, 2, 0);
    lv_obj_set_pos(overline, 24, 42);

    home_time_label_ = lv_label_create(time_card);
    lv_label_set_text(home_time_label_, "00:00");
    lv_obj_set_style_text_font(home_time_label_, &font_noto_60_4, 0);
    lv_obj_set_style_text_color(home_time_label_, lv_color_hex(0xF6F9FF), 0);
    lv_obj_set_pos(home_time_label_, 18, 78);
    lv_obj_set_size(home_time_label_, 300, 74);
    lv_obj_set_style_text_align(home_time_label_, LV_TEXT_ALIGN_CENTER, 0);

    home_date_label_ = lv_label_create(time_card);
    lv_label_set_text(home_date_label_, "");
    lv_obj_set_style_text_font(home_date_label_, text_font, 0);
    lv_obj_set_style_text_color(home_date_label_, lv_color_hex(0xB8C7DA), 0);
    lv_obj_set_pos(home_date_label_, 18, 154);
    lv_obj_set_size(home_date_label_, 300, 42);
    lv_obj_set_style_text_align(home_date_label_, LV_TEXT_ALIGN_CENTER, 0);

    auto* divider = CreateLauncherPanel(time_card, 116, 211, 104, 1, 0x253751, 1);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    auto* zone = lv_label_create(time_card);
    lv_label_set_text(zone, "BEIJING  /  CST");
    lv_obj_set_style_text_font(zone, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(zone, lv_color_hex(0x61758F), 0);
    lv_obj_set_pos(zone, 18, 228);
    lv_obj_set_size(zone, 300, 20);
    lv_obj_set_style_text_align(zone, LV_TEXT_ALIGN_CENTER, 0);

    auto* active_page = CreateLauncherPanel(home_screen_, 166, 385, 20, 6, 0x5DB8FF, 3);
    lv_obj_set_style_bg_opa(active_page, LV_OPA_COVER, 0);
    auto* menu_page = CreateLauncherPanel(home_screen_, 194, 385, 6, 6, 0x304159, 3);
    lv_obj_set_style_bg_opa(menu_page, LV_OPA_COVER, 0);

    auto* hint = lv_label_create(home_screen_);
    lv_label_set_text(hint, "SWIPE LEFT  /  APPS");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x586A82), 0);
    lv_obj_set_style_text_letter_space(hint, 1, 0);
    lv_obj_set_pos(hint, 34, 407);
    lv_obj_set_size(hint, 300, 20);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(home_screen_, [](lv_event_t* event) {
        auto* self = static_cast<AppManager*>(lv_event_get_user_data(event));
        auto* indev = lv_indev_active();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_LEFT) {
            lv_indev_wait_release(indev);
            self->OnSwipeLeft();
        }
    }, LV_EVENT_GESTURE, this);

    UpdateStatusBarIcons(home_wifi_label_, home_battery_label_,
                         home_battery_percent_label_, home_mute_label_);
}

void AppManager::BuildGridScreen() {
    auto* theme = GetLvglTheme();
    auto* display = Board::GetInstance().GetDisplay();
    auto* text_font = theme->text_font()->font();
    auto* icon_font = theme->icon_font()->font();
    DisplayLockGuard lock(display);

    grid_screen_ = lv_obj_create(nullptr);
    ConfigureLauncherScreen(grid_screen_, display->width(), display->height());

    auto* glow = CreateLauncherPanel(grid_screen_, -96, 296, 230, 230, 0x102C51, 115);
    lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

    BuildLauncherStatusBar(grid_screen_, icon_font,
                           grid_wifi_label_, grid_mute_label_,
                           grid_battery_label_, grid_battery_percent_label_);

    auto* title = lv_label_create(grid_screen_);
    lv_label_set_text(title, "Apps");
    lv_obj_set_style_text_font(title, text_font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF3F7FD), 0);
    lv_obj_set_pos(title, 16, 57);

    auto* subtitle = lv_label_create(grid_screen_);
    lv_label_set_text(subtitle, "YOUR APPLICATIONS");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x60758F), 0);
    lv_obj_set_style_text_letter_space(subtitle, 1, 0);
    lv_obj_set_pos(subtitle, 104, 70);

    static const uint32_t accent_colors[9] = {
        0x66C7FF, 0xA98BFF, 0x67D5B5,
        0xFFB66F, 0xFF7E9D, 0x78A7FF,
        0xE7CB72, 0x65D7EA, 0xB994FF,
    };
    static const uint32_t badge_colors[9] = {
        0x102A3E, 0x251C3D, 0x12352E,
        0x3A291B, 0x3A1D28, 0x182B4A,
        0x342E19, 0x12343B, 0x2B1E3E,
    };

    constexpr int tile_width = 104;
    constexpr int tile_height = 90;
    constexpr int x_gap = 10;
    constexpr int y_gap = 8;
    constexpr int start_x = 16;
    constexpr int start_y = 98;

    for (int index = 0; index < 9; ++index) {
        const int row = index / 3;
        const int column = index % 3;
        auto* tile = lv_obj_create(grid_screen_);
        lv_obj_set_pos(tile, start_x + column * (tile_width + x_gap),
                       start_y + row * (tile_height + y_gap));
        lv_obj_set_size(tile, tile_width, tile_height);
        lv_obj_set_style_radius(tile, 22, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x0D1623), 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x17263A), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x1C2B40), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(accent_colors[index]), LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_set_style_transform_scale(tile, 246, LV_STATE_PRESSED);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);

        auto* badge = CreateLauncherPanel(tile, 30, 8, 44, 44,
                                          badge_colors[index], 15);
        lv_obj_set_style_border_width(badge, 1, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(accent_colors[index]), 0);
        lv_obj_set_style_border_opa(badge, LV_OPA_30, 0);

        auto* icon = lv_label_create(badge);
        lv_label_set_text(icon, "");
        lv_obj_set_style_text_font(icon, icon_font, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(accent_colors[index]), 0);
        lv_obj_center(icon);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

        auto* name = lv_label_create(tile);
        lv_label_set_text(name, "");
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(0xD2DDEA), 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name, 4, 62);
        lv_obj_set_size(name, 96, 20);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(tile, [](lv_event_t* event) {
            auto* clicked_tile = lv_event_get_current_target_obj(event);
            auto* self = static_cast<AppManager*>(lv_event_get_user_data(event));
            for (int i = 0; i < 9; ++i) {
                if (self->grid_tiles_[i] == clicked_tile) {
                    self->OpenApp(i);
                    return;
                }
            }
        }, LV_EVENT_CLICKED, this);

        grid_tiles_[index] = tile;
        grid_icon_labels_[index] = icon;
        grid_name_labels_[index] = name;
    }

    auto* home_page = CreateLauncherPanel(grid_screen_, 168, 405, 6, 6, 0x304159, 3);
    lv_obj_set_style_bg_opa(home_page, LV_OPA_COVER, 0);
    auto* active_page = CreateLauncherPanel(grid_screen_, 182, 405, 20, 6, 0x5DB8FF, 3);
    lv_obj_set_style_bg_opa(active_page, LV_OPA_COVER, 0);

    auto* hint = lv_label_create(grid_screen_);
    lv_label_set_text(hint, "SWIPE RIGHT  /  HOME");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x586A82), 0);
    lv_obj_set_style_text_letter_space(hint, 1, 0);
    lv_obj_set_pos(hint, 34, 420);
    lv_obj_set_size(hint, 300, 20);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(grid_screen_, [](lv_event_t* event) {
        auto* self = static_cast<AppManager*>(lv_event_get_user_data(event));
        auto* indev = lv_indev_active();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
            lv_indev_wait_release(indev);
            self->OnSwipeRight();
        }
    }, LV_EVENT_GESTURE, this);

    UpdateStatusBarIcons(grid_wifi_label_, grid_battery_label_,
                         grid_battery_percent_label_, grid_mute_label_);
}

void AppManager::PopulateGridTiles() {
    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);

    int count = static_cast<int>(apps_.size());
    if (count > 9) count = 9;
    for (int index = 0; index < 9; ++index) {
        auto* tile = grid_tiles_[index];
        if (!tile) continue;

        if (index < count) {
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_HIDDEN);
            if (grid_icon_labels_[index]) {
                lv_label_set_text(grid_icon_labels_[index], apps_[index].second->GetIcon());
            }
            if (grid_name_labels_[index]) {
                lv_label_set_text(grid_name_labels_[index], apps_[index].second->GetName());
            }
        } else {
            lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
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

    // Wake from black screen only via BOOT button, not touch
}

void AppManager::UpdateStatusBarIcons(lv_obj_t* wifi_label, lv_obj_t* battery_label,
                                      lv_obj_t* battery_percent_label,
                                      lv_obj_t* mute_label) {
    auto& board = Board::GetInstance();

    if (wifi_label) {
        const char* wifi_icon = board.GetNetworkStateIcon();
        lv_label_set_text(wifi_label, wifi_icon ? wifi_icon : "");
        lv_obj_set_style_text_color(wifi_label,
                                    lv_color_hex(wifi_icon ? 0x77C7FF : 0x617084), 0);
    }

    if (battery_label && battery_percent_label) {
        int level = 0;
        bool charging = false;
        bool discharging = false;
        if (board.GetBatteryLevel(level, charging, discharging)) {
            if (level < 0) level = 0;
            if (level > 100) level = 100;

            const char* icon = nullptr;
            uint32_t color = 0xDCE7F5;
            if (charging) {
                icon = level >= 100 ? FONT_AWESOME_BATTERY_FULL : FONT_AWESOME_BATTERY_BOLT;
                color = 0x62E6A7;
            } else {
                static const char* levels[] = {
                    FONT_AWESOME_BATTERY_EMPTY,
                    FONT_AWESOME_BATTERY_QUARTER,
                    FONT_AWESOME_BATTERY_HALF,
                    FONT_AWESOME_BATTERY_THREE_QUARTERS,
                    FONT_AWESOME_BATTERY_FULL,
                    FONT_AWESOME_BATTERY_FULL,
                };
                icon = levels[level / 20];
                if (level <= 20) color = 0xFF7E72;
                else if (level <= 40) color = 0xFFC56B;
            }

            char percentage[8];
            snprintf(percentage, sizeof(percentage), "%d%%", level);
            lv_label_set_text(battery_label, icon);
            lv_label_set_text(battery_percent_label, percentage);
            lv_obj_set_style_text_color(battery_label, lv_color_hex(color), 0);
            lv_obj_set_style_text_color(battery_percent_label, lv_color_hex(color), 0);
        } else {
            lv_label_set_text(battery_label, FONT_AWESOME_BATTERY_SLASH);
            lv_label_set_text(battery_percent_label, "--");
            lv_obj_set_style_text_color(battery_label, lv_color_hex(0x66768B), 0);
            lv_obj_set_style_text_color(battery_percent_label, lv_color_hex(0x66768B), 0);
        }
    }

    if (mute_label) {
        auto* codec = board.GetAudioCodec();
        const bool muted = codec && codec->output_volume() == 0;
        lv_label_set_text(mute_label, muted ? FONT_AWESOME_VOLUME_XMARK : "");
    }
}

void AppManager::ShowHome(bool animated) {
    if (screen_off_) return;
    home_active_ = true;

    pending_switch_ = false;
    pending_switch_id_.clear();

    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    UpdateStatusBarIcons(home_wifi_label_, home_battery_label_,
                         home_battery_percent_label_, home_mute_label_);
    if (animated && lv_screen_active() != home_screen_) {
        lv_screen_load_anim(home_screen_, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
    } else {
        lv_screen_load(home_screen_);
    }
    ESP_LOGI(TAG, "Showing home screen%s", animated ? " (animated)" : "");
}

void AppManager::ShowGrid(bool animated) {
    if (screen_off_) return;
    home_active_ = false;

    pending_switch_ = false;
    pending_switch_id_.clear();

    auto* display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    UpdateStatusBarIcons(grid_wifi_label_, grid_battery_label_,
                         grid_battery_percent_label_, grid_mute_label_);
    if (animated && lv_screen_active() != grid_screen_) {
        lv_screen_load_anim(grid_screen_, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
    } else {
        lv_screen_load(grid_screen_);
    }
    ESP_LOGI(TAG, "Showing grid screen%s", animated ? " (animated)" : "");
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
                ShowGrid(true);
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
                ShowHome(true);
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

    if (!target_app->CanEnter()) {
        auto* board_display = Board::GetInstance().GetDisplay();
        board_display->ShowNotification("Daily limit reached, try again tomorrow", 3000);
        ESP_LOGW(TAG, "App '%s' blocked by CanEnter()", pending_switch_id_.c_str());
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
                const char* wdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
                snprintf(date_str, sizeof(date_str), "%d月%d日 %s", tm->tm_mon + 1, tm->tm_mday, wdays[tm->tm_wday]);
                lv_label_set_text(home_date_label_, date_str);
            }
        }

        // Refresh the status bar frequently so charging changes appear promptly.
        static int home_tick = 0;
        if (home_tick++ % 2 == 0) {
            DisplayLockGuard lock(display);
            UpdateStatusBarIcons(home_wifi_label_, home_battery_label_,
                                 home_battery_percent_label_, home_mute_label_);
            if (home_tick >= 1000) home_tick = 0;
        }
    } else if (!home_active_ && !IsInApp()) {
        // On grid screen
        static int grid_tick = 0;
        if (grid_tick++ % 2 == 0) {
            DisplayLockGuard lock(display);
            UpdateStatusBarIcons(grid_wifi_label_, grid_battery_label_,
                                 grid_battery_percent_label_, grid_mute_label_);
            if (grid_tick >= 1000) grid_tick = 0;
        }
    }

    // Update active app
    if (current_app_) {
        current_app_->OnUpdate();
    }
}
