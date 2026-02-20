#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class ConfigManager {
public:
    ConfigManager();

    bool begin();
    bool loadConfig();
    bool saveConfig(const JsonDocument& config);
    const JsonDocument& getConfig();

    bool isSettingsMode();
    void enterSettingsMode();
    void exitSettingsMode();
    void clearSettingsModeFlag();  // Clear flag without restarting

    String getButtonFile(int id);
    String getButtonColor(int id);
    String getBTDeviceName();
    String getBTDeviceMac();
    uint8_t getBTVolume();
    uint8_t getBrightness();       // 10..255, default 200
    int     getTouchThreshold();   // 50..500, default 200

private:
    Preferences prefs;
    JsonDocument config;

    void createDefaultConfig();
    bool loadFromNVS();
    bool saveToNVS();
};

#endif
