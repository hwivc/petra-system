#include "oled_io_guard.h"

#include <esp_lcd_panel_io_interface.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>

#define TAG "OledIoGuard"

namespace {

constexpr int kAttempts = 3;
// Minimum time between re-inits triggered by I2C errors.
constexpr int64_t kReinitIntervalUs = 1 * 1000 * 1000;
// Safety net: re-send the init sequence this often even without errors.
// A supply dip can reset the SSD1306 (display off) without any I2C error.
constexpr int64_t kPeriodicReinitUs = 30 * 1000 * 1000;

struct OledIoGuard {
    esp_lcd_panel_io_t base;  // must be first
    esp_lcd_panel_io_handle_t inner;
    i2c_master_bus_handle_t bus;
    esp_lcd_panel_handle_t panel;
    bool mirror_x;
    bool mirror_y;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void* user_ctx;
    bool failed;          // a transfer failed even after retries
    bool glitch;          // any attempt failed: the OLED may have reset itself
    bool reinitialising;
    bool recovered;       // panel was re-initialised, screen needs a redraw
    int64_t last_reinit_us;
    uint32_t failures;
};

OledIoGuard* Guard(esp_lcd_panel_io_t* io) {
    return reinterpret_cast<OledIoGuard*>(io);
}

// Clock out a stuck transfer so the next try starts on an idle bus.
void RecoverBus(OledIoGuard* g) {
    i2c_master_bus_reset(g->bus);
    vTaskDelay(1);
}

void TryReinit(OledIoGuard* g) {
    int64_t now = esp_timer_get_time();
    if (g->panel == nullptr || now - g->last_reinit_us < kReinitIntervalUs) {
        return;
    }
    g->last_reinit_us = now;
    g->reinitialising = true;
    g->failed = false;
    g->glitch = false;
    bool ok = esp_lcd_panel_init(g->panel) == ESP_OK &&
              esp_lcd_panel_mirror(g->panel, g->mirror_x, g->mirror_y) == ESP_OK &&
              esp_lcd_panel_disp_on_off(g->panel, true) == ESP_OK && !g->failed;
    g->reinitialising = false;
    if (ok) {
        g->recovered = true;
        ESP_LOGI(TAG, "OLED re-initialised (%lu failed transfers so far)", (unsigned long)g->failures);
    } else {
        g->failed = true;
    }
}

esp_err_t RxParam(esp_lcd_panel_io_t* io, int lcd_cmd, void* param, size_t param_size) {
    return esp_lcd_panel_io_rx_param(Guard(io)->inner, lcd_cmd, param, param_size);
}

esp_err_t TxParam(esp_lcd_panel_io_t* io, int lcd_cmd, const void* param, size_t param_size) {
    auto g = Guard(io);
    for (int i = 0; i < kAttempts; i++) {
        if (esp_lcd_panel_io_tx_param(g->inner, lcd_cmd, param, param_size) == ESP_OK) {
            return ESP_OK;
        }
        g->glitch = true;
        RecoverBus(g);
    }
    // Pretend success so the panel driver carries on to tx_color, where the
    // frame is completed and recovery is scheduled.
    g->failed = true;
    g->failures++;
    return ESP_OK;
}

esp_err_t TxColor(esp_lcd_panel_io_t* io, int lcd_cmd, const void* color, size_t color_size) {
    auto g = Guard(io);
    // Errors coincide with supply dips (Wi-Fi TX, audio start) that can reset
    // the SSD1306 to display-off, so re-send the init sequence after any of
    // them, and periodically as a safety net.
    bool periodic = esp_timer_get_time() - g->last_reinit_us >= kPeriodicReinitUs;
    if ((g->failed || g->glitch || periodic) && !g->reinitialising) {
        TryReinit(g);
    }
    bool sent = false;
    if (!g->failed) {
        for (int i = 0; i < kAttempts && !sent; i++) {
            // On success the inner IO calls on_color_trans_done itself.
            sent = esp_lcd_panel_io_tx_color(g->inner, lcd_cmd, color, color_size) == ESP_OK;
            if (!sent) {
                g->glitch = true;
                RecoverBus(g);
            }
        }
        if (!sent) {
            g->failed = true;
            g->failures++;
        }
    }
    if (!sent && g->on_color_trans_done != nullptr) {
        // Never leave LVGL waiting for a flush that will not complete.
        g->on_color_trans_done(&g->base, nullptr, g->user_ctx);
    }
    return ESP_OK;
}

esp_err_t RegisterEventCallbacks(esp_lcd_panel_io_t* io, const esp_lcd_panel_io_callbacks_t* cbs, void* user_ctx) {
    auto g = Guard(io);
    g->on_color_trans_done = cbs->on_color_trans_done;
    g->user_ctx = user_ctx;
    return esp_lcd_panel_io_register_event_callbacks(g->inner, cbs, user_ctx);
}

esp_err_t Del(esp_lcd_panel_io_t* io) {
    auto g = Guard(io);
    esp_err_t ret = esp_lcd_panel_io_del(g->inner);
    free(g);
    return ret;
}

}  // namespace

esp_err_t oled_io_guard_new(esp_lcd_panel_io_handle_t inner, i2c_master_bus_handle_t bus,
                            esp_lcd_panel_io_handle_t* ret_io) {
    auto g = static_cast<OledIoGuard*>(calloc(1, sizeof(OledIoGuard)));
    if (g == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    g->inner = inner;
    g->bus = bus;
    g->base.rx_param = RxParam;
    g->base.tx_param = TxParam;
    g->base.tx_color = TxColor;
    g->base.del = Del;
    g->base.register_event_callbacks = RegisterEventCallbacks;
    *ret_io = &g->base;
    return ESP_OK;
}

void oled_io_guard_set_panel(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, bool mirror_x,
                             bool mirror_y) {
    auto g = Guard(io);
    g->panel = panel;
    g->mirror_x = mirror_x;
    g->mirror_y = mirror_y;
    g->last_reinit_us = esp_timer_get_time();
    g->glitch = false;
}

bool oled_io_guard_take_failed(esp_lcd_panel_io_handle_t io) {
    auto g = Guard(io);
    bool failed = g->failed;
    g->failed = false;
    return failed;
}

bool oled_io_guard_take_recovered(esp_lcd_panel_io_handle_t io) {
    auto g = Guard(io);
    if (!g->recovered) {
        return false;
    }
    g->recovered = false;
    return true;
}
