#include "AudioEngine.h"
#include "Utils.h"
#include <iostream>
#include <SDL.h>
#include "lvgl.h"
#include "ui/UIManager.h"
#include "MidiInput.h"
#include <unordered_map>

// Global audio engine
AudioEngine gEngine;


// Audio Callback
void audioCallback(void* userdata, Uint8* stream, int len) {
    float* out = reinterpret_cast<float*>(stream);
    int numFrames = len / (sizeof(float) * 2); // Assuming Stereo float
    gEngine.renderOutput(out, numFrames, 2);
}

// Audio Capture Callback
void audioCaptureCallback(void* userdata, Uint8* stream, int len) {
    float* in = reinterpret_cast<float*>(stream);
    int numFrames = len / sizeof(float); // Mono float capture
    gEngine.renderInput(in, numFrames, 1);
}

int main() {
    std::cout << "Starting Loom Pi Audio Engine + UI..." << std::endl;
    
    // 1. Init Audio Engine
    gEngine.init(48000.0f);
    
    // 2. Init SDL Audio Subsystem
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL Audio Init Failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = audioCallback;
    
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    SDL_PauseAudioDevice(dev, 0); // Start audio
    
    // Open SDL Audio Capture (recording) device
    SDL_AudioSpec wantCapture, haveCapture;
    SDL_zero(wantCapture);
    wantCapture.freq = 48000;
    wantCapture.format = AUDIO_F32SYS;
    wantCapture.channels = 1;
    wantCapture.samples = 256;
    wantCapture.callback = audioCaptureCallback;

    SDL_AudioDeviceID capDev = SDL_OpenAudioDevice(NULL, 1, &wantCapture, &haveCapture, 0);
    if (capDev != 0) {
        SDL_PauseAudioDevice(capDev, 0); // Start capturing microphone input
        std::cout << "SDL Capture Device opened and started successfully." << std::endl;
    } else {
        std::cerr << "SDL_OpenAudioDevice (Capture) failed: " << SDL_GetError() << std::endl;
    }
    
    // 3. Init LVGL
    lv_init();
    
    // Create a 1024x600 window using LVGL's SDL driver
    lv_display_t * disp = lv_sdl_window_create(1024, 600);
    lv_indev_t * indev = lv_sdl_mouse_create();
    
    // 4. Init UI Manager
    UIManager ui(gEngine);
    ui.init();

    // 5. Init MIDI Input (CoreMIDI / Fallback)
    static MidiCallbackData midiData = {&gEngine, &ui};
    setupMidiInput(&midiData);
    
    std::cout << "Engine initialized successfully. Entering main loop." << std::endl;
    
    // QWERTY-to-MIDI note mapping (covers ~3 octaves)
    // Bottom row: Z=C3 ... M=B3
    // Middle row: A=C4 ... L=B4 (with sharps on QWERTY row)
    // Top row: Q=C5 ... P=E5
    auto qwertyToNote = [](SDL_Keycode key) -> int {
        switch (key) {
            // Bottom row - Octave 3 (natural + sharps)
            case SDLK_z: return 48; // C3
            case SDLK_s: return 49; // C#3
            case SDLK_x: return 50; // D3
            case SDLK_d: return 51; // D#3
            case SDLK_c: return 52; // E3
            case SDLK_v: return 53; // F3
            case SDLK_g: return 54; // F#3
            case SDLK_b: return 55; // G3
            case SDLK_h: return 56; // G#3
            case SDLK_n: return 57; // A3
            case SDLK_j: return 58; // A#3
            case SDLK_m: return 59; // B3
            // Top row - Octave 4
            case SDLK_q: return 60; // C4
            case SDLK_2: return 61; // C#4
            case SDLK_w: return 62; // D4
            case SDLK_3: return 63; // D#4
            case SDLK_e: return 64; // E4
            case SDLK_r: return 65; // F4
            case SDLK_5: return 66; // F#4
            case SDLK_t: return 67; // G4
            case SDLK_6: return 68; // G#4
            case SDLK_y: return 69; // A4
            case SDLK_7: return 70; // A#4
            case SDLK_u: return 71; // B4
            // Higher octave 5
            case SDLK_i: return 72; // C5
            case SDLK_9: return 73; // C#5
            case SDLK_o: return 74; // D5
            case SDLK_0: return 75; // D#5
            case SDLK_p: return 76; // E5
            default: return -1;
        }
    };
    
    // Track which keys are currently held (to prevent repeats and stuck notes)
    std::unordered_map<SDL_Keycode, int> activeKeyNotes;

    // Main loop
    while (true) {
        // Selectively grab ONLY keyboard events using SDL_PeepEvents.
        // This leaves mouse/touch events in the queue for LVGL's SDL driver.
        SDL_PumpEvents(); // Refresh the event queue
        
        // Check for quit
        SDL_Event quitEvent;
        if (SDL_PeepEvents(&quitEvent, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) {
            if (capDev != 0) {
                SDL_CloseAudioDevice(capDev);
            }
            SDL_CloseAudioDevice(dev);
            SDL_Quit();
            return 0;
        }

        // If window loses keyboard focus, thread-safely release all currently playing QWERTY notes
        if (SDL_GetKeyboardFocus() == NULL && !activeKeyNotes.empty()) {
            for (auto const& [key, note] : activeKeyNotes) {
                for (int t = 0; t < 8; ++t) {
                    gEngine.releaseNote(t, note);
                }
            }
            activeKeyNotes.clear();
        }
        
        // Transport & QWERTY Keyboard: grab key events ONLY if file browser is NOT open
        SDL_Event keyEvents[32];
        int numKeys = 0;
        if (!ui.isFileBrowserOpen()) {
            numKeys = SDL_PeepEvents(keyEvents, 32, SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYUP);
        }
        for (int k = 0; k < numKeys; ++k) {
            SDL_Keycode sym = keyEvents[k].key.keysym.sym;
            if (keyEvents[k].type == SDL_KEYDOWN) {
                if (sym == SDLK_SPACE && !keyEvents[k].key.repeat) {
                    gEngine.setPlaying(!gEngine.getIsPlaying());
                } else if ((sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) && !keyEvents[k].key.repeat) {
                    bool recState = !gEngine.getIsRecording();
                    gEngine.setIsRecording(recState);
                    if (recState) {
                        gEngine.setPlaying(true);
                    }
                } else if (ui.isKeyboardModeEnabled()) {
                    // Ignore repeats for note triggering
                    if (!keyEvents[k].key.repeat && activeKeyNotes.count(sym) == 0) {
                        int note = qwertyToNote(sym);
                        if (note >= 0 && note < 128) {
                            activeKeyNotes[sym] = note;
                            gEngine.triggerNote(ui.getActiveTrack(), note, 100);
                        }
                    }
                }
            } else if (keyEvents[k].type == SDL_KEYUP) {
                if (sym == SDLK_SPACE || sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) {
                    // Transport keys no-op on key up
                } else {
                    if (activeKeyNotes.count(sym) > 0) {
                        int note = activeKeyNotes[sym];
                        for (int t = 0; t < 8; ++t) {
                            gEngine.releaseNote(t, note);
                        }
                        activeKeyNotes.erase(sym);
                    }
                }
            }
        }

        // Let LVGL handle its timers and internal SDL event processing
        uint32_t time_till_next = lv_timer_handler();
        ui.update();
        
        if (time_till_next > 10) time_till_next = 10;
        SDL_Delay(time_till_next);
    }

    if (capDev != 0) {
        SDL_CloseAudioDevice(capDev);
    }
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
