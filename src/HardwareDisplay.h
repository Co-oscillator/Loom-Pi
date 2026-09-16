#pragma once

#include <SDL.h>
#include "lvgl.h"
#include <cstdint>

class HardwareDisplay {
public:
    static bool init(int uiWidth, int uiHeight);
    static void cleanup();

    static lv_display_t* getDisplay();
    static lv_indev_t* getPointerIndev();
    static lv_indev_t* getKeyboardIndev();

    static int getPhysicalWidth();
    static int getPhysicalHeight();
    static double getRotationAngle();

    // Process incoming SDL events for touch, mouse, and text input
    static bool handleEvent(const SDL_Event& event);

    // Push a control key (e.g. LV_KEY_BACKSPACE) to the LVGL keyboard indev
    static void pushControlKey(uint32_t lvKey);

private:
    static void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void pointerReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    static void keyboardReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    static void transformCoordinates(int rawX, int rawY, int& outX, int& outY);
};
