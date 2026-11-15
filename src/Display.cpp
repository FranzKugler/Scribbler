/**
 * Display
 * SSD1306 via Adafruit's driver; see Display.h for what and why.
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#include "Display.h"

#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include "Board.h"
#include "LogBuffer.h"

namespace
{
    constexpr int WIDTH = 128;
    constexpr int HEIGHT = 32;
    constexpr int LINES = 4;
    constexpr int COLUMNS = 21;   // 6 px per character at text size 1

    Adafruit_SSD1306 oled(WIDTH, HEIGHT, &Wire, -1);
    bool found = false;

    char shown[LINES][COLUMNS + 1] = {};
}

namespace Display
{
    void begin()
    {
        // Ask the bus first: Adafruit's begin() reports success for a display
        // that is not there, because it only fails on the buffer allocation.
        Wire.beginTransmission(Board::DISPLAY_ADDR);
        if (Wire.endTransmission() != 0)
        {
            debugW("Display: none at 0x%02X - carrying on without", Board::DISPLAY_ADDR);
            return;
        }
        if (!oled.begin(SSD1306_SWITCHCAPVCC, Board::DISPLAY_ADDR, true, false))
        {
            debugE("Display: no memory for the frame buffer");
            return;
        }
        found = true;
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.cp437(true);
        debugI("Display: SSD1306 at 0x%02X", Board::DISPLAY_ADDR);
    }

    bool present()
    {
        return found;
    }

    void show(const char *line1, const char *line2, const char *line3, const char *line4)
    {
        if (!found) return;

        const char *lines[LINES] = {line1, line2, line3, line4};
        bool changed = false;
        for (int i = 0; i < LINES; i++)
        {
            char cut[COLUMNS + 1];
            strlcpy(cut, lines[i] ? lines[i] : "", sizeof cut);
            if (strcmp(cut, shown[i]) != 0)
            {
                strlcpy(shown[i], cut, sizeof shown[i]);
                changed = true;
            }
        }
        if (!changed) return;

        oled.clearDisplay();
        for (int i = 0; i < LINES; i++)
        {
            oled.setCursor(0, i * 8);
            oled.print(shown[i]);
        }
        oled.display();
    }
}
