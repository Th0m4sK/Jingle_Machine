# Quick Start Guide

## Prerequisites

- PlatformIO installed (VS Code extension recommended)
- ESP32 with CYD (Cheap Yellow Display)
- MicroSD card (FAT32 formatted)
- Bluetooth speaker

## 5-Minute Setup

### 1. Build and Upload

```bash
# Build the project
pio run

# Upload firmware to ESP32
pio run --target upload

# Upload web interface files to LittleFS
pio run --target uploadfs
```

### 2. Prepare SD Card

1. Format SD card as FAT32
2. Create folder: `/jingles/`
3. Add WAV files (44.1kHz, 16-bit, stereo):
   - `sound1.wav`
   - `sound2.wav`
   - etc.

### 3. First Boot

1. Insert SD card into CYD
2. Power on the device
3. Display shows 8 default buttons

### 4. Configure via Web Interface

**Connect to WiFi:**
- SSID: `jinglebox`
- Password: `jingle1234`

**Open Browser:**
- URL: `http://192.168.4.1`

**Enter Settings Mode:**
- Click "Enter Settings Mode" button

**Configure:**
1. Set Bluetooth speaker name (e.g., "JBL Flip 5")
2. Customize button labels and colors
3. Assign WAV files to buttons
4. Click "Save Configuration"
5. Click "Exit Settings Mode"

### 5. Use Jingle Machine

1. Power on Bluetooth speaker
2. Speaker will auto-pair with ESP32
3. Touch any button to play the assigned jingle
4. Audio streams via Bluetooth

## Common Issues

**"SD CARD ERROR"**
- Check SD card is inserted
- Verify FAT32 format

**"File not found"**
- Check file exists in `/jingles/` folder
- Verify filename in settings matches actual file

**"BT: DISC"** (Bluetooth disconnected)
- Turn on Bluetooth speaker
- Ensure correct speaker name in settings
- Device will auto-reconnect

## Converting Audio Files

Use FFmpeg to convert any audio to the correct format:

```bash
ffmpeg -i input.mp3 -ar 44100 -ac 2 -sample_fmt s16 output.wav
```

Or use Audacity:
1. Open audio file
2. Set project rate to 44100 Hz
3. Convert to stereo (Tracks → Mix → Mix Stereo to Mono → duplicate)
4. Export as WAV (PCM signed 16-bit)

## Serial Monitor (Debugging)

```bash
pio device monitor
```

Monitor output shows:
- Boot mode (Normal/Settings)
- SD card status
- Bluetooth connection status
- Button presses
- File playback status

## Next Steps

- Upload custom WAV files via web interface
- Customize button colors and labels
- Update firmware via OTA (`http://192.168.4.1/update`)

Enjoy your Jingle Machine!
