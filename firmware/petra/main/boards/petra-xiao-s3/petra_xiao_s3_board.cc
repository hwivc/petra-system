#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "esp_video.h"
#include "led/gpio_led.h"
#include "settings.h"
#include "mcp_server.h"
#include "oled_io_guard.h"
#include "photo_to_display.h"
#include "hwivc_info.h"
#include "esp_jpeg_dec.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <vector>
#include <lvgl.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_rom_sys.h>

#define TAG "PetraXiaoS3"

// PDM mic + MAX98357A, with extra output gain and a soft limiter.
class PetraAudioCodec : public NoAudioCodecSimplexPdm {
public:
    using NoAudioCodecSimplexPdm::NoAudioCodecSimplexPdm;

protected:
    int Write(const int16_t* data, int samples) override {
        constexpr int32_t kKnee = 24000;
        buffer_.resize(samples);
        for (int i = 0; i < samples; i++) {
            int32_t v = static_cast<int32_t>(data[i] * AUDIO_OUTPUT_GAIN);
            int32_t mag = v < 0 ? -v : v;
            if (mag > kKnee) {
                // Above the knee, compress 4:1 so loud peaks stay clean.
                mag = kKnee + (mag - kKnee) / 4;
                if (mag > 32767) {
                    mag = 32767;
                }
                v = v < 0 ? -mag : mag;
            }
            buffer_[i] = static_cast<int16_t>(v);
        }
        return NoAudioCodecSimplexPdm::Write(buffer_.data(), samples);
    }

private:
    std::vector<int16_t> buffer_;
};

// EspVideo with logging, so a photo request is visible in the monitor
// (upstream logs nothing while capturing).
class PetraCamera : public EspVideo {
private:
    bool capture_stuck_ = false;

public:
    using EspVideo::EspVideo;

    // Upstream Capture() blocks forever in VIDIOC_DQBUF if no complete frame
    // arrives. Run it in a helper task and give up after a timeout, so neither
    // our photo tool nor Xiaozhi's camera tool can freeze Petra.
    bool Capture() override {
        if (capture_stuck_) {
            ESP_LOGE(TAG, "Camera: a previous capture is still stuck, restart Petra");
            return false;
        }
        ESP_LOGI(TAG, "Camera: capturing photo...");
        int64_t start = esp_timer_get_time();
        struct Job {
            PetraCamera* self;
            SemaphoreHandle_t done;
            bool ok;
        };
        auto* job = new Job{this, xSemaphoreCreateBinary(), false};
        xTaskCreate(
            [](void* arg) {
                auto* j = static_cast<Job*>(arg);
                j->ok = j->self->EspVideo::Capture();
                xSemaphoreGive(j->done);
                vTaskDelete(nullptr);
            },
            "capture", 12288, job, 2, nullptr);
        if (xSemaphoreTake(job->done, pdMS_TO_TICKS(CAMERA_CAPTURE_TIMEOUT_MS)) != pdTRUE) {
            capture_stuck_ = true;  // the helper task is still blocked: keep `job` alive
            ESP_LOGE(TAG, "Camera: capture timed out after %d ms", CAMERA_CAPTURE_TIMEOUT_MS);
            return false;
        }
        bool ok = job->ok;
        vSemaphoreDelete(job->done);
        delete job;
        ESP_LOGI(TAG, "Camera: capture %s in %lld ms", ok ? "OK" : "FAILED",
                 (esp_timer_get_time() - start) / 1000);
        return ok;
    }

    // Captures a frame and converts it to 8-bit grayscale in PSRAM.
    // Caller frees *out with heap_caps_free().
    bool CaptureLuma(uint8_t** out, int* width, int* height) {
        if (!Capture() || frame_.data == nullptr) {
            return false;
        }
        int w = frame_.width, h = frame_.height;
        size_t n = (size_t)w * h;
        auto* luma = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM));
        if (luma == nullptr) {
            return false;
        }
        const uint8_t* src = frame_.data;
        switch (frame_.format) {
            case V4L2_PIX_FMT_YUYV:
                for (size_t i = 0; i < n; i++) luma[i] = src[i * 2];
                break;
            case V4L2_PIX_FMT_UYVY:
                for (size_t i = 0; i < n; i++) luma[i] = src[i * 2 + 1];
                break;
            case V4L2_PIX_FMT_GREY:
                memcpy(luma, src, n);
                break;
            case V4L2_PIX_FMT_JPEG:
                heap_caps_free(luma);
                return DecodeJpegLuma(src, frame_.len, out, width, height);
            case V4L2_PIX_FMT_RGB24:
                for (size_t i = 0; i < n; i++) {
                    luma[i] = (src[i * 3] * 77 + src[i * 3 + 1] * 150 + src[i * 3 + 2] * 29) >> 8;
                }
                break;
            case V4L2_PIX_FMT_RGB565: {
                auto* px = reinterpret_cast<const uint16_t*>(src);
                for (size_t i = 0; i < n; i++) {
                    int r = (px[i] >> 11) << 3, g = ((px[i] >> 5) & 63) << 2, b = (px[i] & 31) << 3;
                    luma[i] = (r * 77 + g * 150 + b * 29) >> 8;
                }
                break;
            }
            default:
                ESP_LOGE(TAG, "Camera: unsupported frame format 0x%08lx", (unsigned long)frame_.format);
                heap_caps_free(luma);
                return false;
        }
        *out = luma;
        *width = w;
        *height = h;
        return true;
    }

    // Decodes the camera's JPEG to 8-bit grayscale (the Y of CbYCrY output).
    static bool DecodeJpegLuma(const uint8_t* jpg, size_t len, uint8_t** out, int* width, int* height) {
        jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
        cfg.output_type = JPEG_PIXEL_FORMAT_CbYCrY;
        jpeg_dec_handle_t dec = nullptr;
        if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
            return false;
        }
        jpeg_dec_io_t io = {};
        io.inbuf = const_cast<uint8_t*>(jpg);
        io.inbuf_len = len;
        jpeg_dec_header_info_t info = {};
        int out_len = 0;
        uint8_t* yuv = nullptr;
        bool ok = jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK &&
                  jpeg_dec_get_outbuf_len(dec, &out_len) == JPEG_ERR_OK &&
                  (yuv = static_cast<uint8_t*>(jpeg_calloc_align(out_len, 16))) != nullptr;
        if (ok) {
            io.outbuf = yuv;
            ok = jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
        }
        jpeg_dec_close(dec);
        if (!ok) {
            ESP_LOGE(TAG, "Camera: JPEG decode failed");
            if (yuv) jpeg_free_align(yuv);
            return false;
        }
        size_t n = (size_t)info.width * info.height;
        auto* luma = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM));
        if (luma != nullptr) {
            for (size_t i = 0; i < n; i++) luma[i] = yuv[i * 2 + 1];
        }
        jpeg_free_align(yuv);
        if (luma == nullptr) return false;
        *out = luma;
        *width = info.width;
        *height = info.height;
        return true;
    }

    std::expected<std::string, std::string> Explain(const std::string& question) override {
        ESP_LOGI(TAG, "Camera: sending photo for \"%s\"", question.c_str());
        int64_t start = esp_timer_get_time();
        auto result = EspVideo::Explain(question);
        if (result) {
            ESP_LOGI(TAG, "Camera: explained in %lld ms", (esp_timer_get_time() - start) / 1000);
        } else {
            ESP_LOGE(TAG, "Camera: explain failed: %s", result.error().c_str());
        }
        return result;
    }
};

class PetraXiaoS3 : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    PetraCamera* camera_ = nullptr;
    esp_timer_handle_t conversation_timer_ = nullptr;
    volatile bool end_requested_ = false;
    bool end_seen_speaking_ = false;
    int64_t end_requested_us_ = 0;
    Button boot_button_;
    Button ext_button_;

    bool CreateDisplayI2c(gpio_num_t sda, gpio_num_t scl) {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        return i2c_new_master_bus(&bus_config, &display_i2c_bus_) == ESP_OK;
    }

    // Same start-up as tools/pcb-test: the OLED only answers ~5 s after
    // power-up, and the test also tried SDA/SCL swapped. Keep scanning both
    // pin orders until DISPLAY_PROBE_DEADLINE_MS after boot. Never abort, so
    // a missing OLED cannot boot-loop Petra.
    // If the OLED was cut off mid-transfer (the battery keeps it powered while
    // the ESP32 resets), it can hold SDA low. Clock SCL 9 times and send a STOP
    // so it lets go of the bus (standard I2C bus clear).
    static void ClearI2cBus(gpio_num_t sda, gpio_num_t scl) {
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << sda) | (1ULL << scl);
        io.mode = GPIO_MODE_INPUT_OUTPUT_OD;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io);
        gpio_set_level(sda, 1);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(10);
        for (int i = 0; i < 9 && gpio_get_level(sda) == 0; i++) {
            gpio_set_level(scl, 0);
            esp_rom_delay_us(10);
            gpio_set_level(scl, 1);
            esp_rom_delay_us(10);
        }
        // STOP: SDA low -> high while SCL is high
        gpio_set_level(sda, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(10);
        gpio_set_level(sda, 1);
        esp_rom_delay_us(10);
        gpio_reset_pin(sda);
        gpio_reset_pin(scl);
    }

    uint8_t FindOled() {
        ClearI2cBus(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
        const gpio_num_t pins[2][2] = {
            {DISPLAY_SDA_PIN, DISPLAY_SCL_PIN},
            {DISPLAY_SCL_PIN, DISPLAY_SDA_PIN},
        };
        const uint8_t addresses[] = {0x3C, 0x3D};
        int attempt = 0;
        do {
            for (auto& p : pins) {
                if (!CreateDisplayI2c(p[0], p[1])) {
                    continue;
                }
                i2c_master_bus_reset(display_i2c_bus_);
                for (uint8_t addr : addresses) {
                    if (i2c_master_probe(display_i2c_bus_, addr, 50) == ESP_OK) {
                        ESP_LOGI(TAG, "OLED found at 0x%02X (SDA=GPIO%d SCL=GPIO%d) %lld ms after boot",
                                 addr, p[0], p[1], esp_timer_get_time() / 1000);
                        return addr;
                    }
                }
                i2c_del_master_bus(display_i2c_bus_);
                display_i2c_bus_ = nullptr;
            }
            if (attempt++ % 8 == 0) {
                ESP_LOGW(TAG, "Waiting for OLED... %lld ms after boot", esp_timer_get_time() / 1000);
                ClearI2cBus(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        } while (esp_timer_get_time() / 1000 < DISPLAY_PROBE_DEADLINE_MS);
        return 0;
    }

    // Give the OLED (and camera) the same power-up time as tools/pcb-test.
    void WaitForPowerUp() {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms < DISPLAY_POWER_UP_MS) {
            ESP_LOGI(TAG, "Waiting %lld ms for OLED/camera power-up", DISPLAY_POWER_UP_MS - now_ms);
            vTaskDelay(pdMS_TO_TICKS(DISPLAY_POWER_UP_MS - now_ms));
        }
    }

    // Without a working OLED, Xiaozhi still loads LVGL fonts from the assets
    // partition, which crashes if LVGL was never initialised. lv_init() is
    // safe to call more than once.
    void UseNoDisplay() {
        lv_init();
        display_ = new NoDisplay();
    }

    void InitializeSsd1306Display() {
        uint8_t addr = FindOled();
        if (addr == 0) {
            ESP_LOGE(TAG, "OLED not found, continuing without display");
            UseNoDisplay();
            return;
        }
        for (int attempt = 1; attempt <= DISPLAY_INIT_ATTEMPTS; attempt++) {
            if (TryInitSsd1306(addr)) {
                ESP_LOGI(TAG, "OLED initialised (attempt %d)", attempt);
                display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                           DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
                StartOledRecoveryTimer();
                return;
            }
            ESP_LOGW(TAG, "OLED init attempt %d failed, retrying", attempt);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        ESP_LOGE(TAG, "Failed to initialize OLED, continuing without display");
        UseNoDisplay();
    }

    // After the guard re-initialises a glitched OLED, redraw the whole screen.
    void StartOledRecoveryTimer() {
        if (!lvgl_port_lock(1000)) {
            return;
        }
        lv_timer_create([](lv_timer_t* timer) {
            static int ticks = 0;
            auto io = static_cast<esp_lcd_panel_io_handle_t>(lv_timer_get_user_data(timer));
            // Every 30 s force a flush even on a static screen, so the guard's
            // periodic re-init also runs when nothing on screen changes.
            if (oled_io_guard_take_recovered(io) || ++ticks % 60 == 0) {
                lv_obj_invalidate(lv_screen_active());
            }
        }, 500, panel_io_);
        lvgl_port_unlock();
    }

    bool TryInitSsd1306(uint8_t addr) {
        if (panel_ != nullptr) {
            esp_lcd_panel_del(panel_);
            panel_ = nullptr;
        }
        if (panel_io_ != nullptr) {
            esp_lcd_panel_io_del(panel_io_);
            panel_io_ = nullptr;
        }

        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = addr,
            .scl_speed_hz = DISPLAY_I2C_SPEED_HZ,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .transaction_timeout_ms = DISPLAY_I2C_TIMEOUT_MS,
        };
        esp_lcd_panel_io_handle_t i2c_io = nullptr;
        if (esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &i2c_io) != ESP_OK) {
            return false;
        }
        // Flaky I2C must never hang LVGL: see oled_io_guard.h
        if (oled_io_guard_new(i2c_io, display_i2c_bus_, &panel_io_) != ESP_OK) {
            esp_lcd_panel_io_del(i2c_io);
            return false;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        if (esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_) != ESP_OK ||
            esp_lcd_panel_reset(panel_) != ESP_OK ||
            esp_lcd_panel_init(panel_) != ESP_OK ||
            esp_lcd_panel_invert_color(panel_, false) != ESP_OK ||
            esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK ||
            oled_io_guard_take_failed(panel_io_)) {
            return false;
        }
        oled_io_guard_set_panel(panel_io_, panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        return true;
    }

    // Only sets the volume when none is saved, so "volume down" still sticks.
    void InitializeDefaultVolume() {
        Settings settings("audio", true);
        if (settings.GetInt("output_volume", -1) < 0) {
            settings.SetInt("output_volume", AUDIO_DEFAULT_OUTPUT_VOLUME);
        }
    }

    // Lets the AI end the conversation by voice ("stop listening", "goodbye"),
    // like pressing BOOT. Waits for Petra's spoken goodbye to finish first.
    void InitializeConversationTools() {
        McpServer::GetInstance().AddTool("self.conversation.end",
            "End the conversation and stop listening. Use this when the user says goodbye, "
            "says they are done, or asks you to stop listening (for example: 'stop listening', "
            "'that's all', 'bye Petra', 'au revoir', 'arrête d'écouter'). Say a short goodbye "
            "in the same reply. Afterwards the device waits for the wake word 'Hey Petra'.",
            PropertyList(),
            [this](const PropertyList&) -> ToolResult {
                end_seen_speaking_ = false;
                end_requested_us_ = esp_timer_get_time();
                end_requested_ = true;
                ESP_LOGI(TAG, "Conversation end requested by voice");
                return true;
            });

        esp_timer_create_args_t args = {};
        args.callback = [](void* arg) { static_cast<PetraXiaoS3*>(arg)->CheckConversationEnd(); };
        args.arg = this;
        args.name = "conv_end";
        ESP_ERROR_CHECK(esp_timer_create(&args, &conversation_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(conversation_timer_, 200 * 1000));
    }

    // Postcard of the visitor -> ePaper panel, all on local Wi-Fi.
    // The last photo is kept in PSRAM for PHOTO_KEEP_MS so it can be restyled
    // without taking a new picture, then deleted.
    uint8_t* last_luma_ = nullptr;
    int last_w_ = 0, last_h_ = 0;
    int64_t last_photo_us_ = 0;
    std::string last_caption_;

    void ForgetLastPhoto() {
        if (last_luma_ != nullptr) {
            heap_caps_free(last_luma_);
            last_luma_ = nullptr;
            ESP_LOGI(TAG, "Photo: last photo deleted");
        }
    }

    // Live info from hwivc.org: new blogs, projects and events appear automatically.
    void InitializeHwivcTools() {
        McpServer::GetInstance().AddTool("self.hwivc.list",
            "List the latest blog posts, projects or events on hwivc.org, the website of the "
            "Hardware Innovation Valley Community (HWIVC), live from the internet. Use it when the "
            "visitor asks what HWIVC or PlastiBytes has written, built or organised, or what is new.\n"
            "  `kind`: blog, projects, events or all.",
            PropertyList({Property("kind", kPropertyTypeString, "all")}),
            [](const PropertyList& properties) -> ToolResult {
                return HwivcList(properties["kind"].value<std::string>());
            });
        McpServer::GetInstance().AddTool("self.hwivc.read",
            "Read one blog post, project or event from hwivc.org live (title, author, summary and "
            "text). Use it to answer questions about a specific HWIVC project, blog or event, e.g. "
            "PlastiWatch, MarineSentinel, Arduino Day, the hackathon. Summarise it in your own words "
            "in 2-4 spoken sentences.\n"
            "  `topic`: the title or a few keywords.",
            PropertyList({Property("topic", kPropertyTypeString)}),
            [](const PropertyList& properties) -> ToolResult {
                return HwivcRead(properties["topic"].value<std::string>());
            });
    }

    void InitializePhotoTools() {
        const std::string options = std::string("\n  `style`: one of ") + kPhotoStyles +
                                    ".\n  `frame`: one of " + kPhotoFrames + ".";
        McpServer::GetInstance().AddTool("self.photo.send_to_display",
            "Take a photo of the visitor with Petra's camera and show it on the big ePaper display "
            "at the stand as a postcard with a frame and caption. Use it when the visitor asks for "
            "a picture, photo, selfie, postcard, sketch, comic, drawing or portrait of themselves. "
            "Before calling, tell the visitor to look at the camera and smile: the device counts "
            "down 3, 2, 1 on its screen. Styles: photo (newspaper photo), sketch (pencil outline), "
            "comic (pop-art dots), woodcut (bold carved print), engraving (banknote lines), stipple "
            "(ink dots). Frames: classic, polaroid, magazine (cover star), hero (Recycling Hero "
            "certificate), wanted (old-west poster), comic (speech bubble), film (film strip), "
            "event (CITS26 badge). Use 'surprise' when the visitor does not choose. The photo is "
            "not sent to the cloud; the visitor can scan a QR code on the display to download it.\n"
            "Args:\n  `caption`: short text on the postcard, max 40 characters, e.g. "
            "'Amina, Recycling Hero!'" + options,
            PropertyList({Property("caption", kPropertyTypeString, "Welcome to PlastiBytes!"),
                          Property("style", kPropertyTypeString, "surprise"),
                          Property("frame", kPropertyTypeString, "surprise")}),
            [this](const PropertyList& properties) -> ToolResult {
                if (camera_ == nullptr) {
                    return std::unexpected("The camera is not available");
                }
                for (int i = PHOTO_COUNTDOWN_S; i > 0; i--) {
                    display_->ShowNotification(std::to_string(i) + "...", 1000);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                display_->ShowNotification("Smile!", 1500);

                uint8_t* luma = nullptr;
                int w = 0, h = 0;
                if (!camera_->CaptureLuma(&luma, &w, &h)) {
                    return std::unexpected("Failed to take the photo");
                }
                ForgetLastPhoto();
                last_luma_ = luma;
                last_w_ = w;
                last_h_ = h;
                last_photo_us_ = esp_timer_get_time();
                last_caption_ = properties["caption"].value<std::string>();
                return SendLastPhoto(last_caption_, properties["style"].value<std::string>(),
                                     properties["frame"].value<std::string>());
            });

        McpServer::GetInstance().AddTool("self.photo.restyle",
            "Show the visitor's last photo again on the ePaper display in another style and/or "
            "frame, without taking a new picture (e.g. 'now make it a comic', 'put me on a "
            "magazine cover'). Works for 3 minutes after the photo was taken.\n"
            "Args:\n  `caption`: new caption, or empty to keep the previous one" + options,
            PropertyList({Property("caption", kPropertyTypeString, ""),
                          Property("style", kPropertyTypeString, "surprise"),
                          Property("frame", kPropertyTypeString, "surprise")}),
            [this](const PropertyList& properties) -> ToolResult {
                if (last_luma_ == nullptr) {
                    return std::string("There is no recent photo: take a new one with "
                                       "self.photo.send_to_display.");
                }
                auto caption = properties["caption"].value<std::string>();
                if (!caption.empty()) last_caption_ = caption;
                return SendLastPhoto(last_caption_, properties["style"].value<std::string>(),
                                     properties["frame"].value<std::string>());
            });
    }

    std::string SendLastPhoto(const std::string& caption, const std::string& style, const std::string& frame) {
        auto s = ResolveStyle(style);
        auto f = ResolveFrame(frame);
        ESP_LOGI(TAG, "Photo %dx%d, style %s, frame %s, caption \"%s\"", last_w_, last_h_, s.c_str(), f.c_str(),
                 caption.c_str());
        std::string result = SendPhotoToDisplay(last_luma_, last_w_, last_h_, caption, s, f);
        ESP_LOGI(TAG, "Photo: %s", result.c_str());
        return result;
    }

    void CheckConversationEnd() {
        if (last_luma_ != nullptr && esp_timer_get_time() - last_photo_us_ > PHOTO_KEEP_MS * 1000LL) {
            Application::GetInstance().Schedule([this]() { ForgetLastPhoto(); });
        }
        if (!end_requested_) {
            return;
        }
        auto& app = Application::GetInstance();
        auto state = app.GetDeviceState();
        if (state == kDeviceStateSpeaking) {
            end_seen_speaking_ = true;
            return;
        }
        if (state == kDeviceStateIdle) {
            end_requested_ = false;  // already ended
            return;
        }
        bool timed_out = esp_timer_get_time() - end_requested_us_ > CONVERSATION_END_TIMEOUT_MS * 1000LL;
        if (state == kDeviceStateListening && (end_seen_speaking_ || timed_out)) {
            end_requested_ = false;
            ESP_LOGI(TAG, "Ending conversation, waiting for wake word");
            app.ToggleChatState();  // closes the audio channel when listening
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // Optional external button: hold to talk (useful in noisy halls)
        ext_button_.OnPressDown([]() {
            Application::GetInstance().StartListening();
        });
        ext_button_.OnPressUp([]() {
            Application::GetInstance().StopListening();
        });
    }

    void InitializeCamera() {
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = 1,
                .scl_pin = CAMERA_PIN_SIOC,
                .sda_pin = CAMERA_PIN_SIOD,
            },
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new PetraCamera(video_config);
    }

public:
    PetraXiaoS3() :
        boot_button_(BOOT_BUTTON_GPIO),
        ext_button_(EXT_BUTTON_GPIO) {
        InitializeDefaultVolume();
        WaitForPowerUp();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeCamera();
        InitializeConversationTools();
        InitializePhotoTools();
        InitializeHwivcTools();
    }

    virtual Led* GetLed() override {
        static GpioLed led(BUILTIN_LED_GPIO, 1);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static PetraAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            I2S_STD_SLOT_BOTH, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(PetraXiaoS3);
