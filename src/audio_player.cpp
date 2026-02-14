#include "audio_player.h"
#include "pin_config.h"

// Static members
File AudioPlayer::currentFile;
bool AudioPlayer::playing = false;
uint32_t AudioPlayer::fileSize = 0;
uint32_t AudioPlayer::bytesRead = 0;

AudioPlayer::AudioPlayer() {
}

bool AudioPlayer::begin(const char* deviceName) {
    a2dp_source.set_auto_reconnect(true);
    a2dp_source.start(deviceName, audioCallback);
    delay(1000); // Give time to establish connection
    return true;
}

bool AudioPlayer::playFile(const String& filepath) {
    if (playing) {
        stop();
    }

    currentFile = SD.open(filepath);
    if (!currentFile) {
        Serial.println("Failed to open file: " + filepath);
        return false;
    }

    if (!validateWAVHeader(currentFile)) {
        Serial.println("Invalid WAV file format");
        currentFile.close();
        return false;
    }

    fileSize = currentFile.size();
    bytesRead = 44; // Skip WAV header (already read by validation)
    playing = true;

    Serial.println("Playing: " + filepath);
    return true;
}

void AudioPlayer::stop() {
    playing = false;
    if (currentFile) {
        currentFile.close();
    }
    bytesRead = 0;
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

int32_t AudioPlayer::audioCallback(Frame *data, int32_t frameCount) {
    if (!playing || !currentFile || !currentFile.available()) {
        if (playing && currentFile && !currentFile.available()) {
            // End of file reached
            playing = false;
            currentFile.close();
        }
        // Return silence
        for (int i = 0; i < frameCount; i++) {
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
        return frameCount;
    }

    int32_t framesRead = 0;
    for (int i = 0; i < frameCount; i++) {
        if (currentFile.available() >= 4) {
            // Read 16-bit stereo sample (4 bytes total)
            int16_t left = currentFile.read() | (currentFile.read() << 8);
            int16_t right = currentFile.read() | (currentFile.read() << 8);

            data[i].channel1 = left;
            data[i].channel2 = right;
            framesRead++;
            bytesRead += 4;
        } else {
            // End of file
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
    }

    return framesRead;
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

    Serial.println("WAV header validated successfully");
    return true;
}
