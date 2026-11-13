# Bluetooth: Myo armband → microcontroller → Pololu 3pi+ 2040

A BLE bridge board connects to the Myo armband, streams raw EMG, and forwards
each sample to the Pololu 3pi+ 2040 over UART.

```
Myo armband ──BLE──> bridge MCU (ESP32 or nRF52840) ──UART 230400──> Pololu 3pi+ 2040
```

Each bridge MCU has its own sketch. Both send identical UART output, so the
3pi+ side doesn't care which one is used.

| Folder | Board | Status (2026-11-12) |
|---|---|---|
| `nrf52840/` | SparkFun Pro nRF52840 Mini (DEV-15025) | Working on hardware |
| `esp32/` | ESP32 dev board (ESP-WROOM-32, "ESP32 Dev Module") | Compiles; not yet run on hardware (first board was defective, see Tools) |
| `tools/` | Windows PowerShell diagnostics for USB-serial boards | — |

## UART protocol (bridge → 3pi+)

- 230400 baud, 8N1, 3.3 V logic, common GND required.
- One ASCII line per EMG sample: `seq,e0,e1,e2,e3,e4,e5,e6,e7\n`
  - `seq`: uint16 sample counter, wraps 65535 → 0 (gaps = dropped samples)
  - `e0..e7`: int8 raw EMG, one per Myo electrode
- Myo is set to raw EMG (mode `0x03`), IMU off, classifier off, never sleep.
  Raw EMG arrives as BLE notifications on 4 characteristics, 2 samples per
  notification, ~200 samples/s total.
- USB `Serial` on the bridge prints debug text only (connection status and a
  `rate=N/s` line every second).

## nRF52840 (SparkFun Pro nRF52840 Mini)

Sketch: `nrf52840/controla_myo_uart_nrf52840/` — uses the Adafruit nRF52 core's
Bluefruit library as a BLE central (sparthan-myo is ESP32-only).

**Wiring:** Mini pin 17 (Serial1 TX) → 3pi+ RX · Mini pin 15 (Serial1 RX) ← 3pi+ TX (optional) · GND ↔ GND.
Power from USB or VIN (3.3–5.5 V).

**One-time IDE setup** (the Adafruit core has no SparkFun Mini entry):
1. Arduino IDE → Preferences → Additional Boards Manager URLs, add
   `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`,
   then Boards Manager → install **Adafruit nRF52** (tested with 1.7.0).
2. Copy the contents of `nrf52840/board_support/` into the installed core folder,
   `%LOCALAPPDATA%\Arduino15\packages\adafruit\hardware\nrf52\<version>\`:
   - `boards.local.txt` → core folder (new file; the IDE merges it with `boards.txt`)
   - `variants/sparkfun_nrf52840_mini/` → core's `variants/` folder
   Both are add-only. Redo this after updating the core (new version folder).
3. Restart the IDE. Board: **Tools → Board → Adafruit nRF52 → SparkFun Pro nRF52840 Mini**
   (FQBN `adafruit:nrf52:sparkfunnrf52840mini`).

`boards.local.txt` is based on the core 1.7.0 Feather nRF52840 entry (same chip,
S140 6.1.1) with SparkFun's name, USB IDs and variant. The variant files are
SparkFun's, from
[sparkfun/nRF52840_Breakout_MDBT50Q](https://github.com/sparkfun/nRF52840_Breakout_MDBT50Q/tree/master/Firmware/Arduino)
(LGPL-2.1 headers retained).

**Upload:** pick the Mini's own COM port (not an Intel AMT "SOL" port) and Upload.
If the port isn't found, double-tap reset (blue LED pulses = bootloader) and pick
the port that appears. Use a micro-USB *data* cable — with a charge-only cable the
LED lights but Windows sees nothing.

## ESP32

Sketch: `esp32/controla_myo_uart/` — based on the sparthan-myo `myo_emg_plot` example.

- Arduino IDE board: **ESP32 Dev Module**, Partition Scheme **Huge APP**
  (esp32 core 2.0.17; sketch is ~1.12 MB, 35% of Huge APP).
- Library: [project-sparthan/sparthan-myo](https://github.com/project-sparthan/sparthan-myo)
  (MIT), unmodified — install into `Documents/Arduino/libraries/`.
- **Wiring:** GPIO17 (Serial1 TX) → 3pi+ RX · GPIO16 (Serial1 RX) ← 3pi+ TX (optional) · GND ↔ GND.

## Tools (Windows)

Run from PowerShell with `powershell -ExecutionPolicy Bypass -File <script> ...`;
close the Arduino IDE / Serial Monitor first so the port is free.

- `tools/serial_loopback_probe.ps1 -Port COMx` — checks a USB-serial board for a
  TX→RX echo. The first ESP32 board failed every upload with esptool's
  *"Serial data stream stopped: Possible serial noise or corruption"*; `esptool --trace`
  showed each SYNC packet echoed back byte-for-byte and the ESP32 never replying,
  and the echo stayed with EN held (chip in reset): a board-level short on the
  CH340 side. Healthy board with EN held: 0 bytes back.
- `tools/watch_usb.ps1 -Seconds 60` — prints devices Windows adds/removes while you
  plug a board in; tells a bad/charge-only cable apart from a driver problem.
