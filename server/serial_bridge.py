# =============================================
#    手宮智慧冷氣 - USB Serial 橋接器
# =============================================
# 功能：讀取 ESP32 透過 USB 傳來的感測資料
#       轉發到本地 FastAPI 伺服器
#       再把風扇指令回傳給 ESP32
#
# 啟動方式（另開一個終端機）：
#   python serial_bridge.py
#
# 注意：server.py 必須已在背景執行
# =============================================

import serial
import serial.tools.list_ports
import requests
import json
import time
import sys

SERVER = "http://localhost:8080"
BAUD   = 115200

# =============================================
#  自動找到 ESP32 的 COM Port
# =============================================
def find_esp32_port():
    """自動掃描並找出 ESP32 的 COM port"""
    ports = list(serial.tools.list_ports.comports())

    # ESP32 常見 USB-Serial 晶片關鍵字
    KEYWORDS = ["CP210", "CH340", "CH341", "FTDI", "USB SERIAL", "USB-SERIAL", "USB UART"]

    # 優先自動匹配
    for p in ports:
        desc = p.description.upper()
        if any(kw in desc for kw in KEYWORDS):
            print(f"[自動偵測] 找到 ESP32：{p.device}（{p.description}）")
            return p.device

    # 找不到就讓使用者選
    if not ports:
        print("❌ 找不到任何 COM port！請確認 USB 已連接")
        return None

    print("\n找到以下 COM port，請選擇 ESP32 的編號：")
    for i, p in enumerate(ports):
        print(f"  {i}: {p.device}  —  {p.description}")
    try:
        idx = int(input("輸入編號: ").strip())
        return ports[idx].device
    except (ValueError, IndexError):
        print("❌ 無效選擇")
        return None


# =============================================
#  主橋接邏輯
# =============================================
def main():
    print("=" * 50)
    print("   手宮智慧冷氣 - USB Serial 橋接器")
    print("=" * 50)

    # 檢查伺服器是否運行
    try:
        requests.get(f"{SERVER}/api/status", timeout=2)
        print(f"[伺服器] ✅ 已連線到 {SERVER}")
    except Exception:
        print(f"[伺服器] ❌ 無法連線到 {SERVER}")
        print("         請先執行：python server.py")
        sys.exit(1)

    # 找 COM port
    port = find_esp32_port()
    if not port:
        sys.exit(1)

    # 開啟 Serial
    try:
        ser = serial.Serial(port, BAUD, timeout=1)
        print(f"[Serial] ✅ 已開啟 {port}（{BAUD} baud）")
        print(f"[Serial] 等待 ESP32 傳資料...\n{'=' * 50}")
        time.sleep(2)  # 等待 ESP32 重啟完成
    except serial.SerialException as e:
        print(f"[Serial] ❌ 無法開啟 {port}：{e}")
        print("         請確認 Arduino IDE 的 Serial Monitor 已關閉")
        sys.exit(1)

    # =============================================
    #  主迴圈：讀取 ESP32 → 轉發到 FastAPI
    # =============================================
    while True:
        try:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            # ── 感測器資料 ────────────────────────
            if line.startswith("SENSOR:"):
                payload_str = line[7:]
                try:
                    data = json.loads(payload_str)
                except json.JSONDecodeError:
                    print(f"[橋接] ⚠️ 無法解析 JSON：{payload_str}")
                    continue

                temp = data.get("temperature", "--")
                humi = data.get("humidity", "--")
                print(f"[感測] 🌡️ {temp}°C  💧 {humi}%  → 轉發到伺服器...")

                try:
                    resp = requests.post(f"{SERVER}/api/sensor", json=data, timeout=3)
                    cmd = resp.json()
                    fan_state = cmd.get("fan", False)
                    reason    = cmd.get("reason", "")
                    fan_icon  = "🔴 ON " if fan_state else "🟢 OFF"
                    print(f"       風扇: {fan_icon}  ← {reason}")

                    # 回傳風扇指令給 ESP32
                    ser.write((json.dumps({"fan": fan_state}) + "\n").encode())

                except Exception as e:
                    print(f"[橋接] ❌ 轉發失敗：{e}")
                    ser.write(b'{"fan":false}\n')

            # ── 按鈕事件 ──────────────────────────
            elif line.startswith("BUTTON:"):
                payload_str = line[7:]
                try:
                    data = json.loads(payload_str)
                except json.JSONDecodeError:
                    data = {"device_id": "esp32_pet_box_01"}

                print("[按鈕] 🔘 收到按鈕事件，記錄餵食時間...")

                try:
                    requests.post(f"{SERVER}/api/button", json=data, timeout=3)
                    print("[按鈕] ✅ 記錄成功！計時器重置")
                    ser.write(b'{"status":"ok"}\n')
                except Exception as e:
                    print(f"[按鈕] ❌ 記錄失敗：{e}")
                    ser.write(b'{"status":"error"}\n')

            # ── ESP32 其他 Debug 訊息（直接印出）──
            else:
                print(f"[ESP32] {line}")

        except KeyboardInterrupt:
            print("\n\n[橋接] 已停止")
            ser.close()
            break
        except Exception as e:
            print(f"[橋接] 錯誤：{e}")
            time.sleep(1)


if __name__ == "__main__":
    main()
