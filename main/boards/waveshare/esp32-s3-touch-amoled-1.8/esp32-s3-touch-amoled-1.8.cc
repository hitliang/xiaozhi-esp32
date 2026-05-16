#include "wifi_board.h"
#include "display/lcd_display.h"
#include "esp_lcd_sh8601.h"

#include "codecs/es8311_audio_codec.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include "i2c_device.h"
#include "app/app_manager.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include "esp_io_expander_tca9554.h"
#include "settings.h"

#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <cmath>

#include "qmi8658.hpp"

using Imu = espp::Qmi8658<>;  // I2C interface (default)

// Complementary filter for IMU orientation
// All values in radians (QMI8658 library uses sinf/cosf on orientation)
// Returns Value with .x=roll, .y=pitch (matching Value::roll and Value::pitch union layout)
static Imu::Value complementary_filter(float dt, const Imu::Value& accel, const Imu::Value& gyro) {
    static float pitch = 0, roll = 0;
    float accel_pitch = atan2f(accel.y, accel.z);       // radians
    float accel_roll  = atan2f(-accel.x, accel.z);      // radians
    float gyro_y_rad = gyro.y * (float)(M_PI / 180.0);  // °/s → rad/s
    float gyro_x_rad = gyro.x * (float)(M_PI / 180.0);
    pitch = 0.98f * (pitch + gyro_y_rad * dt) + 0.02f * accel_pitch;
    roll  = 0.98f * (roll  + gyro_x_rad * dt) + 0.02f * accel_roll;
    return { roll, pitch, 0.0f };  // radians
}

#define TAG "WaveshareEsp32s3TouchAMOLED1inch8"

class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
        WriteReg(0x27, 0x10);  // hold 4s to power off

        // Disable All DCs but DC1
        WriteReg(0x80, 0x01);
        // Disable All LDOs
        WriteReg(0x90, 0x00);
        WriteReg(0x91, 0x00);

        // Set DC1 to 3.3V
        WriteReg(0x82, (3300 - 1500) / 100);

        // Set ALDO1 to 3.3V
        WriteReg(0x92, (3300 - 500) / 100);

        // Enable ALDO1(MIC)
        WriteReg(0x90, 0x01);
    
        WriteReg(0x64, 0x02); // CV charger voltage setting to 4.1V
        
        WriteReg(0x61, 0x02); // set Main battery precharge current to 50mA
        WriteReg(0x62, 0x08); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
        WriteReg(0x63, 0x01); // set Main battery term charge current to 25mA
    }
};

#define LCD_OPCODE_WRITE_CMD (0x02ULL)
#define LCD_OPCODE_READ_CMD (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const sh8601_lcd_init_cmd_t vendor_specific_init[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10}
};

// 在waveshare_amoled_1_8类之前添加新的显示类
class CustomLcdDisplay : public SpiLcdDisplay {
public:
    CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                    esp_lcd_panel_handle_t panel_handle,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy)
        : SpiLcdDisplay(io_handle, panel_handle,
                    width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
        // Note: UI customization should be done in SetupUI(), not in constructor
        // to ensure lvgl objects are created before accessing them
    }

    virtual void SetupUI() override {
        // Call parent SetupUI() first to create all lvgl objects
        SpiLcdDisplay::SetupUI();

        DisplayLockGuard lock(this);
        lv_obj_set_style_pad_left(status_bar_, LV_HOR_RES * 0.1, 0);
        lv_obj_set_style_pad_right(status_bar_, LV_HOR_RES * 0.1, 0);
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(esp_lcd_panel_io_handle_t panel_io) : Backlight(), panel_io_(panel_io) {}

protected:
    esp_lcd_panel_io_handle_t panel_io_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        auto display = Board::GetInstance().GetDisplay();
        DisplayLockGuard lock(display);
        uint8_t data[1] = {((uint8_t)((255 * brightness) / 100))};
        int lcd_cmd = 0x51;
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
        esp_lcd_panel_io_tx_param(panel_io_, lcd_cmd, &data, sizeof(data));
    }
};

class WaveshareEsp32s3TouchAMOLED1inch8 : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Pmic* pmic_ = nullptr;
    Button boot_button_;
    CustomLcdDisplay* display_;
    CustomBacklight* backlight_;
    esp_io_expander_handle_t io_expander = NULL;
    PowerSaveTimer* power_save_timer_;

    // QMI8658 IMU
    Imu* imu_ = nullptr;
    i2c_master_dev_handle_t imu_dev_handle_ = nullptr;
    ImuData imu_data_;
    bool imu_data_valid_ = false;
    esp_timer_handle_t imu_timer_ = nullptr;

    void InitializeQmi8658() {
        printf("[IMU] InitializeQmi8658 start\n");

        // Add QMI8658 device on the shared I2C bus
        i2c_device_config_t imu_dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = Imu::DEFAULT_ADDRESS,  // 0x6B
            .scl_speed_hz = 400000,
        };
        esp_err_t ret = i2c_master_bus_add_device(codec_i2c_bus_, &imu_dev_cfg, &imu_dev_handle_);
        if (ret != ESP_OK) {
            printf("[IMU] add device 0x%02X failed: %s\n", Imu::DEFAULT_ADDRESS, esp_err_to_name(ret));
            // Try alternate address
            imu_dev_cfg.device_address = Imu::DEFAULT_ADDRESS_AD0_LOW;  // 0x6A
            ret = i2c_master_bus_add_device(codec_i2c_bus_, &imu_dev_cfg, &imu_dev_handle_);
            if (ret != ESP_OK) {
                printf("[IMU] add device 0x%02X failed: %s\n", Imu::DEFAULT_ADDRESS_AD0_LOW, esp_err_to_name(ret));
                return;
            }
        }
        printf("[IMU] I2C device added at 0x%02X\n", imu_dev_cfg.device_address);

        // Create I2C write / write_then_read callbacks
        auto dev = imu_dev_handle_;
        auto write_fn = [dev](uint8_t addr, const uint8_t* data, size_t len) -> bool {
            return i2c_master_transmit(dev, data, len, 10) == ESP_OK;
        };
        auto read_fn = [dev](uint8_t addr, uint8_t* data, size_t len) -> bool {
            return i2c_master_receive(dev, data, len, 10) == ESP_OK;
        };
        auto write_then_read_fn = [dev](uint8_t addr, const uint8_t* wdata, size_t wlen,
                                         uint8_t* rdata, size_t rlen) -> bool {
            return i2c_master_transmit_receive(dev, wdata, wlen, rdata, rlen, 10) == ESP_OK;
        };

        Imu::Config config{
            .device_address = static_cast<uint8_t>(imu_dev_cfg.device_address),
            .write = std::move(write_fn),
            .read = std::move(read_fn),
            .imu_config = {
                .accelerometer_range = Imu::AccelerometerRange::RANGE_8G,
                .accelerometer_odr = Imu::ODR::ODR_250_HZ,
                .gyroscope_range = Imu::GyroscopeRange::RANGE_512_DPS,
                .gyroscope_odr = Imu::ODR::ODR_250_HZ,
            },
            .orientation_filter = complementary_filter,
            .auto_init = false,
        };

        imu_ = new Imu(config);
        printf("[IMU] Imu object created\n");
        imu_->set_write_then_read(std::move(write_then_read_fn));

        // Manual init to support QMI8658C (WHO_AM_I=0x4C) in addition to QMI8658 (0x05)
        std::error_code init_ec;
        uint8_t whoami = imu_->get_device_id(init_ec);
        printf("[IMU] WHO_AM_I = 0x%02X\n", whoami);
        if (init_ec || (whoami != 0x05 && whoami != 0x4C)) {
            printf("[IMU] unsupported device ID 0x%02X, init FAILED\n", whoami);
            delete imu_;
            imu_ = nullptr;
            return;
        }

        // Write 0x60 to CTRL1 (register 0x02):
        //   bit6=1: address auto-increment (required for multi-byte reads)
        //   bit5=1: reserved, must be 1
        // Done via raw I2C since write_u8_to_register is private in the library
        {
            uint8_t ctrl1[] = {0x02, 0x60};
            esp_err_t ret = i2c_master_transmit(imu_dev_handle_, ctrl1, sizeof(ctrl1), 10);
            if (ret != ESP_OK) {
                printf("[IMU] CTRL1 write FAILED: %s\n", esp_err_to_name(ret));
                delete imu_;
                imu_ = nullptr;
                return;
            }
        }

        // Apply IMU configuration
        if (!imu_->set_config(config.imu_config, init_ec)) {
            printf("[IMU] set_config FAILED: %s\n", init_ec.message().c_str());
            delete imu_;
            imu_ = nullptr;
            return;
        }

        // Enable accelerometer and gyroscope
        if (!imu_->set_accelerometer_enabled(true, init_ec) || !imu_->set_gyroscope_enabled(true, init_ec)) {
            printf("[IMU] enable sensors FAILED: %s\n", init_ec.message().c_str());
            delete imu_;
            imu_ = nullptr;
            return;
        }

        printf("[IMU] init OK (%s), starting timer\n", whoami == 0x4C ? "QMI8658C" : "QMI8658");

        // Create a periodic timer to update IMU readings at ~50Hz
        esp_timer_create_args_t imu_timer_args = {
            .callback = [](void* arg) {
                auto self = static_cast<WaveshareEsp32s3TouchAMOLED1inch8*>(arg);
                if (!self->imu_) return;
                std::error_code ec;
                self->imu_->update(0.02f, ec);  // 20ms = 50Hz
                if (!ec) {
                    auto accel = self->imu_->get_accelerometer();
                    auto gyro = self->imu_->get_gyroscope();
                    auto orient = self->imu_->get_orientation();
                    self->imu_data_ = {
                        .accel_x = accel.x * 9.80665f, .accel_y = accel.y * 9.80665f, .accel_z = accel.z * 9.80665f,
                        .gyro_x = gyro.x * (float)(M_PI / 180.0), .gyro_y = gyro.y * (float)(M_PI / 180.0), .gyro_z = gyro.z * (float)(M_PI / 180.0),
                        .pitch = orient.y * (float)(180.0 / M_PI), .roll = orient.x * (float)(180.0 / M_PI), .yaw = orient.z * (float)(180.0 / M_PI),
                        .valid = true,
                    };
                    if (!self->imu_data_valid_) {
                        self->imu_data_valid_ = true;
                        ESP_LOGI(TAG, "QMI8658 streaming OK: pitch=%.1f roll=%.1f",
                                 (double)(orient.y * (float)(180.0 / M_PI)), (double)(orient.x * (float)(180.0 / M_PI)));
                    }
                } else {
                    static int err_count = 0;
                    if (++err_count <= 5) {
                        ESP_LOGE(TAG, "QMI8658 update #%d failed: %s", err_count, ec.message().c_str());
                    }
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "imu_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&imu_timer_args, &imu_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(imu_timer_, 20000));  // 20ms

        printf("[IMU] timer started at 50Hz, addr=0x%02X\n", imu_dev_cfg.device_address);
        ESP_LOGI(TAG, "QMI8658 initialized at 0x%02X", imu_dev_cfg.device_address);
    }

    ImuData GetImuData() override { return imu_data_; }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(20);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeTca9554(void) {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(codec_i2c_bus_, I2C_ADDRESS, &io_expander);
        if(ret != ESP_OK)
            ESP_LOGE(TAG, "TCA9554 create returned error");
        ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 |IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT);
        ESP_ERROR_CHECK(ret);
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2, 1);
        ESP_ERROR_CHECK(ret);
        vTaskDelay(pdMS_TO_TICKS(100));
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2, 0);
        ESP_ERROR_CHECK(ret);
        vTaskDelay(pdMS_TO_TICKS(300));
        ret = esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1|IO_EXPANDER_PIN_NUM_2, 1);
        ESP_ERROR_CHECK(ret);
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(codec_i2c_bus_, 0x34);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = GPIO_NUM_11;
        buscfg.data0_io_num = GPIO_NUM_4;
        buscfg.data1_io_num = GPIO_NUM_5;
        buscfg.data2_io_num = GPIO_NUM_6;
        buscfg.data3_io_num = GPIO_NUM_7;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        // Single click: toggle listening in xiaozhi app, navigate elsewhere
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            auto& mgr = AppManager::GetInstance();
            // Black screen: exit
            if (mgr.IsScreenOff()) {
                mgr.HandleBootClick();
                return;
            }
            // In xiaozhi app: single click toggles listening state
            if (mgr.GetCurrentAppId() == "xiaozhi") {
                app.ToggleChatState();
                return;
            }
            // Global (home/grid/other apps): exit app or enter black screen
            mgr.HandleBootClick();
        });

        // Double click: in xiaozhi app, exit to black screen
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                return;
            }
            auto& mgr = AppManager::GetInstance();
            if (mgr.GetCurrentAppId() == "xiaozhi") {
                mgr.HandleBootClick();
                return;
            }
            // Global: same as single click (navigate)
            mgr.HandleBootClick();
        });
    }

    void InitializeSH8601Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
            EXAMPLE_PIN_NUM_LCD_CS,
            nullptr,
            nullptr
        );
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        const sh8601_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(sh8601_lcd_init_cmd_t),
            .flags ={
                .use_qspi_interface = 1,
            }
        };

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.flags.reset_active_high = 1,
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void *)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new CustomLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new CustomBacklight(panel_io);
        backlight_->RestoreBrightness();
    }

    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_21,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 8,
            .flags =
            {
                .disable_control_phase = 1,
            }
        };
        tp_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(codec_i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(), 
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);

        // Increase gesture detection threshold so short taps are not mistaken for swipes
        lv_indev_t* indev = lv_indev_get_next(NULL);
        while (indev) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
                lv_indev_set_gesture_min_distance(indev, 80);
                break;
            }
            indev = lv_indev_get_next(indev);
        }

        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // 初始化工具
    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });
    }

public:
    WaveshareEsp32s3TouchAMOLED1inch8() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializePowerSaveTimer();
        InitializeCodecI2c();
        InitializeTca9554();
        InitializeAxp2101();
        InitializeSpi();
        InitializeSH8601Display();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        InitializeQmi8658();  // After codec_i2c_bus_ is ready
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchAMOLED1inch8);
