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
#include "wifi_credentials.h"

// Test mode - loop playback without WiFi
#define TEST_MODE_LOOP 0

// Test mode - cycle through rotations (0, 90, 180, 270)
#define TEST_MODE_ROTATION 0

// Hardware objects
TFT_eSPI tft = TFT_eSPI();

// Touch (prepared for future hardware)
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS);

// Application objects
AudioPlayer audioPlayer;
ConfigManager configMgr;
ButtonManager btnMgr(&tft, &touch);

// Server objects
SimpleServer simpleServer;
SettingsServer* settingsServer = nullptr;

// Mode flags
bool settingsMode = false;
bool normalMode = false;

// Global storage for BT scan results (scanned before WiFi starts)
std::vector<AudioPlayer::BTDevice> globalBTScanResults;

// Touch debounce
unsigned long lastTouchTime = 0;
const unsigned long touchDebounceDelay = 300;

// Global flag for SD card availability
bool sdCardAvailable = false;

void setupHardware() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n\n=== Jingle Machine Starting ===");

    // Enable Backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    Serial.println("Backlight enabled");

    // Initialize TFT
    tft.init();
    tft.setRotation(1); // Landscape mode
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Jingle Machine", 160, 120, 4);
    delay(500);

    Serial.println("TFT initialized");

    // Show progress
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("1. TFT OK", 10, 10, 2);
    delay(300);

    // Touch - Skip for now (waiting for hardware)
    tft.setTextColor(TFT_ORANGE);
    tft.drawString("2. Touch SKIP (no hw)", 10, 30, 2);
    delay(300);
    // Touch will be activated when new hardware arrives

    // Initialize SD Card
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("3. SD init...", 10, 50, 2);
    delay(100);

    // Use 40MHz SPI clock for faster SD card reads (better audio streaming)
    sdCardAvailable = SD.begin(SD_CS, SPI, 40000000);
    if (!sdCardAvailable) {
        Serial.println("WARNING: SD Card not found");
        tft.setTextColor(TFT_ORANGE);
        tft.drawString("3. SD: No card", 10, 50, 2);
    } else {
        Serial.println("SD Card initialized");
        tft.setTextColor(TFT_GREEN);
        tft.drawString("3. SD OK", 10, 50, 2);

        // Create jingles directory
        if (!SD.exists("/jingles")) {
            SD.mkdir("/jingles");
            Serial.println("Created /jingles directory");
        }
    }
    delay(300);

    // Initialize Config Manager
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("4. Config init...", 10, 70, 2);
    delay(100);

    if (!configMgr.begin()) {
        Serial.println("ConfigManager init failed!");
        tft.setTextColor(TFT_RED);
        tft.drawString("4. Config FAIL", 10, 70, 2);
    } else {
        tft.setTextColor(TFT_GREEN);
        tft.drawString("4. Config OK", 10, 70, 2);
    }
    delay(300);

    tft.setTextColor(TFT_GREEN);
    tft.drawString("5. Hardware OK!", 10, 100, 2);
    delay(1000);
}

void setupWiFi() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Connecting to WiFi...", 160, 80, 2);
    tft.drawString(WIFI_SSID, 160, 110, 2);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");

        tft.fillRect(100, 140, 120, 20, TFT_BLACK);
        tft.setTextColor(TFT_CYAN);
        String dots = "";
        for (int i = 0; i < (attempts % 4); i++) dots += ".";
        tft.drawString(dots, 160, 140, 4);

        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("WiFi Connected!", 160, 60, 4);
        tft.drawString("IP Address:", 160, 110, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString(WiFi.localIP().toString(), 160, 140, 4);
        delay(3000);
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial.println("Starting AP mode...");

        WiFi.mode(WIFI_AP);
        WiFi.softAP("jinglebox", "jingle1234");
        IPAddress IP = WiFi.softAPIP();

        Serial.print("AP IP address: ");
        Serial.println(IP);

        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_ORANGE);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("WiFi Failed!", 160, 60, 4);
        tft.drawString("AP Mode Active", 160, 100, 2);
        tft.drawString("SSID: jinglebox", 160, 130, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("IP: 192.168.4.1", 160, 160, 4);
        delay(3000);
    }
}

void setup() {
    setupHardware();

#if TEST_MODE_LOOP
    // Test mode - skip WiFi, just init BT and loop playback
    Serial.println("=== TEST MODE - LOOP PLAYBACK ===");

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("TEST MODE", 160, 100, 4);
    tft.drawString("Loop Playback", 160, 140, 2);

    // Initialize audio player with T10
    audioPlayer.begin("T10", false);
    audioPlayer.setVolume(80);

    Serial.println("Waiting for BT connection...");
    delay(5000);  // Give BT time to connect

    return;  // Skip rest of setup
#endif

    // Load existing configuration
    Serial.println("Loading config from NVS...");
    configMgr.loadConfig();

    // Check reset reason to decide startup mode
    esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.print("Reset reason: ");
    Serial.println(resetReason);

    // Check if we just rebooted due to BT timeout (flag set)
    bool wasTimeoutReboot = configMgr.isSettingsMode();

    if (wasTimeoutReboot) {
        // We rebooted after BT timeout - go to settings mode
        Serial.println("=== BT TIMEOUT REBOOT - ENTERING SETTINGS MODE ===");

        // CRITICAL: Clear flag immediately so next boot tries normal mode (without restarting)
        configMgr.clearSettingsModeFlag();
        // ==================== SETTINGS MODE ====================
        Serial.println("=== STARTING IN SETTINGS MODE (30s auto-timeout) ===");

        // SCAN FOR BT DEVICES FIRST (before WiFi starts)
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("BT SCAN (30s)", 10, 10, 2);
        tft.drawString("Put speaker in pairing mode", 10, 35, 1);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Scanning...", 10, 60, 2);

        Serial.println("=== Pre-scanning Bluetooth devices (before WiFi) ===");

        // Pass TFT pointer for debug output
        globalBTScanResults = audioPlayer.scanForDevices(30);

        // Show results on screen
        tft.fillRect(0, 60, 320, 180, TFT_BLACK);
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Scan complete!", 10, 60, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("Found: " + String(globalBTScanResults.size()) + " devices", 10, 85, 2);

        // List found devices
        int y = 110;
        for (size_t i = 0; i < globalBTScanResults.size() && i < 6; i++) {
            tft.drawString(globalBTScanResults[i].name, 10, y, 1);
            y += 15;
        }

        Serial.printf("Found %d devices\n", globalBTScanResults.size());
        delay(3000);

        // Start WiFi AP mode directly (no STA mode)
        Serial.println("Starting WiFi AP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("jinglebox", "jingle1234");
        delay(1000);  // Give AP time to start

        IPAddress IP = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(IP);

        tft.fillScreen(TFT_BLUE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SETTINGS MODE", 160, 60, 4);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("WiFi: jinglebox", 160, 100, 2);
        tft.drawString("Password: jingle1234", 160, 120, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("http://192.168.4.1", 160, 160, 4);

        // Draw "Leave" button
        tft.fillRoundRect(110, 170, 100, 50, 8, TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("LEAVE", 160, 195, 4);

        delay(1000);

        Serial.println("Starting SettingsServer...");
        settingsServer = new SettingsServer();
        settingsServer->begin(&configMgr, &audioPlayer);
        settingsMode = true;
        Serial.println("SettingsServer started - NO AUTO-TIMEOUT");
    } else {
        // ==================== NORMAL MODE ATTEMPT (ALWAYS FIRST) ====================
        Serial.println("=== NORMAL MODE - Waiting for BT (30s timeout) ===");

        // Show "Waiting for Bluetooth" with countdown
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_ORANGE);
        tft.drawString("Waiting for", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 40, 4);
        tft.drawString("Bluetooth Connection", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 4);

        // Initialize buttons (no display yet)
        btnMgr.loadConfig(configMgr.getConfig());

        // Initialize audio player
        String btDeviceStr = configMgr.getBTDeviceName();  // Returns MAC address
        static char btDevice[32];
        strncpy(btDevice, btDeviceStr.c_str(), 31);
        btDevice[31] = '\0';

        Serial.print("BT MAC from config: ");
        Serial.println(btDevice);
        Serial.print("BT Volume from config: ");
        Serial.println(configMgr.getBTVolume());

        bool clearPairing = false;
        audioPlayer.begin(btDevice, clearPairing);  // Connect using MAC address
        audioPlayer.setVolume(configMgr.getBTVolume());

        // Wait up to 30 seconds for BT connection
        unsigned long btWaitStart = millis();
        const unsigned long BT_TIMEOUT_MS = 30000;  // 30 seconds
        bool btConnected = false;

        while (millis() - btWaitStart < BT_TIMEOUT_MS) {
            if (audioPlayer.isConnected()) {
                btConnected = true;
                Serial.println("[BT] Connected!");
                break;
            }

            // Update countdown on screen
            int remaining = (BT_TIMEOUT_MS - (millis() - btWaitStart)) / 1000;
            tft.fillRect(0, SCREEN_HEIGHT/2 + 40, SCREEN_WIDTH, 40, TFT_BLACK);
            tft.setTextColor(TFT_YELLOW);
            tft.drawString(String(remaining) + "s", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 50, 4);

            delay(500);
        }

        if (btConnected) {
            // BT connected - enter normal mode
            normalMode = true;
            Serial.println("Normal mode activated (WiFi OFF for perfect audio)");

            // Enable simulated touch (slower, no serial debug to avoid audio interference)
            btnMgr.setSimulatedTouch(true);

            // Show buttons
            Serial.println("[BT] Showing buttons");
            btnMgr.draw();
        } else {
            // BT timeout - set flag and restart for settings mode
            Serial.println("[BT] Connection timeout - switching to Settings Mode");

            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_RED);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("No Bluetooth!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 4);
            tft.setTextColor(TFT_YELLOW);
            tft.drawString("Restarting for Settings...", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 20, 2);

            // Set flag to enter settings mode on next boot
            configMgr.enterSettingsMode();
            delay(2000);

            // RESTART to get clean state for BT scanning
            Serial.println("Restarting ESP32 for clean BT scan...");
            ESP.restart();
        }
    }
}

void switchToNormalMode() {
    Serial.println("=== SWITCHING TO NORMAL MODE ===");

    // Stop settings server
    if (settingsServer) {
        delete settingsServer;
        settingsServer = nullptr;
    }
    settingsMode = false;

    // CRITICAL: Clear settings mode flag so next boot goes to normal mode
    configMgr.exitSettingsMode();

    // Disable WiFi completely for better audio
    Serial.println("Disabling WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);

    // Show "Waiting for Bluetooth" with countdown
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_ORANGE);
    tft.drawString("Waiting for", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 40, 4);
    tft.drawString("Bluetooth Connection", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 4);

    // Load configuration and initialize buttons
    btnMgr.loadConfig(configMgr.getConfig());

    // Initialize audio player
    String btDeviceStr = configMgr.getBTDeviceName();  // Returns MAC address
    static char btDevice[32];
    strncpy(btDevice, btDeviceStr.c_str(), 31);
    btDevice[31] = '\0';

    Serial.print("BT MAC from config: ");
    Serial.println(btDevice);
    Serial.print("BT Volume from config: ");
    Serial.println(configMgr.getBTVolume());

    bool clearPairing = false;
    audioPlayer.begin(btDevice, clearPairing);  // Connect using MAC address
    audioPlayer.setVolume(configMgr.getBTVolume());

    // Wait up to 30 seconds for BT connection
    unsigned long btWaitStart = millis();
    const unsigned long BT_TIMEOUT_MS = 30000;  // 30 seconds
    bool btConnected = false;

    while (millis() - btWaitStart < BT_TIMEOUT_MS) {
        if (audioPlayer.isConnected()) {
            btConnected = true;
            Serial.println("[BT] Connected!");
            break;
        }

        // Update countdown on screen
        int remaining = (BT_TIMEOUT_MS - (millis() - btWaitStart)) / 1000;
        tft.fillRect(0, SCREEN_HEIGHT/2 + 40, SCREEN_WIDTH, 40, TFT_BLACK);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString(String(remaining) + "s", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 50, 4);

        delay(500);
    }

    if (btConnected) {
        // BT connected - enter normal mode
        normalMode = true;
        Serial.println("Normal mode activated (WiFi OFF for perfect audio)");

        // Enable simulated touch
        btnMgr.setSimulatedTouch(true);

        // Show buttons
        Serial.println("[BT] Showing buttons");
        btnMgr.draw();
    } else {
        // BT timeout - restart to try again
        Serial.println("[BT] Connection timeout after settings - restarting...");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No Bluetooth!", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 4);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Restarting...", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 20, 2);
        delay(3000);
        ESP.restart();
    }
}

void oldNormalModeCodeRemoved() {
    if (false) {
        // ==================== NORMAL MODE ====================
        Serial.println("=== NORMAL MODE ===");

        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Normal Mode Init...", 10, 10, 2);

        // Load configuration
        tft.drawString("Loading config...", 10, 30, 2);
        configMgr.loadConfig();
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Config OK", 10, 30, 2);
        delay(300);

        // Initialize buttons (display only - no touch yet)
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Loading buttons...", 10, 50, 2);
        btnMgr.loadConfig(configMgr.getConfig());
        btnMgr.draw();
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Buttons OK", 10, 50, 2);
        delay(300);

        // Initialize audio player
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Starting audio...", 10, 70, 2);

        // Get BT device name and store in a static char array to prevent string corruption
        String btDeviceStr = configMgr.getBTDeviceName();
        static char btDevice[32];
        strncpy(btDevice, btDeviceStr.c_str(), 31);
        btDevice[31] = '\0';

        Serial.print("BT Device from config: ");
        Serial.println(btDevice);
        Serial.print("BT Volume from config: ");
        Serial.println(configMgr.getBTVolume());

        // Clear old pairing to force fresh discovery (set to false after first successful pairing)
        bool clearPairing = false;  // Changed to false to stop boot loop
        audioPlayer.begin(btDevice, clearPairing);
        audioPlayer.setVolume(configMgr.getBTVolume());
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Audio OK", 10, 70, 2);
        delay(300);

        // Start web server with playback controls
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Starting server...", 10, 90, 2);
        simpleServer.setConfigManager(&configMgr);
        simpleServer.setAudioPlayer(&audioPlayer);
        simpleServer.begin();
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Server OK!", 10, 90, 2);
        delay(500);

        normalMode = true;
        Serial.println("Normal mode initialized");
        Serial.println("Web UI: http://" + WiFi.localIP().toString());

        // Check initial BT connection state and show appropriate screen
        delay(500);  // Give BT a moment to establish connection
        if (audioPlayer.isConnected()) {
            Serial.println("[BT] Connected on startup - showing buttons");
            btnMgr.draw();
        } else {
            Serial.println("[BT] Not connected on startup - showing wait screen");
            tft.fillScreen(TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_ORANGE);
            tft.drawString("Waiting for", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 4);
            tft.drawString("Bluetooth Connection", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 20, 4);
        }
    }
}

void loop() {
#if TEST_MODE_ROTATION
    // Test mode - cycle through all rotations every 3 seconds
    static unsigned long lastRotationChange = 0;
    static int rotationIndex = 0;
    static const int rotations[] = {0, 90, 180, 270};
    static const int numRotations = 4;

    if (millis() - lastRotationChange > 3000) {  // Change every 3 seconds
        lastRotationChange = millis();

        // Get current rotation
        int currentRotation = rotations[rotationIndex];

        Serial.printf("\n=== Testing Rotation: %d° ===\n", currentRotation);

        // Update config with new rotation
        JsonDocument doc;
        doc["rotation"] = currentRotation;
        doc["borderColor"] = "#FFFFFF";
        doc["borderThickness"] = 3;

        // Add sample buttons
        JsonArray buttons = doc["buttons"].to<JsonArray>();

        JsonObject btn0 = buttons.add<JsonObject>();
        btn0["id"] = 0;
        btn0["label"] = "Test 0°";
        btn0["file"] = "/jingles/test.wav";
        btn0["color"] = "#FF0000";
        btn0["textColor"] = "#FFFFFF";

        JsonObject btn1 = buttons.add<JsonObject>();
        btn1["id"] = 1;
        btn1["label"] = "Test 90°";
        btn1["file"] = "/jingles/test.wav";
        btn1["color"] = "#00FF00";
        btn1["textColor"] = "#000000";

        JsonObject btn2 = buttons.add<JsonObject>();
        btn2["id"] = 2;
        btn2["label"] = "Test 180°";
        btn2["file"] = "/jingles/test.wav";
        btn2["color"] = "#0000FF";
        btn2["textColor"] = "#FFFFFF";

        JsonObject btn3 = buttons.add<JsonObject>();
        btn3["id"] = 3;
        btn3["label"] = "Test 270°";
        btn3["file"] = "/jingles/test.wav";
        btn3["color"] = "#FFFF00";
        btn3["textColor"] = "#000000";

        // Load config and draw
        btnMgr.loadConfig(doc);
        btnMgr.draw();

        // Show rotation info on screen
        tft.fillRect(0, 0, 150, 30, TFT_BLACK);
        tft.setTextColor(TFT_CYAN);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Rotation: " + String(currentRotation) + "°", 10, 10, 2);

        // Move to next rotation
        rotationIndex = (rotationIndex + 1) % numRotations;
    }

    delay(100);
    return;
#endif

#if TEST_MODE_LOOP
    // Test mode - loop playback every 10 seconds
    static unsigned long lastPlay = 0;
    static int playCount = 0;

    if (millis() - lastPlay > 10000) {  // 10 second pause between plays
        lastPlay = millis();
        playCount++;

        // Check heap memory
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minFreeHeap = ESP.getMinFreeHeap();

        Serial.printf("[%lu] === PLAY #%d ===\n", millis(), playCount);
        Serial.printf("[%lu] Free Heap: %u bytes, Min Free: %u bytes\n", millis(), freeHeap, minFreeHeap);

        if (audioPlayer.isConnected()) {
            Serial.printf("[%lu] BT Connected, starting playback...\n", millis());
            audioPlayer.playFile("/jingles/file_example_WAV_1MG.wav");
        } else {
            Serial.printf("[%lu] BT Not connected!\n", millis());
        }
    }

    delay(100);
    return;
#endif

    if (settingsMode) {
        // Settings Mode: AsyncElegantOTA runs automatically
        // NO AUTO-TIMEOUT - stays in settings mode until user exits
        delay(10);
        return;
    }

    if (normalMode) {
        // CRITICAL: Do absolutely nothing while audio is playing to avoid interference
        if (audioPlayer.isPlaying()) {
            delay(10);
            return;
        }

        // Check if WiFi needs to be reconnected after playback
        audioPlayer.checkAndReconnectWiFi();

        // Check BT connection and show appropriate screen
        static bool lastBTState = false;
        bool currentBTState = audioPlayer.isConnected();

        if (currentBTState != lastBTState) {
            lastBTState = currentBTState;

            if (currentBTState) {
                // BT connected - show buttons
                Serial.println("[BT] Connected - showing buttons");
                btnMgr.draw();
            } else {
                // BT disconnected - show "Wait for Connect" overlay
                Serial.println("[BT] Disconnected - showing wait screen");
                tft.fillScreen(TFT_BLACK);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(TFT_ORANGE);
                tft.drawString("Waiting for", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 4);
                tft.drawString("Bluetooth Connection", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 20, 4);
            }
        }

        // Handle touch events (only when NOT playing and BT is connected)
        if (currentBTState && millis() - lastTouchTime > touchDebounceDelay) {
            int buttonId = btnMgr.checkTouch();

            if (buttonId >= 0) {
                lastTouchTime = millis();
                Serial.printf("[TOUCH] Button %d pressed\n", buttonId);

                // Highlight button
                btnMgr.highlightButton(buttonId);

                // Get file and play
                String filepath = btnMgr.getButtonFile(buttonId);
                if (filepath.length() > 0 && SD.exists(filepath)) {
                    Serial.printf("[PLAY] Starting: %s\n", filepath.c_str());
                    audioPlayer.playFile(filepath);
                } else {
                    Serial.printf("[ERROR] No file for button %d\n", buttonId);
                }
            }
        }
    }

    delay(10);
}
