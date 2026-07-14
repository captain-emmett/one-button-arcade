# One Button Arcade

An Arduino game for an **Adafruit QT Py ESP32-S3**, a **128x64 I2C SSD1306 OLED**, and one normally-open pushbutton.

## What it does

- Normally shows the total number of debounced button presses. The count is stored in the ESP32-S3's non-volatile storage after every press, so it survives resets and unplugging.
- Four quick presses start a 10-second speed round.
- The screen shows live presses per second and remembers the fastest rolling rate from the round.
- The five fastest scores survive power loss.
- A qualifying player enters three initials with the one button: letters cycle automatically; tap to lock each letter.
- Hold the button for about 1.3 seconds from the counter screen to view the high scores. That initial press still counts toward the lifetime total.

## Wiring

### Pushbutton

| QT Py ESP32-S3 | Connect to |
|---|---|
| `A0` | One button leg |
| `GND` | Other button leg |

No resistor is required: the sketch enables the ESP32-S3's internal pull-up resistor.

### OLED (STEMMA QT / Qwiic)

Plug the display into the QT Py's STEMMA QT connector. If the OLED has loose pins instead of a connector, wire them as follows:

| OLED | QT Py ESP32-S3 STEMMA QT |
|---|---|
| `VCC` | `3V` |
| `GND` | `GND` |
| `SDA` | `SDA1` (GPIO 41) |
| `SCL` | `SCL1` (GPIO 40) |

This Adafruit 128x64 SSD1306 display was detected at I2C address `0x3D`, which is configured near the top of `OneButtonArcade.ino`. The startup diagnostics automatically probe and report the alternate `0x3C` address if a different SSD1306 module is connected later.

## Arduino IDE setup

1. Install the **esp32 by Espressif Systems** board package in Boards Manager.
2. Install **Adafruit GFX Library** and **Adafruit SSD1306** in Library Manager. (Adafruit BusIO may be installed automatically.)
3. Open `OneButtonArcade/OneButtonArcade.ino`.
4. Select **Tools > Board > ESP32 > Adafruit QT Py ESP32-S3 No PSRAM**.
5. Select **Tools > USB Mode > Hardware CDC and JTAG**.
6. Select **Tools > USB CDC On Boot > Enabled**.
7. Select **Tools > Upload Mode > UART0 / Hardware CDC**.
8. Select the board's USB port and click **Upload**.
9. Open Serial Monitor at **115200 baud**. If the port changes after uploading, select the new port and press Reset once.

The ESP32 `Preferences` library is included with the ESP32 board package; it does not need a separate install.

At startup, Serial Monitor reports the restored lifetime count, STEMMA QT bus status, whether an I2C device answers at `0x3D`, and whether the lifetime-counter screen was drawn. It then prints a `HEARTBEAT` line every five seconds. This makes display wiring, address, and startup problems visible even when the OLED remains blank or Serial Monitor was opened late.

## Playing

1. Tap normally to increase the lifetime counter.
2. Press four times quickly (each less than 350 ms apart) to begin a speed round.
3. Mash for up to 10 seconds. The round also finishes after two seconds without a press.
4. On a new top-five score, wait for the desired letter to appear and tap to lock it. Repeat for all three initials.
