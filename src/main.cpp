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
    STATE_BT_FAILED,    // Show "Scan BT" / "Open Settings" buttons
    STATE_BT_SCANNING,  // BT scan in progress (blocking)
    STATE_BT_SELECT,    // Show scan results as touch buttons
    STATE_NORMAL,       // Jingle buttons active
    STATE_SETTINGS      // WiFi AP + Web UI
};
AppState currentState;

// BT scan results + pagination (extern used by web_server.cpp)
std::vector<AudioPlayer::BTDevice> globalBTScanResults;
int btSelectPage = 0;
const int DEVICES_PER_PAGE = 4;

// Touch debounce
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 300;

bool sdCardAvailable = false;

// ─────────────────────────────────────────────────────
//  Touch helper – reads & maps coordinates, debounced
// ─────────────────────────────────────────────────────
bool touchDebounced(int& x, int& y) {
    if (millis() - lastTouchTime < TOUCH_DEBOUNCE) return false;
    if (!touch.touched()) return false;   // tirqTouched() not used – caused false negatives
    TS_Point p = touch.getPoint();
    if (p.z < 200) return false;
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

// Start BT scan (blocks ~30s), then show device list or retry screen
void runBTScan() {
    currentState = STATE_BT_SCANNING;
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN);
    tft.drawString("Scanning for BT...", SCREEN_WIDTH / 2, 75, 4);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Put speaker in pairing mode!", SCREEN_WIDTH / 2, 130, 2);
    tft.drawString("Scanning 30s...", SCREEN_WIDTH / 2, 155, 2);

    globalBTScanResults = audioPlayer.scanForDevices(30);

    if (globalBTScanResults.empty()) {
        drawBTFailedScreen("No devices found!");
        currentState = STATE_BT_FAILED;
    } else {
        btSelectPage = 0;
        drawBTSelectScreen();
        currentState = STATE_BT_SELECT;
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

    // Backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

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
        case  1:  currentState = STATE_NORMAL; btnMgr.draw(); break;
        case -1:  runBTScan(); break;
        case -2:  enterSettings(); break;
    }
}

void setup() {
    setupHardware();
    configMgr.loadConfig();

    if (configMgr.isSettingsMode()) {
        configMgr.clearSettingsModeFlag();  // clear before booting (next boot = normal)
        bootSettingsMode();
    } else {
        handleBTConnectResult(tryBTConnect());
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
            String devName = (globalBTScanResults[i].name.length() > 0 &&
                              globalBTScanResults[i].name != "Unknown")
                ? globalBTScanResults[i].name : globalBTScanResults[i].mac;
            String devMac = globalBTScanResults[i].mac;

            Serial.printf("[BT] Selected: name=%s mac=%s\n", devName.c_str(), devMac.c_str());

            // Store both name and MAC
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
    // Don't interfere with audio playback
    if (audioPlayer.isPlaying()) {
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
            btnMgr.draw();
        } else {
            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_ORANGE);
            tft.drawString("Waiting for BT...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 4);
        }
    }

    // Touch → button press (with debounce)
    if (btNow && millis() - lastTouchTime > TOUCH_DEBOUNCE) {
        int buttonId = btnMgr.checkTouch();
        if (buttonId >= 0) {
            lastTouchTime = millis();
            btnMgr.highlightButton(buttonId);
            String filepath = btnMgr.getButtonFile(buttonId);
            if (filepath.length() > 0 && SD.exists(filepath)) {
                audioPlayer.playFile(filepath);
            }
        }
    }
}

void loop() {
    switch (currentState) {
        case STATE_BT_FAILED:  handleBTFailed();  break;
        case STATE_BT_SELECT:  handleBTSelect();  break;
        case STATE_NORMAL:     handleNormal();    break;
        case STATE_SETTINGS:   handleSettings();  break;
        default: break;
    }
    delay(10);
}
