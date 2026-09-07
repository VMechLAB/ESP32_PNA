/*
 * ESP32 Pocket Network Analyzer - FULLY COMPILABLE
 * Uses PROGMEM for flash strings, no F() macro with U8g2
 */

#include <U8g2lib.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <RF24.h>

// ---------- Pin Definitions ----------
#define OLED_SDA   21
#define OLED_SCL   22
#define NRF_CE     4
#define NRF_CSN    5
#define BUTTON_UP  34
#define BUTTON_DOWN 35
#define BUTTON_SEL 32
#define BUTTON_BACK 33

// ---------- Display ----------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ---------- NRF24 ----------
RF24 radio(NRF_CE, NRF_CSN);

// ---------- Button Debounce ----------
const unsigned long DEBOUNCE_MS = 50;
unsigned long lastButtonTime = 0;
bool btnUp = false, btnDown = false, btnSel = false, btnBack = false;
bool lastBtnUp = HIGH, lastBtnDown = HIGH, lastBtnSel = HIGH, lastBtnBack = HIGH;

// ---------- State Machine ----------
enum AppState { MENU, WIFI_SCANNING, WIFI_RESULTS, BLE_SCANNING, BLE_RESULTS, NRF_SCANNING, NRF_RESULTS };
AppState state = MENU;
int menuIndex = 0;

// ---------- Optimized Result Storage ----------
const int MAX_RESULTS = 12;
const int RESULT_LEN = 24;

char wifiResults[MAX_RESULTS][RESULT_LEN];
int wifiCount = 0;
char bleResults[MAX_RESULTS][RESULT_LEN];
int bleCount = 0;
char nrfResults[MAX_RESULTS][RESULT_LEN];
int nrfCount = 0;
int resultScroll = 0;

// ---------- Menu Strings in PROGMEM ----------
const char menu1[] PROGMEM = "1. Wi-Fi Scan";
const char menu2[] PROGMEM = "2. BLE Scan";
const char menu3[] PROGMEM = "3. NRF24 Scan";
const char menu4[] PROGMEM = "4. About";
const char* const menuItems[] PROGMEM = {menu1, menu2, menu3, menu4};
const int menuCount = 4;

// ---------- Static Strings in PROGMEM ----------
const char strMainMenu[] PROGMEM = "== MAIN MENU ==";
const char strArrow[] PROGMEM = ">";
const char strAbout1[] PROGMEM = "ESP32 Analyzer";
const char strAbout2[] PROGMEM = "WiFi/BLE/NRF24";
const char strAbout3[] PROGMEM = "0.96 OLED";
const char strAbout4[] PROGMEM = "4 buttons";
const char strAbout5[] PROGMEM = "Press BACK";
const char strScanWiFi[] PROGMEM = "Scanning WiFi...";
const char strScanBLE[] PROGMEM = "Scanning BLE...";
const char strScanNRF[] PROGMEM = "Scanning NRF24...";
const char strNoAP[] PROGMEM = "No AP found";
const char strNoActivity[] PROGMEM = "No activity";
const char strHidden[] PROGMEM = "<hidden>";
const char strUnknown[] PROGMEM = "Unknown";

// Buffer for copying strings from PROGMEM
char buffer[32];

// ---------- Function Prototypes ----------
void readButtons();
void displayMenu();
void displayResults(char results[][RESULT_LEN], int count, int scroll);
void scanWiFi();
void scanBLE();
void scanNRF24();
void showAbout();

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setFontPosTop();

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SEL, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);

  if (!radio.begin()) {
    Serial.println("NRF24 init failed!");
  } else {
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.stopListening();
  }

  BLEDevice::init("ESP32_Scanner");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.println("Ready.");
}

// ---------- Main Loop ----------
void loop() {
  readButtons();

  switch (state) {
    case MENU:
      displayMenu();
      if (btnSel) {
        switch (menuIndex) {
          case 0: state = WIFI_SCANNING; break;
          case 1: state = BLE_SCANNING; break;
          case 2: state = NRF_SCANNING; break;
          case 3: showAbout(); state = MENU; break;
        }
      }
      break;

    case WIFI_SCANNING: scanWiFi(); state = WIFI_RESULTS; resultScroll = 0; break;
    case WIFI_RESULTS:
      displayResults(wifiResults, wifiCount, resultScroll);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < wifiCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;

    case BLE_SCANNING: scanBLE(); state = BLE_RESULTS; resultScroll = 0; break;
    case BLE_RESULTS:
      displayResults(bleResults, bleCount, resultScroll);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < bleCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;

    case NRF_SCANNING: scanNRF24(); state = NRF_RESULTS; resultScroll = 0; break;
    case NRF_RESULTS:
      displayResults(nrfResults, nrfCount, resultScroll);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < nrfCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;
  }
  delay(50);
}

// ---------- Button Reading ----------
void readButtons() {
  unsigned long now = millis();
  if (now - lastButtonTime < DEBOUNCE_MS) return;

  bool curUp = digitalRead(BUTTON_UP);
  bool curDown = digitalRead(BUTTON_DOWN);
  bool curSel = digitalRead(BUTTON_SEL);
  bool curBack = digitalRead(BUTTON_BACK);

  btnUp = (lastBtnUp == HIGH && curUp == LOW);
  btnDown = (lastBtnDown == HIGH && curDown == LOW);
  btnSel = (lastBtnSel == HIGH && curSel == LOW);
  btnBack = (lastBtnBack == HIGH && curBack == LOW);

  lastBtnUp = curUp; lastBtnDown = curDown; lastBtnSel = curSel; lastBtnBack = curBack;
  if (btnUp || btnDown || btnSel || btnBack) lastButtonTime = now;
}

// ---------- Display Functions ----------
void displayMenu() {
  u8g2.clearBuffer();
  
  strcpy_P(buffer, strMainMenu);
  u8g2.drawStr(0, 0, buffer);
  
  for (int i = 0; i < menuCount; i++) {
    int y = 14 + i * 12;
    if (i == menuIndex) {
      strcpy_P(buffer, strArrow);
      u8g2.drawStr(0, y, buffer);
    }
    const char* menuItem = (const char*)pgm_read_ptr(&menuItems[i]);
    strcpy_P(buffer, menuItem);
    u8g2.drawStr(10, y, buffer);
  }
  u8g2.sendBuffer();
  
  if (btnUp) menuIndex = (menuIndex - 1 + menuCount) % menuCount;
  if (btnDown) menuIndex = (menuIndex + 1) % menuCount;
}

void displayResults(char results[][RESULT_LEN], int count, int scroll) {
  u8g2.clearBuffer();
  char header[16];
  snprintf(header, sizeof(header), "Results (%d)", count);
  u8g2.drawStr(0, 0, header);
  
  int maxLines = 5;
  int start = scroll;
  int end = min(start + maxLines - 1, count);
  for (int i = start; i < end; i++) {
    u8g2.drawStr(0, 12 + (i - start) * 10, results[i]);
  }
  u8g2.sendBuffer();
}

void showAbout() {
  u8g2.clearBuffer();
  
  strcpy_P(buffer, strAbout1); u8g2.drawStr(0, 0, buffer);
  strcpy_P(buffer, strAbout2); u8g2.drawStr(0, 14, buffer);
  strcpy_P(buffer, strAbout3); u8g2.drawStr(0, 28, buffer);
  strcpy_P(buffer, strAbout4); u8g2.drawStr(0, 42, buffer);
  strcpy_P(buffer, strAbout5); u8g2.drawStr(0, 56, buffer);
  
  u8g2.sendBuffer();
  while (digitalRead(BUTTON_BACK) == HIGH) delay(10);
  delay(50);
}

// ---------- Scanning Functions ----------
void scanWiFi() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strScanWiFi);
  u8g2.drawStr(0, 0, buffer);
  u8g2.sendBuffer();

  wifiCount = 0;
  int n = WiFi.scanComplete();
  if (n == -2 || n == -1) {
    WiFi.scanNetworks(true);
    delay(3000);
    n = WiFi.scanComplete();
  }

  if (n > 0) {
    wifiCount = min(n, MAX_RESULTS);
    for (int i = 0; i < wifiCount; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) {
        strcpy_P(buffer, strHidden);
        ssid = String(buffer);
      }
      int rssi = WiFi.RSSI(i);
      snprintf(wifiResults[i], RESULT_LEN, "%-12s %3ddBm", ssid.c_str(), rssi);
    }
  } else {
    wifiCount = 1;
    strcpy_P(wifiResults[0], strNoAP);
  }
  WiFi.scanDelete();
}

void scanBLE() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strScanBLE);
  u8g2.drawStr(0, 0, buffer);
  u8g2.sendBuffer();

  bleCount = 0;
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  BLEScanResults* foundDevices = pBLEScan->start(3, false);
  int count = foundDevices->getCount();
  bleCount = min(count, MAX_RESULTS);
  
  for (int i = 0; i < bleCount; i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    String name = device.getName().c_str();
    if (name.length() == 0) {
      strcpy_P(buffer, strUnknown);
      name = String(buffer);
    }
    int rssi = device.getRSSI();
    snprintf(bleResults[i], RESULT_LEN, "%-10s %3ddBm", name.c_str(), rssi);
  }
  pBLEScan->clearResults();
}

void scanNRF24() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strScanNRF);
  u8g2.drawStr(0, 0, buffer);
  u8g2.sendBuffer();

  nrfCount = 0;
  radio.stopListening();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setPayloadSize(32);

  for (int ch = 0; ch <= 125 && nrfCount < MAX_RESULTS; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    delayMicroseconds(500);
    
    unsigned long start = millis();
    bool detected = false;
    while (millis() - start < 5) {
      if (radio.available()) {
        detected = true;
        uint8_t dummy[32];
        radio.read(&dummy, 1);
        break;
      }
    }
    radio.stopListening();
    
    if (detected) {
      snprintf(nrfResults[nrfCount], RESULT_LEN, "Ch %3d active", ch);
      nrfCount++;
    }
    delay(1);
  }

  if (nrfCount == 0) {
    strcpy_P(nrfResults[0], strNoActivity);
    nrfCount = 1;
  }
}
