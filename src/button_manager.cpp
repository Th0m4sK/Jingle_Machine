#include "button_manager.h"
#include "pin_config.h"

ButtonManager::ButtonManager(TFT_eSPI* tft, XPT2046_Touchscreen* touch)
    : _tft(tft), _touch(touch), globalRotation(0) {
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

    // Draw label with rotation
    int centerX = btn.x + btn.w / 2;
    int centerY = btn.y + btn.h / 2;

    _tft->setTextColor(textColor);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextSize(1);

    // For 0° rotation, draw directly (optimization)
    if (globalRotation == 0) {
        _tft->drawString(btn.label, centerX, centerY, 2);
        return;
    }

    // For rotated text, use sprite-based rotation
    // Check available heap before creating sprites
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 20000) {  // Need at least 20KB free
        Serial.printf("WARNING: Low memory (%d bytes), skipping rotation for button %d\n", freeHeap, id);
        _tft->drawString(btn.label, centerX, centerY, 2);
        return;
    }

    TFT_eSprite textSprite(_tft);

    // Measure text dimensions
    int16_t textWidth = _tft->textWidth(btn.label, 2);
    int16_t textHeight = _tft->fontHeight(2);

    // Create sprite - make it smaller to save memory
    // Limit sprite size to reduce memory usage
    int size = max(textWidth, textHeight) + 10;  // Reduced padding from 20 to 10
    if (size > 100) size = 100;  // Cap at 100x100 pixels max

    void* spritePtr = textSprite.createSprite(size, size);
    if (spritePtr == nullptr) {
        Serial.printf("ERROR: Failed to create text sprite for button %d\n", id);
        _tft->drawString(btn.label, centerX, centerY, 2);
        return;
    }
    textSprite.fillSprite(color);  // Fill with button background color

    // Draw text centered in sprite
    textSprite.setTextColor(textColor);
    textSprite.setTextDatum(MC_DATUM);
    textSprite.setTextSize(1);
    textSprite.drawString(btn.label, size/2, size/2, 2);

    // Create smaller destination sprite - only the text area, not entire button
    TFT_eSprite destSprite(_tft);
    int destSize = size + 10;  // Slightly larger for rotation
    if (destSize > btn.w) destSize = btn.w;
    if (destSize > btn.h) destSize = btn.h;

    spritePtr = destSprite.createSprite(destSize, destSize);
    if (spritePtr == nullptr) {
        Serial.printf("ERROR: Failed to create dest sprite for button %d\n", id);
        textSprite.deleteSprite();
        _tft->drawString(btn.label, centerX, centerY, 2);
        return;
    }
    destSprite.fillSprite(color);  // Fill with button background color

    // Set pivot point for rotation center
    textSprite.setPivot(size/2, size/2);

    // Push rotated text sprite to destination sprite
    // The rotation is done using TFT_eSPI's built-in sprite rotation
    textSprite.pushRotated(&destSprite, globalRotation, color);

    // Push destination sprite to screen at button center
    int spriteX = centerX - destSize/2;
    int spriteY = centerY - destSize/2;
    destSprite.pushSprite(spriteX, spriteY);

    // Clean up sprites
    destSprite.deleteSprite();
    textSprite.deleteSprite();
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
