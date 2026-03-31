/*
  SWR-Buzzer-LX1WJ
  ----------------

  Zweck
  -----
  Dieses Programm laeuft auf einem ESP32 und erzeugt einen Piezo-Ton.
  Es misst Forward und Reverse an einer Richtkoppler-/SWR-Bridge und
  stellt die Messwerte zusaetzlich auf einem eigenen WLAN-Access-Point
  als Weboberflaeche dar.

  Bedienung
  ---------
  Kurzer Tastendruck:
  - wenn das Geraet aus ist: aktuelle Forward-Spannung wird als Referenz
    gespeichert und der Leistungston wird eingeschaltet
  - wenn bereits Leistungston oder SWR-Ton aktiv ist: alles wird ausgeschaltet

  Langer Tastendruck:
  - wenn das Geraet aus ist und SWR im AP eingeschaltet wurde:
    der SWR-Ton wird eingeschaltet
  - wenn bereits Leistungston oder SWR-Ton aktiv ist: alles wird ausgeschaltet
  - je hoeher das SWR, desto hoeher die Frequenz

  Messlogik
  ---------
  - Im Leistungston wird nur Forward frisch gemessen
  - Reverse wird nur im SWR-Ton frisch gemessen
  - dadurch bleibt der Leistungston schnell, auch wenn SWR im AP
    eingeschaltet ist

  SWR-Piepmuster
  --------------
  Anzahl der Piepser pro Gruppe entspricht dem ganzzahligen SWR-Bereich:
  - SWR 1.00 bis 1.99 -> 1 Piepser
  - SWR 2.00 bis 2.99 -> 2 Piepser
  - SWR 3.00 bis 3.99 -> 3 Piepser
  - SWR 4.00 bis 4.99 -> 4 Piepser
  - eine Gruppe wird immer komplett ausgespielt, bevor ein neuer
    Messwert das Muster aendern darf
  - Beispiel:
    SWR 2.x -> 2 Piepser, kurze Pause, wieder 2 Piepser
    SWR 3.x -> 3 Piepser, kurze Pause, wieder 3 Piepser

  WLAN / Access Point
  -------------------
  Der ESP32 erstellt einen eigenen Access Point.

    SSID: SWR-Buzzer-LX1WJ
    Passwort: keines
    Adresse: http://192.168.4.1

  Die Webseite zeigt:
  - Forward-Spannung am ESP32
  - Reverse-Spannung am ESP32
  - SWR
  - ADC-Rohwerte
  - Referenzspannung
  - aktuelle Tonfrequenz

  Einstellbare Parameter im AP
  ----------------------------
  startFreqHz
    Frequenz am Referenzpunkt fuer den Leistungston.
    Default: 1650 Hz

  spanPercent
    Wie stark der Leistungston auf eine Veraenderung der Forward-Spannung
    relativ zur Referenz reagiert.

  swrBaseHz
    Feste Tonfrequenz fuer den SWR-Piepser
    Default: 1000 Hz

  swrStepHz
    Wird nicht mehr fuer die SWR-Tonausgabe verwendet.

  longPressMs
    Tastendauer fuer Umschaltung auf SWR-Ton.
    Default: 700 ms

  buzzerVolume
    Lautstaerke des Buzzers von 1 bis 10.
    Default: 10

  swrEnabled
    Schaltet die SWR-Messung und den SWR-Ton ein oder aus.
    Default: aus

  swrBeepMs
    Dauer eines einzelnen SWR-Piepsers in Millisekunden.
    Default: 70 ms

  swrPauseMs
    Pause zwischen zwei SWR-Piepsern in Millisekunden.
    Default: 110 ms

  swrGroupPauseMs
    Pause zwischen zwei SWR-Gruppen in Millisekunden.
    Default: 420 ms

  Obere Eingabefelder im AP
    Diese Einstellungen werden mit Enter oder beim Verlassen des Feldes
    sofort gespeichert.

  Unterer Speicherbutton
    Dieser speichert nur die Kalibriertabelle.

  Messintervall
    Die ADC-Messung laeuft langsamer als die Tongenerierung, damit die
    Piepser zeitlich sauber bleiben.

  Kalibriertabelle
    Es gibt 30 Zeilen.
    Pro Zeile wird gespeichert:
    - manuell eingegebenes SWR
    - gemessenes Uf
    - gemessenes Ur

    Mit dem Capture-Button einer Zeile werden die aktuellen Werte
    von Uf und Ur in diese Zeile uebernommen.

    Die SWR-Anzeige wird dann aus der Tabelle interpoliert.

  Hardware
  --------
  BUZZER_PIN = 25
    passiver Piezo gegen GND

  BUTTON_PIN = 27
    Taster gegen GND, interner Pullup aktiv

  FWD_PIN = 34
    ADC fuer Forward

  REV_PIN = 35
    ADC fuer Reverse

  Spannungsteiler / Schutz
  ------------------------
  Je Messkanal:
  - 100 kOhm vom Sensor-Ausgang zum ESP32-Pin
  - 33 kOhm vom ESP32-Pin nach GND
  - 3.3 V Zenerdiode parallel zum 33 kOhm

  SWR-Berechnung
  --------------
  Das SWR wird aus der Kalibriertabelle interpoliert.
  Als Vergleichswert wird pro Zeile das Verhaeltnis Ur / Uf benutzt.
  Aus dem aktuellen Verhaeltnis wird zwischen den naechsten bekannten
  Tabellenpunkten linear interpoliert.

  Sicherheit
  ----------
  - Niemals HF direkt an den ESP32 anschliessen.
  - Nur die DC-Messausgaenge der Bridge verwenden.
  - Am ADC-Pin duerfen niemals mehr als 3.3 V anliegen.
  - Bridge-GND und ESP32-GND muessen verbunden sein.
*/

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

const int BUZZER_PIN = 25;
const int BUTTON_PIN = 27;
const int FWD_PIN = 34;
const int REV_PIN = 35;

const int PWM_CHANNEL = 0;
const int PWM_RESOLUTION = 8;

const char *AP_SSID = "SWR-Buzzer-LX1WJ";

const float ADC_FULL_SCALE_V = 3.3f;
const int ADC_MAX = 4095;
const int MIN_FREQ_HZ = 80;
const int MAX_FREQ_HZ = 6000;
const int RAW_SAMPLES_PER_READ = 16;
const int FILTER_DEPTH = 8;
const int CAL_TABLE_SIZE = 30;
const unsigned long MEASUREMENT_INTERVAL_MS = 180;
struct Config {
  int startFreqHz;
  int spanPercent;
  int swrBaseHz;
  int swrStepHz;
  int longPressMs;
  int buzzerVolume;
  bool swrEnabled;
  int swrBeepMs;
  int swrPauseMs;
  int swrGroupPauseMs;
};

struct CalRow {
  float swr;
  float uf;
  float ur;
};

struct Measurements {
  int rawFwd;
  int rawRev;
  float espFwd;
  float espRev;
  float swr;
};

struct FilterState {
  int fwdRaw[FILTER_DEPTH];
  int revRaw[FILTER_DEPTH];
  int index;
  int count;
};

Preferences prefs;
WebServer server(80);
Config config = {1650, 100, 1000, 100, 700, 10, false, 70, 110, 420};
CalRow calTable[CAL_TABLE_SIZE] = {};

int referenceFwdRaw = 0;
bool active = false;
int currentFreqHz = 0;
bool lastButtonState = HIGH;
unsigned long lastButtonChangeMs = 0;
unsigned long buttonPressStartMs = 0;
unsigned long lastSerialMs = 0;
unsigned long lastMeasurementMs = 0;

enum ToneMode {
  MODE_OFF,
  MODE_POWER,
  MODE_SWR
};

ToneMode toneMode = MODE_OFF;
FilterState filterState = {};
int appliedToneHz = -1;
int appliedDuty = -1;
int latchedSwrBeepCount = 0;
unsigned long swrPatternStartMs = 0;
Measurements latestMeasurements = {};

void setupBuzzerPwm(int frequencyHz) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, frequencyHz, PWM_RESOLUTION);
#else
  ledcSetup(PWM_CHANNEL, frequencyHz, PWM_RESOLUTION);
  ledcAttachPin(BUZZER_PIN, PWM_CHANNEL);
#endif
}

void writeToneHz(int frequencyHz) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, frequencyHz);
#else
  ledcWriteTone(PWM_CHANNEL, frequencyHz);
#endif
}

void writeDuty(int duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER_PIN, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

int dutyForVolumeLevel(int level) {
  level = constrain(level, 1, 10);
  return map(level, 1, 10, 10, 128);
}

void applyBuzzerDrive() {
  int targetDuty = appliedToneHz <= 0 ? 0 : dutyForVolumeLevel(config.buzzerVolume);
  if (targetDuty == appliedDuty) {
    return;
  }
  appliedDuty = targetDuty;
  writeDuty(targetDuty);
}

void applyToneHz(int frequencyHz) {
  if (frequencyHz != appliedToneHz) {
    appliedToneHz = frequencyHz;
    writeToneHz(frequencyHz);
  }
  applyBuzzerDrive();
}

float rawToVoltage(int raw) {
  return (static_cast<float>(raw) / ADC_MAX) * ADC_FULL_SCALE_V;
}

float ratioFromVoltages(float uf, float ur) {
  if (uf <= 0.01f) {
    return 0.0f;
  }
  return ur / uf;
}

int readRawAverage(int pin) {
  long sum = 0;
  for (int i = 0; i < RAW_SAMPLES_PER_READ; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return sum / RAW_SAMPLES_PER_READ;
}

void pushFilteredSample(int rawFwd, int rawRev) {
  filterState.fwdRaw[filterState.index] = rawFwd;
  filterState.revRaw[filterState.index] = rawRev;
  filterState.index = (filterState.index + 1) % FILTER_DEPTH;
  if (filterState.count < FILTER_DEPTH) {
    filterState.count++;
  }
}

int averageBuffer(const int *values, int count) {
  if (count <= 0) {
    return 0;
  }

  long sum = 0;
  for (int i = 0; i < count; i++) {
    sum += values[i];
  }
  return sum / count;
}

void updateFilters() {
  int rawFwd = readRawAverage(FWD_PIN);
  int rawRev = 0;

  if (toneMode == MODE_SWR) {
    rawRev = readRawAverage(REV_PIN);
  } else if (filterState.count > 0) {
    int lastIndex = filterState.index == 0 ? FILTER_DEPTH - 1 : filterState.index - 1;
    rawRev = filterState.revRaw[lastIndex];
  }

  pushFilteredSample(rawFwd, rawRev);
}

float calculateSwrFromTable(float fwdVoltage, float revVoltage) {
  float currentRatio = ratioFromVoltages(fwdVoltage, revVoltage);

  struct Point {
    float ratio;
    float swr;
  };

  Point points[CAL_TABLE_SIZE];
  int pointCount = 0;

  for (int i = 0; i < CAL_TABLE_SIZE; i++) {
    if (calTable[i].swr > 0.0f && calTable[i].uf > 0.01f) {
      points[pointCount].ratio = ratioFromVoltages(calTable[i].uf, calTable[i].ur);
      points[pointCount].swr = calTable[i].swr;
      pointCount++;
    }
  }

  if (pointCount == 0) {
    return 1.0f;
  }

  for (int i = 0; i < pointCount - 1; i++) {
    for (int j = i + 1; j < pointCount; j++) {
      if (points[j].ratio < points[i].ratio) {
        Point tmp = points[i];
        points[i] = points[j];
        points[j] = tmp;
      }
    }
  }

  if (pointCount == 1) {
    return points[0].swr;
  }

  if (currentRatio <= points[0].ratio) {
    return points[0].swr;
  }

  for (int i = 0; i < pointCount - 1; i++) {
    float x1 = points[i].ratio;
    float x2 = points[i + 1].ratio;
    float y1 = points[i].swr;
    float y2 = points[i + 1].swr;

    if (currentRatio <= x2) {
      if (fabsf(x2 - x1) < 0.000001f) {
        return y2;
      }
      float t = (currentRatio - x1) / (x2 - x1);
      return y1 + t * (y2 - y1);
    }
  }

  return points[pointCount - 1].swr;
}

Measurements readMeasurements() {
  Measurements m;
  if (filterState.count == 0) {
    updateFilters();
  }

  m.rawFwd = averageBuffer(filterState.fwdRaw, filterState.count);
  m.rawRev = averageBuffer(filterState.revRaw, filterState.count);
  m.espFwd = rawToVoltage(m.rawFwd);
  m.espRev = rawToVoltage(m.rawRev);
  m.swr = calculateSwrFromTable(m.espFwd, m.espRev);
  return m;
}

void refreshMeasurements() {
  updateFilters();
  latestMeasurements = readMeasurements();
  lastMeasurementMs = millis();
}

void refreshMeasurementsIfDue() {
  if (lastMeasurementMs == 0 || millis() - lastMeasurementMs >= MEASUREMENT_INTERVAL_MS) {
    refreshMeasurements();
  }
}

const char *modeToText() {
  if (toneMode == MODE_POWER) {
    return "POWER";
  }
  if (toneMode == MODE_SWR) {
    return "SWR";
  }
  return "OFF";
}

void printSerialStatus(const Measurements &m) {
  if (millis() - lastSerialMs < 500) {
    return;
  }
  lastSerialMs = millis();

  Serial.print("MODE=");
  Serial.print(modeToText());
  Serial.print("  FWD_ESP=");
  Serial.print(m.espFwd, 3);
  Serial.print("V  REV_ESP=");
  Serial.print(m.espRev, 3);
  Serial.print("V  SWR=");
  Serial.print(m.swr, 2);
  Serial.print("  REF=");
  Serial.print(rawToVoltage(referenceFwdRaw), 3);
  Serial.print("V  FREQ=");
  Serial.print(currentFreqHz);
  Serial.print("Hz  VOL=");
  Serial.print(config.buzzerVolume);
  Serial.print("  SWR_EN=");
  Serial.print(config.swrEnabled ? "ON" : "OFF");
  Serial.println();
}

void saveConfig() {
  prefs.putInt("startFreq", config.startFreqHz);
  prefs.putInt("spanPct", config.spanPercent);
  prefs.putInt("swrBase", config.swrBaseHz);
  prefs.putInt("swrStep", config.swrStepHz);
  prefs.putInt("longMs", config.longPressMs);
  prefs.putInt("volume", config.buzzerVolume);
  prefs.putBool("swrOn", config.swrEnabled);
  prefs.putInt("swrBeepMs", config.swrBeepMs);
  prefs.putInt("swrPauseMs", config.swrPauseMs);
  prefs.putInt("swrGrpMs", config.swrGroupPauseMs);
  prefs.putBytes("calTable", calTable, sizeof(calTable));
}

void loadConfig() {
  config.startFreqHz = constrain(prefs.getInt("startFreq", 1650), 80, 6000);
  config.spanPercent = constrain(prefs.getInt("spanPct", 100), 1, 500);
  config.swrBaseHz = constrain(prefs.getInt("swrBase", 1000), 50, 2000);
  config.swrStepHz = constrain(prefs.getInt("swrStep", 100), 1, 2000);
  config.longPressMs = constrain(prefs.getInt("longMs", 700), 200, 5000);
  config.buzzerVolume = constrain(prefs.getInt("volume", 10), 1, 10);
  config.swrEnabled = prefs.getBool("swrOn", false);
  config.swrBeepMs = constrain(prefs.getInt("swrBeepMs", 70), 30, 1000);
  config.swrPauseMs = constrain(prefs.getInt("swrPauseMs", 110), 20, 1000);
  config.swrGroupPauseMs = constrain(prefs.getInt("swrGrpMs", 420), 50, 3000);
  size_t readBytes = prefs.getBytes("calTable", calTable, sizeof(calTable));
  if (readBytes != sizeof(calTable)) {
    for (int i = 0; i < CAL_TABLE_SIZE; i++) {
      calTable[i].swr = 0.0f;
      calTable[i].uf = 0.0f;
      calTable[i].ur = 0.0f;
    }
  }
}

void setReferenceNow() {
  updateFilters();
  referenceFwdRaw = averageBuffer(filterState.fwdRaw, filterState.count);
  if (referenceFwdRaw < 1) {
    referenceFwdRaw = 1;
  }
  active = true;
  currentFreqHz = config.startFreqHz;
  applyToneHz(currentFreqHz);
}

void stopTone() {
  active = false;
  toneMode = MODE_OFF;
  currentFreqHz = 0;
  latchedSwrBeepCount = 0;
  applyToneHz(0);
}

void startPowerMode() {
  setReferenceNow();
  toneMode = MODE_POWER;
}

void startSwrMode() {
  active = true;
  toneMode = MODE_SWR;
  latchedSwrBeepCount = 0;
  swrPatternStartMs = millis();
}

int swrToBeepCount(float swr) {
  if (swr < 1.0f) {
    return 1;
  }

  int beepCount = static_cast<int>(floorf(swr));
  if (beepCount < 1) {
    beepCount = 1;
  }
  if (beepCount > 9) {
    beepCount = 9;
  }
  return beepCount;
}

unsigned long swrPatternLengthMs(int beepCount) {
  return beepCount * (static_cast<unsigned long>(config.swrBeepMs) + static_cast<unsigned long>(config.swrPauseMs)) +
         static_cast<unsigned long>(config.swrGroupPauseMs);
}

bool swrPatternFinished() {
  if (latchedSwrBeepCount <= 0) {
    return true;
  }
  return millis() - swrPatternStartMs >= swrPatternLengthMs(latchedSwrBeepCount);
}

void latchSwrPatternIfNeeded(float swr) {
  int targetBeepCount = swrToBeepCount(swr);

  if (latchedSwrBeepCount == 0) {
    latchedSwrBeepCount = targetBeepCount;
    swrPatternStartMs = millis();
    return;
  }

  if (swrPatternFinished()) {
    latchedSwrBeepCount = targetBeepCount;
    swrPatternStartMs = millis();
  }
}

bool swrToneShouldBeOn(float swr) {
  latchSwrPatternIfNeeded(swr);
  if (latchedSwrBeepCount <= 0) {
    return false;
  }

  unsigned long patternMs = swrPatternLengthMs(latchedSwrBeepCount);
  unsigned long pos = (millis() - swrPatternStartMs) % patternMs;

  for (int i = 0; i < latchedSwrBeepCount; i++) {
    if (pos < static_cast<unsigned long>(config.swrBeepMs)) {
      return true;
    }
    pos -= static_cast<unsigned long>(config.swrBeepMs);

    if (pos < static_cast<unsigned long>(config.swrPauseMs)) {
      return false;
    }
    pos -= static_cast<unsigned long>(config.swrPauseMs);
  }

  return false;
}

void handleToggleSwr() {
  config.swrEnabled = !config.swrEnabled;
  if (!config.swrEnabled && toneMode == MODE_SWR) {
    stopTone();
  }
  saveConfig();
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleSaveSettings() {
  if (server.hasArg("startFreqHz")) {
    config.startFreqHz = constrain(server.arg("startFreqHz").toInt(), 80, 6000);
  }
  if (server.hasArg("spanPercent")) {
    config.spanPercent = constrain(server.arg("spanPercent").toInt(), 1, 500);
  }
  if (server.hasArg("buzzerVolume")) {
    config.buzzerVolume = constrain(server.arg("buzzerVolume").toInt(), 1, 10);
    applyBuzzerDrive();
  }
  if (server.hasArg("swrBaseHz")) {
    config.swrBaseHz = constrain(server.arg("swrBaseHz").toInt(), 50, 2000);
  }
  if (server.hasArg("swrBeepMs")) {
    config.swrBeepMs = constrain(server.arg("swrBeepMs").toInt(), 30, 1000);
  }
  if (server.hasArg("swrPauseMs")) {
    config.swrPauseMs = constrain(server.arg("swrPauseMs").toInt(), 20, 1000);
  }
  if (server.hasArg("swrGroupPauseMs")) {
    config.swrGroupPauseMs = constrain(server.arg("swrGroupPauseMs").toInt(), 50, 3000);
  }
  if (server.hasArg("longPressMs")) {
    config.longPressMs = constrain(server.arg("longPressMs").toInt(), 200, 5000);
  }
  saveConfig();
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleRoot() {
  String page;
  page.reserve(11000);

  page += "<!doctype html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>SWR-Buzzer-LX1WJ</title>";
  page += "<style>";
  page += "body{font-family:Arial,sans-serif;background:#f4efe2;color:#1e1e1e;margin:0;padding:20px;}";
  page += ".box{max-width:820px;margin:0 auto;background:#fff;border:2px solid #222;border-radius:12px;padding:20px;}";
  page += ".v{font-size:1.35rem;margin:10px 0;}label{display:block;margin-top:14px;font-weight:bold;}";
  page += "input{width:100%;padding:10px;font-size:1rem;box-sizing:border-box;}";
  page += "button{margin-top:14px;padding:12px 16px;font-size:1rem;} .hint{color:#444;}";
  page += "table input{padding:6px;font-size:.95rem;}";
  page += ".section{margin-top:28px;padding-top:22px;border-top:6px solid #222;}";
  page += ".swrbtn{background:#d44b20;color:#fff;border:none;font-weight:bold;}";
  page += ".swrline{color:#b22222;font-weight:bold;}";
  page += ".thinsep{border-top:2px solid #777;margin:14px 0 10px 0;padding-top:6px;}";
  page += "</style></head><body><div class='box'>";
  page += "<h1>SWR-Buzzer-LX1WJ</h1>";
  page += "<div class='v'>Forward ESP: <span id='vinFwd'>-</span> V</div>";
  page += "<div class='v'>Reverse ESP: <span id='vinRev'>-</span> V</div>";
  page += "<div class='v swrline'><span id='swrLine'>SWR: aus</span></div>";
  page += "<div class='thinsep'>";
  page += "<div class='v'>ADC Forward: <span id='rawFwd'>-</span></div>";
  page += "<div class='v'>ADC Reverse: <span id='rawRev'>-</span></div>";
  page += "</div>";
  page += "<div class='v'>Forward Referenz: <span id='ref'>-</span> V</div>";
  page += "<div class='v'>Modus: <span id='mode'>-</span></div>";
  page += "<div class='v'>Tonfrequenz: <span id='freq'>-</span> Hz</div>";
  page += "<div class='hint'>WLAN: SWR-Buzzer-LX1WJ / 192.168.4.1</div>";
  page += "<div class='hint'>Taste kurz: Leistung an oder aus. Taste lang: SWR an oder aus, wenn SWR eingeschaltet ist.</div>";
  page += "<form id='settingsForm' action='/saveSettings' method='post'>";
  page += "<label for='startFreqHz'>Startfrequenz in Hz</label>";
  page += "<input class='autosave' id='startFreqHz' name='startFreqHz' type='number' min='80' max='6000' value='" + String(config.startFreqHz) + "'>";
  page += "<label for='spanPercent'>Spannweite in Prozent</label>";
  page += "<input class='autosave' id='spanPercent' name='spanPercent' type='number' min='1' max='500' value='" + String(config.spanPercent) + "'>";
  page += "<div class='hint'>100 bedeutet: +100 Prozent Forward-Signal ergibt +100 Prozent Frequenz.</div>";
  page += "<label for='buzzerVolume'>Buzzer Lautstaerke 1-10</label>";
  page += "<input class='autosave' id='buzzerVolume' name='buzzerVolume' type='number' min='1' max='10' value='" + String(config.buzzerVolume) + "'>";
  page += "<div class='hint'>10 ist die groesste eingestellte Lautstaerke im Sketch.</div>";
  page += "<div class='section'>";
  page += "<h2>SWR</h2>";
  page += "<div><button class='swrbtn' formaction='/toggleSWR' formmethod='post' type='submit'>";
  page += config.swrEnabled ? "SWR Messung AUSSCHALTEN" : "SWR Messung EINSCHALTEN";
  page += "</button></div>";
  page += "<div class='hint'>SWR Status: ";
  page += config.swrEnabled ? "eingeschaltet" : "nicht eingeschaltet";
  page += "</div>";
  page += "<div class='hint'>Ab SWR 2.0 wird in Gruppen gepiept: 2x, 3x, 4x usw.</div>";
  page += "<label for='swrBaseHz'>SWR Tonfrequenz in Hz</label>";
  page += "<input class='autosave' id='swrBaseHz' name='swrBaseHz' type='number' min='50' max='2000' value='" + String(config.swrBaseHz) + "'>";
  page += "<label for='swrBeepMs'>SWR Piepdauer in ms</label>";
  page += "<input class='autosave' id='swrBeepMs' name='swrBeepMs' type='number' min='30' max='1000' value='" + String(config.swrBeepMs) + "'>";
  page += "<label for='swrPauseMs'>SWR Pause zwischen Piepsern in ms</label>";
  page += "<input class='autosave' id='swrPauseMs' name='swrPauseMs' type='number' min='20' max='1000' value='" + String(config.swrPauseMs) + "'>";
  page += "<label for='swrGroupPauseMs'>SWR Gruppenpause in ms</label>";
  page += "<input class='autosave' id='swrGroupPauseMs' name='swrGroupPauseMs' type='number' min='50' max='3000' value='" + String(config.swrGroupPauseMs) + "'>";
  page += "<label for='longPressMs'>Langer Tastendruck in ms</label>";
  page += "<input class='autosave' id='longPressMs' name='longPressMs' type='number' min='200' max='5000' value='" + String(config.longPressMs) + "'>";
  page += "<button type='submit'>Einstellungen speichern</button>";
  page += "<div class='hint'>Aenderungen oben werden mit Enter oder beim Verlassen des Feldes uebernommen.</div>";
  page += "</form>";
  page += "<form action='/save' method='post'>";
  page += "<h2>Kalibriertabelle</h2>";
  page += "<div class='hint'>SWR manuell eingeben. Mit Capture werden die aktuellen Werte fuer Uf und Ur in die Zeile uebernommen.</div>";
  page += "<table style='width:100%;border-collapse:collapse;margin-top:12px;'>";
  page += "<tr><th style='text-align:left;border-bottom:1px solid #999;'>Nr</th><th style='text-align:left;border-bottom:1px solid #999;'>SWR</th><th style='text-align:left;border-bottom:1px solid #999;'>Uf</th><th style='text-align:left;border-bottom:1px solid #999;'>Ur</th><th style='text-align:left;border-bottom:1px solid #999;'>Capture</th></tr>";
  for (int i = 0; i < CAL_TABLE_SIZE; i++) {
    page += "<tr>";
    page += "<td style='padding:4px 6px;'>" + String(i + 1) + "</td>";
    page += "<td style='padding:4px 6px;'><input name='swr" + String(i) + "' type='number' step='0.01' min='0' value='" + String(calTable[i].swr, 2) + "'></td>";
    page += "<td style='padding:4px 6px;'><input name='uf" + String(i) + "' type='number' step='0.001' value='" + String(calTable[i].uf, 3) + "'></td>";
    page += "<td style='padding:4px 6px;'><input name='ur" + String(i) + "' type='number' step='0.001' value='" + String(calTable[i].ur, 3) + "'></td>";
    page += "<td style='padding:4px 6px;'><button formaction='/capture?row=" + String(i) + "' formmethod='post' type='submit'>Capture</button></td>";
    page += "</tr>";
  }
  page += "</table>";
  page += "<button type='submit'>Kalibrierdaten speichern</button>";
  page += "</form>";
  page += "<form action='/setref' method='post'><button type='submit'>Forward-Referenz setzen</button></form>";
  page += "<script>";
  page += "const sf=document.getElementById('settingsForm');";
  page += "document.querySelectorAll('.autosave').forEach(function(el){";
  page += "el.addEventListener('keydown',function(ev){if(ev.key==='Enter'){ev.preventDefault();sf.requestSubmit();}});";
  page += "el.addEventListener('change',function(){sf.requestSubmit();});";
  page += "});";
  page += "async function upd(){const r=await fetch('/status');const j=await r.json();";
  page += "vinFwd.textContent=j.vinFwd.toFixed(3);vinRev.textContent=j.vinRev.toFixed(3);";
  page += "swrLine.textContent=j.swrEnabled?('SWR: '+j.swr.toFixed(2)):'SWR: aus';";
  page += "rawFwd.textContent=j.rawFwd;rawRev.textContent=j.rawRev;";
  page += "ref.textContent=j.ref.toFixed(3);mode.textContent=j.mode;freq.textContent=j.freq;}";
  page += "setInterval(upd,500);upd();";
  page += "</script></div></body></html>";

  server.send(200, "text/html", page);
}

void handleStatus() {
  Measurements m = latestMeasurements;
  String json = "{";
  json += "\"vinFwd\":" + String(m.espFwd, 3) + ",";
  json += "\"vinRev\":" + String(m.espRev, 3) + ",";
  json += "\"swr\":" + String(m.swr, 2) + ",";
  json += "\"rawFwd\":" + String(m.rawFwd) + ",";
  json += "\"rawRev\":" + String(m.rawRev) + ",";
  json += "\"ref\":" + String(rawToVoltage(referenceFwdRaw), 3) + ",";
  json += "\"freq\":" + String(currentFreqHz) + ",";
  json += "\"swrEnabled\":" + String(config.swrEnabled ? "true" : "false") + ",";
  json += "\"mode\":\"" + String(toneMode == MODE_POWER ? "Leistung" : toneMode == MODE_SWR ? "SWR" : "Aus") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleSave() {
  for (int i = 0; i < CAL_TABLE_SIZE; i++) {
    String swrName = "swr" + String(i);
    String ufName = "uf" + String(i);
    String urName = "ur" + String(i);
    if (server.hasArg(swrName)) {
      calTable[i].swr = server.arg(swrName).toFloat();
      if (calTable[i].swr < 0.0f) {
        calTable[i].swr = 0.0f;
      }
    }
    if (server.hasArg(ufName)) {
      calTable[i].uf = server.arg(ufName).toFloat();
      if (calTable[i].uf < 0.0f) {
        calTable[i].uf = 0.0f;
      }
    }
    if (server.hasArg(urName)) {
      calTable[i].ur = server.arg(urName).toFloat();
      if (calTable[i].ur < 0.0f) {
        calTable[i].ur = 0.0f;
      }
    }
  }
  saveConfig();
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleCapture() {
  int row = -1;
  if (server.hasArg("row")) {
    row = server.arg("row").toInt();
  }

  if (row >= 0 && row < CAL_TABLE_SIZE) {
    refreshMeasurements();
    Measurements m = latestMeasurements;
    for (int i = 0; i < CAL_TABLE_SIZE; i++) {
      String swrName = "swr" + String(i);
      String ufName = "uf" + String(i);
      String urName = "ur" + String(i);
      if (server.hasArg(swrName)) {
        calTable[i].swr = max(0.0f, server.arg(swrName).toFloat());
      }
      if (server.hasArg(ufName)) {
        calTable[i].uf = max(0.0f, server.arg(ufName).toFloat());
      }
      if (server.hasArg(urName)) {
        calTable[i].ur = max(0.0f, server.arg(urName).toFloat());
      }
    }
    calTable[row].uf = m.espFwd;
    calTable[row].ur = m.espRev;
    saveConfig();
  }

  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleSetRef() {
  setReferenceNow();
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(FWD_PIN, ADC_11db);
  analogSetPinAttenuation(REV_PIN, ADC_11db);

  prefs.begin("lx1wj-swr", false);
  loadConfig();

  setupBuzzerPwm(config.startFreqHz);
  applyToneHz(0);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/saveSettings", HTTP_POST, handleSaveSettings);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/setref", HTTP_POST, handleSetRef);
  server.on("/capture", HTTP_POST, handleCapture);
  server.on("/toggleSWR", HTTP_POST, handleToggleSwr);
  server.begin();

  refreshMeasurements();

  Serial.println();
  Serial.println("SWR-Buzzer-LX1WJ");
  Serial.println("Serial Monitor: 115200 Baud");
  Serial.println("Taste kurz: Leistung an/aus");
  Serial.println("Taste lang: SWR an/aus, wenn SWR im AP aktiviert ist");
  Serial.println("ADC-Messwerte werden gemittelt.");
}

void loop() {
  server.handleClient();
  if (toneMode == MODE_SWR) {
    if (latchedSwrBeepCount == 0 || swrPatternFinished()) {
      refreshMeasurements();
    }
  } else {
    refreshMeasurementsIfDue();
  }

  bool buttonState = digitalRead(BUTTON_PIN);

  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;
    lastButtonChangeMs = millis();

    if (buttonState == LOW) {
      buttonPressStartMs = millis();
    } else {
      unsigned long pressDuration = millis() - buttonPressStartMs;
      if (pressDuration >= static_cast<unsigned long>(config.longPressMs)) {
        if (toneMode == MODE_OFF) {
          if (config.swrEnabled) {
            startSwrMode();
          }
        } else {
          stopTone();
        }
      } else if (pressDuration >= 30) {
        if (toneMode == MODE_OFF) {
          startPowerMode();
        } else {
          stopTone();
        }
      }
    }
  }

  if (!active) {
    stopTone();
    Measurements m = latestMeasurements;
    printSerialStatus(m);
    delay(20);
    return;
  }

  Measurements m = latestMeasurements;
  printSerialStatus(m);

  if (toneMode == MODE_SWR) {
    if (latchedSwrBeepCount == 0) {
      latchSwrPatternIfNeeded(m.swr);
    }
    currentFreqHz = constrain(config.swrBaseHz, MIN_FREQ_HZ, MAX_FREQ_HZ);
    if (swrToneShouldBeOn(m.swr)) {
      applyToneHz(currentFreqHz);
    } else {
      applyToneHz(0);
    }
    delay(20);
    return;
  }

  if (toneMode != MODE_POWER) {
    delay(20);
    return;
  }

  if (referenceFwdRaw < 1) {
    referenceFwdRaw = 1;
  }

  float relativeChange = static_cast<float>(m.rawFwd - referenceFwdRaw) / referenceFwdRaw;
  float freqFactor = 1.0f + relativeChange * (static_cast<float>(config.spanPercent) / 100.0f);

  if (freqFactor < 0.05f) {
    freqFactor = 0.05f;
  }

  currentFreqHz = static_cast<int>(config.startFreqHz * freqFactor);
  currentFreqHz = constrain(currentFreqHz, MIN_FREQ_HZ, MAX_FREQ_HZ);
  applyToneHz(currentFreqHz);

  delay(20);
}
