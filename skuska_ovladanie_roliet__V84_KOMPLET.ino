// ====================================================================================
// PROJEKT: INTELIGENTNE OVLADANIE 6 ZALUZII S JEMNYM NAKLAPANIM S DIODOU S VIRTUALNOU 0
// VERZIA: V84 - FIX PRELUCHY, POMALA RYCHLOST V KROKU 3, NVS PERSIST
// ARCHITEKTURA: ESP32 WROOM + DUAL I2C + 2x PCA9548A + 2x PCF8574T + 6x SA5600
// LOGIKA S BC337: Pokoja / Start = 0x00 (Vsetky rele bezpecne rozpojene and vypnute)
// FIX V83: ZJEDNOTENE PORADIE VYSTUPOV PRE PCF1 (HORE = BIT 0,2,4 | DOLE = BIT 1,3,5)
// KASKADA: SYSTEMOVY WATCHDOG ZAPISOV + HARDVEROVY I2C BUS RECOVERY REZIM
// POMALA RYCHLOST: JEDINE DIODOVE RELE PRE VSETKY ROLEY NA VETVE 2 (PCF2 BIT 6)
// BEZPECNOST: POODDELOVANE KVALITNE SUB-NAMESPACES PRE PREFERENCES (ZACHRANA PAMATE)
// OPTIMALIZACIA: RETRY MECHANIZMUS PRE PCF EXPANDERY (STOP CVAKANIU A PRESLUCHOM)
// ====================================================================================
// OPRAVY V84:
// 1. POSLEDNY MANEVER (KROK 3) POUZIVA BIT 6 (SLOW SPEED) NA PCF2 PRE MINIMALIZACIU PRELUCHOV
// 2. IZOLACNY CAS 200us + SYNCHRONNE FLUSH PRE ELIMINU PRELUCHY
// 3. AUTOMATICKE UKLADANIE DO NVS KAZDU MINUTU (NIE LEN NA FLAG)
// 4. ZJEDNOTENE PORADIE: PCF1 (Z1-3): BIT 0=UP1,1=DN1,2=UP2,3=DN2,4=UP3,5=DN3,7=LIGHT2
//                        PCF2 (Z4-6): BIT 0=UP4,1=DN4,2=UP5,3=DN5,4=UP6,5=DN6,6=SLOW,7=LIGHT1
// ====================================================================================

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <math.h>
#include <time.h>
#include <ESP_Mail_Client.h>
#include <Wire.h>

// --- HARDVEROVE ADRESY PRE I2C ZBERNICE ---
#define PCA_ADDR_1   0x70  // Multiplexer pre Vetvu 1 (Zaluzie 1, 2, 3)
#define PCA_ADDR_2   0x71  // Multiplexer pre Vetvu 2 (Zaluzie 4, 5, 6)
#define PCF_ADDR_1   0x20  // Expander rele pre Vetvu 1 (Zaluzie 1, 2, 3 + Svetlo 2 na Bit 7)
#define PCF_ADDR_2   0x21  // Expander rele pre Vetvu 2 (Zaluzie 4, 5, 6 + Svetlo 1 na Bit 7)
#define SA5600_ADDR  0x36  // Spolocna vnutorna adresa snimacov enkoderov SA5600

// --- STRUKTURA STAVOVEHO AUTOMATU PRE EXTERNE TLACIDLA ---
struct ButtonStateMachine {
  bool lastRawState;
  bool debouncedState;
  unsigned long lastDebounceTime;
  unsigned long lastClickTime;
  int clickCount;
  bool isHoldingActive;
};

// --- GLOBALNE KONSTANTY A OBJEKTY ---
const unsigned long DIRECTION_DELAY = 1000;
const int GRAPH_SIZE = 96;                  
const unsigned long NVS_AUTO_SAVE_INTERVAL = 60000;

AsyncWebServer server(80);
Preferences preferences;
SMTPSession smtp;

// --- KONFIGURACIA BEZDROTOVEJ SIETE (WIFI) ---
const char* ssid = "O2_Internet_3091";
const char* password = "48HL8MC7KJ";
IPAddress local_IP(192, 168, 1, 85);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);

// --- KONFIGURACIA CASU (NTP SERVER) ---
const char* ntpServer = "sk.pool.ntp.org";
const char* timeZone = "CET-1CEST,M3.5.0,M10.5.0/3";

// --- KONFIGURACIA EMAILU (SMTP GMAIL) ---
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL    "mojeprojektyovladania@gmail.com"
#define AUTHOR_PASSWORD "xgzb pdks sayx gxtg"
#define RECIPIENT_EMAIL "mojeprojektyovladania@gmail.com"

// --- GLOBALNE STAVOVE PREMENNE SENSINGU ---
volatile unsigned long pulseCount = 0;
float windSpeed = 0.0;
float windSpeedMax = 0.0;
float windSpeedMax24h = 0.0; 
float lightLevelFiltered = 0.0;
int lightLevel = 0;
int wifiRSSI = 0;
float tempInt = 0.0;
float tempExt = 0.0;

// --- PAMATOVE REGISTRE PRE ROLLING TIME WINDOW ---
float ntcIntRollBuf[60] = {0.0};
float ntcExtRollBuf[60] = {0.0};
float ldrRollBuf[60] = {0.0};
int ntcIntRollCount = 0;
int ntcExtRollCount = 0;
int ldrRollCount = 0;

// --- CASOVANIE A BEZPECNOSTNE SYSTEMY ---
unsigned long lastSensorMillis = 0;
unsigned long lastGraphMillis = 0;
unsigned long lastNVSSaveMillis = 0;
unsigned long bootMillis = 0;
float windLimit = 45.0;
float windPulsesFor25kmh = 25.0; 
int pulsesPerTurn = 2;
bool safetyActive = false;
unsigned long windSafetyUntil = 0;
unsigned long windSafetyDuration = 60000;
unsigned long windRunUntil = 0;
int windRunTime = 60;
int windMaxHits = 3;
int windHitCounter = 0;
bool globalAutoEnable = true;
bool emailSentForCurrentAlarm = false;
volatile bool resetIntMinMaxFlag = false;
volatile bool resetExtMinMaxFlag = false;
volatile bool resetWindMaxFlag = false;
volatile bool saveSettingsFlag = false;

// --- KALIBRACIA A PARAMETRE FILTROVANIA ---
int ldrSamples = 20;
float ldrFilter = 0.10;
int ntcIntR0 = 10000;
int ntcIntBeta = 3977;
float ntcIntSeriesR = 10000.0;
int ntcIntSamples = 20;
float ntcIntFilter = 0.10;
float ntcIntOffset = -2.5;
int ntcExtR0 = 10000;
int ntcExtBeta = 3977;
float ntcExtSeriesR = 10000.0;
int ntcExtSamples = 20;
float ntcExtFilter = 0.10;
float ntcExtOffset = -2.5;

// --- MINIMA A MAXIMA TEPLOT (DRZANE V RAM PRE OCHRANU FLASH) ---
float tempIntMin = 99.9;
float tempIntMax = -99.9;
float tempExtMin = 99.9;
float tempExtMax = -99.9;

// --- POLIA PRE HISTORIU GRAFOV ---
float historyInt[GRAPH_SIZE] = {0.0};
float historyExt[GRAPH_SIZE] = {0.0};
float historyLight[GRAPH_SIZE] = {0.0};
float historyWind[GRAPH_SIZE] = {0.0}; 
String historyTime[GRAPH_SIZE];
int graphCount = 0;
bool graphInitialized = false;
int lastLoggedMinute = -1;

// --- HARDVEROVE PINY NA DOSKE ESP32 ---
const int WIND_PIN = 35;
const int LIGHT_PIN = 34;
const int EXT_UP_CH1 = 36;
const int EXT_DOWN_CH1 = 39;
const int NTC_INT_PIN = 32;
const int NTC_EXT_PIN = 33;

// --- GLOBALNE POLIA PRE RIADENIE ZALUZII ---
bool webHoldHore[6] = {false, false, false, false, false, false};
bool webHoldDole[6] = {false, false, false, false, false, false};
bool allHore = false;
bool allDole = false;
unsigned long runUntil[6] = {0, 0, 0, 0, 0, 0};
int runDirection[6] = {0, 0, 0, 0, 0, 0};
int tUp[6] = {60, 60, 60, 60, 60, 20};
int tDown[6] = {60, 60, 60, 60, 60, 20};
int allTimeUp = 60;
int allTimeDown = 60;
int32_t blindPosition[6] = {0, 0, 0, 0, 0, 0};
int32_t zeroOffset[6] = {0, 0, 0, 0, 0, 0};
int lastDirection[6] = {0, 0, 0, 0, 0, 0};
unsigned long safeSwitchMillis[6] = {0, 0, 0, 0, 0, 0};

// --- PREMENNE PRE JEMNE NAKLAPANIE S DIODOU ---
bool isMicroAdjusting[6] = {false, false, false, false, false, false};
int microAdjustThreshold = 80;         
int32_t tiltPresets[6][8] = {0};             
int32_t targetPosition[6] = {0};
bool isTargetingPos[6] = {false, false, false, false, false, false};
int extTimeUpCH1 = 60;
int extTimeDownCH1 = 60;
bool extBtnAutoActive = false;

// --- NOVE STRUKTURY PRE JEMNE CASOVE NAKLAPANIE (8 PRESETOV X 6 ZALUZII) ---
bool useTimeTilt[6] = {false, false, false, false, false, false}; // true = Casova autom., false = Snimac SA5600
int timeTiltDir1[6][8];                                           // Krok 1 smer (1=HORE, 2=DOLE)
float timeTiltDur1[6][8];                                         // Krok 1 cas (s)
float timeTiltPause[6][8];                                        // Krok 2 pauza (s)
int timeTiltDir2[6][8];                                           // Krok 3 smer (1=HORE, 2=DOLE)
float timeTiltDur2[6][8];                                         // Krok 3 presny cas naklopenia (s)

int timeTiltPresetIndex[6] = {0, 0, 0, 0, 0, 0};                  // Aktualne beziaci preset (0-7 pre P1-P8)
int timeTiltState[6] = {0, 0, 0, 0, 0, 0};                        // Stav automatu (0=idle, 1=Krok 1, 2=Pauza, 3=Krok 3)
unsigned long timeTiltTimer[6] = {0, 0, 0, 0, 0, 0};              // Cas vyprsania aktivneho kroku

// --- POLIA PRE TEPLOTNA AUTOMATIKU ---
int tTempUp[6] = {15, 15, 15, 15, 15, 15};
int tTempDown[6] = {15, 15, 15, 15, 15, 15};
int tempMode[6] = {0, 0, 0, 0, 0, 0};
float tempLimitUp[6] = {22.0, 22.0, 22.0, 22.0, 22.0, 22.0};
float tempLimitDown[6] = {26.0, 26.0, 26.0, 26.0, 26.0, 26.0};
float tempHysteresis[6] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
bool tempActionTriggered[6] = {false, false, false, false, false, false};

// --- POLIA PRE SVETELNA AUTOMATIKU ---
int tLightUp[6] = {15, 15, 15, 15, 15, 15};
int tLightDown[6] = {15, 15, 15, 15, 15, 15};
int lightMode[6] = {0, 0, 0, 0, 0, 0};
int lightLimitUp[6] = {250, 250, 250, 250, 250, 250};
int lightLimitDown[6] = {750, 750, 750, 750, 750, 750};
int lightHysteresis[6] = {50, 50, 50, 50, 50, 50};
bool lightActionTriggered[6] = {false, false, false, false, false, false};

// --- POLIA PRE DVOJFAZOVU AUTOMATIKU ---
bool seqLightEnable[6] = {false, false, false, false, false, false};
float seqLightPauseTime[6] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
int seqLightSecondActionDir[6] = {1, 1, 1, 1, 1, 1};
float seqLightSecondActionTime[6] = {5.0, 5.0, 5.0, 5.0, 5.0, 5.0};

// --- POLIA PRE DVOJFAZOVU AUTOMATIKU PRE TEPLOTU ---
bool seqTempEnable[6] = {false, false, false, false, false, false};
float seqTempPauseTime[6] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
int seqTempSecondActionDir[6] = {1, 1, 1, 1, 1, 1};
float seqTempSecondActionTime[6] = {5.0, 5.0, 5.0, 5.0, 5.0, 5.0};

int seqLightState[6] = {0, 0, 0, 0, 0, 0};
unsigned long seqLightTimer[6] = {0, 0, 0, 0, 0, 0};
int seqTempState[6] = {0, 0, 0, 0, 0, 0};
unsigned long seqTempTimer[6] = {0, 0, 0, 0, 0, 0};

// --- AUTOMATIKY A CASOVE OKNA ---
bool autoTempEnable[6] = {true, true, true, true, true, true};
bool autoLightEnable[6] = {true, true, true, true, true, true};
int autoHourStart = 7;
int autoHourEnd = 21;

// --- DUALNY SYSTEM OSVETLENIA ---
bool lightState = false;             
unsigned long lightOffTarget = 0;    
int lightTimerMinutes = 0;
bool light2State = false;            
unsigned long light2OffTarget = 0;   
int light2TimerMinutes = 0;

// --- HARDVEROVE ASYNCHRONNE REGISTRE PRE KASKADU ---
unsigned long motorTriggerTime[6] = {0, 0, 0, 0, 0, 0};

// --- STAVOVE AUTOMATY TLACIDIEL ---
ButtonStateMachine btnUpSM = {HIGH, HIGH, 0, 0, 0, false};
ButtonStateMachine btnDownSM = {HIGH, HIGH, 0, 0, 0, false};

// --- EXPLICITNE DOPREDNE DEKLARACIE FUNKCII (PREVENCIA SCOPE ERROV) ---
void IRAM_ATTR countPulse();
String getClockTime();
void smtpCallback(SMTP_Status status);
void sendWindAlarmEmail(float currentWind, float currentLimit);
void checkAndSaveMinMax();
float readNTCRolling(int pin, float r0, float beta, float seriesR, int samples, float filterCoeff, float offset, float lastValidTemp, float* buffer, int& bufCount);
float readLDRRolling(int pin, int samples, float filterCoeff, float lastValidLDR, float* buffer, int& bufCount);
void initGraphsWithCurrentValues();
void addGraphRecord(float tI, float tE, float lL, float wS);
uint16_t readBlindPosition(int i);
void updateBlindPositions(); 
void flushRelays();
void saveAllConfigurationToNVS();
void loadNTCParams();
void loadConfiguration();
float safeVal(float val, float fallback = 0.0); 
String makeStatusJSON();
int32_t getEffectivePosition(int i);
void processButtonStateMachine(int rawState, ButtonStateMachine &sm, int &extMoveUp, int &extMoveDown);
void recoverI2CBus(int busNum);

// --- ASYNCHRONNE WEBOVE ROZHRANIE V PROGMEM ---

// --- ASYNCHRONNE WEBOVE ROZHRANIE V PROGMEM ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Ovladanie Terasy V84</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@2.9.4"></script>
<style>
body { font-family:Arial,sans-serif; background:#121212; color:#e0e0e0; margin:0; padding:10px; text-align:center; user-select:none; }
h1, h2, h3, h4 { color:#ffffff; margin:10px 0; }
.container { max-width:1100px; margin:0 auto; padding:5px; }
.card { background:#1e1e1e; border:1px solid #2d2d2d; border-radius:10px; padding:15px; margin:10px auto; box-shadow:0 4px 6px rgba(0,0,0,0.5); text-align:left; clear:both; }
.grid-sensors { display:grid; grid-template-columns:repeat(auto-fit, minmax(220px, 1fr)); gap:10px; }
.sensor-card { background:#252525; padding:15px; border-radius:8px; border:1px solid #333; text-align:center; position:relative; }
.stat { font-size:24px; font-weight:bold; color:#00adb5; display:block; margin:5px 0; }
.sub-stat { font-size:12px; color:#aaaaaa; margin-top:2px; }
.row { display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #2d2d2d; padding:8px 0; }
.row:last-child { border:none; }
.btn { padding:10px 18px; font-size:14px; margin:4px; border:none; border-radius:6px; cursor:pointer; font-weight:bold; color:#fff; transition: background 0.2s; }
.btn-up { background:#28a745; } .btn-up:hover { background:#218838; }
.btn-down { background:#007bff; } .btn-down:hover { background:#0069d9; }
.btn-stop { background:#dc3545; width:95%; margin:5px auto; padding:15px; font-size:18px; display:block; }
.btn-stop:hover { background:#c82333; }
.btn-all { background:#f39c12; width:45%; padding:12px; margin:4px; font-size:13px; }
.btn-all:hover { background:#d68910; }
.btn-all-time { background:#8e44ad; width:45%; padding:12px; margin:4px; font-size:13px; }
.btn-all-time:hover { background:#732d91; }
.btn-reset { background:#c0392b; padding:4px 8px; font-size:11px; min-width:auto; margin-left:10px; border-radius:4px; border:none; }
.btn-solo-main { padding: 18px; font-size: 16px; width: 45%; margin: 5px 2%; box-sizing: border-box; position:relative; }
.btn-timer { position: absolute; top: 2px; right: 5px; font-size: 10px; background: rgba(0,0,0,0.5); padding: 2px 4px; border-radius: 3px; color: #00ffff; font-weight: bold; display:none; }
.inp-w { width:70px; background:#2a2a2a; color:#fff; border:1px solid #444; padding:5px; border-radius:4px; text-align:center; }
.conf-grid { display:grid; grid-template-columns:40px 45px repeat(6, 1fr); gap:4px; text-align:center; align-items:center; font-size:11px; margin-bottom:5px; }
.conf-grid div { padding:3px 0; }
.conf-grid input, .conf-grid select { width:95%; font-size:11px; padding:3px 0; background:#2a2a2a; color:#fff; border:1px solid #444; border-radius:3px; text-align:center; }
.preset-grid { display:grid; grid-template-columns:40px repeat(8, 1fr); gap:4px; text-align:center; align-items:center; font-size:11px; margin-bottom:5px; }
.preset-grid input { width:95%; font-size:11px; padding:3px 0; background:#2a2a2a; color:#fff; border:1px solid #444; border-radius:3px; text-align:center; }
#alarm { background:#721c24; color:#f8d7da; border:2px solid #f5c6cb; padding:15px; border-radius:8px; font-weight:bold; font-size:18px; margin:15px auto; display:none; animation: blink 1s infinite; }
@keyframes blink { 0% { opacity: 1; } 50% { opacity: 0.4; } 100% { opacity: 1; } }
.saved-banner { position:fixed; top:20px; left:50%; transform:translateX(-50%); background:#28a745; color:white; padding:10px 20px; border-radius:5px; font-weight:bold; display:none; z-index:1000; }
.chart-container { position: relative; background:#1e1e1e; padding:15px; border-radius:8px; border:1px solid #2d2d2d; margin:20px auto; height:260px; width:100%; box-sizing: border-box; }
.btn-active-hold { background: #ff5500 !important; color: #fff !important; box-shadow: 0 0 10px #ff5500; }
.btn-active-hore { background: #155724 !important; color: #d4edda !important; box-shadow: 0 0 10px #28a745; }
.btn-active-dole { background: #1c3d5a !important; color: #cce5ff !important; box-shadow: 0 0 10px #007bff; }
.badge-light { font-size:11px; font-weight:bold; padding:2px 6px; border-radius:4px; display:inline-block; margin-top:4px; border:1px solid #333; }
.blind-row { border-bottom: 1px solid #333; padding: 15px 0; }
.blind-row:last-child { border: none; }
</style>
</head>
<body>
<div class="saved-banner" id="sb">Nastavenia ulozene v NVS!</div>
<div class="container">
  <h1>Ovladanie Terasy V84 - FIXED</h1>

  <div class="grid-sensors">
    <div class="sensor-card">
      <div>Teplota Vnutorna</div>
      <span class="stat"><span id="ti">0.0</span> C <button class="btn btn-reset" onclick="resetMinMax('int')">R</button></span>
      <div class="sub-stat">Min: <span id="timn">0.0</span> C | Max: <span id="timx">0.0</span> C</div>
    </div>
    <div class="sensor-card">
      <div>Teplota Vonkajsia</div>
      <span class="stat"><span id="te">0.0</span> C <button class="btn btn-reset" onclick="resetMinMax('ext')">R</button></span>
      <div class="sub-stat">Min: <span id="temn">0.0</span> C | Max: <span id="temx">0.0</span> C</div>
    </div>
    <div class="sensor-card">
      <div>Rychlost Vetra</div>
      <span class="stat"><span id="ws">0.0</span> km/h <button class="btn btn-reset" onclick="resetWindMax()">R</button></span>
      <div class="sub-stat">Max: <span id="wsmx">0.0</span> km/h | 24h: <span id="wsmx24">0.0</span> km/h</div>
    </div>
    <div class="sensor-card">
      <div>Senzor Svetla / Wi-Fi</div>
      <span class="stat"><span id="lt">0</span> / <span id="rssi">0</span> dBm</span>
      <div style="margin-top:5px;">
        <span id="btn-light-state" class="badge-light">S1: ???</span>
        <span id="btn-light2-state" class="badge-light" style="margin-left:5px;">S2: ???</span>
      </div>
    </div>
  </div>

  <div id="alarm">VETERNY POPLACH! ZALUZIE SU ZABLOKOVANE V HORNEJ POZICII!</div>

  <div class="card">
    <h3>Celkove ovladanie vsetkych zaluzii (150ms kaskada)</h3>
    <div style="display:flex; justify-content:space-around; flex-wrap:wrap;">
      <button class="btn btn-all-time" onclick="timeAll(1)">VSETKO HORE (Cas)</button>
      <button class="btn btn-all-time" onclick="timeAll(2)">VSETKO DOLE (Cas)</button>
      <button class="btn btn-all" id="btn-allhold-H" onmousedown="holdAll(1,1)" onmouseup="holdAll(1,0)" ontouchstart="holdAll(1,1)" ontouchend="holdAll(1,0)">VSETKO HORE (Hold)</button>
      <button class="btn btn-all" id="btn-allhold-D" onmousedown="holdAll(2,1)" onmouseup="holdAll(2,0)" ontouchstart="holdAll(2,1)" ontouchend="holdAll(2,0)">VSETKO DOLE (Hold)</button>
    </div>
    <button class="btn btn-stop" onclick="stopAll()">STOP VSETKO</button>
  </div>

  <div class="card">
    <h3>Samostatne ovladanie a predvolby naklapania</h3>
    <div id="solo-controls"></div>
  </div>

  <div class="card">
    <h3>Ovladanie Osvetlenia Terasy (Dualne Okruhy)</h3>
    <div class="row">
      <span><b>Svetlo 1: Hlavne</b> (Bit 7, PCF2)</span>
      <div>
        <button class="btn btn-up" onclick="toggleLight(1,1)">ZAPNUT</button>
        <button class="btn btn-down" onclick="toggleLight(1,0)">VYPNUT</button>
      </div>
    </div>
    <div class="row">
      <span>Casovac Svetla 1:</span>
      <div>
        <input type="number" id="inp-light-timer" class="inp-w" min="0" max="240" placeholder="Min">
        <button class="btn btn-all-time" onclick="setLightTimer(1)">Spustit a Zapnut</button>
      </div>
    </div>
    <hr style="border:0; border-top:1px dashed #333; margin:15px 0;">
    <div class="row">
      <span><b>Svetlo 2: Pomocne</b> (Bit 7, PCF1)</span>
      <div>
        <button class="btn btn-up" onclick="toggleLight(2,1)">ZAPNUT</button>
        <button class="btn btn-down" onclick="toggleLight(2,0)">VYPNUT</button>
      </div>
    </div>
    <div class="row">
      <span>Casovac Svetla 2:</span>
      <div>
        <input type="number" id="inp-light2-timer" class="inp-w" min="0" max="240" placeholder="Min">
        <button class="btn btn-all-time" onclick="setLightTimer(2)">Spustit a Zapnut</button>
      </div>
    </div>
  </div>

  <div class="card">
    <h3>Grafy Historia Snimacov</h3>
    <div class="chart-container"><canvas id="tempChart"></canvas></div>
    <div class="chart-container"><canvas id="lightChart"></canvas></div>
    <div class="chart-container"><canvas id="windChart"></canvas></div>
  </div>

  <div class="card" style="overflow-x:auto;">
    <h3>Konfiguracia automatiky a poloh zaluzii</h3>
    <div class="row">
      <span>Globalne povolenie automatiky:</span>
      <select id="gaute" onchange="saveGlobalAuto()" class="inp-w" style="width:100px;">
        <option value="1">Povolena</option>
        <option value="0">Zakazana</option>
      </select>
    </div>
    
    <div class="row" style="margin-top:8px;">
      <span>Casove okno pre fungovanie automatiky (Hodiny):</span>
      <div>
        Od: <input type="number" id="hstart" class="inp-w" min="0" max="23" onchange="saveGlobalAuto()" style="width:50px;">
        Do: <input type="number" id="hend" class="inp-w" min="0" max="23" onchange="saveGlobalAuto()" style="width:50px;">
      </div>
    </div>
    
    <h4>Casy uplneho chodu (sekundy)</h4>
    <div id="times-inputs"></div>

    <h4>Kalibracia vychodzieho bodu 0 (Zero Offset)</h4>
    <div id="zero-offset-inputs"></div>

    <h4>Preddefinovane polohy naklapania (Rozsah 0 - 130000)</h4>
    <div id="presets-autom"></div>
    
    <h4>Teplotna automatika</h4>
    <div id="temp-autom"></div>

    <h4>Svetelna automatika</h4>
    <div id="light-autom"></div>

    <h4>Dvojfazova automatika pre Svetlo</h4>
    <div id="seq-light-autom"></div>

    <h4>Dvojfazova automatika pre Teplotu</h4>
    <div id="seq-temp-autom"></div>

    <h4>Casova automatika naklapania polohy (P1 - P8)</h4>
    <div id="time-tilt-autom"></div>

    <h3>Kalibracia a filtre systemu</h3>
    <div class="row"><span>Tolerancia priblizenia k cielu:</span><span><input type="number" id="mcth" class="inp-w" onchange="saveCalib()"> bodov</span></div>

    <h4>Nastavenie vonkajsieho tlacidla (Kanal 1)</h4>
    <div class="row"><span>Trvanie chodu HORE (tlacidlo):</span><span><input type="number" id="etu1" class="inp-w" onchange="saveCalib()"> s</span></div>
    <div class="row"><span>Trvanie chodu DOLE (tlacidlo):</span><span><input type="number" id="etd1" class="inp-w" onchange="saveCalib()"> s</span></div>

    <h4>Anemometer (Vietor)</h4>
    <div class="row"><span>Limit vetra pre poplach:</span><span><input type="number" id="wlim" class="inp-w" onchange="saveCalib()"> km/h</span></div>
    <div class="row"><span>Impulzy pre 25 km/h:</span><span><input type="number" id="wp25" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Pocet impulzov na otacku:</span><span><input type="number" id="ppturn" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Trvanie blokovania poplachu:</span><span><input type="number" id="wsafedur" class="inp-w" onchange="saveCalib()"> s</span></div>
    <div class="row"><span>Cas chodu HORE po vetre:</span><span><input type="number" id="wruntime" class="inp-w" onchange="saveCalib()"> s</span></div>
    <div class="row"><span>Pocet kritickych pulzov:</span><span><input type="number" id="wmaxhits" class="inp-w" onchange="saveCalib()"></span></div>

    <h4>Filter Svetla (LDR)</h4>
    <div class="row"><span>Okno klzaveho filtra LDR:</span><span><input type="number" id="ldrsmp" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Koeficient filtra LDR:</span><span><input type="number" step="0.01" id="ldrflt" class="inp-w" onchange="saveCalib()"></span></div>

    <h4>Filter a kalibracia NTC - Vnutorny</h4>
    <div class="row"><span>NTC Odpor pri 25C (R0):</span><span><input type="number" id="ntciR0" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>NTC Beta koeficient:</span><span><input type="number" id="ntciB" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Seriovy odpor:</span><span><input type="number" id="ntciSR" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>Okno klzaveho filtra:</span><span><input type="number" id="ntciSmp" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Koeficient filtra:</span><span><input type="number" step="0.01" id="ntciFlt" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Teplotny offset:</span><span><input type="number" step="0.1" id="ntciOff" class="inp-w" onchange="saveCalib()"> C</span></div>

    <h4>Filter a kalibracia NTC - Vonkajsi</h4>
    <div class="row"><span>NTC Odpor pri 25C (R0):</span><span><input type="number" id="ntceR0" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>NTC Beta koeficient:</span><span><input type="number" id="ntceB" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Seriovy odpor:</span><span><input type="number" id="ntceSR" class="inp-w" onchange="saveCalib()"> Ohm</span></div>
    <div class="row"><span>Okno klzaveho filtra:</span><span><input type="number" id="ntceSmp" class="inp-w" onchange="saveCalib()"></span></div>
    <div class="row"><span>Koeficient filtra:</span><span><input type="number" step="0.01" id="ntceFlt" class="inp-w" onchange="saveCalib()"> C</span></div>
    <div class="row"><span>Teplotny offset:</span><span><input type="number" step="0.1" id="ntceOff" class="inp-w" onchange="saveCalib()"> C</span></div>
  </div>
</div>

<script>
const chartColors = { text: '#ffffff', grid: 'rgba(255, 255, 255, 0.06)', intTemp: '#ff4d4d', extTemp: '#33ccff', light: '#ffcc00', wind: '#00adb5' };
const showSaved = () => { const b = document.getElementById("sb"); b.style.display = "block"; setTimeout(() => { b.style.display="none"; }, 2000); };

let lockoutCounter = 0;
const toggleLight = (id, val) => { fetch('/light?id=' + id + '&s=' + val); };
const setLightTimer = (id) => { 
  let m = (id === 1) ? document.getElementById("inp-light-timer").value : document.getElementById("inp-light2-timer").value;
  fetch('/lighttimer?id=' + id + '&m=' + m); 
};
const triggerSeq = (i, type) => { fetch('/seqtrigger?i=' + i + '&t=' + type).then(showSaved); };
const holdSolo = (i, d, val) => { fetch('/hold?i=' + i + '&d=' + d + '&v=' + val); };
const timeSolo = (i, d) => { fetch('/time?i=' + i + '&d=' + d); };
const setPreset = (i, p) => { fetch('/setpreset?i=' + i + '&p=' + p); };
const holdAll = (d, val) => { fetch('/holdall?d=' + d + '&v=' + val); };
const timeAll = (d) => { fetch('/timeall?d=' + d); };
const stopAll = () => { fetch('/stop'); };
const resetMinMax = (type) => { fetch('/resetminmax?type=' + type); };
const resetWindMax = () => { fetch('/resetwindmax'); };
const zeroBlind = (i) => { if(confirm("Naozaj resetovat vychodzi bod zaluzie "+(i+1)+" na 0?")) fetch('/zero?i='+i).then(showSaved); };
const saveGlobalAuto = () => { lockoutCounter = 5; fetch('/saveglobalauto?val=' + document.getElementById("gaute").value + '&hstart=' + document.getElementById("hstart").value + '&hend=' + document.getElementById("hend").value).then(showSaved); };

const saveRow = (i) => {
  lockoutCounter = 5;
  let url = '/saverow?i='+i+'&tu='+document.getElementById("tu"+i).value+'&td='+document.getElementById("td"+i).value+'&zOff='+document.getElementById("zOff"+i).value;
  url += '&ate='+(document.getElementById("ate"+i).checked?1:0)+'&tmd='+document.getElementById("tmd"+i).value+'&tlu='+document.getElementById("tlu"+i).value+'&tta='+document.getElementById("tta"+i).value+'&tld='+document.getElementById("tld"+i).value+'&ttd='+document.getElementById("ttd"+i).value+'&thyst='+document.getElementById("thyst"+i).value;
  url += '&ale='+(document.getElementById("ale"+i).checked?1:0)+'&lmd='+document.getElementById("lmd"+i).value+'&llu='+document.getElementById("llu"+i).value+'&lta='+document.getElementById("lta"+i).value+'&lld='+document.getElementById("lld"+i).value+'&ltd='+document.getElementById("ltd"+i).value+'&lhyst='+document.getElementById("lhyst"+i).value;
  url += '&sqle='+(document.getElementById("sqle"+i).checked?1:0)+'&sqlp='+document.getElementById("sqlp"+i).value+'&sqld='+document.getElementById("sqld"+i).value+'&sqlt='+document.getElementById("sqlt"+i).value;
  url += '&sqte='+(document.getElementById("sqte"+i).checked?1:0)+'&sqtp='+document.getElementById("sqtp"+i).value+'&sqtd='+document.getElementById("sqtd"+i).value+'&sqtt='+document.getElementById("sqtt"+i).value;
  for(let p=0; p<8; p++) {
    url += '&p'+p+'='+document.getElementById('prst-'+i+'-'+p).value;
  }
  fetch(url).then(showSaved);
};

const saveCalib = () => {
  lockoutCounter = 5; 
  let url = '/savecalib?wlim='+document.getElementById("wlim").value+'&wp25='+document.getElementById("wp25").value+'&ppturn='+document.getElementById("ppturn").value+'&wsafedur='+document.getElementById("wsafedur").value+'&wruntime='+document.getElementById("wruntime").value+'&wmaxhits='+document.getElementById("wmaxhits").value+'&mcth='+document.getElementById("mcth").value+'&etu1='+document.getElementById("etu1").value+'&etd1='+document.getElementById("etd1").value;
  url += '&ldrsmp='+document.getElementById("ldrsmp").value+'&ldrflt='+document.getElementById("ldrflt").value;
  url += '&ntciR0='+document.getElementById("ntciR0").value+'&ntciB='+document.getElementById("ntciB").value+'&ntciSR='+document.getElementById("ntciSR").value+'&ntciSmp='+document.getElementById("ntciSmp").value+'&ntciFlt='+document.getElementById("ntciFlt").value+'&ntciOff='+document.getElementById("ntciOff").value;
  url += '&ntceR0='+document.getElementById("ntceR0").value+'&ntceB='+document.getElementById("ntceB").value+'&ntceSR='+document.getElementById("ntceSR").value+'&ntceSmp='+document.getElementById("ntceSmp").value+'&ntceFlt='+document.getElementById("ntceFlt").value+'&ntceOff='+document.getElementById("ntceOff").value;
  fetch(url).then(showSaved);
};

const buildUI = () => {
  let solosHtml = "";
  for(let i=0; i<6; i++) {
    solosHtml += `
    <div class="blind-row">
      <strong>Zaluzia ${i+1}</strong> (Poloha: <span id="pos-val-${i}" style="font-weight:bold; color:#ffffff;">0</span>)
      <span id="stat-auto-${i}" style="margin-left:12px; font-size:11px; font-weight:bold; background:#111111; color:#f1c40f; padding:2px 6px; border-radius:3px; border:1px solid #333;">--</span>
      <div style="margin-top:5px; display:flex; justify-content:space-between; flex-wrap:wrap;">
        <button class="btn btn-up btn-solo-main" id="btn-time-H-${i}" onclick="timeSolo(${i},1)">Z${i+1} HORE<span class="btn-timer" id="timer-TH-${i}"></span></button>
        <button class="btn btn-down btn-solo-main" id="btn-time-D-${i}" onclick="timeSolo(${i},2)">Z${i+1} DOLE<span class="btn-timer" id="timer-TD-${i}"></span></button>
        <button class="btn btn-up btn-solo-main" style="background:#1e7e34;" id="btn-hold-H-${i}" onmousedown="holdSolo(${i},1,1)" onmouseup="holdSolo(${i},1,0)" ontouchstart="holdSolo(${i},1,1)" ontouchend="holdSolo(${i},1,0)">Z${i+1} HORE (Hold)</button>
        <button class="btn btn-down btn-solo-main" style="background:#0062cc;" id="btn-hold-D-${i}" onmousedown="holdSolo(${i},2,1)" onmouseup="holdSolo(${i},2,0)" ontouchstart="holdSolo(${i},2,1)" ontouchend="holdSolo(${i},2,0)">Z${i+1} DOLE (Hold)</button>
      </div>
      <div style="margin-top:5px; display:flex; justify-content:space-between; flex-wrap:wrap; width:100%;">
        <span style="font-size:11px; align-self:center; margin-right:5px;">Naklopenie:</span>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},0)">P1</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},1)">P2</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},2)">P3</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},3)">P4</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},4)">P5</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},5)">P6</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},6)">P7</button>
        <button class="btn" style="padding:5px 8px; font-size:11px; flex:1; margin:2px; background:#34495e;" onclick="setPreset(${i},7)">P8</button>
      </div>
    </div>`;
  }
  document.getElementById("solo-controls").innerHTML = solosHtml;

  let timesHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Cas HORE</div><div>Cas DOLE</div><div>-</div><div>-</div><div>-</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    timesHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="number" id="tu${i}" onchange="saveRow(${i})"></div><div><input type="number" id="td${i}" onchange="saveRow(${i})"></div><div></div><div></div><div></div><div></div></div>`;
  }
  document.getElementById("times-inputs").innerHTML = timesHtml;

  let zeroHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Hodnota 0</div><div>Servisny Nulovaci Povely</div><div>-</div><div>-</div><div>-</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    zeroHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="number" id="zOff${i}" onchange="saveRow(${i})"></div><div><button class="btn" style="background:#d35400; font-size:10px; padding:4px 8px; flex:1;" onclick="zeroBlind(${i})">Nulovac</button></div><div></div><div></div><div></div><div></div></div>`;
  }
  document.getElementById("zero-offset-inputs").innerHTML = zeroHtml;

  let presetHtml = '<div class="preset-grid" style="font-weight:bold;"><div>Zal.</div><div>P1</div><div>P2</div><div>P3</div><div>P4</div><div>P5</div><div>P6</div><div>P7</div><div>P8</div></div>';
  for(let i=0; i<6; i++) {
    presetHtml += `<div class="preset-grid"><div>Z${i+1}</div>`;
    for(let p=0; p<8; p++) {
      presetHtml += `<div><input type="number" id="prst-${i}-${p}" onchange="saveRow(${i})"></div>`;
    }
    presetHtml += `</div>`;
  }
  document.getElementById("presets-autom").innerHTML = presetHtml;

  let tempHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Mod</div><div>Lim.HORE</div><div>Cas HORE</div><div>Lim.DOLE</div><div>Cas DOLE</div><div>Hyst</div></div>';
  for(let i=0; i<6; i++) {
    tempHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="ate${i}" onchange="saveRow(${i})" style="width:18px;height:18px;"></div><div><select id="tmd${i}" onchange="saveRow(${i})"><option value="0">OFF</option><option value="1">Vnitra</option><option value="2">Vonka</option></select></div><div><input type="number" step="0.1" id="tlu${i}" onchange="saveRow(${i})"></div><div><input type="number" id="tta${i}" onchange="saveRow(${i})"></div><div><input type="number" step="0.1" id="tld${i}" onchange="saveRow(${i})"></div><div><input type="number" id="ttd${i}" onchange="saveRow(${i})"></div><div><input type="number" step="0.1" id="thyst${i}" onchange="saveRow(${i})"></div></div>`;
  }
  document.getElementById("temp-autom").innerHTML = tempHtml;

  let lightHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Mod</div><div>Lim.HORE</div><div>Cas HORE</div><div>Lim.DOLE</div><div>Cas DOLE</div><div>Hyst</div></div>';
  for(let i=0; i<6; i++) {
    lightHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="ale${i}" onchange="saveRow(${i})" style="width:18px;height:18px;"></div><div><select id="lmd${i}" onchange="saveRow(${i})"><option value="0">OFF</option><option value="1">Automatika</option></select></div><div><input type="number" id="llu${i}" onchange="saveRow(${i})"></div><div><input type="number" id="lta${i}" onchange="saveRow(${i})"></div><div><input type="number" id="lld${i}" onchange="saveRow(${i})"></div><div><input type="number" id="ltd${i}" onchange="saveRow(${i})"></div><div><input type="number" id="lhyst${i}" onchange="saveRow(${i})"></div></div>`;
  }
  document.getElementById("light-autom").innerHTML = lightHtml;

  let seqLightHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Pauza</div><div>Smer2</div><div>Cas2</div><div>Manual</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    seqLightHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="sqle${i}" onchange="saveRow(${i})" style="width:20px;height:20px;"></div><div><input type="number" step="0.1" id="sqlp${i}" onchange="saveRow(${i})"></div><div><select id="sqld${i}" onchange="saveRow(${i})"><option value="1">HORE</option><option value="2">DOLE</option></select></div><div><input type="number" step="0.1" id="sqlt${i}" onchange="saveRow(${i})"></div><div><button class="btn" style="background:#555; font-size:10px; padding:4px 6px;" onclick="triggerSeq(${i},1)">Trigger</button></div><div>-</div></div>`;
  }
  document.getElementById("seq-light-autom").innerHTML = seqLightHtml;

  let seqTempHtml = '<div class="conf-grid" style="font-weight:bold;"><div>Zal.</div><div>Zap</div><div>Pauza</div><div>Smer2</div><div>Cas2</div><div>Manual</div><div>-</div></div>';
  for(let i=0; i<6; i++) {
    seqTempHtml += `<div class="conf-grid"><div>Z${i+1}</div><div><input type="checkbox" id="sqte${i}" onchange="saveRow(${i})" style="width:20px;height:20px;"></div><div><input type="number" step="0.1" id="sqtp${i}" onchange="saveRow(${i})"></div><div><select id="sqtd${i}" onchange="saveRow(${i})"><option value="1">HORE</option><option value="2">DOLE</option></select></div><div><input type="number" step="0.1" id="sqtt${i}" onchange="saveRow(${i})"></div><div><button class="btn" style="background:#555; font-size:10px; padding:4px 6px;" onclick="triggerSeq(${i},2)">Trigger</button></div><div>-</div></div>`;
  }
  document.getElementById("seq-temp-autom").innerHTML = seqTempHtml;

  let timeTiltHtml = `
  <div class="row">
    <span>Vyber roletu:</span>
    <select id="sel-tt-blind" class="inp-w" onchange="loadTimeTiltFields()" style="width:110px;">
      <option value="0">Zaluzia 1</option>
      <option value="1">Zaluzia 2</option>
      <option value="2">Zaluzia 3</option>
      <option value="3">Zaluzia 4</option>
      <option value="4">Zaluzia 5</option>
      <option value="5">Zaluzia 6</option>
    </select>
  </div>
  <div class="row">
    <span>Pouzit časovú automatiku naklápania pre tuto roletu:</span>
    <input type="checkbox" id="utt_input" onchange="saveTimeTiltRow()" style="width:22px;height:22px;">
  </div>
  <hr style="border:0; border-top:1px dashed #333; margin:10px 0;">
  <div class="row">
    <span>Vyber predvoľbu (P1 - P8) pre nastavenie časov:</span>
    <select id="sel-tt-preset" class="inp-w" onchange="loadTimeTiltFields()" style="width:110px;">
      <option value="0">P1</option>
      <option value="1">P2</option>
      <option value="2">P3</option>
      <option value="3">P4</option>
      <option value="4">P5</option>
      <option value="5">P6</option>
      <option value="6">P7</option>
      <option value="7">P8</option>
    </select>
  </div>
  <div class="conf-grid" style="font-weight:bold; grid-template-columns: repeat(5, 1fr); margin-top:10px;">
    <div>Smer 1</div><div>Cas 1 (s)</div><div>Pauza (s)</div><div>Smer 2</div><div>Cas 3 (s)</div>
  </div>
  <div class="conf-grid" style="grid-template-columns: repeat(5, 1fr);">
    <div><select id="ttd1_input" onchange="saveTimeTiltRow()"><option value="1">HORE</option><option value="2">DOLE</option></select></div>
    <div><input type="number" step="0.05" id="ttt1_input" onchange="saveTimeTiltRow()"></div>
    <div><input type="number" step="0.05" id="ttp_input" onchange="saveTimeTiltRow()"></div>
    <div><select id="ttd2_input" onchange="saveTimeTiltRow()"><option value="1">HORE</option><option value="2">DOLE</option></select></div>
    <div><input type="number" step="0.05" id="ttt2_input" onchange="saveTimeTiltRow()"></div>
  </div>`;
  document.getElementById("time-tilt-autom").innerHTML = timeTiltHtml;
};

let tChart, lChart, wChart;
const initCharts = (labels, dataInt, dataExt, dataLight, dataWind) => {
  const commonOptions = { responsive: true, maintainAspectRatio: false, scales: { xAxes: [{ gridLines: { color: chartColors.grid }, ticks: { fontColor: chartColors.text } }], yAxes: [{ gridLines: { color: chartColors.grid }, ticks: { fontColor: chartColors.text } }] }, legend: { labels: { fontColor: chartColors.text } } };
  tChart = new Chart(document.getElementById('tempChart').getContext('2d'), { type: 'line', data: { labels: labels, datasets: [{ label: 'Vnutorna Teplota (C)', data: dataInt, borderColor: chartColors.intTemp, backgroundColor: 'rgba(255,77,77,0.1)', borderWidth: 2, fill: false }, { label: 'Vonkajsia Teplota (C)', data: dataExt, borderColor: chartColors.extTemp, backgroundColor: 'rgba(51,204,255,0.1)', borderWidth: 2, fill: false }] }, options: commonOptions });
  lChart = new Chart(document.getElementById('lightChart').getContext('2d'), { type: 'line', data: { labels: labels, datasets: [{ label: 'Intenzita svetla (0-1000)', data: dataLight, borderColor: chartColors.light, backgroundColor: 'rgba(255,204,0,0.1)', borderWidth: 2, fill: false }] }, options: commonOptions });
  wChart = new Chart(document.getElementById('windChart').getContext('2d'), { type: 'line', data: { labels: labels, datasets: [{ label: 'Rychlost vetra (km/h)', data: dataWind, borderColor: chartColors.wind, backgroundColor: 'rgba(0,173,181,0.1)', borderWidth: 2, fill: false }] }, options: commonOptions });
};

const updateCharts = (labels, dataInt, dataExt, dataLight, dataWind) => {
  if(!tChart) { initCharts(labels, dataInt, dataExt, dataLight, dataWind); return; }
  tChart.data.labels = labels; tChart.data.datasets[0].data = dataInt; tChart.data.datasets[1].data = dataExt; tChart.update();
  lChart.data.labels = labels; lChart.data.datasets[0].data = dataLight; lChart.update();
  wChart.data.labels = labels; wChart.data.datasets[0].data = dataWind; wChart.update();
};

let global_tt_data = { utt: [], ttd1: [], ttt1: [], ttp: [], ttd2: [], ttt2: [] };
const loadTimeTiltFields = () => {
  if (!global_tt_data.utt || global_tt_data.utt.length === 0) return;
  let i = parseInt(document.getElementById("sel-tt-blind").value);
  let p = parseInt(document.getElementById("sel-tt-preset").value);
  
  document.getElementById("utt_input").checked = (global_tt_data.utt[i] == "1");
  document.getElementById("ttd1_input").value = global_tt_data.ttd1[i][p];
  document.getElementById("ttt1_input").value = global_tt_data.ttt1[i][p].toFixed(2);
  document.getElementById("ttp_input").value = global_tt_data.ttp[i][p].toFixed(2);
  document.getElementById("ttd2_input").value = global_tt_data.ttd2[i][p];
  document.getElementById("ttt2_input").value = global_tt_data.ttt2[i][p].toFixed(2);
};

const saveTimeTiltRow = () => {
  lockoutCounter = 5;
  let i = document.getElementById("sel-tt-blind").value;
  let p = document.getElementById("sel-tt-preset").value;
  let utt = document.getElementById("utt_input").checked ? 1 : 0;
  let d1 = document.getElementById("ttd1_input").value;
  let t1 = document.getElementById("ttt1_input").value;
  let pz = document.getElementById("ttp_input").value;
  let d2 = document.getElementById("ttd2_input").value;
  let t2 = document.getElementById("ttt2_input").value;

  global_tt_data.utt[i] = utt.toString();
  global_tt_data.ttd1[i][p] = parseInt(d1);
  global_tt_data.ttt1[i][p] = parseFloat(t1);
  global_tt_data.ttp[i][p] = parseFloat(pz);
  global_tt_data.ttd2[i][p] = parseInt(d2);
  global_tt_data.ttt2[i][p] = parseFloat(t2);

  let url = `/savetimetilt?i=${i}&p=${p}&utt=${utt}&d1=${d1}&t1=${t1}&pz=${pz}&d2=${d2}&t2=${t2}`;
  fetch(url).then(showSaved);
};

const updateStatus = () => {
  const ae = document.activeElement;
  if (ae && (ae.tagName === "INPUT" || ae.tagName === "SELECT")) return;
  fetch('/status').then(r => r.json()).then(c => {
    const txt = (id, val) => { const el = document.getElementById(id); if (el) el.innerText = val; };
    const val = (id, val) => { const el = document.getElementById(id); if (el) el.value = val; };
    const chk = (id, val) => { const el = document.getElementById(id); if (el) el.checked = val; };

    txt("ti", c.ti.toFixed(1)); txt("te", c.te.toFixed(1));
    txt("timn", c.timn.toFixed(1)); txt("timx", c.timx.toFixed(1));
    txt("temn", c.temn.toFixed(1)); txt("temx", c.temx.toFixed(1));
    txt("ws", c.ws.toFixed(1)); txt("wsmx", c.wsmx.toFixed(1)); txt("wsmx24", c.wsmx24.toFixed(1));
    txt("lt", c.lt); txt("rssi", c.rssi); txt("clock", c.clk);
    
    const s1 = document.getElementById("btn-light-state"); if(s1) { s1.innerText = c.light ? "S1: ZAPNUTE" : "S1: VYPNUTE"; s1.style.background = c.light ? "#d35400" : "#2c3e50"; if(c.lrem > 0) s1.innerText += " (" + c.lrem + "m)"; }
    const s2 = document.getElementById("btn-light2-state"); if(s2) { s2.innerText = c.l2 ? "S2: ZAPNUTE" : "S2: VYPNUTE"; s2.style.background = c.l2 ? "#d35400" : "#2c3e50"; if(c.l2rem > 0) s2.innerText += " (" + c.l2rem + "m)"; }

    global_tt_data.utt = c.utt;
    global_tt_data.ttd1 = c.ttd1;
    global_tt_data.ttt1 = c.ttt1;
    global_tt_data.ttp = c.ttp;
    global_tt_data.ttd2 = c.ttd2;
    global_tt_data.ttt2 = c.ttt2;
    
    if (document.getElementById("sel-tt-blind")) { loadTimeTiltFields(); }
    if (lockoutCounter > 0) { lockoutCounter--; return; }
    
    val("gaute", c.gaute); val("hstart", c.hst); val("hend", c.hnd);
    const alarmEl = document.getElementById("alarm"); if (alarmEl) alarmEl.style.display = (c.safety == "1") ? "block" : "none";
    
    for(let i=0; i<6; i++) {
      val("tu"+i, c.tu[i]); val("td"+i, c.td[i]); val("zOff"+i, c.zOff[i]);
      chk("ate"+i, c.ate[i] == "1"); val("tmd"+i, c.tmd[i]);
      val("tlu"+i, c.tlu[i]); val("tta"+i, c.tta[i]); val("tld"+i, c.tld[i]);
      val("ttd"+i, c.ttd[i]); val("thyst"+i, c.thyst[i]);
      chk("ale"+i, c.ale[i] == "1"); val("lmd"+i, c.lmd[i]); val("llu"+i, c.llu[i]);
      val("lta"+i, c.lta[i]); val("lld"+i, c.lld[i]); val("ltd"+i, c.ltd[i]); val("lhyst"+i, c.lhyst[i]);
      chk("sqle"+i, c.sqle[i] == "1"); val("sqlp"+i, c.sqlp[i]); val("sqld"+i, c.sqld[i]); val("sqlt"+i, c.sqlt[i]);
      chk("sqte"+i, c.sqte[i] == "1"); val("sqtp"+i, c.sqtp[i]); val("sqtd"+i, c.sqtd[i]); val("sqtt"+i, c.sqtt[i]);

      for(let p=0; p<8; p++) { val('prst-'+i+'-'+p, c.prst[i][p]); }
      
      txt("pos-val-"+i, c.pos[i]);
      let bhH = document.getElementById("btn-hold-H-"+i);
      let bhD = document.getElementById("btn-hold-D-"+i);
      let btH = document.getElementById("btn-time-H-"+i); let btD = document.getElementById("btn-time-D-"+i);
      let timTH = document.getElementById("timer-TH-"+i); let timTD = document.getElementById("timer-TD-"+i);
      if(bhH) bhH.classList.remove("btn-active-hold"); if(bhD) bhD.classList.remove("btn-active-hold");
      if(btH) { btH.classList.remove("btn-active-hore"); if(timTH) timTH.style.display="none"; }
      if(btD) { btD.classList.remove("btn-active-dole"); if(timTD) timTD.style.display="none"; }
      
      if (c.rDir[i] == "1") { if (c.whH[i] == "1") { if(bhH) bhH.classList.add("btn-active-hold"); } else { if(btH) { btH.classList.add("btn-active-hore"); if(timTH) { timTH.style.display="inline-block"; timTH.innerText = c.rem[i] + "s"; } } } }
      else if (c.rDir[i] == "2") { if (c.whD[i] == "1") { if(bhD) bhD.classList.add("btn-active-hold"); } else { if(btD) { btD.classList.add("btn-active-dole"); if(timTD) { timTD.style.display="inline-block"; timTD.innerText = c.rem[i] + "s"; } } } }
      
      let stA = document.getElementById("stat-auto-"+i);
      if(stA) {
        if(c.as[i] == 1) stA.innerHTML = '<span style="color:#2ecc71;">Teplota</span>';
        else if(c.as[i] == 2) stA.innerHTML = '<span style="color:#2ecc71;">Svetlo</span>';
        else if(c.as[i] == 3) stA.innerHTML = '<span style="color:#f39c12;">Pauza 2.F</span>';
        else if(c.as[i] == 4) stA.innerHTML = '<span style="color:#2ecc71;">2.Faza</span>';
        else stA.innerHTML = '<span style="color:#7f8c8d;">OFF</span>';
      }
    }
    if(firstLoad) { 
      val("mcth", c.mcth); val("wlim", c.wlim); val("wp25", c.wp25); val("ppturn", c.ppturn); val("wsafedur", Math.round(c.wsafedur/1000)); val("wruntime", c.wruntime); val("wmaxhits", c.wmaxhits);
      val("etu1", c.etu1); val("etd1", c.etd1); val("ldrsmp", c.ldrs); val("ldrflt", c.ldrf);
      val("ntciR0", c.ntciR0); val("ntciB", c.ntciB); val("ntciSR", c.ntciSR); val("ntciSmp", c.ntciSmp); val("ntciFlt", c.ntciFlt); val("ntciOff", c.ntciOff);
      val("ntceR0", c.ntceR0); val("ntceB", c.ntceB); val("ntceSR", c.ntceSR); val("ntceSmp", c.ntceSmp); val("ntceFlt", c.ntceFlt); val("ntceOff", c.ntceOff);
      firstLoad = false;
    }
    if(c.labels && c.labels.length > 0) { updateCharts(c.labels, c.histInt, c.gExt, c.histLight, c.histWind); }
  });
};
let firstLoad = true; 
window.onload = () => { buildUI(); updateStatus(); setInterval(updateStatus, 1500); };
</script>
</body>
</html>
)rawliteral";

// --- Podporne a meracie funkcie, Rolling filtre, Multi-turn SA5600 a Preferences NVS ---
void IRAM_ATTR countPulse() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > 15) { 
    pulseCount++;
  }
  lastInterruptTime = interruptTime;
}

String getClockTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String("--:--");
  }
  char timeStringBuff[10];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  return String(timeStringBuff);
}

void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());
}

void sendWindAlarmEmail(float currentWind, float currentLimit) {
  Session_Config config;
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  config.login.user_domain = "";

  SMTP_Message message;
  message.sender.name = "Smart Terasa Zaluzie";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "=?UTF-8?B?4pyoIEFMQVJNOiDFvWEx+nppZSB2eXRpYWhudXTDqSAtIFNpbG7DvSB2aWV0b3Ih?=";
  message.addRecipient("Pouzivatel", RECIPIENT_EMAIL);

  String htmlMsg = "<div style='font-family:Arial,sans-serif;border:2px solid #ff4444;padding:15px;border-radius:8px;background-color:#1e1e1e;color:#e0e0e0;'>";
  htmlMsg += "<h2 style='color:#ff4444;margin-top:0;'>ALARM: VETERNY POPLACH - AKTIVACIA OCHRANY</h2>";
  htmlMsg += "<p>Automaticky system ochrany detegoval nebezpecne zvysenie rychlosti vetra.</p>";
  htmlMsg += "<h3>METEO A SENSORICKA DIAGNOSTIKA:</h3><ul>";
  htmlMsg += "<li><b>Namerana rychlost vetra:</b> <span style='color:#ff4444;font-weight:bold;'>" + String(currentWind, 1) + " km/h</span></li>";
  htmlMsg += "<li><b>Nastaveny limit ochrany:</b> " + String(currentLimit, 1) + " km/h</li>";
  htmlMsg += "<li><b>Cas incidentu v systeme:</b> " + getClockTime() + "</li>";
  htmlMsg += "<li><b>Vnutorna teplota terasy:</b> " + String(tempInt, 1) + " C</li>";
  htmlMsg += "<li><b>Vonkajsia teplota prostredia:</b> " + String(tempExt, 1) + " C</li>";
  htmlMsg += "<li><b>Senzor jasu (LDR):</b> " + String(lightLevel) + " bodov</li>";
  htmlMsg += "<li><b>Sila Wi-Fi signalu (RSSI):</b> " + String(wifiRSSI) + " dBm</li>";
  htmlMsg += "</ul>";
  htmlMsg += "<h3>POLOHY ROLET V MOMENTE INCIDENTU:</h3><ul>";
  for(int i=0; i<6; i++) {
    htmlMsg += "<li><b>Zaluzia " + String(i+1) + ":</b> " + String(getEffectivePosition(i)) + " bodov (Zero Offset: " + String(zeroOffset[i]) + ")</li>";
  }
  htmlMsg += "</ul>";
  htmlMsg += "<p style='background:#721c24;padding:12px;border-radius:4px;color:#f8d7da;font-weight:bold;'>STAV AKCIE: Vsetkych 6 zaluzii bolo kaskadovito vytiahnutych do HORNEJ pozicie (0). Ovladani blokavane na 60 sekund.</p>";
  htmlMsg += "</div>";
  
  message.html.content = htmlMsg.c_str();
  message.html.charSet = "utf-8";
  message.html.transfer_encoding = "7bit";
  message.priority = esp_mail_smtp_priority_normal;
  if (!smtp.connect(&config)) {
    Serial.println("Zlyhalo pripojenie k SMTP serveru.");
    return;
  }
  MailClient.sendMail(&smtp, &message, false);
}

void checkAndSaveMinMax() {
  if (millis() - bootMillis < 45000) { 
    return;
  }
  if (tempInt > -50.0 && tempInt < 100.0) {
    if (tempInt < tempIntMin) { tempIntMin = tempInt; }
    if (tempInt > tempIntMax) { tempIntMax = tempInt; }
  }
  if (tempExt > -50.0 && tempExt < 100.0) {
    if (tempExt < tempExtMin) { tempExtMin = tempExt; }
    if (tempExt > tempExtMax) { tempExtMax = tempExt; }
  }
}

float readNTCRolling(int pin, float r0, float beta, float seriesR, int samples, float filterCoeff, float offset, float lastValidTemp, float* buffer, int& bufCount) {
  int raw = analogRead(pin);
  if (raw >= 4095 || raw <= 0) return lastValidTemp;
  
  float resistance = seriesR / ((4095.0 / (float)raw) - 1.0);
  float steinhart = resistance / r0;
  steinhart = log(steinhart);
  steinhart /= beta;
  steinhart += 1.0 / (25.0 + 273.15);
  steinhart = 1.0 / steinhart;
  steinhart -= 273.15;
  float rawTemp = steinhart + offset;
  
  if (bufCount == 0) {
    for (int i = 0; i < 60; i++) buffer[i] = rawTemp;
    bufCount = 60;
  }
  if (samples > 60) samples = 60;
  if (samples < 1) samples = 1;
  for (int i = 59; i > 0; i--) {
    buffer[i] = buffer[i - 1];
  }
  buffer[0] = rawTemp;

  float sum = 0.0;
  for (int i = 0; i < samples; i++) {
    sum += buffer[i];
  }
  float avgTemp = sum / (float)samples;

  if (lastValidTemp > -50.0 && lastValidTemp < 100.0) {
    return (avgTemp * filterCoeff) + (lastValidTemp * (1.0 - filterCoeff));
  }
  return avgTemp;
}

float readLDRRolling(int pin, int samples, float filterCoeff, float lastValidLDR, float* buffer, int& bufCount) {
  int raw = analogRead(pin);
  float normalized = ((float)raw / 4095.0) * 1000.0;

  if (bufCount == 0) {
    for (int i = 0; i < 60; i++) buffer[i] = normalized;
    bufCount = 60;
  }
  if (samples > 60) samples = 60;
  if (samples < 1) samples = 1;
  for (int i = 59; i > 0; i--) {
    buffer[i] = buffer[i - 1];
  }
  buffer[0] = normalized;

  float sum = 0.0;
  for (int i = 0; i < samples; i++) {
    sum += buffer[i];
  }
  float avgLDR = sum / (float)samples;

  if (lastValidLDR >= 0.0) {
    return (avgLDR * filterCoeff) + (lastValidLDR * (1.0 - filterCoeff));
  }
  return avgLDR;
}

void initGraphsWithCurrentValues() {
  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo);
  int baseHour = 0;
  int baseMin = 0;
  if (timeValid) {
    baseHour = timeinfo.tm_hour;
    baseMin = (timeinfo.tm_min / 15) * 15;
  }
  
  for (int i = 0; i < GRAPH_SIZE; i++) {
    historyInt[i] = (tempInt > -50.0 && tempInt < 100.0) ? tempInt : 22.0;
    historyExt[i] = (tempExt > -50.0 && tempExt < 100.0) ? tempExt : 15.0;
    historyLight[i] = (lightLevelFiltered >= 0.0) ? lightLevelFiltered : 500.0;
    historyWind[i] = 0.0;
    if (timeValid) {
      int minsBack = (GRAPH_SIZE - 1 - i) * 15;
      int currentTotalMins = baseHour * 60 + baseMin - minsBack;
      while (currentTotalMins < 0) currentTotalMins += 24 * 60;
      int h = (currentTotalMins / 60) % 24;
      int m = currentTotalMins % 60;
      char buf[10];
      sprintf(buf, "%02d:%02d", h, m);
      historyTime[i] = String(buf);
    } else {
      historyTime[i] = "--:--";
    }
  }
  graphCount = GRAPH_SIZE;
  graphInitialized = true;
  Serial.println("[GRAFY] Historie uspesne predvyplnene aktualnymi hodnotami.");
}

void addGraphRecord(float tI, float tE, float lL, float wS) {
  String currentTimeStr = getClockTime();
  for (int i = 1; i < GRAPH_SIZE; i++) {
    historyInt[i - 1] = historyInt[i];
    historyExt[i - 1] = historyExt[i];
    historyLight[i - 1] = historyLight[i];
    historyWind[i - 1] = historyWind[i];
    historyTime[i - 1] = historyTime[i];
  }
  historyInt[GRAPH_SIZE - 1] = tI;
  historyExt[GRAPH_SIZE - 1] = tE;
  historyLight[GRAPH_SIZE - 1] = lL;
  historyWind[GRAPH_SIZE - 1] = wS;
  historyTime[GRAPH_SIZE - 1] = currentTimeStr;
  if (graphCount < GRAPH_SIZE) graphCount++;
}

uint16_t readBlindPosition(int i) {
  TwoWire &bus = (i < 3) ? Wire : Wire1;
  uint8_t pca_addr = (i < 3) ? PCA_ADDR_1 : PCA_ADDR_2;
  uint8_t channel = (i < 3) ? i : (i - 3);
  
  bus.beginTransmission(pca_addr);
  bus.write(1 << channel); 
  if (bus.endTransmission() != 0) return 0xFFFF;
  
  bus.beginTransmission(SA5600_ADDR);
  bus.write(0x0E);
  if (bus.endTransmission() != 0) {
    bus.beginTransmission(pca_addr); bus.write(0x00); bus.endTransmission(); // Bezpecne odpojenie zbernice pri chybe
    return 0xFFFF; 
  }
  
  bus.requestFrom(SA5600_ADDR, 2);
  uint16_t finalPos = 0xFFFF;
  if (bus.available() >= 2) {
    uint16_t high = bus.read();
    uint16_t low = bus.read();
    finalPos = ((high & 0x0F) << 8) | low;
  }
  
  // FIX: OKAMZITA IZOLACIA MULTIPLEXERA (ZAMKNE SENZOROVE LINKY A ZABRANI PRESLUCHOM Z EMI SUMU CELI)
  bus.beginTransmission(pca_addr);
  bus.write(0x00);
  bus.endTransmission();
  
  return finalPos;
}

void updateBlindPositions() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 30) return;
  lastUpdate = now;

  static uint16_t lastRaw[6] = {0, 0, 0, 0, 0, 0};
  static bool initialized[6] = {false, false, false, false, false, false};
  for (int i = 0; i < 6; i++) {
    uint16_t raw = readBlindPosition(i);
    if (raw == 0xFFFF) continue; 

    if (!initialized[i]) {
      lastRaw[i] = raw;
      blindPosition[i] = (int32_t)raw;
      initialized[i] = true;
      continue;
    }

    int16_t rawDelta = (int16_t)raw - (int16_t)lastRaw[i];
    if (rawDelta < -2048) rawDelta += 4096;
    else if (rawDelta > 2048) rawDelta -= 4096;

    if (abs(rawDelta) > 800) continue;
    if (runDirection[i] == 0 && abs(rawDelta) <= 4) continue;
    blindPosition[i] += rawDelta;
    lastRaw[i] = raw;
  }
}

int32_t getEffectivePosition(int i) {
  return blindPosition[i] - zeroOffset[i];
}

void recoverI2CBus(int busNum) {
  int sdaPin = (busNum == 1) ? 21 : 4;
  int sclPin = (busNum == 1) ? 22 : 5;
  
  pinMode(sdaPin, INPUT_PULLUP);
  if (digitalRead(sdaPin) == LOW) {
    Serial.printf("[I2C] Vetva %d SDA drzane v LOW. Spustam obnovu...\n", busNum);
    pinMode(sclPin, OUTPUT);
    for (int i = 0; i < 9; i++) {
      digitalWrite(sclPin, LOW);
      delayMicroseconds(10);
      digitalWrite(sclPin, HIGH);
      delayMicroseconds(10);
    }
    Serial.println("[I2C] Taktovanie dokoncene.");
  }
  
  if (busNum == 1) {
    Wire.end();
    Wire.begin(21, 22);
    Wire.setClock(100000);
    Wire.beginTransmission(PCA_ADDR_1); 
    Wire.write(0x07); 
    Wire.endTransmission();
  } else {
    Wire1.end();
    Wire1.begin(4, 5);
    Wire1.setClock(100000);
    Wire1.beginTransmission(PCA_ADDR_2); 
    Wire1.write(0x07); 
    Wire1.endTransmission();
  }
}

void flushRelays() {
  static uint8_t last_pcf1 = 0xFF;
  static uint8_t last_pcf2 = 0xFF;
  static unsigned long lastForceWrite = 0;
  static unsigned long lastFlush = 0;
  unsigned long now = millis();
  
  if (now - lastFlush < 20) return;
  lastFlush = now;
  
  uint8_t pcf1 = 0x00; 
  uint8_t pcf2 = 0x00;

  for (int i = 0; i < 3; i++) {
    if (lastDirection[i] == 1) pcf1 |= (0x01 << (i * 2));
    else if (lastDirection[i] == 2) pcf1 |= (0x02 << (i * 2));
  }
  
  for (int i = 3; i < 6; i++) {
    int channel = i - 3;
    if (lastDirection[i] == 1) pcf2 |= (0x01 << (channel * 2));
    else if (lastDirection[i] == 2) pcf2 |= (0x02 << (channel * 2));
  }
  
  bool slowSpeedActive = false;
  for (int i = 0; i < 6; i++) {
    if (lastDirection[i] != 0) {
      if (isMicroAdjusting[i] || seqLightState[i] == 2 || seqTempState[i] == 2 || timeTiltState[i] == 3) { 
        slowSpeedActive = true;
      }
    }
  }
  if (slowSpeedActive) {
    pcf2 |= (1 << 6);
  }

  if (lightState) { pcf2 |= (1 << 7); }
  if (light2State) { pcf1 |= (1 << 7); }

  bool forceWrite = (now - lastForceWrite >= 100);
  if (forceWrite) {
    lastForceWrite = now;
  }

  // FIX: PRIDANY ROBUSTNY 3-NASOBNY RETRY MECHANIZMUS S MIKRO PAUZOU (STOP CVAKANIU A REBOOTOM ZBERNICE PRI TRANZIENTOCH)
  if (pcf1 != last_pcf1 || forceWrite) {
    uint8_t errCode = 1;
    for (int r = 0; r < 3; r++) {
      Wire.beginTransmission(PCF_ADDR_1); 
      Wire.write(pcf1);
      errCode = Wire.endTransmission();
      if (errCode == 0) { 
        last_pcf1 = pcf1; 
        break; 
      }
      delayMicroseconds(100);
    }
    if (errCode != 0) { recoverI2CBus(1); }
  }

  delayMicroseconds(200); // V84: Izolacny cas 200us pre eliminu preluchov medzi PCF1 a PCF2

  if (pcf2 != last_pcf2 || forceWrite) {
    uint8_t errCode = 1;
    for (int r = 0; r < 3; r++) {
      Wire1.beginTransmission(PCF_ADDR_2); 
      Wire1.write(pcf2);
      errCode = Wire1.endTransmission();
      if (errCode == 0) { 
        last_pcf2 = pcf2; 
        break; 
      }
      delayMicroseconds(100);
    }
    if (errCode != 0) { recoverI2CBus(2); }
  }
}

// FIX: KOREKTNE ROZDELENE PARAMETROV DO INDIVIDUALNYCH SUB-NAMESPACES PRE OCHRANU LIMITOV PAMATE NVS CO DO POCET KLUCOV
void saveAllConfigurationToNVS() {
  preferences.begin("terasa_g", false);
  preferences.putBool("gaute", globalAutoEnable);
  preferences.putInt("hstart", autoHourStart);
  preferences.putInt("hend", autoHourEnd);
  preferences.putInt("atu", allTimeUp);
  preferences.putInt("atd", allTimeDown);
  preferences.putInt("etu1", extTimeUpCH1);
  preferences.putInt("etd1", extTimeDownCH1);
  preferences.putFloat("wlim", windLimit);
  preferences.putFloat("wp25", windPulsesFor25kmh);
  preferences.putInt("ppturn", pulsesPerTurn);
  preferences.putULong("wsafedur", windSafetyDuration);
  preferences.putInt("wruntime", windRunTime);
  preferences.putInt("wmaxhits", windMaxHits);
  preferences.putInt("mcth", microAdjustThreshold);
  preferences.putInt("ldrs", ldrSamples);
  preferences.putFloat("ldrf", ldrFilter);
  preferences.putInt("nir0", ntcIntR0);
  preferences.putInt("nib", ntcIntBeta);
  preferences.putFloat("nisr", ntcIntSeriesR);
  preferences.putInt("nismp", ntcIntSamples);
  preferences.putFloat("niflt", ntcIntFilter);
  preferences.putFloat("nioff", ntcIntOffset);
  preferences.putInt("ner0", ntcExtR0);
  preferences.putInt("neb", ntcExtBeta);
  preferences.putFloat("nesr", ntcExtSeriesR);
  preferences.putInt("nesmp", ntcExtSamples);
  preferences.putFloat("neflt", ntcExtFilter);
  preferences.putFloat("neoff", ntcExtOffset);
  preferences.end();

  for (int i = 0; i < 6; i++) {
    char ns[16];
    sprintf(ns, "terasa_b%d", i);
    preferences.begin(ns, false);
    
    preferences.putInt("tu", tUp[i]);
    preferences.putInt("td", tDown[i]);
    preferences.putInt("zOff", zeroOffset[i]); 
    preferences.putBool("ate", autoTempEnable[i]);
    preferences.putInt("tmd", tempMode[i]);
    preferences.putFloat("tlu", tempLimitUp[i]);
    preferences.putFloat("tld", tempLimitDown[i]);
    preferences.putFloat("thyst", tempHysteresis[i]);
    preferences.putInt("tta", tTempUp[i]);
    preferences.putInt("ttd", tTempDown[i]);

    preferences.putBool("ale", autoLightEnable[i]);
    preferences.putInt("lmd", lightMode[i]);
    preferences.putInt("llu", lightLimitUp[i]);
    preferences.putInt("lld", lightLimitDown[i]);
    preferences.putInt("lhyst", lightHysteresis[i]);
    preferences.putInt("lta", tLightUp[i]);
    preferences.putInt("ltd", tLightDown[i]);
    
    preferences.putBool("sqle", seqLightEnable[i]);
    preferences.putFloat("sqlp", seqLightPauseTime[i]);
    preferences.putInt("sqld", seqLightSecondActionDir[i]);
    preferences.putFloat("sqlt", seqLightSecondActionTime[i]);
    
    preferences.putBool("sqte", seqTempEnable[i]);
    preferences.putFloat("sqtp", seqTempPauseTime[i]);
    preferences.putInt("sqtd", seqTempSecondActionDir[i]);
    preferences.putFloat("sqtt", seqTempSecondActionTime[i]);
    
    preferences.putBool("utt", useTimeTilt[i]);
    for (int p = 0; p < 8; p++) {
      char key[16];
      sprintf(key, "d1_%d", p);  preferences.putInt(key, timeTiltDir1[i][p]);
      sprintf(key, "t1_%d", p);  preferences.putFloat(key, timeTiltDur1[i][p]);
      sprintf(key, "tp_%d", p);  preferences.putFloat(key, timeTiltPause[i][p]);
      sprintf(key, "d2_%d", p);  preferences.putInt(key, timeTiltDir2[i][p]);
      sprintf(key, "t2_%d", p);  preferences.putFloat(key, timeTiltDur2[i][p]);
      sprintf(key, "p_%d", p);   preferences.putInt(key, (int32_t)tiltPresets[i][p]);
    }
    preferences.end();
  }
  Serial.println("[NVS] Vsetky parametre uspesne ulozene do Preferences.");
}

void loadNTCParams() {
  preferences.begin("terasa_g", true);
  ntcIntR0 = preferences.getInt("nir0", 10000);
  ntcIntBeta = preferences.getInt("nib", 3977);
  ntcIntSeriesR = preferences.getFloat("nisr", 10000.0);
  ntcIntSamples = preferences.getInt("nismp", 20);
  ntcIntFilter = preferences.getFloat("niflt", 0.10);
  ntcIntOffset = preferences.getFloat("nioff", -2.5);
  ntcExtR0 = preferences.getInt("ner0", 10000);
  ntcExtBeta = preferences.getInt("neb", 3977);
  ntcExtSeriesR = preferences.getFloat("nesr", 10000.0);
  ntcExtSamples = preferences.getInt("nesmp", 20);
  ntcExtFilter = preferences.getFloat("neflt", 0.10);
  ntcExtOffset = preferences.getFloat("neoff", -2.5);
  ldrSamples = preferences.getInt("ldrs", 20);
  ldrFilter = preferences.getFloat("ldrf", 0.10);
  preferences.end();
}

void loadConfiguration() {
  preferences.begin("terasa_g", true);
  globalAutoEnable = preferences.getBool("gaute", true);
  autoHourStart = preferences.getInt("hstart", 7);
  autoHourEnd = preferences.getInt("hend", 21);
  allTimeUp = preferences.getInt("atu", 60);
  allTimeDown = preferences.getInt("atd", 60);
  extTimeUpCH1 = preferences.getInt("etu1", 60);
  extTimeDownCH1 = preferences.getInt("etd1", 60);
  windLimit = preferences.getFloat("wlim", 45.0);
  windPulsesFor25kmh = preferences.getFloat("wp25", 25.0);
  pulsesPerTurn = preferences.getInt("ppturn", 2);
  windSafetyDuration = preferences.getULong("wsafedur", 60000);
  windRunTime = preferences.getInt("wruntime", 60);
  windMaxHits = preferences.getInt("wmaxhits", 3);
  microAdjustThreshold = preferences.getInt("mcth", 80);
  preferences.end();

  for (int i = 0; i < 6; i++) {
    char ns[16];
    sprintf(ns, "terasa_b%d", i);
    preferences.begin(ns, true);
    
    tUp[i] = preferences.getInt("tu", 60);
    tDown[i] = preferences.getInt("td", 60);
    zeroOffset[i] = preferences.getInt("zOff", 0);
    autoTempEnable[i] = preferences.getBool("ate", true);
    tempMode[i] = preferences.getInt("tmd", 0);
    tempLimitUp[i] = preferences.getFloat("tlu", 22.0);
    tempLimitDown[i] = preferences.getFloat("tld", 26.0);
    tempHysteresis[i] = preferences.getFloat("thyst", 0.5);
    tTempUp[i] = preferences.getInt("tta", 15);
    tTempDown[i] = preferences.getInt("ttd", 15);

    autoLightEnable[i] = preferences.getBool("ale", true);
    lightMode[i] = preferences.getInt("lmd", 0);
    lightLimitUp[i] = preferences.getInt("llu", 250);
    lightLimitDown[i] = preferences.getInt("lld", 750);
    lightHysteresis[i] = preferences.getInt("lhyst", 50);
    tLightUp[i] = preferences.getInt("lta", 15);
    tLightDown[i] = preferences.getInt("ltd", 15);

    seqLightEnable[i] = preferences.getBool("sqle", false);
    seqLightPauseTime[i] = preferences.getFloat("sqlp", 10.0);
    seqLightSecondActionDir[i] = preferences.getInt("sqld", 1);
    seqLightSecondActionTime[i] = preferences.getFloat("sqlt", 5.0);

    seqTempEnable[i] = preferences.getBool("sqte", false);
    seqTempPauseTime[i] = preferences.getFloat("sqtp", 10.0);
    seqTempSecondActionDir[i] = preferences.getInt("sqtd", 1);
    seqTempSecondActionTime[i] = preferences.getFloat("sqtt", 5.0);

    useTimeTilt[i] = preferences.getBool("utt", false);
    for (int p = 0; p < 8; p++) {
      char key[16];
      sprintf(key, "d1_%d", p);  timeTiltDir1[i][p] = preferences.getInt(key, 2); 
      sprintf(key, "t1_%d", p);  timeTiltDur1[i][p] = preferences.getFloat(key, 1.50); 
      sprintf(key, "tp_%d", p);  timeTiltPause[i][p] = preferences.getFloat(key, 0.50);
      sprintf(key, "d2_%d", p);  timeTiltDir2[i][p] = preferences.getInt(key, 1);
      sprintf(key, "t2_%d", p);  timeTiltDur2[i][p] = preferences.getFloat(key, 0.10 * (p + 1));
      sprintf(key, "p_%d", p);   tiltPresets[i][p] = (int32_t)preferences.getInt(key, 0);
    }
    preferences.end();
  }
}

float safeVal(float val, float fallback) {
  if (isnan(val) || isinf(val) || val > 99999.0 || val < -99999.0) return fallback;
  return val;
}

String makeStatusJSON() {
  String json;
  json.reserve(6144); 
  json = "{";
  json += "\"ti\":" + String(safeVal(tempInt, 0.0), 1) + ",";
  json += "\"te\":" + String(safeVal(tempExt, 0.0), 1) + ",";
  json += "\"timn\":" + String(safeVal(tempIntMin, 99.9), 1) + ",";
  json += "\"timx\":" + String(safeVal(tempIntMax, -99.9), 1) + ",";
  json += "\"temn\":" + String(safeVal(tempExtMin, 99.9), 1) + ",";
  json += "\"temx\":" + String(safeVal(tempExtMax, -99.9), 1) + ",";
  json += "\"ws\":" + String(safeVal(windSpeed, 0.0), 1) + ",";
  json += "\"wsmx\":" + String(safeVal(windSpeedMax, 0.0), 1) + ",";
  json += "\"wsmx24\":" + String(safeVal(windSpeedMax24h, 0.0), 1) + ","; 
  json += "\"lt\":" + String(lightLevel) + ",";
  json += "\"rssi\":" + String(wifiRSSI) + ",";
  json += "\"clk\":\"" + getClockTime() + "\",";
  json += "\"safety\":\"" + String(safetyActive ? "1" : "0") + "\",";
  json += "\"allH\":\"" + String(allHore ? "1" : "0") + "\",";
  json += "\"allD\":\"" + String(allDole ? "1" : "0") + "\",";
  json += "\"gaute\":\"" + String(globalAutoEnable ? "1" : "0") + "\",";
  json += "\"hst\":" + String(autoHourStart) + ",";
  json += "\"hnd\":" + String(autoHourEnd) + ",";
  json += "\"light\":" + String(lightState ? "1" : "0") + ",";
  json += "\"l2\":" + String(light2State ? "1" : "0") + ",";
  
  long lremMin = 0;
  if (lightState && lightOffTarget > millis()) { lremMin = (lightOffTarget - millis()) / 60000; }
  json += "\"lrem\":" + String(lremMin) + ",";

  long l2remMin = 0;
  if (light2State && light2OffTarget > millis()) { l2remMin = (light2OffTarget - millis()) / 60000; }
  json += "\"l2rem\":" + String(l2remMin) + ",";

  json += "\"whH\":[";
  for (int i = 0; i < 6; i++) { json += "\"" + String(webHoldHore[i] ? "1" : "0") + "\"" + (i < 5 ? "," : ""); }
  json += "],\"whD\":[";
  for (int i = 0; i < 6; i++) { json += "\"" + String(webHoldDole[i] ? "1" : "0") + "\"" + (i < 5 ? "," : ""); }
  json += "],";
  
  unsigned long now = millis();
  json += "\"rem\":[";
  for (int i = 0; i < 6; i++) {
    long rem = 0;
    if (runUntil[i] > now) rem = (runUntil[i] - now) / 1000;
    json += "\"" + String(rem) + "\"" + (i < 5 ? "," : "");
  }
  json += "],\"rDir\":[";
  for (int i = 0; i < 6; i++) { json += "\"" + String(runDirection[i]) + "\"" + (i < 5 ? "," : ""); }
  json += "],\"as\":[";
  for (int i = 0; i < 6; i++) {
    int statusAuto = 0;
    if (seqLightState[i] == 1 || seqTempState[i] == 1) statusAuto = 3;
    else if (seqLightState[i] == 2 || seqTempState[i] == 2) statusAuto = 4;
    else if (lightActionTriggered[i]) statusAuto = 2;
    else if (tempActionTriggered[i]) statusAuto = 1;
    json += "\"" + String(statusAuto) + "\"" + (i < 5 ? "," : "");
  }
  json += "],\"pos\":[";
  for (int i = 0; i < 6; i++) { json += String(getEffectivePosition(i)) + (i < 5 ? "," : ""); } 
  json += "],\"prst\":[";
  for (int i = 0; i < 6; i++) {
    json += "[";
    for (int p = 0; p < 8; p++) { json += String(tiltPresets[i][p]) + (p < 7 ? "," : ""); }
    json += "]" + String(i < 5 ? "," : ""); 
  }
  json += "],\"tu\":[";
  for(int i=0; i<6; i++) { json += String(tUp[i]) + (i<5?",":""); }
  json += "],\"td\":[";
  for(int i=0; i<6; i++) { json += String(tDown[i]) + (i<5?",":""); }
  json += "],\"zOff\":[";
  for(int i=0; i<6; i++) { json += String(zeroOffset[i]) + (i<5?",":""); }
  json += "],\"ate\":[";
  for(int i=0; i<6; i++) { json += String(autoTempEnable[i] ? "1" : "0") + (i<5?",":""); }
  json += "],\"tmd\":[";
  for(int i=0; i<6; i++) { json += String(tempMode[i]) + (i<5?",":""); }
  json += "],\"tlu\":[";
  for(int i=0; i<6; i++) { json += String(tempLimitUp[i], 1) + (i<5?",":""); }
  json += "],\"tta\":[";
  for(int i=0; i<6; i++) { json += String(tTempUp[i]) + (i<5?",":""); }
  json += "],\"tld\":[";
  for(int i=0; i<6; i++) { json += String(tempLimitDown[i], 1) + (i<5?",":""); }
  json += "],\"ttd\":[";
  for(int i=0; i<6; i++) { json += String(tTempDown[i]) + (i<5?",":""); } 
  json += "],\"thyst\":[";
  for(int i=0; i<6; i++) { json += String(tempHysteresis[i], 1) + (i<5?",":""); }
  json += "],\"ale\":[";
  for(int i=0; i<6; i++) { json += String(autoLightEnable[i] ? "1" : "0") + (i<5?",":""); }
  json += "],\"lmd\":[";
  for (int i=0; i<6; i++) { json += String(lightMode[i]) + (i<5?",":""); }
  json += "],\"llu\":[";
  for (int i=0; i<6; i++) { json += String(lightLimitUp[i]) + (i<5?",":""); }
  json += "],\"lta\":[";
  for (int i=0; i<6; i++) { json += String(tLightUp[i]) + (i<5?",":""); }
  json += "],\"lld\":[";
  for (int i=0; i<6; i++) { json += String(lightLimitDown[i]) + (i<5?",":""); }
  json += "],\"ltd\":[";
  for (int i=0; i<6; i++) { json += String(tLightDown[i]) + (i < 5 ? "," : ""); } 
  json += "],\"lhyst\":["; for (int i=0; i<6; i++) { json += String(lightHysteresis[i]) + (i<5?",":""); }
  json += "],\"sqle\":["; for (int i = 0; i < 6; i++) { json += String(seqLightEnable[i] ? 1 : 0) + (i < 5 ? "," : ""); }
  json += "],\"sqlp\":["; for (int i = 0; i < 6; i++) { json += String(seqLightPauseTime[i], 1) + (i < 5 ? "," : ""); }
  json += "],\"sqld\":["; for (int i = 0; i < 6; i++) { json += String(seqLightSecondActionDir[i]) + (i < 5 ? "," : ""); }
  json += "],\"sqlt\":["; for (int i = 0; i < 6; i++) { json += String(seqLightSecondActionTime[i], 1) + (i < 5 ? "," : ""); }
  json += "],\"sqte\":["; for (int i = 0; i < 6; i++) { json += String(seqTempEnable[i] ? 1 : 0) + (i < 5 ? "," : ""); }
  json += "],\"sqtp\":["; for (int i = 0; i < 6; i++) { json += String(seqTempPauseTime[i], 1) + (i < 5 ? "," : ""); }
  json += "],\"sqtd\":["; for (int i = 0; i < 6; i++) { json += String(seqTempSecondActionDir[i]) + (i < 5 ? "," : ""); }
  json += "],\"sqtt\":["; for (int i = 0; i < 6; i++) { json += String(seqTempSecondActionTime[i], 1) + (i < 5 ? "," : ""); }

  json += "],\"utt\":[";
  for(int i=0; i<6; i++) { json += String(useTimeTilt[i] ? "1" : "0") + (i<5?",":""); }
  
  json += "],\"ttd1\":[";
  for(int i=0; i<6; i++) {
    json += "[";
    for(int p=0; p<8; p++) { json += String(timeTiltDir1[i][p]) + (p<7?",":""); }
    json += "]" + String(i<5?",":"");
  }
  
  json += "],\"ttt1\":[";
  for(int i=0; i<6; i++) {
    json += "[";
    for(int p=0; p<8; p++) { json += String(timeTiltDur1[i][p], 2) + (p<7?",":""); }
    json += "]" + String(i<5?",":"");
  }
  
  json += "],\"ttp\":[";
  for(int i=0; i<6; i++) {
    json += "[";
    for(int p=0; p<8; p++) { json += String(timeTiltPause[i][p], 2) + (p<7?",":""); }
    json += "]" + String(i<5?",":"");
  }
  
  json += "],\"ttd2\":[";
  for(int i=0; i<6; i++) {
    json += "[";
    for(int p=0; p<8; p++) { json += String(timeTiltDir2[i][p]) + (p<7?",":""); }
    json += "]" + String(i<5?",":"");
  }
  
  json += "],\"ttt2\":[";
  for(int i=0; i<6; i++) {
    json += "[";
    for(int p=0; p<8; p++) { json += String(timeTiltDur2[i][p], 2) + (p<7?",":""); }
    json += "]" + String(i<5?",":"");
  }

  json += "],\"wlim\":" + String(windLimit, 1) + ",";
  json += "\"wp25\":" + String(windPulsesFor25kmh, 1) + ",";
  json += "\"ppturn\":" + String(pulsesPerTurn) + ",";
  json += "\"wsafedur\":" + String(windSafetyDuration) + ",";
  json += "\"wruntime\":" + String(windRunTime) + ",";
  json += "\"wmaxhits\":" + String(windMaxHits) + ",";
  json += "\"mcth\":" + String(microAdjustThreshold) + ",";
  json += "\"etu1\":" + String(extTimeUpCH1) + ",";
  json += "\"etd1\":" + String(extTimeDownCH1) + ",";
  json += "\"ldrs\":" + String(ldrSamples) + ",";
  json += "\"ldrf\":" + String(ldrFilter, 2) + ",";
  json += "\"ntciR0\":" + String(ntcIntR0) + ",";
  json += "\"ntciB\":" + String(ntcIntBeta) + ",";
  json += "\"ntciSR\":" + String((long)ntcIntSeriesR) + ",";
  json += "\"ntciSmp\":" + String(ntcIntSamples) + ",";
  json += "\"ntciFlt\":" + String(ntcIntFilter, 2) + ",";
  json += "\"ntciOff\":" + String(ntcIntOffset, 1) + ",";
  
  json += "\"ntceR0\":" + String(ntcExtR0) + ",";
  json += "\"ntceB\":" + String(ntcExtBeta) + ",";
  json += "\"ntceSR\":" + String((long)ntcExtSeriesR) + ",";
  json += "\"ntceSmp\":" + String(ntcExtSamples) + ","; 
  json += "\"ntceFlt\":" + String(ntcExtFilter, 2) + ",";
  json += "\"ntceOff\":" + String(ntcExtOffset, 1) + ","; 

  json += "\"labels\":[";
  for (int i = 0; i < graphCount; i++) { json += "\"" + historyTime[i] + "\"" + (i < graphCount - 1 ? "," : ""); }
  json += "],\"histInt\":["; for (int i = 0; i < graphCount; i++) { json += String(safeVal(historyInt[i], 0.0), 1) + (i < graphCount - 1 ? "," : ""); } 
  json += "],\"gExt\":["; for (int i = 0; i < graphCount; i++) { json += String(safeVal(historyExt[i], 0.0), 1) + (i < graphCount - 1 ? "," : ""); } 
  json += "],\"histLight\":["; for (int i = 0; i < graphCount; i++) { json += String(safeVal(historyLight[i], 0.0), 0) + (i < graphCount - 1 ? "," : ""); }
  json += "],\"histWind\":["; for (int i = 0; i < graphCount; i++) { json += String(safeVal(historyWind[i], 0.0), 1) + (i < graphCount - 1 ? "," : ""); }
  json += "]}";
  return json;
}

void processButtonStateMachine(int rawState, ButtonStateMachine &sm, int &extMoveUp, int &extMoveDown) {
  unsigned long now = millis();
  bool currentReading = (rawState == LOW); 
  
  if (currentReading != sm.lastRawState) {
    sm.lastDebounceTime = now;
    sm.lastRawState = currentReading;
  }
  
  if ((now - sm.lastDebounceTime) > 40) { 
    if (currentReading != sm.debouncedState) {
      sm.debouncedState = currentReading;
      if (sm.debouncedState == true) { 
        sm.clickCount++;
        if (sm.clickCount == 1) {
          sm.lastClickTime = now;
        }
        sm.isHoldingActive = false;
      } else { 
        if (sm.isHoldingActive) {
          sm.isHoldingActive = false;
          sm.clickCount = 0;
        }
      }
    }
  }
  
  if (sm.debouncedState == true && !sm.isHoldingActive) {
    if (now - sm.lastClickTime > 350) { 
      sm.isHoldingActive = true;
    }
  }
  
  if (sm.isHoldingActive && sm.debouncedState == true) {
    extMoveUp = 1;
  }
  
  if (sm.clickCount > 0 && (now - sm.lastClickTime) > 450) {
    if (sm.clickCount >= 2) {
      extMoveDown = 1;
    }
    sm.clickCount = 0;
  }
}

void setup() {
  Wire.begin(21, 22);  
  Wire1.begin(4, 5);   

  Wire.beginTransmission(PCA_ADDR_1); Wire.write(0x07); Wire.endTransmission();
  Wire.beginTransmission(PCF_ADDR_1); Wire.write(0x00); Wire.endTransmission(); 

  Wire1.beginTransmission(PCA_ADDR_2); Wire1.write(0x07); Wire1.endTransmission();
  Wire1.beginTransmission(PCF_ADDR_2); Wire1.write(0x00); Wire1.endTransmission(); 

  Serial.begin(115200);
  bootMillis = millis();
  delay(100);
  Serial.println("\n=== SMART TERASA SUSTAVA START ===");
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }


  loadConfiguration(); loadNTCParams();
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet, primaryDNS); WiFi.begin(ssid, password);
  
  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED && cnt < 20) { delay(500); Serial.print("."); cnt++; }

  configTime(0, 0, ntpServer); setenv("TZ", timeZone, 1);
  tzset();

  pinMode(WIND_PIN, INPUT_PULLUP); attachInterrupt(digitalPinToInterrupt(WIND_PIN), countPulse, FALLING);
  pinMode(LIGHT_PIN, INPUT); pinMode(EXT_UP_CH1, INPUT_PULLUP);
  pinMode(EXT_DOWN_CH1, INPUT_PULLUP);
  pinMode(NTC_INT_PIN, INPUT); pinMode(NTC_EXT_PIN, INPUT);
  analogSetAttenuation(ADC_11db); smtp.callback(smtpCallback);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "application/json", makeStatusJSON()); });
  server.on("/resetwindmax", HTTP_GET, [](AsyncWebServerRequest *request){ resetWindMaxFlag = true; request->send(200, "text/plain", "OK"); });
  
  server.on("/light", HTTP_GET, [](AsyncWebServerRequest *request){
    int id = 1; if(request->hasParam("id")) id = request->getParam("id")->value().toInt();
    int val = 0; if(request->hasParam("s")) val = request->getParam("s")->value().toInt();
    if(id == 1) lightState = (val == 1);
    if(id == 2) light2State = (val == 1);
    request->send(200, "text/plain", "OK");
  });
  
  server.on("/lighttimer", HTTP_GET, [](AsyncWebServerRequest *request){
    int id = 1; if(request->hasParam("id")) id = request->getParam("id")->value().toInt();
    if (request->hasParam("m")) {
      int mins = request->getParam("m")->value().toInt();
      if (mins > 0 && mins <= 240) {
        if(id == 1) { lightTimerMinutes = mins; lightOffTarget = millis() + ((unsigned long)mins * 60000); lightState = true; }
        if(id == 2) { light2TimerMinutes = mins; light2OffTarget = millis() + ((unsigned long)mins * 60000); light2State = true; }
      } else {
        if(id == 1) { lightTimerMinutes = 0; lightOffTarget = 0; }
        if(id == 2) { light2TimerMinutes = 0; light2OffTarget = 0; }
      }
    }
    request->send(200, "text/plain", "OK");
  });
  
  server.on("/seqtrigger", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("i") && request->hasParam("t")) {
      int i = request->getParam("i")->value().toInt();
      int type = request->getParam("t")->value().toInt(); 
      if (i >= 0 && i < 6) {
        isTargetingPos[i] = false;
        if (type == 1 && seqLightEnable[i]) {
          unsigned long dur = (unsigned long)tLightDown[i] * 1000;
          runUntil[i] = millis() + dur; 
          runDirection[i] = 2; lightActionTriggered[i] = true;
          seqLightState[i] = 1; seqLightTimer[i] = millis() + dur + ((unsigned long)seqLightPauseTime[i] * 1000);
        } else if (type == 2 && seqTempEnable[i]) {
          unsigned long dur = (unsigned long)tTempDown[i] * 1000;
          runUntil[i] = millis() + dur; runDirection[i] = 2; tempActionTriggered[i] = true;
          seqTempState[i] = 1; seqTempTimer[i] = millis() + dur + ((unsigned long)seqTempPauseTime[i] * 1000);
        }
      }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/zero", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("i")) {
      int i = request->getParam("i")->value().toInt();
      if (i >= 0 && i < 6) { zeroOffset[i] = blindPosition[i]; saveAllConfigurationToNVS(); }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/setpreset", HTTP_GET, [](AsyncWebServerRequest *request){
    if (safetyActive) { request->send(200, "text/plain", "BLOCKED"); return; }
    int i = request->getParam("i")->value().toInt(); int p = request->getParam("p")->value().toInt();
    if (i >= 0 && i < 6 && p >= 0 && p < 8) {
      if (useTimeTilt[i]) {
        isTargetingPos[i] = false;
        isMicroAdjusting[i] = false;
        timeTiltPresetIndex[i] = p;
        if (timeTiltDur1[i][p] > 0.01) {
          timeTiltState[i] = 1; 
          timeTiltTimer[i] = millis() + (unsigned long)(timeTiltDur1[i][p] * 1000.0);
          runDirection[i] = timeTiltDir1[i][p];
          runUntil[i] = timeTiltTimer[i];
        } else {
          timeTiltState[i] = 2;
          timeTiltTimer[i] = millis() + (unsigned long)(timeTiltPause[i][p] * 1000.0);
          runDirection[i] = 0;
          runUntil[i] = 0;
        }
      } else {
        targetPosition[i] = tiltPresets[i][p];
        isTargetingPos[i] = true;
        runUntil[i] = 0;
        runDirection[i] = 0;
      }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/hold", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (safetyActive) { request->send(200, "text/plain", "BLOCKED"); return; }
    int i = request->getParam("i")->value().toInt(); int d = request->getParam("d")->value().toInt(); int v = request->getParam("v")->value().toInt();
    if (i >= 0 && i < 6) { isTargetingPos[i] = false; isMicroAdjusting[i] = false; timeTiltState[i] = 0; if (d == 1) webHoldHore[i] = (v == 1); if (d == 2) webHoldDole[i] = (v == 1); if (v == 0 && runDirection[i] == d) { runUntil[i] = 0; runDirection[i] = 0; } seqLightState[i] = 0; seqTempState[i] = 0; }
    request->send(200, "text/plain", "OK");
  });

  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (safetyActive) { request->send(200, "text/plain", "BLOCKED"); return; }
    int i = request->getParam("i")->value().toInt(); int d = request->getParam("d")->value().toInt();
    if (i >= 0 && i < 6) { isTargetingPos[i] = false; isMicroAdjusting[i] = false; timeTiltState[i] = 0; if (i == 0) extBtnAutoActive = false; runUntil[i] = millis() + ((d == 1 ? tUp[i] : tDown[i]) * 1000); runDirection[i] = d; seqLightState[i] = 0; seqTempState[i] = 0; }
    request->send(200, "text/plain", "OK");
  });

  server.on("/holdall", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (safetyActive) { request->send(200, "text/plain", "BLOCKED"); return; }
    int d = request->getParam("d")->value().toInt(); int v = request->getParam("v")->value().toInt();
    unsigned long baseTime = millis();
    if (d == 1) allHore = (v == 1); if (d == 2) allDole = (v == 1);
    if (v == 1) {
      for (int i = 0; i < 6; i++) { motorTriggerTime[i] = baseTime + (i * 150); }
    } else {
      for (int i = 0; i < 6; i++) { runUntil[i] = 0; runDirection[i] = 0; webHoldHore[i] = false; webHoldDole[i] = false; isTargetingPos[i] = false; isMicroAdjusting[i] = false; seqLightState[i] = 0; seqTempState[i] = 0; motorTriggerTime[i] = 0; timeTiltState[i] = 0; } allHore = false; allDole = false;
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/timeall", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (safetyActive) { request->send(200, "text/plain", "BLOCKED"); return; }
    int d = request->getParam("d")->value().toInt(); extBtnAutoActive = false; 
    unsigned long baseTime = millis();
    for (int i = 0; i < 6; i++) { 
      isTargetingPos[i] = false; 
      isMicroAdjusting[i] = false;
      timeTiltState[i] = 0;
      runDirection[i] = d; 
      motorTriggerTime[i] = baseTime + (i * 150); 
      runUntil[i] = baseTime + (i * 150) + ((d == 1 ? tUp[i] : tDown[i]) * 1000); 
      seqLightState[i] = 0; seqTempState[i] = 0; 
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
    for (int i = 0; i < 6; i++) { runUntil[i] = 0; runDirection[i] = 0; webHoldHore[i] = false; webHoldDole[i] = false; isTargetingPos[i] = false; isMicroAdjusting[i] = false; seqLightState[i] = 0; seqTempState[i] = 0; motorTriggerTime[i] = 0; timeTiltState[i] = 0; }
    allHore = false; allDole = false; extBtnAutoActive = false; request->send(200, "text/plain", "OK");
  });

  server.on("/resetminmax", HTTP_GET, [](AsyncWebServerRequest *request) {
    String type = request->getParam("type")->value(); if (type == "int") resetIntMinMaxFlag = true; else if (type == "ext") resetExtMinMaxFlag = true;
    request->send(200, "text/plain", "OK");
  });

  server.on("/saveglobalauto", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("val")) globalAutoEnable = request->getParam("val")->value().toInt() == 1;
    if (request->hasParam("hstart")) autoHourStart = request->getParam("hstart")->value().toInt();
    if (request->hasParam("hend")) autoHourEnd = request->getParam("hend")->value().toInt();
    saveAllConfigurationToNVS(); request->send(200, "text/plain", "OK");
  });

  server.on("/saverow", HTTP_GET, [](AsyncWebServerRequest *request) {
    int i = request->getParam("i")->value().toInt();
    if (i >= 0 && i < 6) {
      if (request->hasParam("tu")) tUp[i] = request->getParam("tu")->value().toInt();
      if (request->hasParam("td")) tDown[i] = request->getParam("td")->value().toInt();
      if (request->hasParam("zOff")) zeroOffset[i] = request->getParam("zOff")->value().toInt(); 
      if (request->hasParam("ate")) autoTempEnable[i] = request->getParam("ate")->value().toInt() == 1;
      if (request->hasParam("tmd")) tempMode[i] = request->getParam("tmd")->value().toInt();
      if (request->hasParam("tlu")) tempLimitUp[i] = request->getParam("tlu")->value().toFloat();
      if (request->hasParam("tld")) tempLimitDown[i] = request->getParam("tld")->value().toFloat();
      if (request->hasParam("tta")) tTempUp[i] = request->getParam("tta")->value().toInt();
      if (request->hasParam("ttd")) tTempDown[i] = request->getParam("ttd")->value().toInt();
      if (request->hasParam("thyst")) tempHysteresis[i] = request->getParam("thyst")->value().toFloat();
      if (request->hasParam("ale")) autoLightEnable[i] = request->getParam("ale")->value().toInt() == 1;
      if (request->hasParam("lmd")) lightMode[i] = request->getParam("lmd")->value().toInt();
      if (request->hasParam("llu")) lightLimitUp[i] = request->getParam("llu")->value().toInt();
      if (request->hasParam("lld")) lightLimitDown[i] = request->getParam("lld")->value().toInt();
      if (request->hasParam("lhyst")) lightHysteresis[i] = request->getParam("lhyst")->value().toInt();
      if (request->hasParam("lta")) tLightUp[i] = request->getParam("lta")->value().toInt();
      if (request->hasParam("ltd")) tLightDown[i] = request->getParam("ltd")->value().toInt();
      if (request->hasParam("sqle")) seqLightEnable[i] = request->getParam("sqle")->value().toInt() == 1;
      if (request->hasParam("sqlp")) seqLightPauseTime[i] = request->getParam("sqlp")->value().toFloat();
      if (request->hasParam("sqld")) seqLightSecondActionDir[i] = request->getParam("sqld")->value().toInt();
      if (request->hasParam("sqlt")) seqLightSecondActionTime[i] = request->getParam("sqlt")->value().toFloat();
      if (request->hasParam("sqte")) seqTempEnable[i] = request->getParam("sqte")->value().toInt() == 1;
      if (request->hasParam("sqtp")) seqTempPauseTime[i] = request->getParam("sqtp")->value().toFloat();
      if (request->hasParam("sqtd")) seqTempSecondActionDir[i] = request->getParam("sqtd")->value().toInt();
      if (request->hasParam("sqtt")) seqTempSecondActionTime[i] = request->getParam("sqtt")->value().toFloat();
      for (int p = 0; p < 8; p++) { 
        char pParam[10];
        sprintf(pParam, "p%d", p); 
        if (request->hasParam(pParam)) tiltPresets[i][p] = request->getParam(pParam)->value().toInt(); 
      }
      saveAllConfigurationToNVS();
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/savetimetilt", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("i") && request->hasParam("p")) {
      int i = request->getParam("i")->value().toInt();
      int p = request->getParam("p")->value().toInt();
      if (i >= 0 && i < 6 && p >= 0 && p < 8) {
        if (request->hasParam("utt")) useTimeTilt[i] = request->getParam("utt")->value().toInt() == 1;
        if (request->hasParam("d1")) timeTiltDir1[i][p] = request->getParam("d1")->value().toInt();
        if (request->hasParam("t1")) timeTiltDur1[i][p] = request->getParam("t1")->value().toFloat();
        if (request->hasParam("pz")) timeTiltPause[i][p] = request->getParam("pz")->value().toFloat();
        if (request->hasParam("d2")) timeTiltDir2[i][p] = request->getParam("d2")->value().toInt();
        if (request->hasParam("t2")) timeTiltDur2[i][p] = request->getParam("t2")->value().toFloat();
        saveAllConfigurationToNVS();
      }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/savecalib", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("wlim")) windLimit = request->getParam("wlim")->value().toFloat();
    if (request->hasParam("wp25")) windPulsesFor25kmh = request->getParam("wp25")->value().toFloat();
    if (request->hasParam("ppturn")) pulsesPerTurn = request->getParam("ppturn")->value().toInt();
    if (request->hasParam("wsafedur")) windSafetyDuration = request->getParam("wsafedur")->value().toInt() * 1000;
    if (request->hasParam("wruntime")) windRunTime = request->getParam("wruntime")->value().toInt();
    if (request->hasParam("wmaxhits")) windMaxHits = request->getParam("wmaxhits")->value().toInt();
    if (request->hasParam("mcth")) microAdjustThreshold = request->getParam("mcth")->value().toInt();
    if (request->hasParam("etu1")) extTimeUpCH1 = request->getParam("etu1")->value().toInt();
    if (request->hasParam("etd1")) extTimeDownCH1 = request->getParam("etd1")->value().toInt();
    if (request->hasParam("ldrsmp")) ldrSamples = request->getParam("ldrsmp")->value().toInt();
    if (request->hasParam("ldrflt")) ldrFilter = request->getParam("ldrflt")->value().toFloat();
    if (request->hasParam("ntciR0")) ntcIntR0 = request->getParam("ntciR0")->value().toInt();
    if (request->hasParam("ntciB")) ntcIntBeta = request->getParam("ntciB")->value().toInt();
    if (request->hasParam("ntciSR")) ntcIntSeriesR = request->getParam("ntciSR")->value().toFloat();
    if (request->hasParam("ntciSmp")) ntcIntSamples = request->getParam("ntciSmp")->value().toInt();
    if (request->hasParam("ntciFlt")) ntcIntFilter = request->getParam("ntciFlt")->value().toFloat();
    if (request->hasParam("ntciOff")) ntcIntOffset = request->getParam("ntciOff")->value().toFloat();
    if (request->hasParam("ntceR0")) ntcExtR0 = request->getParam("ntceR0")->value().toInt();
    if (request->hasParam("ntceB")) ntcExtBeta = request->getParam("ntceB")->value().toInt();
    if (request->hasParam("ntceSR")) ntcExtSeriesR = request->getParam("ntceSR")->value().toFloat();
    if (request->hasParam("ntceSmp")) ntcExtSamples = request->getParam("ntceSmp")->value().toInt();
    if (request->hasParam("ntceFlt")) ntcExtFilter = request->getParam("ntceFlt")->value().toFloat();
    if (request->hasParam("ntceOff")) ntcExtOffset = request->getParam("ntceOff")->value().toFloat();
    saveAllConfigurationToNVS(); 
    request->send(200, "text/plain", "OK");
  });
  server.begin();
}

void loop() {
  unsigned long now = millis();
  if (resetWindMaxFlag) { windSpeedMax = 0.0; resetWindMaxFlag = false; }
  if (resetIntMinMaxFlag) { tempIntMin = tempInt; tempIntMax = tempInt; resetIntMinMaxFlag = false; }
  if (resetExtMinMaxFlag) { tempExtMin = tempExt; tempExtMax = tempExt; resetExtMinMaxFlag = false; }

  updateBlindPositions();

  // --- LOGIKA MECHANICKYCH EXTERNYCH TLACIDIEL CH1 (HOLD VS DOUBLE CLICK CHOD) ---
  int rawUp = digitalRead(EXT_UP_CH1);
  int rawDown = digitalRead(EXT_DOWN_CH1);

  int upHold = 0; int upDoubleClick = 0;
  int downHold = 0; int downDoubleClick = 0;
  processButtonStateMachine(rawUp, btnUpSM, upHold, upDoubleClick);
  processButtonStateMachine(rawDown, btnDownSM, downHold, downDoubleClick);

  if (!safetyActive) {
    if (extBtnAutoActive) {
      if (rawUp == LOW || rawDown == LOW) {
        runUntil[0] = 0;
        runDirection[0] = 0;
        extBtnAutoActive = false;
        btnUpSM.clickCount = 0;
        btnDownSM.clickCount = 0;
        seqLightState[0] = 0;
        seqTempState[0] = 0;
        timeTiltState[0] = 0; 
      }
    } else {
      if (upDoubleClick && rawDown == HIGH) {
        isTargetingPos[0] = false;
        timeTiltState[0] = 0; 
        extBtnAutoActive = true;
        runUntil[0] = now + ((unsigned long)extTimeUpCH1 * 1000);
        runDirection[0] = 1;
      } else if (downDoubleClick && rawUp == HIGH) {
        isTargetingPos[0] = false;
        timeTiltState[0] = 0; 
        extBtnAutoActive = true;
        runUntil[0] = now + ((unsigned long)extTimeDownCH1 * 1000);
        runDirection[0] = 2;
      }
      else if (upHold && rawDown == HIGH) {
        isTargetingPos[0] = false;
        timeTiltState[0] = 0; 
        runUntil[0] = now + 200;
        runDirection[0] = 1;
        seqLightState[0] = 0;
        seqTempState[0] = 0;
      } else if (downHold && rawUp == HIGH) {
        isTargetingPos[0] = false;
        timeTiltState[0] = 0; 
        runUntil[0] = now + 200;
        runDirection[0] = 2;
        seqLightState[0] = 0;
        seqTempState[0] = 0;
      }
    }
  }

  // --- NEBLOKUJUCI AUTOMATICKY WIFI WATCHDOG ---
  static unsigned long lastWiFiCheck = 0;
  if (now - lastWiFiCheck >= 15000) { 
    lastWiFiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Stratene pripojenie k sieti! Pokus o automaticke znovupripojenie...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }


  // --- V84 FIX: AUTOMATICKE UKLADANIE DO NVS KAZDU MINUTU ---
  if (now - lastNVSSaveMillis >= NVS_AUTO_SAVE_INTERVAL) {
    lastNVSSaveMillis = now;
    saveAllConfigurationToNVS();
    Serial.println("[NVS] Auto-save: parametre uspesne ulozene do Preferences.");
  }

  if (now - lastSensorMillis >= 1000) {
    unsigned long durationMs = now - lastSensorMillis;
    lastSensorMillis = now;
    noInterrupts(); unsigned long pulses = pulseCount; pulseCount = 0; interrupts();
    float currentWindSpeed = 0.0;
    if (pulses > 0 && durationMs > 0) { float rps = (float)pulses / ((float)pulsesPerTurn * (durationMs / 1000.0));
    currentWindSpeed = rps * (25.0 / (windPulsesFor25kmh / (float)pulsesPerTurn)); }
    windSpeed = windSpeed + 0.5 * (currentWindSpeed - windSpeed);
    if (windSpeed > windSpeedMax) windSpeedMax = windSpeed;
    float maxFound24h = 0.0;
    for (int i = 0; i < graphCount; i++) { if (historyWind[i] > maxFound24h) maxFound24h = historyWind[i]; }
    if (windSpeed > maxFound24h) maxFound24h = windSpeed; windSpeedMax24h = maxFound24h;
    lightLevelFiltered = readLDRRolling(LIGHT_PIN, ldrSamples, ldrFilter, lightLevelFiltered, ldrRollBuf, ldrRollCount); lightLevel = (int)lightLevelFiltered;
    tempInt = readNTCRolling(NTC_INT_PIN, ntcIntR0, ntcIntBeta, ntcIntSeriesR, ntcIntSamples, ntcIntFilter, ntcIntOffset, tempInt, ntcIntRollBuf, ntcIntRollCount);
    tempExt = readNTCRolling(NTC_EXT_PIN, ntcExtR0, ntcExtBeta, ntcExtSeriesR, ntcExtSamples, ntcExtFilter, ntcExtOffset, tempExt, ntcExtRollBuf, ntcExtRollCount);
    checkAndSaveMinMax(); wifiRSSI = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;

    if (windSpeed >= windLimit) {
      windHitCounter++;
      if (windHitCounter >= windMaxHits && !safetyActive) { 
        safetyActive = true;
        windSafetyUntil = now + windSafetyDuration; windRunUntil = now + ((unsigned long)windRunTime * 1000); sendWindAlarmEmail(windSpeed, windLimit);
        for (int i = 0; i < 6; i++) { motorTriggerTime[i] = now + (i * 150); } 
      }
    } else { if (windHitCounter > 0) windHitCounter--; }
    if (safetyActive && now >= windSafetyUntil && windSpeed < windLimit) { safetyActive = false; emailSentForCurrentAlarm = false; }
    
    if (lightState && lightOffTarget > 0 && now >= lightOffTarget) { lightState = false; lightOffTarget = 0; lightTimerMinutes = 0; }
    if (light2State && light2OffTarget > 0 && now >= light2OffTarget) { light2State = false; light2OffTarget = 0; light2TimerMinutes = 0; }
  }

  struct tm timeinfo; bool timeValid = getLocalTime(&timeinfo);
  if (timeValid) {
    int currentMin = timeinfo.tm_min;
    if ((currentMin == 0 || currentMin == 15 || currentMin == 30 || currentMin == 45) && currentMin != lastLoggedMinute) {
      lastLoggedMinute = currentMin;
      if (graphInitialized) addGraphRecord(tempInt, tempExt, lightLevelFiltered, windSpeed);
    }

    if (graphInitialized && historyTime[0] == "--:--") {
      int baseHour = timeinfo.tm_hour;
      int baseMin = (timeinfo.tm_min / 15) * 15;
      for (int i = 0; i < GRAPH_SIZE; i++) {
        int minsBack = (GRAPH_SIZE - 1 - i) * 15;
        int currentTotalMins = baseHour * 60 + baseMin - minsBack;
        while (currentTotalMins < 0) currentTotalMins += 24 * 60;
        int h = (currentTotalMins / 60) % 24;
        int m = currentTotalMins % 60;
        char buf[10];
        sprintf(buf, "%02d:%02d", h, m);
        historyTime[i] = String(buf);
      }
      Serial.println("[GRAFY] Spatna synchronizacia casovych znaciek uspesna.");
    }
  }

  if (!graphInitialized && (now - bootMillis > 60000)) {
    initGraphsWithCurrentValues();
    tempIntMin = tempInt; tempIntMax = tempInt;
    tempExtMin = tempExt; tempExtMax = tempExt;
  }

  for (int i = 0; i < 6; i++) {
    if (isTargetingPos[i] && !safetyActive) {
      int32_t posDiff = targetPosition[i] - getEffectivePosition(i);
      int32_t absDiff = abs(posDiff);
      if (absDiff <= 12) { isTargetingPos[i] = false; runDirection[i] = 0; runUntil[i] = 0; isMicroAdjusting[i] = false; } 
      else { runDirection[i] = (posDiff > 0) ? 2 : 1; runUntil[i] = now + 2000; isMicroAdjusting[i] = (absDiff < (int32_t)microAdjustThreshold); }
    }
  }

  // --- AUTOMAT VYHODNOTENIA DVOJFAZOVEHO CASOVEHO NAKLAPANIA (PRESETY P1-P8) ---
  for (int i = 0; i < 6; i++) {
    if (timeTiltState[i] > 0 && !safetyActive) {
      int p = timeTiltPresetIndex[i];
      if (timeTiltState[i] == 1) { 
        if (millis() >= timeTiltTimer[i]) {
          timeTiltState[i] = 2;
          timeTiltTimer[i] = millis() + (unsigned long)(timeTiltPause[i][p] * 1000.0);
          runDirection[i] = 0;
          runUntil[i] = 0;
        } else {
          runDirection[i] = timeTiltDir1[i][p];
          runUntil[i] = timeTiltTimer[i];
        }
      } 
      else if (timeTiltState[i] == 2) { 
        if (millis() >= timeTiltTimer[i]) {
          float dur2 = timeTiltDur2[i][p];
          if (dur2 > 0.01) {
            timeTiltState[i] = 3;
            timeTiltTimer[i] = millis() + (unsigned long)(dur2 * 1000.0);
            runDirection[i] = timeTiltDir2[i][p];
            runUntil[i] = timeTiltTimer[i];
          } else {
            timeTiltState[i] = 0;
            runDirection[i] = 0;
            runUntil[i] = 0;
          }
        } else {
          runDirection[i] = 0;
          runUntil[i] = 0;
        }
      } 
      else if (timeTiltState[i] == 3) { 
        if (millis() >= timeTiltTimer[i]) {
          timeTiltState[i] = 0;
          runDirection[i] = 0;
          runUntil[i] = 0;
        } else {
          runDirection[i] = timeTiltDir2[i][p];
          runUntil[i] = timeTiltTimer[i];
        }
      }
    }
  }

  bool withinTimeWindow = true;
  if (timeValid) {
    if (autoHourStart <= autoHourEnd) withinTimeWindow = (timeinfo.tm_hour >= autoHourStart && timeinfo.tm_hour < autoHourEnd);
    else withinTimeWindow = (timeinfo.tm_hour >= autoHourStart || timeinfo.tm_hour < autoHourEnd);
  }

  if (globalAutoEnable && withinTimeWindow && !safetyActive && !extBtnAutoActive) {
    for (int i = 0; i < 6; i++) {
      if (!isTargetingPos[i] && timeTiltState[i] == 0) {
        // --- SVETELNA AUTOMATIKA ---
        if (autoLightEnable[i] && lightMode[i] == 1 && runDirection[i] == 0 && seqLightState[i] == 0) {
          if (lightLevel >= (lightLimitDown[i] + lightHysteresis[i]) && !lightActionTriggered[i]) { 
            unsigned long dur = (unsigned long)tLightDown[i] * 1000; 
            runUntil[i] = now + dur; 
            runDirection[i] = 2; 
            lightActionTriggered[i] = true;
            if (seqLightEnable[i]) { 
              seqLightState[i] = 1;
              seqLightTimer[i] = now + dur + ((unsigned long)seqLightPauseTime[i] * 1000);
            } 
          }
          else if (lightLevel <= (lightLimitUp[i] - lightHysteresis[i]) && lightActionTriggered[i]) { 
            unsigned long dur = (unsigned long)tLightUp[i] * 1000;
            runUntil[i] = now + dur; 
            runDirection[i] = 1; 
            lightActionTriggered[i] = false;
            if (seqLightEnable[i]) { 
              seqLightState[i] = 1;
              seqLightTimer[i] = now + dur + ((unsigned long)seqLightPauseTime[i] * 1000);
            } 
          }
        }
        
        if (seqLightEnable[i]) {
          if (seqLightState[i] == 1 && now >= seqLightTimer[i]) {
            if (!safetyActive) {
              unsigned long dur2 = (unsigned long)(seqLightSecondActionTime[i] * 1000.0);
              runDirection[i] = seqLightSecondActionDir[i];
              runUntil[i] = now + dur2;
              seqLightState[i] = 2;
              seqLightTimer[i] = now + dur2;
            } else {
              seqLightState[i] = 0;
            }
          }
          else if (seqLightState[i] == 2 && now >= seqLightTimer[i]) {
            seqLightState[i] = 0;
            if (runDirection[i] == seqLightSecondActionDir[i]) {
              runDirection[i] = 0;
              runUntil[i] = 0;
            }
          }
        }
        
        // --- TEPLOTNA AUTOMATIKA ---
        if (autoTempEnable[i] && (tempMode[i] == 1 || tempMode[i] == 2)) {
          if (runDirection[i] == 0 && seqLightState[i] == 0 && seqTempState[i] == 0) {
            float tCurrent = (tempMode[i] == 1) ? tempInt : tempExt;
            if (tCurrent > -50.0 && tCurrent < 100.0) {
              if (tCurrent >= (tempLimitDown[i] + tempHysteresis[i]) && !tempActionTriggered[i]) { 
                unsigned long dur = (unsigned long)tTempDown[i] * 1000;
                runUntil[i] = now + dur; 
                runDirection[i] = 2; 
                tempActionTriggered[i] = true;
                if (seqTempEnable[i]) { 
                  seqTempState[i] = 1;
                  seqTempTimer[i] = now + dur + ((unsigned long)seqTempPauseTime[i] * 1000);
                } 
              }
              else if (tCurrent <= (tempLimitUp[i] - tempHysteresis[i]) && tempActionTriggered[i]) { 
                unsigned long dur = (unsigned long)tLightUp[i] * 1000; // Zachovanie unifikovanej dlzky pre tTempUp
                runUntil[i] = now + dur; 
                runDirection[i] = 1; 
                tempActionTriggered[i] = false;
                if (seqTempEnable[i]) { 
                  seqTempState[i] = 1;
                  seqTempTimer[i] = now + dur + ((unsigned long)seqTempPauseTime[i] * 1000);
                } 
              }
            }
          }
        }
        
        if (seqTempEnable[i]) {
          if (seqTempState[i] == 1 && now >= seqTempTimer[i]) {
            if (!safetyActive) {
              unsigned long dur2 = (unsigned long)(seqTempSecondActionTime[i] * 1000.0);
              runDirection[i] = seqTempSecondActionDir[i];
              runUntil[i] = now + dur2;
              seqTempState[i] = 2;
              seqTempTimer[i] = now + dur2;
            } else {
              seqTempState[i] = 0;
            }
          }
          else if (seqTempState[i] == 2 && now >= seqTempTimer[i]) {
            seqTempState[i] = 0;
            if (runDirection[i] == seqTempSecondActionDir[i]) {
              runDirection[i] = 0;
              runUntil[i] = 0;
            }
          }
        }
      }
    }
  }

  for (int i = 0; i < 6; i++) {
    bool moveUp = false;
    bool moveDown = false;
    
    if (i == 0 && !extBtnAutoActive && (rawUp == LOW || rawDown == LOW)) {
      if (runDirection[0] == 1) moveUp = true;
      if (runDirection[0] == 2) moveDown = true;
    } else {
      if (allHore) moveUp = true;
      else if (allDole) moveDown = true;
      else if (webHoldHore[i]) moveUp = true; else if (webHoldDole[i]) moveDown = true;
      else if (runUntil[i] > now) { if (runDirection[i] == 1) moveUp = true; if (runDirection[i] == 2) moveDown = true; } 
      else { if (runDirection[i] != 0 && !isTargetingPos[i] && timeTiltState[i] == 0) runDirection[i] = 0; }
    }
    
    int desiredDirection = 0;
    if (safetyActive && (windSpeed >= windLimit || now < windRunUntil)) desiredDirection = 1;
    else { if (moveUp && moveDown) desiredDirection = 0; else if (moveUp) desiredDirection = 1; else if (moveDown) desiredDirection = 2; }
    
    if (motorTriggerTime[i] > 0) {
      if (now >= motorTriggerTime[i]) { motorTriggerTime[i] = 0; } 
      else { desiredDirection = 0; }
    }

    if (desiredDirection != lastDirection[i] && lastDirection[i] != 0 && desiredDirection != 0) { 
      safeSwitchMillis[i] = now;
      lastDirection[i] = 0; runUntil[i] = 0; runDirection[i] = 0; isTargetingPos[i] = false; timeTiltState[i] = 0; desiredDirection = 0;
    }
    if (desiredDirection != 0 && lastDirection[i] == 0) { if (now - safeSwitchMillis[i] < DIRECTION_DELAY) desiredDirection = 0; }
    if (desiredDirection == 0) { if (lastDirection[i] != 0) { safeSwitchMillis[i] = now; lastDirection[i] = 0; } } 
    else lastDirection[i] = desiredDirection;
  }

  flushRelays();
}
