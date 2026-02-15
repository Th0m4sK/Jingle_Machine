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

    bool startInSettingsMode = false;

    if (resetReason == ESP_RST_POWERON) {
        // Power cycle - always start in settings mode
        Serial.println("Power cycle detected - forcing Settings Mode");
        startInSettingsMode = true;
    } else {
        // Software reset or other - use NVS value
        Serial.println("Software reset - checking NVS setting");
        startInSettingsMode = configMgr.isSettingsMode();
    }

    // Check if we should start in settings mode or normal mode
    if (startInSettingsMode) {
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

        setupWiFi();

        tft.fillScreen(TFT_BLUE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SETTINGS MODE", 160, 60, 4);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Open browser:", 160, 110, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("http://" + WiFi.localIP().toString(), 160, 140, 4);

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
        Serial.println("SettingsServer started - will auto-switch to normal mode after 30s");
    } else {
        // ==================== NORMAL MODE ====================
        Serial.println("=== STARTING IN NORMAL MODE (WiFi OFF) ===");

        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("Normal Mode Init...", 10, 10, 2);

        // Initialize buttons (display only - no touch yet)
        tft.drawString("Loading buttons...", 10, 30, 2);
        btnMgr.loadConfig(configMgr.getConfig());
        btnMgr.draw();
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Buttons OK", 10, 30, 2);
        delay(300);

        // Initialize audio player
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Starting audio...", 10, 50, 2);

        // Get BT device MAC address for connection
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
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Audio OK", 10, 50, 2);
        delay(300);

        // Draw button UI
        btnMgr.draw();

        // Show status overlay
        tft.fillRect(0, 0, 320, 25, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_GREEN);
        tft.drawString(String("BT: ") + btDevice, 5, 5, 1);

        normalMode = true;
        Serial.println("Normal mode initialized (WiFi OFF for perfect audio)");
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

    // Disable WiFi completely for better audio
    Serial.println("Disabling WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Normal Mode Init...", 10, 10, 2);

    // Load configuration
    tft.drawString("Loading config...", 10, 30, 2);
    // Config already loaded
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

    // Get BT device MAC address for connection
    String btDeviceStr = configMgr.getBTDeviceName();  // Returns MAC address
    static char btDevice[32];
    strncpy(btDevice, btDeviceStr.c_str(), 31);
    btDevice[31] = '\0';

    Serial.print("BT MAC from config: ");
    Serial.println(btDevice);
    Serial.print("BT Volume from config: ");
    Serial.println(configMgr.getBTVolume());

    // Clear old pairing to force fresh discovery (set to false after first successful pairing)
    bool clearPairing = false;
    audioPlayer.begin(btDevice, clearPairing);  // Connect using MAC address
    audioPlayer.setVolume(configMgr.getBTVolume());
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Audio OK", 10, 70, 2);
    delay(300);

    // Draw button UI
    btnMgr.draw();

    // Show status overlay
    tft.fillRect(0, 0, 320, 25, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString(String("BT: ") + btDevice, 5, 5, 1);

    normalMode = true;
    Serial.println("Normal mode initialized (WiFi OFF for perfect audio)");
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

        // Draw button UI
        btnMgr.draw();

        // Show status overlay
        tft.fillRect(0, 0, 320, 25, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_GREEN);
        tft.drawString(String("BT: ") + btDevice, 5, 5, 1);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(TFT_CYAN);
        tft.drawString(WiFi.localIP().toString(), 315, 5, 1);

        normalMode = true;
        Serial.println("Normal mode initialized");
        Serial.println("Web UI: http://" + WiFi.localIP().toString());
    }
}

void loop() {
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
        // Auto-switch to normal mode after 30 seconds of inactivity
        static bool settingsModeActive = false;
        static int lastDisplayedSeconds = -1;

        // Initialize timer when first entering settings mode
        if (!settingsModeActive) {
            if (settingsServer) {
                settingsServer->resetTimeout();  // Initialize the activity timer
            }
            settingsModeActive = true;
            lastDisplayedSeconds = -1;
            Serial.println("[SETTINGS] Timer started - 30s until auto-switch");
        }

        // Get time since last activity from server
        unsigned long lastActivity = settingsServer ? settingsServer->getLastActivity() : millis();
        unsigned long elapsed = millis() - lastActivity;
        int remainingSeconds = 30 - (elapsed / 1000);
        if (remainingSeconds < 0) remainingSeconds = 0;

        // Update display every second
        if (remainingSeconds != lastDisplayedSeconds) {
            lastDisplayedSeconds = remainingSeconds;

            // Clear countdown area and redraw
            tft.fillRect(0, 0, 60, 30, TFT_BLUE);
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(TFT_WHITE);
            tft.drawString(String(remainingSeconds) + "s", 10, 10, 4);
        }

        if (elapsed > 30000) {  // 30 seconds timeout since last activity
            Serial.println("[TIMEOUT] No activity for 30s - switching to normal mode");
            settingsModeActive = false;  // Reset flag
            switchToNormalMode();
        }

        delay(10);
        return;
    }

    if (normalMode) {
        // Simulate Button 1 press every 10 seconds for testing (touch screen is broken)
        static unsigned long lastButtonSim = 0;
        if (millis() - lastButtonSim > 10000) {
            lastButtonSim = millis();
            Serial.println("[TEST] Simulating Button 1 press (Button 0 in array)");

            String filepath = configMgr.getButtonFile(0);
            if (filepath.length() > 0 && SD.exists(filepath)) {
                Serial.print("[TEST] Playing: ");
                Serial.println(filepath);
                audioPlayer.playFile(filepath);
            } else {
                Serial.println("[TEST] No file configured for Button 1");
            }
        }

        // Check if WiFi needs to be reconnected after playback
        audioPlayer.checkAndReconnectWiFi();

        // Update BT connection status on display
        static bool lastBTState = false;
        bool currentBTState = audioPlayer.isConnected();

        if (currentBTState != lastBTState) {
            lastBTState = currentBTState;

            tft.fillRect(0, 0, 80, 20, TFT_BLACK);
            tft.setTextDatum(TL_DATUM);

            if (currentBTState) {
                tft.setTextColor(TFT_GREEN);
                tft.drawString("BT: OK", 5, 5, 1);
            } else {
                tft.setTextColor(TFT_ORANGE);
                tft.drawString("BT: DISC", 5, 5, 1);
            }
        }

        // Audio playback status feedback
        if (audioPlayer.isPlaying()) {
            static unsigned long lastBlink = 0;
            static bool blinkState = false;

            if (millis() - lastBlink > 500) {
                lastBlink = millis();
                blinkState = !blinkState;

                tft.fillRect(290, 0, 30, 20, blinkState ? TFT_GREEN : TFT_BLACK);
                if (blinkState) {
                    tft.setTextColor(TFT_BLACK);
                    tft.setTextDatum(TC_DATUM);
                    tft.drawString("PLAY", 305, 5, 1);
                }
            }
        }
    }

    delay(10);
}
