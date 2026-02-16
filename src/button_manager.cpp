#include "button_manager.h"
#include "pin_config.h"

ButtonManager::ButtonManager(TFT_eSPI* tft, XPT2046_Touchscreen* touch)
    : _tft(tft), _touch(touch), globalRotation(0), simulatedTouchEnabled(false) {
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

        buttons[i].x = margin + col * (buttonWidth + margin);
        buttons[i].y = margin + row * (buttonHeight + margin);
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

    // Draw button background
    _tft->fillRoundRect(btn.x, btn.y, btn.w, btn.h, 5, color);

    // Draw border
    _tft->drawRoundRect(btn.x, btn.y, btn.w, btn.h, 5, textColor);

    // Draw label with rotation - ALWAYS use the same method for consistency
    drawButtonText(id, btn.label, textColor, color);
}

void ButtonManager::drawButtonText(int id, const String& text, uint16_t textColor, uint16_t bgColor) {
    // Simple, reliable text drawing - no rotation, no sprites, no memory issues
    int cx = buttons[id].x + buttons[id].w / 2;
    int cy = buttons[id].y + buttons[id].h / 2;

    _tft->setTextColor(textColor);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextSize(1);
    _tft->drawString(text, cx, cy, 2);
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
        if (x >= btn.x && x <= btn.x + btn.w &&
            y >= btn.y && y <= btn.y + btn.h) {
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

        if (x >= btn.x && x <= btn.x + btn.w &&
            y >= btn.y && y <= btn.y + btn.h) {
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
