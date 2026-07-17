#include "mp3_player_app.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "display/display.h"
#include "application.h"
#include "audio/audio_service.h"
#include "boards/common/board.h"
#include "assets/lang_config.h"
#include <font_awesome.h>

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_audio_simple_dec.h>
#include <esp_audio_simple_dec_reg.h>
#include <esp_audio_simple_dec_default.h>
#include <esp_audio_dec_default.h>
#include <decoder/impl/esp_mp3_dec.h>
#include <cJSON.h>
#include <cstring>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Mp3Player"

#define BASE_URL "http://59.110.161.101:7081"
#define HTTP_BUF_SIZE (8 * 1024)
#define DEC_OUT_SIZE  (16 * 1024)
#define PLAY_TASK_STACK (8 * 1024)

struct AudioFile {
    std::string name;
    std::string size_mb;
};

struct FolderInfo {
    std::string name;
    int count;
};

// --- Forward declarations ---
class Mp3PlayerView;

static bool g_decoders_registered = false;

static void ensure_decoders_registered() {
    if (g_decoders_registered) return;
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    // Register MP3 simple decoder explicitly
    esp_audio_simple_dec_reg_info_t mp3_reg = {};
    mp3_reg.decoder_ops = ESP_MP3_DEC_DEFAULT_OPS();
    mp3_reg.parser = nullptr;  // MP3 has built-in frame parser
    mp3_reg.free = nullptr;
    esp_audio_simple_dec_register(ESP_AUDIO_SIMPLE_DEC_TYPE_MP3, &mp3_reg);

    g_decoders_registered = true;
    ESP_LOGI(TAG, "MP3 decoder registered");
}

class Mp3PlayerView {
public:
    Mp3PlayerView(lv_obj_t* parent, Display* display, LvglTheme* theme)
        : display_(display), scr_w_(display->width()), scr_h_(display->height()) {
        auto* text_font = theme->text_font()->font();
        auto* icon_font = theme->icon_font()->font();
        auto fg = lv_color_white();

        lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

        // Top bar
        lv_obj_t* top_bar = lv_obj_create(parent);
        lv_obj_set_size(top_bar, scr_w_, 36);
        lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(top_bar, 0, 0);
        lv_obj_set_style_pad_all(top_bar, 8, 0);

        title_label_ = lv_label_create(top_bar);
        lv_obj_set_style_text_font(title_label_, text_font, 0);
        lv_obj_set_style_text_color(title_label_, fg, 0);
        lv_label_set_text(title_label_, "MP3 Player");
        lv_obj_align(title_label_, LV_ALIGN_CENTER, 0, 0);

        back_btn_ = lv_label_create(top_bar);
        lv_obj_set_style_text_font(back_btn_, icon_font, 0);
        lv_obj_set_style_text_color(back_btn_, fg, 0);
        lv_label_set_text(back_btn_, FONT_AWESOME_ARROW_LEFT);
        lv_obj_align(back_btn_, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(back_btn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(back_btn_, onBackTap, LV_EVENT_CLICKED, this);

        // Scrolling list
        list_ = lv_obj_create(parent);
        lv_obj_set_size(list_, scr_w_ - 16, scr_h_ - 130);
        lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_, 0, 0);
        lv_obj_set_style_pad_all(list_, 0, 0);
        lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(list_, LV_DIR_VER);

        // Now playing
        now_playing_ = lv_label_create(parent);
        lv_obj_set_style_text_font(now_playing_, text_font, 0);
        lv_obj_set_style_text_color(now_playing_, lv_color_hex(0xAAAAAA), 0);
        lv_label_set_text(now_playing_, "");
        lv_obj_set_width(now_playing_, scr_w_ - 16);
        lv_label_set_long_mode(now_playing_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(now_playing_, LV_ALIGN_BOTTOM_MID, 0, -50);

        // Control bar
        ctrl_bar_ = lv_obj_create(parent);
        lv_obj_set_size(ctrl_bar_, scr_w_, 44);
        lv_obj_align(ctrl_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(ctrl_bar_, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_width(ctrl_bar_, 0, 0);
        lv_obj_set_style_pad_all(ctrl_bar_, 0, 0);
        lv_obj_set_flex_flow(ctrl_bar_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ctrl_bar_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        play_btn_ = lv_label_create(ctrl_bar_);
        lv_obj_set_style_text_font(play_btn_, icon_font, 0);
        lv_obj_set_style_text_color(play_btn_, lv_color_hex(0x4CAF50), 0);
        lv_label_set_text(play_btn_, FONT_AWESOME_PLAY);
        lv_obj_add_event_cb(play_btn_, onPlayTap, LV_EVENT_CLICKED, this);

        stop_btn_ = lv_label_create(ctrl_bar_);
        lv_obj_set_style_text_font(stop_btn_, icon_font, 0);
        lv_obj_set_style_text_color(stop_btn_, lv_color_hex(0xF44336), 0);
        lv_label_set_text(stop_btn_, FONT_AWESOME_STOP);
        lv_obj_add_event_cb(stop_btn_, onStopTap, LV_EVENT_CLICKED, this);

        FetchFolders();
    }

    ~Mp3PlayerView() {
        StopPlayback();
    }

private:
    Display* display_;
    int scr_w_, scr_h_;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* back_btn_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* now_playing_ = nullptr;
    lv_obj_t* ctrl_bar_ = nullptr;
    lv_obj_t* play_btn_ = nullptr;
    lv_obj_t* stop_btn_ = nullptr;

    std::vector<FolderInfo> folders_;
    std::vector<AudioFile> files_;
    std::string current_folder_;
    bool browsing_folders_ = true;

    TaskHandle_t play_task_ = nullptr;
    std::atomic<bool> stop_requested_{false};
    std::string playing_file_;
    bool is_playing_ = false;

    static std::string HttpGet(const char* url) {
        std::string result;
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = 8000;
        cfg.buffer_size = 2048;
        esp_http_client_handle_t cli = esp_http_client_init(&cfg);

        esp_err_t err = esp_http_client_open(cli, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open failed");
            esp_http_client_cleanup(cli);
            return result;
        }
        int cl = esp_http_client_fetch_headers(cli);
        if (cl < 0) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            return result;
        }
        char buf[512];
        int n;
        while ((n = esp_http_client_read(cli, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            result += buf;
        }
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return result;
    }

    void FetchFolders() {
        StopPlayback();
        std::string json = HttpGet(BASE_URL "/api/folders");
        if (json.empty()) return;
        cJSON* root = cJSON_Parse(json.c_str());
        if (!root) return;
        cJSON* ok = cJSON_GetObjectItem(root, "ok");
        if (!cJSON_IsTrue(ok)) { cJSON_Delete(root); return; }
        cJSON* arr = cJSON_GetObjectItem(root, "folders");
        if (cJSON_IsArray(arr)) {
            folders_.clear();
            cJSON* it;
            cJSON_ArrayForEach(it, arr) {
                FolderInfo f;
                cJSON* nm = cJSON_GetObjectItem(it, "name");
                cJSON* cn = cJSON_GetObjectItem(it, "count");
                if (cJSON_IsString(nm)) f.name = nm->valuestring;
                if (cJSON_IsNumber(cn)) f.count = cn->valueint;
                folders_.push_back(f);
            }
        }
        cJSON_Delete(root);
        browsing_folders_ = true;
        ShowFolders();
    }

    void FetchFiles(const std::string& folder) {
        current_folder_ = folder;
        std::string url = BASE_URL "/api/list";
        if (!folder.empty()) url += "?folder=" + folder;
        std::string json = HttpGet(url.c_str());
        if (json.empty()) return;
        cJSON* root = cJSON_Parse(json.c_str());
        if (!root) return;
        cJSON* ok = cJSON_GetObjectItem(root, "ok");
        if (!cJSON_IsTrue(ok)) { cJSON_Delete(root); return; }
        cJSON* arr = cJSON_GetObjectItem(root, "files");
        if (cJSON_IsArray(arr)) {
            files_.clear();
            cJSON* it;
            cJSON_ArrayForEach(it, arr) {
                AudioFile f;
                cJSON* nm = cJSON_GetObjectItem(it, "name");
                cJSON* mb = cJSON_GetObjectItem(it, "size_mb");
                if (cJSON_IsString(nm)) f.name = nm->valuestring;
                if (cJSON_IsNumber(mb)) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%.1fMB", mb->valuedouble);
                    f.size_mb = buf;
                }
                files_.push_back(f);
            }
        }
        cJSON_Delete(root);
        browsing_folders_ = false;
        ShowFiles();
    }

    void ClearList() { lv_obj_clean(list_); }

    void ShowFolders() {
        ClearList();
        lv_label_set_text(title_label_, "Folders");
        lv_obj_add_flag(back_btn_, LV_OBJ_FLAG_HIDDEN);
        AddListButton("All Files", "", onFolderClick);
        for (auto& f : folders_) {
            char lbl[128];
            snprintf(lbl, sizeof(lbl), "%s  (%d)", f.name.c_str(), f.count);
            AddListButton(lbl, f.name.c_str(), onFolderClick);
        }
    }

    void ShowFiles() {
        ClearList();
        lv_label_set_text(title_label_, current_folder_.empty() ? "All Files" : current_folder_.c_str());
        lv_obj_remove_flag(back_btn_, LV_OBJ_FLAG_HIDDEN);
        for (auto& f : files_) {
            char lbl[256];
            snprintf(lbl, sizeof(lbl), "%s  %s", f.name.c_str(), f.size_mb.c_str());
            AddListButton(lbl, f.name.c_str(), onFileClick);
        }
    }

    lv_obj_t* AddListButton(const char* text, const char* data, lv_event_cb_t cb) {
        auto* theme = static_cast<LvglTheme*>(display_->GetTheme());
        auto* text_font = theme->text_font()->font();
        auto fg = lv_color_white();

        lv_obj_t* btn = lv_obj_create(list_);
        lv_obj_set_size(btn, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 8, 0);
        lv_obj_set_style_radius(btn, 4, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, text_font, 0);
        lv_obj_set_style_text_color(lbl, fg, 0);
        lv_label_set_text(lbl, text);
        lv_obj_set_width(lbl, LV_PCT(100));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        lv_obj_set_user_data(btn, (void*)data);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

        lv_obj_t* sep = lv_obj_create(list_);
        lv_obj_set_size(sep, LV_PCT(100), 1);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        return btn;
    }

    void PlayFile(const std::string& filename) {
        StopPlayback();
        playing_file_ = filename;
        stop_requested_ = false;
        is_playing_ = true;

        size_t last = filename.rfind('/');
        const char* dname = (last != std::string::npos) ? filename.c_str() + last + 1 : filename.c_str();
        lv_label_set_text_fmt(now_playing_, "> %s", dname);
        lv_label_set_text(play_btn_, FONT_AWESOME_PAUSE);

        xTaskCreate(playTask, "mp3play", PLAY_TASK_STACK, this, 5, &play_task_);
    }

    void StopPlayback() {
        if (play_task_) {
            stop_requested_ = true;
            vTaskDelay(pdMS_TO_TICKS(300));
            play_task_ = nullptr;
        }
        is_playing_ = false;
        lv_label_set_text(now_playing_, "");
        lv_label_set_text(play_btn_, FONT_AWESOME_PLAY);
    }

    static void playTask(void* arg) {
        auto* self = static_cast<Mp3PlayerView*>(arg);
        self->StreamLoop();
        self->play_task_ = nullptr;
        self->is_playing_ = false;
        vTaskDelete(nullptr);
    }

    void StreamLoop() {
        ensure_decoders_registered();

        std::string url = BASE_URL "/api/stream/";
        url += playing_file_;

        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.timeout_ms = 15000;
        cfg.buffer_size = HTTP_BUF_SIZE;
        esp_http_client_handle_t cli = esp_http_client_init(&cfg);

        esp_err_t err = esp_http_client_open(cli, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Stream open failed: %d", err);
            esp_http_client_cleanup(cli);
            return;
        }

        int clen = esp_http_client_fetch_headers(cli);
        ESP_LOGI(TAG, "Streaming: %s (%d bytes)", playing_file_.c_str(), clen);

        void* dec = nullptr;
        esp_audio_simple_dec_cfg_t dcfg = {};
        dcfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        dcfg.use_frame_dec = false;
        esp_audio_err_t derr = esp_audio_simple_dec_open(&dcfg, &dec);
        if (derr != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "MP3 decoder open failed: %d", derr);
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            return;
        }

        uint8_t* hbuf = (uint8_t*)malloc(HTTP_BUF_SIZE);
        uint8_t* dbuf = (uint8_t*)malloc(DEC_OUT_SIZE);
        if (!hbuf || !dbuf) {
            free(hbuf); free(dbuf);
            esp_audio_simple_dec_close(dec);
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            return;
        }

        int output_sr = 0;
        int output_ch = 0;
        auto& audio = Application::GetInstance().GetAudioService();

        // Ensure codec output is enabled
        audio.PushPcmToPlaybackQueue(std::vector<int16_t>());  // wake up codec

        while (!stop_requested_) {
            int r = esp_http_client_read(cli, (char*)hbuf, HTTP_BUF_SIZE);
            if (r <= 0) break;

            esp_audio_simple_dec_raw_t raw = {};
            raw.buffer = hbuf;
            raw.len = r;

            while (!stop_requested_ && raw.consumed < (uint32_t)r) {
                esp_audio_simple_dec_out_t out = {};
                out.buffer = dbuf;
                out.len = DEC_OUT_SIZE;

                derr = esp_audio_simple_dec_process(dec, &raw, &out);
                if (derr == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) break;

                if (out.decoded_size > 0) {
                    // Get stream info
                    if (output_sr == 0) {
                        esp_audio_simple_dec_info_t info;
                        if (esp_audio_simple_dec_get_info(dec, &info) == ESP_AUDIO_ERR_OK) {
                            output_sr = info.sample_rate;
                            output_ch = info.channel;
                            ESP_LOGI(TAG, "MP3: %dHz %dch", output_sr, output_ch);
                        }
                    }

                    int samples = out.decoded_size / 2;  // 16-bit
                    int num_out = (output_ch == 2) ? samples / 2 : samples;

                    if (num_out > 0) {
                        std::vector<int16_t> pcm;
                        pcm.reserve(num_out);
                        auto* src = (int16_t*)dbuf;

                        if (output_ch == 2) {
                            // Downmix stereo to mono
                            for (int i = 0; i < num_out; i++) {
                                int32_t mix = (int32_t)src[i * 2] + (int32_t)src[i * 2 + 1];
                                pcm.push_back((int16_t)(mix / 2));
                            }
                        } else {
                            pcm.assign(src, src + num_out);
                        }

                        if (!pcm.empty()) {
                            audio.PushPcmToPlaybackQueue(std::move(pcm));
                        }
                    }
                }
            }
        }

        free(hbuf);
        free(dbuf);
        esp_audio_simple_dec_close(dec);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        ESP_LOGI(TAG, "Stream done");
    }

    static void onFolderClick(lv_event_t* e) {
        auto* self = static_cast<Mp3PlayerView*>(lv_event_get_user_data(e));
        auto* btn = lv_event_get_target_obj(e);
        auto* name = static_cast<const char*>(lv_obj_get_user_data(btn));
        if (name) self->FetchFiles(name);
    }

    static void onFileClick(lv_event_t* e) {
        auto* self = static_cast<Mp3PlayerView*>(lv_event_get_user_data(e));
        auto* btn = lv_event_get_target_obj(e);
        auto* name = static_cast<const char*>(lv_obj_get_user_data(btn));
        if (name) self->PlayFile(name);
    }

    static void onBackTap(lv_event_t* e) {
        auto* self = static_cast<Mp3PlayerView*>(lv_event_get_user_data(e));
        if (!self->browsing_folders_) self->FetchFolders();
    }

    static void onPlayTap(lv_event_t* e) {
        auto* self = static_cast<Mp3PlayerView*>(lv_event_get_user_data(e));
        if (self->is_playing_) self->StopPlayback();
    }

    static void onStopTap(lv_event_t* e) {
        auto* self = static_cast<Mp3PlayerView*>(lv_event_get_user_data(e));
        self->StopPlayback();
    }
};

void Mp3PlayerApp::OnEnter(AppContext& ctx, lv_obj_t* screen) {
    auto* theme = static_cast<LvglTheme*>(ctx.display->GetTheme());
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    view_ = new Mp3PlayerView(screen, ctx.display, theme);
    ESP_LOGI(TAG, "MP3 Player entered");
}

void Mp3PlayerApp::OnExit() {
    delete view_;
    view_ = nullptr;
    ESP_LOGI(TAG, "MP3 Player exited");
}
