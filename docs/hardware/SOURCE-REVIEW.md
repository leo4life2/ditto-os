# Source-review notes

Board repository: `waveshareteam/ESP32-S3-Touch-AMOLED-2.06@b099739ad0e33b34e5fbaae77f02bd84805d79a3`.

BSP 2.0.0 registry source: `waveshareteam/Waveshare-ESP32-components@9daffb7168b3c093a330da700988a22092294eef`.

Local subsets: `../../work/esp32-amoled/official/` and `../../work/esp32-amoled/components/`. Missing generated assets/managed dependencies mean these are not complete build packages. The two inventory JSON files record upstream entries; the download manifest records verified local files.

## Pins established from code

| Interface | GPIO/configuration |
|---|---|
| Shared I2C | SDA 15, SCL 14; BSP default 400 kHz |
| Display QSPI | CS 12, SCLK 11, data 4/5/6/7, reset 8; SPI2 |
| Display geometry | 410 × 502, horizontal controller offset 22, RGB565 |
| Touch | Reset 9, interrupt 38, I2C address 0x38 |
| I2S | Bit clock 41, master clock 16, word select 45, output 40, input 42 |
| Amplifier | Enable 46 |
| SDMMC | Clock 2, command 1, data 3; one-bit mode |
| BOOT | GPIO 0; motion example requests recalibration |
| PMIC | AXP2101 I2C address 0x34 |

Evidence: BSP public header/implementation, Arduino `Mylibrary/pin_config.h`, and example setup code. The Arduino header declares SDCS 17, but the SDMMC path does not use SPI CS. The local schematic was subsequently rendered and inspected. It labels display TE as GPIO 13, motor drive as GPIO 18, motion interrupt as GPIO 21, RTC interrupt as GPIO 39, and SYS_OUT as GPIO 10. GPIO 13 produced the measured panel timing signal; other listed signals were not actively driven/tested. PCB revision/population still needs physical confirmation.

## Display integration

Arduino LVGL examples allocate a full RGB565 buffer, try internal RAM then broader allocation, and transfer a full frame each loop. A one-poll touch interrupt flag is used as press state; robust drag/gesture handling deserves its own input service.

The BSP's `bsp_display_start_with_config()` uses the caller's LVGL task configuration but does not forward buffer-size/double-buffer/flags into its internal display initializer. That initializer selects buffers through Kconfig, assigns the PSRAM flag `false`, and calls an RGB-specific LVGL helper for this QSPI panel. Resolve and bench-test these integration concerns before promising tear-free output.

Brightness uses a panel command, not a separate LED backlight. Preserve the 22-pixel horizontal gap and even-start/odd-end update rounding. UI updates from other tasks must respect LVGL locking.

## Sensors, time, and power

The RTC sketch overwrites time at boot. Our clock service needs time validity, deliberate synchronization, RTC persistence, separate timezone policy, and monotonic intervals.

The Arduino IMU example uses the low I2C address and enables acceleration at ±4g/1 kHz; it reads the gyro without explicitly enabling it. The IDF version uses the high address, ±8g/500 Hz, 200-sample calibration, and a roughly 20 ms update task. Schematic text labels 0x6B. Our hardware probe confirmed address 0x6B and WHO_AM_I 0x05. Calibration retries recursively; bounded/nonblocking calibration is better suited to an OS service.

The IDF PMIC example configures charging and polls status once per second. Its commented rail assignments include unrelated-camera references. The Arduino telemetry example lacks an explicit PMIC `begin()` check, although its default constructor binds to `Wire`; that alone does not prove reads fail. Identify the device and handle errors explicitly in our service.

## Audio and media

The BSP creates I2S transmit/receive channels, ES8311 output, and ES7210 input. Its raw default is mono/22,050 Hz; `bsp_extra` reopens at 16 kHz, 16-bit, two channels. Combining examples requires consistent clock/channel configuration.

FFT processing averages microphones, windows 1,024 samples, computes a spectrum, and draws 64 sampled bins on a 410 × 200 canvas every 33 ms. Its spectrum array is shared with the UI without a snapshot protocol; use queued or double-buffered data for coherent service updates.

Video playback searches `/sdcard/avi`, decodes JPEG frames into alternating 320 × 200 buffers, and uses a 256 KiB AVI buffer. It rejects frames exceeding that output budget. This is a constrained MJPEG/AVI path, not general H.264/MP4 playback. Its FPS log counts polling iterations.

The Arduino echo example initializes ES8311 but not explicitly ES7210. Use the IDF microphone path as the fuller reference and validate cold-boot capture separately.

## App framework and build boundaries

Brookesia's app manager holds C++ pointers/IDs, invokes run/pause/resume/close callbacks, switches screens, and optionally retains snapshots. Resource tracking cleans recorded LVGL screens/timers/animations. This is useful lifecycle design evidence, not a binary loader or protected process model.

The demo's defaults disable its AI framework, services, animation player, and speaker system. Repository presence does not establish enabled runtime capability. Squareline clock/weather/chat/music screens contain sample labels and animations; a screen is not proof of a working network/time/media service.

README toolchains: IDF 5.5.5/6.0.2, Arduino-ESP32 3.3.11. Examined release v1.0.1 uses IDF 5.5.4/6.0.2 and Arduino 3.3.10. Current manifests request LVGL 9.5.0/BSP 2.x; the LVGL example's older lock contains 9.2.0/1.0.3. Do not treat these as one reproduced build.

Reviewed partition CSVs use small factory app regions, not dual OTA slots. The recovery listing is a separate artifact: 29,360,128 bytes; Git blob SHA `8b45c9ad11bde610a8b7a01520855fd1ab5b49d4`. That is remote metadata, not a checksum of downloaded recovery bytes.

## Review limits

Read all 14 entrypoints (shared Arduino scaffolding compared by diff), relevant configs/dependencies, the complete board BSP implementation, its API/pin declarations, QSPI configuration, and the app lifecycle/snapshot path. Selected driver APIs/implementation resolved specific questions. Additional framework code was saved, not claimed as exhaustively read.

This file preserves source-review findings. On-device tests and local schematic inspection were subsequently completed; see [Hardware probe results](HARDWARE-PROBE-RESULTS.md). No factory flash dump, recovery binary analysis, or factory restore test was required. The original factory application identified itself as `phone_s3_box_3`, ESP-IDF 5.5.1-dirty; the published examples are not proven to be the exact source of that binary.
