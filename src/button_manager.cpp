#include "button_manager.h"
#include "pin_config.h"

ButtonManager::ButtonManager(TFT_eSPI* tft, XPT2046_Touchscreen* touch)
    : _tft(tft), _touch(touch), globalRotation(0), globalBorderColor(TFT_WHITE),
      globalBorderThickness(3), simulatedTouchEnabled(false) {
}

void ButtonManager::setSimulatedTouch(bool enabled) {
    simulatedTouchEnabled = enabled;
    // No serial output to avoid audio interference
}

void ButtonManager::loadConfig(const JsonDocument& config) {
    // Load global rotation setting
    globalRotation = config["rotation"].as<int>();
    if (globalRotation != 0 && globalRotation != 90 && globalRotation != 180 && globalRotation != 270) {
        globalRotation = 0;  // Default to 0 if invalid
    }

    // Load global border color (default: white)
    String borderColorStr = config["borderColor"].as<String>();
    if (borderColorStr.length() > 0) {
        globalBorderColor = colorStringToRGB565(borderColorStr);
    } else {
        globalBorderColor = TFT_WHITE;  // Default to white
    }

    // Load global border thickness (default: 3)
    globalBorderThickness = config["borderThickness"].as<int>();
    if (globalBorderThickness < 1 || globalBorderThickness > 5) {
        globalBorderThickness = 3;  // Default to 3 pixels
    }

    JsonVariantConst buttonArray = config["buttons"];

    int idx = 0;
    for (JsonVariantConst btnVar : buttonArray.as<JsonArrayConst>()) {
        if (idx >= 8) break;

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
    const int margin = 5;
    const int buttonWidth = (SCREEN_WIDTH - 5 * margin) / 4;
    const int buttonHeight = (SCREEN_HEIGHT - 3 * margin) / 2;

    for (int i = 0; i < 8; i++) {
        int row = i / 4;
        int col = i % 4;

        // Calculate top-left corner
        int topLeftX = margin + col * (buttonWidth + margin);
        int topLeftY = margin + row * (buttonHeight + margin);

        // Store CENTER position (x, y = center of button)
        buttons[i].x = topLeftX + buttonWidth / 2;
        buttons[i].y = topLeftY + buttonHeight / 2;
        buttons[i].w = buttonWidth;
        buttons[i].h = buttonHeight;
    }
}

void ButtonManager::draw() {
    _tft->fillScreen(TFT_BLACK);
    for (int i = 0; i < 8; i++) {
        // Only draw buttons that have a valid sound file assigned
        if (buttons[i].filepath.length() > 0 && buttons[i].filepath != "") {
            drawButton(i, false);
        }
    }
}

void ButtonManager::drawButton(int id, bool highlighted) {
    Button& btn = buttons[id];

    uint16_t color = highlighted ? TFT_WHITE : btn.color;
    uint16_t textColor = highlighted ? btn.color : btn.textColor;

    // Calculate top-left from center for drawing background
    int topLeftX = btn.x - btn.w / 2;
    int topLeftY = btn.y - btn.h / 2;

    // Draw button background
    _tft->fillRoundRect(topLeftX, topLeftY, btn.w, btn.h, 5, color);

    // Draw border with global border color and thickness
    uint16_t borderColor = highlighted ? btn.color : globalBorderColor;
    for (int i = 0; i < globalBorderThickness; i++) {
        int cornerRadius = 5 - i;
        if (cornerRadius < 1) cornerRadius = 1;
        _tft->drawRoundRect(topLeftX + i, topLeftY + i, btn.w - (i * 2), btn.h - (i * 2), cornerRadius, borderColor);
    }

    // Draw text at center with rotation
    drawButtonText(id, btn.label, textColor, color);
}

void ButtonManager::drawButtonText(int id, const String& text, uint16_t textColor, uint16_t bgColor) {
    Button& btn = buttons[id];

    // Button center in current rotation coordinates
    int centerX = btn.x;
    int centerY = btn.y;

    // For 0° rotation (no rotation), draw directly
    if (globalRotation == 0) {
        _tft->setTextColor(textColor);
        _tft->setTextDatum(MC_DATUM);
        _tft->setTextSize(1);
        _tft->drawString(text, centerX, centerY, 2);
        return;
    }

    // Save current rotation
    uint8_t savedRotation = _tft->getRotation();

    // Calculate target rotation (0=portrait, 1=landscape, 2=portrait-inv, 3=landscape-inv)
    // Current is rotation 1 (landscape 320x240)
    // globalRotation is in degrees: 0, 90, 180, 270
    uint8_t targetRotation = (savedRotation + (globalRotation / 90)) % 4;

    // Transform coordinates from current rotation to target rotation
    int transformedX, transformedY;

    if (globalRotation == 90) {
        // Rotation 1 → 2 (landscape → portrait-inverted): 90° clockwise
        // (x, y) in 320x240 → (y, 319-x) in 240x320
        transformedX = centerY;
        transformedY = 319 - centerX;
    } else if (globalRotation == 180) {
        // Rotation 1 → 3 (landscape → landscape-inverted): 180°
        // (x, y) in 320x240 → (319-x, 239-y) in 320x240
        transformedX = 319 - centerX;
        transformedY = 239 - centerY;
    } else { // globalRotation == 270
        // Rotation 1 → 0 (landscape → portrait): 270° clockwise (90° counter-clockwise)
        // (x, y) in 320x240 → (239-y, x) in 240x320
        transformedX = 239 - centerY;
        transformedY = centerX;
    }

    // Set new rotation for text
    _tft->setRotation(targetRotation);

    // Draw text at transformed coordinates
    _tft->setTextColor(textColor);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextSize(1);
    _tft->drawString(text, transformedX, transformedY, 2);

    // Restore original rotation
    _tft->setRotation(savedRotation);
}

int ButtonManager::checkTouch() {
    // Use simulated touch if enabled, otherwise use hardware
    if (simulatedTouchEnabled) {
        return checkSimulatedTouch();
    }

    // Hardware touch detection
    if (!_touch->tirqTouched() || !_touch->touched()) {
        return -1;
    }

    TS_Point p = _touch->getPoint();

    if (p.z < 200) {  // Pressure threshold - no touch if below
        return -1;
    }

    // Map touch coordinates to screen coordinates (landscape mode)
    // CYD typical calibration (may need adjustment)
    int x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
    int y = map(p.y, 240, 3800, 0, SCREEN_HEIGHT);

    // Check which button was pressed
    for (int i = 0; i < 8; i++) {
        Button& btn = buttons[i];

        // Calculate bounds from center
        int left = btn.x - btn.w / 2;
        int right = btn.x + btn.w / 2;
        int top = btn.y - btn.h / 2;
        int bottom = btn.y + btn.h / 2;

        if (x >= left && x <= right && y >= top && y <= bottom) {
            return i;
        }
    }

    return -1;
}

int ButtonManager::checkSimulatedTouch() {
    // Generate random touch events for testing (without real hardware)
    // Simulate occasional random touches - NO SERIAL OUTPUT to avoid audio interference
    static unsigned long lastSimTouch = 0;
    static int nextTouchDelay = random(5000, 10000);  // Slower: 5-10 seconds between touches

    if (millis() - lastSimTouch < nextTouchDelay) {
        return -1;  // No touch yet
    }

    // Time for a simulated touch!
    lastSimTouch = millis();
    nextTouchDelay = random(5000, 10000);  // Next touch in 5-10 seconds

    // Generate random screen coordinates
    int x = random(0, SCREEN_WIDTH);
    int y = random(0, SCREEN_HEIGHT);

    // Check which button was pressed (if any)
    for (int i = 0; i < 8; i++) {
        Button& btn = buttons[i];
        // Only check visible buttons (those with files)
        if (btn.filepath.length() == 0) continue;

        // Calculate bounds from center
        int left = btn.x - btn.w / 2;
        int right = btn.x + btn.w / 2;
        int top = btn.y - btn.h / 2;
        int bottom = btn.y + btn.h / 2;

        if (x >= left && x <= right && y >= top && y <= bottom) {
            return i;  // Button hit - no debug output to avoid blocking audio
        }
    }

    return -1;  // No button was hit
}

void ButtonManager::highlightButton(int id) {
    if (id < 0 || id >= 8) return;

    drawButton(id, true);
    delay(200);
    drawButton(id, false);
}

String ButtonManager::getButtonFile(int id) {
    if (id < 0 || id >= 8) return "";
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
