#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <SD.h>
#include "BluetoothA2DPSource.h"

class AudioPlayer {
public:
    AudioPlayer();

    bool begin(const char* deviceName = "JBL Flip 5");
    bool playFile(const String& filepath);
    void stop();
    bool isPlaying();
    bool isConnected();
    void setVolume(uint8_t volume); // 0-127

private:
    BluetoothA2DPSource a2dp_source;
    static File currentFile;
    static bool playing;
    static uint32_t fileSize;
    static uint32_t bytesRead;

    static int32_t audioCallback(Frame *data, int32_t frameCount);
    bool validateWAVHeader(File& file);
};

#endif
