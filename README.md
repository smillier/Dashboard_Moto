# Dashboard_CBR

ESP-IDF (`idf.py`, target `esp32c6`) firmware for a single **ESP32-C6
SuperMini** board that runs three independent BLE-central roles at once:

1. Connects to a **RaceBox Micro** and starts/stops its standalone
   recording via the board's BOOT button.
2. Connects to a **GoPro** (Open GoPro BLE protocol) and controls it via 4
   physical buttons (shutter / power / mode / reset).
3. Connects to a standard **BLE Heart Rate Monitor** (chest strap / sports
   watch, GATT Heart Rate service `0x180D`) and shows the live bpm reading.

RaceBox, GoPro and heart-rate-monitor connection status is shown on a
**3-pixel WS2812 RGB bar** (pixel 0 = RaceBox, pixel 1 = GoPro, pixel 2 =
heart rate monitor); the heart rate reading itself is shown as large digits
(with small top-strip connection icons for all three devices) on a
**0.96" SSD1306 128x64 I2C OLED**.

This merges [ESP32C6_RaceBox](https://github.com/smillier/ESP32C6_RaceBox)
and [GoPro_ESP32C6_Remote](https://github.com/smillier/GoPro_ESP32C6_Remote)
into one firmware. Both were already central-only NimBLE apps with no
overlapping GPIOs, so the only real merge work was combining their two
independent BLE scan loops into one shared scanner (only one `ble_gap_disc`
can run at a time) and moving from a single-pixel status LED to a 3-pixel
bar.

## Hardware

| Function                            | GPIO |
|---------------------------------------|------|
| 3-LED WS2812 RGB bar (status)          | 8    |
| RaceBox recording toggle (BOOT button) | 9    |
| GoPro shutter (start/stop)             | 2    |
| GoPro power / sleep                    | 3    |
| GoPro mode (Video/Photo/Timelapse)     | 4    |
| GoPro reset (`esp_restart`)            | 18   |
| OLED SDA (heart rate display)          | 20   |
| OLED SCL (heart rate display)          | 19   |

The RGB bar's data line replaces the SuperMini's onboard single WS2812 pad
(GPIO8): it's the same protocol, just 3 pixels chained instead of 1. The
OLED is wired to this board's silkscreen-labeled I2C pins (SDA=GPIO20,
SCL=GPIO19) - I2C only needs 2 GPIOs, so no IO expander is needed even
though the board is otherwise fairly pin-constrained. All pins are
collision-free. Adjust `kRgbLedPin`/`kRgbLedCount`/`kOledSdaPin`/
`kOledSclPin`/`kOledI2cAddr` in [`main/config.h`](main/config.h) if your
wiring differs (some SSD1306 clones use I2C address `0x3D` instead of the
default `0x3C` - if the display stays blank, that's the first thing to
check).

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
native USB-Serial-JTAG console. The OLED driver uses ESP-IDF's built-in I2C
master driver (`driver` component), no extra dependency needed.

## How it works

- On boot, `ble_manager` starts a single continuous BLE scan. Any
  advertisement is checked against `racebox_ble::matches()` (name prefix
  `RaceBox Micro`), `gopro_ble_matches()` (service `0xFEA6`) and
  `heart_rate_ble_matches()` (service `0x180D`); whichever matches gets
  connected, and that connection's GAP events (GATT discovery,
  subscription, bonding, disconnect) are then handled entirely by that
  device's own module. Only one scan can run at a time, so it's cancelled
  the instant any advertisement matches; it's then resumed immediately once
  that connection attempt finishes (success or failure), so the other two
  peers keep being searched for independently of whether this one is
  present. The scan resumes automatically whenever a connection attempt
  finishes, fails, or a device disconnects, and stops once all three are
  connected - so any subset of the three devices being absent or slow to
  appear never blocks the others from connecting.
- **RaceBox**: once connected, subscribes to the UART service and queries
  recording status (`0xFF 0x22`), unlocking memory security if configured
  (`0xFF 0x30`). The BOOT button toggles recording (`0xFF 0x25`); state
  changes (from either the button or the RaceBox's own physical button)
  arrive via `0xFF 0x26`.
- **GoPro**: once connected, discovers the `0xFEA6` service, subscribes to
  its notify characteristics, then requests bonding (Legacy Pairing - LE
  Secure Connections was confirmed to make this camera terminate the
  connection on security request). Once encrypted, the 4 buttons send
  shutter/sleep/preset-group commands.
- **Heart rate monitor**: once connected, discovers the standard Heart Rate
  service (`0x180D`) and subscribes to the Heart Rate Measurement
  characteristic (`0x2A37`) - no bonding needed. Each notification is
  parsed per the GATT Heart Rate Measurement format (bpm value, either
  8-bit or 16-bit depending on a flag byte); energy-expended and
  RR-interval fields, if present, are parsed past but not used. Each new
  bpm value is also logged (`heart_rate_ble: Heart rate: N bpm`) so it can
  be watched live over `idf.py monitor` without needing the OLED.

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

**Pixel 2 - Heart rate monitor:**

| State                                | Color / pattern |
|----------------------------------------|------------------|
| Searching for the heart rate monitor   | Blue, slow blink |
| Connected                              | Solid green      |

## Heart rate OLED

Top strip: three small connection-status icons (RaceBox, GoPro, heart rate
monitor, left to right), each with a status dot below it - solid when
connected, slow-blinking while scanning, fast-blinking while connecting
(mirrors the LED bar's blink semantics). Below that: large centered digits
showing the current bpm reading, or `--` if no heart rate monitor is
connected yet (or no valid notification has arrived since connecting).
Refreshes when a value or connection state changes, with a 100ms safety
refresh floor so blinking icons stay smooth and nothing stays stale.

## Button behavior

- **RaceBox toggle (GPIO9, BOOT)**: starts standalone recording if stopped,
  stops it otherwise. The filter parameters (stationary, no-fix,
  auto-shutdown) follow the setup recommended by the RaceBox documentation
  and are adjustable in `config.h`.
- **GoPro shutter (GPIO2)**: ignored until the GoPro is ready (pixel 1
  green). Toggles start/stop on each press.
- **GoPro power (GPIO3)**: connected -> sends `Sleep` (0x05), camera
  disconnects. Not connected -> no-op; the shared scan is already running
  continuously and will find/wake the camera as soon as it advertises (a
  sleeping GoPro keeps advertising for 8h, or once put back into pairing
  mode from its screen).
- **GoPro mode (GPIO4)**: cycles Video -> Photo -> Timelapse -> ...
  (`Load Preset Group`, 0x3E). Ignored until ready; the GoPro rejects this
  while recording (normal camera behavior).
- **GoPro reset (GPIO18)**: restarts the ESP32 (`esp_restart()`).

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
- Only one BLE scan can run at a time, so it's briefly cancelled while each
  connection attempt is in progress - but it resumes immediately once that
  attempt finishes (success or failure), so all three devices still connect
  independently of each other and of the order they advertise in (see
  "How it works" above).
