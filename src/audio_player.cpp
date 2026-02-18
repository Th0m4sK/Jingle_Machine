#include "audio_player.h"
#include "pin_config.h"
#include <Preferences.h>
#include <nvs_flash.h>
#include <WiFi.h>
#include "wifi_credentials.h"
#include <math.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <TFT_eSPI.h>

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
static const unsigned long FADEIN_MS = 100;  // Fade in first 100ms of WAV to prevent click
static unsigned long fadeInStart = 0;  // When fade-in started
static bool inFadeIn = false;  // Flag for fade-in mode
static const unsigned long FADEOUT_MS = 100;  // Fade out last 100ms of WAV to prevent click
static unsigned long fadeOutStart = 0;  // When fade-out started
static bool inFadeOut = false;  // Flag for fade-out mode

// NEW: Static variables for BT scanning
static std::vector<AudioPlayer::BTDevice> scannedDevices;
static bool scanComplete = false;

// NEW: Static variables for test sound generation
static bool playingTestTone = false;
static int testToneRemaining = 0;
static float testTonePhase = 0.0;
static float testToneFreq = 1000.0;

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

bool AudioPlayer::begin(const char* deviceName, const char* deviceMac, bool clearPairing) {
    Serial.println("=== Bluetooth A2DP Initialization ===");
    Serial.printf("Name: %s  MAC: %s\n", deviceName, deviceMac ? deviceMac : "(none)");

    if (clearPairing) {
        clearBluetoothPairing();
    }

    a2dp_source.set_data_callback_in_frames(audioCallback);

    // Helper to parse and apply MAC-based reconnect
    auto tryMac = [&](const char* mac) -> bool {
        if (!mac || strlen(mac) != 17) return false;
        int vals[6];
        if (sscanf(mac, "%x:%x:%x:%x:%x:%x",
                   &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) return false;
        esp_bd_addr_t macAddr;
        for (int i = 0; i < 6; i++) macAddr[i] = (uint8_t)vals[i];
        Serial.printf("Connecting by MAC: %s\n", mac);
        a2dp_source.set_auto_reconnect(macAddr);
        a2dp_source.start("");
        return true;
    };

    // 1. Try MAC first (most reliable)
    if (tryMac(deviceMac)) {
        // MAC connect started
    }
    // 2. deviceName itself might be a MAC (legacy configs)
    else if (tryMac(deviceName)) {
        // MAC connect started
    }
    // 3. Fall back to name-based discovery
    else {
        Serial.printf("Connecting by name: %s\n", deviceName);
        a2dp_source.set_auto_reconnect(true);
        a2dp_source.start(deviceName);
    }

    Serial.println("Waiting for connection...");
    delay(1000); // Give time to establish connection

    if (a2dp_source.is_connected()) {
        Serial.println("Bluetooth connected successfully!");
    } else {
        Serial.println("Bluetooth not connected yet (will retry)");
    }

    return true;
}

void AudioPlayer::end() {
    Serial.println("[BT] Stopping A2DP source...");
    playing = false;
    if (currentFile) currentFile.close();
    a2dp_source.end(false);
    delay(300);
    Serial.println("[BT] A2DP stopped");
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

    // WiFi modem sleep is enabled permanently at startup for BT coexistence

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
    inFadeIn = true;  // Enable fade-in at start
    fadeInStart = millis();
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
    inFadeIn = false;
    fadeInStart = 0;
    inFadeOut = false;
    fadeOutStart = 0;

    // Clean up WAV file if active
    if (currentFile) {
        currentFile.close();
    }

    bytesRead = 0;

    // WiFi stays in modem sleep mode permanently (required for BT)
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
    // WiFi modem sleep stays enabled permanently for BT coexistence
    // No action needed - just clear the flag
    if (needsWiFiReconnect) {
        needsWiFiReconnect = false;
    }
}

int32_t AudioPlayer::audioCallback(Frame *data, int32_t frameCount) {
    // Minimal debug output to save memory
    static bool firstCall = true;
    if (firstCall) {
        Serial.println("[AUDIO] Callback started");
        firstCall = false;
    }

    // Check if we're playing test tone
    if (playingTestTone && testToneRemaining > 0) {
        for (int i = 0; i < frameCount; i++) {
            if (testToneRemaining <= 0) {
                data[i].channel1 = 0;
                data[i].channel2 = 0;
                continue;
            }

            // Generate sine wave
            float sample = sin(testTonePhase) * 16000.0;  // Amplitude
            testTonePhase += 2.0 * M_PI * testToneFreq / 44100.0;

            int16_t sampleValue = (int16_t)sample;
            data[i].channel1 = sampleValue;
            data[i].channel2 = sampleValue;

            testToneRemaining--;
        }
        return frameCount;
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

            // Apply fade-in at start of file to prevent click
            if (inFadeIn) {
                unsigned long fadeElapsed = millis() - fadeInStart;
                if (fadeElapsed >= FADEIN_MS) {
                    inFadeIn = false;  // Fade-in complete
                } else {
                    float fadeFactor = (float)fadeElapsed / (float)FADEIN_MS;
                    fadeFactor = max(0.0f, min(1.0f, fadeFactor));  // Clamp 0-1

                    left = (int16_t)((float)left * fadeFactor);
                    right = (int16_t)((float)right * fadeFactor);
                }
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

// ========== NEW: Bluetooth Scanning (Settings Mode only - GAP API) ==========

// Global TFT reference for debug output
extern TFT_eSPI tft;
static int deviceCount = 0;

// GAP callback for pure BT device discovery (no A2DP)
static void gap_scan_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Device discovered
            AudioPlayer::BTDevice device;
            device.name = "Unknown";
            device.rssi = 0;

            // Convert MAC to string first
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
            device.mac = String(macStr);

            // DEBUG: Show all properties
            Serial.printf("\n[BT] Device MAC: %s, Props: %d\n", macStr, param->disc_res.num_prop);

            // Parse device properties
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];

                Serial.printf("  Prop[%d] type=%d len=%d\n", i, prop->type, prop->len);

                if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                    device.name = String((char*)prop->val);
                    Serial.printf("  -> NAME: %s\n", device.name.c_str());
                } else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
                    device.rssi = *(int8_t*)prop->val;
                    Serial.printf("  -> RSSI: %d\n", device.rssi);
                } else if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
                    uint32_t cod = *(uint32_t*)prop->val;
                    Serial.printf("  -> COD: 0x%06X\n", cod);
                } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    Serial.printf("  -> EIR data (%d bytes)\n", prop->len);
                    // Try to parse EIR for device name
                    uint8_t *eir = (uint8_t*)prop->val;
                    uint8_t eir_len = 0;

                    // Parse EIR data looking for device name
                    for (int j = 0; j < prop->len; j += eir_len + 1) {
                        eir_len = eir[j];
                        if (eir_len == 0) break;

                        uint8_t eir_type = eir[j + 1];
                        Serial.printf("    EIR[%d] len=%d type=0x%02X\n", j, eir_len, eir_type);

                        // 0x08 = Shortened Local Name, 0x09 = Complete Local Name
                        if (eir_type == 0x08 || eir_type == 0x09) {
                            char name[256];
                            int name_len = eir_len - 1;  // -1 for type byte
                            if (name_len > 0 && name_len < 256) {
                                memcpy(name, &eir[j + 2], name_len);
                                name[name_len] = '\0';
                                device.name = String(name);
                                Serial.printf("    -> Found name in EIR: %s\n", name);
                            }
                        }
                    }
                }
            }

            // Check for duplicates by MAC address
            bool isDuplicate = false;
            for (size_t i = 0; i < scannedDevices.size(); i++) {
                if (scannedDevices[i].mac == device.mac) {
                    isDuplicate = true;
                    // Update existing entry with better info if available
                    if (device.name != "Unknown" && scannedDevices[i].name == "Unknown") {
                        scannedDevices[i].name = device.name;
                        Serial.printf("[BT SCAN] Updated name for %s\n", macStr);
                    }
                    if (device.rssi != 0) {
                        scannedDevices[i].rssi = device.rssi;
                    }
                    break;
                }
            }

            if (!isDuplicate) {
                scannedDevices.push_back(device);
                deviceCount++;
                Serial.printf("[BT SCAN] NEW device saved: %s (%s) RSSI: %d\n\n", device.name.c_str(), macStr, device.rssi);
                // DON'T update TFT here - will crash (different task context)
                // TFT will be updated from main loop instead
            } else {
                Serial.printf("[BT SCAN] Duplicate MAC %s (ignored)\n\n", macStr);
            }

            break;
        }

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                Serial.println("[BT SCAN] Discovery stopped");
                scanComplete = true;
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                Serial.println("[BT SCAN] Discovery started");
            }
            break;
        }

        default:
            break;
    }
}

// Unused A2DP callback (kept for compatibility)
bool AudioPlayer::scanCallback(const char* ssid, esp_bd_addr_t address, int rssi) {
    return false;
}

// Scan for Bluetooth devices using pure GAP API (Settings Mode only, no A2DP)
std::vector<AudioPlayer::BTDevice> AudioPlayer::scanForDevices(int timeoutSeconds) {
    Serial.println("=== Starting BT Device Scan (GAP only) ===");

    scannedDevices.clear();
    scanComplete = false;
    deviceCount = 0;

    // Properly shut down A2DP library first (stops its FreeRTOS tasks)
    // Without this, the raw BT stack reset below triggers an assert crash
    Serial.println("[BT SCAN] Stopping A2DP source...");
    a2dp_source.end(false);
    delay(300);

    // FULL BT STACK RESET - clean slate
    Serial.println("[BT SCAN] Resetting BT stack...");

    // Disable and deinit Bluedroid if it exists
    esp_bluedroid_disable();
    esp_bluedroid_deinit();

    // Stop BT controller if running
    if (btStarted()) {
        btStop();
    }

    delay(500);  // Let everything settle

    // Start fresh
    Serial.println("[BT SCAN] Starting BT controller...");
    if (!btStart()) {
        Serial.println("[BT SCAN] BT controller start failed!");
        return scannedDevices;
    }

    // Initialize Bluedroid stack
    Serial.println("[BT SCAN] Initializing Bluedroid...");
    esp_err_t ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        Serial.printf("[BT SCAN] Bluedroid init failed: %d\n", ret);
        return scannedDevices;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        Serial.printf("[BT SCAN] Bluedroid enable failed: %d\n", ret);
        return scannedDevices;
    }

    // Register GAP callback
    Serial.println("[BT SCAN] Registering GAP callback...");
    ret = esp_bt_gap_register_callback(gap_scan_callback);
    if (ret != ESP_OK) {
        Serial.printf("[BT SCAN] GAP register failed: %d\n", ret);
        return scannedDevices;
    }

    // Set scan mode
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // Start discovery
    Serial.println("[BT SCAN] Starting discovery...");
    ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    if (ret != ESP_OK) {
        Serial.printf("[BT SCAN] Start discovery failed: %d\n", ret);
        return scannedDevices;
    }

    // Wait for scan to complete with progress indicator
    unsigned long startTime = millis();
    unsigned long lastUpdate = 0;
    int lastDeviceCount = 0;
    Serial.printf("[BT SCAN] Scanning for %d seconds...\n", timeoutSeconds);

    while ((millis() - startTime) < (timeoutSeconds * 1000) && !scanComplete) {
        // Update TFT every 500ms (from main loop, not callback)
        if (millis() - lastUpdate > 500) {
            lastUpdate = millis();

            // Show countdown
            int remaining = timeoutSeconds - ((millis() - startTime) / 1000);
            tft.fillRect(200, 60, 120, 20, TFT_BLACK);
            tft.setTextColor(TFT_YELLOW);
            tft.setTextDatum(TL_DATUM);
            tft.drawString(String(remaining) + "s", 200, 60, 2);

            // Update device count and list if changed
            if (deviceCount != lastDeviceCount) {
                lastDeviceCount = deviceCount;

                tft.fillRect(0, 85, 320, 20, TFT_BLACK);
                tft.setTextColor(TFT_GREEN);
                tft.setTextDatum(TL_DATUM);
                tft.drawString("Unique: " + String(deviceCount), 10, 85, 2);

                // Redraw device list
                tft.fillRect(0, 110, 320, 130, TFT_BLACK);
                int start = max(0, (int)scannedDevices.size() - 9);
                int y = 110;
                for (size_t i = start; i < scannedDevices.size(); i++) {
                    tft.setTextColor(TFT_CYAN);
                    tft.setTextDatum(TL_DATUM);
                    String displayText = scannedDevices[i].name.substring(0, 12);
                    String macShort = scannedDevices[i].mac.substring(12);
                    tft.drawString(displayText + " " + macShort, 10, y, 1);
                    y += 13;
                }
            }
        }

        delay(100);
        yield();
    }

    // Stop discovery if still running
    if (!scanComplete) {
        Serial.println("[BT SCAN] Stopping discovery...");
        esp_bt_gap_cancel_discovery();
    }

    Serial.printf("[BT SCAN] Scan complete: %d devices found\n", scannedDevices.size());

    // Clean up - disable Bluedroid to free resources
    Serial.println("[BT SCAN] Cleaning up Bluedroid...");
    esp_bluedroid_disable();
    esp_bluedroid_deinit();

    return scannedDevices;
}

// Pair - just saves device name, actual pairing happens in Normal Mode
bool AudioPlayer::pairDevice(const String& deviceName, int timeoutSeconds) {
    Serial.println("=== Saving Device Name (no actual pairing in Settings Mode) ===");
    Serial.print("Device: ");
    Serial.println(deviceName);

    // In Settings Mode, we just save the name
    // Actual connection happens in Normal Mode via A2DP
    return true;
}

// Test sound - not available in Settings Mode (no A2DP initialized)
bool AudioPlayer::playTestSound() {
    Serial.println("[TEST SOUND] Not available in Settings Mode");
    return false;
}
