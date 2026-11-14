# Waveshare ESP32-S3-Touch-LCD-7B

This is an ESP32-S3/Xtensa target, not STM32. The independent `esp32/` ESP-IDF project compiles the shared `src/` simulation directly. The older `stm32/` scaffold remains for STM32 hardware.

Hardware: ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB octal PSRAM, 1024×600 RGB565 IPS LCD, GT911 touch. This configuration is specifically for **7B**, not the 800×480 7 or 7C. LCD pins, 30 MHz pixel clock, timings and the address-0x24 I/O expander registers follow Waveshare's reference examples at commit `c652c902db607f7ffb376257393cfd7657aa6428`.

## Build environment

Use the prebuilt official `espressif/idf:v5.5.2` Docker image. No local ESP-IDF, Xtensa compiler, Python environment or Arduino installation is required. Run in a normal host terminal where Docker works (not inside an editor's isolated Flatpak terminal).

```sh
cd /home/alex/Projects/turmite-linux
docker version
docker pull espressif/idf:v5.5.2
sh esp32/container-build.sh build
```

The first pull is large and needs several GB of disk space. The launcher preserves host file ownership (including rootless Docker), uses SELinux shared mount labels, and serializes operations in the same build directory. It mounts the shared source read-only and `esp32/` writable. It never needs `--privileged` or a mounted Docker socket. The image is version-tag pinned, not digest pinned; `TURMITE_IDF_IMAGE` can select a tested digest.

Firmware and flash arguments are generated in `esp32/build/`:

- `turmite_7b.bin`: application firmware.
- `bootloader/bootloader.bin`: bootloader.
- `partition_table/partition-table.bin`: partition table.
- `flash_args` and `flasher_args.json`: the generated offsets and image list. Use these rather than guessing addresses.

Do not flash only the application for the first installation; the bootloader and partition table must match. There is no OTA, secure boot or flash encryption configuration in this first target.

## Configure the application

```sh
sh esp32/container-build.sh menuconfig
```

Open **Turmite Waveshare 7B**. Defaults: 3 ants, divisor 250, cell size 3, 20 FPS maximum, five-minute universe lifetime. Cell size 3 creates a 341×200 simulation grid scaled across the panel, with slight rounding at the horizontal edge. Colors use the shared randomish generator, with color zero forced to black. This is an IPS LCD, so black does not turn off the panel backlight.

Two FreeRTOS workers, one per core, use the shared weighted scheduler with bounded 64-instruction quanta and a one-tick yield. RGB565 staging and scanout buffers live in PSRAM; the RGB DMA driver uses internal bounce buffers. The initial framebuffer-copy path can tear during continuous scanout. FPS is a ceiling, not a measured hardware guarantee.

Touch controls:

- Tap and release: pause/resume.
- Hold for at least one second and release: new universe and palette.
- Missing GT911: log a warning and continue without touch.

Settings are build-time options, not an on-device settings screen. Wi-Fi, Bluetooth, SD, CAN and RS485 are not started. No calibration, networking or touch GUI is required to run the animation.

## Connect and back up the board

These steps require the physical device. Building alone does not change its firmware.

1. Connect a data-capable USB cable to the board's **USB TO UART** Type-C port. Use the UART selection switch position that connects the onboard USB-UART bridge to the ESP32. See the board diagram in Waveshare's documentation. Use stable 5 V power; avoid simultaneous improvised power connections.
2. Identify the device on the host:

   ```sh
   ls -l /dev/serial/by-id/
   ```

   Select the newly appeared USB-UART entry. Prefer that stable name to guessing `/dev/ttyUSB0`. If no entry appears, inspect `lsusb` and reconnect/check the data cable.
3. Set the port for subsequent commands:

   ```sh
   export TURMITE_ESP32_PORT=/dev/serial/by-id/YOUR_BOARD_USB_UART
   ```

4. Before replacing factory firmware, save a full 16 MB backup:

   ```sh
   sh esp32/container-build.sh backup
   ```

   Keep the resulting `esp32/backups/flash-TIMESTAMP.bin` somewhere safe. Backups are excluded from Git. This command reads flash but may reset the board into its bootloader. Confirm it finishes successfully before flashing.

If the port is busy, close serial monitors. If permissions fail, check `ls -l` on the resolved device and your host serial-device group membership. Group changes require a new login. Rootless Docker may require additional host device-access configuration; do not solve it by opening the entire container with privileged mode.

## Flash and observe

Once ready to replace the current application:

```sh
sh esp32/container-build.sh flash monitor
```

This writes the generated bootloader, partition table and application. The launcher maps only the selected serial device into Docker. Quit the monitor with **Ctrl+]**. If automatic download mode fails, hold **BOOT**, press and release **RESET**, then release **BOOT** and retry flashing. No full-chip erase is needed.

Expected serial messages include successful PSRAM initialization, `1024x600 RGB565 LCD initialized`, and a universe seed/grid/ant count. The screen should show colored turmite trails on black. Test tap/hold, a five-minute restart, and power cycling. Watch for repeated resets, watchdog reports or allocation failures.

If the LCD stays blank, first check serial boot messages, confirm the model is 7B/N16R8, and check power/cable seating. If the image shifts or tears, retain the log and report whether the behavior occurs at startup or under animation load. Do not substitute pinouts from the older 7-inch board.

## Implementation and validation

`esp32/main/board_7b.c` owns the panel, I/O expander and touch I2C bus. `main.c` owns the simulation lifecycle and RGB565 conversion. Small `ESP_PLATFORM` branches use ESP32 entropy and adapt pthread condition-variable deadlines; Linux and Android keep their existing paths. The actual workers use the scheduler's nonblocking group-selection API.

Validated on 2026-11-14:

- `sh esp32/container-build.sh build size` cross-compiled and linked with ESP-IDF 5.5.2; no compiler warnings/errors were reported.
- Application binary: 257,776 bytes (about 252 KiB), within the 4 MiB application partition. Bootloader and partition table were generated too. Runtime framebuffer/heap allocations are additional to the static memory report.
- `sh esp32/tests/run_host_tests.sh`: seven launcher tests and the ESP scheduler regression passed. The latter simulates IDF's `ENOSYS` condition attributes and checks that timed waits do not spin.
- Existing core, spawning, scheduler-deadline and worker-pool regression tests passed.

The tested Docker image digest is `sha256:05cbfc42ed2e987b8026722c15bf1d8523d3e4fd1b4ac04d2e4056f5e0918b99`. Hardware execution is not yet verified: no device has been flashed, and panel timing, touch response, CPU/frame rate and power stability still need physical-board testing.

Sources:

- [Waveshare product and pinout](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B)
- [Waveshare ESP-IDF guide](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B/ESP-IDF)
- [Pinned Waveshare examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B/tree/c652c902db607f7ffb376257393cfd7657aa6428/examples/ESP-IDF)
- [Official ESP-IDF Docker environment](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-guides/tools/idf-docker-image.html)
