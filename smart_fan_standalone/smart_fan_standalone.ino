// =============================================
//   手宮智慧冷氣 - ESP32 獨立 Web Server 版本
// =============================================
// 功能：
//   1. ESP32 每 2 秒讀取 DHT11 溫濕度
//   2. 按鈕 (D19) 控制 200 秒倒數計時器，按下重置
//   3. 風扇 (D18) 狀態顯示在網頁
//   4. ESP32 自己架設 Web Server，用瀏覽器連線即可
//
// 安裝函式庫（Arduino IDE → Library Manager）：
//   - SimpleDHT
//   - ArduinoJson (by Benoit Blanchon, 6.x)
//
// WebServer 是 ESP32 Arduino Core 內建，不需額外安裝
// =============================================

#include <SimpleDHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// =============================================
// ★ 請修改以下設定 ★
// =============================================
const char* WIFI_SSID = "DFN";          // WiFi 名稱
const char* WIFI_PASS = "Illyasviel1120"; // WiFi 密碼
// =============================================

// --- 腳位設定 ---
const int pinDHT11  = 4;   // DHT11 DATA
const int fanPin    = 18;  // MOSFET Gate（透過 470Ω 電阻）
const int ledPin    = 2;   // 板載 LED
const int buttonPin = 19;  // 按鈕（另一腳接 GND）

// --- 物件 ---
SimpleDHT11 dht11(pinDHT11);
WebServer   server(80);

// --- 感測資料 ---
int  currentTemp = 0;
int  currentHumi = 0;
bool dhtOk       = false;

// --- 風扇狀態 ---
bool fanRunning  = false;
bool fanManual   = false;  // true = 手動模式（網頁控制），false = 自動

// --- 倒數計時器 ---
const int  TIMER_SECONDS   = 200;       // 計時器總秒數
int        timerRemaining  = TIMER_SECONDS;
unsigned long lastTimerTick = 0;        // 上次計時器跳動時間
bool       timerRunning    = true;      // 計時器是否運行中

// --- 按鈕防抖 ---
bool     lastButtonState    = HIGH;
bool     buttonState        = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// --- DHT11 讀取計時 ---
unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2000; // 每 2 秒讀一次

// --- IP 重印計時 ---
unsigned long lastIPPrint = 0;
const unsigned long IP_PRINT_INTERVAL = 10000; // 每 10 秒重印一次 IP

// =============================================
//  產生 HTML 頁面（含自動更新）
// =============================================
String buildHTML() {
  String html = R"rawhtml(<!DOCTYPE html>
<html lang="zh-TW">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>手宮智慧冷氣控制台</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700;800&display=swap" rel="stylesheet">
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    :root {
      --bg:        #0b0f1a;
      --card-bg:   rgba(255,255,255,0.05);
      --border:    rgba(255,255,255,0.10);
      --blue:      #4f9eff;
      --cyan:      #00d4ff;
      --green:     #00e676;
      --orange:    #ff9800;
      --red:       #ff4444;
      --purple:    #bb86fc;
      --text:      #e8eaed;
      --muted:     #888ea8;
    }

    body {
      font-family: 'Outfit', sans-serif;
      background: var(--bg);
      color: var(--text);
      min-height: 100vh;
      padding: 24px 16px 40px;
      background-image:
        radial-gradient(ellipse at 20% 20%, rgba(79,158,255,0.08) 0%, transparent 60%),
        radial-gradient(ellipse at 80% 80%, rgba(187,134,252,0.08) 0%, transparent 60%);
    }

    .container { max-width: 900px; margin: 0 auto; }

    /* ---- Header ---- */
    header {
      text-align: center;
      margin-bottom: 36px;
    }
    header h1 {
      font-size: clamp(1.8rem, 5vw, 2.8rem);
      font-weight: 800;
      background: linear-gradient(135deg, var(--cyan), var(--purple));
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      background-clip: text;
      letter-spacing: -0.5px;
    }
    header p {
      color: var(--muted);
      font-size: 0.9rem;
      margin-top: 6px;
    }
    .status-dot {
      display: inline-block;
      width: 8px; height: 8px;
      border-radius: 50%;
      background: var(--green);
      margin-right: 6px;
      animation: pulse 2s infinite;
    }
    @keyframes pulse {
      0%,100% { opacity: 1; box-shadow: 0 0 0 0 rgba(0,230,118,0.4); }
      50%      { opacity: 0.7; box-shadow: 0 0 0 6px rgba(0,230,118,0); }
    }

    /* ---- Grid ---- */
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
      gap: 20px;
      margin-bottom: 24px;
    }

    /* ---- Card ---- */
    .card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 20px;
      padding: 28px 24px;
      backdrop-filter: blur(12px);
      transition: transform 0.25s ease, border-color 0.25s ease;
      position: relative;
      overflow: hidden;
    }
    .card::before {
      content: '';
      position: absolute;
      top: 0; left: 0; right: 0;
      height: 3px;
      border-radius: 20px 20px 0 0;
    }
    .card:hover { transform: translateY(-4px); }

    .card-temp::before  { background: linear-gradient(90deg, #ff6b6b, #ff9800); }
    .card-humi::before  { background: linear-gradient(90deg, var(--blue), var(--cyan)); }
    .card-fan::before   { background: linear-gradient(90deg, var(--green), #69ff80); }
    .card-timer::before { background: linear-gradient(90deg, var(--purple), #f06292); }

    .card-label {
      font-size: 0.78rem;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      color: var(--muted);
      margin-bottom: 14px;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .card-label .icon { font-size: 1.1rem; }

    .card-value {
      font-size: clamp(2.8rem, 7vw, 4rem);
      font-weight: 800;
      line-height: 1;
      margin-bottom: 8px;
    }
    .card-unit {
      font-size: 1.2rem;
      font-weight: 400;
      color: var(--muted);
      margin-left: 4px;
    }
    .card-sub {
      font-size: 0.82rem;
      color: var(--muted);
      margin-top: 6px;
    }

    /* Temp color */
    .temp-cold  { color: var(--blue); }
    .temp-warm  { color: var(--orange); }
    .temp-hot   { color: var(--red); }

    /* Humi color */
    .humi-dry   { color: #ffe082; }
    .humi-ok    { color: var(--cyan); }
    .humi-wet   { color: var(--blue); }

    /* ---- Fan Status ---- */
    .fan-badge {
      display: inline-flex;
      align-items: center;
      gap: 10px;
      padding: 10px 20px;
      border-radius: 50px;
      font-size: 1rem;
      font-weight: 600;
      margin-top: 8px;
    }
    .fan-on  { background: rgba(0,230,118,0.15); color: var(--green);  border: 1px solid rgba(0,230,118,0.3); }
    .fan-off { background: rgba(136,142,168,0.12); color: var(--muted); border: 1px solid rgba(136,142,168,0.25); }

    /* ---- Fan Toggle Button ---- */
    .btn-fan-wrap {
      display: flex;
      gap: 10px;
      margin-top: 16px;
    }
    .btn-fan {
      flex: 1;
      padding: 11px 8px;
      border-radius: 12px;
      border: none;
      font-family: 'Outfit', sans-serif;
      font-size: 0.9rem;
      font-weight: 600;
      cursor: pointer;
      transition: opacity 0.2s, transform 0.15s, box-shadow 0.2s;
    }
    .btn-fan:hover  { opacity: 0.88; transform: scale(1.02); }
    .btn-fan:active { transform: scale(0.96); }
    .btn-fan-on  {
      background: linear-gradient(135deg, #00e676, #00bcd4);
      color: #0b0f1a;
      box-shadow: 0 0 0 0 rgba(0,230,118,0);
    }
    .btn-fan-on.active {
      box-shadow: 0 0 14px rgba(0,230,118,0.55);
    }
    .btn-fan-off {
      background: linear-gradient(135deg, #ff4444, #ff9800);
      color: #fff;
      box-shadow: 0 0 0 0 rgba(255,68,68,0);
    }
    .btn-fan-off.active {
      box-shadow: 0 0 14px rgba(255,68,68,0.45);
    }
    .fan-mode-badge {
      display: inline-block;
      font-size: 0.72rem;
      padding: 2px 8px;
      border-radius: 20px;
      margin-top: 8px;
      font-weight: 600;
    }
    .mode-manual { background: rgba(255,152,0,0.18); color: var(--orange); }
    .mode-auto   { background: rgba(79,158,255,0.15); color: var(--blue); }

    .btn-auto {
      display: block;
      width: 100%;
      margin-top: 10px;
      padding: 10px;
      border-radius: 12px;
      border: 1px solid rgba(79,158,255,0.35);
      background: rgba(79,158,255,0.10);
      color: var(--blue);
      font-family: 'Outfit', sans-serif;
      font-size: 0.88rem;
      font-weight: 600;
      cursor: pointer;
      transition: background 0.2s, transform 0.15s;
    }
    .btn-auto:hover  { background: rgba(79,158,255,0.22); transform: scale(1.02); }
    .btn-auto:active { transform: scale(0.96); }

    .fan-spin {
      display: inline-block;
      font-size: 1.5rem;
    }
    .spinning { animation: spin 0.8s linear infinite; }
    @keyframes spin { to { transform: rotate(360deg); } }

    /* ---- Timer ---- */
    .timer-ring-wrap {
      display: flex;
      justify-content: center;
      margin: 12px 0;
    }
    .timer-svg { transform: rotate(-90deg); }
    .timer-track { fill: none; stroke: rgba(255,255,255,0.07); stroke-width: 8; }
    .timer-progress {
      fill: none;
      stroke: url(#timerGrad);
      stroke-width: 8;
      stroke-linecap: round;
      transition: stroke-dashoffset 1s linear;
    }
    .timer-text-wrap {
      position: absolute;
      inset: 0;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
    }
    .timer-seconds {
      font-size: 2.2rem;
      font-weight: 800;
      color: var(--purple);
      line-height: 1;
    }
    .timer-label { font-size: 0.7rem; color: var(--muted); margin-top: 2px; }

    .btn-reset {
      display: block;
      width: 100%;
      margin-top: 16px;
      padding: 12px;
      border-radius: 12px;
      border: none;
      background: linear-gradient(135deg, var(--purple), #f06292);
      color: #fff;
      font-family: 'Outfit', sans-serif;
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      transition: opacity 0.2s, transform 0.15s;
    }
    .btn-reset:hover  { opacity: 0.88; transform: scale(1.02); }
    .btn-reset:active { transform: scale(0.97); }

    /* ---- Last update ---- */
    .footer {
      text-align: center;
      color: var(--muted);
      font-size: 0.8rem;
      margin-top: 16px;
    }
    #last-update { color: var(--cyan); font-weight: 600; }

    /* ---- Responsive ---- */
    @media (max-width: 480px) {
      .card { padding: 22px 18px; }
    }
  </style>
</head>
<body>
<div class="container">
  <header>
    <h1>🌬️ 手宮智慧冷氣</h1>
    <p><span class="status-dot"></span>即時監控儀表板 · ESP32 Web Server</p>
  </header>

  <div class="grid">
    <!-- 溫度 -->
    <div class="card card-temp">
      <div class="card-label"><span class="icon">🌡️</span>溫度</div>
      <div class="card-value" id="temp-val">--</div>
      <div class="card-sub" id="temp-hint">讀取中...</div>
    </div>

    <!-- 濕度 -->
    <div class="card card-humi">
      <div class="card-label"><span class="icon">💧</span>濕度</div>
      <div class="card-value" id="humi-val">--</div>
      <div class="card-sub" id="humi-hint">讀取中...</div>
    </div>

    <!-- 風扇 -->
    <div class="card card-fan">
      <div class="card-label"><span class="icon">💨</span>風扇控制</div>
      <div id="fan-badge" class="fan-badge fan-off">
        <span id="fan-icon" class="fan-spin">🔘</span>
        <span id="fan-text">讀取中...</span>
      </div>
      <span id="fan-mode" class="fan-mode-badge mode-auto">自動模式</span>
      <div class="btn-fan-wrap">
        <button id="btn-on"  class="btn-fan btn-fan-on"  onclick="setFan(true)">💨 開啟</button>
        <button id="btn-off" class="btn-fan btn-fan-off" onclick="setFan(false)">⛔ 關閉</button>
      </div>
      <button class="btn-auto" onclick="setAuto()">🤖 切換自動模式</button>
    </div>

    <!-- 計時器 -->
    <div class="card card-timer">
      <div class="card-label"><span class="icon">⏱️</span>倒數計時器</div>
      <div class="timer-ring-wrap">
        <div style="position:relative;width:120px;height:120px;">
          <svg class="timer-svg" width="120" height="120" viewBox="0 0 120 120">
            <defs>
              <linearGradient id="timerGrad" x1="0%" y1="0%" x2="100%" y2="0%">
                <stop offset="0%"   stop-color="#bb86fc"/>
                <stop offset="100%" stop-color="#f06292"/>
              </linearGradient>
            </defs>
            <circle class="timer-track" cx="60" cy="60" r="52"/>
            <circle class="timer-progress" id="timer-ring" cx="60" cy="60" r="52"
              stroke-dasharray="326.73" stroke-dashoffset="0"/>
          </svg>
          <div class="timer-text-wrap">
            <div class="timer-seconds" id="timer-sec">--</div>
            <div class="timer-label">秒</div>
          </div>
        </div>
      </div>
      <button class="btn-reset" onclick="resetTimer()">🔄 重置計時器</button>
    </div>
  </div>

  <div class="footer">上次更新：<span id="last-update">--</span></div>
</div>

<script>
  const TOTAL = 200;
  const CIRC  = 2 * Math.PI * 52; // ≈ 326.73

  function tempClass(t) {
    if (t < 20) return 'temp-cold';
    if (t < 28) return 'temp-warm';
    return 'temp-hot';
  }
  function humiClass(h) {
    if (h < 40) return 'humi-dry';
    if (h < 70) return 'humi-ok';
    return 'humi-wet';
  }
  function tempHint(t) {
    if (t < 20) return '🧊 涼爽';
    if (t < 28) return '😊 舒適';
    return '🔥 偏熱';
  }
  function humiHint(h) {
    if (h < 40) return '🏜️ 偏乾';
    if (h < 70) return '✅ 適中';
    return '💦 偏濕';
  }

  async function fetchData() {
    try {
      const res  = await fetch('/api/data');
      const data = await res.json();

      // 溫度
      const tv = document.getElementById('temp-val');
      tv.innerHTML = data.temperature + '<span class="card-unit">°C</span>';
      tv.className = 'card-value ' + tempClass(data.temperature);
      document.getElementById('temp-hint').textContent = tempHint(data.temperature);

      // 濕度
      const hv = document.getElementById('humi-val');
      hv.innerHTML = data.humidity + '<span class="card-unit">%</span>';
      hv.className = 'card-value ' + humiClass(data.humidity);
      document.getElementById('humi-hint').textContent = humiHint(data.humidity);

      // 風扇
      const badge = document.getElementById('fan-badge');
      const icon  = document.getElementById('fan-icon');
      const ftxt  = document.getElementById('fan-text');
      const modeBadge = document.getElementById('fan-mode');
      const btnOn  = document.getElementById('btn-on');
      const btnOff = document.getElementById('btn-off');

      if (data.fan_running) {
        badge.className = 'fan-badge fan-on';
        icon.className  = 'fan-spin spinning';
        icon.textContent = '🌀';
        ftxt.textContent = '運轉中';
        btnOn.classList.add('active');
        btnOff.classList.remove('active');
      } else {
        badge.className = 'fan-badge fan-off';
        icon.className  = 'fan-spin';
        icon.textContent = '⭕';
        ftxt.textContent = '已停止';
        btnOff.classList.add('active');
        btnOn.classList.remove('active');
      }

      if (data.fan_manual) {
        modeBadge.textContent = '🖐️ 手動模式';
        modeBadge.className = 'fan-mode-badge mode-manual';
      } else {
        modeBadge.textContent = '🤖 自動模式';
        modeBadge.className = 'fan-mode-badge mode-auto';
      }

      // 計時器
      const sec = data.timer_remaining;
      document.getElementById('timer-sec').textContent = sec;
      const offset = CIRC * (1 - sec / TOTAL);
      document.getElementById('timer-ring').style.strokeDashoffset = offset;

      // 更新時間
      const now = new Date();
      document.getElementById('last-update').textContent =
        now.toLocaleTimeString('zh-TW');

    } catch(e) {
      console.warn('fetch error', e);
    }
  }

  async function resetTimer() {
    try {
      await fetch('/api/reset', { method: 'POST' });
      fetchData();
    } catch(e) {}
  }

  async function setFan(state) {
    try {
      await fetch('/api/fan', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ fan: state })
      });
      fetchData();
    } catch(e) {}
  }

  async function setAuto() {
    try {
      await fetch('/api/auto', { method: 'POST' });
      fetchData();
    } catch(e) {}
  }

  // 每 1.5 秒自動更新
  fetchData();
  setInterval(fetchData, 1500);
</script>
</body>
</html>
)rawhtml";
  return html;
}

// =============================================
//  HTTP API 處理
// =============================================

// GET / → 回傳 HTML 頁面
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildHTML());
}

// GET /api/data → 回傳 JSON 狀態
void handleApiData() {
  StaticJsonDocument<256> doc;
  doc["temperature"]     = currentTemp;
  doc["humidity"]        = currentHumi;
  doc["fan_running"]     = fanRunning;
  doc["fan_manual"]      = fanManual;
  doc["timer_remaining"] = timerRemaining;
  doc["dht_ok"]          = dhtOk;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// POST /api/fan → 控制風扇開關
void handleApiFan() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<64> req;
    DeserializationError err = deserializeJson(req, server.arg("plain"));
    if (!err && req.containsKey("fan")) {
      fanManual  = true;                       // 進入手動模式
      fanRunning = req["fan"].as<bool>();
      digitalWrite(fanPin, fanRunning ? HIGH : LOW);
      digitalWrite(ledPin, fanRunning ? HIGH : LOW);
      Serial.printf("[網頁] 💨 風扇手動 %s\n", fanRunning ? "開啟" : "關閉");
      server.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  server.send(400, "application/json", "{\"error\":\"bad request\"}");
}

// POST /api/reset → 重置計時器
void handleApiReset() {
  timerRemaining = TIMER_SECONDS;
  timerRunning   = true;
  lastTimerTick  = millis();
  Serial.println(F("[計時器] 🔄 已透過網頁重置！"));
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/auto → 切換回自動模式
void handleApiAuto() {
  fanManual = false;
  Serial.println(F("[網頁] 🤖 切換為自動模式"));
  server.send(200, "application/json", "{\"ok\":true}");
}

// =============================================
//  WiFi 連線
// =============================================
void connectWiFi() {
  Serial.print(F("\n[WiFi] 連線到: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WiFi] ✅ 連線成功！"));
    Serial.print(F("[WiFi] 👉 請在瀏覽器輸入：http://"));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\n[WiFi] ❌ 連線失敗！請確認 SSID / 密碼"));
  }
}

// =============================================
//  Setup
// =============================================
void setup() {
  Serial.begin(115200);

  // ★ 等待 Serial Monitor 連線（最多等 3 秒）
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(10); }
  delay(500); // 多等一點讓 Serial Monitor 穩定

  pinMode(fanPin,    OUTPUT);
  pinMode(ledPin,    OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);  // 按下 = LOW

  digitalWrite(fanPin, LOW);
  digitalWrite(ledPin, LOW);

  Serial.println(F("\n======================================"));
  Serial.println(F("  手宮智慧冷氣 - 獨立 Web Server 版本"));
  Serial.println(F("======================================"));
  Serial.println(F("[系統] Serial Monitor 已連線 ✅"));

  delay(1000);  // 等待 DHT11 穩定
  connectWiFi();

  // 設定路由
  server.on("/",          HTTP_GET,  handleRoot);
  server.on("/api/data",  HTTP_GET,  handleApiData);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/fan",   HTTP_POST, handleApiFan);
  server.on("/api/auto",  HTTP_POST, handleApiAuto);

  server.begin();
  Serial.println(F("[Server] ✅ Web Server 已啟動（Port 80）"));

  // 啟動後立刻重印 IP
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n========================================"));
    Serial.print(F("  👉 瀏覽器輸入：http://"));
    Serial.println(WiFi.localIP());
    Serial.println(F("========================================\n"));
  }

  // 初始化計時器起始時間
  lastTimerTick = millis();
  lastDHTRead   = millis();
  lastIPPrint   = millis();
}

// =============================================
//  Loop
// =============================================
void loop() {
  // 處理 HTTP 請求
  server.handleClient();

  // ─── 每 10 秒重印 IP（方便查看）────────────
  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastIPPrint >= IP_PRINT_INTERVAL) {
    lastIPPrint = millis();
    Serial.print(F("[WiFi] 📡 IP: http://"));
    Serial.println(WiFi.localIP());
  }
  // ──────────────────────────────────────────────

  // ─── 按鈕偵測（防抖）────────────────────────
  bool reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {  // LOW = 按下
        Serial.println(F("\n[按鈕] 🔘 按下！計時器重置為 200 秒"));
        timerRemaining = TIMER_SECONDS;
        timerRunning   = true;
        lastTimerTick  = millis();
      }
    }
  }
  lastButtonState = reading;
  // ──────────────────────────────────────────────

  // ─── 倒數計時器（每 1 秒 -1）─────────────────
  if (timerRunning && (millis() - lastTimerTick >= 1000)) {
    lastTimerTick += 1000;
    if (timerRemaining > 0) {
      timerRemaining--;
      if (timerRemaining % 10 == 0) {  // 每 10 秒印一次 log
        Serial.printf("[計時器] ⏱️  剩餘 %d 秒\n", timerRemaining);
      }
    } else {
      // 計時器歸零
      if (timerRunning) {
        Serial.println(F("[計時器] ⏰ 時間到！"));
      }
      timerRunning = false;
    }
  }
  // ──────────────────────────────────────────────

  // ─── DHT11 讀取（每 2 秒）────────────────────
  if (millis() - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = millis();

    byte temperature = 0;
    byte humidity    = 0;

    if (dht11.read(&temperature, &humidity, NULL) == SimpleDHTErrSuccess) {
      currentTemp = (int)temperature;
      currentHumi = (int)humidity;
      dhtOk       = true;

      // ─── 自動溫控邏輯（僅在非手動模式下執行）───
      if (!fanManual) {
        bool shouldRun = (currentTemp > 26);
        if (shouldRun != fanRunning) {
          fanRunning = shouldRun;
          digitalWrite(fanPin, fanRunning ? HIGH : LOW);
          digitalWrite(ledPin, fanRunning ? HIGH : LOW);
          Serial.printf("[自動] 🌡️  %d°C → 風扇 %s\n",
            currentTemp, fanRunning ? "🔴 開啟" : "🟢 關閉");
        }
      }
      // ──────────────────────────────────────────

      Serial.printf("[感測] 🌡️  %d°C  💧 %d%%  💨 風扇: %s  模式: %s  ⏱️  %ds\n",
        currentTemp, currentHumi,
        fanRunning ? "ON" : "OFF",
        fanManual  ? "手動" : "自動",
        timerRemaining);
    } else {
      dhtOk = false;
      Serial.println(F("[DHT11] ❌ 讀取失敗"));
    }
  }
  // ──────────────────────────────────────────────
}
