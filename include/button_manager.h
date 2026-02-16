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
    // Layout constants
    static const int BUTTON_GRID_COLS = 4;
    static const int BUTTON_GRID_ROWS = 2;
    static const int BUTTON_MARGIN = 5;
    static const int BUTTON_CORNER_RADIUS = 5;
    static const int MAX_BUTTONS = 8;

    // Touch calibration constants
    static const int TOUCH_PRESSURE_THRESHOLD = 200;
    static const int TOUCH_X_MIN = 200;
    static const int TOUCH_X_MAX = 3700;
    static const int TOUCH_Y_MIN = 240;
    static const int TOUCH_Y_MAX = 3800;

    // Text constants
    static const int TEXT_SIZE = 1;
    static const int TEXT_FONT = 2;

    // Simulation constants
    static const int SIM_TOUCH_MIN_DELAY = 5000;
    static const int SIM_TOUCH_MAX_DELAY = 10000;

    // Default config values
    static const int DEFAULT_ROTATION = 0;
    static const uint16_t DEFAULT_BORDER_COLOR = TFT_WHITE;
    static const int DEFAULT_BORDER_THICKNESS = 3;

    TFT_eSPI* _tft;
    XPT2046_Touchscreen* _touch;
    Button buttons[8];
    int globalRotation;  // Global text rotation for all buttons: 0, 90, 180, 270
    uint16_t globalBorderColor;  // Global border color for all buttons (default: white)
    int globalBorderThickness;  // Global border thickness in pixels (default: 3)
    bool simulatedTouchEnabled;  // Use simulated touch instead of hardware

    // Helper structures
    struct Point {
        int x, y;
    };

    struct ButtonBounds {
        int left, right, top, bottom;
    };

    struct DrawColors {
        uint16_t fill;
        uint16_t border;
        uint16_t text;
    };

    // Coordinate helper functions (inline for performance)
    inline Point centerToTopLeft(const Button& btn) const {
        return {btn.x - btn.w / 2, btn.y - btn.h / 2};
    }

    inline ButtonBounds getButtonBounds(const Button& btn) const {
        return {
            btn.x - btn.w / 2,  // left
            btn.x + btn.w / 2,  // right
            btn.y - btn.h / 2,  // top
            btn.y + btn.h / 2   // bottom
        };
    }

    inline bool isPointInBounds(int x, int y, const ButtonBounds& bounds) const {
        return x >= bounds.left && x <= bounds.right &&
               y >= bounds.top && y <= bounds.bottom;
    }

    // Rotation helper
    Point transformForRotation(int x, int y, int rotationDegrees) const;

    // Validation helpers (inline for performance)
    inline bool isValidButtonId(int id) const {
        return id >= 0 && id < MAX_BUTTONS;
    }

    inline bool isValidRotation(int rotation) const {
        return rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270;
    }

    inline bool isValidBorderThickness(int thickness) const {
        return thickness >= 1 && thickness <= 5;
    }

    DrawColors getDrawColors(const Button& btn, bool highlighted) const;
    void drawButton(int id, bool highlighted = false);
    void drawButtonBorder(const Point& topLeft, int width, int height,
                         uint16_t borderColor, int thickness);
    void renderText(const String& text, int x, int y, uint16_t color);
    void drawButtonText(int id, const String& text, uint16_t textColor, uint16_t bgColor);
    uint16_t colorStringToRGB565(const String& colorHex);
    void calculateButtonLayout();
    int checkSimulatedTouch();  // Generate random touch events for testing
};

#endif
