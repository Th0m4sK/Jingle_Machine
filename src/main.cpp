#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_system.h>

#include "pin_config.h"
#include "audio_player.h"
#include "button_manager.h"
#include "config_manager.h"
#include "web_server.h"

// Hardware objects
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(HSPI);  // VSPI is used by TFT+SD, HSPI for touch
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// Application objects
AudioPlayer audioPlayer;
ConfigManager configMgr;
ButtonManager btnMgr(&tft, &touch);

// Settings server (only allocated in settings mode)
SettingsServer* settingsServer = nullptr;

// State machine
enum AppState {
    STATE_BT_FAILED,       // Show "Scan BT" / "Open Settings" buttons
    STATE_BT_SCANNING,     // BT scan in progress (non-blocking)
    STATE_BT_SELECT,       // Show scan results as touch buttons
    STATE_NORMAL,          // Jingle buttons active
    STATE_SETTINGS,        // WiFi AP + Web UI
    STATE_QUICK_SETTINGS   // Brightness + Touch threshold overlay
};
AppState currentState;

// BT scan results + pagination (extern used by web_server.cpp)
std::vector<AudioPlayer::BTDevice> globalBTScanResults;
int btSelectPage = 0;
const int DEVICES_PER_PAGE = 4;

// Touch debounce
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 300;

// Configurable at runtime (loaded from config)
int touchPressureThreshold = 200;
uint8_t displayBrightness = 200;

bool sdCardAvailable = false;

// ─────────────────────────────────────────────────────
//  Brightness (PWM on TFT backlight)
// ─────────────────────────────────────────────────────
#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ    5000
#define BL_PWM_BITS    8

void applyBrightness(uint8_t value) {
    displayBrightness = value;
    ledcWrite(BL_PWM_CHANNEL, value);
}

// ─────────────────────────────────────────────────────
//  RGB LED (common anode, active-low; plain digitalWrite)
//  Each channel is either fully on or off — no PWM needed
//  for status colors.
// ─────────────────────────────────────────────────────
void setupLED() {
    pinMode(LED_ANODE_PIN, OUTPUT);
    digitalWrite(LED_ANODE_PIN, HIGH);  // common anode: always HIGH
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    digitalWrite(LED_R_PIN, HIGH);  // cathodes HIGH = off
    digitalWrite(LED_G_PIN, HIGH);
    digitalWrite(LED_B_PIN, HIGH);
}

// r/g/b: 0-255; threshold 128 → on/off per channel (active-low)
void setLED(uint8_t r, uint8_t g, uint8_t b) {
    digitalWrite(LED_R_PIN, r >= 128 ? LOW : HIGH);
    digitalWrite(LED_G_PIN, g >= 128 ? LOW : HIGH);
    digitalWrite(LED_B_PIN, b >= 128 ? LOW : HIGH);
}

// Parse "#RRGGBB" and set the LED
void setLEDHex(const String& hex) {
    String h = hex.startsWith("#") ? hex.substring(1) : hex;
    if (h.length() < 6) { setLED(0, 0, 0); return; }
    uint8_t r = (uint8_t)strtol(h.substring(0, 2).c_str(), nullptr, 16);
    uint8_t g = (uint8_t)strtol(h.substring(2, 4).c_str(), nullptr, 16);
    uint8_t b = (uint8_t)strtol(h.substring(4, 6).c_str(), nullptr, 16);
    setLED(r, g, b);
}

// ─────────────────────────────────────────────────────
//  Touch helper – reads & maps coordinates, debounced
// ─────────────────────────────────────────────────────
bool touchDebounced(int& x, int& y) {
    if (millis() - lastTouchTime < TOUCH_DEBOUNCE) return false;
    if (!touch.touched()) return false;   // tirqTouched() not used – caused false negatives
    TS_Point p = touch.getPoint();
    if (p.z < touchPressureThreshold) return false;
    x = map(p.x, 433, 3527, 0, SCREEN_WIDTH);
    y = map(p.y, 566, 3554, 0, SCREEN_HEIGHT);
    x = constrain(x, 0, SCREEN_WIDTH - 1);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);
    lastTouchTime = millis();
    return true;
}

// ─────────────────────────────────────────────────────
//  Screen drawing
// ─────────────────────────────────────────────────────
void drawBTFailedScreen(const char* title = "No BT Connection!") {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED);
    tft.drawString(title, SCREEN_WIDTH / 2, 35, 4);

    // Scan BT button (blue)  y: 75..140
    tft.fillRoundRect(20, 75, SCREEN_WIDTH - 40, 65, 8, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Scan BT Devices", SCREEN_WIDTH / 2, 107, 2);

    // Settings button (orange)  y: 155..220
    tft.fillRoundRect(20, 155, SCREEN_WIDTH - 40, 65, 8, TFT_ORANGE);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("Open Settings", SCREEN_WIDTH / 2, 187, 2);
}

void drawBTSelectScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);

    int total = (int)globalBTScanResults.size();
    int pages = max(1, (total + DEVICES_PER_PAGE - 1) / DEVICES_PER_PAGE);

    tft.setTextColor(TFT_CYAN);
    String header = "Found " + String(total) + " device" + (total != 1 ? "s" : "");
    if (pages > 1) header += "   pg " + String(btSelectPage + 1) + "/" + String(pages);
    tft.drawString(header, 8, 4, 2);

    int start = btSelectPage * DEVICES_PER_PAGE;
    int end = min(start + DEVICES_PER_PAGE, total);

    for (int i = start; i < end; i++) {
        int row = i - start;
        int btnY = 30 + row * 45;   // rows at y: 30, 75, 120, 165
        tft.fillRoundRect(5, btnY, SCREEN_WIDTH - 10, 40, 5, 0x2945);
        tft.setTextColor(TFT_WHITE);
        String name = globalBTScanResults[i].name.length() > 0
            ? globalBTScanResults[i].name : globalBTScanResults[i].mac;
        if (name.length() > 22) name = name.substring(0, 22);
        tft.drawString(name + " (" + String(globalBTScanResults[i].rssi) + "dB)", 14, btnY + 12, 2);
    }

    // Pagination buttons
    if (pages > 1) {
        tft.setTextDatum(MC_DATUM);
        if (btSelectPage > 0) {
            tft.fillRoundRect(5, 215, 105, 22, 4, TFT_NAVY);
            tft.setTextColor(TFT_WHITE);
            tft.drawString("< Prev", 57, 226, 2);
        }
        if (btSelectPage < pages - 1) {
            tft.fillRoundRect(210, 215, 105, 22, 4, TFT_NAVY);
            tft.setTextColor(TFT_WHITE);
            tft.drawString("Next >", 262, 226, 2);
        }
        tft.setTextDatum(TL_DATUM);
    }
}

// ─────────────────────────────────────────────────────
//  State transitions
// ─────────────────────────────────────────────────────

// Wait for BT connection – no timeout, waits forever.
// Buttons "Scan BT" and "Open Settings" are always visible so the user
// can choose at any time.
// Returns:  1 = connected,  -1 = scan requested,  -2 = settings requested
int tryBTConnect() {
    String btDeviceName = configMgr.getBTDeviceName();
    String btDeviceMac  = configMgr.getBTDeviceMac();

    static char btName[32];
    static char btMac[20];
    strncpy(btName, btDeviceName.c_str(), 31); btName[31] = '\0';
    strncpy(btMac,  btDeviceMac.c_str(),  19); btMac[19]  = '\0';

    // ── Draw waiting screen with buttons ──────────────────────────────
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_ORANGE);
    tft.drawString("Waiting for BT...", SCREEN_WIDTH / 2, 12, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(btDeviceName, SCREEN_WIDTH / 2, 32, 2);
    if (btDeviceMac.length() > 0) {
        tft.setTextColor(TFT_CYAN);
        tft.drawString(btDeviceMac, SCREEN_WIDTH / 2, 52, 1);
    }

    // "Scan BT Devices" button  y: 68..118
    tft.fillRoundRect(20, 68, SCREEN_WIDTH - 40, 50, 8, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Scan BT Devices", SCREEN_WIDTH / 2, 93, 2);

    // "Open Settings" button  y: 128..178
    tft.fillRoundRect(20, 128, SCREEN_WIDTH - 40, 50, 8, TFT_ORANGE);
    tft.setTextColor(TFT_BLACK);
    tft.drawString("Open Settings", SCREEN_WIDTH / 2, 153, 2);
    // ──────────────────────────────────────────────────────────────────

    btnMgr.loadConfig(configMgr.getConfig());
    audioPlayer.begin(btName, btDeviceMac.length() > 0 ? btMac : nullptr, false);
    audioPlayer.setVolume(configMgr.getBTVolume());

    static unsigned long lastDotUpdate = 0;
    static int dotCount = 0;

    while (true) {
        if (audioPlayer.isConnected()) return 1;

        // Animate dots to show it's still working
        if (millis() - lastDotUpdate > 600) {
            lastDotUpdate = millis();
            dotCount = (dotCount + 1) % 4;
            String dots = "";
            for (int i = 0; i < dotCount; i++) dots += ".";
            tft.fillRect(0, 200, SCREEN_WIDTH, 38, TFT_BLACK);
            tft.setTextColor(TFT_YELLOW);
            tft.drawString(dots, SCREEN_WIDTH / 2, 215, 4);
        }

        // Check touch – buttons are always on screen
        if (touch.touched()) {
            TS_Point p = touch.getPoint();
            if (p.z >= 200) {
                int tx = map(p.x, 433, 3527, 0, SCREEN_WIDTH);
                int ty = map(p.y, 566, 3554, 0, SCREEN_HEIGHT);
                ty = constrain(ty, 0, SCREEN_HEIGHT - 1);
                if (ty >= 68  && ty <= 118) { delay(100); return -1; }  // Scan
                if (ty >= 128 && ty <= 178) { delay(100); return -2; }  // Settings
            }
        }

        delay(50);
    }
}

// Save selected device and restart
void selectBTDevice(int idx) {
    String devName = (globalBTScanResults[idx].name.length() > 0 &&
                      globalBTScanResults[idx].name != "Unknown")
        ? globalBTScanResults[idx].name : globalBTScanResults[idx].mac;
    String devMac = globalBTScanResults[idx].mac;

    JsonDocument newConfig;
    newConfig.set(configMgr.getConfig());
    newConfig["btDevice"]    = devName;
    newConfig["btDeviceMac"] = devMac;
    configMgr.saveConfig(newConfig);

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Saved!", SCREEN_WIDTH / 2, 80, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(devName, SCREEN_WIDTH / 2, 130, 2);
    tft.setTextColor(TFT_CYAN);
    tft.drawString(devMac, SCREEN_WIDTH / 2, 155, 2);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Restarting...", SCREEN_WIDTH / 2, 185, 2);
    delay(2000);
    ESP.restart();
}

// Draw the live scan screen header + stop button
void drawScanScreen(int deviceCount) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Scanning BT...", 5, 4, 2);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("(tap device to connect)", 5, 22, 1);

    // device count top-right
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString(String(deviceCount) + " found", SCREEN_WIDTH - 5, 4, 2);

    // Stop button  y: 210..238
    tft.fillRoundRect(20, 210, SCREEN_WIDTH - 40, 28, 6, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Stop Scan", SCREEN_WIDTH / 2, 224, 2);
}

// Redraw the live device button list (up to 4 rows)
void redrawScanDevices(const std::vector<AudioPlayer::BTDevice>& devs) {
    tft.fillRect(0, 38, SCREEN_WIDTH, 168, TFT_BLACK);
    int show = min((int)devs.size(), 4);
    for (int i = 0; i < show; i++) {
        int btnY = 40 + i * 42;
        tft.fillRoundRect(5, btnY, SCREEN_WIDTH - 10, 38, 5, 0x2945);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_WHITE);
        String name = (devs[i].name.length() > 0 && devs[i].name != "Unknown")
            ? devs[i].name : devs[i].mac;
        if (name.length() > 20) name = name.substring(0, 20);
        tft.drawString(name + " (" + String(devs[i].rssi) + "dB)", 12, btnY + 11, 2);
    }
    // update count
    tft.fillRect(SCREEN_WIDTH / 2, 0, SCREEN_WIDTH / 2, 20, TFT_BLACK);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString(String(devs.size()) + " found", SCREEN_WIDTH - 5, 4, 2);
}

// Start BT scan – non-blocking, UI handled in handleBTScanning()
void runBTScan() {
    currentState = STATE_BT_SCANNING;
    drawScanScreen(0);
    if (!audioPlayer.startScan()) {
        drawBTFailedScreen("Scan failed!");
        currentState = STATE_BT_FAILED;
    }
}

// Triggered by touch: set NVS flag and reboot into settings mode
void enterSettings() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Going to Settings...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 2);
    delay(300);
    configMgr.enterSettingsMode();  // sets NVS flag + ESP.restart()
}

// Called at boot when settings_mode NVS flag was set
void bootSettingsMode() {
    currentState = STATE_SETTINGS;
    setLED(255, 180, 0);  // yellow = settings mode

    WiFi.mode(WIFI_AP);
    WiFi.softAP("jinglebox", "jingle1234");
    delay(1000);

    tft.fillScreen(TFT_BLUE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("SETTINGS MODE", SCREEN_WIDTH / 2, 45, 4);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("WiFi: jinglebox", SCREEN_WIDTH / 2, 90, 2);
    tft.drawString("Password: jingle1234", SCREEN_WIDTH / 2, 110, 2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("http://192.168.4.1", SCREEN_WIDTH / 2, 150, 4);

    // Leave button  y: 178..223
    tft.fillRoundRect(110, 178, 100, 45, 8, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("LEAVE", SCREEN_WIDTH / 2, 200, 4);

    settingsServer = new SettingsServer();
    settingsServer->begin(&configMgr, &audioPlayer);
}

// ─────────────────────────────────────────────────────
//  Hardware setup
// ─────────────────────────────────────────────────────
void setupHardware() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== Jingle Machine Starting ===");

    // RGB LED (init early so it's available for status)
    setupLED();
    setLED(255, 0, 0);  // red = not yet connected

    // Backlight – PWM so brightness is adjustable
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_BITS);
    ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
    ledcWrite(BL_PWM_CHANNEL, 200);  // start at default, updated after config loads

    // TFT
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Jingle Machine", 160, 120, 4);
    delay(500);

    // Boot status screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);

    // Touch
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("1. TFT + Touch OK", 10, 10, 2);
    delay(100);

    // SD Card
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("2. SD init...", 10, 30, 2);
    sdCardAvailable = SD.begin(SD_CS, SPI, 40000000);
    if (!sdCardAvailable) {
        tft.setTextColor(TFT_ORANGE);
        tft.drawString("2. SD: No card", 10, 30, 2);
        Serial.println("WARNING: SD Card not found");
    } else {
        tft.setTextColor(TFT_GREEN);
        tft.drawString("2. SD OK", 10, 30, 2);
        if (!SD.exists("/jingles")) SD.mkdir("/jingles");
    }
    delay(200);

    // Config
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("3. Config...", 10, 50, 2);
    if (!configMgr.begin()) {
        tft.setTextColor(TFT_RED);
        tft.drawString("3. Config FAIL", 10, 50, 2);
        Serial.println("ConfigManager init failed!");
    } else {
        tft.setTextColor(TFT_GREEN);
        tft.drawString("3. Config OK", 10, 50, 2);
    }
    delay(800);
}

void handleBTConnectResult(int result) {
    switch (result) {
        case  1:  currentState = STATE_NORMAL; setLED(0, 0, 0); btnMgr.draw(); break;
        case -1:  runBTScan(); break;
        case -2:  enterSettings(); break;
    }
}

void setup() {
    setupHardware();
    configMgr.loadConfig();

    // Apply display + touch settings from config
    applyBrightness(configMgr.getBrightness());
    touchPressureThreshold = configMgr.getTouchThreshold();

    if (configMgr.isSettingsMode()) {
        configMgr.clearSettingsModeFlag();  // clear before booting (next boot = normal)
        bootSettingsMode();
    } else {
        handleBTConnectResult(tryBTConnect());
    }
}

// ─────────────────────────────────────────────────────
//  Quick Settings (brightness + touch threshold)
//  Layout: two large rows + Done button
//
//  Row layout (each row 48px tall, full-width tap zones):
//    [−]  x:0..130   (130px wide)
//    val  x:130..190 (60px wide, center)
//    [+]  x:190..320 (130px wide)
// ─────────────────────────────────────────────────────
#define QS_MINUS_X1  0
#define QS_MINUS_X2  130
#define QS_VAL_X1    130
#define QS_VAL_X2    190
#define QS_PLUS_X1   190
#define QS_PLUS_X2   SCREEN_WIDTH
#define QS_ROW_H     48

// Row 1: Brightness  y: 50..98
#define QS_ROW1_Y    50
// Row 2: Touch       y: 115..163
#define QS_ROW2_Y    115
// Done               y: 185..230
#define QS_DONE_Y    185

void drawQSRow(const char* label, int rowY, int value, uint16_t accentColor) {
    // Background
    tft.fillRect(0, rowY, SCREEN_WIDTH, QS_ROW_H, TFT_BLACK);

    // [−] zone (left 130px)
    tft.fillRoundRect(3, rowY + 3, QS_MINUS_X2 - 6, QS_ROW_H - 6, 6, 0x3186);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("-", QS_MINUS_X2 / 2, rowY + QS_ROW_H / 2, 4);

    // [+] zone (right 130px)
    tft.fillRoundRect(QS_PLUS_X1 + 3, rowY + 3, SCREEN_WIDTH - QS_PLUS_X1 - 6, QS_ROW_H - 6, 6, 0x3186);
    tft.drawString("+", (QS_PLUS_X1 + SCREEN_WIDTH) / 2, rowY + QS_ROW_H / 2, 4);

    // Value in center
    tft.fillRect(QS_VAL_X1, rowY, QS_VAL_X2 - QS_VAL_X1, QS_ROW_H, TFT_BLACK);
    tft.setTextColor(accentColor);
    tft.drawString(String(value), (QS_VAL_X1 + QS_VAL_X2) / 2, rowY + QS_ROW_H / 2, 2);

    // Label above row (only draw if space)
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.drawString(label, 4, rowY - 14, 1);
}

void drawQuickSettingsScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Quick Settings", SCREEN_WIDTH / 2, 18, 2);

    drawQSRow("Brightness",       QS_ROW1_Y, displayBrightness,      TFT_YELLOW);
    drawQSRow("Touch Sensitivity",QS_ROW2_Y, touchPressureThreshold, TFT_GREEN);

    // Done button  y: QS_DONE_Y..230
    tft.fillRoundRect(20, QS_DONE_Y, SCREEN_WIDTH - 40, 40, 8, TFT_BLUE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Done", SCREEN_WIDTH / 2, QS_DONE_Y + 20, 2);
}

void handleQuickSettings() {
    int x, y;
    if (!touchDebounced(x, y)) return;

    bool changed = false;

    // Row hit: true if y is inside rowY..rowY+QS_ROW_H
    auto inRow = [&](int rowY) { return y >= rowY && y <= rowY + QS_ROW_H; };

    int bright = displayBrightness;
    int thresh = touchPressureThreshold;

    if (inRow(QS_ROW1_Y)) {
        if (x < QS_MINUS_X2) {
            bright = max(10,  bright - 15);
            changed = true;
        } else if (x >= QS_PLUS_X1) {
            bright = min(255, bright + 15);
            changed = true;
        }
    } else if (inRow(QS_ROW2_Y)) {
        if (x < QS_MINUS_X2) {
            thresh = max(50,  thresh - 25);
            changed = true;
        } else if (x >= QS_PLUS_X1) {
            thresh = min(500, thresh + 25);
            changed = true;
        }
    }

    if (changed) {
        applyBrightness(bright);
        touchPressureThreshold = thresh;

        // Persist immediately
        JsonDocument cfg;
        cfg.set(configMgr.getConfig());
        cfg["brightness"]     = displayBrightness;
        cfg["touchThreshold"] = touchPressureThreshold;
        configMgr.saveConfig(cfg);

        // Redraw updated rows
        drawQSRow("Brightness",       QS_ROW1_Y, displayBrightness,      TFT_YELLOW);
        drawQSRow("Touch Sensitivity",QS_ROW2_Y, touchPressureThreshold, TFT_GREEN);
    }

    // Done button
    if (y >= QS_DONE_Y && y <= QS_DONE_Y + 40) {
        currentState = STATE_NORMAL;
        setLED(0, 0, 0);  // back to idle
        btnMgr.draw();
    }
}

// ─────────────────────────────────────────────────────
//  Loop handlers
// ─────────────────────────────────────────────────────
void handleBTFailed() {
    int x, y;
    if (!touchDebounced(x, y)) return;

    if (y >= 75 && y <= 140) {          // "Scan BT Devices" button
        runBTScan();
    } else if (y >= 155 && y <= 220) {  // "Open Settings" button
        enterSettings();
    }
}

// Live scan loop: devices appear as buttons as they are discovered
void handleBTScanning() {
    static int lastDrawnCount = -1;

    // Check for newly found devices and redraw list
    auto results = audioPlayer.getScanResults();
    int count = (int)results.size();
    if (count != lastDrawnCount) {
        lastDrawnCount = count;
        redrawScanDevices(results);
    }

    // Scan finished naturally with no devices
    if (audioPlayer.isScanComplete() && count == 0) {
        audioPlayer.stopScan();
        lastDrawnCount = -1;
        drawBTFailedScreen("No devices found!");
        currentState = STATE_BT_FAILED;
        return;
    }

    // Scan finished naturally with devices → stay on screen so user can tap
    // (no auto-transition – user taps or stops manually)

    int x, y;
    if (!touchDebounced(x, y)) return;

    // Stop button  y: 210..238
    if (y >= 210) {
        audioPlayer.stopScan();
        lastDrawnCount = -1;
        globalBTScanResults = audioPlayer.getScanResults();
        if (globalBTScanResults.empty()) {
            drawBTFailedScreen("No devices found!");
            currentState = STATE_BT_FAILED;
        } else {
            btSelectPage = 0;
            drawBTSelectScreen();
            currentState = STATE_BT_SELECT;
        }
        return;
    }

    // Device tap  y: 40 + i*42, height 38
    int show = min(count, 4);
    for (int i = 0; i < show; i++) {
        int btnY = 40 + i * 42;
        if (y >= btnY && y <= btnY + 38) {
            audioPlayer.stopScan();
            lastDrawnCount = -1;
            globalBTScanResults = audioPlayer.getScanResults();
            selectBTDevice(i);
            return;
        }
    }
}

void handleBTSelect() {
    int x, y;
    if (!touchDebounced(x, y)) return;

    int total = (int)globalBTScanResults.size();
    int pages = max(1, (total + DEVICES_PER_PAGE - 1) / DEVICES_PER_PAGE);
    int start = btSelectPage * DEVICES_PER_PAGE;
    int end = min(start + DEVICES_PER_PAGE, total);

    // Device buttons: each row at y = 30 + row*45, height 40
    for (int i = start; i < end; i++) {
        int btnY = 30 + (i - start) * 45;
        if (y >= btnY && y <= btnY + 40) {
            selectBTDevice(i);
            return;
        }
    }

    // Pagination buttons (y >= 215)
    if (pages > 1 && y >= 215) {
        if (x <= 115 && btSelectPage > 0) {
            btSelectPage--;
            drawBTSelectScreen();
        } else if (x >= 205 && btSelectPage < pages - 1) {
            btSelectPage++;
            drawBTSelectScreen();
        }
    }
}

void handleSettings() {
    // "LEAVE" button: x 110..210, y 178..223
    int x, y;
    if (!touchDebounced(x, y)) return;

    if (y >= 178 && y <= 223 && x >= 110 && x <= 210) {
        Serial.println("=== LEAVING SETTINGS – restarting ===");
        if (settingsServer) {
            delete settingsServer;
            settingsServer = nullptr;
        }
        configMgr.exitSettingsMode();  // clears NVS flag + ESP.restart()
    }
}

void handleNormal() {
    // Detect end of playback → restore idle LED
    static bool wasPlaying = false;
    bool nowPlaying = audioPlayer.isPlaying();
    if (wasPlaying && !nowPlaying) {
        setLED(0, 0, 0);  // playback ended → LED off
    }
    wasPlaying = nowPlaying;

    if (nowPlaying) {
        delay(10);
        return;
    }

    audioPlayer.checkAndReconnectWiFi();

    // React to BT connect/disconnect events
    static bool lastBTState = false;
    bool btNow = audioPlayer.isConnected();
    if (btNow != lastBTState) {
        lastBTState = btNow;
        if (btNow) {
            setLED(0, 0, 0);  // reconnected → LED off
            btnMgr.draw();
        } else {
            setLED(255, 0, 0);  // disconnected → red
            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_ORANGE);
            tft.drawString("Waiting for BT...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
        }
    }

    // Touch state machine: short tap fires jingle, long press (2s) opens Quick Settings
    // Key: record button on finger-DOWN, fire on finger-UP only if < 2s held
    static bool fingerDown = false;
    static unsigned long touchDownTime = 0;
    static int pendingButtonId = -1;

    bool isTouching = touch.touched();
    if (isTouching) {
        TS_Point p = touch.getPoint();
        if (p.z < touchPressureThreshold) isTouching = false;
    }

    if (isTouching) {
        if (!fingerDown) {
            // Finger just touched down – record which button is under it
            fingerDown = true;
            touchDownTime = millis();
            pendingButtonId = (btNow && millis() - lastTouchTime > TOUCH_DEBOUNCE)
                ? btnMgr.checkTouch() : -1;
        } else if (millis() - touchDownTime >= 2000) {
            // Long press threshold reached → Quick Settings
            fingerDown = false;
            pendingButtonId = -1;
            lastTouchTime = millis();
            currentState = STATE_QUICK_SETTINGS;
            setLED(255, 180, 0);  // yellow = settings
            drawQuickSettingsScreen();
            return;
        }
        // still holding – wait
    } else {
        if (fingerDown) {
            // Finger just lifted – short tap → fire the recorded button
            fingerDown = false;
            if (pendingButtonId >= 0) {
                lastTouchTime = millis();
                btnMgr.highlightButton(pendingButtonId);
                String filepath = btnMgr.getButtonFile(pendingButtonId);
                if (filepath.length() > 0 && SD.exists(filepath)) {
                    setLEDHex(configMgr.getButtonColor(pendingButtonId));
                    audioPlayer.playFile(filepath);
                }
            }
            pendingButtonId = -1;
        }
    }
}

void loop() {
    switch (currentState) {
        case STATE_BT_FAILED:   handleBTFailed();   break;
        case STATE_BT_SCANNING: handleBTScanning(); break;
        case STATE_BT_SELECT:   handleBTSelect();   break;
        case STATE_NORMAL:         handleNormal();         break;
        case STATE_QUICK_SETTINGS: handleQuickSettings();  break;
        case STATE_SETTINGS:       handleSettings();       break;
        default: break;
    }
    delay(10);
}
