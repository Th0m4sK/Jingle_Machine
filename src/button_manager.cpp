#include "button_manager.h"
#include "pin_config.h"

ButtonManager::ButtonManager(TFT_eSPI* tft, XPT2046_Touchscreen* touch)
    : _tft(tft), _touch(touch) {
}

void ButtonManager::loadConfig(const JsonDocument& config) {
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
        drawButton(i, false);
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

    // Draw label (centered)
    _tft->setTextColor(textColor);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextSize(1);

    int centerX = btn.x + btn.w / 2;
    int centerY = btn.y + btn.h / 2;

    _tft->drawString(btn.label, centerX, centerY, 2);
}

int ButtonManager::checkTouch() {
    // Use tirqTouched for CYD
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
