/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║   AI SINH HỌC — ESP32 + DHT11 + ENS160 + OLED 1.3"         ║
 * ║   MQTT AloT (mqtt.duynguyen.io.vn WSS) + WiFi Portal        ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * PINOUT OLED (module 4 chân, thứ tự in trên PCB):
 *   GND → GND ESP32
 *   VCC → 3V3 (không cấp 5V nếu module ghi 3.3V)
 *   SCL → GPIO 22
 *   SDA → GPIO 21
 *   ENS160+AHT21 SCL/SDA → GPIO 26 / GPIO 25 (Wire1)
 *   DHT11 DATA → GPIO 27
 *
 * MQTT (chipId = 800):
 *   tele/800/status           online|offline
 *   tele/800_temp/status      {"value":25.4}
 *   tele/800_humi/status      {"value":61.0}
 *   tele/800_co2/status       {"value":450}
 *
 * THƯ VIỆN:
 *   WebSockets, MQTTPubSubClient, DHT, SparkFun ENS160
 *   Adafruit SH110X + Adafruit SSD1306 + GFX + AHTX0
 *   (firmware tự thử SH1106 rồi SSD1306, địa chỉ 0x3C/0x3D)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <WebSocketsClient.h>
#include <MQTTPubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>
#include <SparkFun_ENS160.h>
#include <Adafruit_AHTX0.h>
#include <DHT.h>

// ── Chân phần cứng ───────────────────────────────────────────
#define PIN_OLED_SDA  21
#define PIN_OLED_SCL  22
#define PIN_SENS_SDA  25
#define PIN_SENS_SCL  26
#define PIN_DHT       27

#define OLED_W        128
#define OLED_H        64
#define OLED_ADDR     0x3C
#define OLED_ADDR2    0x3D

// ── WiFi portal ──────────────────────────────────────────────
#define AP_SSID       "AI_SinhHoc"
IPAddress apIP(192, 168, 4, 1);
const byte DNS_PORT = 53;

// ── MQTT AloT ────────────────────────────────────────────────
#define MQTT_HOST     "mqtt.duynguyen.io.vn"
#define MQTT_PORT     443
#define MQTT_PATH     "/mqtt"
#define CHIP_ID       "800"

#define DEV_TEMP      "800_temp"
#define DEV_HUMI      "800_humi"
#define DEV_CO2       "800_co2"

#define EEPROM_SIZE   512
#define MAX_WIFI      5
#define READ_MS       1500
#define TELEMETRY_MS  1500
#define HEARTBEAT_MS  30000
#define RECONNECT_MS  10000
#define OLED_MS       1000

// Lọc nhiễu eCO2 ENS160: median-of-5 + EMA + giới hạn nhảy đột biến
#define CO2_MEDIAN_N     5
#define CO2_EMA_ALPHA    0.28f   // 0..1 — càng nhỏ càng mượt
#define CO2_MAX_STEP_PPM 60      // ppm tối đa mỗi lần đọc (~1.5s)

struct WifiEntry {
  char ssid[33];
  char pass[65];
};
WifiEntry wifiList[MAX_WIFI];
int wifiCount = 0;

Adafruit_SH1106G oledSh(OLED_W, OLED_H, &Wire, -1);
Adafruit_SSD1306 oledSd(OLED_W, OLED_H, &Wire, -1);
enum OledDriver : uint8_t { OLED_NONE = 0, OLED_SH1106 = 1, OLED_SSD1306 = 2 };
OledDriver oledDriver = OLED_NONE;

SparkFun_ENS160 ens160;
Adafruit_AHTX0 aht;
DHT dht(PIN_DHT, DHT11);

WebServer webServer(80);
DNSServer dnsServer;
WebSocketsClient wsClient;
MQTTPubSubClient mqttClient;

float bioTemp = NAN;
float bioHum  = NAN;
uint16_t bioCo2 = 0;
bool ensOk = false;
bool ahtOk = false;
bool oledOk = false;

// Bộ đệm lọc CO₂
uint16_t co2RawBuf[CO2_MEDIAN_N];
uint8_t co2RawCount = 0;
uint8_t co2RawIdx = 0;
float co2Ema = 0;
bool co2EmaReady = false;
uint16_t co2Filtered = 0;

Adafruit_GFX* oledGfx() {
  if (oledDriver == OLED_SH1106) return &oledSh;
  if (oledDriver == OLED_SSD1306) return &oledSd;
  return nullptr;
}

void oledShow() {
  if (oledDriver == OLED_SH1106) oledSh.display();
  else if (oledDriver == OLED_SSD1306) oledSd.display();
}

void oledClear() {
  if (oledDriver == OLED_SH1106) oledSh.clearDisplay();
  else if (oledDriver == OLED_SSD1306) oledSd.clearDisplay();
}

void scanI2cBus(TwoWire& bus, const char* tag) {
  Serial.printf("[I2C scan %s] ", tag);
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Serial.printf("0x%02X ", addr);
      found++;
    }
  }
  if (!found) Serial.print(F("KHONG THAY THIET BI"));
  Serial.println();
}

bool initOled() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(100000);  // chậm hơn — ổn với dây dài / hàn xấu
  delay(80);
  scanI2cBus(Wire, "OLED");

  // Ưu tiên SSD1306: nhiều module PCB xanh 4 chân (0.96"/1.3") là SSD1306
  // dù begin(SH1106) vẫn ACK nhưng màn có thể đen.
  oledDriver = OLED_NONE;
  if (oledSd.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledDriver = OLED_SSD1306;
    Serial.println(F("OLED: SSD1306 @0x3C"));
  } else if (oledSd.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR2)) {
    oledDriver = OLED_SSD1306;
    Serial.println(F("OLED: SSD1306 @0x3D"));
  } else if (oledSh.begin(OLED_ADDR, true)) {
    oledDriver = OLED_SH1106;
    Serial.println(F("OLED: SH1106 @0x3C"));
  } else if (oledSh.begin(OLED_ADDR2, true)) {
    oledDriver = OLED_SH1106;
    Serial.println(F("OLED: SH1106 @0x3D"));
  } else {
    Serial.println(F("OLED FAIL — kiem tra day GND/VCC/SCL=22/SDA=21 + han chạm VCC-SCL"));
    return false;
  }

  // Nháy trắng ngắn để xác nhận panel sống
  Adafruit_GFX* g = oledGfx();
  oledClear();
  g->fillScreen(SSD1306_WHITE);
  oledShow();
  delay(200);
  oledClear();
  oledShow();
  return true;
}

bool portalActive = false;
// Portal chuẩn SmartHome AloT: AP_STA, thử WiFi trước khi lưu, không nút vật lý

unsigned long tRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastReconnect = 0;
unsigned long lastMqttRetry = 0;
unsigned long lastOled = 0;
bool wssReady = false;
bool mqttLoggedOk = false;
int wifiRetries = 0;

// Đổi chỗ / WiFi mới: không cần nút / tắt nguồn.
// Không thấy SSID đã lưu trong quét → tự mở portal để khách cấu hình.

void drawOled();
void pubAll();
void pubOnline();
void reconnectMQTT();
void startPortal();
static bool ensureSoftAP();
bool connectBestWifi(bool* outNoSavedVisible = nullptr);

static bool wifiSsidOk(const char* ssid) {
  if (!ssid || !ssid[0]) return false;
  // Cho phép UTF-8 (SSID tiếng Việt). Chỉ loại control char / EEPROM toàn 0xFF.
  bool any = false;
  for (int i = 0; i < 32 && ssid[i]; i++) {
    unsigned char c = (unsigned char)ssid[i];
    if (c < 32) return false;
    if (c != 0xFF) any = true;
  }
  return any;
}

// ── EEPROM WiFi ──────────────────────────────────────────────
void saveWifiList() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, wifiCount);
  int addr = sizeof(wifiCount);
  for (int i = 0; i < MAX_WIFI; i++) {
    EEPROM.put(addr, wifiList[i]);
    addr += sizeof(WifiEntry);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadWifiList() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, wifiCount);
  if (wifiCount < 0 || wifiCount > MAX_WIFI) wifiCount = 0;
  int addr = sizeof(wifiCount);
  for (int i = 0; i < MAX_WIFI; i++) {
    EEPROM.get(addr, wifiList[i]);
    wifiList[i].ssid[sizeof(wifiList[i].ssid) - 1] = '\0';
    wifiList[i].pass[sizeof(wifiList[i].pass) - 1] = '\0';
    addr += sizeof(WifiEntry);
  }
  EEPROM.end();

  // Lọc SSID rác / trống
  int good = 0;
  for (int i = 0; i < wifiCount; i++) {
    if (!wifiSsidOk(wifiList[i].ssid)) continue;
    if (good != i) wifiList[good] = wifiList[i];
    good++;
  }
  if (good != wifiCount) {
    wifiCount = good;
    saveWifiList();
  }
}

void addOrUpdateWifi(const String& ssid, const String& pass) {
  // Đảm bảo null-terminated + không tràn (SSID max 32 byte kể cả '\0')
  if (ssid.length() == 0 || ssid.length() > 32) return;

  for (int i = 0; i < wifiCount; i++) {
    if (ssid == wifiList[i].ssid) {
      memset(wifiList[i].pass, 0, sizeof(wifiList[i].pass));
      pass.toCharArray(wifiList[i].pass, sizeof(wifiList[i].pass));
      // Đưa mạng vừa cập nhật lên cuối (ưu tiên kết nối lần boot sau)
      if (i != wifiCount - 1) {
        WifiEntry tmp = wifiList[i];
        for (int j = i; j < wifiCount - 1; j++) wifiList[j] = wifiList[j + 1];
        wifiList[wifiCount - 1] = tmp;
      }
      saveWifiList();
      return;
    }
  }
  if (wifiCount < MAX_WIFI) {
    memset(&wifiList[wifiCount], 0, sizeof(WifiEntry));
    ssid.toCharArray(wifiList[wifiCount].ssid, sizeof(wifiList[wifiCount].ssid));
    pass.toCharArray(wifiList[wifiCount].pass, sizeof(wifiList[wifiCount].pass));
    wifiCount++;
  } else {
    for (int i = 0; i < MAX_WIFI - 1; i++) wifiList[i] = wifiList[i + 1];
    memset(&wifiList[MAX_WIFI - 1], 0, sizeof(WifiEntry));
    ssid.toCharArray(wifiList[MAX_WIFI - 1].ssid, sizeof(wifiList[MAX_WIFI - 1].ssid));
    pass.toCharArray(wifiList[MAX_WIFI - 1].pass, sizeof(wifiList[MAX_WIFI - 1].pass));
  }
  saveWifiList();
}

static bool ensureSoftAP() {
  // AP mở (không mật khẩu) — khách nối dễ; channel 1 ổn định với hầu hết điện thoại
  WiFi.mode(WIFI_AP_STA);
  delay(30);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(AP_SSID, nullptr, 1, 0, 8);
  if (!ok) {
    delay(80);
    ok = WiFi.softAP(AP_SSID, nullptr, 1, 0, 8);
  }
  return ok && WiFi.softAPIP() == apIP;
}

static bool tryWifiCreds(const char* ssid, const char* pass, int attempts = 24) {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(150);
  Serial.printf("[WiFi] STA begin '%s'\n", ssid);
  WiFi.begin(ssid, pass);
  for (int i = 0; i < attempts && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] OK IP: " + WiFi.localIP().toString());
    return true;
  }
  Serial.printf("\n[WiFi] FAIL status=%d\n", (int)WiFi.status());
  return false;
}

// Quét → chỉ nối SSID đã lưu ĐANG THẤY.
// Ship sang nhà khách: không thấy WiFi cũ → false + outNoSavedVisible=true → mở portal ngay.
bool connectBestWifi(bool* outNoSavedVisible) {
  if (outNoSavedVisible) *outNoSavedVisible = false;
  Serial.println(F("\n[WiFi] Quet mang..."));
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  if (wifiCount <= 0) {
    if (outNoSavedVisible) *outNoSavedVisible = true;
    return false;
  }

  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  if (n < 0) {
    Serial.println(F("[WiFi] scan loi — thu lai sau"));
    return false;
  }

  // Chỉ các SSID đã lưu và đang xuất hiện (tránh mù thử WiFi chỗ cũ khi đã chuyển chỗ)
  int order[MAX_WIFI];
  int orderCount = 0;
  int rssiOf[MAX_WIFI];
  for (int w = 0; w < wifiCount; w++) {
    if (!wifiSsidOk(wifiList[w].ssid)) continue;
    int best = -999;
    bool seen = false;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == wifiList[w].ssid) {
        seen = true;
        if (WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
      }
    }
    if (!seen) continue;
    order[orderCount] = w;
    rssiOf[orderCount] = best;
    orderCount++;
  }
  WiFi.scanDelete();
  Serial.printf("[WiFi] scan=%d savedVisible=%d/%d\n", n, orderCount, wifiCount);

  if (orderCount == 0) {
    Serial.println(F("[WiFi] Khong thay SSID da luu — mang moi / chuyen cho → portal"));
    if (outNoSavedVisible) *outNoSavedVisible = true;
    return false;
  }

  // Sắp xếp RSSI cao → thấp (insertion sort nhỏ)
  for (int i = 1; i < orderCount; i++) {
    int ow = order[i], orr = rssiOf[i], j = i;
    while (j > 0 && rssiOf[j - 1] < orr) {
      order[j] = order[j - 1];
      rssiOf[j] = rssiOf[j - 1];
      j--;
    }
    order[j] = ow;
    rssiOf[j] = orr;
  }

  for (int i = 0; i < orderCount; i++) {
    int w = order[i];
    if (tryWifiCreds(wifiList[w].ssid, wifiList[w].pass)) return true;
  }
  return false;
}

// Mở portal cho khách cấu hình WiFi mới (không cần thao tác vật lý)
static void openPortalForNewWifi(bool clearOldSaved) {
  if (clearOldSaved && wifiCount > 0) {
    Serial.println(F("[WiFi] Xoa SSID cho cu — cho khach nhap mang moi"));
    wifiCount = 0;
    memset(wifiList, 0, sizeof(wifiList));
    saveWifiList();
  }
  startPortal();
}

// Median nhỏ (insertion) — chống spike ENS160
static uint16_t medianU16(uint16_t* arr, uint8_t n) {
  uint16_t tmp[CO2_MEDIAN_N];
  for (uint8_t i = 0; i < n; i++) tmp[i] = arr[i];
  for (uint8_t i = 1; i < n; i++) {
    uint16_t v = tmp[i];
    int j = i;
    while (j > 0 && tmp[j - 1] > v) {
      tmp[j] = tmp[j - 1];
      j--;
    }
    tmp[j] = v;
  }
  return tmp[n / 2];
}

// Lọc eCO2: median → giới hạn bước → EMA → sàn 400 ppm
static uint16_t filterCo2(uint16_t raw) {
  if (raw == 0) return bioCo2;
  if (raw < 400) raw = 400;
  if (raw > 5000) raw = 5000;  // trần thực tế phòng học

  co2RawBuf[co2RawIdx] = raw;
  co2RawIdx = (co2RawIdx + 1) % CO2_MEDIAN_N;
  if (co2RawCount < CO2_MEDIAN_N) co2RawCount++;

  uint16_t med = medianU16(co2RawBuf, co2RawCount);

  // Giới hạn nhảy đột biến so với giá trị đã lọc trước đó
  if (co2EmaReady) {
    int32_t delta = (int32_t)med - (int32_t)co2Filtered;
    if (delta > CO2_MAX_STEP_PPM) med = co2Filtered + CO2_MAX_STEP_PPM;
    else if (delta < -CO2_MAX_STEP_PPM) med = co2Filtered - CO2_MAX_STEP_PPM;
  }

  if (!co2EmaReady) {
    co2Ema = (float)med;
    co2EmaReady = true;
  } else {
    co2Ema = CO2_EMA_ALPHA * (float)med + (1.0f - CO2_EMA_ALPHA) * co2Ema;
  }

  co2Filtered = (uint16_t)(co2Ema + 0.5f);
  if (co2Filtered < 400) co2Filtered = 400;
  return co2Filtered;
}

// ── Cảm biến ─────────────────────────────────────────────────
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // DHT ưu tiên; nếu lỗi thì dùng AHT21 trên module ENS160
  if (!isnan(t) && !isnan(h)) {
    bioTemp = t;
    bioHum = h;
  } else if (ahtOk) {
    sensors_event_t humidity, temp;
    if (aht.getEvent(&humidity, &temp)) {
      bioTemp = temp.temperature;
      bioHum = humidity.relative_humidity;
    }
  }

  // Bù nhiệt/ẩm cho ENS160 → eCO2 ổn định hơn (datasheet)
  if (ensOk && !isnan(bioTemp) && !isnan(bioHum)) {
    ens160.setTempCompensationCelsius(bioTemp);
    ens160.setRHCompensationFloat(bioHum);
  }

  if (ensOk) {
    ens160.checkDataStatus();
    uint16_t raw = ens160.getECO2();
    if (raw > 0) {
      uint16_t filtered = filterCo2(raw);
      bioCo2 = filtered;
      Serial.printf("T=%.1fC H=%.0f%% CO2=%u ppm (raw=%u)\n",
                    bioTemp, bioHum, (unsigned)bioCo2, (unsigned)raw);
      return;
    }
  }

  Serial.printf("T=%.1fC H=%.0f%% CO2=%u ppm\n",
                bioTemp, bioHum, (unsigned)bioCo2);
}

// ── OLED UI (128x64) — bảng đo sạch ──────────────────────────
static void oledPrintCentered(Adafruit_GFX* g, int16_t y, uint8_t size, const char* s) {
  g->setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor((OLED_W - (int16_t)w) / 2, y);
  g->print(s);
}

static void oledPrintCenteredIn(Adafruit_GFX* g, int16_t x0, int16_t boxW, int16_t y,
                                uint8_t size, const char* s) {
  g->setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g->setCursor(x0 + (boxW - (int16_t)w) / 2, y);
  g->print(s);
}

void drawOledPortal() {
  Adafruit_GFX* g = oledGfx();
  if (!g) return;
  oledClear();
  g->setTextColor(SSD1306_WHITE);
  oledPrintCentered(g, 2, 1, "CAI DAT WIFI");
  g->drawFastHLine(16, 14, 96, SSD1306_WHITE);
  oledPrintCentered(g, 18, 1, "Noi DT:");
  oledPrintCentered(g, 30, 2, "AI_SinhHoc");
  oledPrintCentered(g, 52, 1, "192.168.4.1");
  oledShow();
}

void drawOled() {
  if (!oledOk) return;
  Adafruit_GFX* g = oledGfx();
  if (!g) return;

  // Portal: màn hướng dẫn rõ cho khách — không hiện số đo
  if (portalActive) {
    drawOledPortal();
    return;
  }

  oledClear();
  g->setTextColor(SSD1306_WHITE);

  char tBuf[16], hBuf[16], cBuf[12];
  if (isnan(bioTemp)) snprintf(tBuf, sizeof(tBuf), "--.-");
  else snprintf(tBuf, sizeof(tBuf), "%.1f", bioTemp);
  if (isnan(bioHum)) snprintf(hBuf, sizeof(hBuf), "--");
  else snprintf(hBuf, sizeof(hBuf), "%.0f", bioHum);
  if (bioCo2 == 0) snprintf(cBuf, sizeof(cBuf), "----");
  else snprintf(cBuf, sizeof(cBuf), "%u", (unsigned)bioCo2);

  // ══ HERO: CO2 ══════════════════════════════════════════════
  oledPrintCentered(g, 0, 1, "CO2");
  uint8_t cSize = (strlen(cBuf) <= 3) ? 3 : 2;
  int16_t cY = (cSize == 3) ? 10 : 12;
  oledPrintCentered(g, cY, cSize, cBuf);
  oledPrintCentered(g, (cSize == 3) ? 36 : 30, 1, "ppm");

  g->drawFastHLine(10, 44, 108, SSD1306_WHITE);

  oledPrintCenteredIn(g, 0, 63, 48, 1, "TEMP");
  char tLine[16];
  snprintf(tLine, sizeof(tLine), "%sC", tBuf);
  oledPrintCenteredIn(g, 0, 63, 56, 1, tLine);

  oledPrintCenteredIn(g, 65, 63, 48, 1, "HUM");
  char hLine[16];
  snprintf(hLine, sizeof(hLine), "%s%%", hBuf);
  oledPrintCenteredIn(g, 65, 63, 56, 1, hLine);

  g->drawFastVLine(64, 47, 15, SSD1306_WHITE);

  oledShow();
}

// ── MQTT ─────────────────────────────────────────────────────
// QoS0 + retain — QoS1 làm MQTTPubSubClient/WSS dễ drop sau pub (app lệch OLED)
void mqttPub(const char* device, const String& payload, bool retain = true) {
  if (!mqttClient.isConnected()) return;
  String topic = String("tele/") + device + "/status";
  mqttClient.publish(topic, payload, retain, 0);
}

void pubOnline() { mqttPub(CHIP_ID, "online", true); }

void pubTemp() {
  if (isnan(bioTemp)) return;
  mqttPub(DEV_TEMP, "{\"value\":" + String(bioTemp, 1) + "}", true);
}

void pubHumi() {
  if (isnan(bioHum)) return;
  mqttPub(DEV_HUMI, "{\"value\":" + String(bioHum, 1) + "}", true);
}

void pubCo2() {
  if (bioCo2 == 0) return;
  mqttPub(DEV_CO2, "{\"value\":" + String(bioCo2) + "}", true);
}

void pubAll() {
  pubTemp();
  pubHumi();
  pubCo2();
}

bool mqttStarted = false;
bool mqttNeedConnect = false;

static void openWss() {
  wsClient.setExtraHeaders("Sec-WebSocket-Protocol: mqtt");
  wsClient.setReconnectInterval(2000);
  wsClient.beginSSL(MQTT_HOST, MQTT_PORT, MQTT_PATH);
  Serial.printf("[WSS] beginSSL %s:%d%s\n", MQTT_HOST, MQTT_PORT, MQTT_PATH);
  wssReady = false;
}

void startMqttStack() {
  if (mqttStarted) return;
  // Thứ tự đã từng OK trên board này: header → beginSSL → mqtt.begin (onEvent BIN)
  openWss();
  mqttClient.begin(wsClient);
  mqttClient.setTimeout(15000);
  mqttClient.setKeepAliveTimeout(20);
  mqttClient.setKeepAliveSendInterval(8000);
  mqttStarted = true;
  mqttNeedConnect = true;
  mqttLoggedOk = false;
}

void stopMqttStack() {
  if (!mqttStarted) return;
  mqttClient.disconnect();
  wsClient.disconnect();
  wssReady = false;
  mqttLoggedOk = false;
  mqttNeedConnect = false;
  mqttStarted = false;
  Serial.println(F("[WSS] stopped"));
}

void reconnectMQTT() {
  if (!mqttStarted) startMqttStack();

  // Chờ WSS (auto reconnect). Không beginSSL trùng khi đang handshake.
  if (!wsClient.isConnected()) {
    wssReady = false;
    wsClient.loop();
    return;
  }
  wssReady = true;

  if (mqttClient.isConnected()) {
    mqttLoggedOk = true;
    mqttNeedConnect = false;
    return;
  }

  Serial.print(F("MQTT WSS ... "));
  String clientId = String("ESP32-BIO-") + String((uint32_t)ESP.getEfuseMac(), HEX) +
                    "-" + String((uint32_t)millis(), HEX);
  String lwt = String("tele/") + CHIP_ID + "/status";
  mqttClient.setWill(lwt, "offline", true, 0);
  mqttClient.setTimeout(15000);

  if (!mqttClient.connect(clientId, "", "")) {
    Serial.printf("fail err=%d rc=%d — reopen WSS\n",
                  (int)mqttClient.getLastError(),
                  (int)mqttClient.getReturnCode());
    mqttLoggedOk = false;
    mqttNeedConnect = true;
    wssReady = false;
    // connect() fail gọi close() → phải beginSSL lại
    delay(100);
    openWss();
    mqttClient.begin(wsClient);
    return;
  }

  Serial.println(F("OK"));
  mqttLoggedOk = true;
  mqttNeedConnect = false;
  pubOnline();
  delay(40);
  pubAll();
  Serial.printf("[MQTT] live CO2=%u -> tele/800_co2/status\n", (unsigned)bioCo2);
}

// ── Captive portal ───────────────────────────────────────────
const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Sinh Hoc WiFi</title>
<style>
body{font-family:system-ui,sans-serif;background:#071a12;color:#e8f5e9;display:flex;justify-content:center;padding:20px;margin:0}
.c{max-width:400px;width:100%}h2{margin:0 0 6px}
.sub{opacity:.75;font-size:14px;margin-bottom:16px}
input{width:100%;padding:12px;margin-bottom:10px;border-radius:10px;border:1px solid #1b4332;background:#0d2818;color:#fff;box-sizing:border-box}
button{padding:12px;width:100%;background:#2d6a4f;color:#fff;border:none;border-radius:10px;font-weight:700}
.scan{background:#40916c;margin-bottom:10px}
.item{padding:10px;background:#0d2818;margin:6px 0;border-radius:10px;border:1px solid #1b4332}
#st{margin-top:12px;font-size:14px;display:none;padding:10px;border-radius:8px;background:#14532d}
</style></head><body><div class="c">
<h2>AI SINH HOC</h2>
<p class="sub">Chi mang <b>2.4GHz</b> (khong dung 5GHz). May thu ket noi truoc khi luu. Len trang nay = may dang cho cau hinh WiFi nha ban.</p>
<button class="scan" onclick="scan()">Quet mang WiFi</button>
<div id="w"></div>
<input id="s" placeholder="Ten WiFi nha ban" autocomplete="off" autocapitalize="off">
<input type="password" id="p" placeholder="Mat khau WiFi" autocomplete="off">
<button onclick="conn()">Ket noi</button>
<div id="st"></div>
</div>
<script>
function esc(t){return String(t).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;');}
function scan(){
  document.getElementById('w').innerHTML='Dang quet (~6s)... giu dien thoai noi AI_SinhHoc';
  fetch('/scan').then(r=>{ if(!r.ok) throw 0; return r.json(); }).then(l=>{
    const list=(l||[]).filter(n=>n && n.ssid);
    document.getElementById('w').innerHTML=list.map(n=>
      `<div class="item" onclick="document.getElementById('s').value=this.dataset.s" data-s="${esc(n.ssid)}">${esc(n.ssid)} (${n.rssi} dBm)</div>`
    ).join('')||'Khong thay mang 2.4GHz — thu Quet lai hoac go ten WiFi thu cong';
  }).catch(()=>document.getElementById('w').innerHTML='Loi quet — noi lai WiFi <b>AI_SinhHoc</b> roi bam Quet lai');
}
function conn(){
  const s=document.getElementById('s').value.trim();
  const p=document.getElementById('p').value;
  if(!s){alert('Nhap ten WiFi');return;}
  const st=document.getElementById('st');
  st.style.display='block';
  st.textContent='Dang thu ket noi (~15s). Khong doi WiFi dien thoai trong luc nay...';
  fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)})
  .then(r=>r.json()).then(d=>{
    if(d.ok){ st.textContent='OK — da luu WiFi, may restart. OLED se het man CAI DAT WIFI.'; }
    else { st.textContent='Loi: '+(d.message||'sai mat khau / khong phai 2.4GHz'); }
  }).catch(()=>{
    st.textContent='Mat ket noi AP. Neu OLED het man CAI DAT WIFI → thanh cong. Neu van thay portal → noi lai AI_SinhHoc va thu lai.';
  });
}
scan();
</script></body></html>
)rawhtml";

void handleRoot() { webServer.send_P(200, "text/html", PORTAL_HTML); }

// Captive portal: nhiều máy Android/iOS chỉ mở trang setup khi probe trả HTML/204 đúng
void handleCaptiveProbe() {
  // Trả portal HTML (200) — ổn định hơn 302 trên một số ROM/iOS
  webServer.send_P(200, "text/html", PORTAL_HTML);
}

void handleNotFound() {
  // Vẫn redirect + fallback HTML nếu client không theo Location
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.send_P(302, "text/html", PORTAL_HTML);
}

void handleScan() {
  // Giữ AP_STA; sau scan ESP32 hay tụt softAP → restore trước khi trả JSON
  WiFi.mode(WIFI_AP_STA);
  ensureSoftAP();
  delay(50);
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);

  String bestSsid[24];
  int bestRssi[24];
  int out = 0;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      int rssi = WiFi.RSSI(i);
      int found = -1;
      for (int j = 0; j < out; j++) {
        if (bestSsid[j] == ssid) { found = j; break; }
      }
      if (found >= 0) {
        if (rssi > bestRssi[found]) bestRssi[found] = rssi;
      } else if (out < 24) {
        bestSsid[out] = ssid;
        bestRssi[out] = rssi;
        out++;
      }
    }
  }
  WiFi.scanDelete();
  delay(40);
  ensureSoftAP();
  delay(40);
  ensureSoftAP();

  // RSSI cao → thấp
  for (int i = 1; i < out; i++) {
    String s = bestSsid[i];
    int r = bestRssi[i], j = i;
    while (j > 0 && bestRssi[j - 1] < r) {
      bestSsid[j] = bestSsid[j - 1];
      bestRssi[j] = bestRssi[j - 1];
      j--;
    }
    bestSsid[j] = s;
    bestRssi[j] = r;
  }

  String json = "[";
  for (int i = 0; i < out; i++) {
    if (i) json += ',';
    String esc = bestSsid[i];
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    json += "{\"ssid\":\"" + esc + "\",\"rssi\":" + String(bestRssi[i]) + "}";
  }
  json += "]";
  Serial.printf("[PORTAL] scan -> %d mang\n", out);
  webServer.sendHeader("Cache-Control", "no-cache");
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "application/json", json);
}

void handleConnect() {
  // Thử STA trước → chỉ lưu + restart khi OK (tránh lưu sai mật khẩu)
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  ssid.trim();
  if (!ssid.length()) {
    webServer.send(200, "application/json", "{\"ok\":false,\"message\":\"chua nhap ten WiFi\"}");
    return;
  }
  if (ssid.length() > 32) {
    webServer.send(200, "application/json", "{\"ok\":false,\"message\":\"ten WiFi qua dai\"}");
    return;
  }
  if (pass.length() > 63) {
    webServer.send(200, "application/json", "{\"ok\":false,\"message\":\"mat khau qua dai\"}");
    return;
  }

  Serial.printf("[PORTAL] Thu ket noi '%s'...\n", ssid.c_str());
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  ensureSoftAP();
  // Ngắt STA cũ (nếu có) rồi begin — giữ softAP cho điện thoại
  WiFi.disconnect(false, false);
  delay(100);
  ensureSoftAP();
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 36 && WiFi.status() != WL_CONNECTED; i++) {
    delay(150);
    dnsServer.processNextRequest();
    webServer.handleClient();
    delay(350);
    if ((i % 6) == 5) ensureSoftAP();  // giữ AP sống khi STA handshake
  }

  if (WiFi.status() == WL_CONNECTED) {
    addOrUpdateWifi(ssid, pass);
    Serial.printf("[PORTAL] OK %s — luu EEPROM + restart\n",
                  WiFi.localIP().toString().c_str());
    webServer.send(200, "application/json", "{\"ok\":true}");
    delay(1500);  // cho phone nhận JSON trước khi AP tắt
    ESP.restart();
  }

  Serial.printf("[PORTAL] FAIL status=%d — khong luu\n", (int)WiFi.status());
  WiFi.disconnect(true, false);
  delay(100);
  ensureSoftAP();
  delay(50);
  ensureSoftAP();
  webServer.send(200, "application/json",
                 "{\"ok\":false,\"message\":\"sai mat khau hoac khong phai 2.4GHz\"}");
}

void startPortal() {
  portalActive = true;
  if (mqttStarted) stopMqttStack();
  Serial.println(F("\n[PORTAL] AP_STA AI_SinhHoc — cau hinh WiFi khach"));

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.disconnect(true, false);
  delay(80);
  bool apOk = ensureSoftAP();
  Serial.printf("[PORTAL] softAP=%s IP=%s\n",
                apOk ? "OK" : "FAIL", WiFi.softAPIP().toString().c_str());

  dnsServer.stop();
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.stop();
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/scan", HTTP_GET, handleScan);
  webServer.on("/connect", HTTP_POST, handleConnect);
  webServer.on("/generate_204", HTTP_GET, handleCaptiveProbe);
  webServer.on("/gen_204", HTTP_GET, handleCaptiveProbe);
  webServer.on("/connecttest.txt", HTTP_GET, handleCaptiveProbe);
  webServer.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
  webServer.on("/fwlink/", HTTP_GET, handleCaptiveProbe);
  webServer.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
  webServer.on("/library/test/success.html", HTTP_GET, handleCaptiveProbe);
  webServer.on("/canonical.html", HTTP_GET, handleCaptiveProbe);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  Serial.println(F("[PORTAL] Dien thoai noi WiFi AI_SinhHoc → http://192.168.4.1"));
}

// ── Setup / Loop ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== AI Sinh Hoc ESP32 ==="));
  Serial.println(F("OLED I2C0 21/22 | ENS160 Wire1 25/26 | DHT11 GPIO27"));
  Serial.println(F("Portal: tu mo khi khong thay WiFi da luu (chuyen cho khach)"));

  // OLED — Wire (I2C0): GPIO21=SDA, GPIO22=SCL
  oledOk = initOled();

  // ENS160 + AHT21 — Wire1 (I2C1)
  Wire1.begin(PIN_SENS_SDA, PIN_SENS_SCL);
  Wire1.setClock(100000);
  scanI2cBus(Wire1, "SENS");
  ensOk = ens160.begin(Wire1, ENS160_ADDRESS_HIGH);
  if (!ensOk) ensOk = ens160.begin(Wire1, ENS160_ADDRESS_LOW);
  if (ensOk) {
    ens160.setOperatingMode(SFE_ENS160_STANDARD);
    Serial.println(F("ENS160 OK"));
  } else {
    Serial.println(F("ENS160 FAIL"));
  }

  ahtOk = aht.begin(&Wire1);
  Serial.println(ahtOk ? F("AHT21 OK") : F("AHT21 FAIL (dung DHT11)"));

  dht.begin();
  delay(1500);
  readSensors();
  drawOled();

  loadWifiList();

  // Đặt 1 chỉ khi test portal tại xưởng; bàn giao phải = 0 (giữ WiFi đã lưu trong EEPROM)
#ifndef FORCE_PORTAL_TEST
#define FORCE_PORTAL_TEST 0
#endif
#if FORCE_PORTAL_TEST
  Serial.println(F("[TEST] FORCE portal — EEPROM WiFi KHONG bi xoa"));
  startPortal();
  return;
#endif

  // Chưa có WiFi / WiFi chỗ cũ không còn → portal (khách tự cấu hình, không cần nút)
  if (wifiCount <= 0) {
    startPortal();
  } else {
    bool noSavedVisible = false;
    bool ok = connectBestWifi(&noSavedVisible);
    if (!ok && !noSavedVisible) {
      // SSID còn thấy nhưng sai pass / yếu — thử thêm 1 lần
      Serial.println(F("[WiFi] boot retry 1/1"));
      delay(600);
      ok = connectBestWifi(&noSavedVisible);
    }
    if (ok) {
      Serial.println(F("Boot OK — WiFi co, bat MQTT"));
      startMqttStack();
    } else {
      // Chuyển chỗ (không thấy SSID cũ): xóa WiFi chỗ cũ → portal cho khách
      // Còn thấy SSID nhưng sai pass: giữ list, mở portal để nhập lại
      openPortalForNewWifi(/*clearOldSaved=*/noSavedVisible);
    }
  }
}

void loop() {
  if (portalActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    // SoftAP ESP32 đôi khi tụt im — refresh định kỳ để khách không mất AP giữa chừng
    static unsigned long lastApKeep = 0;
    if (millis() - lastApKeep >= 8000) {
      lastApKeep = millis();
      if (WiFi.softAPgetStationNum() == 0) ensureSoftAP();
    }
    if (millis() - lastOled >= OLED_MS) {
      lastOled = millis();
      drawOled();
    }
    return;
  }

  unsigned long now = millis();

  // Mất WiFi → thử lại; không thấy SSID đã lưu (đổi chỗ) → portal ngay
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttStarted) stopMqttStack();
    if (now - lastReconnect >= RECONNECT_MS) {
      lastReconnect = now;
      bool noSavedVisible = false;
      if (connectBestWifi(&noSavedVisible)) {
        wifiRetries = 0;
        startMqttStack();
      } else if (noSavedVisible) {
        // Đổi chỗ / WiFi cũ biến mất → portal ngay (không cần tắt nguồn)
        Serial.println(F("[WiFi] Khong thay mang da luu — mo Portal cho khach"));
        wifiRetries = 0;
        openPortalForNewWifi(/*clearOldSaved=*/true);
      } else {
        // SSID vẫn thấy (mất sóng tạm) — chỉ thử lại, không kẹt portal
        wifiRetries++;
        Serial.printf("[WiFi] con SSID nhung fail — retry #%d\n", wifiRetries);
      }
    }
    if (now - tRead >= READ_MS) {
      tRead = now;
      readSensors();
    }
    if (now - lastOled >= OLED_MS) {
      lastOled = now;
      drawOled();
    }
    return;
  }
  wifiRetries = 0;

  if (!mqttStarted) startMqttStack();

  wsClient.loop();
  if (mqttClient.isConnected()) mqttClient.update();

  if (!wssReady && wsClient.isConnected()) {
    wssReady = true;
    lastMqttRetry = 0;
    Serial.println(F("[WSS] CONNECTED"));
  }
  if (mqttStarted && !wsClient.isConnected()) wssReady = false;

  // Luôn retry MQTT/WSS kể cả khi wssReady=false (sau fail err=-5)
  if ((mqttNeedConnect || !mqttClient.isConnected()) &&
      now - lastMqttRetry >= 3000) {
    lastMqttRetry = now;
    reconnectMQTT();
  }

  // Cùng vòng đọc → OLED + MQTT cùng số
  if (now - tRead >= READ_MS) {
    tRead = now;
    readSensors();
    if (mqttClient.isConnected()) {
      pubAll();
      lastTelemetry = now;
      Serial.printf("[MQTT] sync OLED CO2=%u\n", (unsigned)bioCo2);
    }
  }

  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    pubOnline();
  }

  if (now - lastOled >= OLED_MS) {
    lastOled = now;
    drawOled();
  }
}
