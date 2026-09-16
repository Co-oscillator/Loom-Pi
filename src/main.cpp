#include "AudioEngine.h"
#include "Utils.h"
#include "HardwareDisplay.h"
#include <iostream>
#include <SDL.h>
#include "lvgl.h"
#include "src/indev/lv_indev_private.h"
#include "ui/UIManager.h"
#include "MidiInput.h"
#include <unordered_map>
#include <algorithm>
#include <cctype>

// Global audio engine
AudioEngine gEngine;

#ifdef __APPLE__
MIDIClientRef gMidiClient = 0;
MIDIPortRef gMidiInputPort = 0;
MIDIPortRef gMidiOutputPort = 0;
MidiCallbackData gMidiCallbackData = {nullptr, nullptr};
#else
snd_seq_t* gSeq = nullptr;
snd_seq_t* gSeqOut = nullptr; // Dedicated output handle for thread safety
int gInPort = -1;
int gOutPort = -1;
pthread_t gMidiThread;
bool gMidiThreadRunning = false;
MidiCallbackData gMidiCallbackData = {nullptr, nullptr};
#endif


// Audio Callback
void audioCallback(void* userdata, Uint8* stream, int len) {
    float* out = reinterpret_cast<float*>(stream);
    int numFrames = len / (sizeof(float) * 2); // Assuming Stereo float
    gEngine.renderOutput(out, numFrames, 2);
}

void audioCaptureCallback(void* userdata, Uint8* stream, int len) {
    int16_t* in = reinterpret_cast<int16_t*>(stream);
    int numFrames = len / (sizeof(int16_t) * 2); // Stereo 16-bit capture
    
    float floatBuffer[4096];
    int framesToDo = std::min(numFrames, 2048);
    for (int i = 0; i < framesToDo; ++i) {
        floatBuffer[i * 2] = (float)in[i * 2] / 32768.0f;
        floatBuffer[i * 2 + 1] = (float)in[i * 2 + 1] / 32768.0f;
    }
    gEngine.renderInput(floatBuffer, framesToDo, 2);
}

SDL_AudioDeviceID gAudioDeviceID = 0;
std::string gCurrentAudioDevice = "Default";

bool switchAudioDevice(const std::string& deviceName) {
    int numDevs = SDL_GetNumAudioDevices(0);
    std::cout << "[Audio] Detected " << numDevs << " playback device(s):" << std::endl;
    for (int i = 0; i < numDevs; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        std::cout << "  [" << i << "] " << (name ? name : "(null)") << std::endl;
    }

    std::string targetDev = deviceName;
    if (targetDev.empty() || targetDev == "Default" || targetDev == "SDL Default") {
        std::string chosen = "";
        for (int i = 0; i < numDevs; ++i) {
            const char* name = SDL_GetAudioDeviceName(i, 0);
            if (!name) continue;
            std::string s(name);
            std::string sLower = s;
            std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);
            if (sLower.find("ab13x") != std::string::npos) {
                chosen = s;
                break;
            }
        }
        if (chosen.empty()) {
            for (int i = 0; i < numDevs; ++i) {
                const char* name = SDL_GetAudioDeviceName(i, 0);
                if (!name) continue;
                std::string s(name);
                std::string sLower = s;
                std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);
                if (sLower.find("usb") != std::string::npos) {
                    chosen = s;
                    break;
                }
            }
        }
        if (!chosen.empty()) {
            targetDev = chosen;
            std::cout << "[Audio] Auto-selected USB playback device: " << targetDev << std::endl;
        } else {
            targetDev = "Default";
        }
    }

    if (gAudioDeviceID != 0) {
        SDL_CloseAudioDevice(gAudioDeviceID);
        gAudioDeviceID = 0;
        SDL_Delay(50); // Settle delay for USB hubs / transaction translators
    }
    
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = audioCallback;
    
    const char* devName = (targetDev == "Default" || targetDev == "SDL Default" || targetDev.empty()) ? nullptr : targetDev.c_str();
    gAudioDeviceID = SDL_OpenAudioDevice(devName, 0, &want, &have, 0);
    if (gAudioDeviceID == 0) {
        std::cerr << "switchAudioDevice failed for '" << (devName ? devName : "default") 
                  << "': " << SDL_GetError() << std::endl;
        if (devName != nullptr) {
            std::cout << "[Audio] Falling back to default audio device..." << std::endl;
            gAudioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (gAudioDeviceID != 0) {
                targetDev = "Default";
            }
        }
    }
    
    if (gAudioDeviceID != 0) {
        gCurrentAudioDevice = targetDev;
        SDL_PauseAudioDevice(gAudioDeviceID, 0);
        std::cout << "SDL Audio Device switched to: " << gCurrentAudioDevice 
                  << " (have " << have.samples << " samples @" << have.freq << "Hz)" << std::endl;
        return true;
    }
    return false;
}

SDL_AudioDeviceID gCaptureDeviceID = 0;
std::string gCurrentCaptureDevice = "Default";

void setCaptureActive(bool active) {
    if (active) {
        if (gCaptureDeviceID == 0) {
            SDL_AudioSpec wantCapture, haveCapture;
            SDL_zero(wantCapture);
            wantCapture.freq = 48000;
            wantCapture.format = AUDIO_S16SYS;
            wantCapture.channels = 2;
            wantCapture.samples = 256;
            wantCapture.callback = audioCaptureCallback;

            const char* devName = (gCurrentCaptureDevice.empty() || gCurrentCaptureDevice == "Default" || gCurrentCaptureDevice == "SDL Default") ? nullptr : gCurrentCaptureDevice.c_str();
            gCaptureDeviceID = SDL_OpenAudioDevice(devName, 1, &wantCapture, &haveCapture, 0);
            if (gCaptureDeviceID != 0) {
                SDL_PauseAudioDevice(gCaptureDeviceID, 0);
                std::cout << "[Audio] On-demand Audio Capture STARTED on: " << gCurrentCaptureDevice << std::endl;
            } else {
                std::cerr << "[Audio] Failed to open capture device: " << SDL_GetError() << std::endl;
            }
        } else {
            SDL_PauseAudioDevice(gCaptureDeviceID, 0);
        }
    } else {
        if (gCaptureDeviceID != 0) {
            SDL_CloseAudioDevice(gCaptureDeviceID);
            gCaptureDeviceID = 0;
            std::cout << "[Audio] Audio Capture STOPPED and device closed." << std::endl;
        }
    }
}

bool switchCaptureDevice(const std::string& deviceName) {
    gCurrentCaptureDevice = deviceName;
    std::cout << "[Audio] Capture device configured to: " << gCurrentCaptureDevice << std::endl;
    if (gCaptureDeviceID != 0) {
        setCaptureActive(false);
        setCaptureActive(true);
    }
    return true;
}

int main() {
    std::cout << "Starting Loom Pi Audio Engine + UI..." << std::endl;
    
    // 1. Init Audio Engine
    gEngine.init(48000.0f);
    gEngine.setCaptureStateCallback(setCaptureActive);
    
    // 2. Init SDL Audio Subsystem
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "SDL Audio Init Failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    // 3. Init LVGL & Hardware Video Subsystem
    lv_init();
    if (!HardwareDisplay::init(UIManager::SCREEN_WIDTH, UIManager::SCREEN_HEIGHT)) {
        std::cerr << "CRITICAL ERROR: HardwareDisplay Init Failed!" << std::endl;
        return 1;
    }
    lv_display_t * disp = HardwareDisplay::getDisplay();
    lv_indev_t * indev = HardwareDisplay::getPointerIndev();
    
    // 4. Init UI Manager (loads settings.txt and opens the saved/configured audio device)
    UIManager ui(gEngine);
    ui.init();

    // Fallback: If settings.txt didn't specify an audio device, open it now
    if (gAudioDeviceID == 0) {
        switchAudioDevice(ui.mSettingsAudioDevice);
    }

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
    
    // Drum Number Row 1-8 key mapper
    auto symToDrumKey = [](SDL_Keycode key) -> int {
        switch (key) {
            case SDLK_1: case SDLK_KP_1: return 0;
            case SDLK_2: case SDLK_KP_2: return 1;
            case SDLK_3: case SDLK_KP_3: return 2;
            case SDLK_4: case SDLK_KP_4: return 3;
            case SDLK_5: case SDLK_KP_5: return 4;
            case SDLK_6: case SDLK_KP_6: return 5;
            case SDLK_7: case SDLK_KP_7: return 6;
            case SDLK_8: case SDLK_KP_8: return 7;
            default: return -1;
        }
    };

    // Track which keys are currently held (to prevent repeats and stuck notes)
    std::unordered_map<SDL_Keycode, int> activeKeyNotes;

    // Main loop
    while (true) {
        uint32_t frameStart = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Check for quit
            if (event.type == SDL_QUIT) {
                if (gCaptureDeviceID != 0) {
                    SDL_CloseAudioDevice(gCaptureDeviceID);
                }
                SDL_CloseAudioDevice(gAudioDeviceID);
                HardwareDisplay::cleanup();
                SDL_Quit();
                return 0;
            }

            // Handle scroll wheel on active slider/knob
            if (event.type == SDL_MOUSEWHEEL) {
                if (indev && indev->pointer.act_obj) {
                    lv_obj_t* obj = indev->pointer.act_obj;
                    if (lv_obj_check_type(obj, &lv_slider_class)) {
                        int32_t val = lv_slider_get_value(obj);
                        int32_t min = lv_slider_get_min_value(obj);
                        int32_t max = lv_slider_get_max_value(obj);
                        int32_t step = (max - min) / 30;
                        if (step < 1) step = 1;
                        int32_t newVal = val + (event.wheel.y * step);
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
                        int32_t step = (max - min) / 30;
                        if (step < 1) step = 1;
                        int32_t newVal = val + (event.wheel.y * step);
                        if (newVal < min) newVal = min;
                        if (newVal > max) newVal = max;
                        if (newVal != val) {
                            lv_arc_set_value(obj, newVal);
                            lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr);
                        }
                    }
                }
                continue;
            }

            // Forward touch, mouse, and text events to HardwareDisplay
            if (HardwareDisplay::handleEvent(event)) {
                continue;
            }

            // Keyboard handling
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_ESCAPE) {
                    if (gCaptureDeviceID != 0) {
                        SDL_CloseAudioDevice(gCaptureDeviceID);
                    }
                    SDL_CloseAudioDevice(gAudioDeviceID);
                    HardwareDisplay::cleanup();
                    SDL_Quit();
                    return 0;
                }

                // If file browser or console is open, send navigation/control keys to LVGL
                if (ui.isFileBrowserOpen() || ui.isConsoleModalOpen()) {
                    if (sym == SDLK_BACKSPACE) HardwareDisplay::pushControlKey(LV_KEY_BACKSPACE);
                    else if (sym == SDLK_DELETE) HardwareDisplay::pushControlKey(LV_KEY_DEL);
                    else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) HardwareDisplay::pushControlKey(LV_KEY_ENTER);
                    else if (sym == SDLK_LEFT) HardwareDisplay::pushControlKey(LV_KEY_LEFT);
                    else if (sym == SDLK_RIGHT) HardwareDisplay::pushControlKey(LV_KEY_RIGHT);
                    else if (sym == SDLK_UP) HardwareDisplay::pushControlKey(LV_KEY_UP);
                    else if (sym == SDLK_DOWN) HardwareDisplay::pushControlKey(LV_KEY_DOWN);
                    continue;
                }

                // Transport controls
                if (sym == SDLK_SPACE && !event.key.repeat) {
                    gEngine.setPlaying(!gEngine.getIsPlaying());
                } else if ((sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) && !event.key.repeat) {
                    bool recState = !gEngine.getIsRecording();
                    gEngine.setIsRecording(recState);
                    if (recState) {
                        gEngine.setPlaying(true);
                    }
                } else {
                    int drumIdx = symToDrumKey(sym);
                    if (drumIdx >= 0) {
                        if (!event.key.repeat) {
                            int track = ui.getDrumRowTargetTrack();
                            int note = ui.getDrumRowNote(drumIdx);
                            int ratchet = ui.getDrumRowRatchet(drumIdx);
                            gEngine.triggerDrumRowKey(drumIdx, track, note, ratchet, true);
                        }
                    } else if (ui.isKeyboardModeEnabled()) {
                        if (!event.key.repeat && activeKeyNotes.count(sym) == 0) {
                            int note = qwertyToNote(sym);
                            if (note >= 0 && note < 128) {
                                activeKeyNotes[sym] = note;
                                gEngine.triggerNote(ui.getActiveTrack(), note, 100);
                            }
                        }
                    }
                }
            } else if (event.type == SDL_KEYUP) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_SPACE || sym == SDLK_LSHIFT || sym == SDLK_RSHIFT) {
                    // Transport keys no-op on key up
                } else {
                    int drumIdx = symToDrumKey(sym);
                    if (drumIdx >= 0) {
                        int track = ui.getDrumRowTargetTrack();
                        int note = ui.getDrumRowNote(drumIdx);
                        int ratchet = ui.getDrumRowRatchet(drumIdx);
                        gEngine.triggerDrumRowKey(drumIdx, track, note, ratchet, false);
                    } else if (activeKeyNotes.count(sym) > 0) {
                        int note = activeKeyNotes[sym];
                        for (int t = 0; t < 8; ++t) {
                            gEngine.releaseNote(t, note);
                        }
                        activeKeyNotes.erase(sym);
                    }
                }
            }
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

        // Let LVGL handle timers and refresh
        lv_timer_handler();
        ui.update();

        // Target 60 FPS (16.6ms frame period) to eliminate idle CPU spin
        uint32_t elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16) {
            SDL_Delay(16 - elapsed);
        } else {
            SDL_Delay(1);
        }
    }

    if (gCaptureDeviceID != 0) {
        SDL_CloseAudioDevice(gCaptureDeviceID);
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

