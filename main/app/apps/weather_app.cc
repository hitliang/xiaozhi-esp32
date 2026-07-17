#include "weather_app.h"

#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <new>

#define TAG "WeatherApp"
#define CITY_CODE "110105"
#define AMAP_API_KEY "0a12710ff3b0533bcefa95534c9e4eda"

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_48);

namespace {

constexpr int64_t kCacheFreshUs = 30LL * 60LL * 1000LL * 1000LL;
constexpr int kResponseBufferSize = 6144;

struct HttpContext {
    char* buffer = nullptr;
    int length = 0;
    int capacity = 0;
    bool overflow = false;
};

esp_err_t HttpEventHandler(esp_http_client_event_t* event) {
    auto* context = static_cast<HttpContext*>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    const int remaining = context->capacity - context->length - 1;
    if (event->data_len > remaining) {
        context->overflow = true;
        return ESP_OK;
    }

    memcpy(context->buffer + context->length, event->data, event->data_len);
    context->length += event->data_len;
    context->buffer[context->length] = '\0';
    return ESP_OK;
}

bool HttpGet(const char* url, char* buffer, int capacity) {
    if (!url || !buffer || capacity < 2) {
        return false;
    }

    buffer[0] = '\0';
    HttpContext context = {
        .buffer = buffer,
        .length = 0,
        .capacity = capacity,
        .overflow = false,
    };

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 12000;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = HttpEventHandler;
    config.user_data = &context;
    config.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }

    const esp_err_t error = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (context.overflow) {
        ESP_LOGW(TAG, "Weather response exceeded %d bytes", capacity);
    }
    return error == ESP_OK && status == 200 && context.length > 0 && !context.overflow;
}

const char* JsonString(cJSON* object, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : nullptr;
}

void CopyText(char* destination, size_t capacity, const char* source, const char* fallback = "--") {
    snprintf(destination, capacity, "%s", source && source[0] ? source : fallback);
}

bool Contains(const char* text, const char* token) {
    return text && token && strstr(text, token) != nullptr;
}

const char* WeatherIcon(const char* weather) {
    if (Contains(weather, "雷")) return FONT_AWESOME_CLOUD_BOLT;
    if (Contains(weather, "雪") || Contains(weather, "冰")) return FONT_AWESOME_SNOWFLAKE;
    if (Contains(weather, "雨")) return FONT_AWESOME_CLOUD_RAIN;
    if (Contains(weather, "雾") || Contains(weather, "霾")) return FONT_AWESOME_SMOG;
    if (Contains(weather, "沙") || Contains(weather, "尘") || Contains(weather, "风")) return FONT_AWESOME_WIND;
    if (Contains(weather, "晴")) return FONT_AWESOME_SUN;
    if (Contains(weather, "多云")) return FONT_AWESOME_CLOUD_SUN;
    return FONT_AWESOME_CLOUD;
}

uint32_t WeatherAccent(const char* weather) {
    if (Contains(weather, "雷")) return 0xC59CFF;
    if (Contains(weather, "雪") || Contains(weather, "冰")) return 0xB8F2FF;
    if (Contains(weather, "雨")) return 0x6EB6FF;
    if (Contains(weather, "雾") || Contains(weather, "霾")) return 0xA7B0C0;
    if (Contains(weather, "沙") || Contains(weather, "尘") || Contains(weather, "风")) return 0xFFB36B;
    if (Contains(weather, "晴")) return 0xFFD166;
    return 0x91C9FF;
}

lv_obj_t* CreatePanel(lv_obj_t* parent, int x, int y, int width, int height,
                      uint32_t color, int radius) {
    lv_obj_t* panel = lv_obj_create(parent);
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

void SetLabel(lv_obj_t* label, const char* text, const lv_font_t* font, uint32_t color) {
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void ShortDate(const char* date, char* output, size_t output_size) {
    if (date && strlen(date) >= 10) {
        snprintf(output, output_size, "%c%c/%c%c", date[5], date[6], date[8], date[9]);
    } else {
        CopyText(output, output_size, date);
    }
}

void UpdateTime(const char* report_time, char* output, size_t output_size) {
    if (report_time && strlen(report_time) >= 16) {
        snprintf(output, output_size, "Updated %c%c:%c%c",
                 report_time[11], report_time[12], report_time[14], report_time[15]);
    } else {
        snprintf(output, output_size, "Weather cache");
    }
}

}  // namespace

void WeatherApp::Prefetch(bool force) {
    const int64_t now = esp_timer_get_time();
    const int64_t last_success = last_success_us_.load(std::memory_order_acquire);
    if (!force && has_data_.load(std::memory_order_acquire) &&
        last_success > 0 && now - last_success < kCacheFreshUs) {
        return;
    }

    bool expected = false;
    if (!fetch_running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    fetch_state_.store(kFetchLoading, std::memory_order_release);
    if (xTaskCreate(FetchTask, "weather_fetch", 7168, this, 3, nullptr) != pdPASS) {
        fetch_state_.store(kFetchError, std::memory_order_release);
        fetch_running_.store(false, std::memory_order_release);
        ESP_LOGE(TAG, "Unable to create weather fetch task");
        return;
    }

    ESP_LOGI(TAG, "%s weather refresh started", force ? "Forced" : "Background");
}

void WeatherApp::FetchTask(void* arg) {
    auto* app = static_cast<WeatherApp*>(arg);
    WeatherData result = {};

    if (app->FetchWeather(result)) {
        {
            std::lock_guard<std::mutex> lock(app->data_mutex_);
            app->data_ = result;
        }
        app->last_success_us_.store(esp_timer_get_time(), std::memory_order_release);
        app->has_data_.store(true, std::memory_order_release);
        app->data_version_.fetch_add(1, std::memory_order_acq_rel);
        app->fetch_state_.store(kFetchReady, std::memory_order_release);
        ESP_LOGI(TAG, "Weather cache updated: %s, %s", result.location, result.weather);
    } else {
        app->fetch_state_.store(kFetchError, std::memory_order_release);
        ESP_LOGW(TAG, "Weather refresh failed; cached data retained");
    }

    app->fetch_running_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

bool WeatherApp::FetchWeather(WeatherData& result) {
    char url[256];
    snprintf(url, sizeof(url),
             "https://restapi.amap.com/v3/weather/weatherInfo?"
             "city=%s&key=%s&extensions=all",
             CITY_CODE, AMAP_API_KEY);

    char* response = new (std::nothrow) char[kResponseBufferSize];
    if (!response) {
        ESP_LOGE(TAG, "No memory for weather response");
        return false;
    }

    const bool request_ok = HttpGet(url, response, kResponseBufferSize);
    if (!request_ok) {
        delete[] response;
        return false;
    }

    cJSON* root = cJSON_Parse(response);
    delete[] response;
    if (!root) {
        ESP_LOGW(TAG, "Invalid weather JSON");
        return false;
    }

    bool ok = false;
    do {
        const char* api_status = JsonString(root, "status");
        if (!api_status || strcmp(api_status, "1") != 0) {
            ESP_LOGW(TAG, "Weather API error: %s (%s)",
                     JsonString(root, "info") ? JsonString(root, "info") : "unknown",
                     JsonString(root, "infocode") ? JsonString(root, "infocode") : "-");
            break;
        }

        cJSON* forecasts = cJSON_GetObjectItemCaseSensitive(root, "forecasts");
        cJSON* forecast = cJSON_IsArray(forecasts) ? cJSON_GetArrayItem(forecasts, 0) : nullptr;
        cJSON* casts = forecast ? cJSON_GetObjectItemCaseSensitive(forecast, "casts") : nullptr;
        if (!forecast || !cJSON_IsArray(casts) || cJSON_GetArraySize(casts) < 1) {
            ESP_LOGW(TAG, "Weather forecast payload is incomplete");
            break;
        }

        const char* province = JsonString(forecast, "province");
        const char* city = JsonString(forecast, "city");
        if (province && city && strcmp(province, city) != 0) {
            snprintf(result.location, sizeof(result.location), "%s · %s", province, city);
        } else {
            CopyText(result.location, sizeof(result.location), city ? city : province, "北京 · 朝阳");
        }
        CopyText(result.report_time, sizeof(result.report_time), JsonString(forecast, "reporttime"), "");

        cJSON* today = cJSON_GetArrayItem(casts, 0);
        const char* weather = JsonString(today, "dayweather");
        const char* high = JsonString(today, "daytemp");
        const char* low = JsonString(today, "nighttemp");
        if (!weather || !high || !low) {
            ESP_LOGW(TAG, "Today's weather data is incomplete");
            break;
        }

        CopyText(result.weather, sizeof(result.weather), weather);
        CopyText(result.high, sizeof(result.high), high);
        CopyText(result.low, sizeof(result.low), low);

        const char* wind = JsonString(today, "daywind");
        const char* power = JsonString(today, "daypower");
        if (wind && strstr(wind, "风")) {
            snprintf(result.wind, sizeof(result.wind), "%s  %s级", wind, power ? power : "--");
        } else {
            snprintf(result.wind, sizeof(result.wind), "%s风  %s级",
                     wind ? wind : "--", power ? power : "--");
        }

        const int cast_count = cJSON_GetArraySize(casts);
        for (int i = 0; i < 3; ++i) {
            if (i + 1 >= cast_count) {
                CopyText(result.forecast[i].date, sizeof(result.forecast[i].date), nullptr);
                CopyText(result.forecast[i].weather, sizeof(result.forecast[i].weather), nullptr);
                CopyText(result.forecast[i].high, sizeof(result.forecast[i].high), nullptr);
                CopyText(result.forecast[i].low, sizeof(result.forecast[i].low), nullptr);
                continue;
            }

            cJSON* day = cJSON_GetArrayItem(casts, i + 1);
            CopyText(result.forecast[i].date, sizeof(result.forecast[i].date), JsonString(day, "date"));
            CopyText(result.forecast[i].weather, sizeof(result.forecast[i].weather), JsonString(day, "dayweather"));
            CopyText(result.forecast[i].high, sizeof(result.forecast[i].high), JsonString(day, "daytemp"));
            CopyText(result.forecast[i].low, sizeof(result.forecast[i].low), JsonString(day, "nighttemp"));
        }

        ok = true;
    } while (false);

    cJSON_Delete(root);
    return ok;
}

void WeatherApp::OnEnter(AppContext& context, lv_obj_t* screen) {
    display_ = context.display;
    screen_ = screen;
    rendered_state_ = 100;
    rendered_version_ = UINT32_MAX;

    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);

    Prefetch(false);
    BuildUI();
    ESP_LOGI(TAG, "Weather entered (cached=%d)", has_data_.load() ? 1 : 0);
}

void WeatherApp::OnExit() {
    screen_ = nullptr;
    display_ = nullptr;
    ESP_LOGI(TAG, "Weather exited; cache preserved");
}

bool WeatherApp::OnUpdate() {
    if (!screen_ || !display_) {
        return false;
    }

    const int state = fetch_state_.load(std::memory_order_acquire);
    const uint32_t version = data_version_.load(std::memory_order_acquire);
    if (state == rendered_state_ && version == rendered_version_) {
        return false;
    }

    DisplayLockGuard lock(display_);
    if (screen_) {
        BuildUI();
    }
    return true;
}

void WeatherApp::BuildUI() {
    if (!screen_) {
        return;
    }

    lv_obj_clean(screen_);
    auto* theme = static_cast<LvglTheme*>(display_->GetTheme());
    const lv_font_t* text_font = theme->text_font()->font();
    const lv_font_t* icon_font = theme->icon_font()->font();
    const int state = fetch_state_.load(std::memory_order_acquire);
    const uint32_t version = data_version_.load(std::memory_order_acquire);
    const bool has_data = has_data_.load(std::memory_order_acquire);

    WeatherData snapshot = {};
    if (has_data) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        snapshot = data_;
    }

    const uint32_t accent = has_data ? WeatherAccent(snapshot.weather) : 0x6EB6FF;

    // Header: cached location is shown immediately; refresh remains available at all times.
    lv_obj_t* location_icon = lv_label_create(screen_);
    SetLabel(location_icon, FONT_AWESOME_LOCATION_DOT, icon_font, accent);
    lv_obj_set_pos(location_icon, 16, 11);

    lv_obj_t* location = lv_label_create(screen_);
    SetLabel(location, has_data ? snapshot.location : "北京 · 朝阳", text_font, 0xF4F7FB);
    lv_obj_set_pos(location, 50, 6);
    lv_obj_set_size(location, 250, 40);
    lv_label_set_long_mode(location, LV_LABEL_LONG_DOT);

    lv_obj_t* refresh_button = lv_button_create(screen_);
    lv_obj_set_pos(refresh_button, 310, 5);
    lv_obj_set_size(refresh_button, 43, 43);
    lv_obj_set_style_radius(refresh_button, 22, 0);
    lv_obj_set_style_bg_color(refresh_button, lv_color_hex(0x151F30), 0);
    lv_obj_set_style_bg_color(refresh_button, lv_color_hex(0x23324B), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(refresh_button, 1, 0);
    lv_obj_set_style_border_color(refresh_button, lv_color_hex(0x30425F), 0);
    lv_obj_set_style_shadow_width(refresh_button, 0, 0);
    lv_obj_set_style_pad_all(refresh_button, 0, 0);
    lv_obj_add_event_cb(refresh_button, [](lv_event_t* event) {
        auto* app = static_cast<WeatherApp*>(lv_event_get_user_data(event));
        app->Prefetch(true);
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* refresh_icon = lv_label_create(refresh_button);
    SetLabel(refresh_icon, FONT_AWESOME_ARROWS_ROTATE, icon_font,
             state == kFetchLoading ? accent : 0xDCE8F7);
    lv_obj_center(refresh_icon);

    if (!has_data) {
        lv_obj_t* panel = CreatePanel(screen_, 14, 61, 340, 345, 0x0C1524, 28);
        lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x07101D), 0);
        lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_border_color(panel, lv_color_hex(0x172A45), 0);

        if (state == kFetchLoading || state == kFetchIdle) {
            lv_obj_t* spinner = lv_arc_create(panel);
            lv_obj_set_size(spinner, 58, 58);
            lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -40);
            lv_arc_set_bg_angles(spinner, 0, 360);
            lv_arc_set_angles(spinner, 0, 95);
            lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
            lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(spinner, lv_color_hex(0x21334E), LV_PART_MAIN);
            lv_obj_set_style_arc_color(spinner, lv_color_hex(accent), LV_PART_INDICATOR);
            lv_obj_remove_style(spinner, nullptr, LV_PART_KNOB);
            lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);

            lv_anim_t spinner_animation;
            lv_anim_init(&spinner_animation);
            lv_anim_set_var(&spinner_animation, spinner);
            lv_anim_set_values(&spinner_animation, 0, 360);
            lv_anim_set_duration(&spinner_animation, 850);
            lv_anim_set_repeat_count(&spinner_animation, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_exec_cb(&spinner_animation, [](void* object, int32_t value) {
                lv_arc_set_rotation(static_cast<lv_obj_t*>(object), value);
            });
            lv_anim_start(&spinner_animation);

            lv_obj_t* message = lv_label_create(panel);
            SetLabel(message, "正在准备天气", text_font, 0xF4F7FB);
            lv_obj_align(message, LV_ALIGN_CENTER, 0, 36);

            lv_obj_t* hint = lv_label_create(panel);
            SetLabel(hint, "Loading forecast...", &lv_font_montserrat_14, 0x71839B);
            lv_obj_align(hint, LV_ALIGN_CENTER, 0, 78);
        } else {
            lv_obj_t* error_icon = lv_label_create(panel);
            SetLabel(error_icon, FONT_AWESOME_CLOUD_SLASH, icon_font, 0x8292A8);
            lv_obj_align(error_icon, LV_ALIGN_CENTER, 0, -62);
            lv_obj_update_layout(error_icon);
            lv_obj_set_style_transform_pivot_x(error_icon, lv_obj_get_width(error_icon) / 2, 0);
            lv_obj_set_style_transform_pivot_y(error_icon, lv_obj_get_height(error_icon) / 2, 0);
            lv_obj_set_style_transform_scale(error_icon, 460, 0);

            lv_obj_t* message = lv_label_create(panel);
            SetLabel(message, "暂时无法获取天气", text_font, 0xF4F7FB);
            lv_obj_align(message, LV_ALIGN_CENTER, 0, 30);

            lv_obj_t* retry = lv_button_create(panel);
            lv_obj_set_size(retry, 130, 50);
            lv_obj_align(retry, LV_ALIGN_CENTER, 0, 94);
            lv_obj_set_style_radius(retry, 25, 0);
            lv_obj_set_style_bg_color(retry, lv_color_hex(0x2077D4), 0);
            lv_obj_set_style_bg_color(retry, lv_color_hex(0x185FA9), LV_STATE_PRESSED);
            lv_obj_set_style_shadow_width(retry, 0, 0);
            lv_obj_add_event_cb(retry, [](lv_event_t* event) {
                auto* app = static_cast<WeatherApp*>(lv_event_get_user_data(event));
                app->Prefetch(true);
            }, LV_EVENT_CLICKED, this);
            lv_obj_t* retry_text = lv_label_create(retry);
            SetLabel(retry_text, "重试", text_font, 0xFFFFFF);
            lv_obj_center(retry_text);
        }

        rendered_state_ = state;
        rendered_version_ = version;
        return;
    }

    // Main weather card.
    lv_obj_t* hero = CreatePanel(screen_, 14, 58, 340, 180, 0x102641, 28);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(accent), 0);
    lv_obj_set_style_border_opa(hero, LV_OPA_30, 0);

    lv_obj_t* weather_icon = lv_label_create(hero);
    SetLabel(weather_icon, WeatherIcon(snapshot.weather), icon_font, accent);
    lv_obj_set_pos(weather_icon, 56, 35);
    lv_obj_update_layout(weather_icon);
    lv_obj_set_style_transform_pivot_x(weather_icon, lv_obj_get_width(weather_icon) / 2, 0);
    lv_obj_set_style_transform_pivot_y(weather_icon, lv_obj_get_height(weather_icon) / 2, 0);
    lv_obj_set_style_transform_scale(weather_icon, 500, 0);

    lv_obj_t* condition = lv_label_create(hero);
    SetLabel(condition, snapshot.weather, text_font, 0xF4F7FB);
    lv_obj_set_pos(condition, 20, 91);
    lv_obj_set_size(condition, 145, 38);
    lv_obj_set_style_text_align(condition, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(condition, LV_LABEL_LONG_DOT);

    char temperature[32];
    snprintf(temperature, sizeof(temperature), "%s°", snapshot.high);
    lv_obj_t* temperature_label = lv_label_create(hero);
    SetLabel(temperature_label, temperature, &lv_font_montserrat_48, 0xFFFFFF);
    lv_obj_set_pos(temperature_label, 187, 22);
    lv_obj_set_size(temperature_label, 135, 60);
    lv_obj_set_style_text_align(temperature_label, LV_TEXT_ALIGN_RIGHT, 0);

    char high_low[48];
    snprintf(high_low, sizeof(high_low), "H %s°   /   L %s°", snapshot.high, snapshot.low);
    lv_obj_t* high_low_label = lv_label_create(hero);
    SetLabel(high_low_label, high_low, &lv_font_montserrat_14, 0xB8C7D9);
    lv_obj_set_pos(high_low_label, 174, 84);
    lv_obj_set_size(high_low_label, 148, 22);
    lv_obj_set_style_text_align(high_low_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* wind_panel = CreatePanel(hero, 18, 128, 304, 38, 0x152C47, 19);
    lv_obj_set_style_bg_opa(wind_panel, LV_OPA_70, 0);
    lv_obj_t* wind_icon = lv_label_create(wind_panel);
    SetLabel(wind_icon, FONT_AWESOME_WIND, icon_font, accent);
    lv_obj_set_pos(wind_icon, 13, 4);
    lv_obj_t* wind_label = lv_label_create(wind_panel);
    SetLabel(wind_label, snapshot.wind, text_font, 0xE5EDF7);
    lv_obj_set_pos(wind_label, 56, 1);
    lv_obj_set_size(wind_label, 230, 36);
    lv_label_set_long_mode(wind_label, LV_LABEL_LONG_DOT);

    lv_obj_t* forecast_title = lv_label_create(screen_);
    SetLabel(forecast_title, "3-DAY FORECAST", &lv_font_montserrat_14, 0x71839B);
    lv_obj_set_pos(forecast_title, 18, 249);
    lv_obj_set_style_text_letter_space(forecast_title, 1, 0);

    const int row_y[3] = {276, 324, 372};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* row = CreatePanel(screen_, 14, row_y[i], 340, 43,
                                    i == 1 ? 0x0D1622 : 0x101A28, 16);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x1B2A3E), 0);

        char date[16];
        ShortDate(snapshot.forecast[i].date, date, sizeof(date));
        lv_obj_t* date_label = lv_label_create(row);
        SetLabel(date_label, date, &lv_font_montserrat_14, 0x8FA0B6);
        lv_obj_set_pos(date_label, 13, 13);
        lv_obj_set_size(date_label, 55, 18);

        const uint32_t day_accent = WeatherAccent(snapshot.forecast[i].weather);
        lv_obj_t* day_icon = lv_label_create(row);
        SetLabel(day_icon, WeatherIcon(snapshot.forecast[i].weather), icon_font, day_accent);
        lv_obj_set_pos(day_icon, 77, 5);

        lv_obj_t* day_weather = lv_label_create(row);
        SetLabel(day_weather, snapshot.forecast[i].weather, text_font, 0xEAF0F8);
        lv_obj_set_pos(day_weather, 118, 1);
        lv_obj_set_size(day_weather, 122, 38);
        lv_label_set_long_mode(day_weather, LV_LABEL_LONG_DOT);

        char range[40];
        snprintf(range, sizeof(range), "%s° / %s°",
                 snapshot.forecast[i].high, snapshot.forecast[i].low);
        lv_obj_t* range_label = lv_label_create(row);
        SetLabel(range_label, range, &lv_font_montserrat_14, 0xD7E2EF);
        lv_obj_set_pos(range_label, 244, 13);
        lv_obj_set_size(range_label, 82, 18);
        lv_obj_set_style_text_align(range_label, LV_TEXT_ALIGN_RIGHT, 0);
    }

    uint32_t status_color = 0x54D18B;
    const char* status_text = nullptr;
    char update_text[32];
    if (state == kFetchLoading) {
        status_color = accent;
        status_text = "Refreshing...";
    } else if (state == kFetchError) {
        status_color = 0xFF8A72;
        status_text = "Update failed - cached data";
    } else {
        UpdateTime(snapshot.report_time, update_text, sizeof(update_text));
        status_text = update_text;
    }

    lv_obj_t* status_dot = lv_obj_create(screen_);
    lv_obj_set_pos(status_dot, 18, 430);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_style_radius(status_dot, 4, 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(status_color), 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* status_label = lv_label_create(screen_);
    SetLabel(status_label, status_text, &lv_font_montserrat_14, 0x71839B);
    lv_obj_set_pos(status_label, 34, 424);
    lv_obj_set_size(status_label, 315, 20);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);

    rendered_state_ = state;
    rendered_version_ = version;
}
