# AI Usage Dashboard for Waveshare ESP32-C6 e-Paper 1.54

可直接用 Arduino IDE 燒錄的第一版 AI Usage Dashboard，目標硬體為
Waveshare ESP32-C6-ePaper-1.54（SKU 34393，200×200 黑白電子紙）。

目前 Claude、Codex、Gemini 的 usage 與 reset time 都是明確標示的 mock
data；專案已預留 `UsageCollector` 介面，後續可改接自建的 Usage Collector
API。沒有假設 Claude、Codex 或 Gemini 存在可直接從 ESP32 呼叫的官方 usage
API。

## 已完成

- 相容 `Arduino IDE 2.x`、`arduino-esp32 3.3.11`、`ESP32C6 Dev Module`
- 產品專用 GPIO、TCA9554 e-paper power 與 battery power-hold 控制
- 200×200 1-bit framebuffer、內建 5×7 font 與 dashboard renderer
- Claude / Codex / Gemini mock usage、reset time
- Wi-Fi、電池百分比、更新時間與 mock 標記
- 預設每 15 分鐘 full refresh，刷新後讓 e-paper 休眠並關閉顯示電源
- 不需 LVGL、GxEPD2、Adafruit GFX 或額外 Arduino Library
- 本機已用 arduino-esp32 `3.3.11` compile 驗證
- GitHub Actions 會在 push / pull request 重跑相同 target 的 compile

## 畫面內容

```text
┌────────────────────────┐
│ AI USAGE       WiFi Bat│
├────────────────────────┤
│ CLAUDE             72% │
│ [#############------]  │
│ RESET 3H 42M           │
├────────────────────────┤
│ CODEX              51% │
│ [##########---------]  │
│ RESET 1H 18M           │
├────────────────────────┤
│ GEMINI             86% │
│ [################---]  │
│ RESET 4H 07M           │
├────────────────────────┤
│ BAT 86% WIFI DEMO      │
│ UPDATED 22:48      MOCK│
└────────────────────────┘
```

## Repo 結構

```text
ai-usage-dashboard/
├─ .github/workflows/compile.yml # compile CI pinned to arduino-esp32 3.3.11
├─ CLAUDE_HANDOFF.md             # Claude 技術交接與接手順序
├─ CLAUDE_PROMPT.md              # 可直接貼給 Claude 的 prompt
├─ AI_Usage_Dashboard/
│  ├─ AI_Usage_Dashboard.ino     # setup / refresh loop / dependency wiring
│  ├─ app_config.h               # safe defaults
│  ├─ config.local.h.example     # optional private Wi-Fi settings
│  ├─ board_config.h             # product-specific pins
│  ├─ board_power.*              # TCA9554 power rails
│  ├─ epaper_display.*           # 200×200 display driver
│  ├─ canvas_1bit.*              # framebuffer drawing primitives and font
│  ├─ dashboard_model.h          # normalized data model
│  ├─ dashboard_renderer.*       # screen layout
│  ├─ usage_collector.h          # future collector interface
│  ├─ mock_usage_collector.*     # version 0.1 data source
│  └─ device_status.*            # optional real Wi-Fi, battery, and NTP time
├─ docs/
│  ├─ API_CONTRACT.md
│  └─ HARDWARE_NOTES.md
├─ tools/compile.ps1
├─ THIRD_PARTY_NOTICES.md
└─ README.md
```

## 目前狀態（重要）

舊版 Dashboard 的 Phase A 實機驗證**未通過**。第一次 power sequence 後，
TCA9554（I2C `0x20`）所在的 bus persistent stuck；拔 USB 無效。實際拔開內建鋰電池
接頭後，Waveshare 原廠 firmware 已成功刷新電子紙，PWR `OFF -> ON` 亦成功，證明
硬體目前可工作。確切觸發點仍未驗證，**不要重新燒錄舊版 Dashboard**。完整證據與
安全的下一步請見
[`CODEX_HANDOFF.md`](CODEX_HANDOFF.md)。

診斷 sketch 放在 [`tools/diagnostics/`](tools/diagnostics)，
其中 `i2c_probe_ro` 為唯讀版本，不會寫入任何暫存器或開啟電源軌。

## 交接給 Claude

如果下一階段改由 Claude 執行，先讓它閱讀最新的
[`CODEX_HANDOFF.md`](CODEX_HANDOFF.md) 與
[`CLAUDE_HANDOFF.md`](CLAUDE_HANDOFF.md)，並把
[`CLAUDE_PROMPT.md`](CLAUDE_PROMPT.md) 的內容作為第一則 prompt。交接順序已固定為：

1. Audit 現有 `board_power.cpp` 與 Waveshare 官方 TCA9554 / power sequence 差異。
2. 建立並 compile 最小 one-shot smoke firmware；先停在 Upload 前。
3. 經使用者確認後 Upload，依照片與 Serial log 驗證。
4. smoke 通過後才恢復完整 mock Dashboard。
5. 完整 mock 通過後才處理 real device status 與 Usage Collector。

## 第一次燒錄

### 1. 開啟 sketch

用 Arduino IDE 開啟：

```text
AI_Usage_Dashboard/AI_Usage_Dashboard.ino
```

不要只複製 `.ino`；同資料夾內的 `.h/.cpp` 都是必要檔案。

### 2. 確認開發板套件

- Boards Manager：`esp32 by Espressif Systems`
- Version：`3.3.11`
- Board：`ESP32C6 Dev Module`
- Port：選擇板子目前的 COM port

### 3. 設定 Tools 選項

| 選項 | 值 |
|---|---|
| USB CDC On Boot | Enabled |
| CPU Frequency | 160MHz (WiFi) |
| Core Debug Level | None |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Upload Speed | 921600 |
| Zigbee Mode | Disabled |

這組設定與 Waveshare 官方 repo 的 Tools Configuration 一致。

### 4. Verify 與 Upload

1. 先按 `Verify`。
2. Verify 成功後按 `Upload`。
3. 若卡在連線，按住板上的 `BOOT`，開始 Upload，看到連線後放開。
4. 開啟 Serial Monitor，baud rate 選 `115200`。

成功時會看到：

```text
AI Usage Dashboard booting
Dashboard refreshed
```

電子紙 full refresh 會閃動數次，約數秒後停在 dashboard，屬正常現象。

> 燒錄會覆蓋出廠 demo。需要還原時，使用 Waveshare 官方 repo 的
> `03_Firmware`；本專案不會自動備份或還原原廠 firmware。

## 預設行為

- usage、reset、Wi-Fi、電池與更新時間都是 mock data。
- 第一次開機不需要 Wi-Fi，也不會把任何 credential 寫入 firmware。
- Dashboard 立即刷新一次，之後每 15 分鐘刷新。
- e-paper 顯示完成後進入 deep sleep，畫面仍會保留。

要改 mock 值，編輯：

```text
AI_Usage_Dashboard/mock_usage_collector.cpp
```

## 啟用真實裝置狀態

這一步只會把 Wi-Fi、battery ADC 與 updated time 改成真實值；三家 AI usage
仍維持 mock。

1. 複製 `config.local.h.example` 為 `config.local.h`。
2. 填入 Wi-Fi SSID 與 password。
3. 確認以下設定：

```cpp
#define AI_DASH_USE_MOCK_DEVICE_STATUS 0
#define AI_DASH_ENABLE_WIFI 1
```

`config.local.h` 已被 `.gitignore` 排除，不要把 credential commit 到 GitHub。

電池百分比沿用 Waveshare 範例的 3.00V=0%、4.12V=100% 線性估算；它不是精密
fuel gauge，接近滿電與低電量時誤差會較大。

## 串接真實 Usage Collector

不要把三家服務的 account token 放進 ESP32。建議架構：

```text
Claude / Codex / Gemini
          ↓
server-side Usage Collector
          ↓ normalized HTTPS JSON
ESP32-C6 HttpUsageCollector
          ↓
DashboardData → e-paper
```

實作邊界、JSON schema、錯誤與 security 規則見
[`docs/API_CONTRACT.md`](docs/API_CONTRACT.md)。

## Command-line compile

如果已安裝 Arduino IDE 2.x，可在 PowerShell 執行：

```powershell
.\tools\compile.ps1
```

Script 使用與上方相同的 FQBN 設定，輸出放在 `build/`。

## 已驗證與未驗證

已驗證：

- arduino-esp32 `3.3.11` compile exit code `0`
- Mock-first build：322,726 bytes，約 10% program storage
- Global variables：22,020 bytes，約 6% dynamic memory
- 只解析到 Arduino core 內建 `Wire 3.3.11` 與 `SPI 3.3.11`
- Optional Wi-Fi / battery / NTP build 亦通過 compile：1,044,372 bytes、
  50,140 bytes global variables
- 舊版 Dashboard 與原廠 firmware 都曾在 persistent stuck 狀態重現 TCA9554 failure
- 唯讀探針確認 SDA GPIO18、SCL GPIO8 從開機早期即 externally held low
- 真正拔開內建電池接頭後，原廠 firmware 已成功刷新電子紙
- 原廠 firmware 的 PWR `OFF -> ON` 實機流程成功

尚未驗證：

- 舊版 Dashboard 觸發 persistent stuck 的確切 root cause
- 本專案的安全修正版尚未 Upload，且從未成功刷新電子紙
- 本專案畫面方向、panel batch 差異、BUSY polarity 與長時間 refresh 穩定性
- 真實 Usage Collector/API 尚未實作

## Hardware source

腳位、power rail、display controller sequence 與 waveform 以
[Waveshare 官方 ESP32-C6-ePaper-1.54 repo](https://github.com/waveshareteam/ESP32-C6-ePaper-1.54)
commit `c4c47b6a8001f9daa50b38912393c158371e03be` 為基準。詳細對照與授權邊界見
[`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md) 與
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
