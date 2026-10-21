#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>

// --- OLED Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo dayNightServo;

// --- Pin Definitions ---
const uint8_t BUTTON_PIN = 2;
const uint8_t BUZZER_PIN = 3;
const uint8_t SERVO_PIN  = 9;

// 12 LEDs mapped to Clock Face Hours 1 through 12
// Index 0 = LED 1 (1 o'clock) ... Index 11 = LED 12 (12 o'clock)
const uint8_t NUM_LEDS = 12;
const uint8_t LED_PINS[NUM_LEDS] = {
  4, 5, 6, 7, 8, 10, 11, 12, 13, A0, A1, A2
};

// --- 3-State Operating Modes ---
enum SystemMode {
  MODE_CLOCK,         // 24H Clock + 3-Hand Analog LEDs
  MODE_TIMER_RUNNING, // Stopwatch actively counting up
  MODE_TIMER_HOLD     // Stopwatch stopped: holds final time for 5 seconds
};

SystemMode currentMode = MODE_CLOCK;

// --- Real-Time 24-Hour Clock State (Starts at 06:59:50) ---
uint8_t clockHours   = 6;
uint8_t clockMinutes = 59;
uint8_t clockSeconds = 50;
unsigned long lastClockTick = 0;

// --- Stopwatch / Timer State ---
uint8_t timerHours   = 0;
uint8_t timerMinutes = 0;
uint8_t timerSeconds = 0;
unsigned long lastTimerTick = 0;

// --- 5-Second Hold Timer State ---
unsigned long timerHoldStartTime = 0;
const unsigned long HOLD_DURATION_MS = 5000; // 5 seconds hold

// --- LED Chaser State (Timer Mode) ---
uint8_t chaserIndex = 0;
unsigned long lastChaserTick = 0;
const unsigned long CHASER_INTERVAL_MS = 85;

// --- Pushbutton Debounce State ---
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY_MS = 50;

// --- OLED Refresh Timing ---
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL_MS = 100;

// --- Servo State Tracking ---
int currentServoAngle = -1;

// --- Function Prototypes ---
uint8_t dialPositionToLedIndex(uint8_t dialPos);
void handleButton(unsigned long now);
void updateRealTimeClock(unsigned long now);
void updateStopwatch(unsigned long now);
void updateLEDs(unsigned long now);
void updateDayNightServo();
void renderOLED(unsigned long now);

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  dayNightServo.attach(SERVO_PIN);
  updateDayNightServo();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  renderOLED(millis());
}

void loop() {
  unsigned long now = millis();

  handleButton(now);
  updateRealTimeClock(now); // Always runs in background so clock stays accurate
  updateStopwatch(now);     // Handles counting AND the 5-second hold timeout
  updateDayNightServo();
  updateLEDs(now);

  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    renderOLED(now);
  }
}

// Converts a 0-11 analog dial position (0 = 12 o'clock, 1 = 1 o'clock ... 11 = 11 o'clock)
// into the matching array index for LED_PINS[] (LED 1 is index 0, LED 12 is index 11)
uint8_t dialPositionToLedIndex(uint8_t dialPos) {
  uint8_t wrapped = dialPos % 12;
  return (wrapped == 0) ? 11 : (wrapped - 1);
}

// --- Non-Blocking Pushbutton Handler ---
void handleButton(unsigned long now) {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      // Button pressed (active LOW)
      if (stableButtonState == LOW) {
        if (currentMode == MODE_CLOCK) {
          // 1st Press: Start Stopwatch from 00:00:00
          tone(BUZZER_PIN, 2200, 80);
          timerHours   = 0;
          timerMinutes = 0;
          timerSeconds = 0;
          lastTimerTick = now;
          chaserIndex   = 0;
          currentMode   = MODE_TIMER_RUNNING;
        } 
        else if (currentMode == MODE_TIMER_RUNNING) {
          // 2nd Press: Stop timer and freeze display for 5 seconds
          tone(BUZZER_PIN, 1800, 120);
          timerHoldStartTime = now;
          currentMode        = MODE_TIMER_HOLD;
        } 
        else if (currentMode == MODE_TIMER_HOLD) {
          // Optional: Pressing button during the 5s hold returns to Clock immediately
          tone(BUZZER_PIN, 2500, 60);
          currentMode = MODE_CLOCK;
        }

        renderOLED(now);
      }
    }
  }

  lastButtonReading = reading;
}

// --- Non-Blocking 24-Hour Clock (Always runs in background) ---
void updateRealTimeClock(unsigned long now) {
  if (now - lastClockTick >= 1000) {
    lastClockTick += 1000;
    clockSeconds++;

    if (clockSeconds >= 60) {
      clockSeconds = 0;
      clockMinutes++;
      if (clockMinutes >= 60) {
        clockMinutes = 0;
        clockHours = (clockHours + 1) % 24;
      }
    }
  }
}

// --- Non-Blocking Stopwatch & 5-Second Hold Manager ---
void updateStopwatch(unsigned long now) {
  if (currentMode == MODE_TIMER_RUNNING) {
    // Increment elapsed time every 1 second
    if (now - lastTimerTick >= 1000) {
      lastTimerTick += 1000;
      timerSeconds++;

      if (timerSeconds >= 60) {
        timerSeconds = 0;
        timerMinutes++;
        if (timerMinutes >= 60) {
          timerMinutes = 0;
          timerHours = (timerHours + 1) % 100;
        }
      }
    }
  } 
  else if (currentMode == MODE_TIMER_HOLD) {
    // Keep final time frozen on screen for 5000 ms (5 seconds), then switch to Clock
    if (now - timerHoldStartTime >= HOLD_DURATION_MS) {
      tone(BUZZER_PIN, 2500, 60); // Short chirp when returning to Clock Mode
      currentMode = MODE_CLOCK;
    }
  }
}

// --- Servo Day/Night Indicator ---
// Right (0 deg): Daytime (07:00 to 18:59)
// Left (180 deg): Nighttime (19:00 to 06:59)
void updateDayNightServo() {
  bool isDaytime = (clockHours >= 7 && clockHours <= 18);
  int targetAngle = isDaytime ? 0 : 180;

  if (targetAngle != currentServoAngle) {
    currentServoAngle = targetAngle;
    dayNightServo.write(currentServoAngle);
  }
}

// --- 12-LED Controller (3-Hand Analog Clock + Timer Chaser) ---
void updateLEDs(unsigned long now) {
  if (currentMode == MODE_CLOCK) {
    // Map Hour, Minute, and Second hands to the 12 LEDs
    uint8_t hourLedIndex   = dialPositionToLedIndex(clockHours % 12);
    uint8_t minuteLedIndex = dialPositionToLedIndex(clockMinutes / 5);
    uint8_t secondLedIndex = dialPositionToLedIndex(clockSeconds / 5);

    // Distinct non-blocking blink rates for Minute and Second hands
    bool minuteBlinkState = ((now / 400) % 2) == 0; // Slow pulse (400 ms)
    bool secondBlinkState = ((now / 100) % 2) == 0; // Fast pulse (100 ms)

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      bool ledState = LOW;

      // 1. Hour Hand: Solid ON
      if (i == hourLedIndex) {
        ledState = HIGH;
      }
      // 2. Minute Hand: Slow Blink
      if (i == minuteLedIndex) {
        ledState = minuteBlinkState;
      }
      // 3. Second Hand: Fast Blink (highest priority if hands overlap)
      if (i == secondLedIndex) {
        ledState = secondBlinkState;
      }

      digitalWrite(LED_PINS[i], ledState ? HIGH : LOW);
    }
  } 
  else if (currentMode == MODE_TIMER_RUNNING) {
    // Advance rotating chaser while timer is running
    if (now - lastChaserTick >= CHASER_INTERVAL_MS) {
      lastChaserTick = now;
      chaserIndex = (chaserIndex + 1) % NUM_LEDS;
    }

    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      digitalWrite(LED_PINS[i], (i == chaserIndex) ? HIGH : LOW);
    }
  } 
  else if (currentMode == MODE_TIMER_HOLD) {
    // Freeze the active chaser LED solid so you can see the timer is paused
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      digitalWrite(LED_PINS[i], (i == chaserIndex) ? HIGH : LOW);
    }
  }
}

// --- SSD1306 OLED Display Renderer ---
void renderOLED(unsigned long now) {
  display.clearDisplay();

  // Top Header Bar
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (currentMode == MODE_CLOCK) {
    display.print(F("ANALOG + 24H CLOCK"));
  } else if (currentMode == MODE_TIMER_RUNNING) {
    display.print(F("TIMER: RUNNING"));
  } else {
    display.print(F("TIMER: STOPPED (5s)"));
  }
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  // Format HH:MM:SS string (Shows Clock in MODE_CLOCK, or Timer in RUNNING/HOLD)
  char timeBuffer[9];
  uint8_t h = (currentMode == MODE_CLOCK) ? clockHours   : timerHours;
  uint8_t m = (currentMode == MODE_CLOCK) ? clockMinutes : timerMinutes;
  uint8_t s = (currentMode == MODE_CLOCK) ? clockSeconds : timerSeconds;

  snprintf(timeBuffer, sizeof(timeBuffer), "%02u:%02u:%02u", h, m, s);

  // Center main HH:MM:SS readout
  display.setTextSize(2);
  display.setCursor(16, 22);
  display.print(timeBuffer);

  // Bottom Status Bar
  display.drawLine(0, 48, 127, 48, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 54);

  if (currentMode == MODE_CLOCK) {
    uint8_t hLedNum = (clockHours % 12 == 0) ? 12 : (clockHours % 12);
    uint8_t mLedNum = (clockMinutes / 5 == 0) ? 12 : (clockMinutes / 5);
    uint8_t sLedNum = (clockSeconds / 5 == 0) ? 12 : (clockSeconds / 5);

    display.print(F("LED H:"));
    display.print(hLedNum);
    display.print(F(" M:"));
    display.print(mLedNum);
    display.print(F(" S:"));
    display.print(sLedNum);
  } 
  else if (currentMode == MODE_TIMER_RUNNING) {
    display.print(F("Press btn to stop"));
  } 
  else if (currentMode == MODE_TIMER_HOLD) {
    // Show live 5-second countdown at the bottom of the screen
    unsigned long elapsedHold = now - timerHoldStartTime;
    uint8_t remainingSec = 5;
    if (elapsedHold < HOLD_DURATION_MS) {
      remainingSec = (uint8_t)((HOLD_DURATION_MS - elapsedHold + 999) / 1000);
    } else {
      remainingSec = 0;
    }
    display.print(F("Hold: "));
    display.print(remainingSec);
    display.print(F("s -> Clock"));
  }

  display.display();
}
