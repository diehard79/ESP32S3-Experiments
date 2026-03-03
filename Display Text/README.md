# Experiment 1 — Display Text on ESP32-S3-Touch-LCD-1.69

> **Goal:** Render "Welcome Saeed !" centred on the 240×280 ST7789V2 display  
> **Stack:** ESP-IDF v5.x · LVGL v9.x · Windows + VS Code

---

## Table of Contents

1. [Hardware Overview](#1-hardware-overview)
2. [Pin Reference](#2-pin-reference)
3. [Project Structure](#3-project-structure)
4. [Architecture & Philosophy](#4-architecture--philosophy)
5. [Component Deep-Dive](#5-component-deep-dive)
6. [LVGL on Microcontrollers — Key Concepts](#6-lvgl-on-microcontrollers--key-concepts)
7. [Confirmed Hardware Quirks](#7-confirmed-hardware-quirks)
8. [sdkconfig Reference](#8-sdkconfig-reference)
9. [Build, Flash & Monitor](#9-build-flash--monitor)
10. [Expected Serial Output](#10-expected-serial-output)
11. [Troubleshooting](#11-troubleshooting)
12. [Lessons Learned & Future Experiments](#12-lessons-learned--future-experiments)

---

## 1. Hardware Overview

| Property | Value |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-1.69 |
| MCU | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Flash | 16 MB QSPI |
| PSRAM | 8 MB (AP_3v3) |
| Display | 1.69" ST7789V2, 240×280, RGB565 |
| Touch | CST816T capacitive (I2C) |
| IMU | QMI8658C 6-axis (I2C) |
| RTC | PCF85063A (I2C) |
| USB | Native USB-Serial/JTAG (no separate USB-UART chip) |
| Power | LiPo battery charging via ETA6098 + USB-C |

---

## 2. Pin Reference

All GPIO assignments are derived from the official Waveshare schematic
(`ESP32-S3-Touch-LCD-1.69-Sch.pdf`).

### Display — ST7789V2 (SPI)

| Signal | GPIO | Notes |
|---|---|---|
| `LCD_DC` | 4 | Data / Command select |
| `LCD_CS` | 5 | SPI Chip Select (active LOW) |
| `LCD_CLK` | 6 | SPI Clock — up to 40 MHz |
| `LCD_MOSI` | 7 | SPI Data (write-only, no MISO) |
| `LCD_RST` | 8 | Hardware reset (active LOW) |
| `LCD_BL` | 15 | Backlight PWM (active HIGH) |

### Touch Controller — CST816T (I2C)

| Signal | GPIO | Notes |
|---|---|---|
| `TP_SCL` | 10 | Shared I2C bus |
| `TP_SDA` | 11 | Shared I2C bus |
| `TP_RST` | 13 | Touch controller reset |
| `TP_INT` | 14 | Interrupt — fires on touch event |
| I2C Address | `0x15` | Fixed |

### IMU — QMI8658C (I2C, shared bus)

| Signal | GPIO | Notes |
|---|---|---|
| `SCL` | 10 | Shared with Touch + RTC |
| `SDA` | 11 | Shared with Touch + RTC |
| `INT2` | 9 | Motion interrupt |
| I2C Address | `0x6B` | Fixed (SDO pin pulled HIGH) |

### RTC — PCF85063A (I2C, shared bus)

| Signal | GPIO | Notes |
|---|---|---|
| `SCL` | 10 | Shared I2C bus |
| `SDA` | 11 | Shared I2C bus |
| `INT` | 41 | Alarm / timer interrupt |

### Other Peripherals

| Peripheral | GPIO | Notes |
|---|---|---|
| Battery ADC | 1 | Voltage divider; read with `adc1_get_raw()` |
| Buzzer | 33 | Drive HIGH to enable; **pull LOW at boot** or LDO overheats |
| Boot button | 0 | Strapping pin; LOW at reset = download mode |
| SYS_EN | 36 | Power latch — drive HIGH to keep LDO alive |
| SYS_OUT | 35 | System power status output |
| Key1 | 0 | Shared with BOOT |
| Key2 | — | See schematic for additional buttons |

> ⚠️ **Critical:** On power-up, assert `GPIO36 (SYS_EN) = HIGH` early in
> `app_main()` to hold the LDO active. Failing to do this causes random
> reboots when running from battery.

---

## 3. Project Structure

```
exp1_display_text/
├── CMakeLists.txt              ← Top-level build; registers component dirs
├── sdkconfig.defaults          ← Kconfig presets applied before menuconfig
├── main/
│   ├── CMakeLists.txt          ← Declares main.c, REQUIRES lcd + lvgl_setup
│   ├── idf_component.yml       ← Pulls lvgl/lvgl ≥9.0 from component registry
│   └── main.c                  ← app_main() + UI creation
└── components/
    ├── lcd/
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   └── app_lcd.h       ← GPIO/SPI constants + public API
    │   └── app_lcd.c           ← ST7789V2 init, backlight PWM
    └── lvgl_setup/
        ├── CMakeLists.txt
        ├── include/
        │   └── app_lvgl.h      ← LVGL init API + mutex helpers
        └── app_lvgl.c          ← LVGL init, flush CB, tick timer, task
```

Each subdirectory under `components/` is an independent IDF component with
its own `CMakeLists.txt`. This means they can be reused across future
experiments without copying source files — just add to `EXTRA_COMPONENT_DIRS`.

---

## 4. Architecture & Philosophy

### 4.1 Separation of Concerns

Embedded projects often collapse everything into `app_main()`. This becomes
unmaintainable quickly. The guiding principle here is **one responsibility
per component**:

```
┌─────────────────────────────────────────────┐
│                  main.c                     │  ← Only UI logic
│  app_main() → init → draw screen            │
└────────────┬───────────────┬────────────────┘
             │               │
     ┌───────▼──────┐  ┌─────▼──────────┐
     │  lcd/        │  │  lvgl_setup/   │
     │  app_lcd.c   │  │  app_lvgl.c    │
     │              │  │                │
     │  SPI + panel │  │  LVGL core +   │
     │  LEDC (BL)   │  │  flush + task  │
     └──────────────┘  └────────────────┘
             │               │
     ┌───────▼───────────────▼────────────┐
     │           ESP-IDF drivers          │
     │  spi_master · ledc · esp_lcd       │
     └────────────────────────────────────┘
```

### 4.2 The Initialisation Contract

Hardware must always be ready before software that depends on it:

```
1. app_lcd_init()    → SPI bus, panel driver, backlight
2. app_lvgl_init()   → needs panel handle from step 1
3. ui_create_*()     → needs LVGL running from step 2
```

Violating this order causes hard faults or silent rendering failures.
`ESP_ERROR_CHECK()` is used at every init step so the system halts with a
clear error rather than silently misbehaving.

### 4.3 Thread Safety with LVGL

LVGL is **not thread-safe**. Its entire state — widgets, animations, timers —
must be accessed from one context at a time. The pattern used here:

```c
// Always wrap lv_* calls outside the LVGL task:
if (app_lvgl_lock(0)) {      // acquire mutex
    lv_label_set_text(...);   // safe LVGL call
    app_lvgl_unlock();         // release mutex
}
```

The LVGL task itself calls `lv_timer_handler()` in a loop while holding the
same mutex, so all rendering is serialised correctly.

### 4.4 DMA-Capable Buffers

Draw buffers are allocated with `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`:

```c
heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
```

This is critical on ESP32-S3. Without `MALLOC_CAP_DMA`, the SPI DMA engine
cannot access the buffer and the transfer silently fails or corrupts data.
External PSRAM (`MALLOC_CAP_SPIRAM`) cannot be used for DMA buffers on
this chip.

### 4.5 Double Buffering

Two draw buffers are provided to LVGL (`LV_DISPLAY_RENDER_MODE_PARTIAL`).
While LVGL renders into buffer B, the SPI DMA is transmitting buffer A to
the panel — effectively pipelining CPU work with DMA transfers and
eliminating the visual tearing you'd see with a single buffer.

### 4.6 The LEDC Backlight Pattern

The backlight is driven via the LEDC (LED PWM Controller) peripheral rather
than a bare `gpio_set_level()`. This gives two benefits:

- **Soft start:** backlight comes on smoothly at the end of init rather than
  flashing full-brightness during the panel reset sequence
- **Dimming:** future experiments can dim the display by calling
  `ledc_set_duty()` without any hardware changes

### 4.7 Microcontroller Development Principles

Working with resource-constrained embedded systems requires a different
mindset from application development:

**Everything is a resource.** RAM, flash, CPU cycles, GPIO pins, DMA channels,
SPI bus bandwidth — all are finite and shared. A design decision in one
component (e.g. buffer size) directly constrains another (e.g. available
heap for the TCP stack).

**Initialisation order matters.** Unlike a desktop OS, there is no dynamic
linker or lazy initialisation. Hardware peripherals must be configured in
dependency order, and using a peripheral before its clock is enabled or its
pins are configured will produce subtle, hard-to-debug failures.

**Failures should be loud.** Use `ESP_ERROR_CHECK()` on every hardware call
during development. A panic with a clear error message is far easier to debug
than a silent failure that manifests three layers higher. Swap to graceful
error handling only in production code paths.

**Polling vs interrupts vs DMA.** Prefer DMA for bulk transfers (display
pixels), interrupts for low-latency events (touch, IMU), and polling only
for slow or non-critical peripherals. Never poll in a tight loop — always
use `vTaskDelay()` or event groups to yield to the FreeRTOS scheduler.

**Stack sizes are explicit.** Unlike threads on a desktop OS, FreeRTOS task
stacks are fixed at creation time. If a task overflows its stack the system
will crash or silently corrupt memory. Use `uxTaskGetStackHighWaterMark()` to
measure actual usage and size stacks with ~20% headroom.

---

## 5. Component Deep-Dive

### `components/lcd/` — ST7789V2 Driver

**`app_lcd.h`** declares all hardware constants in one place so they are
never magic numbers scattered through `.c` files. Any pin change only
requires editing the header.

**`app_lcd_init()` execution sequence:**

```
_backlight_init()                   PWM timer + channel on GPIO15, duty=0
spi_bus_initialize()                SPI2_HOST, MOSI=7, CLK=6, DMA=auto
esp_lcd_new_panel_io_spi()          DC=4, CS=5, 40MHz, mode-0, depth=10
esp_lcd_new_panel_st7789()          RGB565, RGB endian, RST=8
esp_lcd_panel_reset()               Hardware pulse on RST pin
esp_lcd_panel_init()                Send ST7789V2 init command sequence
esp_lcd_panel_set_gap(0, 20)        Shift GRAM window (see §7)
esp_lcd_panel_swap_xy(false)        Portrait orientation
esp_lcd_panel_mirror(false, false)  Correct L/R and T/B orientation
esp_lcd_panel_disp_on_off(true)     Display ON
_backlight_set(100)                 Backlight to 100%
```

### `components/lvgl_setup/` — LVGL Integration

**`app_lvgl_init()` execution sequence:**

```
xSemaphoreCreateMutex()             Thread-safety mutex
lv_init()                           LVGL core initialisation
esp_timer_start_periodic(2ms)       Feeds lv_tick_inc() every 2ms
heap_caps_malloc() × 2              Two DMA draw buffers (1/10 frame each)
lv_display_create(240, 280)         Register display with LVGL
lv_display_set_buffers()            Attach double buffers, partial mode
lv_display_set_flush_cb()           Route rendered pixels → esp_lcd
xTaskCreatePinnedToCore(core=1)     LVGL handler task on APP_CPU
```

**The flush callback** is the bridge between LVGL and esp_lcd:

```c
static void _lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                            uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    lv_display_flush_ready(disp);  // MUST be called or LVGL stalls
}
```

---

## 6. LVGL on Microcontrollers — Key Concepts

### Tick Source
LVGL needs a monotonic millisecond clock. On ESP32 this is provided by
an `esp_timer` that calls `lv_tick_inc(2)` every 2 ms. Never use
`vTaskDelay()` inside the LVGL task as the tick source — it's too imprecise.

### Render Mode
`LV_DISPLAY_RENDER_MODE_PARTIAL` renders only dirty regions, saving CPU and
SPI bandwidth. Full-screen mode (`FULL`) requires a buffer the size of the
entire frame (240×280×2 = 134 KB) which may exceed available internal SRAM.

### Font System
LVGL fonts are **pre-compiled bitmaps**, not vector fonts. Each font size
(14, 24, 32, 36, 48…) must be explicitly enabled in Kconfig and adds to
flash usage. The anti-aliasing is baked into the glyph alpha channel at
compile time — larger sizes have smoother edges because the bitmap has more
pixels to represent each curve.

### Widget Coordinate System
- `(0, 0)` is top-left of the parent object
- `LV_ALIGN_CENTER` aligns the object's centre to its parent's centre
- Y offsets are positive downward, negative upward
- For a 240×280 screen, true centre is `(120, 140)`

### The LVGL Task Loop

```c
for (;;) {
    if (app_lvgl_lock(10)) {
        sleep_ms = lv_timer_handler();  // processes all pending work
        app_lvgl_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(sleep_ms));  // LVGL tells us how long to sleep
}
```

`lv_timer_handler()` returns the number of milliseconds until the next
scheduled event. Sleeping for that duration lets FreeRTOS run other tasks
efficiently instead of busy-waiting.

---

## 7. Confirmed Hardware Quirks

These are discoveries made during development of this experiment and are
specific to the Waveshare ESP32-S3-Touch-LCD-1.69.

### 7.1 GRAM Window Offset (`set_gap`)

The ST7789V2 controller has **240×320 internal GRAM** but the physical panel
only exposes **240×280 pixels**. The 40 unused rows sit at the top of GRAM
(rows 0–19 are unconnected to panel pixels on this board).

Without correction, the first 20 rows of every frame are written into
invisible GRAM, shifting the entire image up and leaving 20 rows of
uninitialised GRAM visible as noise at the bottom.

**Fix:** `esp_lcd_panel_set_gap(panel, 0, 20)` shifts the active draw window
down by 20 rows so pixel `(0,0)` maps to GRAM row 20.

```
GRAM row 0  ─ invisible (above panel)     ┐
GRAM row 19 ─ invisible                   ┘ 20 rows skipped by set_gap
GRAM row 20 ─ panel pixel (0,0)           ← our frame starts here
GRAM row 299─ panel pixel (0,279)         ← our frame ends here
GRAM row 300─ unused (below panel)        ┐
GRAM row 319─ unused                      ┘
```

### 7.2 Colour Endianness

Setting `rgb_endian = LCD_RGB_ENDIAN_RGB` produces correct colours. Using
`LCD_RGB_ENDIAN_BGR` causes the entire display to render with incorrect hues
(green background instead of dark, etc.). Do not attempt to compensate via
software colour-swapping — use `LCD_RGB_ENDIAN_RGB` and pass colour hex
values as-is.

### 7.3 Boot / Download Mode

The ESP32-S3 enters download mode if `GPIO0` is LOW at reset. Because this
board uses native USB (no dedicated USB-UART chip), the RTS/DTR auto-reset
circuit behaves differently from traditional ESP32 boards. If the device
shows `waiting for download` in the monitor:

1. Use `idf.py flash monitor -p COMx` (not separate commands)
2. Or manually: hold **BOOT**, tap **RESET**, release **BOOT**

Setting `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` routes the console through
the USB-JTAG controller, which handles reset signalling more reliably.

### 7.4 Orientation Matrix

The physical mounting of the ST7789V2 panel on this board requires:

```c
esp_lcd_panel_swap_xy(panel, false);          // portrait, no rotation
esp_lcd_panel_mirror(panel, false, false);    // no flip on either axis
```

Any other combination produces mirrored, inverted, or rotated output.
If experimenting with landscape mode, set `swap_xy=true` and adjust
`LCD_H_RES`/`LCD_V_RES` accordingly.

### 7.5 Buzzer / LDO Thermal Issue

`GPIO33` controls the buzzer transistor. If left floating or driven HIGH
inadvertently, current flows through the buzzer coil and its series resistor,
causing the 3.3V LDO to overheat. Always configure GPIO33 as output LOW
early in `app_main()` unless you intend to use the buzzer.

---

## 8. sdkconfig Reference

`sdkconfig.defaults` is applied once when you first run `idf.py build` or
`idf.py set-target`. It seeds the `sdkconfig` file. To re-apply after
changes, delete `sdkconfig` and rebuild, or use `idf.py fullclean`.

| Key | Value | Reason |
|---|---|---|
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240` | `y` | Maximum frequency for smooth LVGL rendering |
| `CONFIG_FREERTOS_HZ` | `1000` | 1 ms tick → accurate `vTaskDelay(pdMS_TO_TICKS(1))` |
| `CONFIG_LV_COLOR_DEPTH_16` | `y` | RGB565 matches the ST7789V2 native format |
| `CONFIG_LV_FONT_MONTSERRAT_36` | `y` | Primary font used in `main.c` |
| `CONFIG_LV_FONT_MONTSERRAT_14` | `y` | Fallback / debug font |
| `CONFIG_LV_MEMCPY_MEMSET_STD` | `y` | Use libc memcpy (faster than LVGL's default on Xtensa) |
| `CONFIG_LV_FONT_SUBPX_BGR` | `y` | Subpixel anti-aliasing for smoother glyph edges |
| `CONFIG_LV_DRAW_SW_COMPLEX` | `y` | Enables full anti-aliased software renderer |
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | `y` | Console via native USB-JTAG (no UART chip) |
| `CONFIG_LOG_DEFAULT_LEVEL_INFO` | `y` | Show INFO and above in monitor |

---

## 9. Build, Flash & Monitor

### Prerequisites

- ESP-IDF v5.x installed and activated (`export.ps1` on Windows)
- VS Code with the **ESP-IDF extension** (optional but recommended)
- Board connected via USB-C

### Commands

```powershell
# 1. Activate ESP-IDF environment (run once per terminal session)
. $env:IDF_PATH\export.ps1

# 2. Navigate to project
cd "Z:\...\exp1_display_text"

# 3. Fetch LVGL from the Espressif Component Registry (first time only)
idf.py update-dependencies

# 4. Set the target chip
idf.py set-target esp32s3

# 5. (Optional) inspect / change settings
idf.py menuconfig

# 6. Full clean build (required when changing fonts or sdkconfig)
idf.py fullclean
idf.py build

# 7. Flash and open monitor in one command
idf.py flash monitor -p COM7
```

### Boot Sequence After Flash

If monitor shows `waiting for download`:
1. Hold **BOOT** button
2. Tap **RESET** button
3. Release **BOOT** button

The device will boot normally and print the startup log.

### Exiting the Monitor

Press `Ctrl+]` to exit `idf_monitor` cleanly.

---

## 10. Expected Serial Output

```
I (307)  main:      === Experiment 1 : Display Text ===
I (310)  main:      Initialising LCD ...
I (310)  app_lcd:   Configuring backlight (initially OFF)
I (315)  app_lcd:   Initialising SPI2 bus
I (318)  app_lcd:   Installing panel IO (SPI)
I (320)  app_lcd:   Installing ST7789V2 panel driver
I (450)  app_lcd:   Turning backlight ON
I (450)  app_lcd:   LCD init complete (240x280)
I (451)  main:      Initialising LVGL ...
I (453)  app_lvgl:  Initialising LVGL 9.x.x
I (455)  app_lvgl:  Draw buffers: 2 × 13440 bytes in DMA-capable SRAM
I (457)  app_lvgl:  LVGL display registered (240x280)
I (460)  app_lvgl:  LVGL task started on core 1
I (461)  app_lvgl:  LVGL init complete
I (562)  main:      Drawing welcome screen ...
I (562)  main:      Welcome screen created
I (563)  main:      app_main done - LVGL task is running.
```

---

## 11. Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `waiting for download` in monitor | GPIO0 LOW at reset | Do BOOT+RESET sequence, or use `idf.py flash monitor` |
| Green / wrong colour background | `LCD_RGB_ENDIAN_BGR` set | Use `LCD_RGB_ENDIAN_RGB`; pass colours as normal hex |
| Text mirrored horizontally | `mirror_x=true` | Set `mirror(false, false)` |
| Text upside down | `mirror_y=true` | Set `mirror(false, false)` |
| Static noise at bottom edge | GRAM offset wrong | Adjust `set_gap(0, N)` — confirmed value is `20` |
| Black gap at top | `set_gap` value too large | Reduce row offset |
| Pixelated / jagged text | Wrong font size or AA disabled | Enable `LV_DRAW_SW_COMPLEX`, use font size ≥ 36 |
| `implicit declaration of ESP_RETURN_ON_ERROR` | Missing include | Add `#include "esp_check.h"` |
| Board overheating | GPIO33 (buzzer) floating | Drive GPIO33 LOW at startup |
| Random reboots on battery | SYS_EN not held | Drive GPIO36 HIGH in `app_main()` |
| LVGL renders nothing | `lv_display_flush_ready()` not called | Ensure flush callback calls it unconditionally |
| Heap allocation failure | DMA buffer too large | Reduce `LCD_DRAW_BUFFER_SIZE` or use PSRAM for non-DMA buffers |

---

## 12. Lessons Learned & Future Experiments

### What This Experiment Established

- **Confirmed working SPI + LVGL stack** for this board — reusable as a
  base template for all future display experiments
- **GRAM offset quirk** documented and fixed — `set_gap(0, 20)` is the
  correct value for this board
- **Colour pipeline** understood — `LCD_RGB_ENDIAN_RGB` is correct; no
  software colour manipulation needed
- **Orientation** confirmed — `swap_xy=false`, `mirror(false, false)`
- **Threading model** established — mutex pattern for LVGL thread safety

### Suggested Next Experiments

| # | Topic | New concepts introduced |
|---|---|---|
| 2 | Touch input (CST816T) | I2C driver, interrupt handling, LVGL input device |
| 3 | IMU display (QMI8658C) | I2C shared bus, sensor reading, real-time LVGL updates |
| 4 | RTC clock face (PCF85063A) | I2C RTC, LVGL arc/canvas widgets, animations |
| 5 | Battery gauge | ADC reading, voltage divider math, LVGL bar widget |
| 6 | Buzzer feedback | GPIO PWM tone generation, event-driven UI |
| 7 | Deep sleep + wake | ESP32-S3 power modes, RTC wakeup, ULP |
| 8 | Wi-Fi + NTP clock | Wi-Fi stack, SNTP, timezone handling |