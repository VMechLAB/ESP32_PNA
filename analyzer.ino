/*
 * ESP32 Pocket Network Analyzer - ENHANCED
 * Features: WiFi/BLE/NRF24 scanning, RSSI graph, channel analyzer,
 * signal history, filtering, deep sleep, and more!
 * No SD card support (yet)
 * Made by VMechLAB ; Vasilije
 */

#include <U8g2lib.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <RF24.h>
#include <esp_sleep.h>

#define OLED_SDA   21
#define OLED_SCL   22
#define NRF_CE     4
#define NRF_CSN    5
#define BUTTON_UP  26
#define BUTTON_DOWN 25
#define BUTTON_SEL 32
#define BUTTON_BACK 33
#define LED_BUILTIN 2

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

RF24 radio(NRF_CE, NRF_CSN);

const unsigned long DEBOUNCE_MS = 30;
unsigned long lastButtonTime = 0;
bool btnUp = false, btnDown = false, btnSel = false, btnBack = false;
bool lastBtnUp = HIGH, lastBtnDown = HIGH, lastBtnSel = HIGH, lastBtnBack = HIGH;

enum AppState { 
  MENU, 
  WIFI_SCANNING, WIFI_RESULTS, WIFI_DETAIL, WIFI_GRAPH,
  BLE_SCANNING, BLE_RESULTS, BLE_DETAIL,
  NRF_SCANNING, NRF_RESULTS,
  CHANNEL_ANALYZER,
  SETTINGS,
  ABOUT
};
AppState state = MENU;
AppState previousState = MENU;
int menuIndex = 0;
int subMenuIndex = 0;
int selectedIndex = -1;  // For detail view

int scanDuration = 3;  // seconds
bool filterStrongOnly = false;
int sleepTimeout = 60; // seconds
unsigned long lastActivity = 0;

const int MAX_RESULTS = 15;
const int RESULT_LEN = 32;
const int HISTORY_LEN = 20;

char wifiResults[MAX_RESULTS][RESULT_LEN];
int wifiCount = 0;
int wifiRSSI[MAX_RESULTS];
int wifiChannel[MAX_RESULTS];
String wifiBSSID[MAX_RESULTS];
String wifiEncryption[MAX_RESULTS];

char bleResults[MAX_RESULTS][RESULT_LEN];
int bleCount = 0;
int bleRSSI[MAX_RESULTS];
String bleAddress[MAX_RESULTS];

char nrfResults[MAX_RESULTS][RESULT_LEN];
int nrfCount = 0;

int resultScroll = 0;

int rssiHistory[HISTORY_LEN];
int historyIndex = 0;
int historyCount = 0;
char historyTarget[32] = "";

int channelUsage[14] = {0};  // Channels 1-14
int totalAPs = 0;

const char menu1[] PROGMEM = "1. Wi-Fi Scan";
const char menu2[] PROGMEM = "2. BLE Scan";
const char menu3[] PROGMEM = "3. NRF24 Scan";
const char menu4[] PROGMEM = "4. Channel Analyzer";
const char menu5[] PROGMEM = "5. Settings";
const char menu6[] PROGMEM = "6. About";
const char* const menuItems[] PROGMEM = {menu1, menu2, menu3, menu4, menu5, menu6};
const int menuCount = 6;

const char settings1[] PROGMEM = "Scan Duration";
const char settings2[] PROGMEM = "Filter: Strong Only";
const char settings3[] PROGMEM = "Sleep Timeout";
const char settings4[] PROGMEM = "Back to Menu";
const char* const settingsItems[] PROGMEM = {settings1, settings2, settings3, settings4};
const int settingsCount = 4;

const char filterAll[] PROGMEM = "All";
const char filterStrong[] PROGMEM = "Strong (>-60dBm)";
const char filterWeak[] PROGMEM = "Weak (<=-60dBm)";

const char strMainMenu[] PROGMEM = "== MAIN MENU ==";
const char strArrow[] PROGMEM = ">";
const char strAbout1[] PROGMEM = "ESP32 Analyzer v2.0";
const char strAbout2[] PROGMEM = "WiFi/BLE/NRF24 Scanner";
const char strAbout3[] PROGMEM = "RSSI Graph & History";
const char strAbout4[] PROGMEM = "Channel Analyzer";
const char strAbout5[] PROGMEM = "Press BACK to exit";
const char strScanWiFi[] PROGMEM = "Scanning WiFi...";
const char strScanBLE[] PROGMEM = "Scanning BLE...";
const char strScanNRF[] PROGMEM = "Scanning NRF24...";
const char strNoAP[] PROGMEM = "No AP found";
const char strNoActivity[] PROGMEM = "No activity";
const char strHidden[] PROGMEM = "<hidden>";
const char strUnknown[] PROGMEM = "Unknown";
const char strPressSel[] PROGMEM = "Press SEL for details";
const char strPressBack[] PROGMEM = "Press BACK";
const char strSignalGraph[] PROGMEM = "Signal Graph";
const char strChannelHeat[] PROGMEM = "Channel Heatmap";
const char strHistory[] PROGMEM = "History";

char buffer[32];

void readButtons();
void displayMenu();
void displaySettings();
void displayResults(char results[][RESULT_LEN], int count, int scroll, int rssiArr[]);
void displayDetail(int type, int index);
void displayGraph();
void displayChannelAnalyzer();
void displayHistory();
void scanWiFi();
void scanBLE();
void scanNRF24();
void analyzeChannels();
void showAbout();
void goToSleep();
void resetActivity();

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Pocket Analyzer v2.0");

  // OLED
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setFontPosTop();
  u8g2.setContrast(255);

  // Buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SEL, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  
  // LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // NRF24
  if (!radio.begin()) {
    Serial.println("NRF24 init failed!");
  } else {
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.stopListening();
  }

  // BLE
  BLEDevice::init("ESP32_Scanner");
  
  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Init history
  for (int i = 0; i < HISTORY_LEN; i++) rssiHistory[i] = -100;
  
  lastActivity = millis();
  Serial.println("Ready.");
}

void loop() {
  readButtons();
  resetActivity();

  // Check for sleep
  if (millis() - lastActivity > sleepTimeout * 1000 && state != MENU) {
    goToSleep();
  }

  switch (state) {
    case MENU:
      displayMenu();
      if (btnSel) {
        switch (menuIndex) {
          case 0: state = WIFI_SCANNING; break;
          case 1: state = BLE_SCANNING; break;
          case 2: state = NRF_SCANNING; break;
          case 3: state = CHANNEL_ANALYZER; analyzeChannels(); break;
          case 4: state = SETTINGS; break;
          case 5: showAbout(); state = MENU; break;
        }
        selectedIndex = -1;
      }
      break;

    case WIFI_SCANNING: 
      scanWiFi(); 
      state = WIFI_RESULTS; 
      resultScroll = 0; 
      selectedIndex = -1;
      break;
      
    case WIFI_RESULTS:
      displayResults(wifiResults, wifiCount, resultScroll, wifiRSSI);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < wifiCount - 1) resultScroll++;
      if (btnSel && wifiCount > 0) {
        selectedIndex = resultScroll;
        state = WIFI_DETAIL;
      }
      if (btnBack) { state = MENU; selectedIndex = -1; }
      break;
      
    case WIFI_DETAIL:
      displayDetail(0, selectedIndex);
      if (btnBack || btnSel) { state = WIFI_RESULTS; }
      break;

    case BLE_SCANNING: 
      scanBLE(); 
      state = BLE_RESULTS; 
      resultScroll = 0;
      selectedIndex = -1;
      break;
      
    case BLE_RESULTS:
      displayResults(bleResults, bleCount, resultScroll, bleRSSI);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < bleCount - 1) resultScroll++;
      if (btnSel && bleCount > 0) {
        selectedIndex = resultScroll;
        state = BLE_DETAIL;
      }
      if (btnBack) { state = MENU; selectedIndex = -1; }
      break;
      
    case BLE_DETAIL:
      displayDetail(1, selectedIndex);
      if (btnBack || btnSel) { state = BLE_RESULTS; }
      break;

    case NRF_SCANNING: 
      scanNRF24(); 
      state = NRF_RESULTS; 
      resultScroll = 0;
      break;
      
    case NRF_RESULTS:
      displayResults(nrfResults, nrfCount, resultScroll, NULL);
      if (btnUp && resultScroll > 0) resultScroll--;
      if (btnDown && resultScroll < nrfCount - 1) resultScroll++;
      if (btnBack || btnSel) { state = MENU; }
      break;

    case CHANNEL_ANALYZER:
      displayChannelAnalyzer();
      if (btnBack || btnSel) { state = MENU; }
      break;

    case SETTINGS:
      displaySettings();
      break;

    case ABOUT:
      showAbout();
      break;
  }
  delay(30);
}

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

void resetActivity() {
  if (btnUp || btnDown || btnSel || btnBack) {
    lastActivity = millis();
  }
}

void goToSleep() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 0, "Sleeping...");
  u8g2.drawStr(0, 14, "Press any button");
  u8g2.drawStr(0, 28, "to wake up");
  u8g2.sendBuffer();
  
  esp_light_sleep_start();
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setFontPosTop();
  lastActivity = millis();
}

void displayMenu() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strMainMenu);
  u8g2.drawStr(0, 0, buffer);
  
  // Show battery/sleep indicator
  u8g2.drawStr(110, 0, "Zzz");
  
  for (int i = 0; i < menuCount; i++) {
    int y = 14 + i * 9;
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

void displaySettings() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 0, "== SETTINGS ==");
  for (int i = 0; i < settingsCount; i++) {
    int y = 14 + i * 12;
    if (i == subMenuIndex) u8g2.drawStr(0, y, ">");
    const char* item = (const char*)pgm_read_ptr(&settingsItems[i]);
    strcpy_P(buffer, item);
    u8g2.drawStr(10, y, buffer);
    if (i == 0) {
      u8g2.setCursor(110, y);
      u8g2.print(scanDuration);
      u8g2.print("s");
    } else if (i == 1) {
      u8g2.setCursor(110, y);
      u8g2.print(filterStrongOnly ? "ON" : "OFF");
    } else if (i == 2) {
      u8g2.setCursor(110, y);
      u8g2.print(sleepTimeout);
      u8g2.print("s");
    }
  }
  u8g2.sendBuffer();
  if (btnUp) subMenuIndex = (subMenuIndex - 1 + settingsCount) % settingsCount;
  if (btnDown) subMenuIndex = (subMenuIndex + 1) % settingsCount;
  if (btnSel) {
    switch (subMenuIndex) {
      case 0: // Scan duration
        scanDuration = (scanDuration % 8) + 2; // 2-10 seconds
        break;
      case 1: // Filter
        filterStrongOnly = !filterStrongOnly;
        break;
      case 2: // Sleep timeout
        sleepTimeout = (sleepTimeout == 10) ? 30 : (sleepTimeout == 30) ? 60 : 120;
        break;
      case 3: // Back
        state = MENU;
        break;
    }
  }
  if (btnBack) state = MENU;
}
void displayResults(char results[][RESULT_LEN], int count, int scroll, int rssiArr[]) {
  u8g2.clearBuffer();
  char header[16];
  snprintf(header, sizeof(header), "Results (%d)", count);
  u8g2.drawStr(0, 0, header);
  if (count > 5) {
    u8g2.drawStr(120, 0, (scroll < count - 5) ? "v" : " ");
  }
  int maxLines = 5;
  int start = scroll;
  int end = min(start + maxLines, count);
  for (int i = start; i < end; i++) {
    int y = 12 + (i - start) * 10;
    u8g2.drawStr(0, y, results[i]);
    if (rssiArr != NULL && i < count) {
      int rssi = rssiArr[i];
      int barLen = map(constrain(rssi, -100, -30), -100, -30, 1, 12);
      for (int b = 0; b < barLen; b++) {
        u8g2.drawPixel(120 + b, y + 7);
        u8g2.drawPixel(120 + b, y + 8);
      }
    }
  }
  u8g2.sendBuffer();
}

void displayDetail(int type, int index) {
  u8g2.clearBuffer();
  if (type == 0 && index < wifiCount) {
    u8g2.drawStr(0, 0, "== WiFi Detail ==");
    char line[32];
    snprintf(line, sizeof(line), "SSID: %s", wifiResults[index]);
    u8g2.drawStr(0, 12, line);
    snprintf(line, sizeof(line), "RSSI: %d dBm", wifiRSSI[index]);
    u8g2.drawStr(0, 22, line);
    snprintf(line, sizeof(line), "Channel: %d", wifiChannel[index]);
    u8g2.drawStr(0, 32, line);
    snprintf(line, sizeof(line), "BSSID: %s", wifiBSSID[index].c_str());
    u8g2.drawStr(0, 42, line);
    u8g2.drawStr(0, 52, "SEL:Graph  BACK:Exit");
    strcpy(historyTarget, wifiResults[index]);
    
  } else if (type == 1 && index < bleCount) {
    u8g2.drawStr(0, 0, "== BLE Detail ==");
    char line[32];
    snprintf(line, sizeof(line), "Name: %s", bleResults[index]);
    u8g2.drawStr(0, 12, line);
    snprintf(line, sizeof(line), "RSSI: %d dBm", bleRSSI[index]);
    u8g2.drawStr(0, 22, line);
    snprintf(line, sizeof(line), "MAC: %s", bleAddress[index].c_str());
    u8g2.drawStr(0, 32, line);
    u8g2.drawStr(0, 52, "SEL:Graph  BACK:Exit");
    strcpy(historyTarget, bleResults[index]);
  }
  
  u8g2.sendBuffer();
  
  // If SEL pressed, show graph
  if (btnSel && (type == 0 || type == 1)) {
    displayGraph();
  }
}

void displayGraph() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strSignalGraph);
  u8g2.drawStr(0, 0, buffer);
  u8g2.drawStr(80, 0, historyTarget);
  
  for (int i = 0; i < 4; i++) {
    int y = 12 + i * 12;
    u8g2.drawHLine(0, y, 128);
    u8g2.setCursor(0, y-2);
    u8g2.print(-20 - i*20);
    u8g2.print("dBm");
  }
  
  for (int i = 1; i < historyCount && i < HISTORY_LEN; i++) {
    int x1 = (i-1) * 6 + 10;
    int y1 = 12 + map(constrain(rssiHistory[i-1], -100, -20), -100, -20, 48, 0);
    int x2 = i * 6 + 10;
    int y2 = 12 + map(constrain(rssiHistory[i], -100, -20), -100, -20, 48, 0);
    u8g2.drawLine(x1, y1, x2, y2);
  }
  
  u8g2.setCursor(0, 58);
  u8g2.print("Press BACK");
  u8g2.sendBuffer();
  
  while (!btnBack) {
    readButtons();
    delay(50);
  }
}

void displayChannelAnalyzer() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strChannelHeat);
  u8g2.drawStr(0, 0, buffer);
  
  int barWidth = 8;
  int maxBarHeight = 40;
  
  for (int ch = 1; ch <= 14; ch++) {
    int x = (ch-1) * barWidth + 4;
    int usage = channelUsage[ch];
    int height = map(constrain(usage, 0, 20), 0, 20, 0, maxBarHeight);
    int y = 12 + (maxBarHeight - height);
    
    u8g2.drawBox(x, y, barWidth-1, height);
    u8g2.setCursor(x, 58);
    u8g2.print(ch);
  }
  
  u8g2.setCursor(0, 48);
  u8g2.print("Total APs: ");
  u8g2.print(totalAPs);
  
  u8g2.sendBuffer();
}

void scanWiFi() {
  u8g2.clearBuffer();
  strcpy_P(buffer, strScanWiFi);
  u8g2.drawStr(0, 0, buffer);
  u8g2.drawStr(0, 14, "Duration: ");
  u8g2.print(scanDuration);
  u8g2.print("s");
  u8g2.sendBuffer();

  wifiCount = 0;
  totalAPs = 0;
  for (int i = 0; i < 14; i++) channelUsage[i] = 0;
  
  int n = WiFi.scanComplete();
  if (n == -2 || n == -1) {
    WiFi.scanNetworks(true);
    delay(scanDuration * 1000);
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
      int ch = WiFi.channel(i);
      
      // Apply filter
      if (filterStrongOnly && rssi < -60) {
        wifiCount--;
        continue;
      }
      
      snprintf(wifiResults[i], RESULT_LEN, "%-14s %3ddBm", ssid.c_str(), rssi);
      wifiRSSI[i] = rssi;
      wifiChannel[i] = ch;
      wifiBSSID[i] = WiFi.BSSIDstr(i);
      
      // Update channel stats
      if (ch >= 1 && ch <= 14) {
        channelUsage[ch]++;
        totalAPs++;
      }
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
  u8g2.drawStr(0, 14, "Duration: ");
  u8g2.print(scanDuration);
  u8g2.print("s");
  u8g2.sendBuffer();

  bleCount = 0;
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  
  BLEScanResults* foundDevices = pBLEScan->start(scanDuration, false);
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
    
    snprintf(bleResults[i], RESULT_LEN, "%-12s %3ddBm", name.c_str(), rssi);
    bleRSSI[i] = rssi;
    bleAddress[i] = device.getAddress().toString().c_str();
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

void analyzeChannels() {
  if (wifiCount == 0) {
    WiFi.scanNetworks(true);
    delay(2000);
    int n = WiFi.scanComplete();
    if (n > 0) {
      for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        if (ch >= 1 && ch <= 14) channelUsage[ch]++;
      }
      totalAPs = n;
    }
    WiFi.scanDelete();
  }
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
