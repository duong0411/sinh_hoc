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
#define PIN_BOOT_BTN  0
#define BOOT_HOLD_MS  3000

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
#define READ_MS       2500
#define TELEMETRY_MS  2500
#define HEARTBEAT_MS  30000
#define RECONNECT_MS  10000
#define OLED_MS       1000

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
bool pendingStaConnect = false;  // giữ tương thích (không dùng — connect = lưu + restart)

unsigned long tRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastReconnect = 0;
unsigned long lastMqttRetry = 0;
unsigned long lastOled = 0;
bool wssReady = false;
bool mqttLoggedOk = false;
int wifiRetries = 0;

unsigned long bootPressStart = 0;
bool bootWasPressed = false;
bool bootHoldUiShown = false;

#define DOUBLE_RESET_MAGIC 0x12345678
RTC_DATA_ATTR uint32_t rtcMagic = 0;
bool isDoubleReset = false;

void drawOled();
void pubAll();
void pubOnline();
void reconnectMQTT();
void startPortal();

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

// Chờ STA — thoát sớm nếu sai mật khẩu / không thấy SSID
bool waitWifiConnected(uint16_t maxMs = 15000) {
  const uint16_t step = 100;
  uint16_t waited = 0;
  while (waited < maxMs) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) return true;
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
      Serial.printf("\n[WiFi] fail status=%d\n", (int)st);
      return false;
    }
    delay(step);
    waited += step;
    if ((waited % 500) == 0) Serial.print('.');
  }
  return WiFi.status() == WL_CONNECTED;
}

bool tryWifiCreds(const char* ssid, const char* pass) {
  if (!wifiSsidOk(ssid)) return false;
  Serial.printf("[WiFi] STA begin '%s'\n", ssid);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.begin(ssid, pass);
  if (waitWifiConnected(15000)) {
    Serial.println("\n[WiFi] OK IP: " + WiFi.localIP().toString());
    return true;
  }
  Serial.printf("\n[WiFi] FAIL status=%d\n", (int)WiFi.status());
  WiFi.disconnect(true, true);
  delay(100);
  return false;
}

bool connectBestWifi() {
  if (wifiCount <= 0) return false;

  // Ưu tiên mạng mới nhất (vừa lưu từ portal) — không phụ thuộc scan
  int last = wifiCount - 1;
  if (tryWifiCreds(wifiList[last].ssid, wifiList[last].pass)) return true;

  for (int w = last - 1; w >= 0; w--) {
    if (tryWifiCreds(wifiList[w].ssid, wifiList[w].pass)) return true;
  }
  return false;
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

  // Luôn lấy eCO2 hiện tại để OLED và MQTT cùng một số
  if (ensOk) {
    ens160.checkDataStatus();
    uint16_t c = ens160.getECO2();
    if (c > 0) {
      bioCo2 = c < 400 ? 400 : c;
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
  oledPrintCentered(g, 4, 1, "CAI DAT WIFI");
  g->drawFastHLine(16, 16, 96, SSD1306_WHITE);
  oledPrintCentered(g, 24, 1, "Noi dien thoai:");
  oledPrintCentered(g, 36, 2, "AI_SinhHoc");
  oledPrintCentered(g, 56, 1, "192.168.4.1");
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
// Telemetry: QoS1 + retain để broker/app luôn nhận đúng số OLED hiện tại
void mqttPub(const char* device, const String& payload, bool retain, uint8_t qos) {
  if (!mqttClient.isConnected()) return;
  String topic = String("tele/") + device + "/status";
  mqttClient.publish(topic, payload, retain, qos);
}

void pubOnline() { mqttPub(CHIP_ID, "online", true, 1); }

void pubTemp() {
  if (isnan(bioTemp)) return;
  mqttPub(DEV_TEMP, "{\"value\":" + String(bioTemp, 1) + "}", true, 1);
}

void pubHumi() {
  if (isnan(bioHum)) return;
  mqttPub(DEV_HUMI, "{\"value\":" + String(bioHum, 1) + "}", true, 1);
}

void pubCo2() {
  if (bioCo2 == 0) return;
  mqttPub(DEV_CO2, "{\"value\":" + String(bioCo2) + "}", true, 1);
}

void pubAll() {
  pubTemp();
  pubHumi();
  pubCo2();
}

bool mqttStarted = false;
bool mqttNeedConnect = false;

void startMqttStack() {
  if (mqttStarted) return;
  Serial.printf("[WSS] beginSSL %s:%d%s\n", MQTT_HOST, MQTT_PORT, MQTT_PATH);
  // fingerprint NULL → thư viện gọi setInsecure() (ESP32)
  wsClient.beginSSL(MQTT_HOST, MQTT_PORT, MQTT_PATH);
  wsClient.setExtraHeaders("Sec-WebSocket-Protocol: mqtt");
  wsClient.setReconnectInterval(3000);
  wsClient.enableHeartbeat(15000, 4000, 2);
  wsClient.onEvent([](WStype_t type, uint8_t* payload, size_t length) {
    (void)payload;
    (void)length;
    switch (type) {
      case WStype_DISCONNECTED:
        wssReady = false;
        mqttLoggedOk = false;
        mqttNeedConnect = false;
        Serial.println(F("[WSS] DISCONNECTED"));
        break;
      case WStype_CONNECTED:
        wssReady = true;
        mqttNeedConnect = true;  // connect MQTT trong loop, không trong callback
        lastMqttRetry = 0;
        Serial.println(F("[WSS] CONNECTED"));
        break;
      case WStype_ERROR:
        wssReady = false;
        Serial.println(F("[WSS] ERROR"));
        break;
      default:
        break;
    }
  });
  mqttClient.begin(wsClient);
  mqttClient.setTimeout(15000);
  mqttClient.setKeepAliveTimeout(20);
  mqttClient.setKeepAliveSendInterval(8000);
  mqttStarted = true;
  wssReady = false;
  mqttNeedConnect = false;
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
  if (!wssReady) return;
  if (mqttClient.isConnected()) {
    mqttLoggedOk = true;
    return;
  }
  Serial.print(F("MQTT WSS ... "));
  // ClientId unique mỗi lần — tránh broker đá session cũ
  String clientId = String("ESP32-BIO-") + String((uint32_t)ESP.getEfuseMac(), HEX) +
                    "-" + String((uint32_t)millis(), HEX);
  String lwt = String("tele/") + CHIP_ID + "/status";
  mqttClient.setWill(lwt, "offline", true, 1);
  if (!mqttClient.connect(clientId, "", "")) {
    Serial.println(F("fail"));
    mqttLoggedOk = false;
    return;
  }
  Serial.println(F("OK"));
  mqttLoggedOk = true;
  mqttNeedConnect = false;
  pubOnline();
  delay(40);
  pubAll();
  Serial.println(F("[MQTT] pub tele/800_*/status"));
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
<p class="sub">Cau hinh WiFi (chi mang 2.4GHz). Giữ BOOT 3s de mo lai portal.</p>
<button class="scan" onclick="scan()">Quet mang WiFi</button>
<div id="w"></div>
<input id="s" placeholder="Ten WiFi" autocomplete="off">
<input type="password" id="p" placeholder="Mat khau" autocomplete="off">
<button onclick="conn()">Luu & Ket noi</button>
<div id="st"></div>
</div>
<script>
function scan(){
  document.getElementById('w').innerHTML='Dang quet...';
  fetch('/scan').then(r=>r.json()).then(l=>{
    document.getElementById('w').innerHTML=l.map(n=>
      `<div class="item" onclick="document.getElementById('s').value=this.dataset.s" data-s="${n.ssid.replace(/"/g,'&quot;')}">${n.ssid} (${n.rssi} dBm)</div>`
    ).join('');
  }).catch(()=>document.getElementById('w').innerHTML='Loi quet mang');
}
function conn(){
  const s=document.getElementById('s').value.trim();
  const p=document.getElementById('p').value;
  if(!s){alert('Nhap ten WiFi');return;}
  const st=document.getElementById('st');
  st.style.display='block';
  st.textContent='Dang luu... may se restart va ket noi WiFi.';
  // Form POST — captive portal dien thoai on dinh hon fetch
  const f=document.createElement('form');
  f.method='POST'; f.action='/connect';
  const a=document.createElement('input'); a.name='ssid'; a.value=s; f.appendChild(a);
  const b=document.createElement('input'); b.name='pass'; b.value=p; f.appendChild(b);
  document.body.appendChild(f); f.submit();
}
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
  // Giữ AP, bật thêm STA chỉ để scan — rồi về AP thuần
  WiFi.mode(WIFI_AP_STA);
  delay(80);
  // async=false, show_hidden=false; timeout ms tùy SDK
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  String json = "[";
  if (n > 0) {
    // Giới hạn 24 mạng — JSON nhỏ, portal không treo
    int lim = n > 24 ? 24 : n;
    for (int i = 0; i < lim; i++) {
      if (i) json += ',';
      String ssid = WiFi.SSID(i);
      ssid.replace("\\", "\\\\");
      ssid.replace("\"", "\\\"");
      json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
  }
  WiFi.scanDelete();
  json += "]";
  // Khôi phục AP + cấu hình IP (một số board mất softAPConfig sau AP_STA)
  WiFi.mode(WIFI_AP);
  delay(30);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAPIP() != apIP) {
    WiFi.softAP(AP_SSID, nullptr, 1, 0, 4);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  }
  webServer.sendHeader("Cache-Control", "no-cache");
  webServer.send(200, "application/json", json);
}

void handleConnect() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  ssid.trim();
  if (!ssid.length()) {
    webServer.send(200, "text/html",
      "<html><body style='background:#071a12;color:#fff;padding:24px;font-family:sans-serif'>"
      "<h2>Loi</h2><p>Chua nhap ten WiFi.</p>"
      "<a style='color:#52b788' href='/'>Quay lai</a></body></html>");
    return;
  }
  if (ssid.length() > 32) {
    webServer.send(200, "text/html",
      "<html><body style='background:#071a12;color:#fff;padding:24px;font-family:sans-serif'>"
      "<h2>Loi</h2><p>Ten WiFi qua dai (toi da 32 ky tu).</p>"
      "<a style='color:#52b788' href='/'>Quay lai</a></body></html>");
    return;
  }

  // Lưu EEPROM trước → hiện trang OK trên AP → restart → STA thuần
  addOrUpdateWifi(ssid, pass);
  Serial.printf("[PORTAL] Da luu '%s' (pass len=%u) — restart\n",
                ssid.c_str(), (unsigned)pass.length());

  String html =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Da luu</title></head>"
    "<body style='background:#071a12;color:#e8f5e9;font-family:sans-serif;padding:28px;text-align:center'>"
    "<h2 style='color:#52b788'>Da luu WiFi!</h2>"
    "<p>May dang <b>restart</b> de ket noi <b>";
  html += ssid;
  html +=
    "</b>.</p>"
    "<p style='color:#95d5b2'>Doi 15–20 giay. Mat mang AI_SinhHoc la binh thuong.</p>"
    "<p>Neu that bai: noi lai <b>AI_SinhHoc</b>, kiem tra mat khau (dung <b>2.4GHz</b>).</p>"
    "<p>Hoac giu nut <b>BOOT 3 giay</b> de mo lai portal.</p>"
    "</body></html>";

  webServer.send(200, "text/html", html);
  webServer.client().flush();
  delay(2000);  // đủ thời gian điện thoại tải trang
  ESP.restart();
}

void startPortal() {
  portalActive = true;
  pendingStaConnect = false;
  Serial.println(F("\n[PORTAL] Khoi tao AP..."));

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(80);
  WiFi.mode(WIFI_AP);
  delay(50);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  // AP mở — nullptr ổn định hơn chuỗi rỗng trên ESP32
  bool apOk = WiFi.softAP(AP_SSID, nullptr, 1, 0, 4);
  Serial.printf("[PORTAL] softAP=%s SSID=%s IP=%s\n",
                apOk ? "OK" : "FAIL", AP_SSID, WiFi.softAPIP().toString().c_str());

  dnsServer.stop();
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.stop();
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/scan", HTTP_GET, handleScan);
  webServer.on("/connect", HTTP_POST, handleConnect);
  // Android captive
  webServer.on("/generate_204", HTTP_GET, handleCaptiveProbe);
  webServer.on("/gen_204", HTTP_GET, handleCaptiveProbe);
  webServer.on("/connecttest.txt", HTTP_GET, handleCaptiveProbe);
  // Windows NCSI
  webServer.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
  webServer.on("/fwlink/", HTTP_GET, handleCaptiveProbe);
  // iOS / macOS captive
  webServer.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
  webServer.on("/library/test/success.html", HTTP_GET, handleCaptiveProbe);
  webServer.on("/canonical.html", HTTP_GET, handleCaptiveProbe);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  Serial.println(F("[PORTAL] Noi WiFi 'AI_SinhHoc' → http://192.168.4.1"));
}

// ── Double reset / BOOT hold ─────────────────────────────────
void checkDoubleReset() {
  if (rtcMagic == DOUBLE_RESET_MAGIC) {
    isDoubleReset = true;
    rtcMagic = 0;
    wifiCount = 0;
    memset(wifiList, 0, sizeof(wifiList));
    saveWifiList();
    Serial.println(F("DOUBLE RESET — xoa WiFi, mo Portal"));
  } else {
    isDoubleReset = false;
    rtcMagic = DOUBLE_RESET_MAGIC;
  }
}

void clearDoubleResetFlag() {
  if (rtcMagic == DOUBLE_RESET_MAGIC) rtcMagic = 0;
}

void handleBootButton() {
  // Chỉ dùng khi chưa vào portal — giữ BOOT 3s → xóa WiFi + mở AP
  if (portalActive) return;
  bool pressed = digitalRead(PIN_BOOT_BTN) == LOW;
  if (pressed) {
    if (!bootWasPressed) {
      bootWasPressed = true;
      bootHoldUiShown = false;
      bootPressStart = millis();
      Serial.println(F("[BOOT] Giu 3s de xoa WiFi + mo Portal"));
    }
    if (!bootHoldUiShown && millis() - bootPressStart >= BOOT_HOLD_MS) {
      bootHoldUiShown = true;
      Serial.println(F("[BOOT] Xoa WiFi + mo Portal"));
      wifiCount = 0;
      memset(wifiList, 0, sizeof(wifiList));
      saveWifiList();
      WiFi.disconnect(true, true);
      bootWasPressed = false;
      startPortal();
    }
  } else {
    bootWasPressed = false;
    bootHoldUiShown = false;
  }
}

// ── Setup / Loop ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== AI Sinh Hoc ESP32 ==="));
  Serial.println(F("OLED I2C0 21/22 | ENS160 Wire1 25/26 | DHT11 GPIO27"));

  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

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
  checkDoubleReset();

  if (isDoubleReset) {
    startPortal();
  } else if (!(wifiCount > 0 && connectBestWifi())) {
    startPortal();
  } else {
    Serial.println(F("Boot OK — WiFi co, bat MQTT"));
    startMqttStack();
  }
}

void loop() {
  if (millis() > 4000) clearDoubleResetFlag();
  handleBootButton();

  if (portalActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    if (millis() - lastOled >= OLED_MS) {
      lastOled = millis();
      drawOled();
    }
    return;
  }

  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (mqttStarted) stopMqttStack();
    if (now - lastReconnect >= RECONNECT_MS) {
      lastReconnect = now;
      if (connectBestWifi()) {
        wifiRetries = 0;
        startMqttStack();
      } else if (++wifiRetries >= 2) {
        Serial.println(F("[WiFi] Mat mang — mo Portal de cau hinh lai"));
        wifiRetries = 0;
        startPortal();
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
  mqttClient.update();

  if (!wssReady && wsClient.isConnected()) {
    wssReady = true;
    lastMqttRetry = 0;
  }
  if (mqttStarted && !wsClient.isConnected()) wssReady = false;

  // MQTT CONNECT khi WSS vừa lên hoặc bị rớt
  if (wssReady && (mqttNeedConnect || !mqttClient.isConnected()) &&
      now - lastMqttRetry >= 2500) {
    lastMqttRetry = now;
    reconnectMQTT();
  }

  // Cùng một vòng đọc → OLED + MQTT cùng số (tránh app giữ retain cũ)
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
