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
    lv_indev_t* s_pointerIndev = nullptr;
    lv_indev_t* s_keyboardIndev = nullptr;

    // Pointer state
    bool s_pointerDown = false;
    int s_rawPointerX = 0;
    int s_rawPointerY = 0;
    int s_uiPointerX = 0;
    int s_uiPointerY = 0;

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

    // Create pointer indev (touchscreen and mouse)
    s_pointerIndev = lv_indev_create();
    lv_indev_set_type(s_pointerIndev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_pointerIndev, pointerReadCallback);
    lv_indev_set_mode(s_pointerIndev, LV_INDEV_MODE_EVENT);

    // Create keypad indev (keyboard / text input)
    s_keyboardIndev = lv_indev_create();
    lv_indev_set_type(s_keyboardIndev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keyboardIndev, keyboardReadCallback);
    lv_indev_set_mode(s_keyboardIndev, LV_INDEV_MODE_EVENT);

    // Start text input for modal entries
    SDL_StartTextInput();

    std::cout << "[HardwareDisplay] Display initialized successfully. ("
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
    return s_pointerIndev;
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
    (void)indev;
    data->point.x = s_uiPointerX;
    data->point.y = s_uiPointerY;
    data->state = s_pointerDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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
                s_pointerDown = true;
                s_rawPointerX = event.button.x;
                s_rawPointerY = event.button.y;
                transformCoordinates(s_rawPointerX, s_rawPointerY, s_uiPointerX, s_uiPointerY);
                if (s_pointerIndev) lv_indev_read(s_pointerIndev);
            }
            return true;

        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                s_pointerDown = false;
                s_rawPointerX = event.button.x;
                s_rawPointerY = event.button.y;
                transformCoordinates(s_rawPointerX, s_rawPointerY, s_uiPointerX, s_uiPointerY);
                if (s_pointerIndev) lv_indev_read(s_pointerIndev);
            }
            return true;

        case SDL_MOUSEMOTION:
            s_rawPointerX = event.motion.x;
            s_rawPointerY = event.motion.y;
            transformCoordinates(s_rawPointerX, s_rawPointerY, s_uiPointerX, s_uiPointerY);
            if (s_pointerDown && s_pointerIndev) {
                lv_indev_read(s_pointerIndev);
            }
            return true;

        case SDL_FINGERDOWN: {
            s_pointerDown = true;
            s_rawPointerX = static_cast<int>(event.tfinger.x * s_physWidth);
            s_rawPointerY = static_cast<int>(event.tfinger.y * s_physHeight);
            transformCoordinates(s_rawPointerX, s_rawPointerY, s_uiPointerX, s_uiPointerY);
            std::cout << "[HardwareDisplay Touch] Finger DOWN raw: (" << s_rawPointerX
                      << ", " << s_rawPointerY << ") -> UI: (" << s_uiPointerX
                      << ", " << s_uiPointerY << ")" << std::endl;
            if (s_pointerIndev) lv_indev_read(s_pointerIndev);
            return true;
        }

        case SDL_FINGERUP: {
            s_pointerDown = false;
            s_rawPointerX = static_cast<int>(event.tfinger.x * s_physWidth);
            s_rawPointerY = static_cast<int>(event.tfinger.y * s_physHeight);
            transformCoordinates(s_rawPointerX, s_rawPointerY, s_uiPointerX, s_uiPointerY);
            std::cout << "[HardwareDisplay Touch] Finger UP raw: (" << s_rawPointerX
                      << ", " << s_rawPointerY << ")" << std::endl;
            if (s_pointerIndev) lv_indev_read(s_pointerIndev);
            return true;
        }

        case SDL_FINGERMOTION: {
            s_rawPointerX = static_cast<int>(event.tfinger.x * s_physWidth);
            s_rawPointerY = static_cast<int>(event.tfinger.y * s_physHeight);
            transformCoordinates(s_rawPointerX, s_rawPointerY, s_uiPointerX, s_uiPointerY);
            if (s_pointerDown && s_pointerIndev) {
                lv_indev_read(s_pointerIndev);
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
