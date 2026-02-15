#include "audio_player.h"
#include "pin_config.h"
#include <Preferences.h>
#include <nvs_flash.h>
#include <WiFi.h>
#include "wifi_credentials.h"
#include <math.h>

// Static members
File AudioPlayer::currentFile;
bool AudioPlayer::playing = false;
bool AudioPlayer::isMono = false;
uint32_t AudioPlayer::fileSize = 0;
uint32_t AudioPlayer::bytesRead = 0;
bool AudioPlayer::needsWiFiReconnect = false;
static unsigned long silencePaddingStart = 0;  // Track when to start silence padding
static const unsigned long SILENCE_PADDING_MS = 200;  // 200ms silence after WAV to prevent click
static bool inSilencePadding = false;  // Flag to track if we're in silence padding mode
static const unsigned long FADEOUT_MS = 50;  // Fade out last 50ms of WAV to prevent click
static unsigned long fadeOutStart = 0;  // When fade-out started
static bool inFadeOut = false;  // Flag for fade-out mode

AudioPlayer::AudioPlayer() {
}

void AudioPlayer::clearBluetoothPairing() {
    Serial.println("Clearing Bluetooth pairing data...");
    Serial.println("Erasing NVS partition...");

    // Nuclear option: Erase the entire NVS partition for Bluetooth
    nvs_flash_erase_partition("nvs");

    Preferences prefs;
    // Clear the ESP32-A2DP library's NVS namespace
    prefs.begin("NVS_A2DP", false);
    prefs.clear();
    prefs.end();

    // Also try other possible namespaces
    prefs.begin("a2dp", false);
    prefs.clear();
    prefs.end();

    Serial.println("Bluetooth pairing cleared! Device will restart...");
    delay(2000);
    ESP.restart();
}

bool AudioPlayer::begin(const char* deviceName, bool clearPairing) {
    Serial.println("=== Bluetooth A2DP Initialization ===");
    Serial.print("Target device: ");
    Serial.println(deviceName);

    // Clear old pairing if requested
    if (clearPairing) {
        clearBluetoothPairing();
        Serial.println("Will search for device by name...");
    }

    // CRITICAL: Register the audio callback BEFORE starting
    Serial.println("Registering audio callback...");
    a2dp_source.set_data_callback_in_frames(audioCallback);

    a2dp_source.set_auto_reconnect(true);
    Serial.println("Starting Bluetooth A2DP Source...");
    a2dp_source.start(deviceName);

    Serial.println("Waiting for connection...");
    delay(1000); // Give time to establish connection

    if (a2dp_source.is_connected()) {
        Serial.println("Bluetooth connected successfully!");
    } else {
        Serial.println("Bluetooth not connected yet (will retry)");
    }

    return true;
}

bool AudioPlayer::playFile(const String& filepath) {
    Serial.println("=== playFile() called ===");
    Serial.print("File: ");
    Serial.println(filepath);
    Serial.print("BT Connected: ");
    Serial.println(a2dp_source.is_connected() ? "YES" : "NO");

    // CRITICAL: Don't play if Bluetooth is not connected
    if (!a2dp_source.is_connected()) {
        Serial.println("ERROR: Cannot play - Bluetooth not connected!");
        return false;
    }

    if (playing) {
        Serial.println("Stopping current playback...");
        stop();
    }

    // Reset audio buffers to ensure clean start
    resetAudioBuffers();

    // Disconnect WiFi during playback (only if WiFi is active)
    if (WiFi.getMode() != WIFI_MODE_NULL) {
        Serial.println("Disconnecting WiFi for better audio quality...");
        WiFi.disconnect(false, true);  // Don't erase config, do erase AP
        delay(100);
    }

    // WAV file handling
    Serial.println("Opening SD file...");
    currentFile = SD.open(filepath);
    if (!currentFile) {
        Serial.println("Failed to open file: " + filepath);
        return false;
    }

    Serial.println("Validating WAV header...");
    if (!validateWAVHeader(currentFile)) {
        Serial.println("Invalid WAV file format");
        currentFile.close();
        return false;
    }

    fileSize = currentFile.size();
    bytesRead = 44; // Skip WAV header (already read by validation)
    silencePaddingStart = 0;  // Reset silence padding timer
    inSilencePadding = false;  // Reset silence padding flag
    inFadeOut = false;  // Reset fade-out flag
    fadeOutStart = 0;

    // Reset audio callback static buffers (declared in audioCallback)
    // We need to use a global or add a reset function

    playing = true;

    Serial.println("Playing: " + filepath);
    Serial.print("File size: ");
    Serial.println(fileSize);
    return true;
}

void AudioPlayer::stop() {
    playing = false;
    inSilencePadding = false;
    silencePaddingStart = 0;
    inFadeOut = false;
    fadeOutStart = 0;

    // Clean up WAV file if active
    if (currentFile) {
        currentFile.close();
    }

    bytesRead = 0;

    // Reconnect WiFi after playback (only if WiFi was active)
    if (WiFi.getMode() != WIFI_MODE_NULL) {
        Serial.println("Reconnecting WiFi...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        // Don't wait - let it connect in background
    }
}

bool AudioPlayer::isPlaying() {
    return playing;
}

bool AudioPlayer::isConnected() {
    return a2dp_source.is_connected();
}

void AudioPlayer::setVolume(uint8_t volume) {
    a2dp_source.set_volume(volume);
}

// Static buffers used in audioCallback - need to be accessible from here
static uint8_t audioBuf[2048];
static int audioBufPos = 0;
static int audioBufLen = 0;

void AudioPlayer::resetAudioBuffers() {
    // Reset all static buffers used in audioCallback
    memset(audioBuf, 0, sizeof(audioBuf));
    audioBufPos = 0;
    audioBufLen = 0;
    Serial.println("[AUDIO] Buffers reset");
}

void AudioPlayer::checkAndReconnectWiFi() {
    // Skip if WiFi not initialized
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        needsWiFiReconnect = false;
        return;
    }

    static bool reconnecting = false;
    static unsigned long reconnectStart = 0;

    if (needsWiFiReconnect && !reconnecting) {
        needsWiFiReconnect = false;
        reconnecting = true;
        reconnectStart = millis();

        Serial.println("Reconnecting WiFi after playback...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    // Non-blocking WiFi reconnection check
    if (reconnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi reconnected: " + WiFi.localIP().toString());
            reconnecting = false;
        } else if (millis() - reconnectStart > 10000) {
            // Timeout after 10 seconds
            Serial.println("WiFi reconnection timeout");
            reconnecting = false;
        }
    }
}

int32_t AudioPlayer::audioCallback(Frame *data, int32_t frameCount) {
    // Minimal debug output to save memory
    static bool firstCall = true;
    if (firstCall) {
        Serial.println("[AUDIO] Callback started");
        firstCall = false;
    }

    // Check if we have a file to play
    if (!playing) {
        // Not playing - return silence
        for (int i = 0; i < frameCount; i++) {
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
        return frameCount;
    }

    // Handle WAV playback
    if (!currentFile && !inSilencePadding) {
        // No file and not padding - return silence
        for (int i = 0; i < frameCount; i++) {
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
        return frameCount;
    }

    // If in silence padding mode, play silence to prevent click
    if (inSilencePadding) {
        // Check if padding is complete
        if ((millis() - silencePaddingStart) >= SILENCE_PADDING_MS) {
            Serial.println("[AUDIO CB] Silence padding complete - stopping");
            playing = false;
            inSilencePadding = false;
            silencePaddingStart = 0;
            needsWiFiReconnect = true;
        }

        // Send silence
        for (int i = 0; i < frameCount; i++) {
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
        return frameCount;
    }

    // Read audio data from file (optimized: 2KB buffer for smooth playback)
    // Using extern static buffers defined at file scope
    int bytesPerFrame = isMono ? 2 : 4;  // Mono=2 bytes, Stereo=4 bytes

    // Check if we should start fade-out (near end of file)
    if (!inFadeOut && currentFile.available() > 0) {
        int bytesLeft = currentFile.available();
        int bytesForFadeOut = (FADEOUT_MS * 44100 * bytesPerFrame) / 1000;  // Bytes needed for 50ms

        if (bytesLeft <= bytesForFadeOut && bytesLeft > 0) {
            inFadeOut = true;
            fadeOutStart = millis();
            Serial.printf("[AUDIO CB] Starting fade-out, %d bytes left\n", bytesLeft);
        }
    }

    for (int i = 0; i < frameCount; i++) {
        // Refill buffer if needed
        if (audioBufPos + bytesPerFrame > audioBufLen) {
            // Save remaining bytes at the end of buffer
            int remaining = audioBufLen - audioBufPos;
            if (remaining > 0) {
                // Move remaining bytes to beginning of buffer
                for (int j = 0; j < remaining; j++) {
                    audioBuf[j] = audioBuf[audioBufPos + j];
                }
            }

            // Check if file has data available before reading
            int available = currentFile.available();

            // Read next block from SD to fill the rest of the buffer
            int newBytes = 0;
            if (available > 0) {
                newBytes = currentFile.read(audioBuf + remaining, 2048 - remaining);
            }
            audioBufLen = remaining + newBytes;
            audioBufPos = 0;

            if (audioBufLen == 0 || newBytes == 0) {
                // End of file - start silence padding
                if (playing && !inSilencePadding) {
                    Serial.println("[AUDIO CB] End of file reached - starting silence padding");
                    silencePaddingStart = millis();
                    inSilencePadding = true;
                    currentFile.close();
                }

                // Silence during this frame (sine wave starts next callback)
                data[i].channel1 = 0;
                data[i].channel2 = 0;
                continue;
            }
        }

        // Read from buffer (handle both mono and stereo)
        if (audioBufPos + bytesPerFrame <= audioBufLen) {
            int16_t left, right;

            if (isMono) {
                // Mono: read 2 bytes and duplicate to both channels
                int16_t sample = audioBuf[audioBufPos] | (audioBuf[audioBufPos + 1] << 8);
                left = right = sample;
                audioBufPos += 2;
                bytesRead += 2;
            } else {
                // Stereo: read 4 bytes (left and right)
                left = audioBuf[audioBufPos] | (audioBuf[audioBufPos + 1] << 8);
                right = audioBuf[audioBufPos + 2] | (audioBuf[audioBufPos + 3] << 8);
                audioBufPos += 4;
                bytesRead += 4;
            }

            // Apply fade-out if we're near end of file
            if (inFadeOut) {
                unsigned long fadeElapsed = millis() - fadeOutStart;
                float fadeFactor = 1.0f - ((float)fadeElapsed / (float)FADEOUT_MS);
                fadeFactor = max(0.0f, min(1.0f, fadeFactor));  // Clamp 0-1

                left = (int16_t)((float)left * fadeFactor);
                right = (int16_t)((float)right * fadeFactor);
            }

            data[i].channel1 = left;
            data[i].channel2 = right;
        } else {
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
    }

    return frameCount;
}

bool AudioPlayer::validateWAVHeader(File& file) {
    if (file.size() < 44) {
        return false;
    }

    char header[44];
    file.read((uint8_t*)header, 44);

    // Check RIFF header
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        return false;
    }

    // Check WAVE format
    if (header[8] != 'W' || header[9] != 'A' || header[10] != 'V' || header[11] != 'E') {
        return false;
    }

    // Check audio format (PCM = 1)
    uint16_t audioFormat = header[20] | (header[21] << 8);
    if (audioFormat != 1) {
        Serial.println("Non-PCM format not supported");
        return false;
    }

    // Check number of channels (1=mono, 2=stereo)
    uint16_t numChannels = header[22] | (header[23] << 8);
    if (numChannels != 1 && numChannels != 2) {
        Serial.printf("Channel count %d not supported (need 1 or 2)\n", numChannels);
        return false;
    }
    isMono = (numChannels == 1);

    // Check sample rate (44.1kHz)
    uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    if (sampleRate != 44100) {
        Serial.printf("Sample rate %d not supported (need 44100)\n", sampleRate);
        return false;
    }

    // Check bits per sample (16-bit)
    uint16_t bitsPerSample = header[34] | (header[35] << 8);
    if (bitsPerSample != 16) {
        Serial.printf("Bit depth %d not supported (need 16)\n", bitsPerSample);
        return false;
    }

    Serial.printf("WAV header validated: %s, 44.1kHz, 16-bit\n", isMono ? "Mono" : "Stereo");
    return true;
}
