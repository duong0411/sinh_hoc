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
#define AP_PASSWORD   ""
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
bool pendingStaConnect = false;
char pendingSsid[33] = {0};
char pendingPass[65] = {0};
unsigned long pendingStaAt = 0;

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
    addr += sizeof(WifiEntry);
  }
  EEPROM.end();
}

void addOrUpdateWifi(String ssid, String pass) {
  for (int i = 0; i < wifiCount; i++) {
    if (String(wifiList[i].ssid) == ssid) {
      pass.toCharArray(wifiList[i].pass, sizeof(wifiList[i].pass));
      saveWifiList();
      return;
    }
  }
  if (wifiCount >= MAX_WIFI) {
    for (int i = 0; i < MAX_WIFI - 1; i++) wifiList[i] = wifiList[i + 1];
    wifiCount = MAX_WIFI - 1;
  }
  ssid.toCharArray(wifiList[wifiCount].ssid, sizeof(wifiList[wifiCount].ssid));
  pass.toCharArray(wifiList[wifiCount].pass, sizeof(wifiList[wifiCount].pass));
  wifiCount++;
  saveWifiList();
}

bool connectBestWifi() {
  Serial.println(F("\nQuet WiFi..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(100);
  if (wifiCount == 0) return false;

  int n = WiFi.scanNetworks();
  int bestIdx = -1, bestRSSI = -999;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String scanned = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      for (int w = 0; w < wifiCount; w++) {
        if (String(wifiList[w].ssid) == scanned && rssi > bestRSSI) {
          bestRSSI = rssi;
          bestIdx = w;
        }
      }
    }
    WiFi.scanDelete();
  }
  if (bestIdx < 0) bestIdx = wifiCount - 1;

  Serial.printf("Thu ket noi: %s\n", wifiList[bestIdx].ssid);
  WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK " + WiFi.localIP().toString());
    return true;
  }
  Serial.println(F("\nWiFi fail"));
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
    oled.setCursor(78, 0);
    oled.print(F("AP"));
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
<title>AI Sinh Hoc</title>
<style>
body{font-family:system-ui,sans-serif;background:#071a12;color:#e8f5e9;display:flex;justify-content:center;padding:20px;margin:0}
.c{max-width:400px;width:100%}h2{margin:0 0 6px;letter-spacing:.04em}
.sub{opacity:.75;font-size:14px;margin-bottom:16px}
input{width:100%;padding:12px;margin-bottom:10px;border-radius:10px;border:1px solid #1b4332;background:#0d2818;color:#fff;box-sizing:border-box}
button{padding:12px;width:100%;background:#2d6a4f;color:#fff;border:none;border-radius:10px;font-weight:700}
.scan{background:#40916c;margin-bottom:10px}
.item{padding:10px;background:#0d2818;margin:6px 0;border-radius:10px;border:1px solid #1b4332}
#st{margin-top:12px;font-size:14px}
</style></head><body><div class="c">
<h2>AI SINH HOC</h2>
<p class="sub">Cau hinh WiFi — DHT11 / ENS160 / OLED</p>
<button class="scan" onclick="scan()">Quet mang WiFi</button>
<div id="w"></div>
<input id="s" placeholder="Ten WiFi">
<input type="password" id="p" placeholder="Mat khau">
<button onclick="conn()">Ket noi</button>
<div id="st"></div>
</div>
<script>
function scan(){document.getElementById('w').innerHTML='Dang quet...';
fetch('/scan').then(r=>r.json()).then(l=>{
document.getElementById('w').innerHTML=l.map(n=>
`<div class="item" onclick="document.getElementById('s').value='${n.ssid.replace(/'/g,"\\'")}'">${n.ssid} (${n.rssi} dBm)</div>`
).join('')}).catch(()=>document.getElementById('w').innerHTML='Loi quet')}
function conn(){
const s=document.getElementById('s').value,p=document.getElementById('p').value;
if(!s)return alert('Nhap WiFi');
const st=document.getElementById('st');st.textContent='Dang ket noi...';
fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)})
.then(r=>r.json()).then(d=>{st.textContent=d.ok?'OK — dang restart...':'Loi: '+(d.message||'that bai')})
}
</script></body></html>
)rawhtml";

void handleNotFound() {
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.send(302, "text/plain", "");
}
void handleRoot() { webServer.send_P(200, "text/html", PORTAL_HTML); }

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ',';
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  webServer.send(200, "application/json", json);
}

void handleConnect() {
  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("pass");
  if (!ssid.length()) {
    webServer.send(200, "application/json", "{\"ok\":false,\"message\":\"empty\"}");
    return;
  }
  ssid.toCharArray(pendingSsid, sizeof(pendingSsid));
  pass.toCharArray(pendingPass, sizeof(pendingPass));
  pendingStaConnect = true;
  pendingStaAt = millis();
  webServer.send(200, "application/json", "{\"ok\":true,\"message\":\"connecting\"}");
}

void startPortal() {
  portalActive = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dnsServer.start(DNS_PORT, "*", apIP);
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/scan", HTTP_GET, handleScan);
  webServer.on("/connect", HTTP_POST, handleConnect);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println(F("Portal AP: AI_SinhHoc → http://192.168.4.1"));
}

void processPendingSta() {
  if (!pendingStaConnect) return;
  if (millis() - pendingStaAt < 300) return;
  pendingStaConnect = false;

  WiFi.begin(pendingSsid, pendingPass);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    addOrUpdateWifi(String(pendingSsid), String(pendingPass));
    delay(500);
    ESP.restart();
  } else {
    Serial.println(F("Portal connect fail"));
  }
}

// ── Double reset / BOOT hold ─────────────────────────────────
void checkDoubleReset() {
  if (rtcMagic == DOUBLE_RESET_MAGIC) {
    isDoubleReset = true;
    rtcMagic = 0;
    wifiCount = 0;
    saveWifiList();
    Serial.println(F("Double reset → xoa WiFi"));
  } else {
    isDoubleReset = false;
    rtcMagic = DOUBLE_RESET_MAGIC;
  }
}

void clearDoubleResetFlag() {
  if (rtcMagic == DOUBLE_RESET_MAGIC) rtcMagic = 0;
}

void handleBootButton() {
  bool pressed = digitalRead(PIN_BOOT_BTN) == LOW;
  if (pressed && !bootWasPressed) {
    bootWasPressed = true;
    bootPressStart = millis();
    bootHoldUiShown = false;
  } else if (pressed && bootWasPressed) {
    if (!bootHoldUiShown && millis() - bootPressStart >= BOOT_HOLD_MS) {
      bootHoldUiShown = true;
      Serial.println(F("BOOT hold → portal"));
      wifiCount = 0;
      saveWifiList();
      startPortal();
    }
  } else {
    bootWasPressed = false;
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
    processPendingSta();
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
      if (connectBestWifi()) wifiRetries = 0;
      else if (++wifiRetries >= 3) {
        startPortal();
        wifiRetries = 0;
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
