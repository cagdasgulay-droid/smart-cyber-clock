/*
 * Smart Cyber Clock v9
 * =====================
 * Changes from v5/v6:
 *   - HA LED control simplified:
 *     - Removed light entity (was causing mode conflicts)
 *     - Added "Edison LED Dimmer" number entity (0-255 slider)
 *     - Slider only changes brightness, does NOT change LED mode
 *     - LED mode select on HA removed (modes are device-only)
 *     - ON/OFF, Breathe, Blink, SOS still work from device menu
 *   - All other features unchanged
 *
 * Menu: Monitor, Pomodoro, Alarm, Snake, Tetris, Edison LED, Ayarlar
 */

#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "time.h"

// ====== WiFi Config ======
const char* ssid     = "Asya";
const char* password = "Asya2018";

// ====== NTP Config (GMT+3 Türkiye) ======
const char* ntpServer          = "pool.ntp.org";
const long  gmtOffset_sec      = 3 * 3600;
const int   daylightOffset_sec = 0;

// ====== MQTT Config ======
const char* mqtt_server    = "homeassistant.local";
const int   mqtt_port      = 1883;
const char* mqtt_user      = "cgdsgly";
const char* mqtt_password  = "Asya_^2016";
const char* mqtt_client_id = "cyberclock18";

// MQTT Topics
#define TOPIC_PREFIX       "cyberclock18"
#define TOPIC_STATUS       TOPIC_PREFIX "/status"
#define TOPIC_TEMP         TOPIC_PREFIX "/sensor/temperature"
#define TOPIC_HUM          TOPIC_PREFIX "/sensor/humidity"
#define TOPIC_TVOC         TOPIC_PREFIX "/sensor/tvoc"
#define TOPIC_CO2          TOPIC_PREFIX "/sensor/co2"
#define TOPIC_ALARM_SET    TOPIC_PREFIX "/alarm/set"
#define TOPIC_ALARM_STATE  TOPIC_PREFIX "/alarm/state"
#define TOPIC_BUZZER_SET   TOPIC_PREFIX "/buzzer/set"
// LED ON/OFF (switch) + dimmer (number slider) + mode (select)
#define TOPIC_LED_SET      TOPIC_PREFIX "/led/set"
#define TOPIC_LED_STATE    TOPIC_PREFIX "/led/state"
#define TOPIC_LED_DIM_SET  TOPIC_PREFIX "/led/dimmer/set"
#define TOPIC_LED_DIM_ST   TOPIC_PREFIX "/led/dimmer/state"
#define TOPIC_LED_MODE_SET TOPIC_PREFIX "/led/mode/set"
#define TOPIC_LED_MODE_ST  TOPIC_PREFIX "/led/mode/state"
#define TOPIC_VOL_SET      TOPIC_PREFIX "/volume/set"
#define TOPIC_VOL_STATE    TOPIC_PREFIX "/volume/state"
#define TOPIC_TCAL_SET     TOPIC_PREFIX "/calibration/temp/set"
#define TOPIC_TCAL_STATE   TOPIC_PREFIX "/calibration/temp/state"
#define TOPIC_HCAL_SET     TOPIC_PREFIX "/calibration/hum/set"
#define TOPIC_HCAL_STATE   TOPIC_PREFIX "/calibration/hum/state"
#define TOPIC_SLEEP_SET    TOPIC_PREFIX "/sleep/set"
#define TOPIC_SLEEP_STATE  TOPIC_PREFIX "/sleep/state"
#define TOPIC_AUTOSLEEP_SET  TOPIC_PREFIX "/autosleep/set"
#define TOPIC_AUTOSLEEP_STATE TOPIC_PREFIX "/autosleep/state"

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttPublish   = 0;
unsigned long lastMqttReconnect = 0;
const unsigned long MQTT_PUBLISH_INTERVAL   = 30000;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

// ====== Pins ======
#define TFT_CS    9
#define TFT_DC    8
#define TFT_RST   7
#define TFT_MOSI  6
#define TFT_SCLK  5
#define ENC_A_PIN   10
#define ENC_B_PIN   20
#define ENC_BTN_PIN 21
#define KEY0_PIN    0
#define SDA_PIN 1
#define SCL_PIN 2
#define LED_PIN  4
#define BUZZ_PIN 3

// ====== TFT & Sensors ======
Adafruit_ST7735  tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_AHTX0   aht;
ScioSense_ENS160 ens160(0x53);

// ====== Colors ======
#define CYBER_BG      ST77XX_BLACK
#define CYBER_GREEN   0x07E0
#define CYBER_ACCENT  0x07FF
#define CYBER_LIGHT   0xFD20
#define CYBER_PINK    0xF81F
#define AQ_BAR_GREEN  0x07E0
#define AQ_BAR_YELLOW 0xFFE0
#define AQ_BAR_ORANGE 0xFD20
#define AQ_BAR_RED    0xF800
#define CYBER_DARK    0x4208
#define CYBER_GREY    0x9CF3    // Light grey for labels
#define CLR_CLOCK     0xFFFF    // Beyaz - saat
#define CLR_HUM       0x0E9F    // Turkuaz - nem
#define CLR_TEMP      0xFCA0    // Amber/turuncu - sicaklik
#define CLR_DATE      0xC37F    // Lavanta/acik mor - tarih
#define CLR_WEATHER   0x47E0    // Yesil - hava durumu
#define CLR_WDETAIL   0xAD55    // Acik gri - hava detay

#ifndef PI
#define PI 3.1415926
#endif

// ====== Volume ======
uint8_t volumeLevel = 80;  // 0-100 percent

// ====== Temperature Calibration ======
float tempOffset = 0.0;  // -15.0 to +15.0 degrees
float humOffset = 0.0;   // -20.0 to +20.0 percent

// ====== Sleep timeout (declared early for saveSettings/loadSettings) ======
unsigned long sleepTimeoutMs = 60000;    // Auto-sleep after 60s inactivity (0=disabled)

// ====== Persistent Storage (NVS) ======
Preferences prefs;

// Forward declare for saveSettings/loadSettings
#define ALARM_COUNT_EARLY 8
uint8_t selectedAlarm = 0;

void saveSettings() {
  prefs.begin("cyberclock", false);  // read-write
  prefs.putFloat("tempOff", tempOffset);
  prefs.putFloat("humOff", humOffset);
  prefs.putUChar("volume", volumeLevel);
  prefs.putULong("sleepMs", sleepTimeoutMs);
  prefs.putUChar("alarmSnd", selectedAlarm);
  prefs.end();
}

void loadSettings() {
  prefs.begin("cyberclock", true);  // read-only
  tempOffset = prefs.getFloat("tempOff", 0.0);
  humOffset = prefs.getFloat("humOff", 0.0);
  volumeLevel = prefs.getUChar("volume", 80);
  sleepTimeoutMs = prefs.getULong("sleepMs", 60000);
  selectedAlarm = prefs.getUChar("alarmSnd", 0);
  prefs.end();
  selectedAlarm = constrain(selectedAlarm, 0, ALARM_COUNT_EARLY-1);
  // Clamp values
  tempOffset = constrain(tempOffset, -15.0f, 15.0f);
  humOffset = constrain(humOffset, -20.0f, 20.0f);
  volumeLevel = constrain(volumeLevel, 0, 100);
}

// Helper: play tone with volume control via PWM duty cycle
unsigned long vToneEndMs = 0;
bool buzzAttached = false;

void vTone(int pin, int freq, int durMs) {
  if (volumeLevel == 0) return;
  if (freq <= 0) { vToneStop(); return; }
  int duty = map(volumeLevel, 0, 100, 0, 128);
  if (buzzAttached) {
    ledcWrite(pin, 0);
    ledcDetach(pin);
    delayMicroseconds(800);
  }
  ledcAttach(pin, freq, 8);
  ledcWrite(pin, duty);
  buzzAttached = true;
  if (durMs > 0) vToneEndMs = millis() + durMs;
}

void vToneStop() {
  if (buzzAttached) {
    ledcWrite(BUZZ_PIN, 0);
    ledcDetach(BUZZ_PIN);
    buzzAttached = false;
  }
  vToneEndMs = 0;
}

void vToneUpdate() {
  if (vToneEndMs > 0 && millis() >= vToneEndMs) {
    vToneStop();
  }
}

// ====== Modes ======
enum UIMode { MODE_MENU=0, MODE_CLOCK, MODE_POMODORO, MODE_ALARM, MODE_GAMES_MENU, MODE_GAME, MODE_TETRIS, MODE_BREAKOUT, MODE_FLAPPY, MODE_PONG, MODE_2048, MODE_LED, MODE_HOME, MODE_SETTINGS, MODE_SLEEP };
UIMode currentMode = MODE_CLOCK;
UIMode preSleepMode = MODE_CLOCK;  // Remember mode before sleep
int menuIndex = 0;
const int MENU_ITEMS = 8;  // Monitor, Pomodoro, Alarm, Oyunlar, Edison LED, Ev, Ayarlar, Uyku

// ====== Games Sub-Menu ======
int gamesMenuIndex = 0;
const int GAMES_COUNT = 6;
const char* gameNames[] = {"Snake", "Tetris", "Breakout", "Flappy Bird", "Pong", "2048"};

// ====== Sleep Mode ======
bool screenSleeping = false;
unsigned long lastActivityMs = 0;        // Last user interaction timestamp
unsigned long k0HoldStart = 0;           // K0 long press detection
bool k0Held = false;

// ====== Edison LED ======
enum LedMode { LED_OFF=0, LED_ON, LED_DIM, LED_BREATHE, LED_BLINK, LED_SOS };
LedMode ledMode = LED_OFF;
uint8_t ledBrightness = 200;
int ledMenuSelection = 0;
unsigned long lastLedUpdate = 0;
int breatheDir = 1, breatheVal = 0;
int sosStep = 0;
unsigned long sosLastMs = 0;
const int LED_MODE_COUNT = 6;
const char* ledModeNames[] = {"Kapali", "Acik", "Dim", "Nefes", "Blink", "SOS"};

// ====== Melodies ======
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_Bb4 466
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_REST 0

// ====== RTTTL Parser & Player ======
#define RTTTL_MAX_NOTES 64
int rtttlNotes[RTTTL_MAX_NOTES];
int rtttlDurs[RTTTL_MAX_NOTES];
int rtttlLen = 0;

const int rtttlFreqs[] = {0, 262,277,294,311,330,349,370,392,415,440,466,494};
//                         p  c   c#  d   d#  e   f   f#  g   g#  a   a#  b

int rtttlNoteFreq(int note, int octave) {
  if (note == 0) return 0;
  int f = rtttlFreqs[note];
  int shift = octave - 4;
  if (shift > 0) for(int i=0;i<shift;i++) f *= 2;
  if (shift < 0) for(int i=0;i<-shift;i++) f /= 2;
  return f;
}

int parseRTTTL(const char* rtttl) {
  rtttlLen = 0;
  const char* p = rtttl;
  while(*p && *p != ':') p++;
  if(*p == ':') p++;
  int defDur = 4, defOct = 6, bpm = 63;
  while(*p && *p != ':') {
    while(*p == ' ' || *p == ',') p++;
    if(*p == 'd' && *(p+1) == '=') { p+=2; defDur = atoi(p); while(*p >= '0' && *p <= '9') p++; }
    else if(*p == 'o' && *(p+1) == '=') { p+=2; defOct = atoi(p); while(*p >= '0' && *p <= '9') p++; }
    else if(*p == 'b' && *(p+1) == '=') { p+=2; bpm = atoi(p); while(*p >= '0' && *p <= '9') p++; }
    else p++;
  }
  if(*p == ':') p++;
  int wholeNote = (60000UL * 4) / bpm;

  while(*p && rtttlLen < RTTTL_MAX_NOTES) {
    while(*p == ' ' || *p == ',') p++;
    if(!*p) break;
    int dur = 0;
    while(*p >= '0' && *p <= '9') { dur = dur*10 + (*p-'0'); p++; }
    if(dur == 0) dur = defDur;
    int note = 0;
    switch(*p) {
      case 'c': note=1; break; case 'd': note=3; break; case 'e': note=5; break;
      case 'f': note=6; break; case 'g': note=8; break; case 'a': note=10; break;
      case 'b': note=12; break; case 'p': note=0; break;
    }
    if(*p) p++;
    if(*p == '#') { note++; p++; }
    bool dotted = false;
    int oct = defOct;
    if(*p >= '3' && *p <= '8') { oct = *p - '0'; p++; }
    if(*p == '.') { dotted = true; p++; }
    int ms = wholeNote / dur;
    if(dotted) ms = ms * 3 / 2;
    rtttlNotes[rtttlLen] = rtttlNoteFreq(note, oct);
    rtttlDurs[rtttlLen] = ms;
    rtttlLen++;
  }
  return rtttlLen;
}

// RTTTL alarm melodileri
const char* rtttlAlarms[] = {
  "Klasik:d=4,o=5,b=140:c,e,g,e,c,e,g,p",
  "Sabah:d=4,o=5,b=100:e4,g4,a4,p,g4,a4,c,p,a4,c,e,p",
  "Nokia:d=8,o=5,b=225:e6,d6,4f,4g,c#6,b,4d,4e,b,a,4c,4e,2a",
  "Godfather:d=4,o=5,b=120:8g4,8c,8d#,8d,c,8a#4,8c,8g#4,2g4,8p,8g4,8c,8d#,8d,c,8g#4,8a4,8a#4,2c",
  "StarWars:d=4,o=5,b=108:8d4,8d4,8d4,2g4,2d,8c,8b4,8a4,2g,d,8c,8b4,8a4,2g,d,8c,8b4,8c,2a4",
  "MissionImp:d=16,o=6,b=150:d5,8p,d5,8p,d5,8p,d#5,p,e5,8p,d5,8p,d5,8p,d5,8p,d#5,p,e5,8p,d5,8p,d5,8p,e5,p,f5,8p,f5,p,e5,e5,d5",
  "TakeOnMe:d=8,o=4,b=160:f#5,f#5,d5,b,p,b,p,e5,p,e5,p,e5,g#5,g#5,a5,b5,a5,a5,a5,e5,p,d5,p,f#5,p,f#5,p,f#5,e5,e5,f#5,e5",
  "Macarena:d=8,o=5,b=180:f,f,f,4f,f,f,f,f,f,f,d,c",
};
const char* rtttlNames[] = {"Klasik", "Sabah", "Nokia", "Baba", "StarWars", "Gorev", "TakeOnMe", "Macarena"};
const int ALARM_COUNT = 8;

int parsedAlarmIdx = -1;

void playAlarmByIndex(int idx, bool loop) {
  if(idx < 0 || idx >= ALARM_COUNT) idx = 0;
  parseRTTTL(rtttlAlarms[idx]);
  parsedAlarmIdx = idx;
  startMelody(rtttlNotes, rtttlDurs, rtttlLen, loop);
}

const int co2AlarmMelody[]    = {NOTE_A4, NOTE_REST, NOTE_A4, NOTE_REST, NOTE_A4, NOTE_REST, NOTE_E5, NOTE_REST};
const int co2AlarmDurations[] = {200, 100, 200, 100, 200, 300, 400, 500};
const int CO2_ALARM_MELODY_LEN = 8;
const int pomoDoneMelody[]    = {NOTE_E4, NOTE_G4, NOTE_C5, NOTE_REST, NOTE_C5, NOTE_REST};
const int pomoDoneDurations[] = {200, 200, 400, 100, 400, 200};
const int POMO_MELODY_LEN = 6;
const int notifMelody[]    = {NOTE_A4, NOTE_REST, NOTE_A4, NOTE_REST};
const int notifDurations[] = {100, 50, 100, 50};
const int NOTIF_MELODY_LEN = 4;

bool melodyPlaying = false;
const int* melodyNotes = NULL;
const int* melodyDurs = NULL;
int melodyLen = 0, melodyIdx = 0;
unsigned long melodyLastMs = 0;
bool melodyLoop = false;

// ====== Pomodoro (free timer) ======
enum PomodoroState { POMO_SELECT=0, POMO_RUNNING, POMO_PAUSED, POMO_DONE };
PomodoroState pomoState = POMO_SELECT;
uint8_t pomoMinutes = 5;
uint8_t pomoSeconds = 0;
int pomoField = 0;
unsigned long pomoStartMillis = 0, pomoPausedMillis = 0;
int prevPomoRemainSec = -1;
bool prevPomoForce = true;

// ====== Env values ======
float curTemp = 0, curHum = 0;
uint16_t curTVOC = 0, curECO2 = 400;
unsigned long lastEnvRead = 0;

// ====== Clock vars ======
int prevSecond = -1;
String prevTimeStr = "";

// ====== Encoder & Buttons ======
int lastEncA = HIGH, lastEncB = HIGH;
bool lastEncBtn = HIGH, lastKey0 = HIGH;
unsigned long lastBtnMs = 0;

// ====== Alarm ======
bool alarmEnabled = false;
uint8_t alarmHour = 7, alarmMinute = 0;
bool alarmRinging = false;
int alarmSelectedField = 0, lastAlarmDayTriggered = -1;

// ====== Alert ======
enum AlertLevel { ALERT_NONE=0, ALERT_CO2, ALERT_ALARM };
AlertLevel currentAlertLevel = ALERT_NONE;
unsigned long lastCo2BlinkMs = 0;
bool co2BlinkOn = false;
bool alertLedActive = false;
unsigned long lastAlertLedMs = 0;
bool alertLedState = false;
bool co2AlertTriggered = false;  // True when CO2 alarm is actively ringing

// ====== Snake Game ======
#define SNK_COLS 20
#define SNK_ROWS 16
#define SNK_BLOCK 6
#define SNK_X 20
#define SNK_Y 14
int snakeX[320], snakeY[320];
int snakeLen = 3;
int snakeDir = 0;
int snkDirQueue[4];       // Direction input queue (max 2 buffered turns)
int snkDirQueueLen = 0;
int foodX, foodY;
int snkScore = 0;
bool snkGameOver = false, snkInited = false;
unsigned long snkLastMove = 0, snkMoveInterval = 200;

// ====== Tetris ======
// Forward declaration needed (Tetris K0 returns to games menu)
void drawGamesMenu();
// Tetris uses landscape mode (rotation 1) — screen becomes 160x128
// Layout: Left = game grid, Right = score/next/info
#define TET_COLS 10
#define TET_ROWS 20
#define TET_BLOCK 6    // 20*6=120px height (fits 128px), 10*6=60px wide
#define TET_X 1        // Left aligned with 1px margin
#define TET_Y 4        // Top margin to center vertically (128-120)/2=4
uint8_t tetBoard[TET_ROWS][TET_COLS];
int tetPieceX, tetPieceY, tetPieceType, tetPieceRot;
int tetNextPiece;
unsigned long tetLastDrop = 0, tetDropInterval = 500;
int tetScore = 0;
bool tetGameOver = false, tetInited = false;
unsigned long encBtnHoldStart = 0;
bool encBtnHeld = false;

const uint16_t tetPieces[7][4] = {
  {0x0F00, 0x2222, 0x00F0, 0x4444},
  {0x0E20, 0x44C0, 0x8E00, 0x6440},
  {0x0E80, 0xC440, 0x2E00, 0x4460},
  {0x0660, 0x0660, 0x0660, 0x0660},
  {0x06C0, 0x4620, 0x06C0, 0x4620},
  {0x0E40, 0x4C40, 0x4E00, 0x4640},
  {0x0C60, 0x2640, 0x0C60, 0x2640},
};
const uint16_t tetColors[] = {CYBER_ACCENT, 0x001F, CYBER_LIGHT, AQ_BAR_YELLOW, CYBER_GREEN, CYBER_PINK, AQ_BAR_RED};

bool tetGetBlock(int type, int rot, int r, int c) {
  uint16_t mask = tetPieces[type][rot];
  int bit = r * 4 + c;
  return (mask >> (15 - bit)) & 1;
}

void tetDrawBlock(int col, int row, uint16_t color) {
  if (row < 0 || row >= TET_ROWS || col < 0 || col >= TET_COLS) return;
  int x = TET_X + col * TET_BLOCK;
  int y = TET_Y + row * TET_BLOCK;
  tft.fillRect(x, y, TET_BLOCK-1, TET_BLOCK-1, color ? color : CYBER_BG);
}

void tetDrawBoard() {
  for (int r = 0; r < TET_ROWS; r++)
    for (int c = 0; c < TET_COLS; c++)
      tetDrawBlock(c, r, tetBoard[r][c] ? tetColors[tetBoard[r][c]-1] : 0);
}

void tetDrawPiece(int px, int py, int type, int rot, uint16_t color) {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (tetGetBlock(type, rot, r, c)) {
        int dr = py+r, dc = px+c;
        if (dr >= 0 && dr < TET_ROWS && dc >= 0 && dc < TET_COLS) {
          if (color == 0 && tetBoard[dr][dc] == 0)
            tetDrawBlock(dc, dr, 0);
          else if (color != 0)
            tetDrawBlock(dc, dr, color);
        }
      }
}

bool tetCollision(int px, int py, int type, int rot) {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (tetGetBlock(type, rot, r, c)) {
        int nr = py + r, nc = px + c;
        if (nc < 0 || nc >= TET_COLS || nr >= TET_ROWS) return true;
        if (nr >= 0 && tetBoard[nr][nc]) return true;
      }
  return false;
}

void tetLockPiece() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (tetGetBlock(tetPieceType, tetPieceRot, r, c)) {
        int nr = tetPieceY + r, nc = tetPieceX + c;
        if (nr >= 0 && nr < TET_ROWS && nc >= 0 && nc < TET_COLS)
          tetBoard[nr][nc] = tetPieceType + 1;
      }
}

int tetClearLines() {
  int cleared = 0;
  for (int r = TET_ROWS - 1; r >= 0; r--) {
    bool full = true;
    for (int c = 0; c < TET_COLS; c++) if (!tetBoard[r][c]) { full = false; break; }
    if (full) {
      cleared++;
      for (int rr = r; rr > 0; rr--)
        for (int c = 0; c < TET_COLS; c++) tetBoard[rr][c] = tetBoard[rr-1][c];
      for (int c = 0; c < TET_COLS; c++) tetBoard[0][c] = 0;
      r++;
    }
  }
  return cleared;
}

void tetSpawnPiece() {
  tetPieceType = tetNextPiece;
  tetNextPiece = random(7);
  tetPieceX = 3;
  tetPieceY = -1;
  tetPieceRot = 0;
  if (tetCollision(tetPieceX, tetPieceY, tetPieceType, tetPieceRot)) {
    tetGameOver = true;
  }
}

void tetDrawScore() {
  int px = TET_X + TET_COLS*TET_BLOCK + 6;
  tft.fillRect(px, 28, 90, 20, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(px, 30); tft.print("Skor:");
  tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(px + 35, 30); tft.printf("%d", tetScore);
}

void tetDrawNext() {
  int px = TET_X + TET_COLS*TET_BLOCK + 6;
  int oy = 52;
  tft.fillRect(px, oy, 90, 40, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(px, oy); tft.print("Next:");
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (tetGetBlock(tetNextPiece, 0, r, c))
        tft.fillRect(px + 40 + c*7, oy + r*7, 6, 6, tetColors[tetNextPiece]);
}

void initTetris() {
  tetInited = true;
  tetGameOver = false;
  tetScore = 0;
  tetDropInterval = 500;
  encBtnHoldStart = 0;
  encBtnHeld = false;
  memset(tetBoard, 0, sizeof(tetBoard));
  tetNextPiece = random(7);
  tetSpawnPiece();

  // Landscape mode (160x128)
  tft.setRotation(1);

  tft.fillScreen(CYBER_BG);

  // Left: game grid border
  tft.drawRect(TET_X-1, TET_Y-1, TET_COLS*TET_BLOCK+2, TET_ROWS*TET_BLOCK+2, ST77XX_WHITE);

  // Right panel (x=65 to 159, ~93px wide)
  int px = TET_X + TET_COLS*TET_BLOCK + 6;

  // Title
  tft.setTextSize(2); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(px, 4); tft.print("TETRIS");

  // Divider line
  tft.drawFastHLine(px, 24, 90, CYBER_DARK);

  // Controls help (bottom of right panel)
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(px, 100); tft.print("Enc:L/R Bas:Don");
  tft.setCursor(px, 112); tft.print("K0:Cikis");

  tetDrawScore();
  tetDrawNext();
  tetDrawBoard();
}

void updateTetris(int enc, bool encP, bool back) {
  if (back) { tetInited=false; currentMode=MODE_GAMES_MENU; drawGamesMenu(); return; }

  if (tetGameOver) {
    int px = TET_X + TET_COLS*TET_BLOCK + 6;
    tft.fillRect(px, 28, 90, 70, CYBER_BG);
    tft.setTextSize(2); tft.setTextColor(ST77XX_RED, CYBER_BG);
    tft.setCursor(px, 30); tft.print("GAME");
    tft.setCursor(px, 48); tft.print("OVER!");
    tft.setTextSize(1); tft.setTextColor(CYBER_GREEN, CYBER_BG);
    tft.setCursor(px, 70); tft.printf("Skor: %d", tetScore);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(px, 85); tft.print("Bas: yeni oyun");
    if (encP || enc != 0) { initTetris(); }
    return;
  }

  bool encBtnCur = digitalRead(ENC_BTN_PIN);
  if (encBtnCur == LOW) {
    if (encBtnHoldStart == 0) encBtnHoldStart = millis();
    if (!encBtnHeld && millis() - encBtnHoldStart > 300) {
      encBtnHeld = true;
      tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, 0);
      while (!tetCollision(tetPieceX, tetPieceY + 1, tetPieceType, tetPieceRot)) {
        tetPieceY++;
      }
      tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, tetColors[tetPieceType]);
      tetLockPiece();
      int lines = tetClearLines();
      if (lines > 0) {
        int pts[] = {0, 100, 300, 500, 800};
        tetScore += pts[lines];
        vTone(BUZZ_PIN, 1000 + lines * 200, 100);
        if (tetDropInterval > 150) tetDropInterval -= 20;
        tetDrawBoard(); tetDrawScore();
      }
      tetSpawnPiece(); tetDrawNext();
      tetLastDrop = millis();
      vTone(BUZZ_PIN, 400, 30);
    }
  } else {
    if (encBtnHoldStart > 0 && !encBtnHeld) {
      tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, 0);
      int newRot = (tetPieceRot + 1) % 4;
      if (!tetCollision(tetPieceX, tetPieceY, tetPieceType, newRot)) {
        tetPieceRot = newRot;
      }
      tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, tetColors[tetPieceType]);
    }
    encBtnHoldStart = 0;
    encBtnHeld = false;
  }

  if (enc != 0) {
    tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, 0);
    int newX = tetPieceX + enc;
    if (!tetCollision(newX, tetPieceY, tetPieceType, tetPieceRot)) {
      tetPieceX = newX;
    }
    tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, tetColors[tetPieceType]);
  }

  unsigned long now = millis();
  if (now - tetLastDrop > tetDropInterval) {
    tetLastDrop = now;
    tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, 0);

    if (!tetCollision(tetPieceX, tetPieceY + 1, tetPieceType, tetPieceRot)) {
      tetPieceY++;
    } else {
      tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, tetColors[tetPieceType]);
      tetLockPiece();
      int lines = tetClearLines();
      if (lines > 0) {
        int pts[] = {0, 100, 300, 500, 800};
        tetScore += pts[lines];
        vTone(BUZZ_PIN, 1000 + lines * 200, 100);
        if (tetDropInterval > 150) tetDropInterval -= 20;
        tetDrawBoard();
        tetDrawScore();
      }
      tetSpawnPiece();
      tetDrawNext();
    }
    tetDrawPiece(tetPieceX, tetPieceY, tetPieceType, tetPieceRot, tetColors[tetPieceType]);
  }
}

// ============================================================
// BREAKOUT (Tuğla Kırma) — Landscape mode (160x128)
// ============================================================
// Layout: Left 130px = game area, Right 30px = score/lives panel
#define BRK_AREA_W 130   // Game area width
#define BRK_AREA_H 128   // Full height
#define BRK_AREA_X 0
#define BRK_PANEL_X 132  // Right panel start
#define BRK_COLS 13
#define BRK_ROWS 5
#define BRK_BRICK_W 10   // 13*10=130
#define BRK_BRICK_H 5
#define BRK_BRICK_Y 2    // Bricks start Y
#define BRK_PAD_W 26
#define BRK_PAD_H 3
#define BRK_PAD_Y 122    // Paddle near bottom

uint8_t brkBricks[BRK_ROWS][BRK_COLS];  // 0=empty, 1-6=color tier
float brkBallX, brkBallY, brkBallDX, brkBallDY;
int brkPadX;  // Paddle center X (in game area coords)
int brkScore = 0;
int brkLives = 3;
int brkBricksLeft = 0;
bool brkGameOver = false, brkInited = false, brkBallActive = false;
unsigned long brkLastFrame = 0;
const unsigned long BRK_FRAME_MS = 20;  // ~50fps

const uint16_t brkRowColors[] = {AQ_BAR_RED, AQ_BAR_ORANGE, AQ_BAR_YELLOW, CYBER_GREEN, CYBER_ACCENT};

void brkDrawBrick(int col, int row, uint16_t color) {
  int x = BRK_AREA_X + col * BRK_BRICK_W;
  int y = BRK_BRICK_Y + row * BRK_BRICK_H;
  if (color) {
    tft.fillRect(x+1, y+1, BRK_BRICK_W-2, BRK_BRICK_H-2, color);
  } else {
    tft.fillRect(x, y, BRK_BRICK_W, BRK_BRICK_H, CYBER_BG);
  }
}

void brkDrawPaddle(uint16_t color) {
  int px = BRK_AREA_X + brkPadX - BRK_PAD_W/2;
  tft.fillRect(BRK_AREA_X, BRK_PAD_Y, BRK_AREA_W, BRK_PAD_H, CYBER_BG);
  tft.fillRect(px, BRK_PAD_Y, BRK_PAD_W, BRK_PAD_H, color);
}

void brkDrawBall(uint16_t color) {
  tft.fillRect((int)brkBallX, (int)brkBallY, 3, 3, color);
}

void brkDrawScore() {
  tft.fillRect(BRK_PANEL_X, 20, 28, 22, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(BRK_PANEL_X, 20); tft.print("Skr");
  tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(BRK_PANEL_X, 32); tft.printf("%d", brkScore);
}

void brkDrawLives() {
  tft.fillRect(BRK_PANEL_X, 50, 28, 22, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(BRK_PANEL_X, 50); tft.print("Can");
  tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(BRK_PANEL_X, 62);
  for(int i=0; i<brkLives; i++) tft.print("*");
}

void brkResetBall() {
  brkBallActive = false;
  brkBallX = BRK_AREA_X + brkPadX;
  brkBallY = BRK_PAD_Y - 5;
  brkBallDX = 1.2;
  brkBallDY = -1.5;
}

void initBreakout() {
  brkInited = true;
  brkGameOver = false;
  brkScore = 0;
  brkLives = 3;
  brkBricksLeft = BRK_ROWS * BRK_COLS;
  brkPadX = BRK_AREA_W / 2;
  
  // Fill bricks
  for (int r = 0; r < BRK_ROWS; r++)
    for (int c = 0; c < BRK_COLS; c++)
      brkBricks[r][c] = r + 1;

  brkResetBall();

  // Landscape mode (same as normal)
  tft.setRotation(1);
  tft.fillScreen(CYBER_BG);
  
  // Game area border (left side)
  tft.drawFastVLine(BRK_AREA_W, 0, BRK_AREA_H, ST77XX_WHITE);
  
  // Right panel title
  tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(BRK_PANEL_X, 4); tft.print("BRK");
  tft.drawFastHLine(BRK_PANEL_X, 15, 26, CYBER_DARK);
  
  // Draw all bricks
  for (int r = 0; r < BRK_ROWS; r++)
    for (int c = 0; c < BRK_COLS; c++)
      brkDrawBrick(c, r, brkRowColors[r]);
  
  brkDrawPaddle(ST77XX_WHITE);
  brkDrawScore();
  brkDrawLives();
  brkDrawBall(ST77XX_WHITE);
}

void updateBreakout(int enc, bool encP, bool back) {
  if (back) { brkInited=false; currentMode=MODE_GAMES_MENU; drawGamesMenu(); return; }
  
  if (brkGameOver) {
    tft.setTextSize(2); tft.setTextColor(ST77XX_RED, CYBER_BG);
    tft.setCursor(10, 45); tft.print("GAME OVER");
    tft.setTextSize(1); tft.setTextColor(CYBER_GREEN, CYBER_BG);
    tft.setCursor(25, 70); tft.printf("Skor: %d", brkScore);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(15, 90); tft.print("Bas: yeni oyun");
    if (encP || enc != 0) initBreakout();
    return;
  }
  
  // Win check
  if (brkBricksLeft <= 0) {
    tft.setTextSize(2); tft.setTextColor(CYBER_GREEN, CYBER_BG);
    tft.setCursor(5, 45); tft.print("KAZANDIN!");
    tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
    tft.setCursor(25, 70); tft.printf("Skor: %d", brkScore);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(15, 90); tft.print("Bas: yeni oyun");
    if (encP || enc != 0) initBreakout();
    return;
  }
  
  // Move paddle with encoder
  if (enc != 0) {
    brkDrawPaddle(CYBER_BG);  // Erase old
    brkPadX = constrain(brkPadX + enc * 4, BRK_PAD_W/2, BRK_AREA_W - BRK_PAD_W/2);
    brkDrawPaddle(ST77XX_WHITE);
    if (!brkBallActive) {
      // Ball follows paddle before launch
      brkDrawBall(CYBER_BG);
      brkBallX = BRK_AREA_X + brkPadX - 1;
      brkBallY = BRK_PAD_Y - 5;
      brkDrawBall(ST77XX_WHITE);
    }
  }
  
  // Launch ball
  if (encP && !brkBallActive) {
    brkBallActive = true;
    brkBallDX = (random(2) == 0) ? 1.2 : -1.2;
    brkBallDY = -1.5;
    vTone(BUZZ_PIN, 800, 30);
  }
  
  if (!brkBallActive) return;
  
  // Frame timing
  unsigned long now = millis();
  if (now - brkLastFrame < BRK_FRAME_MS) return;
  brkLastFrame = now;
  
  // Erase ball
  brkDrawBall(CYBER_BG);
  
  // Move ball
  float newX = brkBallX + brkBallDX;
  float newY = brkBallY + brkBallDY;
  
  // Wall bounce (left/right)
  if (newX <= BRK_AREA_X || newX >= BRK_AREA_X + BRK_AREA_W - 3) {
    brkBallDX = -brkBallDX;
    newX = brkBallX + brkBallDX;
    vTone(BUZZ_PIN, 400, 15);
  }
  
  // Top bounce
  if (newY <= 0) {
    brkBallDY = -brkBallDY;
    newY = brkBallY + brkBallDY;
    vTone(BUZZ_PIN, 400, 15);
  }
  
  // Paddle collision
  if (newY >= BRK_PAD_Y - 3 && newY <= BRK_PAD_Y && brkBallDY > 0) {
    int padLeft = BRK_AREA_X + brkPadX - BRK_PAD_W/2;
    int padRight = padLeft + BRK_PAD_W;
    if (newX + 3 >= padLeft && newX <= padRight) {
      brkBallDY = -brkBallDY;
      // Angle based on where ball hits paddle
      float hitPos = (newX - padLeft) / (float)BRK_PAD_W;  // 0..1
      brkBallDX = (hitPos - 0.5f) * 3.0f;  // -1.5 to +1.5
      if (brkBallDX > -0.5f && brkBallDX < 0.5f) brkBallDX = (brkBallDX < 0) ? -0.5f : 0.5f;
      newY = BRK_PAD_Y - 4;
      vTone(BUZZ_PIN, 600, 30);
    }
  }
  
  // Miss — ball goes below paddle
  if (newY > BRK_AREA_H - 2) {
    brkLives--;
    brkDrawLives();
    if (brkLives <= 0) {
      brkGameOver = true;
      vTone(BUZZ_PIN, 200, 500);
      return;
    }
    vTone(BUZZ_PIN, 300, 200);
    brkResetBall();
    brkDrawBall(ST77XX_WHITE);
    return;
  }
  
  // Brick collision
  int bCol = ((int)newX - BRK_AREA_X) / BRK_BRICK_W;
  int bRow = ((int)newY - BRK_BRICK_Y) / BRK_BRICK_H;
  if (bRow >= 0 && bRow < BRK_ROWS && bCol >= 0 && bCol < BRK_COLS && brkBricks[bRow][bCol] > 0) {
    brkBricks[bRow][bCol] = 0;
    brkBricksLeft--;
    brkScore += (BRK_ROWS - bRow) * 10;  // Top rows = more points
    brkBallDY = -brkBallDY;
    brkDrawBrick(bCol, bRow, 0);
    brkDrawScore();
    vTone(BUZZ_PIN, 1000 + bRow * 100, 30);
    // Slight speed up
    if (brkBallDY > 0) brkBallDY += 0.02; else brkBallDY -= 0.02;
  }
  
  brkBallX = newX;
  brkBallY = newY;
  brkDrawBall(ST77XX_WHITE);
}

// ============================================================
// FLAPPY BIRD — Landscape mode (160x128)
// ============================================================
// Layout: Left 130px = game, Right 30px = score panel
#define FLP_AREA_W 130
#define FLP_AREA_H 128
#define FLP_PANEL_X 132
#define FLP_BIRD_X 25      // Bird fixed X position
#define FLP_BIRD_SIZE 6
#define FLP_PIPE_W 12
#define FLP_GAP 36         // Gap between top and bottom pipes
#define FLP_PIPE_SPEED 2
#define FLP_GRAVITY 0.35f
#define FLP_FLAP_FORCE -3.5f
#define FLP_MAX_PIPES 3

struct FlpPipe {
  int x;
  int gapY;   // Center of gap
  bool scored; // Already scored?
  bool active;
};

FlpPipe flpPipes[FLP_MAX_PIPES];
float flpBirdY, flpBirdVY;
int flpScore = 0;
int flpBestScore = 0;
bool flpGameOver = false, flpInited = false, flpStarted = false;
unsigned long flpLastFrame = 0;
const unsigned long FLP_FRAME_MS = 25;  // ~40fps
int flpPipeSpawnTimer = 0;

void flpDrawBird(uint16_t color) {
  int by = (int)flpBirdY;
  // Bird body
  tft.fillRect(FLP_BIRD_X, by, FLP_BIRD_SIZE, FLP_BIRD_SIZE, color);
  if (color != CYBER_BG) {
    // Eye
    tft.drawPixel(FLP_BIRD_X + 4, by + 1, CYBER_BG);
    // Beak
    tft.fillRect(FLP_BIRD_X + FLP_BIRD_SIZE, by + 2, 3, 2, AQ_BAR_ORANGE);
  } else {
    tft.fillRect(FLP_BIRD_X + FLP_BIRD_SIZE, by + 2, 3, 2, CYBER_BG);
  }
}

void flpDrawPipe(int idx, uint16_t color) {
  if (!flpPipes[idx].active || flpPipes[idx].x < -FLP_PIPE_W || flpPipes[idx].x > FLP_AREA_W) return;
  int drawX = max(0, flpPipes[idx].x);
  int drawW = min(FLP_PIPE_W, FLP_AREA_W - drawX);
  if (flpPipes[idx].x < 0) drawW = FLP_PIPE_W + flpPipes[idx].x;
  if (drawW <= 0) return;
  // Top pipe
  int topH = flpPipes[idx].gapY - FLP_GAP/2;
  if (topH > 0) tft.fillRect(drawX, 0, drawW, topH, color == CYBER_BG ? CYBER_BG : CYBER_GREEN);
  // Bottom pipe
  int botY = flpPipes[idx].gapY + FLP_GAP/2;
  int botH = FLP_AREA_H - botY;
  if (botH > 0) tft.fillRect(drawX, botY, drawW, botH, color == CYBER_BG ? CYBER_BG : CYBER_GREEN);
}

void flpDrawScore() {
  tft.fillRect(FLP_PANEL_X, 20, 28, 22, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(FLP_PANEL_X, 20); tft.print("Skr");
  tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(FLP_PANEL_X, 32); tft.printf("%d", flpScore);
}

void flpDrawBest() {
  tft.fillRect(FLP_PANEL_X, 50, 28, 22, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(FLP_PANEL_X, 50); tft.print("En");
  tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(FLP_PANEL_X, 62); tft.printf("%d", flpBestScore);
}

void flpSpawnPipe(int idx) {
  flpPipes[idx].x = FLP_AREA_W;
  flpPipes[idx].gapY = random(FLP_GAP/2 + 8, FLP_AREA_H - FLP_GAP/2 - 8);
  flpPipes[idx].scored = false;
  flpPipes[idx].active = true;
}

void initFlappy() {
  flpInited = true;
  flpGameOver = false;
  flpStarted = false;
  flpScore = 0;
  flpBirdY = FLP_AREA_H / 2 - FLP_BIRD_SIZE / 2;
  flpBirdVY = 0;
  flpPipeSpawnTimer = 0;
  
  for (int i = 0; i < FLP_MAX_PIPES; i++) flpPipes[i].active = false;
  
  tft.setRotation(1);
  tft.fillScreen(CYBER_BG);
  
  // Game area border
  tft.drawFastVLine(FLP_AREA_W, 0, FLP_AREA_H, ST77XX_WHITE);
  
  // Right panel
  tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(FLP_PANEL_X, 4); tft.print("FLP");
  tft.drawFastHLine(FLP_PANEL_X, 15, 26, CYBER_DARK);
  
  tft.setTextColor(CYBER_DARK);
  tft.setCursor(FLP_PANEL_X, 85); tft.print("Bas");
  tft.setCursor(FLP_PANEL_X, 95); tft.print(" zip");
  tft.setCursor(FLP_PANEL_X, 110); tft.print("K0");
  tft.setCursor(FLP_PANEL_X, 120); tft.print(" cik");
  
  flpDrawScore();
  flpDrawBest();
  flpDrawBird(AQ_BAR_YELLOW);
  
  // Start prompt
  tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, CYBER_BG);
  tft.setCursor(20, 60); tft.print("Bas: Baslat");
}

void updateFlappy(int enc, bool encP, bool back) {
  if (back) { flpInited=false; currentMode=MODE_GAMES_MENU; drawGamesMenu(); return; }
  
  if (flpGameOver) {
    tft.setTextSize(2); tft.setTextColor(ST77XX_RED, CYBER_BG);
    tft.setCursor(15, 35); tft.print("BITTI!");
    tft.setTextSize(1); tft.setTextColor(CYBER_GREEN, CYBER_BG);
    tft.setCursor(20, 60); tft.printf("Skor: %d", flpScore);
    if (flpScore > flpBestScore) {
      flpBestScore = flpScore;
      tft.setTextColor(CYBER_LIGHT, CYBER_BG);
      tft.setCursor(15, 75); tft.print("Yeni rekor!");
    }
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(10, 95); tft.print("Bas: tekrar");
    if (encP) initFlappy();
    return;
  }
  
  // Start game on first press
  if (!flpStarted) {
    if (encP) {
      flpStarted = true;
      // Clear start text
      tft.fillRect(0, 55, FLP_AREA_W, 15, CYBER_BG);
      flpBirdVY = FLP_FLAP_FORCE;
      vTone(BUZZ_PIN, 800, 30);
    }
    return;
  }
  
  // Flap
  if (encP) {
    flpBirdVY = FLP_FLAP_FORCE;
    vTone(BUZZ_PIN, 800, 20);
  }
  
  // Frame timing
  unsigned long now = millis();
  if (now - flpLastFrame < FLP_FRAME_MS) return;
  flpLastFrame = now;
  
  // Erase bird
  flpDrawBird(CYBER_BG);
  
  // Physics
  flpBirdVY += FLP_GRAVITY;
  flpBirdY += flpBirdVY;
  
  // Floor/ceiling
  if (flpBirdY < 0) { flpBirdY = 0; flpBirdVY = 0; }
  if (flpBirdY > FLP_AREA_H - FLP_BIRD_SIZE) {
    flpGameOver = true;
    vTone(BUZZ_PIN, 200, 300);
    return;
  }
  
  // Spawn pipes
  flpPipeSpawnTimer++;
  if (flpPipeSpawnTimer >= 28) {  // Every ~700ms
    flpPipeSpawnTimer = 0;
    for (int i = 0; i < FLP_MAX_PIPES; i++) {
      if (!flpPipes[i].active) { flpSpawnPipe(i); break; }
    }
  }
  
  // Update pipes
  for (int i = 0; i < FLP_MAX_PIPES; i++) {
    if (!flpPipes[i].active) continue;
    
    // Erase old position
    flpDrawPipe(i, CYBER_BG);
    
    // Move
    flpPipes[i].x -= FLP_PIPE_SPEED;
    
    // Off screen
    if (flpPipes[i].x < -FLP_PIPE_W) {
      flpPipes[i].active = false;
      continue;
    }
    
    // Collision check
    int bx = FLP_BIRD_X, by = (int)flpBirdY;
    int bx2 = bx + FLP_BIRD_SIZE, by2 = by + FLP_BIRD_SIZE;
    int px = flpPipes[i].x, px2 = px + FLP_PIPE_W;
    int topH = flpPipes[i].gapY - FLP_GAP/2;
    int botY = flpPipes[i].gapY + FLP_GAP/2;
    
    if (bx2 > px && bx < px2) {
      if (by < topH || by2 > botY) {
        flpGameOver = true;
        vTone(BUZZ_PIN, 200, 300);
        return;
      }
    }
    
    // Score
    if (!flpPipes[i].scored && flpPipes[i].x + FLP_PIPE_W < FLP_BIRD_X) {
      flpPipes[i].scored = true;
      flpScore++;
      flpDrawScore();
      vTone(BUZZ_PIN, 1200, 30);
    }
    
    // Draw new position
    flpDrawPipe(i, CYBER_GREEN);
  }
  
  // Draw bird
  flpDrawBird(AQ_BAR_YELLOW);
}

// ============================================================
// PONG — Landscape mode (160x128), AI opponent
// ============================================================
// Layout: Left 130px = game, Right 30px = score panel
#define PNG_AREA_W 130
#define PNG_AREA_H 128
#define PNG_PANEL_X 132
#define PNG_PAD_W 3
#define PNG_PAD_H 22
#define PNG_BALL_SIZE 3
#define PNG_P1_X 4         // Player paddle X (left side)
#define PNG_AI_X (PNG_AREA_W - PNG_PAD_W - 4)  // AI paddle X (right side)

float pngBallX, pngBallY, pngBallDX, pngBallDY;
int pngP1Y, pngAIY;   // Paddle Y positions (top edge)
int pngScoreP1 = 0, pngScoreAI = 0;
bool pngGameOver = false, pngInited = false, pngBallActive = false;
unsigned long pngLastFrame = 0;
const unsigned long PNG_FRAME_MS = 20;
int pngWinScore = 7;

void pngDrawPaddle(int x, int y, uint16_t color) {
  tft.fillRect(x, y, PNG_PAD_W, PNG_PAD_H, color);
}

void pngDrawBall(uint16_t color) {
  tft.fillRect((int)pngBallX, (int)pngBallY, PNG_BALL_SIZE, PNG_BALL_SIZE, color);
}

void pngDrawScores() {
  tft.fillRect(PNG_PANEL_X, 20, 28, 45, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(PNG_PANEL_X, 20); tft.print("Sen");
  tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(PNG_PANEL_X, 32); tft.printf("%d", pngScoreP1);
  tft.setTextColor(CYBER_DARK);
  tft.setCursor(PNG_PANEL_X, 48); tft.print("AI");
  tft.setTextColor(AQ_BAR_RED, CYBER_BG);
  tft.setCursor(PNG_PANEL_X, 60); tft.printf("%d", pngScoreAI);
}

void pngDrawCenterLine() {
  for (int y = 0; y < PNG_AREA_H; y += 8) {
    tft.drawFastVLine(PNG_AREA_W / 2, y, 4, CYBER_DARK);
  }
}

void pngResetBall(int dir) {
  pngBallActive = false;
  pngBallX = PNG_AREA_W / 2 - 1;
  pngBallY = PNG_AREA_H / 2 - 1;
  pngBallDX = dir * 2.0f;
  pngBallDY = (random(2) == 0) ? 1.2f : -1.2f;
}

void initPong() {
  pngInited = true;
  pngGameOver = false;
  pngScoreP1 = 0;
  pngScoreAI = 0;
  pngP1Y = PNG_AREA_H / 2 - PNG_PAD_H / 2;
  pngAIY = PNG_AREA_H / 2 - PNG_PAD_H / 2;
  pngResetBall(1);

  tft.setRotation(1);
  tft.fillScreen(CYBER_BG);
  
  // Game area border
  tft.drawFastVLine(PNG_AREA_W, 0, PNG_AREA_H, ST77XX_WHITE);
  pngDrawCenterLine();
  
  // Right panel
  tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(PNG_PANEL_X, 4); tft.print("PONG");
  tft.drawFastHLine(PNG_PANEL_X, 15, 26, CYBER_DARK);
  
  tft.setTextColor(CYBER_DARK);
  tft.setCursor(PNG_PANEL_X, 80); tft.print("Enc");
  tft.setCursor(PNG_PANEL_X, 90); tft.print(" U/D");
  tft.setCursor(PNG_PANEL_X, 105); tft.print("Bas");
  tft.setCursor(PNG_PANEL_X, 115); tft.print(" atla");
  
  pngDrawScores();
  pngDrawPaddle(PNG_P1_X, pngP1Y, CYBER_ACCENT);
  pngDrawPaddle(PNG_AI_X, pngAIY, AQ_BAR_RED);
  pngDrawBall(ST77XX_WHITE);
  
  // Start text
  tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, CYBER_BG);
  tft.setCursor(30, 60); tft.print("Bas: Baslat");
}

void updatePong(int enc, bool encP, bool back) {
  if (back) { pngInited=false; currentMode=MODE_GAMES_MENU; drawGamesMenu(); return; }
  
  if (pngGameOver) {
    tft.setTextSize(2);
    if (pngScoreP1 >= pngWinScore) {
      tft.setTextColor(CYBER_GREEN, CYBER_BG);
      tft.setCursor(10, 35); tft.print("KAZANDIN!");
    } else {
      tft.setTextColor(ST77XX_RED, CYBER_BG);
      tft.setCursor(10, 35); tft.print("KAYBETTIN");
    }
    tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
    tft.setCursor(20, 65); tft.printf("%d - %d", pngScoreP1, pngScoreAI);
    tft.setCursor(15, 85); tft.print("Bas: tekrar");
    if (encP) initPong();
    return;
  }
  
  // Move player paddle with encoder
  if (enc != 0) {
    pngDrawPaddle(PNG_P1_X, pngP1Y, CYBER_BG);
    pngP1Y = constrain(pngP1Y + enc * 5, 0, PNG_AREA_H - PNG_PAD_H);
    pngDrawPaddle(PNG_P1_X, pngP1Y, CYBER_ACCENT);
  }
  
  // Launch ball
  if (encP && !pngBallActive) {
    pngBallActive = true;
    // Clear start text
    tft.fillRect(20, 55, 100, 15, CYBER_BG);
    pngDrawCenterLine();
    vTone(BUZZ_PIN, 600, 30);
  }
  
  if (!pngBallActive) return;
  
  // Frame timing
  unsigned long now = millis();
  if (now - pngLastFrame < PNG_FRAME_MS) return;
  pngLastFrame = now;
  
  // Erase ball
  pngDrawBall(CYBER_BG);
  
  // AI movement — follows ball with slight delay/imperfection
  int aiTarget = (int)pngBallY - PNG_PAD_H / 2;
  int aiCenter = pngAIY + PNG_PAD_H / 2;
  int ballCenter = (int)pngBallY + PNG_BALL_SIZE / 2;
  if (abs(aiCenter - ballCenter) > 3) {
    pngDrawPaddle(PNG_AI_X, pngAIY, CYBER_BG);
    if (aiCenter < ballCenter) pngAIY += 2;
    else pngAIY -= 2;
    pngAIY = constrain(pngAIY, 0, PNG_AREA_H - PNG_PAD_H);
    pngDrawPaddle(PNG_AI_X, pngAIY, AQ_BAR_RED);
  }
  
  // Move ball
  float newX = pngBallX + pngBallDX;
  float newY = pngBallY + pngBallDY;
  
  // Top/bottom bounce
  if (newY <= 0 || newY >= PNG_AREA_H - PNG_BALL_SIZE) {
    pngBallDY = -pngBallDY;
    newY = pngBallY + pngBallDY;
    vTone(BUZZ_PIN, 300, 10);
  }
  
  // Player paddle collision (left)
  if (newX <= PNG_P1_X + PNG_PAD_W && pngBallDX < 0) {
    if (newY + PNG_BALL_SIZE >= pngP1Y && newY <= pngP1Y + PNG_PAD_H) {
      pngBallDX = -pngBallDX;
      // Angle based on hit position
      float hitPos = ((newY + PNG_BALL_SIZE/2) - pngP1Y) / (float)PNG_PAD_H;
      pngBallDY = (hitPos - 0.5f) * 4.0f;
      newX = PNG_P1_X + PNG_PAD_W + 1;
      // Speed up slightly
      if (pngBallDX < 3.5f) pngBallDX += 0.1f;
      vTone(BUZZ_PIN, 800, 20);
    }
  }
  
  // AI paddle collision (right)
  if (newX + PNG_BALL_SIZE >= PNG_AI_X && pngBallDX > 0) {
    if (newY + PNG_BALL_SIZE >= pngAIY && newY <= pngAIY + PNG_PAD_H) {
      pngBallDX = -pngBallDX;
      float hitPos = ((newY + PNG_BALL_SIZE/2) - pngAIY) / (float)PNG_PAD_H;
      pngBallDY = (hitPos - 0.5f) * 4.0f;
      newX = PNG_AI_X - PNG_BALL_SIZE - 1;
      if (pngBallDX > -3.5f) pngBallDX -= 0.1f;
      vTone(BUZZ_PIN, 600, 20);
    }
  }
  
  // Score — ball passed left edge (AI scores)
  if (newX < -PNG_BALL_SIZE) {
    pngScoreAI++;
    pngDrawScores();
    if (pngScoreAI >= pngWinScore) { pngGameOver = true; vTone(BUZZ_PIN, 200, 500); return; }
    vTone(BUZZ_PIN, 300, 200);
    pngResetBall(1);  // Ball goes toward AI
    pngDrawBall(ST77XX_WHITE);
    pngBallActive = true;
    return;
  }
  
  // Score — ball passed right edge (Player scores)
  if (newX > PNG_AREA_W) {
    pngScoreP1++;
    pngDrawScores();
    if (pngScoreP1 >= pngWinScore) { pngGameOver = true; vTone(BUZZ_PIN, 1500, 300); return; }
    vTone(BUZZ_PIN, 1000, 100);
    pngResetBall(-1);  // Ball goes toward player
    pngDrawBall(ST77XX_WHITE);
    pngBallActive = true;
    return;
  }
  
  pngBallX = newX;
  pngBallY = newY;
  pngDrawBall(ST77XX_WHITE);
  
  // Redraw center line where ball erased it
  int centerX = PNG_AREA_W / 2;
  if (abs((int)pngBallX - centerX) < PNG_BALL_SIZE + 2) pngDrawCenterLine();
}

// ============================================================
// 2048 — Landscape mode (160x128)
// ============================================================
// Layout: Left 120px = 4x4 grid, Right 40px = score/direction panel
#define G48_GRID 4
#define G48_CELL 28       // 4*28=112px
#define G48_PAD 2
#define G48_X 2           // Grid start X
#define G48_Y 2           // Grid start Y
#define G48_PANEL_X 120

uint16_t g48Board[G48_GRID][G48_GRID];
int g48Score = 0;
int g48BestScore = 0;
int g48Dir = 0;  // 0=up, 1=right, 2=down, 3=left (encoder selects)
bool g48GameOver = false, g48Inited = false, g48Won = false;
const char* g48DirNames[] = {"Yukari", "Sag", "Asagi", "Sol"};
const char* g48DirArrows[] = {"^", ">", "v", "<"};

uint16_t g48Color(uint16_t val) {
  switch(val) {
    case 2:    return 0xEF3D;  // light beige
    case 4:    return 0xEF1A;  // beige
    case 8:    return 0xFC60;  // orange
    case 16:   return 0xFB40;  // deep orange
    case 32:   return 0xFA24;  // red-orange
    case 64:   return 0xF800;  // red
    case 128:  return 0xEF00;  // yellow
    case 256:  return 0xEEA0;  // gold
    case 512:  return 0xEE40;  // deep gold
    case 1024: return 0xEE00;  // amber
    case 2048: return 0x07E0;  // green!
    default:   return 0x4208;  // empty dark
  }
}

void g48DrawCell(int col, int row) {
  int x = G48_X + col * G48_CELL;
  int y = G48_Y + row * G48_CELL;
  uint16_t val = g48Board[row][col];
  uint16_t bg = g48Color(val);
  
  tft.fillRect(x + G48_PAD, y + G48_PAD, G48_CELL - 2*G48_PAD, G48_CELL - 2*G48_PAD, bg);
  
  if (val > 0) {
    tft.setTextColor(val >= 8 ? ST77XX_WHITE : ST77XX_BLACK, bg);
    char buf[6]; sprintf(buf, "%d", val);
    int len = strlen(buf);
    // Size and position based on digits
    if (val < 100) {
      tft.setTextSize(2);
      tft.setCursor(x + (G48_CELL - len*12)/2, y + 8);
    } else if (val < 1000) {
      tft.setTextSize(1);
      tft.setCursor(x + (G48_CELL - len*6)/2, y + 10);
    } else {
      tft.setTextSize(1);
      tft.setCursor(x + 2, y + 10);
    }
    tft.print(buf);
  }
}

void g48DrawBoard() {
  for (int r = 0; r < G48_GRID; r++)
    for (int c = 0; c < G48_GRID; c++)
      g48DrawCell(c, r);
}

void g48DrawPanel() {
  tft.fillRect(G48_PANEL_X, 0, 40, 128, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(G48_PANEL_X, 2); tft.print("2048");
  tft.drawFastHLine(G48_PANEL_X, 13, 38, CYBER_DARK);
  
  // Score
  tft.setTextColor(CYBER_DARK);
  tft.setCursor(G48_PANEL_X, 18); tft.print("Skor");
  tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(G48_PANEL_X, 30); tft.printf("%d", g48Score);
  
  // Best
  tft.setTextColor(CYBER_DARK);
  tft.setCursor(G48_PANEL_X, 44); tft.print("En");
  tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(G48_PANEL_X, 56); tft.printf("%d", g48BestScore);
  
  tft.drawFastHLine(G48_PANEL_X, 68, 38, CYBER_DARK);
  
  // Direction indicator
  g48DrawDir();
  
  // Controls
  tft.setTextColor(CYBER_DARK);
  tft.setCursor(G48_PANEL_X, 100); tft.print("Cvr:");
  tft.setCursor(G48_PANEL_X, 110); tft.print(" yon");
  tft.setCursor(G48_PANEL_X, 122); tft.print("K0:cik");
}

void g48DrawDir() {
  tft.fillRect(G48_PANEL_X, 72, 38, 24, CYBER_BG);
  tft.setTextSize(2); tft.setTextColor(CYBER_ACCENT, CYBER_BG);
  tft.setCursor(G48_PANEL_X + 8, 73); tft.print(g48DirArrows[g48Dir]);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(G48_PANEL_X, 91); tft.print("Bas:git");
}

void g48DrawScore() {
  tft.fillRect(G48_PANEL_X, 28, 38, 12, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(G48_PANEL_X, 30); tft.printf("%d", g48Score);
}

void g48SpawnTile() {
  // Find empty cells
  int empty[16]; int cnt = 0;
  for (int r = 0; r < G48_GRID; r++)
    for (int c = 0; c < G48_GRID; c++)
      if (g48Board[r][c] == 0) empty[cnt++] = r * G48_GRID + c;
  if (cnt == 0) return;
  int pick = empty[random(cnt)];
  int r = pick / G48_GRID, c = pick % G48_GRID;
  g48Board[r][c] = (random(10) < 9) ? 2 : 4;
  g48DrawCell(c, r);
}

bool g48Slide(int dir) {
  // Returns true if anything moved
  bool moved = false;
  uint16_t prev[G48_GRID][G48_GRID];
  memcpy(prev, g48Board, sizeof(g48Board));
  
  // Process based on direction
  for (int pass = 0; pass < G48_GRID; pass++) {
    for (int i = 0; i < G48_GRID; i++) {
      // Build line based on direction
      uint16_t line[G48_GRID];
      for (int j = 0; j < G48_GRID; j++) {
        switch(dir) {
          case 0: line[j] = g48Board[j][i]; break;        // up: column top to bottom
          case 1: line[j] = g48Board[i][G48_GRID-1-j]; break; // right: row right to left
          case 2: line[j] = g48Board[G48_GRID-1-j][i]; break; // down: column bottom to top
          case 3: line[j] = g48Board[i][j]; break;        // left: row left to right
        }
      }
      
      if (pass == 0) {
        // First pass: compact (remove zeros)
        uint16_t compact[G48_GRID] = {0};
        int ci = 0;
        for (int j = 0; j < G48_GRID; j++) if (line[j] > 0) compact[ci++] = line[j];
        memcpy(line, compact, sizeof(line));
      } else if (pass == 1) {
        // Second pass: merge adjacent equal
        for (int j = 0; j < G48_GRID - 1; j++) {
          if (line[j] > 0 && line[j] == line[j+1]) {
            line[j] *= 2;
            g48Score += line[j];
            if (line[j] == 2048) g48Won = true;
            line[j+1] = 0;
          }
        }
      } else if (pass == 2) {
        // Third pass: compact again
        uint16_t compact[G48_GRID] = {0};
        int ci = 0;
        for (int j = 0; j < G48_GRID; j++) if (line[j] > 0) compact[ci++] = line[j];
        memcpy(line, compact, sizeof(line));
      }
      
      // Write back
      for (int j = 0; j < G48_GRID; j++) {
        switch(dir) {
          case 0: g48Board[j][i] = line[j]; break;
          case 1: g48Board[i][G48_GRID-1-j] = line[j]; break;
          case 2: g48Board[G48_GRID-1-j][i] = line[j]; break;
          case 3: g48Board[i][j] = line[j]; break;
        }
      }
    }
  }
  
  // Check if anything changed
  for (int r = 0; r < G48_GRID; r++)
    for (int c = 0; c < G48_GRID; c++)
      if (g48Board[r][c] != prev[r][c]) moved = true;
  
  return moved;
}

bool g48CanMove() {
  for (int r = 0; r < G48_GRID; r++)
    for (int c = 0; c < G48_GRID; c++) {
      if (g48Board[r][c] == 0) return true;
      if (c < G48_GRID-1 && g48Board[r][c] == g48Board[r][c+1]) return true;
      if (r < G48_GRID-1 && g48Board[r][c] == g48Board[r+1][c]) return true;
    }
  return false;
}

void init2048() {
  g48Inited = true;
  g48GameOver = false;
  g48Won = false;
  g48Score = 0;
  g48Dir = 0;
  memset(g48Board, 0, sizeof(g48Board));
  
  tft.setRotation(1);
  tft.fillScreen(CYBER_BG);
  
  // Spawn 2 initial tiles
  g48SpawnTile();
  g48SpawnTile();
  
  g48DrawBoard();
  g48DrawPanel();
}

void update2048(int enc, bool encP, bool back) {
  if (back) { g48Inited=false; currentMode=MODE_GAMES_MENU; drawGamesMenu(); return; }
  
  if (g48GameOver) {
    tft.setTextSize(2); tft.setTextColor(ST77XX_RED, CYBER_BG);
    tft.setCursor(15, 45); tft.print("BITTI!");
    tft.setTextSize(1); tft.setTextColor(CYBER_GREEN, CYBER_BG);
    tft.setCursor(15, 70); tft.printf("Skor: %d", g48Score);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(10, 90); tft.print("Bas: tekrar");
    if (encP) init2048();
    return;
  }
  
  if (g48Won) {
    tft.setTextSize(2); tft.setTextColor(CYBER_GREEN, CYBER_BG);
    tft.setCursor(5, 45); tft.print("2048!!!");
    tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
    tft.setCursor(10, 70); tft.printf("Skor: %d", g48Score);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(10, 90); tft.print("Bas: devam");
    if (encP) { g48Won = false; g48DrawBoard(); g48DrawPanel(); }
    return;
  }
  
  // Encoder: select direction
  if (enc != 0) {
    g48Dir = (g48Dir + enc + 4) % 4;
    g48DrawDir();
  }
  
  // Encoder press: execute move
  if (encP) {
    bool moved = g48Slide(g48Dir);
    if (moved) {
      g48SpawnTile();
      g48DrawBoard();
      g48DrawScore();
      vTone(BUZZ_PIN, 600, 30);
      
      if (g48Score > g48BestScore) {
        g48BestScore = g48Score;
        tft.fillRect(G48_PANEL_X, 54, 38, 12, CYBER_BG);
        tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
        tft.setCursor(G48_PANEL_X, 56); tft.printf("%d", g48BestScore);
      }
      
      if (!g48CanMove()) {
        g48GameOver = true;
        vTone(BUZZ_PIN, 200, 500);
      }
    } else {
      vTone(BUZZ_PIN, 300, 50);  // Can't move in that direction
    }
  }
}

// ============================================================
// SMART HOME — HA Control via MQTT
// ============================================================
// Forward declaration
void drawMenu();
// HA publishes room/device JSON to cyberclock/home/config
// Clock subscribes and builds menu from it
// User actions publish to homeassistant/<domain>/<entity>/set
//
// JSON format from HA:
// [{"r":"Salon","d":[{"n":"Tavan Isik","t":"light","e":"light.salon_tavan","s":"off"},
//                    {"n":"Priz","t":"switch","e":"switch.salon_priz","s":"off"}]},
//  {"r":"Yatak","d":[{"n":"Klima","t":"climate","e":"climate.yatak_klima","s":"off"}]}]

#define HOME_MAX_ROOMS 8
#define HOME_MAX_DEVICES 6   // Per room
#define HOME_MAX_NAME 16
#define HOME_MAX_ENTITY 64

#define TOPIC_HOME_CONFIG  TOPIC_PREFIX "/home/config"
#define TOPIC_HOME_REQUEST TOPIC_PREFIX "/home/request"
#define TOPIC_HOME_STATE   TOPIC_PREFIX "/home/state"

struct HomeDevice {
  char name[HOME_MAX_NAME];
  char type[10];        // "light", "switch", "climate"
  char entity[HOME_MAX_ENTITY];  // HA entity_id
  char state[8];        // "on", "off", "25" (brightness)
  uint8_t brightness;   // For lights: 0-255
};

struct HomeRoom {
  char name[HOME_MAX_NAME];
  HomeDevice devices[HOME_MAX_DEVICES];
  int deviceCount;
};

HomeRoom homeRooms[HOME_MAX_ROOMS];
int homeRoomCount = 0;
int homeMenuLevel = 0;   // 0=room list, 1=device list, 2=device control
int homeRoomIdx = 0;
int homeDevIdx = 0;
bool homeLoaded = false;
bool homeInited = false;

void homeParseConfig(const char* json) {
  // Parse JSON device config from HA
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) { Serial.printf("Home JSON error: %s\n", err.c_str()); return; }
  
  homeRoomCount = 0;
  JsonArray rooms = doc.as<JsonArray>();
  for (JsonObject room : rooms) {
    if (homeRoomCount >= HOME_MAX_ROOMS) break;
    HomeRoom &hr = homeRooms[homeRoomCount];
    strlcpy(hr.name, room["r"] | "?", HOME_MAX_NAME);
    hr.deviceCount = 0;
    JsonArray devs = room["d"];
    for (JsonObject dev : devs) {
      if (hr.deviceCount >= HOME_MAX_DEVICES) break;
      HomeDevice &hd = hr.devices[hr.deviceCount];
      strlcpy(hd.name, dev["n"] | "?", HOME_MAX_NAME);
      strlcpy(hd.type, dev["t"] | "?", 10);
      strlcpy(hd.entity, dev["e"] | "", HOME_MAX_ENTITY);
      strlcpy(hd.state, dev["s"] | "off", 8);
      hd.brightness = dev["b"] | 0;
      hr.deviceCount++;
    }
    homeRoomCount++;
  }
  homeLoaded = true;
  Serial.printf("Home config: %d rooms loaded\n", homeRoomCount);
}

#define TOPIC_HOME_CMD TOPIC_PREFIX "/home/command"

// Dimmer speed tracking
unsigned long lastDimmerEncMs = 0;

int calcDimmerDelta(bool up) {
  unsigned long now = millis();
  unsigned long elapsed = now - lastDimmerEncMs;
  lastDimmerEncMs = now;

  int pct;
  if (elapsed < 50)       pct = 25;  // very fast rotation
  else if (elapsed < 120) pct = 10;  // fast
  else if (elapsed < 250) pct = 5;   // medium
  else                    pct = 1;   // slow

  int delta = (255 * pct) / 100;
  if (delta < 1) delta = 1;
  return up ? delta : -delta;
}

void homePublishCommand(int roomIdx, int devIdx, const char* cmd, int brightDelta = 0) {
  HomeDevice &dev = homeRooms[roomIdx].devices[devIdx];
  if (!mqttClient.connected()) return;

  // Send command to HA via single topic — HA automation handles service calls
  // Format: {"e":"light.salon","c":"toggle"} or {"e":"light.salon","c":"bright_up","b":180}
  JsonDocument doc;
  doc["e"] = dev.entity;
  doc["t"] = dev.type;

  if (strcmp(cmd, "toggle") == 0) {
    doc["c"] = "toggle";
    // Don't optimistically update — wait for HA state callback
  }
  else if (strcmp(cmd, "bright") == 0) {
    dev.brightness = constrain((int)dev.brightness + brightDelta, 0, 255);
    doc["c"] = (brightDelta > 0) ? "bright_up" : "bright_down";
    doc["b"] = dev.brightness;
    strlcpy(dev.state, "on", 8);
  }

  char buf[128]; serializeJson(doc, buf);
  mqttClient.publish(TOPIC_HOME_CMD, buf);
}

void homeRequestConfig() {
  // Ask HA to send the config (HA automation listens for this)
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_HOME_REQUEST, "refresh", true);
  }
}

void drawHomeScreen() {
  tft.fillScreen(CYBER_BG);
  tft.setTextSize(1);
  
  if (!homeLoaded || homeRoomCount == 0) {
    tft.setTextColor(CYBER_LIGHT); tft.setCursor(8, 4); tft.print("EV");
    tft.setTextColor(CYBER_DARK); tft.setCursor(40, 4); tft.print("K0:geri");
    tft.setTextColor(ST77XX_WHITE, CYBER_BG);
    tft.setCursor(10, 40); tft.print("Cihaz listesi bekleniyor...");
    tft.setCursor(10, 55); tft.print("HA otomasyonu gerekli.");
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(10, 75); tft.print("Bas: yenile");
    return;
  }
  
  if (homeMenuLevel == 0) {
    // Room list
    tft.setTextColor(CYBER_LIGHT); tft.setCursor(8, 4); tft.print("ODALAR");
    tft.setTextColor(CYBER_DARK); tft.setCursor(80, 4); tft.print("K0:geri");
    drawAlarmIcon();
    for (int i = 0; i < homeRoomCount; i++) {
      int y = 20 + i * 14;
      if (i == homeRoomIdx) { tft.fillRect(4, y-1, 152, 13, CYBER_ACCENT); tft.setTextColor(CYBER_BG); }
      else { tft.fillRect(4, y-1, 152, 13, CYBER_BG); tft.setTextColor(ST77XX_WHITE); }
      tft.setCursor(10, y); tft.print(homeRooms[i].name);
      // Device count
      char cnt[6]; sprintf(cnt, "(%d)", homeRooms[i].deviceCount);
      tft.setCursor(130, y); tft.print(cnt);
    }
  }
  else if (homeMenuLevel == 1) {
    // Device list for selected room
    HomeRoom &room = homeRooms[homeRoomIdx];
    tft.setTextColor(CYBER_LIGHT); tft.setCursor(8, 4); tft.print(room.name);
    tft.setTextColor(CYBER_DARK); tft.setCursor(100, 4); tft.print("K0:geri");
    drawAlarmIcon();
    for (int i = 0; i < room.deviceCount; i++) {
      int y = 20 + i * 16;
      HomeDevice &dev = room.devices[i];
      if (i == homeDevIdx) { tft.fillRect(4, y-1, 152, 14, CYBER_ACCENT); tft.setTextColor(CYBER_BG); }
      else { tft.fillRect(4, y-1, 152, 14, CYBER_BG); tft.setTextColor(ST77XX_WHITE); }
      tft.setCursor(10, y); tft.print(dev.name);
      // State indicator
      uint16_t sc = (strcmp(dev.state, "on") == 0) ? CYBER_GREEN : ST77XX_RED;
      if (i == homeDevIdx) sc = CYBER_BG;
      tft.setTextColor(sc);
      tft.setCursor(130, y);
      if (strcmp(dev.state, "on") == 0) tft.print("ON");
      else tft.print("OFF");
    }
    tft.setTextColor(CYBER_DARK); tft.setCursor(4, 116);
    tft.print("Bas:ac/kapa Cvr:sec");
  }
  else if (homeMenuLevel == 2) {
    // Device control (for lights with brightness)
    HomeRoom &room = homeRooms[homeRoomIdx];
    HomeDevice &dev = room.devices[homeDevIdx];
    tft.setTextColor(CYBER_LIGHT); tft.setCursor(8, 4); tft.print(dev.name);
    tft.setTextColor(CYBER_DARK); tft.setCursor(100, 4); tft.print("K0:geri");
    
    // On/Off status
    tft.setTextSize(2);
    if (strcmp(dev.state, "on") == 0) { tft.setTextColor(CYBER_GREEN, CYBER_BG); tft.setCursor(30, 30); tft.print("ACIK"); }
    else { tft.setTextColor(ST77XX_RED, CYBER_BG); tft.setCursor(30, 30); tft.print("KAPALI"); }
    
    // Brightness bar (for lights)
    if (strcmp(dev.type, "light") == 0) {
      tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
      tft.setCursor(8, 60); tft.print("Parlaklik:");
      int barW = map(dev.brightness, 0, 255, 0, 130);
      tft.fillRect(8, 72, 140, 10, CYBER_DARK);
      tft.fillRect(8, 72, barW, 10, CYBER_LIGHT);
      char pct[8]; sprintf(pct, "%d%%", map(dev.brightness, 0, 255, 0, 100));
      tft.setTextColor(ST77XX_WHITE, CYBER_BG);
      tft.setCursor(60, 88); tft.print(pct);
    }
    
    tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
    tft.setCursor(4, 108); tft.print("Bas:ac/kapa");
    if (strcmp(dev.type, "light") == 0) {
      tft.setCursor(4, 118); tft.print("Cvr:parlaklik K0:geri");
    }
  }
}

void initHome() {
  homeInited = true;
  homeMenuLevel = 0;
  homeRoomIdx = 0;
  homeDevIdx = 0;
  homeRequestConfig();  // Ask HA to send device list
  drawHomeScreen();
}

void updateHome(int enc, bool encP, bool back) {
  if (back) {
    if (homeMenuLevel == 0) { homeInited=false; currentMode=MODE_MENU; drawMenu(); return; }
    else if (homeMenuLevel == 1) { homeMenuLevel = 0; homeDevIdx = 0; drawHomeScreen(); return; }
    else { homeMenuLevel = 1; drawHomeScreen(); return; }
  }
  
  if (!homeLoaded || homeRoomCount == 0) {
    // Waiting for config — press to refresh
    if (encP) { homeRequestConfig(); vTone(BUZZ_PIN, 800, 50); }
    return;
  }
  
  if (homeMenuLevel == 0) {
    // Room selection
    if (enc != 0) {
      homeRoomIdx += enc;
      if (homeRoomIdx < 0) homeRoomIdx = homeRoomCount - 1;
      if (homeRoomIdx >= homeRoomCount) homeRoomIdx = 0;
      drawHomeScreen();
    }
    if (encP) {
      homeMenuLevel = 1;
      homeDevIdx = 0;
      drawHomeScreen();
    }
  }
  else if (homeMenuLevel == 1) {
    // Device selection
    HomeRoom &room = homeRooms[homeRoomIdx];
    if (enc != 0) {
      homeDevIdx += enc;
      if (homeDevIdx < 0) homeDevIdx = room.deviceCount - 1;
      if (homeDevIdx >= room.deviceCount) homeDevIdx = 0;
      drawHomeScreen();
    }
    if (encP) {
      if (strcmp(homeRooms[homeRoomIdx].devices[homeDevIdx].type, "light") == 0) {
        homeMenuLevel = 2;
        homePublishCommand(homeRoomIdx, homeDevIdx, "toggle");
        drawHomeScreen();
      } else {
        homePublishCommand(homeRoomIdx, homeDevIdx, "toggle");
        vTone(BUZZ_PIN, 1000, 50);
        drawHomeScreen();
      }
    }
  }
  else if (homeMenuLevel == 2) {
    // Light brightness control — speed-based delta
    if (enc != 0) {
      int delta = calcDimmerDelta(enc > 0);
      homePublishCommand(homeRoomIdx, homeDevIdx, "bright", delta);
      drawHomeScreen();
    }
    if (encP) {
      homePublishCommand(homeRoomIdx, homeDevIdx, "toggle");
      drawHomeScreen();
      vTone(BUZZ_PIN, 1000, 50);
    }
  }
}

// ============================================================
// HELPER FUNCTIONS
// ============================================================
int readEncoderStep() {
  int encA = digitalRead(ENC_A_PIN), encB = digitalRead(ENC_B_PIN);
  int step = 0;
  if (encA != lastEncA) { if (encA == LOW) step = (encB == HIGH) ? +1 : -1; }
  lastEncA = encA; lastEncB = encB;
  return step;
}

bool checkButtonPressed(uint8_t pin, bool &lastState) {
  bool cur = digitalRead(pin);
  bool pressed = false;
  unsigned long now = millis();
  if (cur == LOW && lastState == HIGH && (now - lastBtnMs) > 150) { pressed = true; lastBtnMs = now; }
  lastState = cur;
  return pressed;
}

void drawAlarmIcon() {
  int x = 148;
  tft.fillRect(x-10, 0, 12, 12, CYBER_BG);
  if (!alarmEnabled) return;
  uint16_t c = CYBER_LIGHT;
  tft.drawRoundRect(x-9, 2, 10, 7, 2, c);
  tft.drawFastHLine(x-8, 8, 8, c);
  tft.fillCircle(x-4, 10, 1, c);
}

// ============================================================
// MELODY PLAYER
// ============================================================
void startMelody(const int* notes, const int* durs, int len, bool loop) {
  melodyNotes=notes; melodyDurs=durs; melodyLen=len; melodyIdx=0;
  melodyPlaying=true; melodyLoop=loop; melodyLastMs=millis();
  if (notes[0]>0) vTone(BUZZ_PIN, notes[0], durs[0]);
}

void stopMelody() { melodyPlaying=false; vToneStop(); }

void updateMelody() {
  if (!melodyPlaying) return;
  if (millis()-melodyLastMs >= (unsigned long)melodyDurs[melodyIdx]) {
    melodyIdx++;
    if (melodyIdx>=melodyLen) { if(melodyLoop) melodyIdx=0; else { stopMelody(); return; } }
    melodyLastMs=millis();
    if (melodyNotes[melodyIdx]>0) vTone(BUZZ_PIN, melodyNotes[melodyIdx], melodyDurs[melodyIdx]);
    else vToneStop();
  }
}

// ============================================================
// EDISON LED
// ============================================================
void updateLed() {
  if (alertLedActive) return;
  unsigned long now = millis();
  switch (ledMode) {
    case LED_OFF: analogWrite(LED_PIN, 0); break;
    case LED_ON: analogWrite(LED_PIN, ledBrightness); break;
    case LED_DIM: analogWrite(LED_PIN, ledBrightness); break;
    case LED_BREATHE:
      if (now-lastLedUpdate>20) { lastLedUpdate=now; breatheVal+=breatheDir*3;
        if(breatheVal>=ledBrightness){breatheVal=ledBrightness;breatheDir=-1;} if(breatheVal<=5){breatheVal=5;breatheDir=1;}
        analogWrite(LED_PIN, breatheVal); } break;
    case LED_BLINK:
      if (now-lastLedUpdate>500) { lastLedUpdate=now; static bool bs=false; bs=!bs; analogWrite(LED_PIN, bs?ledBrightness:0); } break;
    case LED_SOS: {
      const int sp[]={1,0,1,0,1,0,0,0,2,0,2,0,2,0,0,0,1,0,1,0,1,0,0,0};
      const int st[]={100,100,100,100,100,300,100,100,300,100,300,100,300,300,100,100,100,100,100,100,100,700,100,100};
      if(now-sosLastMs>(unsigned long)st[sosStep]){sosLastMs=now; analogWrite(LED_PIN,sp[sosStep]?ledBrightness:0); sosStep=(sosStep+1)%24;}
    } break;
  }
}

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void drawMenu();
void drawGamesMenu();
void drawAlarmScreen(bool full);
void drawPomodoroScreen(bool full);
void drawLedScreen(bool full);
void drawSettingsScreen(bool full);
void initClockStaticUI();
void drawClockTime(String hS, String mS, String sS);
void drawEnvDynamic(float t, float h, uint16_t tv, uint16_t e);
void drawAlarmRingingScreen();
void updateEnvSensors(bool force=false);
String getTimeStr(char type);

// ============================================================
// SLEEP MODE
// ============================================================
void enterSleep() {
  if (screenSleeping) return;
  screenSleeping = true;
  preSleepMode = currentMode;
  currentMode = MODE_SLEEP;
  
  // Tetris now uses landscape — no rotation restore needed
  
  // Simply black out the screen
  tft.fillScreen(ST77XX_BLACK);
  
  // Publish sleep state to HA
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_SLEEP_STATE, "ON", true);
  }
  Serial.println("Sleep mode ON");
}

void wakeUp() {
  if (!screenSleeping) return;
  screenSleeping = false;
  lastActivityMs = millis();
  
  // No special wake command needed — just redraw
  
  // Restore previous mode
  currentMode = preSleepMode;
  
  // Redraw current screen
  switch (currentMode) {
    case MODE_CLOCK:
      initClockStaticUI(); prevTimeStr = "";
      drawClockTime(getTimeStr('H'), getTimeStr('M'), getTimeStr('S'));
      drawEnvDynamic(curTemp, curHum, curTVOC, curECO2);
      break;
    case MODE_MENU: drawMenu(); break;
    case MODE_GAMES_MENU: drawGamesMenu(); break;
    case MODE_POMODORO: drawPomodoroScreen(true); break;
    case MODE_ALARM: drawAlarmScreen(true); break;
    case MODE_LED: drawLedScreen(true); break;
    case MODE_SETTINGS: drawSettingsScreen(true); break;
    default: 
      currentMode = MODE_CLOCK;
      initClockStaticUI(); prevTimeStr = "";
      drawClockTime(getTimeStr('H'), getTimeStr('M'), getTimeStr('S'));
      drawEnvDynamic(curTemp, curHum, curTVOC, curECO2);
      break;
  }
  
  // Publish wake state to HA
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_SLEEP_STATE, "OFF", true);
  }
  Serial.println("Sleep mode OFF");
}

void resetActivityTimer() {
  lastActivityMs = millis();
}

// Publish LED ON/OFF state + dimmer brightness + mode to HA
void publishLedState() {
  if (!mqttClient.connected()) return;
  // ON/OFF: anything other than LED_OFF is "ON"
  mqttClient.publish(TOPIC_LED_STATE, ledMode==LED_OFF ? "OFF" : "ON", true);
  // Dimmer brightness
  char buf[8]; sprintf(buf, "%d", ledBrightness);
  mqttClient.publish(TOPIC_LED_DIM_ST, buf, true);
  // Mode name (no "dim" option in HA — dim is handled by slider)
  const char* modeHA[] = {"off","on","on","breathe","blink","sos"};  // LED_DIM maps to "on"
  mqttClient.publish(TOPIC_LED_MODE_ST, modeHA[(int)ledMode], true);
}

void drawLedScreen(bool full) {
  if (full) { tft.fillScreen(CYBER_BG); tft.setTextSize(1); tft.setCursor(8,4);
    tft.setTextColor(CYBER_LIGHT); tft.print("EDISON LED"); drawAlarmIcon(); }
  for (int i=0; i<LED_MODE_COUNT; i++) {
    int y=20+i*14;
    if(i==ledMenuSelection){tft.fillRect(4,y-1,152,12,CYBER_ACCENT);tft.setTextColor(CYBER_BG,CYBER_ACCENT);}
    else{tft.fillRect(4,y-1,152,12,CYBER_BG);tft.setTextColor((int)ledMode==i?CYBER_GREEN:ST77XX_WHITE,CYBER_BG);}
    tft.setCursor(10,y); tft.print(ledModeNames[i]);
    if(i==2){char b[8];sprintf(b," %d%%",map(ledBrightness,0,255,0,100));tft.print(b);}
    if((int)ledMode==i&&i!=ledMenuSelection){tft.setCursor(140,y);tft.setTextColor(CYBER_GREEN,CYBER_BG);tft.print("<");}
  }
  tft.fillRect(0,108,160,20,CYBER_BG); tft.setCursor(4,110); tft.setTextSize(1);
  tft.setTextColor(CYBER_DARK); tft.print("Cevir:sec Bas:aktif K0:geri");
}

// ============================================================
// SETTINGS SCREEN (Volume + Calibration + Auto Sleep)
// ============================================================
int settingsField = 0;
const int SETTINGS_COUNT = 5;

// Auto-sleep timeout options (milliseconds, 0=disabled)
const unsigned long sleepOptions[] = {0, 30000, 60000, 120000, 300000};
const char* sleepLabels[] = {"Kapali", "30sn", "1dk", "2dk", "5dk"};
const int SLEEP_OPTION_COUNT = 5;
int sleepOptionIndex = 2;  // Default: 1dk (60000ms)

int getSleepOptionIndex() {
  for (int i = 0; i < SLEEP_OPTION_COUNT; i++) {
    if (sleepOptions[i] == sleepTimeoutMs) return i;
  }
  return 0;
}

void drawSettingsScreen(bool full) {
  if (full) {
    tft.fillScreen(CYBER_BG); tft.setTextSize(1); tft.setCursor(8,4);
    tft.setTextColor(CYBER_LIGHT); tft.print("AYARLAR"); drawAlarmIcon();
    sleepOptionIndex = getSleepOptionIndex();
  }

  const char* labels[] = {"Ses:", "Alarm:", "Sicaklik:", "Nem:", "Oto Uyku:"};
  char values[5][16];
  sprintf(values[0], "%d%%", volumeLevel);
  sprintf(values[1], "%s", rtttlNames[selectedAlarm]);
  if (tempOffset >= 0) sprintf(values[2], "+%.1fC", tempOffset);
  else sprintf(values[2], "%.1fC", tempOffset);
  if (humOffset >= 0) sprintf(values[3], "+%.1f%%", humOffset);
  else sprintf(values[3], "%.1f%%", humOffset);
  sprintf(values[4], "%s", sleepLabels[sleepOptionIndex]);

  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = 22 + i * 20;
    tft.fillRect(4, y-2, 152, 18, CYBER_BG);
    tft.setTextSize(1);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(8, y); tft.print(labels[i]);
    tft.setTextSize(1);
    if (i == settingsField) {
      tft.setTextColor(CYBER_LIGHT, CYBER_BG);
    } else {
      tft.setTextColor(ST77XX_WHITE, CYBER_BG);
    }
    tft.setCursor(8, y + 9); tft.print(values[i]);
    if (i == settingsField) {
      tft.setTextColor(CYBER_ACCENT);
      tft.setCursor(145, y + 9); tft.print("<");
    }
  }

  tft.fillRect(0, 124, 160, 4, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
  tft.setCursor(4, 119);
  tft.printf("Okunan: %.1fC %.1f%%", curTemp, curHum);
}

// ============================================================
// WiFi
// ============================================================
void connectWiFiAndSyncTime() {
  tft.fillScreen(CYBER_BG); tft.setTextColor(CYBER_LIGHT); tft.setTextSize(1);
  tft.setCursor(10,40); tft.print("Connecting WiFi...");
  WiFi.disconnect(true); delay(500); WiFi.mode(WIFI_STA); delay(100);
  WiFi.begin(ssid, password);
  uint8_t retry=0;
  while(WiFi.status()!=WL_CONNECTED&&retry<40){delay(500);tft.print(".");retry++;}
  if(WiFi.status()==WL_CONNECTED){
    tft.fillScreen(CYBER_BG); tft.setCursor(10,40); tft.setTextColor(CYBER_GREEN);
    tft.print("WiFi OK: "); tft.print(WiFi.localIP());
    configTime(gmtOffset_sec,daylightOffset_sec,ntpServer);
    tft.setCursor(10,55); tft.print("Time synced (GMT+3)"); delay(500);
    mqttClient.setServer(mqtt_server,mqtt_port); mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(2048);  // Larger buffer for home config JSON
    tft.setCursor(10,70); tft.print("MQTT connecting...");
    connectMqtt();
    tft.setCursor(10,85); if(mqttClient.connected()){tft.setTextColor(CYBER_GREEN);tft.print("MQTT OK!");}
    else{tft.setTextColor(AQ_BAR_ORANGE);tft.print("MQTT retry later...");}
    delay(800);
  } else {
    tft.fillScreen(CYBER_BG); tft.setCursor(10,55); tft.setTextColor(ST77XX_RED);
    tft.print("WiFi FAILED!"); delay(1500);
  }
}

String getTimeStr(char type) {
  struct tm ti; if(!getLocalTime(&ti)) return "--";
  char buf[8];
  if(type=='H')strftime(buf,sizeof(buf),"%H",&ti);
  else if(type=='M')strftime(buf,sizeof(buf),"%M",&ti);
  else if(type=='S')strftime(buf,sizeof(buf),"%S",&ti);
  return String(buf);
}

// ============================================================
// MQTT
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg=""; for(unsigned int i=0;i<length;i++) msg+=(char)payload[i]; msg.trim();
  String tp = String(topic);
  
  // Alarm ON/OFF from HA
  if(tp==TOPIC_ALARM_SET){
    if(msg=="ON") alarmEnabled=true; else if(msg=="OFF"){alarmEnabled=false; if(alarmRinging){alarmRinging=false;stopMelody();}}
    mqttClient.publish(TOPIC_ALARM_STATE,alarmEnabled?"ON":"OFF",true); drawAlarmIcon();
  }
  
  // Buzzer from HA
  if(tp==TOPIC_BUZZER_SET) {
    playAlarmByIndex(selectedAlarm,false);
  }
  
  // ===== Edison LED ON/OFF from HA =====
  if(tp==TOPIC_LED_SET){
    if(msg=="ON"){
      // Turn on: if currently off, switch to ON mode
      if(ledMode==LED_OFF){ ledMode=LED_ON; analogWrite(LED_PIN,ledBrightness); }
    } else if(msg=="OFF"){
      ledMode=LED_OFF; analogWrite(LED_PIN,0);
    }
    publishLedState();
    if(currentMode == MODE_LED) drawLedScreen(false);
  }
  
  // ===== Edison LED Dimmer from HA =====
  // Only changes brightness, does NOT change LED mode
  if(tp==TOPIC_LED_DIM_SET){
    int v = msg.toInt();
    if(v >= 0 && v <= 255){
      ledBrightness = v;
      // Apply immediately to hardware if LED is in a mode that uses brightness
      if(ledMode == LED_ON || ledMode == LED_DIM) {
        analogWrite(LED_PIN, ledBrightness);
      }
      // For breathe/blink/sos the new brightness will be picked up automatically
      // in the next updateLed() cycle since they all reference ledBrightness
      publishLedState();
      // Update LED screen if we're on it
      if(currentMode == MODE_LED) drawLedScreen(false);
    }
  }
  
  // ===== Edison LED Mode from HA =====
  // Options: off, on, breathe, blink, sos (no "dim" — slider handles that)
  if(tp==TOPIC_LED_MODE_SET){
    if(msg=="off")      { ledMode=LED_OFF; analogWrite(LED_PIN,0); }
    else if(msg=="on")  { ledMode=LED_ON; analogWrite(LED_PIN,ledBrightness); }
    else if(msg=="breathe") { ledMode=LED_BREATHE; breatheVal=0; breatheDir=1; }
    else if(msg=="blink")   { ledMode=LED_BLINK; lastLedUpdate=millis(); }
    else if(msg=="sos")     { ledMode=LED_SOS; sosStep=0; sosLastMs=millis(); }
    publishLedState();
    if(currentMode == MODE_LED) drawLedScreen(false);
  }
  
  // Volume from HA
  if(tp==TOPIC_VOL_SET){
    int v=msg.toInt(); volumeLevel=constrain(v,0,100);
    char b[8]; sprintf(b,"%d",volumeLevel);
    mqttClient.publish(TOPIC_VOL_STATE,b,true);
    saveSettings();
  }
  
  // Temperature calibration from HA
  if(tp==TOPIC_TCAL_SET){
    tempOffset=constrain(msg.toFloat(),-15.0f,15.0f);
    char b[8]; dtostrf(tempOffset,4,1,b);
    mqttClient.publish(TOPIC_TCAL_STATE,b,true);
    saveSettings();
  }
  
  // Humidity calibration from HA
  if(tp==TOPIC_HCAL_SET){
    humOffset=constrain(msg.toFloat(),-20.0f,20.0f);
    char b[8]; dtostrf(humOffset,4,1,b);
    mqttClient.publish(TOPIC_HCAL_STATE,b,true);
    saveSettings();
  }
  
  // Sleep mode from HA
  if(tp==TOPIC_SLEEP_SET){
    if(msg=="ON") enterSleep();
    else if(msg=="OFF") wakeUp();
  }
  
  // Auto-sleep enable/disable from HA (persistent)
  if(tp==TOPIC_AUTOSLEEP_SET){
    if(msg=="ON"){
      if(sleepTimeoutMs==0){ sleepTimeoutMs=60000; sleepOptionIndex=2; }  // Default 1min
    } else if(msg=="OFF"){
      sleepTimeoutMs=0; sleepOptionIndex=0;
    }
    mqttClient.publish(TOPIC_AUTOSLEEP_STATE, sleepTimeoutMs>0?"ON":"OFF", true);
    resetActivityTimer();
    saveSettings();
    if(currentMode==MODE_SETTINGS) drawSettingsScreen(false);
  }
  
  // Smart Home config from HA
  if(tp==TOPIC_HOME_CONFIG){
    homeParseConfig(msg.c_str());
    if(currentMode==MODE_HOME) drawHomeScreen();
  }
  
  // Smart Home state updates from HA (individual device)
  // Format: cyberclock/home/state with JSON {"e":"light.salon","s":"on","b":128}
  if(tp==TOPIC_HOME_STATE){
    JsonDocument sdoc;
    if(!deserializeJson(sdoc, msg)){
      const char* eid = sdoc["e"] | "";
      const char* st = sdoc["s"] | "";
      int br = sdoc["b"] | -1;
      // Find device and update
      for(int r=0;r<homeRoomCount;r++)
        for(int d=0;d<homeRooms[r].deviceCount;d++){
          if(strcmp(homeRooms[r].devices[d].entity, eid)==0){
            if(strlen(st)>0) strlcpy(homeRooms[r].devices[d].state, st, 8);
            if(br>=0) homeRooms[r].devices[d].brightness = br;
          }
        }
      if(currentMode==MODE_HOME) drawHomeScreen();
    }
  }
}

void publishMqttDiscovery() {
  // Sensors + Alarm + Buzzer
  struct{const char*c;const char*id;const char*n;const char*st;const char*ct;const char*u;const char*dc;}
  ent[]={
    {"sensor","cyberclock18_temp","CC18 Temperature",TOPIC_TEMP,NULL,"°C","temperature"},
    {"sensor","cyberclock18_hum","CC18 Humidity",TOPIC_HUM,NULL,"%","humidity"},
    {"sensor","cyberclock18_tvoc","CC18 TVOC",TOPIC_TVOC,NULL,"ppb",NULL},
    {"sensor","cyberclock18_co2","CC18 CO2",TOPIC_CO2,NULL,"ppm","carbon_dioxide"},
    {"switch","cyberclock18_alarm","CC18 Alarm",TOPIC_ALARM_STATE,TOPIC_ALARM_SET,NULL,NULL},
    {"button","cyberclock18_buzzer","CC18 Buzzer",NULL,TOPIC_BUZZER_SET,NULL,NULL},
  };
  for(int i=0;i<6;i++){
    JsonDocument doc; doc["name"]=ent[i].n; doc["uniq_id"]=ent[i].id;
    doc["dev"]["ids"][0]="cyberclock18_01"; doc["dev"]["name"]="Cyber Clock 18";
    doc["dev"]["mf"]="DIY"; doc["dev"]["mdl"]="Smart Cyber Clock v9";
    if(ent[i].st)doc["stat_t"]=ent[i].st; if(ent[i].ct)doc["cmd_t"]=ent[i].ct;
    if(ent[i].u)doc["unit_of_meas"]=ent[i].u; if(ent[i].dc)doc["dev_cla"]=ent[i].dc;
    char t[128],b[512]; sprintf(t,"homeassistant/%s/%s/config",ent[i].c,ent[i].id);
    serializeJson(doc,b); mqttClient.publish(t,b,true);
  }

  // ===== Edison LED ON/OFF — switch entity =====
  {JsonDocument doc; doc["name"]="Edison LED"; doc["cmd_t"]=TOPIC_LED_SET;
   doc["stat_t"]=TOPIC_LED_STATE; doc["icon"]="mdi:led-on";
   doc["uniq_id"]="cyberclock18_led_switch"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/switch/cyberclock_led_switch/config",b,true);}

  // ===== Edison LED Dimmer — number entity (0-255 slider) =====
  {JsonDocument doc; doc["name"]="Edison LED Dimmer"; doc["cmd_t"]=TOPIC_LED_DIM_SET;
   doc["stat_t"]=TOPIC_LED_DIM_ST; doc["min"]=0; doc["max"]=255;
   doc["step"]=1; doc["icon"]="mdi:brightness-6";
   doc["uniq_id"]="cyberclock18_led_dimmer"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/number/cyberclock_led_dimmer/config",b,true);}

  // ===== Edison LED Mode — select entity (no dim option) =====
  {JsonDocument doc; doc["name"]="Edison LED Mode"; doc["cmd_t"]=TOPIC_LED_MODE_SET;
   doc["stat_t"]=TOPIC_LED_MODE_ST; doc["icon"]="mdi:led-variant-on";
   doc["options"][0]="off"; doc["options"][1]="on";
   doc["options"][2]="breathe"; doc["options"][3]="blink"; doc["options"][4]="sos";
   doc["uniq_id"]="cyberclock18_ledmode"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/select/cyberclock_ledmode/config",b,true);}

  // Volume number
  {JsonDocument doc; doc["name"]="CC18 Volume"; doc["cmd_t"]=TOPIC_VOL_SET;
   doc["stat_t"]=TOPIC_VOL_STATE; doc["min"]=0; doc["max"]=100;
   doc["unit_of_meas"]="%"; doc["icon"]="mdi:volume-high";
   doc["uniq_id"]="cyberclock18_vol"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/number/cyberclock_vol/config",b,true);}

  // Temp calibration number
  {JsonDocument doc; doc["name"]="CC18 Temp Offset"; doc["cmd_t"]=TOPIC_TCAL_SET;
   doc["stat_t"]=TOPIC_TCAL_STATE; doc["min"]=-15; doc["max"]=15;
   doc["step"]=0.5; doc["unit_of_meas"]="°C"; doc["icon"]="mdi:thermometer-alert";
   doc["uniq_id"]="cyberclock18_tcal"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/number/cyberclock_tcal/config",b,true);}

  // Hum calibration number
  {JsonDocument doc; doc["name"]="CC18 Hum Offset"; doc["cmd_t"]=TOPIC_HCAL_SET;
   doc["stat_t"]=TOPIC_HCAL_STATE; doc["min"]=-20; doc["max"]=20;
   doc["step"]=1; doc["unit_of_meas"]="%"; doc["icon"]="mdi:water-percent";
   doc["uniq_id"]="cyberclock18_hcal"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/number/cyberclock_hcal/config",b,true);}

  // Sleep mode switch
  {JsonDocument doc; doc["name"]="CC18 Uyku"; doc["cmd_t"]=TOPIC_SLEEP_SET;
   doc["stat_t"]=TOPIC_SLEEP_STATE; doc["icon"]="mdi:sleep";
   doc["uniq_id"]="cyberclock18_sleep"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/switch/cyberclock_sleep/config",b,true);}

  // Auto-sleep enable/disable switch
  {JsonDocument doc; doc["name"]="CC18 Oto Uyku"; doc["cmd_t"]=TOPIC_AUTOSLEEP_SET;
   doc["stat_t"]=TOPIC_AUTOSLEEP_STATE; doc["icon"]="mdi:sleep-off";
   doc["uniq_id"]="cyberclock18_autosleep"; doc["dev"]["ids"][0]="cyberclock18_01";
   doc["dev"]["name"]="Cyber Clock 18"; char b[512]; serializeJson(doc,b);
   mqttClient.publish("homeassistant/switch/cyberclock_autosleep/config",b,true);}

  // ===== Remove old entities that are no longer used =====
  // Send empty config to remove old light entity (replaced by switch + dimmer)
  mqttClient.publish("homeassistant/light/cyberclock18_led/config", "", true);

  Serial.println("MQTT Discovery v7 published");
}

void connectMqtt() {
  if(mqttClient.connected()) return;
  unsigned long now=millis(); if(now-lastMqttReconnect<MQTT_RECONNECT_INTERVAL) return;
  lastMqttReconnect=now;
  if(mqttClient.connect(mqtt_client_id,mqtt_user,mqtt_password,TOPIC_STATUS,0,true,"offline")){
    mqttClient.publish(TOPIC_STATUS,"online",true);
    mqttClient.subscribe(TOPIC_ALARM_SET);
    mqttClient.subscribe(TOPIC_BUZZER_SET);
    mqttClient.subscribe(TOPIC_LED_SET);       // LED ON/OFF
    mqttClient.subscribe(TOPIC_LED_DIM_SET);   // LED dimmer slider
    mqttClient.subscribe(TOPIC_LED_MODE_SET);  // LED mode select
    mqttClient.subscribe(TOPIC_VOL_SET);
    mqttClient.subscribe(TOPIC_TCAL_SET);
    mqttClient.subscribe(TOPIC_HCAL_SET);
    mqttClient.subscribe(TOPIC_SLEEP_SET);
    mqttClient.subscribe(TOPIC_AUTOSLEEP_SET);
    mqttClient.subscribe(TOPIC_HOME_CONFIG);
    mqttClient.subscribe(TOPIC_HOME_STATE);
    publishMqttDiscovery();
    mqttClient.publish(TOPIC_ALARM_STATE,alarmEnabled?"ON":"OFF",true);
    publishLedState();
    char vb[8]; sprintf(vb,"%d",volumeLevel); mqttClient.publish(TOPIC_VOL_STATE,vb,true);
    char tb[8]; dtostrf(tempOffset,4,1,tb); mqttClient.publish(TOPIC_TCAL_STATE,tb,true);
    char hb[8]; dtostrf(humOffset,4,1,hb); mqttClient.publish(TOPIC_HCAL_STATE,hb,true);
    mqttClient.publish(TOPIC_SLEEP_STATE, screenSleeping?"ON":"OFF", true);
    mqttClient.publish(TOPIC_AUTOSLEEP_STATE, sleepTimeoutMs>0?"ON":"OFF", true);
  }
}

void publishSensorData() {
  if(!mqttClient.connected()) return;
  unsigned long now=millis(); if(now-lastMqttPublish<MQTT_PUBLISH_INTERVAL) return;
  lastMqttPublish=now; char buf[16];
  dtostrf(curTemp,4,1,buf); mqttClient.publish(TOPIC_TEMP,buf);
  dtostrf(curHum,4,1,buf); mqttClient.publish(TOPIC_HUM,buf);
  sprintf(buf,"%d",curTVOC); mqttClient.publish(TOPIC_TVOC,buf);
  sprintf(buf,"%d",curECO2); mqttClient.publish(TOPIC_CO2,buf);
}

// ============================================================
// Env Sensors
// ============================================================
void updateEnvSensors(bool force) {
  unsigned long now=millis(); if(!force&&(now-lastEnvRead)<5000) return; lastEnvRead=now;
  sensors_event_t hum,temp;
  if(aht.getEvent(&hum,&temp)){
    curTemp=temp.temperature + tempOffset;
    curHum=constrain(hum.relative_humidity + humOffset, 0.0f, 100.0f);
  }
  ens160.set_envdata(curTemp,curHum); ens160.measure();
  uint16_t nT=ens160.getTVOC(),nC=ens160.geteCO2();
  if(nT!=0xFFFF)curTVOC=nT; if(nC!=0xFFFF)curECO2=nC;
}

// ============================================================
// Clock UI
// ============================================================
const int GRID_L=4,GRID_R=156,GRID_TOP=56,GRID_MID=80,GRID_BOT=104;
const int GRID_MID_X=(GRID_L+GRID_R)/2;
const int TOP_LABEL_Y=GRID_TOP+4,TOP_VALUE_Y=GRID_TOP+15;
const int BOTTOM_LABEL_Y=GRID_MID+4,BOTTOM_VALUE_Y=GRID_MID+15;
const int BAR_MARGIN_X=2,BAR_GAP=2,BAR_Y=110,BAR_H=6;
const int BAR_W=(160-2*BAR_MARGIN_X-3*BAR_GAP)/4;

void printCenteredText(const String &t,int x0,int x1,int y,uint16_t c,uint16_t bg,uint8_t s=1){
  int16_t bx,by;uint16_t w,h;tft.setTextSize(s);tft.getTextBounds(t,0,0,&bx,&by,&w,&h);
  tft.setTextColor(c,bg);tft.setCursor(x0+((x1-x0)-(int)w)/2,y);tft.print(t);}

uint16_t colorForCO2(uint16_t e){if(e<=800)return AQ_BAR_GREEN;if(e<=1200)return AQ_BAR_YELLOW;if(e<=1600)return AQ_BAR_ORANGE;return AQ_BAR_RED;}

void initClockStaticUI() {
  tft.fillScreen(CYBER_BG); tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4,4); tft.print("Monitor");
  if(mqttClient.connected()){tft.setTextColor(CYBER_GREEN);tft.setCursor(60,4);tft.print("MQTT");}
  if(ledMode!=LED_OFF){tft.setTextColor(CYBER_LIGHT);tft.setCursor(100,4);tft.print("LED");}
  tft.setTextColor(CYBER_GREY); tft.setCursor(4,44); tft.print("Hava Kalitesi:");
  tft.drawFastHLine(GRID_L,GRID_TOP,GRID_R-GRID_L,CYBER_GREY);
  tft.drawFastHLine(GRID_L,GRID_MID,GRID_R-GRID_L,CYBER_GREY);
  tft.drawFastHLine(GRID_L,GRID_BOT,GRID_R-GRID_L,CYBER_GREY);
  tft.drawFastVLine(GRID_MID_X,GRID_TOP,GRID_BOT-GRID_TOP,CYBER_GREY);

  int ic = (GRID_L+GRID_MID_X)/2;
  tft.fillCircle(ic-8, TOP_LABEL_Y+5, 3, CLR_HUM);
  tft.fillTriangle(ic-8, TOP_LABEL_Y, ic-11, TOP_LABEL_Y+4, ic-5, TOP_LABEL_Y+4, CLR_HUM);
  tft.setCursor(ic-2, TOP_LABEL_Y+1); tft.setTextColor(CLR_HUM,CYBER_BG); tft.print("Nem");

  ic = (GRID_MID_X+GRID_R)/2;
  tft.fillRect(ic-9, TOP_LABEL_Y, 2, 7, CLR_TEMP);
  tft.fillCircle(ic-8, TOP_LABEL_Y+8, 2, CLR_TEMP);
  tft.setCursor(ic-2, TOP_LABEL_Y+1); tft.setTextColor(CLR_TEMP,CYBER_BG); tft.print("Sic");

  ic = (GRID_L+GRID_MID_X)/2;
  tft.drawLine(ic-12, BOTTOM_LABEL_Y+3, ic-4, BOTTOM_LABEL_Y+3, CYBER_GREEN);
  tft.drawLine(ic-10, BOTTOM_LABEL_Y+5, ic-3, BOTTOM_LABEL_Y+5, CYBER_GREEN);
  tft.drawLine(ic-11, BOTTOM_LABEL_Y+7, ic-5, BOTTOM_LABEL_Y+7, CYBER_GREEN);
  tft.setCursor(ic, BOTTOM_LABEL_Y+1); tft.setTextColor(CYBER_GREY,CYBER_BG); tft.print("VOC");

  ic = (GRID_MID_X+GRID_R)/2;
  tft.setCursor(ic-8, BOTTOM_LABEL_Y+1); tft.setTextColor(CYBER_GREY,CYBER_BG); tft.print("CO2");

  int x=BAR_MARGIN_X;
  tft.fillRect(x,BAR_Y,BAR_W,BAR_H,AQ_BAR_GREEN);
  tft.fillRect(x+BAR_W+BAR_GAP,BAR_Y,BAR_W,BAR_H,AQ_BAR_YELLOW);
  tft.fillRect(x+2*(BAR_W+BAR_GAP),BAR_Y,BAR_W,BAR_H,AQ_BAR_ORANGE);
  tft.fillRect(x+3*(BAR_W+BAR_GAP),BAR_Y,BAR_W,BAR_H,AQ_BAR_RED);
  drawAlarmIcon();
}

void drawClockTime(String hS,String mS,String sS){
  String c=hS+":"+mS+":"+sS; if(c==prevTimeStr)return; prevTimeStr=c;
  int16_t x1,y1;uint16_t w,h; tft.setTextSize(3);tft.getTextBounds(c,0,0,&x1,&y1,&w,&h);
  tft.setTextColor(CLR_CLOCK,CYBER_BG);tft.setCursor((160-w)/2,18);tft.print(c);}

void drawCO2Value(uint16_t e,uint16_t c){char b[12];sprintf(b,"%4uppm",e);printCenteredText(String(b),GRID_MID_X,GRID_R,BOTTOM_VALUE_Y,c,CYBER_BG);}

void drawEnvDynamic(float t,float h,uint16_t tv,uint16_t e){
  char b[16];
  sprintf(b,"%2.0f%%",h);printCenteredText(String(b),GRID_L,GRID_MID_X,TOP_VALUE_Y,CLR_HUM,CYBER_BG);
  sprintf(b,"%2.1fC",t);printCenteredText(String(b),GRID_MID_X,GRID_R,TOP_VALUE_Y,CLR_TEMP,CYBER_BG);
  sprintf(b,"%.3fmg/m3",tv/1000.0f);printCenteredText(String(b),GRID_L,GRID_MID_X,BOTTOM_VALUE_Y,CYBER_GREEN,CYBER_BG);
  drawCO2Value(e,colorForCO2(e));
  uint8_t l=(e>1600)?4:(e>1200)?3:(e>800)?2:1;
  tft.fillRect(0,BAR_Y+BAR_H+1,160,8,CYBER_BG);
  int cx=BAR_MARGIN_X+BAR_W/2+(l-1)*(BAR_W+BAR_GAP);
  tft.fillTriangle(cx,BAR_Y+BAR_H-2,cx-4,BAR_Y+BAR_H+4,cx+4,BAR_Y+BAR_H+4,ST77XX_WHITE);
}

// ============================================================
// Menu
// ============================================================
// Scrolling menu: shows MENU_VISIBLE items at a time
#define MENU_VISIBLE 7
int menuScrollOffset = 0;

void drawMenu() {
  tft.fillScreen(CYBER_BG); tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT);
  tft.setCursor(10,6); tft.print("MODE SELECT");
  if(mqttClient.connected()){tft.setTextColor(CYBER_GREEN);tft.setCursor(100,6);tft.print("[MQTT]");}
  const char* items[MENU_ITEMS]={"Monitor","Pomodoro","Alarm","Oyunlar","Edison LED","Ev","Ayarlar","Uyku"};
  
  // Keep selected item in visible window
  if(menuIndex < menuScrollOffset) menuScrollOffset = menuIndex;
  if(menuIndex >= menuScrollOffset + MENU_VISIBLE) menuScrollOffset = menuIndex - MENU_VISIBLE + 1;
  if(menuScrollOffset < 0) menuScrollOffset = 0;
  
  for(int vi=0; vi<MENU_VISIBLE && (menuScrollOffset+vi)<MENU_ITEMS; vi++){
    int i = menuScrollOffset + vi;
    int y = 20 + vi * 15;
    if(i==menuIndex){tft.fillRect(6,y-2,148,13,CYBER_ACCENT);tft.setTextColor(CYBER_BG);}
    else{tft.fillRect(6,y-2,148,13,CYBER_BG);tft.setTextColor(ST77XX_WHITE);}
    tft.setCursor(12,y);tft.print(items[i]);
    if(i==3 || i==5){
      if(i==menuIndex) tft.setTextColor(CYBER_BG);
      else tft.setTextColor(CYBER_DARK);
      tft.setCursor(145, y); tft.print(">");
    }
  }
  // Scroll indicators
  if(menuScrollOffset > 0){tft.setTextColor(CYBER_DARK);tft.setCursor(76,14);tft.print("^");}
  if(menuScrollOffset + MENU_VISIBLE < MENU_ITEMS){tft.setTextColor(CYBER_DARK);tft.setCursor(76,20+MENU_VISIBLE*15);tft.print("v");}
  drawAlarmIcon();
}

// ============================================================
// Games Sub-Menu
// ============================================================
#define GAMES_VISIBLE 6
int gamesScrollOffset = 0;

void drawGamesMenu() {
  tft.fillScreen(CYBER_BG); tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT);
  tft.setCursor(10,6); tft.print("OYUNLAR");
  tft.setTextColor(CYBER_DARK); tft.setCursor(100,6); tft.print("K0:geri");
  
  if(gamesMenuIndex < gamesScrollOffset) gamesScrollOffset = gamesMenuIndex;
  if(gamesMenuIndex >= gamesScrollOffset + GAMES_VISIBLE) gamesScrollOffset = gamesMenuIndex - GAMES_VISIBLE + 1;
  if(gamesScrollOffset < 0) gamesScrollOffset = 0;
  
  for(int vi=0; vi<GAMES_VISIBLE && (gamesScrollOffset+vi)<GAMES_COUNT; vi++){
    int i = gamesScrollOffset + vi;
    int y = 22 + vi * 16;
    if(i==gamesMenuIndex){tft.fillRect(6,y-2,148,14,CYBER_ACCENT);tft.setTextColor(CYBER_BG);}
    else{tft.fillRect(6,y-2,148,14,CYBER_BG);tft.setTextColor(ST77XX_WHITE);}
    tft.setCursor(12,y); tft.print(gameNames[i]);
  }
  if(gamesScrollOffset > 0){tft.setTextColor(CYBER_DARK);tft.setCursor(76,16);tft.print("^");}
  if(gamesScrollOffset + GAMES_VISIBLE < GAMES_COUNT){tft.setTextColor(CYBER_DARK);tft.setCursor(76,22+GAMES_VISIBLE*16);tft.print("v");}
  drawAlarmIcon();
}

// ============================================================
// Pomodoro (free timer)
// ============================================================
void drawPomodoroScreen(bool full) {
  if (full) {
    tft.fillScreen(CYBER_BG); tft.setTextSize(1); tft.setCursor(8,4);
    tft.setTextColor(CYBER_LIGHT); tft.print("POMODORO"); drawAlarmIcon();
  }

  if (pomoState == POMO_SELECT) {
    tft.fillRect(10,25,140,50,CYBER_BG);
    tft.setTextSize(1); tft.setTextColor(CYBER_DARK);
    tft.setCursor(35,28); tft.print("Zamani ayarla:");

    char mBuf[4], sBuf[4];
    sprintf(mBuf, "%02d", pomoMinutes);
    sprintf(sBuf, "%02d", pomoSeconds);

    tft.setTextSize(3);
    int16_t x1,y1; uint16_t w,h;
    String disp = String(mBuf) + ":" + String(sBuf);
    tft.getTextBounds(disp,0,0,&x1,&y1,&w,&h);
    int x = (160-w)/2;
    tft.setCursor(x, 42);
    tft.setTextColor(pomoField==0 ? CYBER_LIGHT : ST77XX_WHITE, CYBER_BG);
    tft.print(mBuf);
    tft.setTextColor(ST77XX_WHITE, CYBER_BG); tft.print(":");
    tft.setTextColor(pomoField==1 ? CYBER_LIGHT : ST77XX_WHITE, CYBER_BG);
    tft.print(sBuf);

    tft.setTextSize(1);
    tft.setTextColor(CYBER_DARK);
    tft.setCursor(x, 68); tft.print("dk");
    tft.setCursor(x + w - 12, 68); tft.print("sn");

    tft.fillRect(0,80,160,48,CYBER_BG);
    tft.setTextColor(CYBER_ACCENT); tft.setCursor(10,85);
    tft.print("Cevir: deger degistir");
    tft.setCursor(10,97); tft.print("Bas: dk<->sn / Baslat");
    tft.setCursor(10,109); tft.print("Uzun bas: BASLAT");
  } else {
    unsigned long dMs = (pomoMinutes*60UL + pomoSeconds) * 1000UL;
    unsigned long elapsed = 0;
    if(pomoState==POMO_RUNNING) elapsed=millis()-pomoStartMillis;
    else if(pomoState==POMO_PAUSED) elapsed=pomoPausedMillis-pomoStartMillis;
    else if(pomoState==POMO_DONE) elapsed=dMs;
    if(elapsed>dMs) elapsed=dMs;

    float progress = (dMs>0) ? (float)elapsed/dMs : 0;
    int cx=80,cy=64,rO=55,rI=44;
    float startD=-225,endD=45,span=endD-startD;
    for(float d=startD;d<=endD;d+=6.0f){
      float frac=(d-startD)/span;
      uint16_t col;
      if(frac<=progress){if(frac<0.33f)col=AQ_BAR_GREEN;else if(frac<0.66f)col=AQ_BAR_YELLOW;else if(frac<0.85f)col=AQ_BAR_ORANGE;else col=AQ_BAR_RED;}
      else col=CYBER_DARK;
      float rad=d*PI/180.0f;
      tft.drawLine(cx+cos(rad)*rI,cy+sin(rad)*rI,cx+cos(rad)*rO,cy+sin(rad)*rO,col);
    }

    tft.fillCircle(cx,cy,38,CYBER_BG);
    unsigned long rem=dMs-elapsed;
    int remMin=rem/60000UL, remSec=(rem/1000UL)%60;
    char buf[8]; sprintf(buf,"%02d:%02d",remMin,remSec);
    int16_t x1,y1; uint16_t w,h;
    tft.setTextSize(3);tft.getTextBounds(buf,0,0,&x1,&y1,&w,&h);
    tft.setCursor(cx-w/2,cy-8);tft.setTextColor(ST77XX_WHITE,CYBER_BG);tft.print(buf);

    tft.fillRect(0,96,160,32,CYBER_BG);tft.setTextSize(1);tft.setCursor(8,100);
    tft.setTextColor(CYBER_GREEN,CYBER_BG);
    if(pomoState==POMO_PAUSED) tft.print("Durdu | Bas:devam K0:iptal");
    else if(pomoState==POMO_DONE) tft.print("Tamamlandi! Bas:yeni K0:menu");
    else tft.print("Calisiyor | Bas:durdur");
  }
}

// ============================================================
// Alarm
// ============================================================
void drawAlarmScreen(bool full) {
  if(full){tft.fillScreen(CYBER_BG);tft.setTextSize(1);tft.setCursor(8,4);
    tft.setTextColor(CYBER_LIGHT);tft.print("ALARM");drawAlarmIcon();}
  char hB[3],mB[3];sprintf(hB,"%02d",alarmHour);sprintf(mB,"%02d",alarmMinute);
  tft.setTextSize(3);String disp=String(hB)+":"+String(mB);
  int16_t x1,y1;uint16_t w,h;tft.getTextBounds(disp,0,0,&x1,&y1,&w,&h);
  int x=(160-w)/2;tft.setCursor(x,30);
  tft.setTextColor(alarmSelectedField==0?CYBER_LIGHT:ST77XX_WHITE,CYBER_BG);tft.print(hB);
  tft.setTextColor(ST77XX_WHITE,CYBER_BG);tft.print(":");
  tft.setTextColor(alarmSelectedField==1?CYBER_LIGHT:ST77XX_WHITE,CYBER_BG);tft.print(mB);
  tft.setTextSize(1);tft.fillRect(20,80,120,24,CYBER_BG);
  tft.setCursor(30,84);tft.setTextColor(ST77XX_WHITE,CYBER_BG);tft.print("Alarm:");
  tft.setCursor(80,84);
  uint16_t ec=alarmSelectedField==2?CYBER_LIGHT:(alarmEnabled?CYBER_GREEN:ST77XX_RED);
  tft.setTextColor(ec,CYBER_BG);tft.print(alarmEnabled?"ON ":"OFF");
}

void drawAlarmRingingScreen(){
  tft.fillScreen(ST77XX_RED);tft.setTextSize(2);tft.setTextColor(ST77XX_WHITE,ST77XX_RED);
  tft.setCursor(30,40);tft.print("ALARM!");
  tft.setTextSize(1);tft.setCursor(15,70);tft.print("Herhangi tusa basin");
}

void checkAlarmTrigger(){
  if(!alarmEnabled||alarmRinging) return;
  struct tm ti; if(!getLocalTime(&ti)) return;
  if(ti.tm_hour==alarmHour&&ti.tm_min==alarmMinute&&ti.tm_sec==0){
    alarmRinging=true;lastAlarmDayTriggered=ti.tm_mday;
    if(currentMode==MODE_TETRIS) { tetInited=false; }
    if(currentMode==MODE_BREAKOUT) { brkInited=false; }
    if(currentMode==MODE_FLAPPY) { flpInited=false; }
    if(currentMode==MODE_PONG) { pngInited=false; }
    if(currentMode==MODE_2048) { g48Inited=false; }
    currentMode=MODE_ALARM;drawAlarmRingingScreen();
    playAlarmByIndex(selectedAlarm,true);
  }
}

void updateAlertStateAndLED(){
  if(alarmRinging)currentAlertLevel=ALERT_ALARM;
  else if(curECO2>1600)currentAlertLevel=ALERT_CO2;
  else currentAlertLevel=ALERT_NONE;

  unsigned long now=millis();

  if(currentAlertLevel==ALERT_ALARM){
    alertLedActive=true;
    if(now-lastAlertLedMs>120){
      lastAlertLedMs=now;
      alertLedState=!alertLedState;
      analogWrite(LED_PIN, alertLedState?255:0);
    }
  }
  else if(currentAlertLevel==ALERT_CO2){
    alertLedActive=true;
    
    // Start CO2 alarm once (melody + screen)
    if(!co2AlertTriggered){
      co2AlertTriggered=true;
      if(currentMode==MODE_TETRIS) { tetInited=false; }
      if(currentMode==MODE_BREAKOUT) { brkInited=false; }
      if(currentMode==MODE_FLAPPY) { flpInited=false; }
      if(currentMode==MODE_PONG) { pngInited=false; }
      if(currentMode==MODE_2048) { g48Inited=false; }
      startMelody(co2AlarmMelody, co2AlarmDurations, CO2_ALARM_MELODY_LEN, true);
      // Show CO2 alert screen
      tft.fillScreen(AQ_BAR_RED);
      tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, AQ_BAR_RED);
      tft.setCursor(15,20); tft.print("CO2 ALARM!");
      tft.setTextSize(3); tft.setCursor(20,50);
      tft.printf("%dppm", curECO2);
      tft.setTextSize(1); tft.setCursor(10,90);
      tft.print("Hava kalitesi cok kotu!");
      tft.setCursor(10,105); tft.print("Pencereyi acin!");
      tft.setCursor(15,120); tft.print("Herhangi tusa basin");
    }
    
    // Blink LED
    if(now-lastCo2BlinkMs>350){
      lastCo2BlinkMs=now;
      co2BlinkOn=!co2BlinkOn;
      alertLedState=co2BlinkOn;
      analogWrite(LED_PIN, alertLedState?255:0);
    }
  }
  else {
    // CO2 dropped below threshold — reset alert
    if(co2AlertTriggered){
      co2AlertTriggered=false;
      stopMelody();
    }
    if(alertLedActive){
      alertLedActive=false;
      updateLed();
    }
  }
}

void drawCo2AlertScreen(){
  tft.fillScreen(AQ_BAR_RED);
  tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE, AQ_BAR_RED);
  tft.setCursor(15,20); tft.print("CO2 ALARM!");
  tft.setTextSize(3); tft.setCursor(20,50);
  tft.printf("%dppm", curECO2);
  tft.setTextSize(1); tft.setCursor(10,90);
  tft.print("Hava kalitesi cok kotu!");
  tft.setCursor(10,105); tft.print("Pencereyi acin!");
  tft.setCursor(15,120); tft.print("Herhangi tusa basin");
}

void dismissAlert(){
  if(currentAlertLevel==ALERT_ALARM){
    alarmRinging=false;
    stopMelody();
    mqttClient.publish(TOPIC_ALARM_STATE,"OFF");
  }
  if(co2AlertTriggered){
    co2AlertTriggered=false;
    stopMelody();
  }
  alertLedActive=false;
  updateLed();
}

// ============================================================
// Snake Game
// ============================================================
void snkSpawnFood() {
  bool valid;
  do {
    valid = true;
    foodX = random(SNK_COLS);
    foodY = random(SNK_ROWS);
    for (int i = 0; i < snakeLen; i++) {
      if (snakeX[i] == foodX && snakeY[i] == foodY) { valid = false; break; }
    }
  } while (!valid);
}

void snkDrawBlock(int col, int row, uint16_t color) {
  tft.fillRect(SNK_X + col * SNK_BLOCK, SNK_Y + row * SNK_BLOCK, SNK_BLOCK-1, SNK_BLOCK-1, color);
}

void snkDrawScore() {
  tft.fillRect(0, 0, 80, 12, CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_GREEN, CYBER_BG);
  tft.setCursor(4, 2); tft.printf("Skor:%d", snkScore);
}

void initSnake() {
  snkInited = true; snkGameOver = false;
  snkScore = 0; snakeLen = 3; snakeDir = 0;
  snkMoveInterval = 200;
  snkDirQueueLen = 0;  // Clear direction queue
  
  for (int i = 0; i < snakeLen; i++) {
    snakeX[i] = SNK_COLS/2 - i;
    snakeY[i] = SNK_ROWS/2;
  }
  snkSpawnFood();
  
  tft.fillScreen(CYBER_BG);
  tft.setTextSize(1); tft.setTextColor(CYBER_LIGHT, CYBER_BG);
  tft.setCursor(80, 2); tft.print("SNAKE");
  tft.drawRect(SNK_X-1, SNK_Y-1, SNK_COLS*SNK_BLOCK+2, SNK_ROWS*SNK_BLOCK+2, ST77XX_WHITE);
  snkDrawScore();
  
  for (int i = 0; i < snakeLen; i++) {
    snkDrawBlock(snakeX[i], snakeY[i], i==0 ? CYBER_ACCENT : CYBER_GREEN);
  }
  snkDrawBlock(foodX, foodY, AQ_BAR_RED);
}

void updateSnake(int enc, bool encP, bool back) {
  if (back) { snkInited=false; currentMode=MODE_GAMES_MENU; drawGamesMenu(); return; }
  
  if (snkGameOver) {
    tft.setTextSize(2); tft.setTextColor(ST77XX_RED, CYBER_BG);
    tft.setCursor(25, 50); tft.print("GAME OVER");
    tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE, CYBER_BG);
    tft.setCursor(35, 75); tft.printf("Skor: %d", snkScore);
    if (encP || enc != 0) initSnake();
    return;
  }
  
  // Queue direction changes (max 2 buffered) — prevents fast triple-turn self-kill
  if (enc != 0 && snkDirQueueLen < 2) {
    // Calculate new direction based on last queued (or current) direction
    int baseDir = (snkDirQueueLen > 0) ? snkDirQueue[snkDirQueueLen-1] : snakeDir;
    int newDir;
    if (enc > 0) newDir = (baseDir + 1) % 4;  // clockwise = turn right
    else newDir = (baseDir + 3) % 4;           // counter-clockwise = turn left
    
    // Allow all turns including 180° — queue limit (max 2) prevents 270° crash
    snkDirQueue[snkDirQueueLen++] = newDir;
  }
  
  unsigned long interval = encP ? 50 : snkMoveInterval;
  
  unsigned long now = millis();
  if (now - snkLastMove < interval) return;
  snkLastMove = now;
  
  // Dequeue one direction per movement step
  if (snkDirQueueLen > 0) {
    snakeDir = snkDirQueue[0];
    // Shift queue
    for (int i = 1; i < snkDirQueueLen; i++) snkDirQueue[i-1] = snkDirQueue[i];
    snkDirQueueLen--;
  }
  
  int newX = snakeX[0], newY = snakeY[0];
  switch (snakeDir) {
    case 0: newX++; break;
    case 1: newY++; break;
    case 2: newX--; break;
    case 3: newY--; break;
  }
  
  if (newX < 0 || newX >= SNK_COLS || newY < 0 || newY >= SNK_ROWS) {
    snkGameOver = true; vTone(BUZZ_PIN, 200, 500); return;
  }
  
  for (int i = 0; i < snakeLen; i++) {
    if (snakeX[i] == newX && snakeY[i] == newY) {
      snkGameOver = true; vTone(BUZZ_PIN, 200, 500); return;
    }
  }
  
  bool ate = (newX == foodX && newY == foodY);
  
  if (!ate) {
    snkDrawBlock(snakeX[snakeLen-1], snakeY[snakeLen-1], 0);
    for (int i = snakeLen-1; i > 0; i--) {
      snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1];
    }
  } else {
    snakeLen++;
    for (int i = snakeLen-1; i > 0; i--) {
      snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1];
    }
    snkScore += 10;
    snkDrawScore();
    vTone(BUZZ_PIN, 1500, 30);
    if (snkMoveInterval > 80) snkMoveInterval -= 5;
    snkSpawnFood();
    snkDrawBlock(foodX, foodY, AQ_BAR_RED);
  }
  
  snakeX[0] = newX; snakeY[0] = newY;
  snkDrawBlock(snakeX[0], snakeY[0], CYBER_ACCENT);
  if (snakeLen > 1) snkDrawBlock(snakeX[1], snakeY[1], CYBER_GREEN);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200); delay(1500);
  loadSettings();  // Load saved calibration, volume, sleep timeout from NVS
  Serial.printf("Loaded: tempOff=%.1f humOff=%.1f vol=%d sleep=%lums\n", tempOffset, humOffset, volumeLevel, sleepTimeoutMs);
  pinMode(ENC_A_PIN,INPUT_PULLUP); pinMode(ENC_B_PIN,INPUT_PULLUP);
  pinMode(ENC_BTN_PIN,INPUT_PULLUP); pinMode(KEY0_PIN,INPUT_PULLUP);
  pinMode(LED_PIN,OUTPUT); pinMode(BUZZ_PIN,OUTPUT);
  analogWrite(LED_PIN,0);
  Wire.begin(SDA_PIN,SCL_PIN);
  SPI.begin(TFT_SCLK,-1,TFT_MOSI,TFT_CS);
  tft.initR(INITR_BLACKTAB); tft.setRotation(1); tft.fillScreen(CYBER_BG);
  connectWiFiAndSyncTime();
  Wire.end(); delay(100); Wire.begin(SDA_PIN,SCL_PIN); delay(100);
  if(!aht.begin()) Serial.println("AHT21 fail"); else Serial.println("AHT21 OK");
  if(!ens160.begin()) Serial.println("ENS160 fail"); else{ens160.setMode(ENS160_OPMODE_STD);Serial.println("ENS160 OK");}
  updateEnvSensors(true);
  initClockStaticUI(); prevTimeStr="";
  drawClockTime(getTimeStr('H'),getTimeStr('M'),getTimeStr('S'));
  drawEnvDynamic(curTemp,curHum,curTVOC,curECO2);
  randomSeed(millis() ^ (uint32_t)curTemp);
  pinMode(KEY0_PIN, INPUT_PULLUP);
  lastActivityMs = millis();  // Initialize auto-sleep timer
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  static unsigned long lastWifiCheck=0;
  bool inGame = (currentMode==MODE_TETRIS || currentMode==MODE_GAME || currentMode==MODE_BREAKOUT || currentMode==MODE_FLAPPY || currentMode==MODE_PONG || currentMode==MODE_2048);

  // WiFi/MQTT always runs (even in sleep — sensors + HA control)
  if(!inGame || screenSleeping){
    if(millis()-lastWifiCheck>30000){lastWifiCheck=millis();
      if(WiFi.status()!=WL_CONNECTED){WiFi.disconnect(true);delay(200);WiFi.mode(WIFI_STA);WiFi.begin(ssid,password);}}
    if(WiFi.status()==WL_CONNECTED){if(!mqttClient.connected())connectMqtt();mqttClient.loop();publishSensorData();}
  } else {
    if(WiFi.status()==WL_CONNECTED) mqttClient.loop();
  }

  updateLed(); updateMelody(); vToneUpdate();

  int encStep=readEncoderStep();
  bool encPressed=checkButtonPressed(ENC_BTN_PIN,lastEncBtn);
  bool k0Pressed=checkButtonPressed(KEY0_PIN,lastKey0);

  // Track any user activity for auto-sleep timer
  bool anyActivity = (encStep != 0 || encPressed || k0Pressed);
  if(anyActivity) resetActivityTimer();

  checkAlarmTrigger(); updateAlertStateAndLED();

  // Always read sensors (not just on clock screen)
  updateEnvSensors();

  // === SLEEP MODE HANDLING ===
  if(screenSleeping){
    // Keep reading sensors and publishing to HA even while sleeping
    updateEnvSensors();
    
    // Wake on alarm
    if(alarmRinging){
      wakeUp();
      currentMode=MODE_ALARM;
      drawAlarmRingingScreen();
      return;
    }
    // Wake on CO2 alert too
    if(currentAlertLevel==ALERT_CO2){
      wakeUp();
      return;
    }
    // Wake on any button/encoder press (but NOT encoder rotation to avoid accidental wake)
    if(encPressed || k0Pressed){
      wakeUp();
      return;  // Don't process the button press further
    }
    // While sleeping, still read encoder but ignore it
    return;  // Skip all UI updates
  }

  // === K0 LONG PRESS DETECTION (sleep from any screen) ===
  bool k0Cur = digitalRead(KEY0_PIN);
  if(k0Cur == LOW){
    if(k0HoldStart == 0) k0HoldStart = millis();
    if(!k0Held && millis() - k0HoldStart > 1000){
      // Long press detected — enter sleep
      k0Held = true;
      enterSleep();
      return;
    }
  } else {
    k0HoldStart = 0;
    k0Held = false;
  }
  // Suppress k0Pressed if we're in a long press hold (avoid triggering menu exit)
  if(k0HoldStart > 0 && millis() - k0HoldStart > 200) k0Pressed = false;

  // === AUTO-SLEEP TIMER ===
  if(sleepTimeoutMs > 0 && !inGame && currentMode != MODE_SLEEP){
    if(millis() - lastActivityMs > sleepTimeoutMs){
      enterSleep();
      return;
    }
  }

  // === CO2 ALERT DISMISS ===
  // When CO2 alert is active, any button dismisses it and restores previous screen
  if(co2AlertTriggered && (encPressed || k0Pressed)){
    dismissAlert();
    // Redraw current screen
    switch(currentMode){
      case MODE_CLOCK: initClockStaticUI(); prevTimeStr="";
        drawClockTime(getTimeStr('H'),getTimeStr('M'),getTimeStr('S'));
        drawEnvDynamic(curTemp,curHum,curTVOC,curECO2); break;
      case MODE_MENU: drawMenu(); break;
      default: drawMenu(); currentMode=MODE_MENU; break;
    }
    return;
  }

  switch(currentMode){
    case MODE_MENU:
      if(encStep!=0){menuIndex+=encStep;if(menuIndex<0)menuIndex=MENU_ITEMS-1;if(menuIndex>=MENU_ITEMS)menuIndex=0;drawMenu();}
      if(encPressed){
        switch(menuIndex){
          case 0:currentMode=MODE_CLOCK;initClockStaticUI();prevTimeStr="";updateEnvSensors(true);
            drawClockTime(getTimeStr('H'),getTimeStr('M'),getTimeStr('S'));
            drawEnvDynamic(curTemp,curHum,curTVOC,curECO2);break;
          case 1:currentMode=MODE_POMODORO;pomoState=POMO_SELECT;pomoField=0;prevPomoForce=true;drawPomodoroScreen(true);break;
          case 2:currentMode=MODE_ALARM;alarmSelectedField=0;drawAlarmScreen(true);break;
          case 3:currentMode=MODE_GAMES_MENU;gamesMenuIndex=0;drawGamesMenu();break;  // Oyunlar
          case 4:currentMode=MODE_LED;ledMenuSelection=(int)ledMode;drawLedScreen(true);break;
          case 5:currentMode=MODE_HOME;homeInited=false;break;  // Ev
          case 6:currentMode=MODE_SETTINGS;settingsField=0;drawSettingsScreen(true);break;
          case 7:enterSleep();break;
        }
      }
      break;

    // === GAMES SUB-MENU ===
    case MODE_GAMES_MENU:
      if(encStep!=0){gamesMenuIndex+=encStep;if(gamesMenuIndex<0)gamesMenuIndex=GAMES_COUNT-1;if(gamesMenuIndex>=GAMES_COUNT)gamesMenuIndex=0;drawGamesMenu();}
      if(encPressed){
        switch(gamesMenuIndex){
          case 0:currentMode=MODE_GAME;snkInited=false;break;       // Snake
          case 1:currentMode=MODE_TETRIS;tetInited=false;break;      // Tetris
          case 2:currentMode=MODE_BREAKOUT;brkInited=false;break;    // Breakout
          case 3:currentMode=MODE_FLAPPY;flpInited=false;break;      // Flappy Bird
          case 4:currentMode=MODE_PONG;pngInited=false;break;        // Pong
          case 5:currentMode=MODE_2048;g48Inited=false;break;        // 2048
        }
      }
      if(k0Pressed){currentMode=MODE_MENU;drawMenu();}
      break;

    case MODE_CLOCK:{
      struct tm ti;
      if(getLocalTime(&ti)){if(ti.tm_sec!=prevSecond){prevSecond=ti.tm_sec;
        drawClockTime(getTimeStr('H'),getTimeStr('M'),getTimeStr('S'));
        if(ti.tm_sec%5==0){updateEnvSensors(true);drawEnvDynamic(curTemp,curHum,curTVOC,curECO2);}}}
      if(k0Pressed){currentMode=MODE_MENU;drawMenu();}
      break;}

    case MODE_POMODORO:{
      if(pomoState==POMO_SELECT){
        if(encStep!=0){
          if(pomoField==0){pomoMinutes=constrain((int)pomoMinutes+encStep,0,99);}
          else{pomoSeconds=constrain((int)pomoSeconds+encStep,0,59);}
          drawPomodoroScreen(false);
        }
        if(encPressed){
          static unsigned long lastPress=0;
          unsigned long now=millis();
          if(now-lastPress<500 && (pomoMinutes>0||pomoSeconds>0)){
            pomoState=POMO_RUNNING;
            pomoStartMillis=millis();
            prevPomoRemainSec=-1;
            drawPomodoroScreen(true);
          } else {
            pomoField=(pomoField+1)%2;
            drawPomodoroScreen(false);
          }
          lastPress=now;
        }
      }
      else if(pomoState==POMO_RUNNING||pomoState==POMO_PAUSED){
        unsigned long dMs=(pomoMinutes*60UL+pomoSeconds)*1000UL;
        unsigned long elapsed=0;
        if(pomoState==POMO_RUNNING)elapsed=millis()-pomoStartMillis;
        else elapsed=pomoPausedMillis-pomoStartMillis;
        if(elapsed>=dMs){
          if(pomoState!=POMO_DONE){pomoState=POMO_DONE;startMelody(pomoDoneMelody,pomoDoneDurations,POMO_MELODY_LEN,false);drawPomodoroScreen(true);}
        }
        int rs=(dMs-elapsed)/1000UL;
        if(rs!=prevPomoRemainSec){prevPomoRemainSec=rs;drawPomodoroScreen(false);}

        if(encPressed){
          if(pomoState==POMO_RUNNING){pomoState=POMO_PAUSED;pomoPausedMillis=millis();drawPomodoroScreen(true);}
          else{pomoStartMillis+=(millis()-pomoPausedMillis);pomoState=POMO_RUNNING;prevPomoRemainSec=-1;drawPomodoroScreen(true);}
        }
      }
      else if(pomoState==POMO_DONE){
        if(encPressed){pomoState=POMO_SELECT;pomoField=0;drawPomodoroScreen(true);}
      }
      if(k0Pressed){currentMode=MODE_MENU;drawMenu();}
      break;}

    case MODE_ALARM:{
      if(alarmRinging){
        if(encPressed||k0Pressed){dismissAlert();drawAlarmScreen(true);}
        break;}
      bool ch=false;
      if(encStep!=0){
        if(alarmSelectedField==0){alarmHour=(alarmHour+(encStep>0?1:23))%24;ch=true;}
        else if(alarmSelectedField==1){alarmMinute=(alarmMinute+(encStep>0?1:59))%60;ch=true;}
        else{alarmEnabled=!alarmEnabled;ch=true;mqttClient.publish(TOPIC_ALARM_STATE,alarmEnabled?"ON":"OFF",true);}
        if(ch)lastAlarmDayTriggered=-1;}
      if(encPressed){alarmSelectedField=(alarmSelectedField+1)%3;ch=true;}
      if(k0Pressed){currentMode=MODE_MENU;drawMenu();break;}
      if(ch){drawAlarmScreen(false);drawAlarmIcon();}
      break;}

    case MODE_GAME:
      if(!snkInited) initSnake();
      updateSnake(encStep,encPressed,k0Pressed);
      break;

    case MODE_TETRIS:
      if(!tetInited) initTetris();
      updateTetris(encStep,encPressed,k0Pressed);
      break;

    case MODE_BREAKOUT:
      if(!brkInited) initBreakout();
      updateBreakout(encStep,encPressed,k0Pressed);
      break;

    case MODE_FLAPPY:
      if(!flpInited) initFlappy();
      updateFlappy(encStep,encPressed,k0Pressed);
      break;

    case MODE_PONG:
      if(!pngInited) initPong();
      updatePong(encStep,encPressed,k0Pressed);
      break;

    case MODE_2048:
      if(!g48Inited) init2048();
      update2048(encStep,encPressed,k0Pressed);
      break;

    case MODE_HOME:
      if(!homeInited) initHome();
      updateHome(encStep,encPressed,k0Pressed);
      break;

    case MODE_LED:{
      static bool dimAdj=false;
      if(encStep!=0){
        if(dimAdj){
          ledBrightness=constrain((int)ledBrightness+encStep*15,5,255);
          analogWrite(LED_PIN,ledBrightness);
          publishLedState();  // Sync dimmer to HA when changed on device
        } else {
          ledMenuSelection+=encStep;
          if(ledMenuSelection<0)ledMenuSelection=LED_MODE_COUNT-1;
          if(ledMenuSelection>=LED_MODE_COUNT)ledMenuSelection=0;
        }
        drawLedScreen(false);
      }
      if(encPressed){
        if(dimAdj){
          dimAdj=false;
        } else {
          ledMode=(LedMode)ledMenuSelection;
          breatheVal=0; breatheDir=1;
          sosStep=0; sosLastMs=millis();
          lastLedUpdate=millis();
          publishLedState();  // Sync brightness to HA
          if(ledMode==LED_DIM) dimAdj=true;
        }
        drawLedScreen(false);
      }
      if(k0Pressed){dimAdj=false;currentMode=MODE_MENU;drawMenu();}
      break;}

    case MODE_SETTINGS:
      if(encStep!=0){
        switch(settingsField){
          case 0: volumeLevel=constrain((int)volumeLevel+encStep*5,0,100);
                  if(volumeLevel>0){ vTone(BUZZ_PIN,1000,50); }
                  break;
          case 1: selectedAlarm=constrain((int)selectedAlarm+encStep,0,ALARM_COUNT-1);
                  playAlarmByIndex(selectedAlarm,false);
                  break;
          case 2: tempOffset=constrain(tempOffset+encStep*0.5f,-15.0f,15.0f); break;
          case 3: humOffset=constrain(humOffset+encStep*1.0f,-20.0f,20.0f); break;
          case 4: sleepOptionIndex=constrain(sleepOptionIndex+encStep,0,SLEEP_OPTION_COUNT-1);
                  sleepTimeoutMs=sleepOptions[sleepOptionIndex];
                  resetActivityTimer();
                  // Sync to HA
                  if(mqttClient.connected()) mqttClient.publish(TOPIC_AUTOSLEEP_STATE, sleepTimeoutMs>0?"ON":"OFF", true);
                  break;
        }
        drawSettingsScreen(false);
      }
      if(encPressed){
        settingsField=(settingsField+1)%SETTINGS_COUNT;
        drawSettingsScreen(false);
      }
      if(k0Pressed){
        vToneStop(); stopMelody();
        saveSettings();  // Save all settings to NVS
        currentMode=MODE_MENU;
        drawMenu();
      }
      break;
  }
}
