#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define PIN_HEATER_PWM    4
#define PIN_FAN_PWM       5
#define PIN_REED_SW       6

#define PIN_DISP_DC       8
#define PIN_DISP_CS       9
#define PIN_TC_CS         10
#define PIN_SPI_MOSI      11
#define PIN_SPI_SCK       12
#define PIN_SPI_MISO      13
#define PIN_DISP_RST      18

#define PIN_ENC1_A        33
#define PIN_ENC1_B        34
#define PIN_ENC2_A        35
#define PIN_ENC2_B        36

#define TEMP_MIN_C            100
#define TEMP_MAX_C            480
#define TEMP_OVERHEAT_LIMIT_C 510
#define TEMP_SAFE_SLEEP_C     50
#define TEMP_STEP_C           5

#define FAN_MIN_PCT           25
#define FAN_MAX_PCT           100
#define FAN_COOLDOWN_PCT      90
#define FAN_STEP_PCT          5

#define COOLDOWN_DURATION_MS  60000UL
#define TC_READ_INTERVAL_MS   250UL
#define UI_REFRESH_INTERVAL   150UL
#define PID_WINDOW_MS         500UL

float Kp = 18.0f;
float Ki = 0.45f;
float Kd = 45.0f;

enum StationState {
  STATE_ACTIVE_HEATING = 0,
  STATE_COOLDOWN       = 1,
  STATE_STANDBY_SLEEP  = 2,
  STATE_SENSOR_FAULT   = 3
};

StationState currentState = STATE_STANDBY_SLEEP;
StationState lastDrawnState = (StationState)99;

volatile int targetTempC = 320;
volatile int targetFanPct = 60;

float actualTempC = 25.0f;
bool sensorFault = false;

float pidIntegral = 0.0f;
float prevError = 0.0f;
float heaterDutyWindowMs = 0.0f;
unsigned long windowStartTime = 0;

unsigned long lastTcReadTime = 0;
unsigned long lastUiRefreshTime = 0;
unsigned long cooldownStartTime = 0;
bool cooldownTimerRunning = false;

int lastDrawnActualTemp  = -999;
int lastDrawnTargetTemp  = -999;
int lastDrawnFanPct      = -999;
int lastDrawnCooldownSec = -999;
int lastDrawnHeaterPct   = -999;

volatile uint8_t enc1LastState = 0;
volatile uint8_t enc2LastState = 0;
volatile unsigned long enc1LastUs = 0;
volatile unsigned long enc2LastUs = 0;

Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, PIN_DISP_DC, PIN_DISP_CS, PIN_DISP_RST);

#define FAN_PWM_FREQ_HZ  25000
#define FAN_PWM_RES_BITS 8

void initFanPWM() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(PIN_FAN_PWM, FAN_PWM_FREQ_HZ, FAN_PWM_RES_BITS);
#else
  ledcSetup(0, FAN_PWM_FREQ_HZ, FAN_PWM_RES_BITS);
  ledcAttachPin(PIN_FAN_PWM, 0);
#endif
}

void setFanSpeedPercent(int pct) {
  pct = constrain(pct, 0, 100);
  uint32_t duty = (pct == 0) ? 0 : map(pct, 1, 100, 55, 255);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(PIN_FAN_PWM, duty);
#else
  ledcWrite(0, duty);
#endif
}

void IRAM_ATTR isrEncoderTemp() {
  unsigned long nowUs = micros();
  if (nowUs - enc1LastUs < 1500) return;
  uint8_t a = digitalRead(PIN_ENC1_A);
  uint8_t b = digitalRead(PIN_ENC1_B);
  if (a != enc1LastState) {
    if (b != a) {
      targetTempC = min(targetTempC + TEMP_STEP_C, TEMP_MAX_C);
    } else {
      targetTempC = max(targetTempC - TEMP_STEP_C, TEMP_MIN_C);
    }
    enc1LastState = a;
    enc1LastUs = nowUs;
  }
}

void IRAM_ATTR isrEncoderFan() {
  unsigned long nowUs = micros();
  if (nowUs - enc2LastUs < 1500) return;
  uint8_t a = digitalRead(PIN_ENC2_A);
  uint8_t b = digitalRead(PIN_ENC2_B);
  if (a != enc2LastState) {
    if (b != a) {
      targetFanPct = min(targetFanPct + FAN_STEP_PCT, FAN_MAX_PCT);
    } else {
      targetFanPct = max(targetFanPct - FAN_STEP_PCT, FAN_MIN_PCT);
    }
    enc2LastState = a;
    enc2LastUs = nowUs;
  }
}

float readMAX6675HardwareSPI(bool &faultOut) {
  digitalWrite(PIN_DISP_CS, HIGH);
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TC_CS, LOW);
  delayMicroseconds(2);
  uint16_t raw = SPI.transfer16(0x0000);
  digitalWrite(PIN_TC_CS, HIGH);
  SPI.endTransaction();

  if (raw & 0x0004) {
    faultOut = true;
    return NAN;
  }
  faultOut = false;
  raw >>= 3;
  return (float)raw * 0.25f;
}

void updatePID(float setpoint, float input, float dtSec) {
  float error = setpoint - input;

  if (abs(error) < 40.0f) {
    pidIntegral += (Ki * error * dtSec);
    pidIntegral = constrain(pidIntegral, 0.0f, (float)PID_WINDOW_MS * 0.7f);
  } else {
    pidIntegral = 0.0f;
  }

  float derivative = (error - prevError) / dtSec;
  prevError = error;

  float output = (Kp * error) + pidIntegral + (Kd * derivative);
  heaterDutyWindowMs = constrain(output, 0.0f, (float)PID_WINDOW_MS);
}

void runHeaterZeroCrossWindow() {
  if (currentState != STATE_ACTIVE_HEATING || sensorFault) {
    digitalWrite(PIN_HEATER_PWM, LOW);
    heaterDutyWindowMs = 0.0f;
    return;
  }

  unsigned long now = millis();
  if (now - windowStartTime >= PID_WINDOW_MS) {
    windowStartTime += PID_WINDOW_MS;
  }

  if ((now - windowStartTime) < (unsigned long)heaterDutyWindowMs) {
    digitalWrite(PIN_HEATER_PWM, HIGH);
  } else {
    digitalWrite(PIN_HEATER_PWM, LOW);
  }
}

void drawStaticLayout() {
  tft.fillScreen(ILI9341_BLACK);

  tft.fillRect(0, 0, 320, 32, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(12, 8);
  tft.print("CHRONOS-858D REWORK");

  tft.drawRoundRect(10, 40, 195, 118, 6, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  tft.setCursor(20, 48);
  tft.print("NOZZLE TEMPERATURE (C)");

  tft.drawRoundRect(212, 40, 98, 56, 6, ILI9341_DARKGREY);
  tft.setCursor(222, 48);
  tft.print("SET TEMP");

  tft.drawRoundRect(212, 102, 98, 56, 6, ILI9341_DARKGREY);
  tft.setCursor(222, 110);
  tft.print("AIRFLOW");

  tft.drawRoundRect(10, 166, 300, 64, 6, ILI9341_DARKGREY);
  tft.setCursor(20, 174);
  tft.print("STATION MODE:");
  tft.setCursor(200, 174);
  tft.print("HEATER LOAD:");
}

void updateDisplayUI() {
  int cooldownRemainingSec = 0;
  if (currentState == STATE_COOLDOWN && cooldownTimerRunning) {
    unsigned long elapsed = millis() - cooldownStartTime;
    if (elapsed < COOLDOWN_DURATION_MS) {
      cooldownRemainingSec = (COOLDOWN_DURATION_MS - elapsed) / 1000;
    }
  }

  if (currentState != lastDrawnState ||
      (currentState == STATE_COOLDOWN && cooldownRemainingSec != lastDrawnCooldownSec)) {
    tft.fillRect(18, 188, 175, 34, ILI9341_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 194);

    switch (currentState) {
      case STATE_ACTIVE_HEATING:
        tft.setTextColor(ILI9341_GREEN);
        tft.print("ACTIVE HEAT");
        break;
      case STATE_COOLDOWN:
        tft.setTextColor(ILI9341_CYAN);
        tft.print("COOLING ");
        tft.print(cooldownRemainingSec);
        tft.print("s");
        break;
      case STATE_STANDBY_SLEEP:
        tft.setTextColor(ILI9341_YELLOW);
        tft.print("STANDBY");
        break;
      case STATE_SENSOR_FAULT:
        tft.setTextColor(ILI9341_RED);
        tft.print("TC FAULT!");
        break;
    }
    lastDrawnState = currentState;
    lastDrawnCooldownSec = cooldownRemainingSec;
  }

  int dispTemp = sensorFault ? -1 : (int)round(actualTempC);
  if (dispTemp != lastDrawnActualTemp) {
    tft.setTextSize(6);
    if (dispTemp < 0) {
      tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
      tft.setCursor(24, 78);
      tft.print("ERR ");
    } else {
      uint16_t tempColor = (dispTemp > 80) ? ILI9341_ORANGE : ILI9341_CYAN;
      if (currentState == STATE_ACTIVE_HEATING && abs(dispTemp - targetTempC) <= 5) {
        tempColor = ILI9341_GREEN;
      }
      tft.setTextColor(tempColor, ILI9341_BLACK);
      tft.setCursor(24, 78);
      char buf[8];
      snprintf(buf, sizeof(buf), "%3dC", dispTemp);
      tft.print(buf);
    }
    lastDrawnActualTemp = dispTemp;
  }

  int setTempSnapshot = targetTempC;
  if (setTempSnapshot != lastDrawnTargetTemp) {
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor(220, 64);
    char buf[8];
    snprintf(buf, sizeof(buf), "%3dC", setTempSnapshot);
    tft.print(buf);
    lastDrawnTargetTemp = setTempSnapshot;
  }

  int activeFanDisplay = targetFanPct;
  if (currentState == STATE_COOLDOWN) activeFanDisplay = FAN_COOLDOWN_PCT;
  else if (currentState == STATE_STANDBY_SLEEP) activeFanDisplay = 0;

  if (activeFanDisplay != lastDrawnFanPct) {
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
    tft.setCursor(220, 126);
    char buf[8];
    snprintf(buf, sizeof(buf), "%3d%%", activeFanDisplay);
    tft.print(buf);
    lastDrawnFanPct = activeFanDisplay;
  }

  int heaterPct = (int)round((heaterDutyWindowMs / (float)PID_WINDOW_MS) * 100.0f);
  if (currentState != STATE_ACTIVE_HEATING) heaterPct = 0;
  if (heaterPct != lastDrawnHeaterPct) {
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK);
    tft.setCursor(200, 194);
    char buf[8];
    snprintf(buf, sizeof(buf), "%3d%% ", heaterPct);
    tft.print(buf);
    lastDrawnHeaterPct = heaterPct;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_HEATER_PWM, OUTPUT);
  digitalWrite(PIN_HEATER_PWM, LOW);

  pinMode(PIN_REED_SW, INPUT_PULLUP);

  initFanPWM();
  setFanSpeedPercent(0);

  pinMode(PIN_ENC1_A, INPUT_PULLUP);
  pinMode(PIN_ENC1_B, INPUT_PULLUP);
  pinMode(PIN_ENC2_A, INPUT_PULLUP);
  pinMode(PIN_ENC2_B, INPUT_PULLUP);

  enc1LastState = digitalRead(PIN_ENC1_A);
  enc2LastState = digitalRead(PIN_ENC2_A);

  attachInterrupt(digitalPinToInterrupt(PIN_ENC1_A), isrEncoderTemp, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC2_A), isrEncoderFan, CHANGE);

  pinMode(PIN_TC_CS, OUTPUT);
  digitalWrite(PIN_TC_CS, HIGH);
  pinMode(PIN_DISP_CS, OUTPUT);
  digitalWrite(PIN_DISP_CS, HIGH);

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  tft.begin();
  tft.setRotation(1);
  drawStaticLayout();

  windowStartTime = millis();
}

void loop() {
  unsigned long now = millis();

  if (now - lastTcReadTime >= TC_READ_INTERVAL_MS) {
    float dtSec = (now - lastTcReadTime) / 1000.0f;
    lastTcReadTime = now;

    actualTempC = readMAX6675HardwareSPI(sensorFault);

    if (sensorFault || actualTempC >= TEMP_OVERHEAT_LIMIT_C) {
      sensorFault = true;
      currentState = STATE_SENSOR_FAULT;
      digitalWrite(PIN_HEATER_PWM, LOW);
      setFanSpeedPercent(100);
    } else {
      bool gunOnCradle = (digitalRead(PIN_REED_SW) == LOW);

      if (gunOnCradle) {
        digitalWrite(PIN_HEATER_PWM, LOW);
        heaterDutyWindowMs = 0.0f;
        pidIntegral = 0.0f;

        if (currentState == STATE_ACTIVE_HEATING) {
          currentState = STATE_COOLDOWN;
          cooldownStartTime = now;
          cooldownTimerRunning = true;
        } else if (currentState == STATE_STANDBY_SLEEP && actualTempC > TEMP_SAFE_SLEEP_C) {
          currentState = STATE_COOLDOWN;
          cooldownStartTime = now;
          cooldownTimerRunning = true;
        }

        if (currentState == STATE_COOLDOWN) {
          setFanSpeedPercent(FAN_COOLDOWN_PCT);
          bool oneMinuteElapsed = (now - cooldownStartTime >= COOLDOWN_DURATION_MS);
          bool nozzleSafeTemp   = (actualTempC < TEMP_SAFE_SLEEP_C);

          if (oneMinuteElapsed && nozzleSafeTemp) {
            setFanSpeedPercent(0);
            cooldownTimerRunning = false;
            currentState = STATE_STANDBY_SLEEP;
          }
        } else {
          setFanSpeedPercent(0);
        }

      } else {
        currentState = STATE_ACTIVE_HEATING;
        cooldownTimerRunning = false;
        setFanSpeedPercent(targetFanPct);
        updatePID((float)targetTempC, actualTempC, dtSec);
      }
    }
  }

  runHeaterZeroCrossWindow();

  if (now - lastUiRefreshTime >= UI_REFRESH_INTERVAL) {
    lastUiRefreshTime = now;
    updateDisplayUI();
  }
}
