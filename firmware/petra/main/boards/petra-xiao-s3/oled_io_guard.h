#ifndef _OLED_IO_GUARD_H_
#define _OLED_IO_GUARD_H_

#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

// Wraps the OLED's I2C panel IO so that flaky I2C transfers never hang LVGL.
//
// The stock I2C panel IO only calls on_color_trans_done when a transfer
// succeeds, so one failed transfer leaves LVGL waiting for flush-ready
// forever (task watchdog in wait_for_flushing). This guard:
//   - retries every transfer a few times, resetting the I2C bus between
//     tries (frees an OLED that holds SDA low),
//   - always reports the colour transfer as done,
//   - re-initialises the panel after any I2C error (errors coincide with
//     supply dips that reset the SSD1306 to display-off) and every 30 s,
//     like tools/pcb-test's recovery, then asks for a full redraw.
//
// Create the wrapped IO, create the SSD1306 panel on it, then call
// oled_io_guard_set_panel().
esp_err_t oled_io_guard_new(esp_lcd_panel_io_handle_t inner, i2c_master_bus_handle_t bus,
                            esp_lcd_panel_io_handle_t* ret_io);
// mirror_x/mirror_y are re-applied after every re-init (the SSD1306 init
// sequence turns mirroring off, which flips the screen upside down).
void oled_io_guard_set_panel(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, bool mirror_x,
                             bool mirror_y);

// True if any transfer failed since the last call (and clears the flag).
// Used to check whether the first panel init really worked.
bool oled_io_guard_take_failed(esp_lcd_panel_io_handle_t io);

// Returns true once after the panel was re-initialised, so the caller can
// invalidate the whole screen. Call from the LVGL task.
bool oled_io_guard_take_recovered(esp_lcd_panel_io_handle_t io);

#endif  // _OLED_IO_GUARD_H_
