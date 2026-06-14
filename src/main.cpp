#include "AudioEngine.h"
#include "Utils.h"
#include <iostream>
#include <SDL.h>
#include "lvgl.h"
#include "src/indev/lv_indev_private.h"
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
    int numFrames = len / (sizeof(float) * 2); // Stereo float capture
    gEngine.renderInput(in, numFrames, 2);
}

SDL_AudioDeviceID gAudioDeviceID = 0;
std::string gCurrentAudioDevice = "Default";

bool switchAudioDevice(const std::string& deviceName) {
    if (gAudioDeviceID != 0) {
        SDL_CloseAudioDevice(gAudioDeviceID);
        gAudioDeviceID = 0;
    }
    
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = audioCallback;
    
    const char* devName = (deviceName.empty() || deviceName == "Default" || deviceName == "SDL Default") ? nullptr : deviceName.c_str();
    gAudioDeviceID = SDL_OpenAudioDevice(devName, 0, &want, &have, 0);
    if (gAudioDeviceID == 0) {
        std::cerr << "switchAudioDevice failed: " << SDL_GetError() << std::endl;
        // Fallback to default
        gAudioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        gCurrentAudioDevice = "Default";
    } else {
        gCurrentAudioDevice = deviceName;
    }
    
    if (gAudioDeviceID != 0) {
        SDL_PauseAudioDevice(gAudioDeviceID, 0);
        std::cout << "SDL Audio Device switched to: " << gCurrentAudioDevice << std::endl;
        return true;
    }
    return false;
}

int main() {
    std::cout << "Starting Loom Pi Audio Engine + UI..." << std::endl;
    
    // 1. Init Audio Engine
    gEngine.init(48000.0f);
    
    // 2. Init SDL Audio Subsystem
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "SDL Audio Init Failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    if (!switchAudioDevice(gCurrentAudioDevice)) {
        std::cerr << "Failed to open default SDL Audio Device." << std::endl;
        return 1;
    }
    
    // Open SDL Audio Capture (recording) device
    SDL_AudioSpec wantCapture, haveCapture;
    SDL_zero(wantCapture);
    wantCapture.freq = 48000;
    wantCapture.format = AUDIO_F32SYS;
    wantCapture.channels = 2;
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

        // Handle scroll wheel on active slider/knob when left mouse button is held
        SDL_Event wheelEvent;
        while (SDL_PeepEvents(&wheelEvent, 1, SDL_PEEKEVENT, SDL_MOUSEWHEEL, SDL_MOUSEWHEEL) > 0) {
            int mouseX, mouseY;
            Uint32 mouseState = SDL_GetMouseState(&mouseX, &mouseY);
            if (mouseState & SDL_BUTTON(SDL_BUTTON_LEFT)) {
                // Consume (remove) the event since we are handling it for slider/knob control mapping
                SDL_PeepEvents(&wheelEvent, 1, SDL_GETEVENT, SDL_MOUSEWHEEL, SDL_MOUSEWHEEL);
                if (indev && indev->pointer.act_obj) {
                    lv_obj_t* obj = indev->pointer.act_obj;
                    if (lv_obj_check_type(obj, &lv_slider_class)) {
                        int32_t val = lv_slider_get_value(obj);
                        int32_t min = lv_slider_get_min_value(obj);
                        int32_t max = lv_slider_get_max_value(obj);
                        int32_t step = (max - min) / 30; // ~3% step per click
                        if (step < 1) step = 1;
                        int32_t newVal = val + (wheelEvent.wheel.y * step);
                        if (newVal < min) newVal = min;
                        if (newVal > max) newVal = max;
                        if (newVal != val) {
                            lv_slider_set_value(obj, newVal, LV_ANIM_OFF);
                            lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr);
                        }
                    } else if (lv_obj_check_type(obj, &lv_arc_class)) {
                        int32_t val = lv_arc_get_value(obj);
                        int32_t min = lv_arc_get_min_value(obj);
                        int32_t max = lv_arc_get_max_value(obj);
                        int32_t step = (max - min) / 30; // ~3% step per click
                        if (step < 1) step = 1;
                        int32_t newVal = val + (wheelEvent.wheel.y * step);
                        if (newVal < min) newVal = min;
                        if (newVal > max) newVal = max;
                        if (newVal != val) {
                            lv_arc_set_value(obj, newVal);
                            lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr);
                        }
                    }
                }
            } else {
                // Left mouse button is not held: leave the scroll event in the queue for LVGL's default SDL driver to scroll containers
                break;
            }
        }
        
        // Check for quit
        SDL_Event quitEvent;
        if (SDL_PeepEvents(&quitEvent, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) {
            if (capDev != 0) {
                SDL_CloseAudioDevice(capDev);
            }
            SDL_CloseAudioDevice(gAudioDeviceID);
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
    SDL_CloseAudioDevice(gAudioDeviceID);
    SDL_Quit();
    return 0;
}

std::vector<std::string> getSystemConnectedMidiInputs() {
    std::vector<std::string> devices;
#ifdef __APPLE__
    ItemCount numSources = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < numSources; ++i) {
        MIDIEndpointRef source = MIDIGetSource(i);
        if (source != 0) {
            CFStringRef nameRef = NULL;
            MIDIObjectGetStringProperty(source, kMIDIPropertyName, &nameRef);
            if (nameRef) {
                char name[256];
                CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8);
                CFRelease(nameRef);
                devices.push_back(name);
            } else {
                devices.push_back("Unknown CoreMIDI Source");
            }
        }
    }
#else
    // On Linux ALSA
    if (gSeq) {
        snd_seq_client_info_t *cinfo = nullptr;
        snd_seq_port_info_t *pinfo = nullptr;
        if (snd_seq_client_info_malloc(&cinfo) >= 0 && snd_seq_port_info_malloc(&pinfo) >= 0) {
            snd_seq_client_info_set_client(cinfo, -1);
            while (snd_seq_query_next_client(gSeq, cinfo) >= 0) {
                int client = snd_seq_client_info_get_client(cinfo);
                if (client == snd_seq_client_id(gSeq)) continue;
                
                const char* clientName = snd_seq_client_info_get_name(cinfo);
                snd_seq_port_info_set_client(pinfo, client);
                snd_seq_port_info_set_port(pinfo, -1);
                while (snd_seq_query_next_port(gSeq, pinfo) >= 0) {
                    unsigned int capability = snd_seq_port_info_get_capability(pinfo);
                    if ((capability & SND_SEQ_PORT_CAP_READ) && (capability & SND_SEQ_PORT_CAP_SUBS_READ)) {
                        const char* portName = snd_seq_port_info_get_name(pinfo);
                        std::string fullName = clientName ? clientName : "Unknown Client";
                        if (portName && strlen(portName) > 0) {
                            fullName += " - " + std::string(portName);
                        }
                        devices.push_back(fullName);
                    }
                }
            }
            snd_seq_port_info_free(pinfo);
            snd_seq_client_info_free(cinfo);
        }
    }
#endif
    if (devices.empty()) {
        devices.push_back("No MIDI devices detected");
    }
    return devices;
}

std::vector<std::string> getSystemConnectedJoysticks() {
    std::vector<std::string> joysticks;
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; ++i) {
        const char* name = SDL_JoystickNameForIndex(i);
        if (name) {
            joysticks.push_back(name);
        } else {
            joysticks.push_back("Unknown Joystick");
        }
    }
    if (joysticks.empty()) {
        joysticks.push_back("No USB controllers / Joysticks detected");
    }
    return joysticks;
}

