/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║   AI SINH HỌC — ESP32 + DHT11 + ENS160 + OLED 1.3"         ║
 * ║   MQTT AloT (mqtt.duynguyen.io.vn WSS) + WiFi Portal        ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * PINOUT:
 *   OLED 1.3" (SH1106)  SCL/SDA → GPIO 22 / GPIO 21   (Wire  I2C0)
 *   ENS160+AHT21        SCL/SDA → GPIO 26 / GPIO 25   (Wire1 I2C1)
 *   DHT11               DATA    → GPIO 27
 *   Nguồn chung         3V3 / GND
 *
 * MQTT (chipId = 800):
 *   tele/800/status           online|offline
 *   tele/800_temp/status      {"value":25.4}
 *   tele/800_humi/status      {"value":61.0}
 *   tele/800_co2/status       {"value":450}
 *
 * THƯ VIỆN:
 *   WebSockets (Markus Sattler), MQTTPubSubClient (Hideaki Tai)
 *   DHT sensor library + Adafruit Unified Sensor
 *   SparkFun Indoor Air Quality Sensor - ENS160
 *   Adafruit SH110X, Adafruit GFX, Adafruit AHTX0
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
#define TELEMETRY_MS  5000
#define HEARTBEAT_MS  30000
#define RECONNECT_MS  10000
#define OLED_MS       1000

struct WifiEntry {
  char ssid[33];
  char pass[65];
};
WifiEntry wifiList[MAX_WIFI];
int wifiCount = 0;

Adafruit_SH1106G oled(OLED_W, OLED_H, &Wire, -1);
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
  // Bỏ entry EEPROM rác (ký tự không in được)
  for (int i = 0; i < 32 && ssid[i]; i++) {
    char c = ssid[i];
    if (c < 32 || c > 126) return false;
  }
  return true;
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
  for (int i = 0; i < wifiCount; i++) {
    if (ssid == wifiList[i].ssid) {
      pass.toCharArray(wifiList[i].pass, sizeof(wifiList[i].pass));
      // Đưa mạng vừa cập nhật lên cuối (ưu tiên kết nối)
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
    ssid.toCharArray(wifiList[wifiCount].ssid, sizeof(wifiList[wifiCount].ssid));
    pass.toCharArray(wifiList[wifiCount].pass, sizeof(wifiList[wifiCount].pass));
    wifiCount++;
  } else {
    for (int i = 0; i < MAX_WIFI - 1; i++) wifiList[i] = wifiList[i + 1];
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

  if (ensOk && ens160.checkDataStatus()) {
    bioCo2 = ens160.getECO2();
    if (bioCo2 < 400) bioCo2 = 400;  // sàn eCO2 điển hình
  }

  Serial.printf("T=%.1fC H=%.0f%% CO2=%u ppm\n",
                bioTemp, bioHum, (unsigned)bioCo2);
}

// ── OLED ─────────────────────────────────────────────────────
void drawOled() {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextColor(SH110X_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(F("AI SINH HOC"));
  if (WiFi.status() == WL_CONNECTED) {
    oled.setCursor(90, 0);
    oled.print(mqttClient.isConnected() ? F("MQTT") : F("WiFi"));
  } else if (portalActive) {
    oled.setCursor(64, 0);
    oled.print(F("AP SETUP"));
  }
  oled.drawLine(0, 10, 127, 10, SH110X_WHITE);

  // Nhiệt độ
  oled.setCursor(0, 16);
  oled.print(F("Nhiet do"));
  oled.setTextSize(2);
  oled.setCursor(0, 26);
  if (isnan(bioTemp)) oled.print(F("--.-"));
  else oled.printf("%.1f", bioTemp);
  oled.setTextSize(1);
  oled.print(F(" C"));

  // Độ ẩm
  oled.setTextSize(1);
  oled.setCursor(72, 16);
  oled.print(F("Do am"));
  oled.setTextSize(2);
  oled.setCursor(72, 26);
  if (isnan(bioHum)) oled.print(F("--"));
  else oled.printf("%.0f", bioHum);
  oled.setTextSize(1);
  oled.print(F("%"));

  // CO2 full width
  oled.drawLine(0, 46, 127, 46, SH110X_WHITE);
  oled.setCursor(0, 50);
  oled.print(F("CO2 "));
  oled.setTextSize(2);
  oled.setCursor(28, 48);
  if (bioCo2 == 0) oled.print(F("----"));
  else oled.printf("%u", (unsigned)bioCo2);
  oled.setTextSize(1);
  oled.print(F(" ppm"));

  oled.display();
}

// ── MQTT ─────────────────────────────────────────────────────
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

void reconnectMQTT() {
  if (mqttClient.isConnected()) return;
  Serial.print(F("MQTT WSS ... "));
  String clientId = String("ESP32-BIO-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  String lwt = String("tele/") + CHIP_ID + "/status";
  mqttClient.setWill(lwt, "offline", true, 1);
  if (!mqttClient.connect(clientId, "", "")) {
    Serial.println(F("fail"));
    mqttLoggedOk = false;
    return;
  }
  Serial.println(F("OK"));
  mqttLoggedOk = true;
  pubOnline();
  delay(20);
  pubAll();
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

void handleNotFound() {
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.send(302, "text/plain", "");
}
void handleRoot() { webServer.send_P(200, "text/html", PORTAL_HTML); }

void handleScan() {
  // Bật tạm AP+STA để scan, rồi về AP thuần (tránh làm rớt portal)
  WiFi.mode(WIFI_AP_STA);
  delay(40);
  int n = WiFi.scanNetworks(false, false, false, 120);
  String json = "[";
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      if (i) json += ',';
      String ssid = WiFi.SSID(i);
      ssid.replace("\\", "\\\\");
      ssid.replace("\"", "\\\"");
      json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    WiFi.scanDelete();
  }
  json += "]";
  WiFi.mode(WIFI_AP);
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

  // OLED — Wire (I2C0)
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  oledOk = oled.begin(OLED_ADDR, true);
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SH110X_WHITE);
    oled.setCursor(8, 28);
    oled.print(F("AI Sinh Hoc..."));
    oled.display();
    Serial.println(F("OLED OK"));
  } else {
    Serial.println(F("OLED FAIL — thu dia chi 0x3D neu can"));
  }

  // ENS160 + AHT21 — Wire1 (I2C1)
  Wire1.begin(PIN_SENS_SDA, PIN_SENS_SCL);
  Wire1.setClock(100000);
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

  wsClient.beginSSL(MQTT_HOST, MQTT_PORT, MQTT_PATH);
  wsClient.setExtraHeaders("Sec-WebSocket-Protocol: mqtt");
  mqttClient.begin(wsClient);

  loadWifiList();
  checkDoubleReset();

  if (isDoubleReset) startPortal();
  else if (!(wifiCount > 0 && connectBestWifi())) startPortal();
  else Serial.println(F("Boot OK — cho app mobile / Xiaozhi"));
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
    if (now - lastReconnect >= RECONNECT_MS) {
      lastReconnect = now;
      if (connectBestWifi()) {
        wifiRetries = 0;
      } else if (++wifiRetries >= 2) {
        // Mất WiFi / sai mật khẩu → về portal sớm (không chờ lâu)
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

  wsClient.loop();
  mqttClient.update();

  if (!wssReady && wsClient.isConnected()) {
    wssReady = true;
    lastMqttRetry = 0;
  }
  if (!wsClient.isConnected()) wssReady = false;

  if (wssReady && !mqttClient.isConnected() && now - lastMqttRetry >= 5000) {
    lastMqttRetry = now;
    reconnectMQTT();
  }

  if (now - tRead >= READ_MS) {
    tRead = now;
    readSensors();
  }

  if (now - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = now;
    pubAll();
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
