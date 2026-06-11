# 守宮智慧冷氣 🦎❄️

> **智慧物聯網應用與實作 期末報告**
>
> 組員：楊珽鈞、王勤硯

---

## 專案簡介

白天陽光直射，房間溫度可以飆得很高，對守宮這種需要穩定溫度環境的爬蟲類寵物來說十分不友善。

本專案開發了一套**守宮智慧冷氣系統**，讓守宮在飼主不在家的白天也能住得舒適。系統以 ESP32 為核心，透過 DHT11 感測器即時偵測寵物箱內溫濕度，並由本地伺服器依據設定門檻自動控制風扇；使用者同時可透過網頁介面遠端監控與手動操控。

---

## 系統架構

```
┌──────────────┐        HTTP / JSON        ┌─────────────────┐       Serial / WiFi      ┌────────────┐
│  使用者網頁   │ ◄────────────────────────► │   本地伺服器     │ ◄───────────────────────► │   ESP32    │
│  (瀏覽器)    │                            │  (FastAPI)      │                           │  硬體控制  │
└──────────────┘                            └─────────────────┘                           └────────────┘
                                                                                                │
                                                                                          ┌─────┴──────┐
                                                                                          │  DHT11 溫濕 │
                                                                                          │  度感測器   │
                                                                                          │  12V 風扇   │
                                                                                          │  實體按鈕   │
                                                                                          └────────────┘
```

- **ESP32**：每 3 秒讀取 DHT11 感測資料，透過 WiFi 或 USB Serial 回報給伺服器，並依據伺服器指令控制風扇 ON/OFF
- **本地伺服器（FastAPI）**：接收感測資料、執行溫控決策（遲滯控制）、提供 REST API 給網頁
- **使用者網頁**：即時顯示溫濕度圖表、風扇狀態，支援手動遠端控制與餵食紀錄

---

## 硬體元件

| 元件 | 型號 / 規格 | 用途 |
|---|---|---|
| 微控制器 | ESP32 | 感測資料讀取、WiFi 通訊、風扇控制 |
| 溫濕度感測器 | DHT11 | 偵測寵物箱內溫度與濕度 |
| 風扇 | 12V DC 風扇 | 降溫散熱 |
| MOSFET | IRLZ44N | 讓 ESP32 (3.3V GPIO) 控制 12V 風扇電源 |
| 實體按鈕 | 通用按鈕 | 記錄上次餵食時間 |
| 保護電阻 | 470Ω（Gate 串聯） | 保護 MOSFET 閘極 |
| 飛輪二極體 | 1N4007 | 風扇斷電時保護電路不受反向電動勢損壞 |

> 詳細電路連接請參閱電路圖。

### 腳位定義（ESP32）

| 功能 | GPIO 腳位 |
|---|---|
| DHT11 DATA | GPIO 4 |
| 風扇控制（MOSFET Gate） | GPIO 18 |
| 板載 LED（狀態指示） | GPIO 2 |
| 實體按鈕 | GPIO 19 |

---

## 軟體功能

### 溫控邏輯（遲滯控制）

為避免風扇在臨界溫度頻繁切換，採用上下雙門檻設計：

- 溫度 **> 26°C** → 開啟風扇
- 溫度 **< 22°C** → 關閉風扇
- 介於 22–26°C → 維持目前狀態不變

門檻可透過 API 或網頁動態調整，無需重新燒錄韌體。

### 網頁功能

- 📊 即時溫濕度折線圖（每 3 秒更新）
- 🌡️ 目前溫度 / 濕度顯示
- 💨 風扇狀態顯示（自動 / 手動模式）
- 🎛️ 手動強制開啟 / 關閉 / 恢復自動控制
- 🍽️ 餵食紀錄（顯示距上次餵食時間）
- ⚙️ 溫控門檻設定

### 餵食提醒

按下實體按鈕 → 記錄餵食時間並重置倒數計時器（預設 200 秒警示）；超過設定時數未餵食（預設 4 小時）則網頁顯示警告。

---

## 專案結構

```
aiot_final_project/
│
├── smart_fan_wifi/              # ESP32 主韌體（WiFi 版本）
│   └── smart_fan_wifi.ino      #   每 3 秒送 DHT11 資料給伺服器
│
├── smart_fan_standalone/        # ESP32 獨立版（無需伺服器，自帶邏輯）
│   └── smart_fan_standalone.ino
│
├── smart_fan/                   # ESP32 基礎版（早期開發版）
│   └── smart_fan.ino
│
├── server/                      # 本地控制伺服器
│   ├── server.py               #   FastAPI 主程式（API + 決策邏輯）
│   ├── serial_bridge.py        #   USB Serial 橋接器（ESP32 ↔ server）
│   ├── streamlit_app.py        #   Streamlit 監控儀表板（備選 UI）
│   └── requirements.txt        #   Python 套件需求
│
├── dht11_test/                  # DHT11 感測器單元測試
│   └── dht11_test.ino
│
└── fan_test/                    # 風扇控制單元測試
    └── fan_test.ino
```

---

## 快速開始

### 環境需求

- Python 3.10+
- Arduino IDE（含 ESP32 開發板支援）
- Arduino 函式庫：`SimpleDHT`、`ArduinoJson (v6.x)`

### 1. 啟動伺服器

```bash
cd server
pip install -r requirements.txt
python server.py
```

伺服器啟動後，API 文件可於 http://localhost:8080/docs 查看。

### 2. 燒錄 ESP32 韌體

開啟 `smart_fan_wifi/smart_fan_wifi.ino`，修改以下設定後燒錄：

```cpp
const char* WIFI_SSID   = "你的WiFi名稱";
const char* WIFI_PASS   = "你的WiFi密碼";
const char* SERVER_IP   = "電腦的區網IP";  // 例如：192.168.1.100
```

### 3. 連線方式選擇

| 方式 | 說明 |
|---|---|
| **WiFi 模式**（推薦） | 燒錄 `smart_fan_wifi.ino`，ESP32 直接透過 WiFi 與伺服器溝通 |
| **USB Serial 模式** | 燒錄任意韌體，另開終端機執行 `python serial_bridge.py` |

### 4. 開啟網頁介面

瀏覽器開啟 http://localhost:8080/docs 或啟動 Streamlit 儀表板：

```bash
cd server
streamlit run streamlit_app.py
```

---

## API 端點摘要

| 方法 | 路徑 | 說明 |
|---|---|---|
| POST | `/api/sensor` | ESP32 上報感測資料，取得風扇指令 |
| GET | `/api/status` | 取得系統目前狀態（網頁輪詢用） |
| GET | `/api/history` | 取得歷史感測紀錄（最多 200 筆） |
| PATCH | `/api/settings` | 動態調整溫控門檻與模式 |
| POST | `/api/fan/on` | 手動強制開啟風扇 |
| POST | `/api/fan/off` | 手動強制關閉風扇 |
| POST | `/api/fan/auto` | 恢復自動溫控模式 |
| POST | `/api/button` | 實體按鈕事件（記錄餵食時間） |
| POST | `/api/feed` | 網頁手動記錄餵食 |

---

## 注意事項

- `smart_fan_wifi.ino` 內含 WiFi 密碼，**請勿將真實密碼 push 至公開 repo**，可於燒錄前填入或使用 `.gitignore` 排除設定檔
- 上傳 ESP32 韌體前，請確認 Arduino IDE 的 Serial Monitor 已關閉（避免 COM Port 衝突）
- 若使用 USB Serial 模式，需先啟動 `server.py` 再執行 `serial_bridge.py`

---

*智慧物聯網應用與實作 期末報告 — 守宮智慧冷氣*
*楊珽鈞 · 王勤硯*
