#include "button_manager.h"
#include "pin_config.h"

ButtonManager::ButtonManager(TFT_eSPI* tft, XPT2046_Touchscreen* touch)
    : _tft(tft), _touch(touch), globalRotation(DEFAULT_ROTATION),
      globalBorderColor(DEFAULT_BORDER_COLOR), globalBorderThickness(DEFAULT_BORDER_THICKNESS),
      simulatedTouchEnabled(false) {
}

void ButtonManager::setSimulatedTouch(bool enabled) {
    simulatedTouchEnabled = enabled;
    // No serial output to avoid audio interference
}

ButtonManager::Point ButtonManager::transformForRotation(int x, int y, int rotationDegrees) const {
    switch(rotationDegrees) {
        case 90:
            return {y, (SCREEN_WIDTH - 1) - x};
        case 180:
            return {(SCREEN_WIDTH - 1) - x, (SCREEN_HEIGHT - 1) - y};
        case 270:
            return {(SCREEN_HEIGHT - 1) - y, x};
        default:
            return {x, y};
    }
}

void ButtonManager::loadConfig(const JsonDocument& config) {
    // Load and validate rotation
    globalRotation = config["rotation"].as<int>();
    if (!isValidRotation(globalRotation)) {
        globalRotation = DEFAULT_ROTATION;
    }

    // Load and validate border color
    String borderColorStr = config["borderColor"].as<String>();
    if (borderColorStr.length() > 0) {
        globalBorderColor = colorStringToRGB565(borderColorStr);
    } else {
        globalBorderColor = DEFAULT_BORDER_COLOR;
    }

    // Load and validate border thickness
    globalBorderThickness = config["borderThickness"].as<int>();
    if (!isValidBorderThickness(globalBorderThickness)) {
        globalBorderThickness = DEFAULT_BORDER_THICKNESS;
    }

    // Load button configurations
    JsonVariantConst buttonArray = config["buttons"];
    if (!buttonArray.is<JsonArrayConst>()) {
        return;  // No buttons configured
    }
    int idx = 0;

    for (JsonVariantConst btnVar : buttonArray.as<JsonArrayConst>()) {
        if (idx >= MAX_BUTTONS) break;

        JsonObjectConst btn = btnVar.as<JsonObjectConst>();
        buttons[idx].id = btn["id"].as<int>();
        buttons[idx].label = btn["label"].as<const char*>();
        buttons[idx].filepath = btn["file"].as<const char*>();
        buttons[idx].color = colorStringToRGB565(String(btn["color"].as<const char*>()));
        buttons[idx].textColor = colorStringToRGB565(String(btn["textColor"].as<const char*>()));
        idx++;
    }

    calculateButtonLayout();
}

void ButtonManager::calculateButtonLayout() {
    const int buttonWidth = (SCREEN_WIDTH - (BUTTON_GRID_COLS + 1) * BUTTON_MARGIN) / BUTTON_GRID_COLS;
    const int buttonHeight = (SCREEN_HEIGHT - (BUTTON_GRID_ROWS + 1) * BUTTON_MARGIN) / BUTTON_GRID_ROWS;

    for (int i = 0; i < MAX_BUTTONS; i++) {
        int row = i / BUTTON_GRID_COLS;
        int col = i % BUTTON_GRID_COLS;

        // Calculate top-left corner
        int topLeftX = BUTTON_MARGIN + col * (buttonWidth + BUTTON_MARGIN);
        int topLeftY = BUTTON_MARGIN + row * (buttonHeight + BUTTON_MARGIN);

        // Store CENTER position (x, y = center of button)
        buttons[i].x = topLeftX + buttonWidth / 2;
        buttons[i].y = topLeftY + buttonHeight / 2;
        buttons[i].w = buttonWidth;
        buttons[i].h = buttonHeight;
    }
}

void ButtonManager::draw() {
    _tft->fillScreen(TFT_BLACK);
    for (int i = 0; i < MAX_BUTTONS; i++) {
        // Only draw buttons that have a valid sound file assigned
        if (buttons[i].filepath.length() > 0 && buttons[i].filepath != "") {
            drawButton(i, false);
        }
    }
}

ButtonManager::DrawColors ButtonManager::getDrawColors(const Button& btn, bool highlighted) const {
    if (highlighted) {
        return {
            TFT_WHITE,      // fill (inverted)
            btn.color,      // border (button color)
            btn.color       // text (button color)
        };
    } else {
        return {
            btn.color,           // fill (button color)
            globalBorderColor,   // border (global setting)
            btn.textColor        // text (button text color)
        };
    }
}

void ButtonManager::drawButtonBorder(const Point& topLeft, int width, int height,
                                    uint16_t borderColor, int thickness) {
    for (int i = 0; i < thickness; i++) {
        int cornerRadius = max(1, BUTTON_CORNER_RADIUS - i);
        _tft->drawRoundRect(
            topLeft.x + i,
            topLeft.y + i,
            width - (i * 2),
            height - (i * 2),
            cornerRadius,
            borderColor
        );
    }
}

void ButtonManager::drawButton(int id, bool highlighted) {
    Button& btn = buttons[id];
    DrawColors colors = getDrawColors(btn, highlighted);
    Point topLeft = centerToTopLeft(btn);

    // Draw button background
    _tft->fillRoundRect(topLeft.x, topLeft.y, btn.w, btn.h,
                       BUTTON_CORNER_RADIUS, colors.fill);

    // Draw border
    drawButtonBorder(topLeft, btn.w, btn.h, colors.border, globalBorderThickness);

    // Draw text
    drawButtonText(id, btn.label, colors.text, colors.fill);
}

void ButtonManager::renderText(const String& text, int x, int y, uint16_t color) {
    _tft->setTextColor(color);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextSize(TEXT_SIZE);
    _tft->drawString(text, x, y, TEXT_FONT);
}

void ButtonManager::drawButtonText(int id, const String& text, uint16_t textColor, uint16_t bgColor) {
    Button& btn = buttons[id];
    int centerX = btn.x;
    int centerY = btn.y;

    // No rotation - draw directly
    if (globalRotation == 0) {
        renderText(text, centerX, centerY, textColor);
        return;
    }

    // Rotated text
    uint8_t savedRotation = _tft->getRotation();
    uint8_t targetRotation = (savedRotation + (globalRotation / 90)) % 4;

    // Transform coordinates for the rotated display
    Point transformed = transformForRotation(centerX, centerY, globalRotation);

    // Draw text in rotated coordinate system
    _tft->setRotation(targetRotation);
    renderText(text, transformed.x, transformed.y, textColor);
    _tft->setRotation(savedRotation);
}

int ButtonManager::checkTouch() {
    // Use simulated touch if enabled, otherwise use hardware
    if (simulatedTouchEnabled) {
        return checkSimulatedTouch();
    }

    // Hardware touch detection (tirqTouched() not used – caused false negatives)
    if (!_touch->touched()) {
        return -1;
    }

    TS_Point p = _touch->getPoint();

    if (p.z < TOUCH_PRESSURE_THRESHOLD) {  // Pressure threshold - no touch if below
        return -1;
    }

    // Map touch coordinates to screen coordinates (landscape mode)
    // CYD typical calibration (may need adjustment)
    int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH);
    int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT);

    // Check which button was pressed
    for (int i = 0; i < MAX_BUTTONS; i++) {
        ButtonBounds bounds = getButtonBounds(buttons[i]);
        if (isPointInBounds(x, y, bounds)) {
            return i;
        }
    }

    return -1;
}

int ButtonManager::checkSimulatedTouch() {
    // Generate random touch events for testing (without real hardware)
    // Simulate occasional random touches - NO SERIAL OUTPUT to avoid audio interference
    static unsigned long lastSimTouch = 0;
    static int nextTouchDelay = random(SIM_TOUCH_MIN_DELAY, SIM_TOUCH_MAX_DELAY);

    if (millis() - lastSimTouch < nextTouchDelay) {
        return -1;  // No touch yet
    }

    // Time for a simulated touch!
    lastSimTouch = millis();
    nextTouchDelay = random(SIM_TOUCH_MIN_DELAY, SIM_TOUCH_MAX_DELAY);

    // Generate random screen coordinates
    int x = random(0, SCREEN_WIDTH);
    int y = random(0, SCREEN_HEIGHT);

    // Check which button was pressed (if any)
    for (int i = 0; i < MAX_BUTTONS; i++) {
        // Only check visible buttons (those with files)
        if (buttons[i].filepath.length() == 0) continue;

        ButtonBounds bounds = getButtonBounds(buttons[i]);
        if (isPointInBounds(x, y, bounds)) {
            return i;  // Button hit - no debug output to avoid blocking audio
        }
    }

    return -1;  // No button was hit
}

void ButtonManager::highlightButton(int id) {
    if (!isValidButtonId(id)) return;

    drawButton(id, true);
    delay(200);
    drawButton(id, false);
}

String ButtonManager::getButtonFile(int id) {
    if (!isValidButtonId(id)) return "";
    return buttons[id].filepath;
}

uint16_t ButtonManager::colorStringToRGB565(const String& colorHex) {
    // Parse hex color string like "#FF5733"
    if (colorHex.length() != 7 || colorHex[0] != '#') {
        return TFT_WHITE; // Default fallback
    }

    uint32_t color = strtol(colorHex.substring(1).c_str(), NULL, 16);

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Convert 8-8-8 RGB to 5-6-5 RGB565
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
