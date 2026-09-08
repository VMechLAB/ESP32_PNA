
#include <U8g2lib.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <RF24.h>

#define OLED_SDA   21
#define OLED_SCL   22
#define NRF_CE     4
#define NRF_CSN    5
#define BUTTON_UP  26
#define BUTTON_DOWN 25
#define BUTTON_SEL 32
#define BUTTON_BACK 33
#define LED_BUILTIN 2

#define RESULT_LEN 24

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
RF24 radio(NRF_CE, NRF_CSN);

// Button states (no interrupts)
bool btnUp = false, btnDown = false, btnSel = false, btnBack = false;
bool lastUp = HIGH, lastDown = HIGH, lastSel = HIGH, lastBack = HIGH;
unsigned long debounceTime = 0;
const unsigned long DEBOUNCE_MS = 20;

// Hold repeat
bool upHeld = false, downHeld = false;
unsigned long upHoldStart = 0, downHoldStart = 0;
const unsigned long HOLD_DELAY = 300;
const unsigned long REPEAT_INTERVAL = 80;

enum AppState { 
  MENU, WIFI_SCANNING, WIFI_RESULTS, WIFI_DETAIL, WIFI_GRAPH,
  BLE_SCANNING, BLE_RESULTS, BLE_DETAIL,
  NRF_SCANNING, NRF_RESULTS,
  CHANNEL_ANALYZER, SETTINGS, ABOUT
};
AppState state = MENU;
int menuIndex = 0, subMenuIndex = 0, selectedIndex = -1;
int resultScroll = 0;

int scanDuration = 3;
bool filterStrongOnly = false;

const int MAX_RESULTS = 25;
const int HISTORY_LEN = 40;

char wifiResults[MAX_RESULTS][RESULT_LEN];
int wifiCount = 0, wifiRSSI[MAX_RESULTS], wifiChannel[MAX_RESULTS];
String wifiBSSID[MAX_RESULTS];

char bleResults[MAX_RESULTS][RESULT_LEN];
int bleCount = 0, bleRSSI[MAX_RESULTS];
String bleAddress[MAX_RESULTS];

char nrfResults[MAX_RESULTS][RESULT_LEN];
int nrfCount = 0;

int rssiHistory[HISTORY_LEN];
int historyIndex = 0, historyCount = 0;
char historyTarget[24] = "";

int channelUsage[14] = {0};
int totalAPs = 0;
unsigned long lastActivity = 0;

void readButtons();
void drawSignalBar(int x, int y, int rssi, int maxWidth);
void drawBorder();
void drawProgressBar(int x, int y, int width, int percent);
void drawScrollbar(int total, int visible, int position);
void displayMenu();
void displayResults(char results[][RESULT_LEN], int count, int scroll, int rssiArr[], int type);
void displayDetail(int type, int index);
void displayGraph();
void displayChannelAnalyzer();
void displaySettings();
void showAbout();
void scanWiFi();
void scanBLE();
void scanNRF24();
void analyzeChannels();

void readButtons() {
  unsigned long now = millis();
  // Read current states (pull-up, so LOW when pressed)
  bool curUp = digitalRead(BUTTON_UP);
  bool curDown = digitalRead(BUTTON_DOWN);
  bool curSel = digitalRead(BUTTON_SEL);
  bool curBack = digitalRead(BUTTON_BACK);

  // Debounce: only process if stable for DEBOUNCE_MS
  if (now - debounceTime > DEBOUNCE_MS) {
    // Detect edges (press = transition from HIGH to LOW)
    btnUp = (lastUp == HIGH && curUp == LOW);
    btnDown = (lastDown == HIGH && curDown == LOW);
    btnSel = (lastSel == HIGH && curSel == LOW);
    btnBack = (lastBack == HIGH && curBack == LOW);

    // Update last states
    lastUp = curUp; lastDown = curDown; lastSel = curSel; lastBack = curBack;
    debounceTime = now;
  }

  // Handle hold-to-repeat for Up and Down
  if (curUp == LOW) {
    if (!upHeld) { upHeld = true; upHoldStart = now; }
    if (now - upHoldStart > HOLD_DELAY && (now - debounceTime > REPEAT_INTERVAL)) {
      btnUp = true; // simulate press
      debounceTime = now;
    }
  } else { upHeld = false; }

  if (curDown == LOW) {
    if (!downHeld) { downHeld = true; downHoldStart = now; }
    if (now - downHoldStart > HOLD_DELAY && (now - debounceTime > REPEAT_INTERVAL)) {
      btnDown = true;
      debounceTime = now;
    }
  } else { downHeld = false; }

  // Clear button flags after they've been processed by the main loop
  // We'll clear them in the main loop after using them.
}

void drawSignalBar(int x, int y, int rssi, int maxWidth) {
  int strength = map(constrain(rssi, -100, -30), -100, -30, 0, maxWidth);
  for (int i = 0; i < maxWidth; i++) {
    if (i < strength) u8g2.drawBox(x + i*2, y+2, 1, 6 - i*2/maxWidth);
  }
}

void drawBorder() {
  u8g2.drawFrame(0, 0, 128, 64);
}

void drawProgressBar(int x, int y, int width, int percent) {
  u8g2.drawFrame(x, y, width, 6);
  int fill = map(constrain(percent, 0, 100), 0, 100, 0, width-2);
  if (fill > 0) u8g2.drawBox(x+1, y+1, fill, 4);
}

void drawScrollbar(int total, int visible, int position) {
  if (total <= visible) return;
  int barHeight = map(visible, 0, total, 10, 50);
  int barPos = map(position, 0, total - visible, 10, 60 - barHeight);
  u8g2.drawFrame(126, 10, 2, 50);
  u8g2.drawBox(126, barPos, 2, barHeight);
}

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setFontPosTop();
  
  pinMode(BUTTON_UP, INPUT_PULLUP); pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SEL, INPUT_PULLUP); pinMode(BUTTON_BACK, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(LED_BUILTIN, LOW);
  
  if (!radio.begin()) Serial.println("NRF24 fail");
  else { radio.setPALevel(RF24_PA_LOW); radio.setDataRate(RF24_250KBPS); radio.stopListening(); }
  
  BLEDevice::init("ESP32_Scanner");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  for (int i = 0; i < HISTORY_LEN; i++) rssiHistory[i] = -100;
  lastActivity = millis();
}

void loop() {
  readButtons(); // update button flags

  // Process state machine
  switch (state) {
    case MENU: displayMenu(); break;
    case WIFI_SCANNING: scanWiFi(); state = WIFI_RESULTS; resultScroll = 0; selectedIndex = -1; break;
    case WIFI_RESULTS: displayResults(wifiResults, wifiCount, resultScroll, wifiRSSI, 0); break;
    case WIFI_DETAIL: displayDetail(0, selectedIndex); break;
    case BLE_SCANNING: scanBLE(); state = BLE_RESULTS; resultScroll = 0; selectedIndex = -1; break;
    case BLE_RESULTS: displayResults(bleResults, bleCount, resultScroll, bleRSSI, 1); break;
    case BLE_DETAIL: displayDetail(1, selectedIndex); break;
    case NRF_SCANNING: scanNRF24(); state = NRF_RESULTS; resultScroll = 0; break;
    case NRF_RESULTS: displayResults(nrfResults, nrfCount, resultScroll, NULL, 2); break;
    case CHANNEL_ANALYZER: displayChannelAnalyzer(); break;
    case SETTINGS: displaySettings(); break;
    case ABOUT: showAbout(); break;
  }
  
  // Clear button flags after processing to avoid repeated actions
  btnUp = false; btnDown = false; btnSel = false; btnBack = false;
  
  delay(5); // small delay to prevent watchdog
}

void displayMenu() {
  u8g2.clearBuffer();
  drawBorder();
  const char* items[] = {"WiFi", "BLE", "NRF24", "Chan", "Set", "About"};
  int count = 6, yStart = 8, spacing = 8;
  for (int i = 0; i < count; i++) {
    int y = yStart + i * spacing;
    if (i == menuIndex) {
      u8g2.drawBox(2, y-1, 124, 7);
      u8g2.setDrawColor(0);
      u8g2.drawStr(8, y, items[i]);
      u8g2.setDrawColor(1);
    } else u8g2.drawStr(8, y, items[i]);
  }
  u8g2.setCursor(0,0); u8g2.print(">");
  u8g2.setCursor(8,0); u8g2.print("Analyzer");
  u8g2.sendBuffer();
  
  if (btnUp) { menuIndex = (menuIndex-1+count)%count; btnUp=false; }
  if (btnDown) { menuIndex = (menuIndex+1)%count; btnDown=false; }
  if (btnSel) {
    btnSel=false;
    switch(menuIndex) {
      case 0: state = WIFI_SCANNING; break;
      case 1: state = BLE_SCANNING; break;
      case 2: state = NRF_SCANNING; break;
      case 3: state = CHANNEL_ANALYZER; analyzeChannels(); break;
      case 4: state = SETTINGS; break;
      case 5: state = ABOUT; break;
    }
  }
}

void displayResults(char results[][RESULT_LEN], int count, int scroll, int rssiArr[], int type) {
  u8g2.clearBuffer();
  drawBorder();
  char header[16]; const char* names[] = {"WiFi","BLE","NRF"};
  snprintf(header, sizeof(header), "%s(%d)", names[type], count);
  u8g2.drawStr(2, 0, header);
  
  if (count == 0) {
    u8g2.drawStr(2, 20, "None");
    u8g2.sendBuffer();
    if (btnBack||btnSel) { state=MENU; btnBack=false; btnSel=false; }
    return;
  }
  
  int maxLines = 6;
  int start = scroll, end = min(start+maxLines, count);
  drawScrollbar(count, maxLines, scroll);
  
  int yStart = 8, lineSpacing = 8;
  for (int i=start; i<end; i++) {
    int y = yStart + (i-start)*lineSpacing;
    u8g2.drawStr(2, y, results[i]);
    if (rssiArr != NULL && i<count) {
      int rssi = rssiArr[i];
      drawSignalBar(90, y+1, rssi, 6);
      u8g2.setCursor(112, y); u8g2.print(rssi);
    }
  }
  u8g2.setCursor(2, 60); u8g2.print("S>v  B<");
  u8g2.sendBuffer();
  
  if (btnUp && scroll>0) { resultScroll--; btnUp=false; }
  if (btnDown && scroll<count-maxLines) { resultScroll++; btnDown=false; }
  if (btnSel && count>0) {
    selectedIndex = resultScroll;
    state = (type==0) ? WIFI_DETAIL : (type==1) ? BLE_DETAIL : MENU;
    btnSel=false;
  }
  if (btnBack) { state=MENU; btnBack=false; selectedIndex=-1; }
}

void displayDetail(int type, int index) {
  u8g2.clearBuffer();
  drawBorder();
  if (type==0 && index<wifiCount) {
    u8g2.drawStr(2, 0, wifiResults[index]);
    u8g2.setCursor(2, 10); u8g2.print("RSSI:"); u8g2.print(wifiRSSI[index]);
    u8g2.setCursor(2, 20); u8g2.print("CH:"); u8g2.print(wifiChannel[index]);
    u8g2.setCursor(2, 30); u8g2.print(wifiBSSID[index]);
    strcpy(historyTarget, wifiResults[index]);
  } else if (type==1 && index<bleCount) {
    u8g2.drawStr(2, 0, bleResults[index]);
    u8g2.setCursor(2, 10); u8g2.print("RSSI:"); u8g2.print(bleRSSI[index]);
    u8g2.setCursor(2, 20); u8g2.print(bleAddress[index]);
    strcpy(historyTarget, bleResults[index]);
  }
  u8g2.setCursor(2, 50); u8g2.print("S:Graph  B:Back");
  u8g2.sendBuffer();
  
  if (btnSel) { btnSel=false; displayGraph(); }
  if (btnBack) { btnBack=false; state = (type==0)?WIFI_RESULTS:BLE_RESULTS; }
}

void displayGraph() {
  u8g2.clearBuffer();
  drawBorder();
  u8g2.drawStr(2, 0, "History");
  u8g2.drawStr(70,0, historyTarget);
  for (int i=0; i<5; i++) {
    int y = 8 + i*9;
    u8g2.drawHLine(2, y, 124);
    u8g2.setCursor(2, y-1); u8g2.print(-20 - i*20);
  }
  for (int i=1; i<historyCount && i<HISTORY_LEN; i++) {
    int x1 = (i-1)*3 + 28, y1 = 7 + map(constrain(rssiHistory[i-1],-100,-20),-100,-20,41,0);
    int x2 = i*3 + 28, y2 = 7 + map(constrain(rssiHistory[i],-100,-20),-100,-20,41,0);
    u8g2.drawLine(x1,y1,x2,y2);
  }
  u8g2.setCursor(2,60); u8g2.print("B:exit");
  u8g2.sendBuffer();
  while (!btnBack) { readButtons(); delay(10); }
  btnBack=false;
}

void displayChannelAnalyzer() {
  u8g2.clearBuffer();
  drawBorder();
  u8g2.drawStr(2,0, "Channels");
  u8g2.setCursor(80,0); u8g2.print("AP:"); u8g2.print(totalAPs);
  int bw=8, maxH=38, yBase=8;
  for (int ch=0; ch<14; ch++) {
    int x = ch*bw + 6;
    int h = map(constrain(channelUsage[ch+1],0,20),0,20,1,maxH);
    int y = yBase + (maxH-h);
    u8g2.drawBox(x,y,bw-2,h);
    u8g2.drawFrame(x,yBase,bw-2,maxH);
    if (ch%2==0) { u8g2.setCursor(x+1, yBase+maxH+4); u8g2.print(ch+1); }
  }
  u8g2.setCursor(2,60); u8g2.print("2.4GHz");
  u8g2.sendBuffer();
  if (btnBack||btnSel) { state=MENU; btnBack=false; btnSel=false; }
}

void displaySettings() {
  u8g2.clearBuffer();
  drawBorder();
  const char* items[] = {"Time", "Filter", "Back"};
  int count=3, yStart=12, spacing=12;
  for (int i=0; i<count; i++) {
    int y = yStart + i*spacing;
    if (i==subMenuIndex) {
      u8g2.drawBox(2, y-1, 124, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(6, y, items[i]);
      u8g2.setDrawColor(1);
    } else u8g2.drawStr(6, y, items[i]);
    if (i==0) {
      u8g2.setCursor(90,y); u8g2.print(scanDuration); u8g2.print("s");
      drawProgressBar(80,y+2,20, map(scanDuration,2,10,0,100));
    }
    if (i==1) { u8g2.setCursor(90,y); u8g2.print(filterStrongOnly?"ON":"OFF"); }
  }
  u8g2.sendBuffer();
  if (btnUp) { subMenuIndex=(subMenuIndex-1+count)%count; btnUp=false; }
  if (btnDown) { subMenuIndex=(subMenuIndex+1)%count; btnDown=false; }
  if (btnSel) {
    btnSel=false;
    switch(subMenuIndex) {
      case 0: scanDuration = (scanDuration%8)+2; break;
      case 1: filterStrongOnly = !filterStrongOnly; break;
      case 2: state = MENU; break;
    }
  }
  if (btnBack) { state=MENU; btnBack=false; }
}

void showAbout() {
  u8g2.clearBuffer(); drawBorder();
  u8g2.drawStr(2,0, "Analyzer v2");
  u8g2.drawStr(2,12, "WiFi/BLE/NRF");
  u8g2.drawStr(2,24, "RSSI graph");
  u8g2.drawStr(2,36, "Channel view");
  u8g2.drawStr(2,48, "B:menu");
  u8g2.sendBuffer();
  if (btnBack||btnSel) { state=MENU; btnBack=false; btnSel=false; }
}

void scanWiFi() {
  u8g2.clearBuffer(); drawBorder();
  u8g2.drawStr(2,0, "Scan WiFi");
  u8g2.drawStr(2,16, "Dur:"); u8g2.setCursor(40,16); u8g2.print(scanDuration); u8g2.print("s");
  drawProgressBar(2,26,124,0); u8g2.sendBuffer();
  
  wifiCount=0; totalAPs=0; for (int i=0;i<14;i++) channelUsage[i]=0;
  WiFi.scanNetworks(true);
  int steps = scanDuration*2;
  for (int i=0; i<steps; i++) {
    u8g2.clearBuffer(); drawBorder();
    u8g2.drawStr(2,0, "Scan WiFi");
    drawProgressBar(2,26,124, (i*100)/steps);
    u8g2.setCursor(2,36); u8g2.print((i*100)/steps); u8g2.print("%");
    u8g2.sendBuffer();
    delay(500);
  }
  int n = WiFi.scanComplete();
  if (n>0) {
    wifiCount = min(n, MAX_RESULTS);
    int valid=0;
    for (int i=0; i<n && valid<MAX_RESULTS; i++) {
      String ssid = WiFi.SSID(i); int rssi = WiFi.RSSI(i); int ch = WiFi.channel(i);
      if (filterStrongOnly && rssi < -60) continue;
      if (ssid.length()==0) ssid = "?";
      char line[RESULT_LEN];
      snprintf(line, sizeof(line), "%-10s %3d", ssid.c_str(), rssi);
      strcpy(wifiResults[valid], line);
      wifiRSSI[valid]=rssi; wifiChannel[valid]=ch; wifiBSSID[valid]=WiFi.BSSIDstr(i);
      if (ch>=1 && ch<=14) { channelUsage[ch]++; totalAPs++; }
      valid++;
    }
    wifiCount = valid;
  } else { wifiCount=1; strcpy(wifiResults[0], "None"); }
  WiFi.scanDelete();
}

void scanBLE() {
  u8g2.clearBuffer(); drawBorder();
  u8g2.drawStr(2,0, "Scan BLE");
  u8g2.drawStr(2,16, "Dur:"); u8g2.setCursor(40,16); u8g2.print(scanDuration); u8g2.print("s");
  drawProgressBar(2,26,124,0); u8g2.sendBuffer();
  
  bleCount=0;
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true); pBLEScan->setInterval(100); pBLEScan->setWindow(99);
  int steps = scanDuration*2;
  for (int i=0; i<steps; i++) {
    u8g2.clearBuffer(); drawBorder();
    u8g2.drawStr(2,0, "Scan BLE");
    drawProgressBar(2,26,124, (i*100)/steps);
    u8g2.setCursor(2,36); u8g2.print((i*100)/steps); u8g2.print("%");
    u8g2.sendBuffer();
    delay(500);
  }
  BLEScanResults* found = pBLEScan->start(scanDuration, false);
  int count = found->getCount();
  bleCount = min(count, MAX_RESULTS);
  for (int i=0; i<bleCount; i++) {
    BLEAdvertisedDevice dev = found->getDevice(i);
    String name = dev.getName().c_str();
    if (name.length()==0) name = "?";
    int rssi = dev.getRSSI();
    char line[RESULT_LEN];
    snprintf(line, sizeof(line), "%-10s %3d", name.c_str(), rssi);
    strcpy(bleResults[i], line);
    bleRSSI[i]=rssi; bleAddress[i]=dev.getAddress().toString().c_str();
  }
  pBLEScan->clearResults();
}

void scanNRF24() {
  u8g2.clearBuffer(); drawBorder();
  u8g2.drawStr(2,0, "Scan NRF");
  drawProgressBar(2,26,124,0); u8g2.sendBuffer();
  
  nrfCount=0; radio.stopListening(); radio.setPALevel(RF24_PA_LOW); radio.setDataRate(RF24_250KBPS); radio.setPayloadSize(32);
  int total = 126;
  for (int ch=0; ch<total && nrfCount<MAX_RESULTS; ch++) {
    if (ch%10==0) {
      u8g2.clearBuffer(); drawBorder();
      u8g2.drawStr(2,0, "Scan NRF");
      drawProgressBar(2,26,124, (ch*100)/total);
      u8g2.setCursor(2,36); u8g2.print((ch*100)/total); u8g2.print("%");
      u8g2.sendBuffer();
    }
    radio.setChannel(ch); radio.startListening(); delayMicroseconds(300);
    if (radio.available()) { uint8_t dummy[32]; radio.read(&dummy,1); char line[RESULT_LEN]; snprintf(line, sizeof(line), "CH%3d", ch); strcpy(nrfResults[nrfCount], line); nrfCount++; }
    radio.stopListening(); delay(1);
  }
  if (nrfCount==0) { strcpy(nrfResults[0], "None"); nrfCount=1; }
}

void analyzeChannels() {
  if (wifiCount==0) {
    WiFi.scanNetworks(true); delay(2000); int n = WiFi.scanComplete();
    if (n>0) { for (int i=0;i<n;i++) { int ch=WiFi.channel(i); if (ch>=1&&ch<=14) channelUsage[ch]++; } totalAPs=n; }
    WiFi.scanDelete();
  }
}
