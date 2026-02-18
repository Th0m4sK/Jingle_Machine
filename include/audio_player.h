#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include "BluetoothA2DPSource.h"

class AudioPlayer {
public:
    AudioPlayer();

    bool begin(const char* deviceName = "JBL Flip 5", const char* deviceMac = nullptr, bool clearPairing = false);
    void end();  // Stop A2DP (call before starting WiFi AP)
    bool playFile(const String& filepath);
    void stop();
    bool isPlaying();
    bool isConnected();
    void setVolume(uint8_t volume); // 0-127
    void clearBluetoothPairing(); // Clear stored BT pairing
    void checkAndReconnectWiFi(); // Check if WiFi reconnection needed after playback
    static void resetAudioBuffers(); // Reset static buffers in callback

    // NEW: Scanning and pairing methods (Settings Mode only)
    struct BTDevice {
        String name;
        String mac;
        int rssi;
    };

    std::vector<BTDevice> scanForDevices(int timeoutSeconds = 10);
    bool pairDevice(const String& deviceName, int timeoutSeconds = 60);
    bool playTestSound();

private:
    BluetoothA2DPSource a2dp_source;
    static File currentFile;
    static bool playing;
    static bool isMono;  // Track if current file is mono (1 channel)
    static uint32_t fileSize;
    static uint32_t bytesRead;
    static bool needsWiFiReconnect;  // Flag to trigger WiFi reconnection after playback

    static int32_t audioCallback(Frame *data, int32_t frameCount);
    bool validateWAVHeader(File& file);

    // NEW: Static callback for scanning
    static bool scanCallback(const char* ssid, esp_bd_addr_t address, int rssi);
};

#endif
