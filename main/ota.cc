#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

#define TAG "Ota"


Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // Read Serial Number from efuse user_data
    uint8_t serial_number[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            has_serial_number_ = false;
        } else {
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
    Settings settings("wifi", false);
    std::string url = settings.GetString("ota_url");
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }
    return url;
}

std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/* 
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
esp_err_t Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return ESP_ERR_INVALID_ARG;
    }

    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    if (!http->Open(method, url)) {
        int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open HTTP connection, code=0x%x", last_error);
        return last_error;
    }

    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return status_code;
    }

    data = http->ReadAll();
    http->Close();

    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL
    
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;
            
            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }
            
            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    firmware_version_.clear();
    firmware_url_.clear();
    firmware_sha256_.clear();
    firmware_size_ = 0;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }
        cJSON *sha256 = cJSON_GetObjectItem(firmware, "sha256");
        if (cJSON_IsString(sha256)) {
            firmware_sha256_ = sha256->valuestring;
        }
        cJSON *size = cJSON_GetObjectItem(firmware, "size");
        if (cJSON_IsNumber(size) && size->valuedouble > 0) {
            firmware_size_ = static_cast<size_t>(size->valuedouble);
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // If the force flag is set to 1, the given version is forced to be installed
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

bool Ota::Upgrade(
    const std::string& firmware_url,
    std::function<void(int progress, size_t speed)> callback,
    const std::string& expected_version,
    const std::string& expected_sha256,
    size_t expected_size) {
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    esp_ota_handle_t update_handle = 0;
    bool update_started = false;
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    std::string image_header;

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http->Open("GET", firmware_url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get firmware, status code: %d", http->GetStatusCode());
        return false;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        http->Close();
        return false;
    }
    if (content_length > update_partition->size) {
        ESP_LOGE(TAG, "Firmware is too large: %u > partition %u",
                 content_length, update_partition->size);
        http->Close();
        return false;
    }
    if (expected_size != 0 && content_length != expected_size) {
        ESP_LOGE(TAG, "Firmware size mismatch: HTTP=%u expected=%u",
                 content_length, expected_size);
        http->Close();
        return false;
    }

    constexpr size_t PAGE_SIZE = 4096;
    char* buffer = (char*)heap_caps_malloc(PAGE_SIZE, MALLOC_CAP_INTERNAL);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        http->Close();
        return false;
    }

    mbedtls_sha256_context sha256_context;
    mbedtls_sha256_init(&sha256_context);
    if (mbedtls_sha256_starts(&sha256_context, 0) != 0) {
        ESP_LOGE(TAG, "Failed to initialize SHA-256");
        mbedtls_sha256_free(&sha256_context);
        heap_caps_free(buffer);
        http->Close();
        return false;
    }

    auto fail_upgrade = [&](const char* message) {
        ESP_LOGE(TAG, "%s", message);
        if (update_started) {
            esp_ota_abort(update_handle);
        }
        mbedtls_sha256_free(&sha256_context);
        heap_caps_free(buffer);
        http->Close();
        return false;
    };

    size_t buffer_offset = 0;  // Current data size in buffer
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        int ret = http->Read(buffer + buffer_offset, PAGE_SIZE - buffer_offset);
        if (ret < 0) {
            char message[96];
            snprintf(message, sizeof(message), "Failed to read HTTP data: %s",
                     esp_err_to_name(ret));
            return fail_upgrade(message);
        }

        if (ret > 0 &&
            mbedtls_sha256_update(
                &sha256_context,
                reinterpret_cast<const unsigned char*>(buffer + buffer_offset),
                static_cast<size_t>(ret)) != 0) {
            return fail_upgrade("Failed to update firmware SHA-256");
        }
        if (!image_header_checked && ret > 0) {
            image_header.append(buffer + buffer_offset, static_cast<size_t>(ret));
        }

        // Calculate speed and progress every second
        recent_read += ret;
        total_read += ret;
        buffer_offset += ret;
        if (total_read > content_length) {
            return fail_upgrade("Firmware body exceeds declared content length");
        }
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            ESP_LOGI(TAG, "Progress: %u%% (%u/%u), Speed: %uB/s", progress, total_read, content_length, recent_read);
            if (callback) {
                callback(progress, recent_read);
            }
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (!image_header_checked) {
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));

                if (new_app_info.magic_word != ESP_APP_DESC_MAGIC_WORD) {
                    return fail_upgrade("Invalid ESP app descriptor");
                }
                const auto running_app_info = esp_app_get_description();
                if (strcmp(new_app_info.project_name,
                           running_app_info->project_name) != 0) {
                    char message[128];
                    snprintf(message, sizeof(message),
                             "Firmware project mismatch: got %s expected %s",
                             new_app_info.project_name,
                             running_app_info->project_name);
                    return fail_upgrade(message);
                }
                if (!expected_version.empty() &&
                    expected_version != new_app_info.version) {
                    char message[128];
                    snprintf(message, sizeof(message),
                             "Firmware version mismatch: got %s expected %s",
                             new_app_info.version, expected_version.c_str());
                    return fail_upgrade(message);
                }

                esp_err_t begin_error = esp_ota_begin(
                    update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                    &update_handle);
                if (begin_error != ESP_OK) {
                    char message[96];
                    snprintf(message, sizeof(message),
                             "Failed to begin OTA: %s",
                             esp_err_to_name(begin_error));
                    return fail_upgrade(message);
                }
                update_started = true;

                image_header_checked = true;
                std::string().swap(image_header);
            }
        }

        // Write to flash when buffer is full (4KB) or it's the last chunk
        bool is_last_chunk = (ret == 0);
        if (buffer_offset == PAGE_SIZE || (is_last_chunk && buffer_offset > 0)) {
            auto err = esp_ota_write(update_handle, buffer, buffer_offset);
            if (err != ESP_OK) {
                char message[96];
                snprintf(message, sizeof(message),
                         "Failed to write OTA data: %s", esp_err_to_name(err));
                return fail_upgrade(message);
            }

            buffer_offset = 0;
        }

        if (is_last_chunk) {
            break;
        }
    }
    http->Close();
    heap_caps_free(buffer);

    if (!image_header_checked || !update_started) {
        mbedtls_sha256_free(&sha256_context);
        ESP_LOGE(TAG, "Firmware ended before a complete app header was received");
        return false;
    }
    if (total_read != content_length ||
        (expected_size != 0 && total_read != expected_size)) {
        esp_ota_abort(update_handle);
        mbedtls_sha256_free(&sha256_context);
        ESP_LOGE(TAG, "Firmware length mismatch after download: read=%u expected=%u",
                 total_read, expected_size != 0 ? expected_size : content_length);
        return false;
    }

    unsigned char actual_digest[32];
    if (mbedtls_sha256_finish(&sha256_context, actual_digest) != 0) {
        esp_ota_abort(update_handle);
        mbedtls_sha256_free(&sha256_context);
        ESP_LOGE(TAG, "Failed to finalize firmware SHA-256");
        return false;
    }
    mbedtls_sha256_free(&sha256_context);

    char actual_sha256[65];
    for (size_t i = 0; i < sizeof(actual_digest); ++i) {
        snprintf(actual_sha256 + i * 2, sizeof(actual_sha256) - i * 2,
                 "%02x", actual_digest[i]);
    }
    if (!expected_sha256.empty()) {
        std::string normalized_sha256 = expected_sha256;
        std::transform(
            normalized_sha256.begin(), normalized_sha256.end(),
            normalized_sha256.begin(),
            [](unsigned char value) { return std::tolower(value); });
        if (normalized_sha256.size() != 64 ||
            normalized_sha256 != actual_sha256) {
            esp_ota_abort(update_handle);
            ESP_LOGE(TAG, "Firmware SHA-256 mismatch");
            return false;
        }
    }
    ESP_LOGI(TAG, "Firmware SHA-256 verified: %s", actual_sha256);

    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    return Upgrade(
        firmware_url_, callback, firmware_version_,
        firmware_sha256_, firmware_size_);
}


bool Ota::ParseVersion(
    const std::string& version, std::vector<int>& output) {
    output.clear();
    std::stringstream ss(version);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) {
            return false;
        }
        int value = 0;
        for (unsigned char character : segment) {
            if (!std::isdigit(character)) {
                return false;
            }
            if (value > 1000000) {
                return false;
            }
            value = value * 10 + (character - '0');
        }
        output.push_back(value);
    }
    return !output.empty();
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current;
    std::vector<int> newer;
    if (!ParseVersion(currentVersion, current) ||
        !ParseVersion(newVersion, newer)) {
        ESP_LOGE(TAG, "Invalid firmware version: current=%s new=%s",
                 currentVersion.c_str(), newVersion.c_str());
        return false;
    }
    
    const size_t component_count = std::max(current.size(), newer.size());
    current.resize(component_count, 0);
    newer.resize(component_count, 0);
    for (size_t i = 0; i < component_count; ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }
    
    return false;
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节
    
    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

esp_err_t Ota::Activate() {
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "No activation challenge found");
        return ESP_FAIL;
    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }
    
    auto status_code = http->GetStatusCode();
    if (status_code == 202) {
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}
