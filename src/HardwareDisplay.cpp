#include "HardwareDisplay.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {
    SDL_Window* s_window = nullptr;
    SDL_Renderer* s_renderer = nullptr;
    SDL_Texture* s_texture = nullptr;

    int s_uiWidth = 1280;
    int s_uiHeight = 800;
    int s_physWidth = 1280;
    int s_physHeight = 800;
    double s_rotationAngle = 0.0;

    uint32_t* s_framebuffer = nullptr;
    uint8_t* s_drawBuf1 = nullptr;
    uint8_t* s_drawBuf2 = nullptr;

    lv_display_t* s_display = nullptr;
    lv_indev_t* s_keyboardIndev = nullptr;

    // Multitouch state (supports up to 10 simultaneous touches)
    constexpr int MAX_TOUCH_SLOTS = 10;

    struct TouchSlot {
        bool active = false;
        SDL_FingerID fingerId = -1;
        bool pressed = false;
        int rawX = 0;
        int rawY = 0;
        int uiX = 0;
        int uiY = 0;
        lv_indev_t* indev = nullptr;
    };

    TouchSlot s_touchSlots[MAX_TOUCH_SLOTS];

    int findSlotForFinger(SDL_FingerID fingerId) {
        for (int i = 0; i < MAX_TOUCH_SLOTS; ++i) {
            if (s_touchSlots[i].active && s_touchSlots[i].fingerId == fingerId) {
                return i;
            }
        }
        return -1;
    }

    int allocateSlotForFinger(SDL_FingerID fingerId) {
        int existing = findSlotForFinger(fingerId);
        if (existing >= 0) return existing;

        for (int i = 0; i < MAX_TOUCH_SLOTS; ++i) {
            if (!s_touchSlots[i].active) {
                s_touchSlots[i].active = true;
                s_touchSlots[i].fingerId = fingerId;
                return i;
            }
        }
        return 0; // Fallback to slot 0 if all 10 are occupied
    }

    // Keyboard buffer
    char s_keyBuf[64] = {0};
    size_t s_keyBufLen = 0;
    bool s_dummyKeyRead = false;
}

bool HardwareDisplay::init(int uiWidth, int uiHeight) {
    s_uiWidth = uiWidth;
    s_uiHeight = uiHeight;

    // Ensure SDL video subsystem is initialized
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
            std::cerr << "[HardwareDisplay] SDL Video Init Failed: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    // Disable synthetic mouse events from touchscreen to prevent event conflicts
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    // Query physical display resolution from KMS / windowing system
    SDL_DisplayMode dm;
    s_physWidth = uiWidth;
    s_physHeight = uiHeight;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        s_physWidth = dm.w;
        s_physHeight = dm.h;
        std::cout << "[HardwareDisplay] SDL reported physical display mode: "
                  << s_physWidth << "x" << s_physHeight << "@" << dm.refresh_rate << "Hz" << std::endl;
    }

    // Determine rotation angle
    const char* rotEnv = std::getenv("LOOM_ROTATION");
    if (rotEnv != nullptr && std::strlen(rotEnv) > 0) {
        s_rotationAngle = std::atof(rotEnv);
        std::cout << "[HardwareDisplay] LOOM_ROTATION environment variable override: "
                  << s_rotationAngle << " deg" << std::endl;
    } else {
        if (s_physWidth < s_physHeight) {
            // Mobile portrait panel (e.g. 800x1280) mounted in landscape device
            s_rotationAngle = 270.0;
            std::cout << "[HardwareDisplay] Auto-detected portrait display (" << s_physWidth
                      << "x" << s_physHeight << "). Setting rotation to 270 deg." << std::endl;
        } else {
            s_rotationAngle = 0.0;
            std::cout << "[HardwareDisplay] Native landscape display (" << s_physWidth
                      << "x" << s_physHeight << "). Rotation: 0 deg." << std::endl;
        }
    }

    // Setup window dimensions and flags
    Uint32 windowFlags = SDL_WINDOW_SHOWN;
    int winW = s_physWidth;
    int winH = s_physHeight;

#ifdef __APPLE__
    // On macOS desktop simulator: create normal window sized to UI
    winW = s_uiWidth;
    winH = s_uiHeight;
    windowFlags |= SDL_WINDOW_RESIZABLE;
#else
    // On Raspberry Pi / Linux KMSDRM: run fullscreen at native CRTC resolution
    windowFlags |= SDL_WINDOW_FULLSCREEN;
#endif

    s_window = SDL_CreateWindow(
        "Loom Pi",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        winW, winH,
        windowFlags
    );

    if (!s_window) {
        std::cerr << "[HardwareDisplay] Failed to create SDL Window: " << SDL_GetError() << std::endl;
        return false;
    }

    // Try hardware-accelerated renderer first (uses VC4/GLES2 on Pi), fallback to software
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        std::cerr << "[HardwareDisplay] Accelerated renderer not available (" << SDL_GetError()
                  << "), falling back to software renderer." << std::endl;
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_renderer) {
        std::cerr << "[HardwareDisplay] Failed to create SDL Renderer: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create streaming texture for UI canvas
    s_texture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        s_uiWidth, s_uiHeight
    );
    if (!s_texture) {
        std::cerr << "[HardwareDisplay] Failed to create SDL Texture: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_SetTextureBlendMode(s_texture, SDL_BLENDMODE_NONE);

    // Framebuffer for dirty-rectangle blending
    s_framebuffer = new uint32_t[s_uiWidth * s_uiHeight];
    std::memset(s_framebuffer, 0, s_uiWidth * s_uiHeight * sizeof(uint32_t));

    // Double draw buffers for partial LVGL refreshes (80 lines high = ~400KB per buffer)
    size_t drawBufSize = s_uiWidth * 80 * sizeof(lv_color_t);
    s_drawBuf1 = static_cast<uint8_t*>(std::malloc(drawBufSize));
    s_drawBuf2 = static_cast<uint8_t*>(std::malloc(drawBufSize));

    // Create and configure LVGL display
    s_display = lv_display_create(s_uiWidth, s_uiHeight);
    if (!s_display) {
        std::cerr << "[HardwareDisplay] Failed to create LVGL display!" << std::endl;
        return false;
    }
    lv_display_set_buffers(s_display, s_drawBuf1, s_drawBuf2, drawBufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, flushCallback);

    // Provide high-resolution tick callback to LVGL
    lv_tick_set_cb(SDL_GetTicks);

    // Create 10 pointer indevs for multi-touch (one for each simultaneous contact point)
    for (int i = 0; i < MAX_TOUCH_SLOTS; ++i) {
        s_touchSlots[i].active = false;
        s_touchSlots[i].fingerId = -1;
        s_touchSlots[i].pressed = false;
        s_touchSlots[i].rawX = 0;
        s_touchSlots[i].rawY = 0;
        s_touchSlots[i].uiX = 0;
        s_touchSlots[i].uiY = 0;

        s_touchSlots[i].indev = lv_indev_create();
        lv_indev_set_type(s_touchSlots[i].indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_touchSlots[i].indev, pointerReadCallback);
        lv_indev_set_driver_data(s_touchSlots[i].indev, &s_touchSlots[i]);
        lv_indev_set_mode(s_touchSlots[i].indev, LV_INDEV_MODE_EVENT);
    }

    // Create keypad indev (keyboard / text input)
    s_keyboardIndev = lv_indev_create();
    lv_indev_set_type(s_keyboardIndev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keyboardIndev, keyboardReadCallback);
    lv_indev_set_mode(s_keyboardIndev, LV_INDEV_MODE_EVENT);

    // Start text input for modal entries
    SDL_StartTextInput();

    std::cout << "[HardwareDisplay] Display initialized successfully with 10x multitouch. ("
              << s_uiWidth << "x" << s_uiHeight << " UI on "
              << s_physWidth << "x" << s_physHeight << " physical screen, rotation: "
              << s_rotationAngle << " deg)" << std::endl;

    return true;
}

void HardwareDisplay::cleanup() {
    if (s_texture) {
        SDL_DestroyTexture(s_texture);
        s_texture = nullptr;
    }
    if (s_renderer) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = nullptr;
    }
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
    }
    delete[] s_framebuffer;
    s_framebuffer = nullptr;
    if (s_drawBuf1) {
        std::free(s_drawBuf1);
        s_drawBuf1 = nullptr;
    }
    if (s_drawBuf2) {
        std::free(s_drawBuf2);
        s_drawBuf2 = nullptr;
    }
}

lv_display_t* HardwareDisplay::getDisplay() {
    return s_display;
}

lv_indev_t* HardwareDisplay::getPointerIndev() {
    // Return primary pointer indev (Slot 0)
    return s_touchSlots[0].indev;
}

lv_indev_t* HardwareDisplay::getKeyboardIndev() {
    return s_keyboardIndev;
}

int HardwareDisplay::getPhysicalWidth() {
    return s_physWidth;
}

int HardwareDisplay::getPhysicalHeight() {
    return s_physHeight;
}

double HardwareDisplay::getRotationAngle() {
    return s_rotationAngle;
}

void HardwareDisplay::flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    uint32_t* src = reinterpret_cast<uint32_t*>(px_map);
    uint32_t* dst = s_framebuffer + (area->y1 * s_uiWidth + area->x1);

    for (int32_t y = 0; y < h; ++y) {
        std::memcpy(dst, src, w * sizeof(uint32_t));
        src += w;
        dst += s_uiWidth;
    }

    if (lv_display_flush_is_last(disp)) {
        SDL_UpdateTexture(s_texture, nullptr, s_framebuffer, s_uiWidth * sizeof(uint32_t));
        SDL_RenderClear(s_renderer);

        if (s_rotationAngle == 90.0 || s_rotationAngle == 270.0) {
            SDL_Rect dstRect;
            dstRect.x = (s_physWidth - s_uiWidth) / 2;
            dstRect.y = (s_physHeight - s_uiHeight) / 2;
            dstRect.w = s_uiWidth;
            dstRect.h = s_uiHeight;

            SDL_Point center = { s_uiWidth / 2, s_uiHeight / 2 };
            SDL_RenderCopyEx(s_renderer, s_texture, nullptr, &dstRect, s_rotationAngle, &center, SDL_FLIP_NONE);
        } else if (s_rotationAngle == 180.0) {
            SDL_RenderCopyEx(s_renderer, s_texture, nullptr, nullptr, 180.0, nullptr, SDL_FLIP_NONE);
        } else {
            SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
        }

        SDL_RenderPresent(s_renderer);
    }

    lv_display_flush_ready(disp);
}

void HardwareDisplay::pointerReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    TouchSlot* slot = static_cast<TouchSlot*>(lv_indev_get_driver_data(indev));
    if (slot) {
        data->point.x = slot->uiX;
        data->point.y = slot->uiY;
        data->state = slot->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
}

void HardwareDisplay::keyboardReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (s_dummyKeyRead) {
        s_dummyKeyRead = false;
        data->state = LV_INDEV_STATE_RELEASED;
    } else if (s_keyBufLen > 0) {
        s_dummyKeyRead = true;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = s_keyBuf[0];
        std::memmove(s_keyBuf, s_keyBuf + 1, s_keyBufLen - 1);
        s_keyBufLen--;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void HardwareDisplay::pushControlKey(uint32_t lvKey) {
    if (s_keyBufLen < sizeof(s_keyBuf) - 1) {
        s_keyBuf[s_keyBufLen++] = static_cast<char>(lvKey);
        s_keyBuf[s_keyBufLen] = '\0';
        if (s_keyboardIndev) {
            lv_indev_read(s_keyboardIndev);
        }
    }
}

void HardwareDisplay::transformCoordinates(int rawX, int rawY, int& outX, int& outY) {
    if (s_rotationAngle == 270.0) {
        // Physical panel is 800w x 1280h
        // Long edge (1280) is UI horizontal, short edge (800) is UI vertical
        outX = (s_physHeight - 1) - rawY;
        outY = rawX;
    } else if (s_rotationAngle == 90.0) {
        outX = rawY;
        outY = (s_physWidth - 1) - rawX;
    } else if (s_rotationAngle == 180.0) {
        outX = (s_physWidth - 1) - rawX;
        outY = (s_physHeight - 1) - rawY;
    } else {
        // 0 deg (standard landscape)
        outX = rawX;
        outY = rawY;
    }

    outX = std::max(0, std::min(outX, s_uiWidth - 1));
    outY = std::max(0, std::min(outY, s_uiHeight - 1));
}

bool HardwareDisplay::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                TouchSlot& slot = s_touchSlots[0];
                slot.active = true;
                slot.fingerId = -999;
                slot.pressed = true;
                slot.rawX = event.button.x;
                slot.rawY = event.button.y;
                transformCoordinates(slot.rawX, slot.rawY, slot.uiX, slot.uiY);
                if (slot.indev) lv_indev_read(slot.indev);
            }
            return true;

        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                TouchSlot& slot = s_touchSlots[0];
                slot.pressed = false;
                slot.rawX = event.button.x;
                slot.rawY = event.button.y;
                transformCoordinates(slot.rawX, slot.rawY, slot.uiX, slot.uiY);
                if (slot.indev) lv_indev_read(slot.indev);
                slot.active = false;
                slot.fingerId = -1;
            }
            return true;

        case SDL_MOUSEMOTION:
            if (s_touchSlots[0].pressed) {
                TouchSlot& slot = s_touchSlots[0];
                slot.rawX = event.motion.x;
                slot.rawY = event.motion.y;
                transformCoordinates(slot.rawX, slot.rawY, slot.uiX, slot.uiY);
                if (slot.indev) lv_indev_read(slot.indev);
            }
            return true;

        case SDL_FINGERDOWN: {
            int slotIdx = allocateSlotForFinger(event.tfinger.fingerId);
            TouchSlot& slot = s_touchSlots[slotIdx];
            slot.pressed = true;
            slot.rawX = static_cast<int>(event.tfinger.x * s_physWidth);
            slot.rawY = static_cast<int>(event.tfinger.y * s_physHeight);
            transformCoordinates(slot.rawX, slot.rawY, slot.uiX, slot.uiY);

            std::cout << "[Touch] Finger DOWN [Finger " << event.tfinger.fingerId << " -> Slot " << slotIdx
                      << "] raw: (" << slot.rawX << ", " << slot.rawY
                      << ") -> UI: (" << slot.uiX << ", " << slot.uiY << ")" << std::endl;

            if (slot.indev) lv_indev_read(slot.indev);
            return true;
        }

        case SDL_FINGERUP: {
            int slotIdx = findSlotForFinger(event.tfinger.fingerId);
            if (slotIdx >= 0) {
                TouchSlot& slot = s_touchSlots[slotIdx];
                slot.pressed = false;
                slot.rawX = static_cast<int>(event.tfinger.x * s_physWidth);
                slot.rawY = static_cast<int>(event.tfinger.y * s_physHeight);
                transformCoordinates(slot.rawX, slot.rawY, slot.uiX, slot.uiY);

                std::cout << "[Touch] Finger UP [Finger " << event.tfinger.fingerId << " -> Slot " << slotIdx
                          << "] UI: (" << slot.uiX << ", " << slot.uiY << ")" << std::endl;

                if (slot.indev) lv_indev_read(slot.indev);
                slot.active = false;
                slot.fingerId = -1;
            }
            return true;
        }

        case SDL_FINGERMOTION: {
            int slotIdx = findSlotForFinger(event.tfinger.fingerId);
            if (slotIdx >= 0) {
                TouchSlot& slot = s_touchSlots[slotIdx];
                slot.rawX = static_cast<int>(event.tfinger.x * s_physWidth);
                slot.rawY = static_cast<int>(event.tfinger.y * s_physHeight);
                transformCoordinates(slot.rawX, slot.rawY, slot.uiX, slot.uiY);
                if (slot.indev) lv_indev_read(slot.indev);
            }
            return true;
        }

        case SDL_TEXTINPUT: {
            size_t addLen = std::strlen(event.text.text);
            if (s_keyBufLen + addLen < sizeof(s_keyBuf) - 1) {
                std::memcpy(s_keyBuf + s_keyBufLen, event.text.text, addLen);
                s_keyBufLen += addLen;
                s_keyBuf[s_keyBufLen] = '\0';
                if (s_keyboardIndev) {
                    lv_indev_read(s_keyboardIndev);
                }
            }
            return true;
        }

        default:
            break;
    }
    return false;
}
