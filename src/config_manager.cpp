#include "config_manager.h"

ConfigManager::ConfigManager() {
}

bool ConfigManager::begin() {
    prefs.begin("jinglebox", false);

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return false;
    }

    return true;
}

bool ConfigManager::loadConfig() {
    if (!loadFromNVS()) {
        Serial.println("Creating default config");
        createDefaultConfig();
        saveToNVS();
    }
    return true;
}

bool ConfigManager::saveConfig(const JsonDocument& newConfig) {
    config.clear();
    config.set(newConfig);
    return saveToNVS();
}

const JsonDocument& ConfigManager::getConfig() {
    return config;
}

bool ConfigManager::isSettingsMode() {
    return prefs.getBool("settings_mode", false);
}

void ConfigManager::enterSettingsMode() {
    prefs.putBool("settings_mode", true);
    Serial.println("Entering Settings Mode - Restarting...");
    delay(500);
    ESP.restart();
}

void ConfigManager::exitSettingsMode() {
    prefs.putBool("settings_mode", false);
    Serial.println("Exiting Settings Mode - Restarting...");
    delay(500);
    ESP.restart();
}

void ConfigManager::clearSettingsModeFlag() {
    prefs.putBool("settings_mode", false);
    Serial.println("Settings mode flag cleared (no restart)");
}

String ConfigManager::getButtonFile(int id) {
    if (id < 0 || id >= 8) {
        return "";
    }

    JsonArray buttons = config["buttons"].as<JsonArray>();
    for (JsonObject btn : buttons) {
        if (btn["id"].as<int>() == id) {
            return btn["file"].as<String>();
        }
    }
    return "";
}

String ConfigManager::getBTDeviceName() {
    return config["btDevice"].as<String>();
}

String ConfigManager::getBTDeviceMac() {
    return config["btDeviceMac"].as<String>();
}

uint8_t ConfigManager::getBTVolume() {
    return config["btVolume"].as<uint8_t>();
}

void ConfigManager::createDefaultConfig() {
    config.clear();

    config["btDevice"] = "T10";
    config["btVolume"] = 80;

    JsonArray buttons = config["buttons"].to<JsonArray>();

    const char* defaultLabels[8] = {
        "Jingle 1", "Jingle 2", "Jingle 3", "Jingle 4",
        "Jingle 5", "Jingle 6", "Jingle 7", "Jingle 8"
    };

    const char* defaultColors[8] = {
        "#FF5733", "#33FF57", "#3357FF", "#FF33F5",
        "#F5FF33", "#33FFF5", "#FF8C33", "#8C33FF"
    };

    for (int i = 0; i < 8; i++) {
        JsonObject btn = buttons.add<JsonObject>();
        btn["id"] = i;
        btn["label"] = defaultLabels[i];
        btn["file"] = String("/jingles/sound") + String(i + 1) + ".wav";
        btn["color"] = defaultColors[i];
    }
}

bool ConfigManager::loadFromNVS() {
    String jsonStr = prefs.getString("config", "");
    if (jsonStr.length() == 0) {
        Serial.println("No config in NVS");
        return false;
    }

    DeserializationError error = deserializeJson(config, jsonStr);
    if (error) {
        Serial.print("Failed to parse config from NVS: ");
        Serial.println(error.c_str());
        return false;
    }

    Serial.println("Config loaded from NVS");
    return true;
}

bool ConfigManager::saveToNVS() {
    String jsonStr;
    if (serializeJson(config, jsonStr) == 0) {
        Serial.println("Failed to serialize config");
        return false;
    }

    if (!prefs.putString("config", jsonStr)) {
        Serial.println("Failed to write config to NVS");
        return false;
    }

    Serial.println("Config saved to NVS");
    return true;
}
