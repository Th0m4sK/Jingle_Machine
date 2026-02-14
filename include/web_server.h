#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include "config_manager.h"
#include "audio_player.h"

// Simple Server for Normal Mode
class SimpleServer {
public:
    SimpleServer();
    void begin();
    void handle();
    void setConfigManager(ConfigManager* mgr);
    void setAudioPlayer(AudioPlayer* player);

private:
    WiFiServer server;
    ConfigManager* configMgr;
    AudioPlayer* audioPlayer;
};

// Full AsyncWebServer for Settings Mode
class SettingsServer {
public:
    SettingsServer();
    void begin(ConfigManager* mgr);

private:
    AsyncWebServer server;
    ConfigManager* configMgr;

    void setupRoutes();
    void handleFileUpload(AsyncWebServerRequest *request, String filename,
                         size_t index, uint8_t *data, size_t len, bool final);
};

#endif
