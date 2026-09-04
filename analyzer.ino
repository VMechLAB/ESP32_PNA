/*
 * ESP32 Pocket Network Analyzer
 * Made by: VMechLAB; Vasilije Jovanovic
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <RF24.h>

//Pin Definitions
#define OLED_SDA   21
#define OLED_SCL   22
#define NRF_CE     4
#define NRF_CSN    5
#define BUTTON_UP  34
#define BUTTON_DOWN 35
#define BUTTON_SEL 32
#define BUTTON_BACK 33

U8G2_SSD1306_128x64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

RF24 radio(NRF_CE, NRF_CSN);

const unsigned long DEBOUNCE_MS = 50;
unsigned long lastButtonTime = 0;

bool btnUp, btnDown, btnSel, btnBack;
bool lastBtnUp = HIGH, lastBtnDown = HIGH, lastBtnSel = HIGH, lastBtnBack = HIGH;

//State Machine
enum AppState {
  MENU,
  WIFI_SCANNING, WIFI_RESULTS,
  BLE_SCANNING, BLE_RESULTS,
  NRF_SCANNING, NRF_RESULTS
};
AppState state = MENU;

// Menu items
const char* menuItems[] = { "1. Wi-Fi Scan", "2. BLE Scan", "3. NRF24 Scan", "4. About" };
const int menuCount = 4;
int menuIndex = 0;

// Results storage
const int MAX_WIFI = 20;
const int MAX_BLE = 20;
const int MAX_NRF = 20;

String wifiResults[MAX_WIFI];
int wifiCount = 0;

String bleResults[MAX_BLE];
int bleCount = 0;

String nrfResults[MAX_NRF];
int nrfCount = 0;

int resultScroll = 0;
bool backToMenu = false;

void readButtons();
void displayMenu();
void displayResults(const String results[], int count, int scroll);
void scanWiFi();
void scanBLE();
void scanNRF24();

void setup() {
  Serial.begin(115200);

  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setBusClock(400000);

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SEL, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);

  if (!radio.begin()) {
    Serial.println("NRF24 init failed!");
  } else {
    radio.setChannel(0);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.startListening();
    radio.stopListening();
  }

  BLEDevice::init("ESP32_Scanner");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("Ready.");
}

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

    case WIFI_SCANNING:
      scanWiFi();
      state = WIFI_RESULTS;
      resultScroll = 0;
      break;

    case WIFI_RESULTS:
      displayResults(wifiResults, wifiCount, resultScroll);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < wifiCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;

    case BLE_SCANNING:
      scanBLE();
      state = BLE_RESULTS;
      resultScroll = 0;
      break;

    case BLE_RESULTS:
      displayResults(bleResults, bleCount, resultScroll);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < bleCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;

    case NRF_SCANNING:
      scanNRF24();
      state = NRF_RESULTS;
      resultScroll = 0;
      break;

    case NRF_RESULTS:
      displayResults(nrfResults, nrfCount, resultScroll);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < nrfCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;
  }

  delay(50);
}

//Button Reading (with debounce)
void readButtons() {
  unsigned long now = millis();
  if (now - lastButtonTime < DEBOUNCE_MS) return;

  bool curUp = digitalRead(BUTTON_UP);
  bool curDown = digitalRead(BUTTON_DOWN);
  bool curSel = digitalRead(BUTTON_SEL);
  bool curBack = digitalRead(BUTTON_BACK);

  // Detect falling edge (active LOW)
  btnUp = (lastBtnUp == HIGH && curUp == LOW);
  btnDown = (lastBtnDown == HIGH && curDown == LOW);
  btnSel = (lastBtnSel == HIGH && curSel == LOW);
  btnBack = (lastBtnBack == HIGH && curBack == LOW);

  lastBtnUp = curUp;
  lastBtnDown = curDown;
  lastBtnSel = curSel;
  lastBtnBack = curBack;

  if (btnUp || btnDown || btnSel || btnBack) {
    lastButtonTime = now;
  }
}

//Display Functions
void displayMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 0, "== MAIN MENU ==");

  for (int i = 0; i < menuCount; i++) {
    int y = 14 + i * 12;
    if (i == menuIndex) {
      u8g2.drawStr(0, y, ">");
    }
    u8g2.drawStr(10, y, menuItems[i]);
  }
  u8g2.sendBuffer();

  // Menu navigation
  if (btnUp) menuIndex = (menuIndex - 1 + menuCount) % menuCount;
  if (btnDown) menuIndex = (menuIndex + 1) % menuCount;
}

void displayResults(const String results[], int count, int scroll) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  int lines = 0;
  int maxLines = 5; // 64/12 ≈ 5 lines

  String header = "Results (" + String(count) + ")";
  u8g2.drawStr(0, 0, header.c_str());
  lines++;

  if (count > maxLines) {
    u8g2.drawStr(110, 0, (scroll < count - maxLines) ? "v" : " ");
  }

  int start = scroll;
  int end = min(start + maxLines - 1, count);
  for (int i = start; i < end; i++) {
    int y = 12 + (i - start) * 10;
    u8g2.drawStr(0, y, results[i].c_str());
  }
  u8g2.sendBuffer();
}

void showAbout() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 0, "ESP32 Analyzer");
  u8g2.drawStr(0, 14, "WiFi/BLE/NRF24");
  u8g2.drawStr(0, 28, "0.96 OLED");
  u8g2.drawStr(0, 42, "4 buttons");
  u8g2.drawStr(0, 56, "Press BACK");
  u8g2.sendBuffer();

  while (digitalRead(BUTTON_BACK) == HIGH) delay(10);
  delay(50);
}

//Scanning Functions
void scanWiFi() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 0, "Scanning WiFi...");
  u8g2.sendBuffer();

  wifiCount = 0;
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
    delay(2000);
    n = WiFi.scanComplete();
  }
  if (n == -1) {
    // start new scan
    WiFi.scanNetworks(true);
    delay(3000);
    n = WiFi.scanComplete();
  }

  if (n > 0) {
    wifiCount = min(n, MAX_WIFI);
    for (int i = 0; i < wifiCount; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) ssid = "<hidden>";
      int rssi = WiFi.RSSI(i);
      int ch = WiFi.channel(i);
      String enc = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SEC";
      wifiResults[i] = ssid.substring(0, 12) + " " + String(rssi) + "dBm";
    }
  } else {
    wifiCount = 1;
    wifiResults[0] = "No AP found";
  }
  WiFi.scanDelete();
}

void scanBLE() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 0, "Scanning BLE...");
  u8g2.sendBuffer();

  bleCount = 0;
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  BLEScanResults foundDevices = pBLEScan->start(3, false);

  int count = foundDevices.getCount();
  bleCount = min(count, MAX_BLE);
  for (int i = 0; i < bleCount; i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    String name = device.getName().c_str();
    if (name.length() == 0) name = "Unknown";
    String addr = device.getAddress().toString().c_str();
    int rssi = device.getRSSI();
    bleResults[i] = name.substring(0, 10) + " " + String(rssi) + "dBm";
    // shorten address if needed
  }
  pBLEScan->clearResults();
}

void scanNRF24() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 0, "Scanning NRF24...");
  u8g2.sendBuffer();

  nrfCount = 0;
  radio.stopListening();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setPayloadSize(32);

  for (int ch = 0; ch <= 125 && nrfCount < MAX_NRF; ch++) {
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
      nrfResults[nrfCount++] = "Ch " + String(ch) + " active";
    }
    delay(1);
  }

  if (nrfCount == 0) {
    nrfResults[0] = "No activity";
    nrfCount = 1;
  }
}