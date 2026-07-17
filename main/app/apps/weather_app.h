#pragma once

#include "../app.h"

#include <font_awesome.h>

#include <atomic>
#include <cstdint>
#include <mutex>

class WeatherApp : public App {
public:
    const char* GetName() const override { return "Weather"; }
    const char* GetIcon() const override { return FONT_AWESOME_CLOUD_SUN; }

    void OnEnter(AppContext& ctx, lv_obj_t* screen) override;
    void OnExit() override;
    bool OnUpdate() override;

    // Starts a background refresh. Fresh cached data is kept unless force is true.
    // This is called as soon as the device gets network access, before the app opens.
    void Prefetch(bool force = false);

private:
    enum FetchState : int {
        kFetchError = -1,
        kFetchIdle = 0,
        kFetchLoading = 1,
        kFetchReady = 2,
    };

    struct ForecastDay {
        char date[16] = {};
        char weather[64] = {};
        char high[16] = {};
        char low[16] = {};
    };

    struct WeatherData {
        char location[64] = {};
        char weather[64] = {};
        char high[16] = {};
        char low[16] = {};
        char wind[64] = {};
        char report_time[32] = {};
        ForecastDay forecast[3] = {};
    };

    static void FetchTask(void* arg);
    bool FetchWeather(WeatherData& result);
    void BuildUI();

    Display* display_ = nullptr;
    lv_obj_t* screen_ = nullptr;

    std::mutex data_mutex_;
    WeatherData data_ = {};
    std::atomic<bool> has_data_{false};
    std::atomic<bool> fetch_running_{false};
    std::atomic<int> fetch_state_{kFetchIdle};
    std::atomic<uint32_t> data_version_{0};
    std::atomic<int64_t> last_success_us_{0};

    int rendered_state_ = 100;
    uint32_t rendered_version_ = UINT32_MAX;
};
