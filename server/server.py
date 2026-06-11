# =============================================
#    手宮智慧冷氣 - 本地控制伺服器
# =============================================
# 功能：
#   1. 接收 ESP32 傳來的溫濕度資料
#   2. 判斷是否需要開啟風扇
#   3. 回傳風扇控制指令給 ESP32
#
# 可擴充：
#   - 接入氣象 API 做溫度預測
#   - 提供 REST API 給手機 APP 連接
#   - 歷史資料查詢
#   - 手動遠端控制風扇
#
# 啟動方式：
#   pip install -r requirements.txt
#   python server.py
#
# API 文件：
#   http://localhost:8080/docs  (啟動後開啟瀏覽器)
# =============================================

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from datetime import datetime
from collections import deque
from typing import Optional
import uvicorn
import time
import threading
import json
import re

# USB Serial 支援（如果沒安裝 pyserial 就略過）
try:
    import serial
    import serial.tools.list_ports
    SERIAL_OK = True
except ImportError:
    SERIAL_OK = False

# =============================================
#  FastAPI 應用程式初始化
# =============================================
app = FastAPI(
    title="手宮智慧冷氣 API",
    description="""
## 寵物箱智慧環境控制系統

### 功能
- 接收 ESP32 溫濕度感測資料
- 自動溫控邏輯（可設定門檻）
- 手動遠端控制風扇
- 歷史資料查詢（供 APP 圖表）

### 未來擴充
- 氣象 API 整合（溫度預測）
- 推播通知
- 多裝置管理
    """,
    version="1.0.0"
)

# 允許 APP 或網頁跨域存取
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# =============================================
#  全域狀態（未來可換成 SQLite 或 Redis）
# =============================================
sensor_history: deque = deque(maxlen=200)  # 最近 200 筆紀錄
latest_reading: dict = {}
fan_state: bool = False

# 溫控設定（可透過 PATCH /api/settings 動態調整）
settings: dict = {
    "temp_on": 26,       # 超過此溫度 → 開啟風扇
    "temp_off": 22,      # 低於此溫度 → 關閉風扇
    "mode": "auto",      # "auto" 自動 | "manual" 手動
    "manual_fan": False, # 手動模式下的風扇狀態
}

# 倒數計時器
TIMER_DURATION = 200  # 秒
timer_start: float = time.time()

def get_remaining_time() -> int:
    """回傳計時器剩餘秒數"""
    elapsed = time.time() - timer_start
    return max(0, int(TIMER_DURATION - elapsed))

def reset_timer():
    """重置計時器為 200 秒"""
    global timer_start
    timer_start = time.time()

# 餅食追蹤
feeding_state: dict = {
    "last_feed_time": None,   # 最後一次餅食的 ISO 時間戳
    "warning_hours": 4,       # 超過幾小時未餅食則發出警告
}

def get_feeding_status() -> dict:
    """計算餵食狀態與警告"""
    if feeding_state["last_feed_time"] is None:
        return {
            "last_feed_time": None,
            "seconds_since_feed": None,
            "warning_active": False,
            "warning_hours": feeding_state["warning_hours"],
            "display": "尚未記錄餅食時間"
        }
    last = datetime.fromisoformat(feeding_state["last_feed_time"])
    elapsed = (datetime.now() - last).total_seconds()
    warning_secs = feeding_state["warning_hours"] * 3600
    hrs = int(elapsed // 3600)
    mins = int((elapsed % 3600) // 60)
    display = f"{hrs} 小時 {mins} 分鐘前" if hrs > 0 else f"{mins} 分鐘前"
    return {
        "last_feed_time": feeding_state["last_feed_time"],
        "seconds_since_feed": int(elapsed),
        "warning_active": elapsed > warning_secs,
        "warning_hours": feeding_state["warning_hours"],
        "display": display
    }

# =============================================
#  資料模型（Pydantic）
# =============================================
class SensorData(BaseModel):
    """ESP32 傳來的感測器資料"""
    device_id: str
    temperature: float
    humidity: float
    fan_state: bool = False

class SettingsUpdate(BaseModel):
    """設定更新（所有欄位可選）"""
    temp_on: Optional[int] = None
    temp_off: Optional[int] = None
    mode: Optional[str] = None
    manual_fan: Optional[bool] = None

# =============================================
#  溫控決策邏輯（核心）
# =============================================
def decide_fan(temp: float, current_fan: bool) -> tuple[bool, str]:
    """
    根據溫度和目前狀態決定風扇開關（遲滯控制）

    設計說明：
    - 使用上下兩個門檻，避免風扇在臨界點頻繁開關
    - 手動模式下忽略溫度，直接採用手動設定
    - 未來可在此加入：氣象 API 預測 / AI 模型 / 時段設定

    Args:
        temp: 目前溫度 (°C)
        current_fan: 目前風扇狀態

    Returns:
        (新風扇狀態, 原因說明)
    """
    if settings["mode"] == "manual":
        return settings["manual_fan"], "手動模式"

    # 自動模式：遲滯控制
    if not current_fan and temp > settings["temp_on"]:
        return True, f"溫度 {temp}°C 超過開啟門檻 {settings['temp_on']}°C"
    elif current_fan and temp < settings["temp_off"]:
        return False, f"溫度 {temp}°C 低於關閉門檻 {settings['temp_off']}°C"
    else:
        status = "開啟中" if current_fan else "關閉中"
        return current_fan, f"溫度正常（{settings['temp_off']}~{settings['temp_on']}°C），風扇維持{status}"

# =============================================
#  API 路由
# =============================================

@app.post("/api/sensor", summary="接收感測器資料（ESP32 呼叫）")
async def receive_sensor(data: SensorData):
    """
    ESP32 每 3 秒 POST 一次感測器資料。
    伺服器回傳風扇控制指令。
    """
    global fan_state

    # 決策
    new_fan_state, reason = decide_fan(data.temperature, fan_state)
    fan_changed = new_fan_state != fan_state
    fan_state = new_fan_state

    # 儲存紀錄
    record = {
        "timestamp": datetime.now().isoformat(),
        "device_id": data.device_id,
        "temperature": data.temperature,
        "humidity": data.humidity,
        "fan": fan_state,
        "reason": reason
    }
    sensor_history.append(record)
    latest_reading.update(record)

    # 終端機 log
    fan_icon = "🔴 ON " if fan_state else "🟢 OFF"
    change_str = " ← 【狀態改變！】" if fan_changed else ""
    print(f"[{record['timestamp'][11:19]}] "
          f"🌡️ {data.temperature:4.1f}°C  "
          f"💧 {data.humidity:4.1f}%  "
          f"風扇: {fan_icon}{change_str}")
    if fan_changed:
        print(f"             └─ {reason}")

    return {
        "fan": fan_state,
        "reason": reason,
        "timestamp": record["timestamp"]
    }


@app.get("/api/status", summary="取得系統狀態（APP 使用）")
async def get_status():
    """取得目前的感測器數値、風扇狀態、計時器、餅食狀態和設定。"""
    remaining = get_remaining_time()
    return {
        "online": True,
        "latest": latest_reading,
        "fan": fan_state,
        "settings": settings,
        "timer": {
            "remaining": remaining,
            "duration": TIMER_DURATION,
            "percentage": remaining / TIMER_DURATION if TIMER_DURATION > 0 else 0
        },
        "feeding": get_feeding_status(),
        "server_time": datetime.now().isoformat()
    }


@app.get("/api/history", summary="取得歷史紀錄（APP 圖表使用）")
async def get_history(limit: int = 50):
    """
    取得最近的感測紀錄。
    limit: 最多回傳筆數（預設 50，最多 200）
    """
    limit = min(limit, 200)
    history = list(sensor_history)[-limit:]
    return {
        "count": len(history),
        "data": history
    }


@app.patch("/api/settings", summary="更新溫控設定")
async def update_settings(update: SettingsUpdate):
    """
    動態調整溫控門檻和模式。
    供 APP 或未來的氣象 API 整合呼叫。
    """
    if update.temp_on is not None:
        if update.temp_on <= settings["temp_off"]:
            raise HTTPException(400, "temp_on 必須大於 temp_off")
        settings["temp_on"] = update.temp_on

    if update.temp_off is not None:
        if update.temp_off >= settings["temp_on"]:
            raise HTTPException(400, "temp_off 必須小於 temp_on")
        settings["temp_off"] = update.temp_off

    if update.mode is not None:
        if update.mode not in ["auto", "manual"]:
            raise HTTPException(400, "mode 只能是 'auto' 或 'manual'")
        settings["mode"] = update.mode

    if update.manual_fan is not None:
        settings["manual_fan"] = update.manual_fan

    print(f"[設定] 已更新：{settings}")
    return {"status": "ok", "settings": settings}


@app.post("/api/fan/on", summary="手動強制開啟風扇")
async def fan_on():
    """切換為手動模式並強制開啟風扇。"""
    global fan_state
    settings["mode"] = "manual"
    settings["manual_fan"] = True
    fan_state = True
    print("[手動] 風扇強制開啟")
    return {"fan": True, "mode": "manual"}


@app.post("/api/fan/off", summary="手動強制關閉風扇")
async def fan_off():
    """切換為手動模式並強制關閉風扇。"""
    global fan_state
    settings["mode"] = "manual"
    settings["manual_fan"] = False
    fan_state = False
    print("[手動] 風扇強制關閉")
    return {"fan": False, "mode": "manual"}


@app.post("/api/fan/auto", summary="切換回自動溫控模式")
async def fan_auto():
    """恢復自動溫控模式。"""
    settings["mode"] = "auto"
    print("[自動] 切換回自動溫控模式")
    return {"mode": "auto", "settings": settings}

# =============================================
#  實體按鈕 - 記錄餵食時間
# =============================================
class ButtonEvent(BaseModel):
    device_id: str

@app.post("/api/button", summary="實體按鈕：記錄餵食時間（ESP32 呼叫）")
async def button_pressed(event: ButtonEvent):
    """按下實體按鈕 → 記錄目前時間為最後餵食時間，並重置計時器。"""
    now_str = datetime.now().isoformat()
    feeding_state["last_feed_time"] = now_str
    reset_timer()

    record = {
        "timestamp": now_str,
        "device_id": event.device_id,
        "event": "feeding",
        "message": "餵食時間已記錄"
    }
    sensor_history.append(record)
    print(f"[{now_str[11:19]}] 🔘 按鈕 [{event.device_id}] → 記錄餵食時間")

    return {
        "status": "ok",
        "message": "餵食時間已記錄，計時器重置",
        "feed_time": now_str
    }


@app.post("/api/feed", summary="手動記錄餵食（網頁按鈕用）")
async def manual_feed():
    """網頁上按「已餵食」時呼叫，功能同實體按鈕。"""
    now_str = datetime.now().isoformat()
    feeding_state["last_feed_time"] = now_str
    reset_timer()
    print(f"[{now_str[11:19]}] 🐾 網頁手動記錄餵食")
    return {"status": "ok", "message": "餵食已記錄", "feed_time": now_str}


@app.patch("/api/feed/warning", summary="調整餵食警告時間")
async def set_feed_warning(hours: float = 4.0):
    """設定幾小時未餵食後發出警告（預設 4 小時）。"""
    feeding_state["warning_hours"] = hours
    return {"status": "ok", "warning_hours": hours}


@app.post("/api/timer/reset", summary="手動重置計時器")
async def timer_reset():
    reset_timer()
    return {"status": "ok", "remaining": TIMER_DURATION}



# =============================================
#  USB Serial 控制 API
# =============================================
serial_paused: bool = False  # 暂停旗標

@app.post("/api/serial/pause", summary="暫停 USB 橋接（燒錄 Arduino IDE 上傳使用）")
async def serial_pause():
    global serial_paused
    serial_paused = True
    print("[Serial] ⏸️ 已暫停 USB 橋接，可安全上傳 ESP32")
    return {"status": "paused", "message": "可安全上傳 ESP32，完成後按 resume"}


@app.post("/api/serial/resume", summary="恢復 USB 橋接")
async def serial_resume():
    global serial_paused
    serial_paused = False
    print("[Serial] ▶️ 已恢復 USB 橋接")
    return {"status": "resumed"}


@app.get("/api/serial/status", summary="查詢 USB 橋接狀態")
async def serial_status():
    return {"paused": serial_paused, "serial_available": SERIAL_OK}


def _find_esp32_port() -> str | None:
    """自動掃描找出 ESP32 的 COM port"""
    keywords = ["CP210", "CH340", "CH341", "FTDI", "USB SERIAL", "USB-SERIAL", "USB UART"]
    for p in serial.tools.list_ports.comports():
        if any(kw in p.description.upper() for kw in keywords):
            return p.device
    # 備援：找任何非藍牙的 COM port
    for p in serial.tools.list_ports.comports():
        if "bluetooth" not in p.description.lower():
            return p.device
    return None


def _serial_bridge():
    """背景執行緒：讀 ESP32 Serial → 更新伺服器狀態"""
    global fan_state

    if not SERIAL_OK:
        print("[Serial] pyserial 未安裝，跳過 USB 橋接（pip install pyserial）")
        return

    # 等待伺服器完全啟動
    time.sleep(2)

    port = _find_esp32_port()
    if not port:
        print("[Serial] 未偵測到 ESP32，USB 橋接待機中...")
        # 持續等待插入
        while True:
            time.sleep(5)
            port = _find_esp32_port()
            if port:
                break

    print(f"[Serial] ✅ 偵測到 ESP32：{port}，開始 USB 橋接")

    while True:
        # 暫停模式：釋放 COM port 讓 Arduino IDE 上傳
        if serial_paused:
            print(f"[Serial] ⏸️ COM port 已釋放，等待恢復...")
            while serial_paused:
                time.sleep(0.5)
            print(f"[Serial] ▶️ 恢復，重新連接 {port}")

        try:
            ser = serial.Serial(port, 115200, timeout=1)
            time.sleep(1.5)

            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                # ── 感測器資料 ──────────────────────
                if line.startswith("SENSOR:"):
                    try:
                        data = json.loads(line[7:])
                    except json.JSONDecodeError:
                        continue

                    temp = float(data.get("temperature", 0))
                    humi = float(data.get("humidity", 0))
                    current_fan = data.get("fan_state", fan_state)

                    new_fan, reason = decide_fan(temp, current_fan)
                    changed = new_fan != fan_state
                    fan_state = new_fan

                    record = {
                        "timestamp": datetime.now().isoformat(),
                        "device_id": data.get("device_id", "esp32_usb"),
                        "temperature": temp,
                        "humidity": humi,
                        "fan": fan_state,
                        "reason": reason
                    }
                    sensor_history.append(record)
                    latest_reading.update(record)

                    fan_icon = "🔴 ON " if fan_state else "🟢 OFF"
                    change_str = " ← 【狀態改變！】" if changed else ""
                    print(f"[{record['timestamp'][11:19]}] "
                          f"🌡️ {temp:4.1f}°C  "
                          f"💧 {humi:4.1f}%  "
                          f"風扇: {fan_icon}{change_str}")
                    if changed:
                        print(f"             └─ {reason}")

                    # 回傳指令給 ESP32
                    ser.write((json.dumps({"fan": fan_state}) + "\n").encode())

                # ── 按鈕事件 ───────────────────────
                elif line.startswith("BUTTON:"):
                    try:
                        data = json.loads(line[7:])
                    except json.JSONDecodeError:
                        data = {"device_id": "esp32_usb"}

                    now_str = datetime.now().isoformat()
                    feeding_state["last_feed_time"] = now_str
                    reset_timer()
                    sensor_history.append({
                        "timestamp": now_str,
                        "device_id": data.get("device_id", "esp32_usb"),
                        "event": "feeding",
                        "message": "餵食時間已記錄（USB 按鈕）"
                    })
                    print(f"[{now_str[11:19]}] 🔘 按鈕（USB）→ 記錄餵食時間")
                    ser.write(b'{"status":"ok"}\n')

                # ── 其他 debug 訊息（對舊韓體嘗試解析） ────────
                else:
                    if not line:
                        continue

                    # 對舊韓體格式尚未更新）嘗試解析感測數據
                    # 老格式： "[感測] 🌡️ 28°C  💧 51%  → ..."
                    m = re.search(r'(\d+(?:\.\d+)?)°C.*?(\d+(?:\.\d+)?)%', line)
                    if m and ('感測' in line or '°C' in line):
                        try:
                            temp = float(m.group(1))
                            humi = float(m.group(2))
                            new_fan, reason = decide_fan(temp, fan_state)
                            changed = new_fan != fan_state
                            fan_state = new_fan

                            record = {
                                "timestamp": datetime.now().isoformat(),
                                "device_id": "esp32_legacy",
                                "temperature": temp,
                                "humidity": humi,
                                "fan": fan_state,
                                "reason": reason
                            }
                            sensor_history.append(record)
                            latest_reading.update(record)

                            fan_icon = "🔴 ON " if fan_state else "🟢 OFF"
                            change_str = " ← 【狀態改變！】" if changed else ""
                            print(f"[{record['timestamp'][11:19]}] "
                                  f"🌡️ {temp:4.1f}°C  "
                                  f"💧 {humi:4.1f}%  "
                                  f"風扇: {fan_icon}{change_str} [舊格式特別解析]")
                            if changed:
                                print(f"             └─ {reason}")
                        except Exception:
                            print(f"[ESP32] {line}")
                    else:
                        # 不含感測資料，過濾掉舊韌體的 HTTP 錯誤訊息
                        NOISE = ['伺服器是否已啟動', 'SERVER_IP', '防火牆', '無法連線', '[HTTP]', 'HTTP→USB']
                        if not any(kw in line for kw in NOISE):
                            print(f"[ESP32] {line}")


        except serial.SerialException:
            print(f"[Serial] ⚠️ ESP32 中斷連線，等待重新插入...")
            time.sleep(3)
            port = _find_esp32_port()
            if not port:
                time.sleep(5)
        except Exception as e:
            print(f"[Serial] 錯誤：{e}")
            time.sleep(2)


if __name__ == "__main__":
    print("=" * 50)
    print("      手宮智慧冷氣 控制伺服器")
    print("=" * 50)
    print(f"  溫控設定：")
    print(f"    超過 {settings['temp_on']}°C → 開啟風扇")
    print(f"    低於 {settings['temp_off']}°C → 關閉風扇")
    print(f"  API 互動文件：http://localhost:8080/docs")
    usb_status = "✅ 已啟用（自動偵測 ESP32）" if SERIAL_OK else "❌ 請安裝 pyserial"
    print(f"  USB 橋接：{usb_status}")
    print(f"  等待 ESP32 連線...")
    print("=" * 50)

    # 啟動 USB Serial 橋接背景執行緒
    t = threading.Thread(target=_serial_bridge, daemon=True)
    t.start()

    uvicorn.run(app, host="0.0.0.0", port=8080, log_level="warning")

