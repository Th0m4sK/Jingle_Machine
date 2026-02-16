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
    String getBTDeviceName();
    uint8_t getBTVolume();

private:
    Preferences prefs;
    JsonDocument config;

    void createDefaultConfig();
    bool loadFromNVS();
    bool saveToNVS();
};

#endif
