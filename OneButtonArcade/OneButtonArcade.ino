/*
  One Button Arcade
  Adafruit QT Py ESP32-S3 + 128x64 I2C SSD1306 OLED + one pushbutton

  Button wiring: A0 ---- pushbutton ---- GND
  OLED wiring:   use the board's STEMMA QT connector (3V, GND, SDA, SCL)

  Required libraries (Arduino Library Manager):
    Adafruit GFX Library
    Adafruit SSD1306
*/

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <Wire.h>

// ---------- Hardware ----------
constexpr uint8_t BUTTON_PIN = A0;
constexpr uint8_t OLED_ADDRESS = 0x3D;
constexpr int8_t OLED_RESET_PIN = -1;
constexpr uint8_t OLED_SDA_PIN = 41;  // STEMMA QT SDA1
constexpr uint8_t OLED_SCL_PIN = 40;  // STEMMA QT SCL1
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET_PIN);
Preferences preferences;

// ---------- Game tuning ----------
constexpr uint16_t DEBOUNCE_MS = 30;
constexpr uint16_t LONG_HOLD_MS = 1300;
constexpr uint16_t RAPID_INTERVAL_MS = 350;
constexpr uint8_t RAPID_PRESSES_TO_START = 4;
constexpr uint32_t ROUND_LENGTH_MS = 10000;
constexpr uint16_t ROUND_EARLY_END_IDLE_MS = 2000;
constexpr uint16_t ROUND_MINIMUM_MS = 3000;
constexpr uint16_t INITIALS_STEP_MS = 450;
constexpr uint16_t RESULT_SCREEN_MS = 4500;
constexpr uint16_t LEADERBOARD_SCREEN_MS = 6000;
constexpr uint16_t SERIAL_HEARTBEAT_MS = 5000;
constexpr uint8_t SCORE_COUNT = 5;
constexpr uint8_t HISTORY_SIZE = 20;

enum class ScreenMode : uint8_t {
  COUNTER,
  SPEED_ROUND,
  INITIALS,
  RESULT,
  LEADERBOARD,
  DISPLAY_ERROR
};

struct HighScore {
  char initials[4];
  float pressesPerSecond;
};

ScreenMode mode = ScreenMode::COUNTER;
HighScore highScores[SCORE_COUNT];

uint64_t lifetimePresses = 0;
uint32_t pressHistory[HISTORY_SIZE] = {};
uint8_t historyCount = 0;

bool rawButtonDown = false;
bool stableButtonDown = false;
bool longHoldHandled = false;
uint32_t rawButtonChangedAt = 0;
uint32_t stableButtonChangedAt = 0;

uint32_t roundStartedAt = 0;
float currentSpeed = 0.0f;
float bestRoundSpeed = 0.0f;

int8_t pendingRank = -1;
char enteredInitials[4] = {'A', 'A', 'A', '\0'};
uint8_t initialsPosition = 0;
uint8_t cyclingLetter = 0;
uint32_t initialsStepAt = 0;

uint32_t screenExpiresAt = 0;
bool displayReady = false;

// ---------- Drawing helpers ----------
void drawCentered(const char *text, int16_t y, uint8_t size = 1) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - static_cast<int16_t>(w)) / 2, y);
  display.print(text);
}

void beginFrame() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
}

void drawCounter() {
  if (!displayReady) return;
  beginFrame();
  drawCentered("LIFETIME PRESSES", 0, 1);

  char countText[24];
  snprintf(countText, sizeof(countText), "%llu",
           static_cast<unsigned long long>(lifetimePresses));
  uint8_t size = strlen(countText) <= 5 ? 4 : (strlen(countText) <= 10 ? 2 : 1);
  drawCentered(countText, size == 4 ? 19 : (size == 2 ? 24 : 28), size);

  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  drawCentered("Hold:scores Mash:play", 56, 1);
  display.display();
}

void drawSpeedRound(uint32_t now) {
  if (!displayReady) return;
  beginFrame();
  uint32_t elapsed = now - roundStartedAt;
  float secondsLeft = elapsed >= ROUND_LENGTH_MS
                          ? 0.0f
                          : (ROUND_LENGTH_MS - elapsed) / 1000.0f;

  char line[24];
  snprintf(line, sizeof(line), "SPEED ROUND  %4.1fs", secondsLeft);
  drawCentered(line, 0, 1);

  snprintf(line, sizeof(line), "%.1f", currentSpeed);
  drawCentered(line, 15, 3);
  drawCentered("PRESSES / SECOND", 41, 1);

  snprintf(line, sizeof(line), "BEST %.1f", bestRoundSpeed);
  drawCentered(line, 54, 1);
  display.display();
}

void drawInitials(uint32_t now) {
  if (!displayReady) return;
  beginFrame();
  drawCentered("NEW HIGH SCORE!", 0, 1);

  char line[24];
  snprintf(line, sizeof(line), "#%d   %.1f PPS", pendingRank + 1, bestRoundSpeed);
  drawCentered(line, 11, 1);

  char shown[4];
  memcpy(shown, enteredInitials, sizeof(shown));
  shown[initialsPosition] = static_cast<char>('A' + cyclingLetter);
  drawCentered(shown, 27, 3);

  int16_t charWidth = 18;
  int16_t textLeft = (SCREEN_WIDTH - (3 * charWidth)) / 2;
  int16_t underlineX = textLeft + initialsPosition * charWidth;
  display.drawLine(underlineX, 52, underlineX + 14, 52, SSD1306_WHITE);
  drawCentered("Tap to lock letter", 56, 1);
  display.display();
}

void drawResult() {
  if (!displayReady) return;
  beginFrame();
  drawCentered("ROUND OVER", 0, 1);
  char line[24];
  snprintf(line, sizeof(line), "%.1f", bestRoundSpeed);
  drawCentered(line, 16, 3);
  drawCentered("PRESSES / SECOND", 42, 1);
  drawCentered("Keep mashing to retry!", 56, 1);
  display.display();
}

void drawLeaderboard() {
  if (!displayReady) return;
  beginFrame();
  drawCentered("HIGH SCORES (PPS)", 0, 1);
  for (uint8_t i = 0; i < SCORE_COUNT; ++i) {
    display.setCursor(8, 12 + i * 10);
    display.print(i + 1);
    display.print(". ");
    display.print(highScores[i].initials);
    display.setCursor(78, 12 + i * 10);
    if (highScores[i].pressesPerSecond > 0.0f) {
      display.print(highScores[i].pressesPerSecond, 1);
    } else {
      display.print("---");
    }
  }
  display.display();
}

// ---------- Persistent storage ----------
void loadPersistentData() {
  preferences.begin("onebutton", false);
  lifetimePresses = preferences.getULong64("presses", 0);

  for (uint8_t i = 0; i < SCORE_COUNT; ++i) {
    char scoreKey[8];
    char nameKey[8];
    snprintf(scoreKey, sizeof(scoreKey), "score%u", i);
    snprintf(nameKey, sizeof(nameKey), "name%u", i);

    highScores[i].pressesPerSecond = preferences.getFloat(scoreKey, 0.0f);
    String savedName = preferences.getString(nameKey, "---");
    if (savedName.length() != 3) savedName = "---";
    for (uint8_t j = 0; j < 3; ++j) highScores[i].initials[j] = savedName[j];
    highScores[i].initials[3] = '\0';
  }
}

void saveLifetimeCount() {
  // NVS is wear-levelled. Saving each debounced press makes the displayed lifetime
  // count survive an unexpected unplug with no deliberate batching loss.
  preferences.putULong64("presses", lifetimePresses);
}

void saveHighScores() {
  for (uint8_t i = 0; i < SCORE_COUNT; ++i) {
    char scoreKey[8];
    char nameKey[8];
    snprintf(scoreKey, sizeof(scoreKey), "score%u", i);
    snprintf(nameKey, sizeof(nameKey), "name%u", i);
    preferences.putFloat(scoreKey, highScores[i].pressesPerSecond);
    preferences.putString(nameKey, highScores[i].initials);
  }
}

// ---------- Press history and speed ----------
void recordPressTime(uint32_t now) {
  if (historyCount < HISTORY_SIZE) {
    pressHistory[historyCount++] = now;
    return;
  }
  memmove(pressHistory, pressHistory + 1, (HISTORY_SIZE - 1) * sizeof(uint32_t));
  pressHistory[HISTORY_SIZE - 1] = now;
}

bool rapidStartDetected() {
  if (historyCount < RAPID_PRESSES_TO_START) return false;
  uint8_t first = historyCount - RAPID_PRESSES_TO_START;
  for (uint8_t i = first + 1; i < historyCount; ++i) {
    if (pressHistory[i] - pressHistory[i - 1] > RAPID_INTERVAL_MS) return false;
  }
  return true;
}

float calculateRecentSpeed(uint32_t now) {
  if (historyCount < 2) return 0.0f;

  // Use up to the most recent 8 presses and at most a two-second window.
  uint8_t newest = historyCount - 1;
  uint8_t oldest = newest;
  while (oldest > 0 && newest - oldest < 7 &&
         now - pressHistory[oldest - 1] <= 2000) {
    --oldest;
  }

  uint8_t presses = newest - oldest + 1;
  uint32_t span = pressHistory[newest] - pressHistory[oldest];
  if (presses < 2 || span == 0) return 0.0f;
  return (presses - 1) * 1000.0f / span;
}

void startSpeedRound(uint32_t now) {
  mode = ScreenMode::SPEED_ROUND;
  roundStartedAt = now;
  currentSpeed = calculateRecentSpeed(now);
  bestRoundSpeed = currentSpeed;
  drawSpeedRound(now);
}

int8_t rankForScore(float score) {
  for (uint8_t i = 0; i < SCORE_COUNT; ++i) {
    if (score > highScores[i].pressesPerSecond) return i;
  }
  return -1;
}

void beginInitialsEntry(int8_t rank, uint32_t now) {
  pendingRank = rank;
  strcpy(enteredInitials, "AAA");
  initialsPosition = 0;
  cyclingLetter = 0;
  initialsStepAt = now;
  mode = ScreenMode::INITIALS;
  drawInitials(now);
}

void finishRound(uint32_t now) {
  int8_t rank = rankForScore(bestRoundSpeed);
  if (rank >= 0) {
    beginInitialsEntry(rank, now);
  } else {
    mode = ScreenMode::RESULT;
    screenExpiresAt = now + RESULT_SCREEN_MS;
    drawResult();
  }
}

void lockInitial(uint32_t now) {
  enteredInitials[initialsPosition] = static_cast<char>('A' + cyclingLetter);
  ++initialsPosition;

  if (initialsPosition < 3) {
    cyclingLetter = 0;
    initialsStepAt = now;
    drawInitials(now);
    return;
  }

  for (int8_t i = SCORE_COUNT - 1; i > pendingRank; --i) {
    highScores[i] = highScores[i - 1];
  }
  strcpy(highScores[pendingRank].initials, enteredInitials);
  highScores[pendingRank].pressesPerSecond = bestRoundSpeed;
  saveHighScores();

  mode = ScreenMode::LEADERBOARD;
  screenExpiresAt = now + LEADERBOARD_SCREEN_MS;
  drawLeaderboard();
}

// ---------- Button events ----------
void onButtonPressed(uint32_t now) {
  ++lifetimePresses;
  saveLifetimeCount();
  recordPressTime(now);

  switch (mode) {
    case ScreenMode::COUNTER:
      if (rapidStartDetected()) {
        startSpeedRound(now);
      } else {
        drawCounter();
      }
      break;

    case ScreenMode::SPEED_ROUND:
      currentSpeed = calculateRecentSpeed(now);
      if (currentSpeed > bestRoundSpeed && historyCount >= 4) {
        bestRoundSpeed = currentSpeed;
      }
      drawSpeedRound(now);
      break;

    case ScreenMode::INITIALS:
      lockInitial(now);
      break;

    case ScreenMode::RESULT:
    case ScreenMode::LEADERBOARD:
      mode = ScreenMode::COUNTER;
      drawCounter();
      break;

    case ScreenMode::DISPLAY_ERROR:
      break;
  }
}

void updateButton(uint32_t now) {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down != rawButtonDown) {
    rawButtonDown = down;
    rawButtonChangedAt = now;
  }

  if (rawButtonDown != stableButtonDown && now - rawButtonChangedAt >= DEBOUNCE_MS) {
    stableButtonDown = rawButtonDown;
    stableButtonChangedAt = now;
    if (stableButtonDown) {
      longHoldHandled = false;
      onButtonPressed(now);
    }
  }

  if (stableButtonDown && !longHoldHandled && mode == ScreenMode::COUNTER &&
      now - stableButtonChangedAt >= LONG_HOLD_MS) {
    longHoldHandled = true;
    mode = ScreenMode::LEADERBOARD;
    screenExpiresAt = now + LEADERBOARD_SCREEN_MS;
    drawLeaderboard();
  }
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);

  // Native USB serial may take a moment to reconnect after an upload or reset.
  // Wait briefly so the boot banner is not sent before Serial Monitor is ready.
  uint32_t serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 2500) {
    delay(10);
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("One Button Arcade is booting");
  Serial.println("================================");
  Serial.printf("Button: A0 (active LOW)\n");
  Serial.printf("OLED: SSD1306 128x64 at 0x%02X\n", OLED_ADDRESS);
  Serial.printf("STEMMA QT: SDA GPIO %u, SCL GPIO %u\n", OLED_SDA_PIN,
                OLED_SCL_PIN);

  loadPersistentData();
  Serial.printf("Restored lifetime count: %llu\n",
                static_cast<unsigned long long>(lifetimePresses));

  bool i2cStarted = Wire1.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Serial.printf("STEMMA QT I2C bus: %s\n", i2cStarted ? "started" : "FAILED");

  uint8_t probeResult = 4;  // Arduino Wire: other/unknown error.
  if (i2cStarted) {
    Wire1.beginTransmission(OLED_ADDRESS);
    probeResult = Wire1.endTransmission();
  }

  if (probeResult == 0) {
    Serial.printf("I2C probe: device found at 0x%02X\n", OLED_ADDRESS);
  } else {
    Serial.printf("I2C probe: NO device at 0x%02X (Wire error %u)\n",
                  OLED_ADDRESS, probeResult);

    // Some SSD1306 modules use the alternate address. Report it to make a
    // mismatched-address problem immediately visible in Serial Monitor.
    if (i2cStarted) {
      constexpr uint8_t alternateAddress = OLED_ADDRESS == 0x3C ? 0x3D : 0x3C;
      Wire1.beginTransmission(alternateAddress);
      uint8_t alternateResult = Wire1.endTransmission();
      if (alternateResult == 0) {
        Serial.printf("I2C probe: device found at 0x%02X; change OLED_ADDRESS to 0x%02X\n",
                      alternateAddress, alternateAddress);
      }
    }
  }

  // Wire1 is already configured for the QT connector above. Passing false for
  // periphBegin prevents the display library from calling Wire1.begin() again
  // without the QT-specific pins.
  displayReady = i2cStarted && probeResult == 0 &&
                 display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
  if (!displayReady) {
    mode = ScreenMode::DISPLAY_ERROR;
    Serial.println("ERROR: SSD1306 startup failed. Check the I2C messages above.");
    return;
  }

  display.cp437(true);
  drawCounter();
  Serial.println("OLED initialized and lifetime counter drawn.");
  Serial.println("Boot complete. Ready for button presses.");
}

void loop() {
  uint32_t now = millis();
  updateButton(now);

  // Keep proving that the sketch is alive even if Serial Monitor was opened
  // after the one-time boot banner had already been sent.
  static uint32_t lastHeartbeatAt = 0;
  if (now - lastHeartbeatAt >= SERIAL_HEARTBEAT_MS) {
    lastHeartbeatAt = now;
    Serial.printf("HEARTBEAT: alive, lifetime=%llu, OLED=%s\n",
                  static_cast<unsigned long long>(lifetimePresses),
                  displayReady ? "ready" : "error");
  }

  switch (mode) {
    case ScreenMode::SPEED_ROUND: {
      uint32_t elapsed = now - roundStartedAt;
      bool timedOut = elapsed >= ROUND_LENGTH_MS;
      bool stoppedEarly = elapsed >= ROUND_MINIMUM_MS && historyCount > 0 &&
                          now - pressHistory[historyCount - 1] >= ROUND_EARLY_END_IDLE_MS;
      if (timedOut || stoppedEarly) {
        finishRound(now);
      } else {
        static uint32_t lastSpeedDraw = 0;
        if (now - lastSpeedDraw >= 100) {
          lastSpeedDraw = now;
          drawSpeedRound(now);
        }
      }
      break;
    }

    case ScreenMode::INITIALS:
      if (now - initialsStepAt >= INITIALS_STEP_MS) {
        uint32_t steps = (now - initialsStepAt) / INITIALS_STEP_MS;
        initialsStepAt += steps * INITIALS_STEP_MS;
        cyclingLetter = (cyclingLetter + steps) % 26;
        drawInitials(now);
      }
      break;

    case ScreenMode::RESULT:
    case ScreenMode::LEADERBOARD:
      if (static_cast<int32_t>(now - screenExpiresAt) >= 0) {
        mode = ScreenMode::COUNTER;
        drawCounter();
      }
      break;

    case ScreenMode::COUNTER:
    case ScreenMode::DISPLAY_ERROR:
      break;
  }
}
