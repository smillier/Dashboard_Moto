# Dashboard_CBR

ESP-IDF (`idf.py`, target `esp32c6`) firmware for a single **ESP32-C6
SuperMini** board that runs three independent BLE-central roles at once:

1. Connects to a **RaceBox Micro** and starts/stops its standalone
   recording via the board's BOOT button.
2. Connects to a **GoPro** (Open GoPro BLE protocol) and controls shutter
   start/stop via a physical button.
3. Connects to a standard **BLE Heart Rate Monitor** (chest strap / sports
   watch, GATT Heart Rate service `0x180D`) and shows the live bpm reading.

RaceBox and GoPro status is shown on a **2-pixel WS2812 RGB bar** (pixel 0 =
RaceBox, pixel 1 = GoPro); heart rate is shown as large digits on a
**Waveshare 1.44" LCD HAT (ST7735S, 128x128, 4-wire SPI)**, whose 5-way
joystick and KEY1/2/3 also provide the device's reset button (KEY1) and
inputs reserved for a future on-screen menu.

This merges [ESP32C6_RaceBox](https://github.com/smillier/ESP32C6_RaceBox)
and [GoPro_ESP32C6_Remote](https://github.com/smillier/GoPro_ESP32C6_Remote)
into one firmware. Both were already central-only NimBLE apps with no
overlapping GPIOs, so the only real merge work was combining their two
independent BLE scan loops into one shared scanner (only one `ble_gap_disc`
can run at a time) and moving from a single-pixel status LED to a 2-pixel
bar.

The GoPro's power/sleep and mode-cycle buttons, and the heart-rate SSD1306
OLED, were dropped in favor of the LCD HAT: the HAT alone needs 14 GPIOs
(SPI + joystick + 3 keys), and the SuperMini doesn't have that many spare
without giving something up - see [Hardware](#hardware) below for the full
pin budget.

## Hardware

| Function                            | GPIO |
|---------------------------------------|------|
| 2-LED WS2812 RGB bar (status)          | 8    |
| RaceBox recording toggle (BOOT button) | 9    |
| GoPro shutter (start/stop)             | 2    |
| LCD SCLK                               | 18   |
| LCD MOSI                               | 19   |
| LCD CS                                 | 20   |
| LCD DC                                 | 21   |
| LCD RST                                | 22   |
| LCD backlight (BL)                     | 23   |
| Joystick UP                            | 0    |
| Joystick DOWN                          | 1    |
| Joystick LEFT                          | 6    |
| Joystick RIGHT                         | 7    |
| Joystick PRESS                         | 14   |
| KEY1 (device reset)                    | 16   |
| KEY2 (reserved)                        | 17   |
| KEY3 (reserved)                        | 3    |

The RGB bar's data line replaces the SuperMini's onboard single WS2812 pad
(GPIO8): it's the same protocol, just 2 pixels chained instead of 1.

The LCD is a [Waveshare 1.44" LCD HAT](https://www.waveshare.com/1.44inch-lcd-hat.htm)
- a Raspberry Pi HAT (2x20 header), not a native ESP32 module, so it's
hand-wired here via jumpers rather than plugged in. Its 14 signals (6 SPI/
backlight + 5 joystick + 3 keys) were deliberately assigned to GPIOs that
are *not* strapping pins on this chip (strapping pins: GPIO4/5/8/9/15), so
none of this wiring can glitch boot mode selection even if a button is held
down at power-on - unlike the RaceBox BOOT button and RGB bar above, which
do sit on strapping pins (GPIO9/GPIO8) but were already proven safe by the
original projects this merges. All pins are collision-free. Adjust the `kLcd*`/
`kJoy*`/`kHatKey*` constants in [`main/config.h`](main/config.h) if your
wiring differs.

## Open and build

Native ESP-IDF project (target `esp32c6`), no Arduino/PlatformIO involved.

```bash
. $HOME/esp/esp-idf/export.sh     # or the ESP-IDF VSCode extension
idf.py set-target esp32c6
idf.py build
idf.py -p <port> flash monitor
```

The first build downloads the `espressif/led_strip` component (see
[`main/idf_component.yml`](main/idf_component.yml)) via the ESP-IDF
Component Manager - an internet connection is needed for that first build.
[`sdkconfig.defaults`](sdkconfig.defaults) configures NimBLE (central-only,
3 simultaneous connections), Legacy Pairing bonding for the GoPro, and the
native USB-Serial-JTAG console. The LCD driver uses ESP-IDF's built-in SPI
master driver (`driver` component), no extra dependency needed.

## How it works

- On boot, `ble_manager` starts a single continuous BLE scan. Any
  advertisement is checked against `racebox_ble::matches()` (name prefix
  `RaceBox Micro`), `gopro_ble_matches()` (service `0xFEA6`) and
  `heart_rate_ble_matches()` (service `0x180D`); whichever matches gets
  connected, and that connection's GAP events (GATT discovery,
  subscription, bonding, disconnect) are then handled entirely by that
  device's own module. The scan resumes automatically whenever a connection
  attempt fails or a device disconnects, and stops once all three are
  connected.
- **RaceBox**: once connected, subscribes to the UART service and queries
  recording status (`0xFF 0x22`), unlocking memory security if configured
  (`0xFF 0x30`). The BOOT button toggles recording (`0xFF 0x25`); state
  changes (from either the button or the RaceBox's own physical button)
  arrive via `0xFF 0x26`.
- **GoPro**: once connected, discovers the `0xFEA6` service, subscribes to
  its notify characteristics, then requests bonding (Legacy Pairing - LE
  Secure Connections was confirmed to make this camera terminate the
  connection on security request). Once encrypted, the shutter button sends
  start/stop commands. `gopro_control_power_button()` and
  `gopro_control_cycle_mode()` still exist in
  [`gopro_control.c`](main/gopro_control.c) but aren't wired to any input
  right now - they're the natural place to hook up the LCD HAT's joystick
  or KEY2/KEY3 later.
- **Heart rate monitor**: once connected, discovers the standard Heart Rate
  service (`0x180D`) and subscribes to the Heart Rate Measurement
  characteristic (`0x2A37`) - no bonding needed. Each notification is
  parsed per the GATT Heart Rate Measurement format (bpm value, either
  8-bit or 16-bit depending on a flag byte); energy-expended and
  RR-interval fields, if present, are parsed past but not used.

## LED color code

**Pixel 0 - RaceBox:**

| State                                          | Color / pattern           |
|-------------------------------------------------|----------------------------|
| Looking for the RaceBox (BLE scan)               | Blue, slow blink           |
| Connecting                                       | Cyan, fast blink           |
| Connected, recording stopped                     | Solid green                |
| Recording in progress                            | Solid red                  |
| Recording paused (stationary/no-fix filter)      | Orange, slow blink         |
| Memory locked (unlock failed)                    | Purple, fast blink         |

**Pixel 1 - GoPro:**

| State                     | Color / pattern      |
|----------------------------|------------------------|
| Searching for the GoPro    | Blue, blinking         |
| Connected, bonding/discovery in progress | Solid yellow |
| Ready                      | Solid green            |
| Recording in progress      | Red, blinking (overrides connection color) |

## Heart rate LCD

Large red-on-black centered digits showing the current bpm reading, or `--`
if no heart rate monitor is connected yet (or no valid notification has
arrived since connecting). Refreshes when the value changes, with a 250ms
safety refresh floor so it never stays stale. Same direct-driver approach as
the SSD1306 OLED it replaces (see [`main/lcd_display.cpp`](main/lcd_display.cpp)):
no graphics library, digits are filled rectangles from a small
self-contained 7-segment-style renderer, and each redraw sets an SPI
window and streams solid color rather than keeping a full framebuffer in
RAM. The ST7735S register init sequence and the panel's pixel offset are
ported from Waveshare's own reference driver (`LCD_1in44.c`) rather than
guessed, since this is real hardware bring-up - if the image ever looks
shifted, mirrored, or the colors look off, `kColOffset`/`kRowOffset`/the
`0x36` MADCTL value in `lcd_display.cpp` are the first things to check.

## Button behavior

- **RaceBox toggle (GPIO9, BOOT)**: starts standalone recording if stopped,
  stops it otherwise. The filter parameters (stationary, no-fix,
  auto-shutdown) follow the setup recommended by the RaceBox documentation
  and are adjustable in `config.h`.
- **GoPro shutter (GPIO2)**: ignored until the GoPro is ready (pixel 1
  green). Toggles start/stop on each press.
- **LCD HAT KEY1**: restarts the ESP32 (`esp_restart()`) - replaces the old
  GPIO18 physical reset button.
- **LCD HAT joystick (UP/DOWN/LEFT/RIGHT/PRESS) and KEY2/KEY3**: wired,
  debounced and logged (see [`main/hat_input.cpp`](main/hat_input.cpp)) but not
  bound to any action yet - reserved for a future on-screen menu (GoPro
  power/sleep and mode-cycle are natural candidates, since their BLE calls
  already exist in `gopro_control.c`).

## Deliberate scope limits

- RaceBox: only recording trigger/status, as in the original project. No
  data download (`0xFF 0x23`/`0xFF 0x21`), memory erase (`0xFF 0x24`), or
  GNSS configuration (`0xFF 0x27`) - see the included *RaceBox BLE Protocol
  Description rev 8* PDF for those. Only the Mini S and Micro support
  standalone recording (not the plain Mini); change `kRaceBoxNamePrefix` in
  `config.h` to target a Mini S.
- GoPro: the camera must be put into pairing mode from its screen
  (Preferences -> Connections -> Connect Device) before it advertises
  `0xFEA6` for the first pairing. Bonding keys persist in NVS afterwards.
- Heart rate monitor: only the standard, unencrypted Heart Rate service is
  supported - a strap/watch that mandates pairing/encryption before it'll
  notify on `0x2A37` isn't handled (no bonding is requested for this
  connection). Most consumer chest straps (Polar, Wahoo, etc.) don't
  require this.
- All three devices connect sequentially (whichever advertises first), not
  concurrently - the shared scanner cancels scanning while a connection is
  being established, matching how each original project already behaved
  for its single peer.
