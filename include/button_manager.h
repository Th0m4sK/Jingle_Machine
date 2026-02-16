#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <ArduinoJson.h>

struct Button {
    int id;
    int x, y, w, h;
    String label;
    String filepath;
    uint16_t color;
    uint16_t textColor;
};

class ButtonManager {
public:
    ButtonManager(TFT_eSPI* tft, XPT2046_Touchscreen* touch);

    void loadConfig(const JsonDocument& config);
    void draw();
    int checkTouch();
    void setSimulatedTouch(bool enabled);  // Enable/disable simulated touch for testing
    void highlightButton(int id);
    String getButtonFile(int id);

private:
    TFT_eSPI* _tft;
    XPT2046_Touchscreen* _touch;
    Button buttons[8];
    int globalRotation;  // Global text rotation for all buttons: 0, 90, 180, 270
    uint16_t globalBorderColor;  // Global border color for all buttons (default: white)
    int globalBorderThickness;  // Global border thickness in pixels (default: 3)
    bool simulatedTouchEnabled;  // Use simulated touch instead of hardware

    void drawButton(int id, bool highlighted = false);
    void drawButtonText(int id, const String& text, uint16_t textColor, uint16_t bgColor);
    uint16_t colorStringToRGB565(const String& colorHex);
    void calculateButtonLayout();
    int checkSimulatedTouch();  // Generate random touch events for testing
};

#endif
