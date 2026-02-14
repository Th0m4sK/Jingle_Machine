// User_Setup.h for TFT_eSPI Library
// CYD (Cheap Yellow Display) - ILI9341 2.8" 320x240

// Driver Selection
#define ILI9341_2_DRIVER  // ILI9341 alternative driver

// Display Size
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Pin Configuration for CYD ESP32
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1  // Not used (connected to ESP32 EN)
#define TFT_BL   21  // Backlight control

// Touch Configuration (handled by XPT2046 library)
#define TOUCH_CS 33

// Font Loading
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT  // Enable anti-aliasing for fonts

// SPI Frequency
#define SPI_FREQUENCY       40000000  // 40MHz for display
#define SPI_READ_FREQUENCY  20000000  // 20MHz for reading
#define SPI_TOUCH_FREQUENCY  2500000  // 2.5MHz for touch

// Other Options
#define USE_HSPI_PORT  // Use HSPI instead of VSPI (ESP32 default)

// ILI9341 Commands
#define TFT_NOP        0x00
#define TFT_SWRESET    0x01
#define TFT_CASET      0x2A
#define TFT_PASET      0x2B
#define TFT_RAMWR      0x2C
#define TFT_RAMRD      0x2E
#define TFT_MADCTL     0x36
#define TFT_PIXFMT     0x3A
#define TFT_INVOFF     0x20
#define TFT_INVON      0x21
#define TFT_DISPOFF    0x28
#define TFT_DISPON     0x29
