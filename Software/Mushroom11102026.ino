#include <Wire.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "Adafruit_SHT31.h"

Adafruit_SHT31 sht31 = Adafruit_SHT31();
Preferences prefs;

#define FOG_A_PIN      23
#define FOG_B_PIN      25
#define FAN_RELAY_PIN  18
#define LED_RELAY_PIN  19
#define BUTTON_PIN      4
#define STATUS_LED_PIN  2

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

const float HUMIDITY_MIN = 75.0;
const float HUMIDITY_MAX = 85.0;

const float         FOG_STEP_PERCENT    = 3.0;
const unsigned long FOG_SINGLE_MAX_MS   = 180000;
const unsigned long FOG_SWITCH_PAUSE_MS = 1000;
const unsigned long FOG_STALL_WINDOW_MS = 90000;
const float         FOG_STALL_MIN_RISE  = 0.5;
const unsigned long FOG_BOTH_MAX_MS     = 240000;
const unsigned long FOG_BOTH_LOCKOUT_MS = 360000;
const unsigned long FOG_TOTAL_MAX_MS    = 1200000;
const unsigned long FOG_REST_MS         = 300000;

const unsigned long SENSOR_READ_INTERVAL   = 2000;
const unsigned long SENSOR_REINIT_INTERVAL = 10000;
const uint8_t       SENSOR_MAX_ERRORS      = 3;
const uint16_t      SENSOR_STUCK_READS     = 150;
const uint8_t       HUM_AVG_SIZE           = 5;

const unsigned long FAN_ON_DURATION    = 15000;
const unsigned long FAN_OFF_FRUITING   = 300000;
const unsigned long FAN_OFF_INCUBATION = 5400000;

const unsigned long LIGHT_HALF_CYCLE = 43200000;

const unsigned long DEBOUNCE_MS          = 50;
const unsigned long LONG_PRESS_MS        = 3000;
const unsigned long PAUSE_AUTO_RESUME_MS = 1800000;

const uint32_t WDT_TIMEOUT_S = 10;

enum Mode : uint8_t { MODE_FRUITING = 0, MODE_INCUBATION = 1 };
uint8_t mode = MODE_FRUITING;

bool paused = false;
unsigned long pauseStart = 0;

bool     sensorOk = false;
uint8_t  sensorErrors = 0;
uint16_t sensorSameCount = 0;
float    lastRawHum = -1.0;
float    lastTemp = NAN;
float    humBuf[HUM_AVG_SIZE];
uint8_t  humBufCount = 0;
uint8_t  humBufIndex = 0;
float    humAvg = 0;
unsigned long sensorTimer = 0;
unsigned long sensorReinitTimer = 0;

enum FogState : uint8_t { FOG_IDLE, FOG_SINGLE, FOG_SWITCHING, FOG_BOTH, FOG_REST };
uint8_t fogState = FOG_IDLE;
uint8_t fogActive = 0;
uint8_t fogNextStart = 0;
bool fogAOn = false;
bool fogBOn = false;
unsigned long fogStateStart = 0;
unsigned long fogRunStart = 0;
float fogSegmentStartHum = 0;
float fogStallRefHum = 0;
unsigned long fogStallRefTime = 0;
bool fogBothLockout = false;
unsigned long fogBothLockoutStart = 0;

bool fanOn = false;
unsigned long fanTimer = 0;

bool lightPhaseOn = true;
bool lightRelayOn = false;
unsigned long lightTimer = 0;

bool btnRaw = HIGH;
bool btnStable = HIGH;
unsigned long btnLastChange = 0;
unsigned long btnPressStart = 0;
bool btnLongDone = false;

bool statusLedOn = false;

const char* modeName() {
  return (mode == MODE_FRUITING) ? "ПЛОДОНОШЕНИЕ" : "ИНКУБАЦИЯ";
}

void writeRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? RELAY_ON : RELAY_OFF);
}

void setFog(bool a, bool b) {
  if (a != fogAOn) { fogAOn = a; writeRelay(FOG_A_PIN, a); }
  if (b != fogBOn) { fogBOn = b; writeRelay(FOG_B_PIN, b); }
}

void setFan(bool on) {
  fanOn = on;
  writeRelay(FAN_RELAY_PIN, on);
}

void resetHumAvg() {
  humBufCount = 0;
  humBufIndex = 0;
}

void pushHum(float h) {
  humBuf[humBufIndex] = h;
  humBufIndex = (humBufIndex + 1) % HUM_AVG_SIZE;
  if (humBufCount < HUM_AVG_SIZE) humBufCount++;

  float sum = 0;
  for (uint8_t i = 0; i < humBufCount; i++) sum += humBuf[i];
  humAvg = sum / humBufCount;
}

bool humValid() {
  return sensorOk && humBufCount > 0;
}

bool sensorInit() {
  if (sht31.begin(0x44)) {
    sensorOk = true;
    sensorErrors = 0;
    sensorSameCount = 0;
    lastRawHum = -1.0;
    resetHumAvg();
    return true;
  }
  return false;
}

void sensorFail(const char* reason, unsigned long now) {
  sensorOk = false;
  resetHumAvg();
  sensorReinitTimer = now;
  Serial.print("ОШИБКА ДАТЧИКА: ");
  Serial.print(reason);
  Serial.println(". Туман отключён, пробую переподключить.");
}

void printStatus(float h) {
  Serial.print('[');
  Serial.print(modeName());
  if (paused) Serial.print(" | ПАУЗА");
  Serial.print("] Т: ");
  Serial.print(lastTemp, 1);
  Serial.print(" °C | Вл: ");
  Serial.print(h, 1);
  Serial.print(" % (ср ");
  Serial.print(humAvg, 1);
  Serial.print(" %) | Туман: ");
  if (fogAOn && fogBOn) Serial.print("A+B");
  else if (fogAOn)      Serial.print("A");
  else if (fogBOn)      Serial.print("B");
  else                  Serial.print("-");
  Serial.print(" | Вент: ");
  Serial.println(fanOn ? "ВКЛ" : "-");
}

void handleSensor(unsigned long now) {
  if (now - sensorTimer < SENSOR_READ_INTERVAL) return;
  sensorTimer = now;

  if (!sensorOk) {
    if (now - sensorReinitTimer >= SENSOR_REINIT_INTERVAL) {
      sensorReinitTimer = now;
      if (sensorInit()) Serial.println("Датчик SHT31 снова на связи.");
      else              Serial.println("SHT31 не отвечает...");
    }
    return;
  }

  float t, h;
  bool ok = sht31.readBoth(&t, &h) && !isnan(t) && !isnan(h);

  if (!ok) {
    sensorErrors++;
    Serial.println("Сбой чтения SHT31");
    if (sensorErrors >= SENSOR_MAX_ERRORS) sensorFail("нет ответа", now);
    return;
  }
  sensorErrors = 0;

  if (h == lastRawHum) {
    sensorSameCount++;
    if (sensorSameCount >= SENSOR_STUCK_READS) {
      sensorFail("показания не меняются", now);
      return;
    }
  } else {
    sensorSameCount = 0;
  }
  lastRawHum = h;
  lastTemp = t;

  pushHum(h);
  printStatus(h);
}

void fogStop() {
  setFog(false, false);
  fogState = FOG_IDLE;
}

void fogStartSingle(uint8_t gen, unsigned long now) {
  fogActive = gen;
  setFog(gen == 0, gen == 1);
  fogState = FOG_SINGLE;
  fogStateStart = now;
  fogSegmentStartHum = humAvg;
  fogStallRefHum = humAvg;
  fogStallRefTime = now;
  Serial.print("Туман: работает генератор ");
  Serial.println(gen == 0 ? "A" : "B");
}

void fogSwitchTo(uint8_t gen, unsigned long now) {
  setFog(false, false);
  fogActive = gen;
  fogState = FOG_SWITCHING;
  fogStateStart = now;
}

void handleFog(unsigned long now) {
  if (paused || fanOn || !humValid()) {
    if (fogState != FOG_IDLE && fogState != FOG_REST) fogStop();
    return;
  }

  if (fogState == FOG_IDLE) {
    if (humAvg < HUMIDITY_MIN) {
      fogRunStart = now;
      fogStartSingle(fogNextStart, now);
      fogNextStart ^= 1;
    }
    return;
  }
  if (fogState == FOG_REST) {
    if (now - fogStateStart >= FOG_REST_MS) {
      fogState = FOG_IDLE;
      Serial.println("Туман: отдых окончен");
    }
    return;
  }

  if (humAvg >= HUMIDITY_MAX) {
    fogStop();
    Serial.println("Туман: влажность достигнута");
    return;
  }

  if (now - fogRunStart >= FOG_TOTAL_MAX_MS) {
    setFog(false, false);
    fogState = FOG_REST;
    fogStateStart = now;
    Serial.println("Туман: слишком долго без перерыва, отдых 5 минут");
    return;
  }

  if (fogBothLockout && now - fogBothLockoutStart >= FOG_BOTH_LOCKOUT_MS) {
    fogBothLockout = false;
  }

  switch (fogState) {
    case FOG_SINGLE:
      if (humAvg - fogSegmentStartHum >= FOG_STEP_PERCENT ||
          now - fogStateStart >= FOG_SINGLE_MAX_MS) {
        fogSwitchTo(fogActive ^ 1, now);
      }
      else if (now - fogStallRefTime >= FOG_STALL_WINDOW_MS) {
        if (humAvg - fogStallRefHum < FOG_STALL_MIN_RISE && !fogBothLockout) {
          setFog(true, true);
          fogState = FOG_BOTH;
          fogStateStart = now;
          Serial.println("Туман: влажность не растёт, включены оба");
        } else {
          fogStallRefHum = humAvg;
          fogStallRefTime = now;
        }
      }
      break;

    case FOG_SWITCHING:
      if (now - fogStateStart >= FOG_SWITCH_PAUSE_MS) {
        fogStartSingle(fogActive, now);
      }
      break;

    case FOG_BOTH:
      if (now - fogStateStart >= FOG_BOTH_MAX_MS) {
        fogBothLockout = true;
        fogBothLockoutStart = now;
        fogSwitchTo(fogActive ^ 1, now);
        Serial.println("Туман: лимит работы вдвоём, возврат к поочерёдному режиму");
      }
      break;
  }
}

void startVentNow(unsigned long now) {
  setFan(true);
  fanTimer = now;
  Serial.println("Вентилятор: продувка");
}

void handleFan(unsigned long now) {
  if (paused) return;

  unsigned long offTime = (mode == MODE_FRUITING) ? FAN_OFF_FRUITING : FAN_OFF_INCUBATION;

  if (fanOn) {
    if (now - fanTimer >= FAN_ON_DURATION) {
      setFan(false);
      fanTimer = now;
      Serial.println("Вентилятор: ждём");
    }
  } else if (now - fanTimer >= offTime) {
    startVentNow(now);
  }
}

void handleLight(unsigned long now) {
  if (now - lightTimer >= LIGHT_HALF_CYCLE) {
    lightTimer += LIGHT_HALF_CYCLE;
    lightPhaseOn = !lightPhaseOn;
    Serial.println(lightPhaseOn ? "Свет: день" : "Свет: ночь");
  }

  bool want;
  if (paused)                       want = true;
  else if (mode == MODE_INCUBATION) want = false;
  else                              want = lightPhaseOn;

  if (want != lightRelayOn) {
    lightRelayOn = want;
    writeRelay(LED_RELAY_PIN, want);
  }
}

void enterPause(unsigned long now) {
  paused = true;
  pauseStart = now;
  setFan(false);
  fogStop();
  Serial.println("=== ПАУЗА: туман и вентилятор выключены, свет включён ===");
}

void exitPause(unsigned long now, bool automatic) {
  paused = false;
  Serial.println(automatic ? "=== ПАУЗА ЗАВЕРШЕНА АВТОМАТИЧЕСКИ ===" : "=== РАБОТА ВОЗОБНОВЛЕНА ===");
  startVentNow(now);
}

void handlePauseTimeout(unsigned long now) {
  if (paused && now - pauseStart >= PAUSE_AUTO_RESUME_MS) {
    exitPause(now, true);
  }
}

void toggleMode() {
  mode = (mode == MODE_FRUITING) ? MODE_INCUBATION : MODE_FRUITING;
  prefs.putUChar("mode", mode);
  Serial.print("=== РЕЖИМ: ");
  Serial.print(modeName());
  Serial.println(" ===");
}

void handleButton(unsigned long now) {
  bool raw = digitalRead(BUTTON_PIN);

  if (raw != btnRaw) {
    btnRaw = raw;
    btnLastChange = now;
  }

  if (now - btnLastChange >= DEBOUNCE_MS && raw != btnStable) {
    btnStable = raw;
    if (btnStable == LOW) {
      btnPressStart = now;
      btnLongDone = false;
    } else if (!btnLongDone) {
      if (paused) exitPause(now, false);
      else        enterPause(now);
    }
  }

  if (btnStable == LOW && !btnLongDone && now - btnPressStart >= LONG_PRESS_MS) {
    btnLongDone = true;
    toggleMode();
  }
}

void handleStatusLed(unsigned long now) {
  bool want;
  if (paused)                       want = (now / 250) % 2;
  else if (mode == MODE_INCUBATION) want = (now % 3000) < 100;
  else                              want = false;

  if (want != statusLedOn) {
    statusLedOn = want;
    digitalWrite(STATUS_LED_PIN, want ? HIGH : LOW);
  }
}

void setupWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg;
  cfg.timeout_ms = WDT_TIMEOUT_S * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  if (esp_task_wdt_init(&cfg) != ESP_OK) esp_task_wdt_reconfigure(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);
}

void setup() {
  Serial.begin(115200);

  pinMode(FOG_A_PIN, OUTPUT);
  pinMode(FOG_B_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  pinMode(LED_RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  writeRelay(FOG_A_PIN, false);
  writeRelay(FOG_B_PIN, false);
  writeRelay(FAN_RELAY_PIN, false);
  writeRelay(LED_RELAY_PIN, false);
  digitalWrite(STATUS_LED_PIN, LOW);

  prefs.begin("incubator", false);
  mode = prefs.getUChar("mode", MODE_FRUITING);
  if (mode > MODE_INCUBATION) mode = MODE_FRUITING;
  Serial.print("Старт. Режим: ");
  Serial.println(modeName());

  unsigned long now = millis();

  if (sensorInit()) {
    Serial.println("SHT31 найден");
  } else {
    Serial.println("ОШИБКА: Нет контакта с SHT31. Туман заблокирован, повтор каждые 10 с");
    sensorReinitTimer = now;
  }

  lightTimer = now;
  lightPhaseOn = true;

  startVentNow(now);

  setupWatchdog();
}

void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();

  handleButton(now);
  handlePauseTimeout(now);
  handleSensor(now);
  handleFan(now);
  handleFog(now);
  handleLight(now);
  handleStatusLed(now);
}
