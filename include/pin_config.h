#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// CYD (Cheap Yellow Display) Pin Configuration
// TFT Display Pins (ILI9341)
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_BL   21

// Touchscreen Pins (XPT2046) - Separate SPI bus!
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25

// SD Card Pins (shared SPI with TFT)
#define SD_CS 5
#define SD_MISO TFT_MISO
#define SD_MOSI TFT_MOSI
#define SD_SCLK TFT_SCLK

// Display Settings
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// Audio Settings
#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define CHANNELS 2

#endif
