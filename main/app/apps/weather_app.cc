#include "weather_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "boards/common/board.h"

#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>

#define TAG "WeatherApp"
#define CITY_CODE "110105"
#define AMAP_API_KEY "0a12710ff3b0533bcefa95534c9e4eda"

LV_FONT_DECLARE(lv_font_montserrat_48);

struct HttpCtx {
    char* buf;
    int len;
    int max;
    int status;
};

static esp_err_t httpHandler(esp_http_client_event_t* evt) {
    auto* ctx = (HttpCtx*)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx->len + evt->data_len < ctx->max) {
            memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
            ctx->len += evt->data_len;
            ctx->buf[ctx->len] = 0;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ctx->status = esp_http_client_get_status_code(evt->client);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static bool httpGet(const char* url, char* buf, int max) {
    HttpCtx ctx = { buf, 0, max, 0 };
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 15000;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.event_handler = httpHandler;
    cfg.user_data = &ctx;

    auto* cli = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);
    return err == ESP_OK && ctx.status == 200 && ctx.len > 0;
}

static void fetchWeather(void* arg) {
    auto* app = (WeatherApp*)arg;

    char url[256];
    snprintf(url, sizeof(url),
             "https://restapi.amap.com/v3/weather/weatherInfo?"
             "city=%s&key=%s&extensions=all",
             CITY_CODE, AMAP_API_KEY);

    // Allocate buffer on heap (too large for task stack)
    char* buf = new char[4096];
    if (!httpGet(url, buf, 4096)) {
        ESP_LOGW(TAG, "Weather API fetch failed");
        delete[] buf;
        app->fetchState_ = -1;
        vTaskDelete(NULL);
        return;
    }

    cJSON* j = cJSON_Parse(buf);
    delete[] buf;
    if (!j) { app->fetchState_ = -1; vTaskDelete(NULL); return; }

    cJSON* forecasts = cJSON_GetObjectItem(j, "forecasts");
    if (forecasts) {
        auto* fc = cJSON_GetArrayItem(forecasts, 0);
        if (fc) {
            cJSON* casts = cJSON_GetObjectItem(fc, "casts");
            if (casts && cJSON_GetArraySize(casts) >= 4) {
                auto* today = cJSON_GetArrayItem(casts, 0);
                auto* dw = cJSON_GetObjectItem(today, "dayweather");
                auto* dt = cJSON_GetObjectItem(today, "daytemp");
                auto* dwd = cJSON_GetObjectItem(today, "daywind");
                auto* dwp = cJSON_GetObjectItem(today, "daypower");

                snprintf(app->todayWeather_, sizeof(app->todayWeather_), "%s",
                         dw ? dw->valuestring : "--");
                snprintf(app->todayTemp_, sizeof(app->todayTemp_), "%s°",
                         dt ? dt->valuestring : "--");
                snprintf(app->todayWind_, sizeof(app->todayWind_), "%s %s级",
                         dwd ? dwd->valuestring : "--",
                         dwp ? dwp->valuestring : "--");

                for (int i = 0; i < 3; i++) {
                    auto* day = cJSON_GetArrayItem(casts, i + 1);
                    auto* d = cJSON_GetObjectItem(day, "date");
                    auto* w = cJSON_GetObjectItem(day, "dayweather");
                    auto* hi = cJSON_GetObjectItem(day, "daytemp");
                    auto* lo = cJSON_GetObjectItem(day, "nighttemp");
                    const char* ds = d ? d->valuestring : "?";
                    snprintf(app->forecast_[i], sizeof(app->forecast_[i]),
                             "%s %s %s°/%s°",
                             ds + 5,
                             w ? w->valuestring : "--",
                             hi ? hi->valuestring : "--",
                             lo ? lo->valuestring : "--");
                }
            }
        }
    }
    cJSON_Delete(j);
    app->fetchState_ = 1;
    ESP_LOGI(TAG, "Weather data loaded");
    vTaskDelete(NULL);
}

// ---- App lifecycle ----

void WeatherApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_color_t color = theme->text_color();
    auto* font = theme->text_font()->font();

    display_ = ctx.display;
    screen_ = screen;
    fetchState_ = 0;

    lv_obj_set_style_bg_color(screen, theme->background_color(), 0);
    lv_obj_set_style_pad_all(screen, 16, 0);

    // Loading indicator
    loadingLabel_ = lv_label_create(screen);
    lv_label_set_text(loadingLabel_, "加载中...");
    lv_obj_set_style_text_font(loadingLabel_, font, 0);
    lv_obj_set_style_text_color(loadingLabel_, color, 0);
    lv_obj_center(loadingLabel_);

    // Start fetch in background
    xTaskCreate(fetchWeather, "wx_fetch", 6144, this, 3, nullptr);

    // Poll timer for UI update
    pollTimer_ = lv_timer_create([](lv_timer_t* t) {
        auto* self = (WeatherApp*)lv_timer_get_user_data(t);
        if (self->fetchState_ == 0) return;
        lv_timer_del(t);
        self->pollTimer_ = nullptr;
        self->BuildUI();
    }, 500, this);

    ESP_LOGI(TAG, "Weather entered");
}

void WeatherApp::OnExit() {
    if (pollTimer_) lv_timer_del(pollTimer_);
    ESP_LOGI(TAG, "Weather exited");
}

void WeatherApp::BuildUI() {
    DisplayLockGuard lock(display_);
    if (loadingLabel_) { lv_obj_del(loadingLabel_); loadingLabel_ = nullptr; }
    lv_obj_clean(screen_);

    auto* theme = static_cast<LvglTheme*>(display_->GetTheme());
    lv_color_t color = theme->text_color();
    auto* font = theme->text_font()->font();

    if (fetchState_ < 0) {
        auto* l = lv_label_create(screen_);
        lv_label_set_text(l, "无法获取天气\n请检查网络");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, color, 0);
        lv_obj_center(l);
        return;
    }

    lv_obj_t* l;

    // City name
    l = lv_label_create(screen_);
    lv_label_set_text(l, "北京朝阳");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 8);

    // Temperature - big
    l = lv_label_create(screen_);
    lv_label_set_text(l, todayTemp_);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 45);

    // Weather description
    l = lv_label_create(screen_);
    lv_label_set_text(l, todayWeather_);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 105);

    // Wind
    l = lv_label_create(screen_);
    lv_label_set_text(l, todayWind_);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 133);

    // Divider
    auto* div = lv_obj_create(screen_);
    lv_obj_set_size(div, 300, 1);
    lv_obj_align(div, LV_ALIGN_TOP_MID, 0, 163);
    lv_obj_set_style_bg_color(div, color, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Forecast title
    l = lv_label_create(screen_);
    lv_label_set_text(l, "预报");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 173);

    // 3-day forecast
    for (int i = 0; i < 3; i++) {
        l = lv_label_create(screen_);
        lv_label_set_text(l, forecast_[i][0] ? forecast_[i] : "--");
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, color, 0);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 198 + i * 28);
    }
}
