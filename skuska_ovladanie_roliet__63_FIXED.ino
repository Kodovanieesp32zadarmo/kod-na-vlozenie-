// ====================================================================================
// PROJEKT: INTELIGENTNE OVLADANIE 6 ZALUZII S JEMNYM NAKLAPANIM S DIODOU S VIRTUALNOU 0
// VERZIA: V84 - INDUSTRIAL SHIELD FIX (FIX PRELUSY, POMALA RYCHLOST V KROKU 3, NVS PERSIST)
// ARCHITEKTURA: ESP32 WROOM + DUAL I2C + 2x PCA9548A + 2x PCF8574T + 6x SA5600
// LOGIKA S BC337: Pokoja / Start = 0x00 (Vsetky rele bezpecne rozpojene and vypnute)
// FIX V84: 
//   1. POSLEDNY MANEVER (KROK 3) POUZIVA BIT 6 (SLOW SPEED) NA PCF2
//   2. DLHSI IZOLACNY CAS (200us) + SYNCHRONNE FLUSH PRE ELIMINU PRELUCHY
//   3. AUTOMATICKE UKLADANIE DO NVS KAZDU MINUTU (NIE LEN NA FLAG)
//   4. ZJEDNOTENE PORADIE: PCF1 (Z1-3): BIT 0=UP1,1=DN1,2=UP2,3=DN2,4=UP3,5=DN3,7=LIGHT2
//                         PCF2 (Z4-6): BIT 0=UP4,1=DN4,2=UP5,3=DN5,4=UP6,5=DN6,6=SLOW,7=LIGHT1
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
const unsigned long NVS_AUTO_SAVE_INTERVAL = 60000; // Automaticke ukladanie kazdu minutu

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
unsigned long lastNVSSaveMillis = 0;  // NOVA: Citac pre automaticke ukladanie
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
bool useTimeTilt[6] = {false, false, false, false, false, false};
int timeTiltDir1[6][8];
float timeTiltDur1[6][8];
float timeTiltPause[6][8];
int timeTiltDir2[6][8];
float timeTiltDur2[6][8];

int timeTiltPresetIndex[6] = {0, 0, 0, 0, 0, 0};
int timeTiltState[6] = {0, 0, 0, 0, 0, 0};
unsigned long timeTiltTimer[6] = {0, 0, 0, 0, 0, 0};

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

// --- EXPLICITNE DOPREDNE DEKLARACIE FUNKCII ---
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

