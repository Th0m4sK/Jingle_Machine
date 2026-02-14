#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <WiFi.h>

#include "pin_config.h"
#include "audio_player.h"
#include "button_manager.h"
#include "config_manager.h"
#include "web_server.h"
#include "wifi_credentials.h"

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

    sdCardAvailable = SD.begin(SD_CS);
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
    setupWiFi();

    if (configMgr.isSettingsMode()) {
        // ==================== SETTINGS MODE ====================
        Serial.println("=== SETTINGS MODE ===");

        // Load existing configuration
        Serial.println("Loading config from NVS...");
        configMgr.loadConfig();

        tft.fillScreen(TFT_BLUE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SETTINGS MODE", 160, 60, 4);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Open browser:", 160, 110, 2);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("http://" + WiFi.localIP().toString(), 160, 140, 4);
        tft.setTextColor(TFT_CYAN);
        tft.drawString("Starting server...", 160, 180, 2);

        delay(1000);

        Serial.println("Starting SettingsServer...");
        settingsServer = new SettingsServer();
        settingsServer->begin(&configMgr);
        settingsMode = true;

        tft.fillRect(0, 180, 320, 40, TFT_BLUE);
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Server Ready!", 160, 200, 2);
        Serial.println("SettingsServer started");

    } else {
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
        String btDevice = configMgr.getBTDeviceName();
        audioPlayer.begin(btDevice.c_str());
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
        tft.drawString("BT: " + btDevice, 5, 5, 1);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(TFT_CYAN);
        tft.drawString(WiFi.localIP().toString(), 315, 5, 1);

        normalMode = true;
        Serial.println("Normal mode initialized");
        Serial.println("Web UI: http://" + WiFi.localIP().toString());
    }
}

void loop() {
    if (settingsMode) {
        // Settings Mode: AsyncElegantOTA runs automatically
        delay(10);
        return;
    }

    if (normalMode) {
        // Handle web requests
        simpleServer.handle();

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
