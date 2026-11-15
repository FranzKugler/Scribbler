/**
 * Display
 * The 0.91" SSD1306 (128 x 32): four lines of text, for the moments the web
 * UI cannot be reached - above all the setup portal, where it names the access
 * point and the address to open.
 *
 * Optional at runtime. A display that does not answer on the bus at boot is
 * logged once and every call afterwards is a no-op; it is never a reason not
 * to boot (PLAN.md section 0.5).
 *
 * What it is not: a renderer for the plot or a menu. Four lines, redrawn only
 * when their text changes - an I2C frame is ~10 ms at 400 kHz, and nothing
 * here may take that from the motion side for an unchanged picture.
 *
 * Expects Wire to be started (main.cpp does, once, for display and IMU).
 *
 * @mc       ESP32S3
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  15.11.2026
 * @updated  15.11.2026
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

namespace Display
{
    void begin();
    bool present();

    /** Up to four lines of up to 21 characters; longer ones are cut. */
    void show(const char *line1, const char *line2 = "", const char *line3 = "",
              const char *line4 = "");
}

#endif
