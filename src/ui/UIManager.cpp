#include "UIManager.h"
#include <SDL.h>
#include <iostream>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <fstream>
#include <cmath>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static std::string getLocalIPAddress() {
    std::string ipAddr = "Unknown";
    struct ifaddrs *interfaces = nullptr;
    struct ifaddrs *tempAddr = nullptr;
    
    if (getifaddrs(&interfaces) == 0) {
        tempAddr = interfaces;
        while (tempAddr != nullptr) {
            if (tempAddr->ifa_addr != nullptr && tempAddr->ifa_addr->sa_family == AF_INET) {
                std::string interfaceName = tempAddr->ifa_name;
                if (interfaceName != "lo" && interfaceName.find("lo") == std::string::npos) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(((struct sockaddr_in*)tempAddr->ifa_addr)->sin_addr), ip, INET_ADDRSTRLEN);
                    ipAddr = ip;
                    if (interfaceName.find("wlan") != std::string::npos || interfaceName.find("eth") != std::string::npos || interfaceName.find("en") != std::string::npos) {
                        freeifaddrs(interfaces);
                        return ipAddr;
                    }
                }
            }
            tempAddr = tempAddr->ifa_next;
        }
        freeifaddrs(interfaces);
    }
    return ipAddr;
}


extern std::string gCurrentAudioDevice;
extern bool switchAudioDevice(const std::string& deviceName);

static int s_activeFmPreset[18] = {0};

static const char* FM_PRESET_NAMES[32] = {
    "Brass", "Strings Soft", "Orchestra", "Piano", "E. Piano", "Tine Synth",
    "Bass", "Organ", "Percussive Organ", "Tubular", "Mallet", "Vibe",
    "Marimba", "Chime", "Flute", "Tubular Bells", "Clavi", "Pluck",
    "Calliope", "Oboe", "Voice", "Xylophone", "Church Bells", "Synth Lead",
    "Recorders", "Shimmer", "Filter Sweep", "Funky Rise", "Refs Whisl", "Feedback Noise",
    "Harmonics", "Space Bell"
};

float UIManager::mapLinearToNonLinear(float norm, float minVal, float maxVal, const std::string& labelText) {
    std::string labelStr = labelText;
    for (auto & c: labelStr) c = toupper(c);
    
    std::string type = "";
    if (labelStr == "A" || labelStr == "ATTACK" || labelStr.find("ATTACK") != std::string::npos) {
        type = "A";
    } else if (labelStr == "D" || labelStr == "DECAY" || labelStr.find("DECAY") != std::string::npos) {
        type = "D";
    } else if (labelStr == "R" || labelStr == "RELEASE" || labelStr.find("RELEASE") != std::string::npos) {
        type = "R";
    }
    
    if (type.empty()) {
        return minVal + norm * (maxVal - minVal);
    }
    
    float midVal = 0.75f;
    if (type == "D") {
        midVal = 0.65f;
    } else if (type == "R") {
        midVal = 0.65f;
    }
    
    if (midVal <= minVal || midVal >= maxVal) {
        return minVal + norm * (maxVal - minVal);
    }
    
    float ratio = (midVal - minVal) / (maxVal - minVal);
    float p = logf(ratio) / logf(0.5f);
    
    return minVal + powf(norm, p) * (maxVal - minVal);
}

float UIManager::mapNonLinearToLinear(float val, float minVal, float maxVal, const std::string& labelText) {
    std::string labelStr = labelText;
    for (auto & c: labelStr) c = toupper(c);
    
    std::string type = "";
    if (labelStr == "A" || labelStr == "ATTACK" || labelStr.find("ATTACK") != std::string::npos) {
        type = "A";
    } else if (labelStr == "D" || labelStr == "DECAY" || labelStr.find("DECAY") != std::string::npos) {
        type = "D";
    } else if (labelStr == "R" || labelStr == "RELEASE" || labelStr.find("RELEASE") != std::string::npos) {
        type = "R";
    }
    
    if (type.empty()) {
        return (val - minVal) / (maxVal - minVal);
    }
    
    float midVal = 0.75f;
    if (type == "D") {
        midVal = 0.65f;
    } else if (type == "R") {
        midVal = 0.65f;
    }
    
    if (midVal <= minVal || midVal >= maxVal) {
        return (val - minVal) / (maxVal - minVal);
    }
    
    float ratio = (midVal - minVal) / (maxVal - minVal);
    float p = logf(ratio) / logf(0.5f);
    
    float norm = (val - minVal) / (maxVal - minVal);
    if (norm <= 0.0f) return 0.0f;
    if (norm >= 1.0f) return 1.0f;
    
    return powf(norm, 1.0f / p);
}

float UIManager::scaleParamFromNormalized(int paramId, float normValue) {
    // Attack parameters across all engines (0.001f to 4.0f)
    if (paramId == 100 || paramId == 114 || paramId == 310 || paramId == 425 || paramId == 454 || paramId == 471 ||
        paramId == 161 || paramId == 167 || paramId == 173 || paramId == 179 || paramId == 185 || paramId == 191) {
        return mapLinearToNonLinear(normValue, 0.001f, 4.0f, "A");
    }
    // Decay parameters across all engines (0.0f to 4.0f)
    if (paramId == 101 || paramId == 115 || paramId == 311 || paramId == 426 || paramId == 455 || paramId == 472 ||
        paramId == 162 || paramId == 168 || paramId == 174 || paramId == 180 || paramId == 186 || paramId == 192) {
        return mapLinearToNonLinear(normValue, 0.0f, 4.0f, "D");
    }
    // Release parameters across all engines (0.001f to 4.0f)
    if (paramId == 103 || paramId == 117 || paramId == 313 || paramId == 428 || paramId == 457 || paramId == 474 ||
        paramId == 164 || paramId == 170 || paramId == 176 || paramId == 182 || paramId == 188 || paramId == 194) {
        return mapLinearToNonLinear(normValue, 0.001f, 4.0f, "R");
    }
    return normValue;
}

float UIManager::normalizeParamValue(int paramId, float scaledValue) {
    // Attack parameters across all engines (0.001f to 4.0f)
    if (paramId == 100 || paramId == 114 || paramId == 310 || paramId == 425 || paramId == 454 || paramId == 471 ||
        paramId == 161 || paramId == 167 || paramId == 173 || paramId == 179 || paramId == 185 || paramId == 191) {
        return mapNonLinearToLinear(scaledValue, 0.001f, 4.0f, "A");
    }
    // Decay parameters across all engines (0.0f to 4.0f)
    if (paramId == 101 || paramId == 115 || paramId == 311 || paramId == 426 || paramId == 455 || paramId == 472 ||
        paramId == 162 || paramId == 168 || paramId == 174 || paramId == 180 || paramId == 186 || paramId == 192) {
        return mapNonLinearToLinear(scaledValue, 0.0f, 4.0f, "D");
    }
    // Release parameters across all engines (0.001f to 4.0f)
    if (paramId == 103 || paramId == 117 || paramId == 313 || paramId == 428 || paramId == 457 || paramId == 474 ||
        paramId == 164 || paramId == 170 || paramId == 176 || paramId == 182 || paramId == 188 || paramId == 194) {
        return mapNonLinearToLinear(scaledValue, 0.001f, 4.0f, "R");
    }
    return scaledValue;
}

UIManager::UIManager(AudioEngine& engine) : mEngine(engine) {
    mCcPlay = 59;
    mCcStop = 59;
    mCcRecord = 60;
    mCcClear = 61;
    mCcPrevTrack = 62;
    mCcNextTrack = 63;
    
    mSettingsPadCount = 16;
    mSettingsPadMode = 0;
    mSettingsOctaveOffset = 0;
    mSettingsFxPadMomentary = false;
    mSettingsKeyboardMode = false;
    mSettingsAudioDevice = "";
    
    mSettingsKnobCount = 12;
    mSettingsSliderCount = 4;

    for (int i = 0; i < 24; ++i) {
        mSettingsPadNoteMap[i] = 20 + i;
        mSettingsPadFxAssign[i] = i % 8;
        mSettingsPadDrumAssign[i] = i % 8;
        mSettingsPadChordCount[i] = 0;
        mSettingsPadFxToggleState[i] = false;
        for (int j = 0; j < 8; ++j) {
            mSettingsPadChordNotes[i][j] = 60;
        }
    }

    for (int t = 0; t < 8; ++t) {
        mTrackEnabled[t] = true; // Default all tracks to enabled for now
        mAftertouchDestParamId[t] = -1;
        mAftertouchDestBtnLabel[t] = nullptr;
        
        applyDefaultMidiMappings(t, mEngine.getTracks()[t].engineType);
    }

    // Default FX pedal chain values:
    mFxChainPedals[0][0] = -1;  // Empty
    mFxChainPedals[0][1] = -1;  // Empty
    mFxChainPedals[0][2] = -1;  // Empty
    mFxChainPedals[0][3] = -1; // Empty
    mFxChainPedals[0][4] = -1; // Empty

    mFxChainPedals[1][0] = -1;  // Empty
    mFxChainPedals[1][1] = -1;  // Empty
    mFxChainPedals[1][2] = -1; // Empty
    mFxChainPedals[1][3] = -1; // Empty
    mFxChainPedals[1][4] = -1; // Empty

    updateAudioEngineFxChains();

    for (int i = 0; i < 8; ++i) {
        for (int d = 0; d < 2; ++d) {
            mMacroDestParamId[i][d] = -1;
            mMacroDestTrack[i][d] = 0;
            mMacroDestType[i][d] = 5;
            mMacroDestAmount[i][d] = 0.0f;
            mMacroDestBtnLabel[i][d] = nullptr;
            mMacroArc[i][d] = nullptr;
        }
    }

    for (int i = 0; i < 6; ++i) {
        mLfoDestParamId[i] = -1;
        mLfoDestTrack[i] = 0;
        mLfoDestType[i] = 5;
        mLfoDestBtnLabel[i] = nullptr;
    }
}

UIManager::~UIManager() {}

void UIManager::init() {
    mMainScreen = lv_screen_active();
    
    // Set a dark background for the shell
    lv_obj_set_style_bg_color(mMainScreen, lv_color_hex(0x121212), 0);

    // Create the main flex container that holds the 3 columns
    lv_obj_t* mainFlex = lv_obj_create(mMainScreen);
    lv_obj_set_size(mainFlex, 1024, 600);
    lv_obj_set_style_pad_all(mainFlex, 0, 0);
    lv_obj_set_style_border_width(mainFlex, 0, 0);
    lv_obj_set_style_bg_opa(mainFlex, LV_OPA_TRANSP, 0);
    
    // Use Flex row layout: [Left Bar] [Center Area] [Right Bar]
    lv_obj_set_layout(mainFlex, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mainFlex, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mainFlex, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // 1. Create Left Mixer Bar (Fixed Width)
    mLeftBar = lv_obj_create(mainFlex);
    lv_obj_set_size(mLeftBar, 80, 600); // Narrower
    lv_obj_set_style_pad_all(mLeftBar, 5, 0);
    lv_obj_set_style_border_width(mLeftBar, 0, 0);
    lv_obj_set_style_bg_color(mLeftBar, lv_color_hex(0x1e1e1e), 0);
    lv_obj_set_style_radius(mLeftBar, 0, 0);
    lv_obj_set_layout(mLeftBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mLeftBar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mLeftBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 2. Create Center Content Area (Flexible Width)
    mCenterArea = lv_obj_create(mainFlex);
    lv_obj_set_flex_grow(mCenterArea, 1); // Grow to take remaining space
    lv_obj_set_height(mCenterArea, 600);
    lv_obj_set_style_pad_all(mCenterArea, 0, 0);
    lv_obj_set_style_border_width(mCenterArea, 0, 0);
    lv_obj_set_style_bg_color(mCenterArea, lv_color_hex(0x121212), 0); // Pure dark mode
    lv_obj_set_style_radius(mCenterArea, 0, 0);

    // 3. Create Right Nav Bar (Fixed Width)
    mRightBar = lv_obj_create(mainFlex);
    lv_obj_set_size(mRightBar, 100, 600); // Narrower
    lv_obj_set_style_pad_all(mRightBar, 5, 0);
    lv_obj_set_style_border_width(mRightBar, 0, 0);
    lv_obj_set_style_bg_color(mRightBar, lv_color_hex(0x1e1e1e), 0);
    lv_obj_set_style_radius(mRightBar, 0, 0);
    lv_obj_set_layout(mRightBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mRightBar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mRightBar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Populate the sections
    createLeftMixerBar();
    createRightNavBar();
    createCenterContentArea();
    
    // Set initial highlighting
    updateHighlighting();

    // Auto-load Init project if it exists at startup
    const char* browseDir = getenv("HOME");
    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
    std::string initLoomPath = homeStr + "/projects/Init.loom";
    mSettingsPadCount = 16;
    mSettingsFilePath = initLoomPath + ".settings";
    std::ifstream f(initLoomPath);
    if (f.good()) {
        f.close();
        std::cout << "Auto-loading Init project: " << initLoomPath << std::endl;
        mEngine.loadProject(initLoomPath);
        loadSettings(mSettingsFilePath);
    }
    mNeedsScreenRebuild = true;

    // Load custom FM presets persistently
    std::string presetsFmDir = homeStr + "/presets/fm";
    mkdir(presetsFmDir.c_str(), 0777); // ensure it exists
    for (int t = 0; t < 8; ++t) {
        auto& fmEngine = mEngine.getTracks()[t].fmEngine;
        fmEngine.mCustomPresets.clear();
        DIR* dir = opendir(presetsFmDir.c_str());
        if (dir) {
            struct dirent* entry;
            std::vector<std::string> presetFiles;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name.rfind(".", 0) == 0) continue;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".fmp") {
                    presetFiles.push_back(name);
                }
            }
            closedir(dir);
            // Sort files so they are always loaded in alphabetical order
            std::sort(presetFiles.begin(), presetFiles.end());
            for (const auto& file : presetFiles) {
                fmEngine.importPreset(presetsFmDir + "/" + file);
            }
        }
    }
}

lv_color_t UIManager::getTrackColor(int trackIndex) {
    // Standard Loom engine colors
    switch (trackIndex) {
        case 0: return lv_color_hex(0xFF4500); // Orange Red
        case 1: return lv_color_hex(0x32CD32); // Lime Green
        case 2: return lv_color_hex(0x1E90FF); // Dodger Blue
        case 3: return lv_color_hex(0xFFD700); // Gold
        case 4: return lv_color_hex(0x8A2BE2); // Blue Violet
        case 5: return lv_color_hex(0xFF1493); // Deep Pink
        case 6: return lv_color_hex(0x00FFFF); // Cyan
        case 7: return lv_color_hex(0xFFFFFF); // White
        default: return lv_color_hex(0x808080); // Gray
    }
}

void UIManager::createLeftMixerBar() {
    for (int i = 0; i < 8; ++i) {
        lv_obj_t* btn = lv_button_create(mLeftBar);
        lv_obj_set_size(btn, 70, 65);
        lv_obj_set_style_bg_color(btn, getTrackColor(i), 0);
        lv_obj_set_style_bg_opa(btn, mTrackEnabled[i] ? LV_OPA_50 : LV_OPA_10, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        
        // Setup border for highlighting
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        // Disable standard button layout and clear all padding to allow exact absolute positioning/alignment
        lv_obj_set_layout(btn, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        // Create track number label (top-left aligned with small offset inside rounded border)
        lv_obj_t* numLabel = lv_label_create(btn);
        lv_label_set_text_fmt(numLabel, "%d", i + 1);
        lv_obj_set_style_text_font(numLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(numLabel, lv_color_hex(0xBBBBBB), 0);
        lv_obj_align(numLabel, LV_ALIGN_TOP_LEFT, 6, 4);

        // Create engine icon label (centered, slightly below center for visual balance)
        lv_obj_t* iconLabel = lv_label_create(btn);
        lv_label_set_text(iconLabel, "");
        lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(iconLabel, LV_ALIGN_CENTER, 0, 6);
        
        lv_obj_add_event_cb(btn, trackBtnEventCb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn, trackBtnEventCb, LV_EVENT_LONG_PRESSED, this);
        mTrackButtons[i] = btn;
    }
}

void UIManager::createRightNavBar() {
    const char* navLabels[] = {"Param", "FX", "Seq", "Arp", "Assign", LV_SYMBOL_SETTINGS, "Mix/Rec"};
    for (int i = 0; i < 7; ++i) {
        lv_obj_t* btn = lv_button_create(mRightBar);
        lv_obj_set_size(btn, 90, 75); // Slightly smaller to fit 7
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        
        // Setup border for highlighting
        lv_obj_set_style_border_color(btn, lv_color_hex(0x44AAFF), 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, navLabels[i]);
        if (i == 5) {
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        }
        lv_obj_center(label);
        
        lv_obj_add_event_cb(btn, navBtnEventCb, LV_EVENT_CLICKED, this);
        mNavButtons[i] = btn;
    }
}

bool UIManager::isTrackPlaying(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= 8) return false;
    const auto& track = mEngine.getTracks()[trackIdx];
    if (!track.isTrackEnabled) return false;
    
    // Check if the synth voice is rendering sound, or any note is gate-active
    if (track.isActive) return true;
    if (mEngine.getActiveNoteMask(trackIdx) != 0) return true;
    
    // Check if Audio In engine is selected (it is always processing if enabled)
    if (track.engineType == 8) return true;
    
    // Check if arpeggiator has active notes
    if (track.arpeggiator.getMode() != ArpMode::OFF && !track.arpeggiator.getNotes().empty()) return true;
    
    // Check if sequence is playing notes
    if (mEngine.getIsPlaying()) {
        // Sequencer is running, check if track has active steps or triggers
        const auto& seq = track.sequencer;
        if (seq.getLoopLength() > 0) {
            const auto& steps = seq.getSteps();
            for (int s = 0; s < seq.getLoopLength(); ++s) {
                if (steps[s].active && !steps[s].notes.empty()) return true;
            }
        }
        // Also check drum sequencers if FmDrum/AnalogDrum
        if (track.engineType == 5 || track.engineType == 6) {
            for (int d = 0; d < 16; ++d) {
                const auto& dseq = track.drumSequencers[d];
                if (dseq.getLoopLength() > 0) {
                    const auto& dsteps = dseq.getSteps();
                    for (int s = 0; s < dseq.getLoopLength(); ++s) {
                        if (dsteps[s].active && !dsteps[s].notes.empty()) return true;
                    }
                }
            }
        }
    }
    
    return false;
}

void UIManager::updateHighlighting() {
    uint32_t tMs = lv_tick_get();
    float bpm = mEngine.getBpm();
    if (bpm < 1.0f) bpm = 80.0f;

    for (int i = 0; i < 8; ++i) {
        const auto& track = mEngine.getTracks()[i];
        bool isPlaying = isTrackPlaying(i);

        if (i == mActiveTrack) {
            lv_obj_set_style_border_width(mTrackButtons[i], 3, 0);
            lv_obj_set_style_border_color(mTrackButtons[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_opa(mTrackButtons[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_opa(mTrackButtons[i], LV_OPA_COVER, 0);
        } else if (isPlaying) {
            // Pulse the thin border
            float speedMultiplier = 1.0f;
            if (track.arpeggiator.getMode() != ArpMode::OFF) {
                speedMultiplier = track.arpeggiator.getSpeedMultiplier();
            } else {
                speedMultiplier = track.mClockMultiplier;
            }
            if (speedMultiplier < 0.01f) speedMultiplier = 1.0f;

            float periodMs = 60000.0f / (bpm * speedMultiplier);
            float phase = (2.0f * M_PI * tMs) / periodMs;
            float pulseVal = (sinf(phase) + 1.0f) * 0.5f;

            int borderOpa = (int)(40 + pulseVal * 215); // ranges from 40 to 255
            lv_obj_set_style_border_width(mTrackButtons[i], 1, 0);
            lv_obj_set_style_border_color(mTrackButtons[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_opa(mTrackButtons[i], borderOpa, 0);
            lv_obj_set_style_bg_opa(mTrackButtons[i], mTrackEnabled[i] ? LV_OPA_50 : LV_OPA_10, 0);
        } else {
            lv_obj_set_style_border_width(mTrackButtons[i], 0, 0);
            lv_obj_set_style_bg_opa(mTrackButtons[i], mTrackEnabled[i] ? LV_OPA_50 : LV_OPA_10, 0);
        }

        // Dynamically update track buttons with corresponding built-in FontAwesome icons and highlighted numbers
        lv_obj_t* numLabel = lv_obj_get_child(mTrackButtons[i], 0);
        if (numLabel) {
            lv_obj_set_style_text_color(numLabel, (i == mActiveTrack) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xBBBBBB), 0);
        }

        lv_obj_t* iconLabel = lv_obj_get_child(mTrackButtons[i], 1);
        if (iconLabel) {
            int engineType = mEngine.getTracks()[i].engineType;
            const char* icon = LV_SYMBOL_KEYBOARD;
            switch (engineType) {
                case 0: icon = LV_SYMBOL_KEYBOARD; break; // Subtractive (synth keyboard 🎹)
                case 1: icon = LV_SYMBOL_BELL;     break; // FM Synth (bell/chime 🔔)
                case 2: icon = LV_SYMBOL_LOOP;     break; // Sampler (sample looping 🔁)
                case 3: icon = LV_SYMBOL_SHUFFLE;  break; // Granular (random grains 🔀)
                case 4: icon = LV_SYMBOL_TINT;     break; // Wavetable (waveform drop/morphing 💧)
                case 5: icon = LV_SYMBOL_WARNING;  break; // FM Drum (percussive strike/impact ⚠️)
                case 6: icon = LV_SYMBOL_CHARGE;   break; // Analog Drum (voltage charge/trigger ⚡)
                case 8: icon = LV_SYMBOL_PLAY;     break; // Audio In (audio signal input ▶)
                case 9: icon = LV_SYMBOL_AUDIO;    break; // SoundFont (polyphonic MIDI note 🎵)
                default: icon = LV_SYMBOL_KEYBOARD; break;
            }
            lv_label_set_text(iconLabel, icon);
        }
    }
    
    for (int i = 0; i < 7; ++i) {
        if (i == mActiveNav) {
            lv_obj_set_style_border_width(mNavButtons[i], 3, 0);
            lv_obj_set_style_bg_color(mNavButtons[i], lv_color_hex(0x555555), 0);
        } else {
            lv_obj_set_style_border_width(mNavButtons[i], 0, 0);
            lv_obj_set_style_bg_color(mNavButtons[i], lv_color_hex(0x333333), 0);
        }
    }

    // Dynamic Mixer track highlighting
    for (int i = 0; i < 8; ++i) {
        if (mMixerCards[i]) {
            if (i == mActiveTrack) {
                lv_obj_set_style_border_color(mMixerCards[i], getTrackColor(i), 0);
                lv_obj_set_style_border_width(mMixerCards[i], 3, 0);
                lv_obj_set_style_bg_opa(mMixerCards[i], LV_OPA_20, 0);
            } else {
                lv_obj_set_style_border_color(mMixerCards[i], lv_color_hex(0x444444), 0);
                lv_obj_set_style_border_width(mMixerCards[i], 1, 0);
                lv_obj_set_style_bg_opa(mMixerCards[i], LV_OPA_10, 0);
            }
        }
    }
    
    // Force Left Mixer Bar redraw to reflect highlights immediately
    if (mLeftBar) {
        lv_obj_invalidate(mLeftBar);
    }
}

void UIManager::trackBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    int clickedTrack = -1;
    for (int i = 0; i < 8; ++i) {
        if (ui->mTrackButtons[i] == btn) {
            clickedTrack = i;
            break;
        }
    }
    if (clickedTrack == -1) return;

    if (code == LV_EVENT_CLICKED) {
        if (ui->mLongPressedTrack) {
            ui->mLongPressedTrack = false;
            return;
        }
        ui->mActiveTrack = clickedTrack;
        ui->updateHighlighting();
        // Refresh the active screen so step colors + themed elements match the new track
        if (ui->mActiveNav == 0 || ui->mActiveNav == 1 || ui->mActiveNav == 2 ||
            ui->mActiveNav == 3 || ui->mActiveNav == 4 || ui->mActiveNav == 5) {
            ui->createCenterContentArea();
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ui->mLongPressedTrack = true;
        ui->openMixerPopup(clickedTrack);
    }
}

void UIManager::navBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    for (int i = 0; i < 7; ++i) {
        if (ui->mNavButtons[i] == btn) {
            if (i != 5) {
                ui->mSettingsActiveTabIdx = 0;
            }
            ui->mActiveNav = i;
            break;
        }
    }
    ui->updateHighlighting();
    ui->createCenterContentArea(); // Re-render the center area
}

void UIManager::createCenterContentArea() {
    // Save settings active tab if settings tabview exists
    if (mSettingsTabview) {
        mSettingsActiveTabIdx = lv_tabview_get_tab_active(mSettingsTabview);
    }
    // Save assign active tab if assign tabview exists
    if (mAssignTabview) {
        mAssignActiveTabIdx = lv_tabview_get_tab_active(mAssignTabview);
    }
    // Save param active tab if param tabview exists
    if (mParamTabview) {
        mParamActiveTabIdx = lv_tabview_get_tab_active(mParamTabview);
    }

    int currentEngineType = mEngine.getTracks()[mActiveTrack].engineType;
    if (mActiveTrack != mLastActiveTrack || mActiveNav != mLastActiveNav || currentEngineType != mLastEngineType) {
        mParamActiveTabIdx = 0;
        mLastActiveTrack = mActiveTrack;
        mLastActiveNav = mActiveNav;
        mLastEngineType = currentEngineType;
    }

    // Safely close and delete all active popup modals to prevent them from getting stuck when switching screens
    if (mRemapModal) { lv_obj_delete(mRemapModal); mRemapModal = nullptr; }
    if (mModDestModal) { lv_obj_delete(mModDestModal); mModDestModal = nullptr; }
    if (mPedalPickerModal) { lv_obj_delete(mPedalPickerModal); mPedalPickerModal = nullptr; }
    if (mMixerModal) { lv_obj_delete(mMixerModal); mMixerModal = nullptr; }
    if (mMacroLearnModal) { lv_obj_delete(mMacroLearnModal); mMacroLearnModal = nullptr; }
    if (mStepModal) { lv_obj_delete(mStepModal); mStepModal = nullptr; }
    if (mSoundFontPresetModal) { lv_obj_delete(mSoundFontPresetModal); mSoundFontPresetModal = nullptr; }
    if (mFmPresetModal) { lv_obj_delete(mFmPresetModal); mFmPresetModal = nullptr; }
    if (mSettingsCreditsModal) { lv_obj_delete(mSettingsCreditsModal); mSettingsCreditsModal = nullptr; }
    if (mSettingsFxSelectModal) { lv_obj_delete(mSettingsFxSelectModal); mSettingsFxSelectModal = nullptr; }
    if (mSeqModal) { lv_obj_delete(mSeqModal); mSeqModal = nullptr; }

    // Clear existing center area
    lv_obj_clean(mCenterArea);

    mSettingsTabview = nullptr;
    mAssignTabview = nullptr;
    mParamTabview = nullptr;

    mSamplerWaveformContainer = nullptr;
    mSamplerStartLine = nullptr;
    mSamplerEndLine = nullptr;
    mSamplerRecordBtn = nullptr;
    mSamplerLatchBtn = nullptr;
    mSamplerScrubHandle = nullptr;
    std::fill(std::begin(mSamplerWaveformBars), std::end(mSamplerWaveformBars), nullptr);
    std::fill(std::begin(mSamplerPlayheadLines), std::end(mSamplerPlayheadLines), nullptr);
    std::fill(std::begin(mSamplerPlayheadShades), std::end(mSamplerPlayheadShades), nullptr);
    std::fill(std::begin(mSamplerSliceLines), std::end(mSamplerSliceLines), nullptr);
    std::fill(std::begin(mSamplerSliceHandles), std::end(mSamplerSliceHandles), nullptr);

    mGranularWaveformContainer = nullptr;
    mGranularStartLine = nullptr;
    mGranularEndLine = nullptr;
    mGranularPlayheadLine = nullptr;
    mGranularPlayheadShade = nullptr;
    mGranularRecordBtn = nullptr;
    mGranularLatchBtn = nullptr;
    mGranularLockBtn = nullptr;
    std::fill(std::begin(mGranularWaveformBars), std::end(mGranularWaveformBars), nullptr);

    mActiveParamWidgets.clear();
    mActiveFxWidgets.clear();
    mMidiLearnBtnLabel = nullptr;

    // Nullify sequencer step button pointers since they were deleted
    for (int i = 0; i < 64; ++i) {
        mSeqStepButtons[i] = nullptr;
    }

    if (mActiveNav == 0) { // Param
        populateParamScreen();
        return;
    }

    if (mActiveNav == 1) { // FX
        populateFxScreen();
        return;
    }

    if (mActiveNav == 2) { // Seq
        populateSeqScreen();
        return;
    }

    if (mActiveNav == 3) { // Arp
        populateArpScreen();
        return;
    }

    if (mActiveNav == 4) { // Assign
        populateAssignScreen();
        return;
    }

    if (mActiveNav == 6) { // Mix/Rec
        populateMixRecScreen();
        return;
    }

    if (mActiveNav == 5) { // Set
        populateSettingsScreen();
        return;
    }

    // Default tabview for other screens
    lv_obj_t* tabview = lv_tabview_create(mCenterArea);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);
    
    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "Page 1");
    lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "Page 2");

    lv_obj_t* label1 = lv_label_create(tab1);
    lv_label_set_text_fmt(label1, "Center Content Area - Menu %d, Page 1", mActiveNav);
    lv_obj_center(label1);
}

void UIManager::populateArpScreen() {
    lv_obj_t* tabview = lv_tabview_create(mCenterArea);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);
    
    // Set active track theme color for the tab indicator line
    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, getTrackColor(mActiveTrack), LV_PART_INDICATOR);
    
    // Set modern dark look for the tabview
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tabview, 0, 0);

    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "Settings");
    lv_obj_t* tab3 = lv_tabview_add_tab(tabview, "Pattern");

    // Style the individual tab buttons in the tab bar
    for(uint32_t i = 0; i < lv_obj_get_child_count(tab_bar); i++) {
        lv_obj_t* btn = lv_obj_get_child(tab_bar, i);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_color(btn, getTrackColor(mActiveTrack), LV_STATE_CHECKED);
    }

    // Clear standard tab padding to give us full screen area
    lv_obj_set_style_pad_all(tab1, 10, 0);
    lv_obj_set_style_pad_all(tab3, 10, 0);

    // =========================================================================
    // --- Tab 1: Settings (3-Column Dashboard Card Layout) ---
    // =========================================================================
    lv_obj_set_flex_flow(tab1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tab1, 20, 0);

    lv_color_t trackColor = getTrackColor(mActiveTrack);
    // Card background & border style helper
    auto applyCardStyle = [trackColor](lv_obj_t* card) {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 15, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    };

    // --- Column 1: Note Arpeggiator ---
    lv_obj_t* col1 = lv_obj_create(tab1);
    lv_obj_set_size(col1, 240, 460);
    applyCardStyle(col1);

    lv_obj_t* title1 = lv_label_create(col1);
    lv_label_set_text(title1, "NOTE ARPEGGIATOR");
    lv_obj_set_style_text_font(title1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title1, getTrackColor(mActiveTrack), 0); // Active track theme color

    const auto& arp = mEngine.getTracks()[mActiveTrack].arpeggiator;
    bool isArpOn = arp.getMode() != ArpMode::OFF;

    // Arp ON/OFF Toggle Button
    mArpToggleBtn = lv_button_create(col1);
    lv_obj_set_size(mArpToggleBtn, 200, 38);
    lv_obj_add_flag(mArpToggleBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(mArpToggleBtn, 8, 0);
    lv_obj_t* toggleLbl = lv_label_create(mArpToggleBtn);
    if (isArpOn) {
        lv_obj_add_state(mArpToggleBtn, LV_STATE_CHECKED);
        lv_label_set_text(toggleLbl, "Arpeggiator: ON");
        lv_obj_set_style_bg_color(mArpToggleBtn, getTrackColor(mActiveTrack), 0);
    } else {
        lv_obj_set_style_bg_color(mArpToggleBtn, lv_color_hex(0x444444), 0);
        lv_label_set_text(toggleLbl, "Arpeggiator: OFF");
    }
    lv_obj_center(toggleLbl);
    lv_obj_add_event_cb(mArpToggleBtn, arpToggleBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Note Pattern
    lv_obj_t* patternGrp = lv_obj_create(col1);
    lv_obj_set_size(patternGrp, 210, 75);
    lv_obj_set_style_bg_opa(patternGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(patternGrp, 0, 0);
    lv_obj_set_style_pad_all(patternGrp, 0, 0);
    lv_obj_set_layout(patternGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(patternGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(patternGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* patternLbl = lv_label_create(patternGrp);
    lv_label_set_text(patternLbl, "Pattern");
    lv_obj_set_style_text_font(patternLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(patternLbl, lv_color_hex(0x888888), 0);
    
    mArpPatternDd = lv_dropdown_create(patternGrp);
    lv_dropdown_set_options(mArpPatternDd, "Up\nDown\nUp/Down\nStagger Up\nStagger Down\nRandom\nBach\nBrownian\nConverge\nDiverge");
    lv_obj_set_width(mArpPatternDd, 200);
    lv_obj_t* patternList = lv_dropdown_get_list(mArpPatternDd);
    lv_obj_set_style_max_height(patternList, 200, 0);
    int activeMode = static_cast<int>(arp.getMode());
    if (activeMode > 0) {
        lv_dropdown_set_selected(mArpPatternDd, activeMode - 1);
    } else {
        lv_dropdown_set_selected(mArpPatternDd, 0);
    }
    lv_obj_add_event_cb(mArpPatternDd, arpPatternDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Rate / Division
    lv_obj_t* rateGrp = lv_obj_create(col1);
    lv_obj_set_size(rateGrp, 210, 75);
    lv_obj_set_style_bg_opa(rateGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rateGrp, 0, 0);
    lv_obj_set_style_pad_all(rateGrp, 0, 0);
    lv_obj_set_layout(rateGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rateGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rateGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* rateLbl = lv_label_create(rateGrp);
    lv_label_set_text(rateLbl, "Rate / Division");
    lv_obj_set_style_text_font(rateLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rateLbl, lv_color_hex(0x888888), 0);

    mArpRateDd = lv_dropdown_create(rateGrp);
    lv_dropdown_set_options(mArpRateDd, "1/2.\n1/2\n1/2T\n1/4.\n1/4\n1/4T\n1/8.\n1/8\n1/8T\n1/16.\n1/16\n1/16T\n1/32.\n1/32\n1/32T\n1/48\n1/64");
    lv_obj_set_width(mArpRateDd, 200);
    lv_dropdown_set_selected(mArpRateDd, 4); // Default 1/4
    lv_obj_t* rateList = lv_dropdown_get_list(mArpRateDd);
    lv_obj_set_style_max_height(rateList, 200, 0);
    lv_obj_add_event_cb(mArpRateDd, arpRateDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Octaves Slider
    lv_obj_t* octGrp = lv_obj_create(col1);
    lv_obj_set_size(octGrp, 210, 75);
    lv_obj_set_style_bg_opa(octGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(octGrp, 0, 0);
    lv_obj_set_style_pad_all(octGrp, 0, 0);
    lv_obj_set_layout(octGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(octGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(octGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* octLbl = lv_label_create(octGrp);
    int activeOctaves = arp.getOctaves();
    lv_label_set_text_fmt(octLbl, "Octaves: %+d", activeOctaves);
    lv_obj_set_style_text_font(octLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(octLbl, lv_color_hex(0x888888), 0);

    mArpOctavesSlider = lv_slider_create(octGrp);
    lv_obj_set_size(mArpOctavesSlider, 185, 12);
    lv_slider_set_range(mArpOctavesSlider, -3, 3);
    lv_slider_set_value(mArpOctavesSlider, activeOctaves, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mArpOctavesSlider, getTrackColor(mActiveTrack), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mArpOctavesSlider, getTrackColor(mActiveTrack), LV_PART_KNOB);
    lv_obj_set_style_pad_hor(mArpOctavesSlider, 10, 0); // Prevent handle cutout at extremes
    lv_obj_set_user_data(mArpOctavesSlider, octLbl);
    lv_obj_add_event_cb(mArpOctavesSlider, octavesSliderEventCb, LV_EVENT_VALUE_CHANGED, this);


    // --- Column 2: Playback & Rhythm ---
    lv_obj_t* col2 = lv_obj_create(tab1);
    lv_obj_set_size(col2, 240, 460);
    applyCardStyle(col2);

    lv_obj_t* title2 = lv_label_create(col2);
    lv_label_set_text(title2, "PLAYBACK");
    lv_obj_set_style_text_font(title2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title2, getTrackColor(mActiveTrack), 0); // Active track theme color

    // Latch Button
    mArpLatchBtn = lv_button_create(col2);
    lv_obj_set_size(mArpLatchBtn, 200, 45);
    lv_obj_add_flag(mArpLatchBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(mArpLatchBtn, 8, 0);
    lv_obj_t* latchLbl = lv_label_create(mArpLatchBtn);
    if (arp.isLatched()) {
        lv_obj_add_state(mArpLatchBtn, LV_STATE_CHECKED);
        lv_label_set_text(latchLbl, "Latch: ON");
        lv_obj_set_style_bg_color(mArpLatchBtn, getTrackColor(mActiveTrack), 0);
    } else {
        lv_obj_set_style_bg_color(mArpLatchBtn, lv_color_hex(0x444444), 0);
        lv_label_set_text(latchLbl, "Latch: OFF");
    }
    lv_obj_center(latchLbl);
    lv_obj_add_event_cb(mArpLatchBtn, latchBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Strum Arc
    lv_obj_t* strumGrp = lv_obj_create(col2);
    lv_obj_set_size(strumGrp, 210, 120);
    lv_obj_set_style_bg_opa(strumGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(strumGrp, 0, 0);
    lv_obj_set_style_pad_all(strumGrp, 0, 0);
    lv_obj_set_layout(strumGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(strumGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(strumGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* strumLbl = lv_label_create(strumGrp);
    lv_label_set_text(strumLbl, "Strum");
    lv_obj_set_style_text_font(strumLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(strumLbl, lv_color_hex(0x888888), 0);

    mArpStrumArc = lv_arc_create(strumGrp);
    lv_obj_set_size(mArpStrumArc, 90, 90);
    lv_arc_set_range(mArpStrumArc, 0, 100);
    lv_obj_set_style_arc_color(mArpStrumArc, getTrackColor(mActiveTrack), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mArpStrumArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(mArpStrumArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mArpStrumArc, 0, LV_PART_KNOB);
    int activeStrum = static_cast<int>(arp.getStrum() * 100.0f);
    lv_arc_set_value(mArpStrumArc, activeStrum);
    
    lv_obj_t* strumValLbl = lv_label_create(mArpStrumArc);
    lv_label_set_text_fmt(strumValLbl, "%d%%", activeStrum);
    lv_obj_set_style_text_font(strumValLbl, &lv_font_montserrat_14, 0);
    lv_obj_center(strumValLbl);
    lv_obj_set_user_data(mArpStrumArc, strumValLbl);
    lv_obj_add_event_cb(mArpStrumArc, strumArcEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Probability Arc
    lv_obj_t* probGrp = lv_obj_create(col2);
    lv_obj_set_size(probGrp, 210, 120);
    lv_obj_set_style_bg_opa(probGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(probGrp, 0, 0);
    lv_obj_set_style_pad_all(probGrp, 0, 0);
    lv_obj_set_layout(probGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(probGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(probGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* probLbl = lv_label_create(probGrp);
    lv_label_set_text(probLbl, "Probability");
    lv_obj_set_style_text_font(probLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(probLbl, lv_color_hex(0x888888), 0);

    mArpProbArc = lv_arc_create(probGrp);
    lv_obj_set_size(mArpProbArc, 90, 90);
    lv_arc_set_range(mArpProbArc, 0, 100);
    lv_obj_set_style_arc_color(mArpProbArc, getTrackColor(mActiveTrack), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mArpProbArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(mArpProbArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mArpProbArc, 0, LV_PART_KNOB);
    int activeProb = static_cast<int>(arp.getProbability() * 100.0f);
    lv_arc_set_value(mArpProbArc, activeProb);

    lv_obj_t* probValLbl = lv_label_create(mArpProbArc);
    lv_label_set_text_fmt(probValLbl, "%d%%", activeProb);
    lv_obj_set_style_text_font(probValLbl, &lv_font_montserrat_14, 0);
    lv_obj_center(probValLbl);
    lv_obj_set_user_data(mArpProbArc, probValLbl);
    lv_obj_add_event_cb(mArpProbArc, probArcEventCb, LV_EVENT_VALUE_CHANGED, this);


    // --- Column 3: Chord Generator ---
    lv_obj_t* col3 = lv_obj_create(tab1);
    lv_obj_set_size(col3, 240, 460);
    applyCardStyle(col3);

    lv_obj_t* title3 = lv_label_create(col3);
    lv_label_set_text(title3, "CHORD GENERATOR");
    lv_obj_set_style_text_font(title3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title3, getTrackColor(mActiveTrack), 0); // Active track theme color

    // Chord Gen Button
    mArpChordGenBtn = lv_button_create(col3);
    lv_obj_set_size(mArpChordGenBtn, 200, 45);
    lv_obj_add_flag(mArpChordGenBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(mArpChordGenBtn, 8, 0);
    lv_obj_t* chEnLbl = lv_label_create(mArpChordGenBtn);
    if (arp.isChordProgEnabled()) {
        lv_obj_add_state(mArpChordGenBtn, LV_STATE_CHECKED);
        lv_label_set_text(chEnLbl, "Chord Gen: ON");
        lv_obj_set_style_bg_color(mArpChordGenBtn, getTrackColor(mActiveTrack), 0);
    } else {
        lv_obj_set_style_bg_color(mArpChordGenBtn, lv_color_hex(0x444444), 0);
        lv_label_set_text(chEnLbl, "Chord Gen: OFF");
    }
    lv_obj_center(chEnLbl);
    lv_obj_add_event_cb(mArpChordGenBtn, chEnBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Mood Dropdown
    lv_obj_t* moodGrp = lv_obj_create(col3);
    lv_obj_set_size(moodGrp, 210, 75);
    lv_obj_set_style_bg_opa(moodGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(moodGrp, 0, 0);
    lv_obj_set_style_pad_all(moodGrp, 0, 0);
    lv_obj_set_layout(moodGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(moodGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(moodGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* moodLbl = lv_label_create(moodGrp);
    lv_label_set_text(moodLbl, "Chord Mood");
    lv_obj_set_style_text_font(moodLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(moodLbl, lv_color_hex(0x888888), 0);

    mArpChordMoodDd = lv_dropdown_create(moodGrp);
    lv_dropdown_set_options(mArpChordMoodDd, "Calm\nHappy\nSad\nSpooky\nAngry\nExcited\nGrandiose\nTense\nEthereal\nRomantic\nMysterious\nUplifting\nMelancholy\nDark\nDreamy\nMajestic");
    lv_obj_set_width(mArpChordMoodDd, 200);
    lv_dropdown_set_selected(mArpChordMoodDd, arp.getChordProgMood());
    lv_obj_t* chMoodList = lv_dropdown_get_list(mArpChordMoodDd);
    lv_obj_set_style_max_height(chMoodList, 200, 0);
    lv_obj_add_event_cb(mArpChordMoodDd, arpChordMoodDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Complexity Dropdown
    lv_obj_t* compGrp = lv_obj_create(col3);
    lv_obj_set_size(compGrp, 210, 75);
    lv_obj_set_style_bg_opa(compGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compGrp, 0, 0);
    lv_obj_set_style_pad_all(compGrp, 0, 0);
    lv_obj_set_layout(compGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(compGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(compGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* compLbl = lv_label_create(compGrp);
    lv_label_set_text(compLbl, "Chord Complexity");
    lv_obj_set_style_text_font(compLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(compLbl, lv_color_hex(0x888888), 0);

    mArpChordComplexityDd = lv_dropdown_create(compGrp);
    lv_dropdown_set_options(mArpChordComplexityDd, "Simple\nComplex\nColtrane");
    lv_obj_set_width(mArpChordComplexityDd, 200);
    lv_dropdown_set_selected(mArpChordComplexityDd, arp.getChordProgComplexity());
    lv_obj_t* chCompList = lv_dropdown_get_list(mArpChordComplexityDd);
    lv_obj_set_style_max_height(chCompList, 200, 0);
    lv_obj_add_event_cb(mArpChordComplexityDd, arpChordComplexityDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Inversions Slider
    lv_obj_t* invGrp = lv_obj_create(col3);
    lv_obj_set_size(invGrp, 210, 75);
    lv_obj_set_style_bg_opa(invGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(invGrp, 0, 0);
    lv_obj_set_style_pad_all(invGrp, 0, 0);
    lv_obj_set_layout(invGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(invGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(invGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* invLbl = lv_label_create(invGrp);
    int activeInversion = arp.getInversion();
    lv_label_set_text_fmt(invLbl, "Inversions: %+d", activeInversion);
    lv_obj_set_style_text_font(invLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(invLbl, lv_color_hex(0x888888), 0);

    mArpInversionsSlider = lv_slider_create(invGrp);
    lv_obj_set_size(mArpInversionsSlider, 185, 12);
    lv_slider_set_range(mArpInversionsSlider, -3, 3);
    lv_slider_set_value(mArpInversionsSlider, activeInversion, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mArpInversionsSlider, getTrackColor(mActiveTrack), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mArpInversionsSlider, getTrackColor(mActiveTrack), LV_PART_KNOB);
    lv_obj_set_style_pad_hor(mArpInversionsSlider, 10, 0); // Prevent handle cutout at extremes
    lv_obj_set_user_data(mArpInversionsSlider, invLbl);
    lv_obj_add_event_cb(mArpInversionsSlider, inversionsSliderEventCb, LV_EVENT_VALUE_CHANGED, this);


    // =========================================================================
    // --- Tab 3: Pattern (Custom 16-Step Column Grid with Dividers) ---
    // =========================================================================
    lv_obj_set_layout(tab3, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab3, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab3, 20, 0);

    // 1. Grid Row Container (Holds labels + columns)
    lv_obj_t* gridRow = lv_obj_create(tab3);
    lv_obj_set_size(gridRow, 760, 245);
    lv_obj_set_style_bg_opa(gridRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gridRow, 0, 0);
    lv_obj_set_style_pad_all(gridRow, 0, 0);
    lv_obj_set_style_pad_column(gridRow, 6, 0);
    lv_obj_set_layout(gridRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gridRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create Row Label Column (Rt, +1, -1, etc.) at leftmost index
    lv_obj_t* lblCol = lv_obj_create(gridRow);
    lv_obj_set_size(lblCol, 30, 245);
    lv_obj_set_style_bg_opa(lblCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lblCol, 0, 0);
    lv_obj_set_style_pad_all(lblCol, 0, 0);
    lv_obj_set_layout(lblCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lblCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lblCol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 0-spacer at top matching column number height
    lv_obj_t* topSpacer = lv_label_create(lblCol);
    lv_label_set_text(topSpacer, " ");
    lv_obj_set_style_text_font(topSpacer, &lv_font_montserrat_12, 0);

    const char* rowLabels[] = {"+2", "+1", "Rt", "-1"};
    for (int r = 0; r < 4; ++r) {
        lv_obj_t* rl = lv_label_create(lblCol);
        lv_label_set_text(rl, rowLabels[r]);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rl, lv_color_hex(0xAAAAAA), 0);
    }

    // 2. Add the 16 step columns
    for (int c = 0; c < 16; ++c) {
        lv_obj_t* colCont = lv_obj_create(gridRow);
        lv_obj_set_size(colCont, 38, 245);
        lv_obj_set_style_bg_opa(colCont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(colCont, 0, 0);
        lv_obj_set_style_pad_all(colCont, 0, 0);
        lv_obj_set_layout(colCont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(colCont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(colCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Column label 1-16; beat-start columns (1,5,9,13) are bolder
        lv_obj_t* colNum = lv_label_create(colCont);
        lv_label_set_text_fmt(colNum, "%d", c + 1);
        bool isBeatStart = (c == 0 || c == 4 || c == 8 || c == 12);
        lv_obj_set_style_text_font(colNum, isBeatStart ? &lv_font_montserrat_14 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(colNum, isBeatStart ? lv_color_hex(0xCCCCCC) : lv_color_hex(0x666666), 0);

        // Create 4 buttons for the 4 rows
        for (int r = 0; r < 4; ++r) {
            lv_obj_t* btn = lv_button_create(colCont);
            lv_obj_set_size(btn, 36, 46);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
            lv_obj_set_style_radius(btn, 10, 0); // Soft rounded rectangle
            lv_obj_set_style_border_width(btn, 0, 0);

            // Inactive (default) background
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x252525), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

            // Row-specific checked colors
            lv_color_t checkedColor;
            if (r == 0) checkedColor = lv_color_hex(0x8A2BE2);      // +2 (Blue Violet)
            else if (r == 1) checkedColor = lv_color_hex(0x32CD32); // +1 (Lime Green)
            else if (r == 2) checkedColor = lv_color_hex(0x1E90FF); // Rt (Dodger Blue)
            else checkedColor = lv_color_hex(0xFF4500);             // -1 (Orange Red)

            const auto& rhythm = arp.getRhythm();
            bool isChecked = false;
            if (r < (int)rhythm.size() && c < (int)rhythm[r].size()) {
                isChecked = rhythm[r][c];
            }
            if (isChecked) {
                lv_obj_add_state(btn, LV_STATE_CHECKED);
            }

            lv_obj_set_style_bg_color(btn, checkedColor, LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);

            // Event Callback
            lv_obj_set_user_data(btn, (void*)(uintptr_t)(r * 16 + c));
            lv_obj_add_event_cb(btn, arpButtonEventCb, LV_EVENT_VALUE_CHANGED, this);
            
            mArpButtons[r][c] = btn;
        }

        // Add subtle 1px floating vertical line after every 4th column (except last)
        // Set to LV_OBJ_FLAG_FLOATING so it doesn't affect column spacing or flex layout
        if (c == 3 || c == 7 || c == 11) {
            lv_obj_t* sep = lv_obj_create(colCont);
            lv_obj_add_flag(sep, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(sep, 1, 200);
            lv_obj_set_style_bg_color(sep, lv_color_hex(0x222222), 0); // Subtle, muted gray
            lv_obj_set_style_border_width(sep, 0, 0);
            lv_obj_set_style_radius(sep, 0, 0);
            lv_obj_set_style_pad_all(sep, 0, 0);
            lv_obj_align(sep, LV_ALIGN_TOP_RIGHT, 3, 20); // Align 3px to the right of column container (exactly centered in the 6px flex gap)
        }
    }

    // 3. Bottom Row: Legend (Left) & Randomize Rhythm (Right)
    lv_obj_t* bottomRow = lv_obj_create(tab3);
    lv_obj_set_size(bottomRow, 760, 50);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Legend container
    lv_obj_t* legendCont = lv_obj_create(bottomRow);
    lv_obj_set_size(legendCont, 450, 40);
    lv_obj_set_style_bg_opa(legendCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legendCont, 0, 0);
    lv_obj_set_style_pad_all(legendCont, 0, 0);
    lv_obj_set_layout(legendCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(legendCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legendCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(legendCont, 20, 0);

    const char* legendTexts[] = {"1 Below", "Root", "1 Above", "2 Above"};
    lv_color_t legendColors[] = {
        lv_color_hex(0xFF4500), // 1 Below
        lv_color_hex(0x1E90FF), // Root
        lv_color_hex(0x32CD32), // 1 Above
        lv_color_hex(0x8A2BE2)  // 2 Above
    };

    for (int i = 0; i < 4; ++i) {
        // Draw tiny color dot
        lv_obj_t* dot = lv_obj_create(legendCont);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_bg_color(dot, legendColors[i], 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);

        lv_obj_t* txt = lv_label_create(legendCont);
        lv_label_set_text(txt, legendTexts[i]);
        lv_obj_set_style_text_font(txt, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(txt, legendColors[i], 0);
        lv_obj_set_style_text_decor(txt, LV_TEXT_DECOR_NONE, 0);
    }

    // Randomize Buttons Container (Right)
    lv_obj_t* randCont = lv_obj_create(bottomRow);
    lv_obj_set_size(randCont, 290, 40);
    lv_obj_set_style_bg_opa(randCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(randCont, 0, 0);
    lv_obj_set_style_pad_all(randCont, 0, 0);
    lv_obj_set_layout(randCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(randCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(randCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Rand Rhythm Button
    lv_obj_t* randRhyBtn = lv_button_create(randCont);
    lv_obj_set_size(randRhyBtn, 140, 40);
    lv_obj_set_style_bg_color(randRhyBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(randRhyBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(randRhyBtn, 1, 0);
    lv_obj_set_style_radius(randRhyBtn, 8, 0);
    
    lv_obj_t* randRhyLbl = lv_label_create(randRhyBtn);
    lv_label_set_text(randRhyLbl, "Rand Rhythm");
    lv_obj_set_style_text_font(randRhyLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(randRhyLbl);
    lv_obj_add_event_cb(randRhyBtn, randRhythmBtnEventCb, LV_EVENT_CLICKED, this);

    // Rand Notes Button
    lv_obj_t* randNotBtn = lv_button_create(randCont);
    lv_obj_set_size(randNotBtn, 140, 40);
    lv_obj_set_style_bg_color(randNotBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(randNotBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(randNotBtn, 1, 0);
    lv_obj_set_style_radius(randNotBtn, 8, 0);
    
    lv_obj_t* randNotLbl = lv_label_create(randNotBtn);
    lv_label_set_text(randNotLbl, "Rand Notes");
    lv_obj_set_style_text_font(randNotLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(randNotLbl);
    lv_obj_add_event_cb(randNotBtn, randNotesBtnEventCb, LV_EVENT_CLICKED, this);
}

// =========================================================================
// --- Settings Screen Event Callbacks ---
// =========================================================================

void UIManager::latchBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    bool isChecked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    if (isChecked) {
        lv_label_set_text(label, "Latch: ON");
        lv_obj_set_style_bg_color(btn, ui->getTrackColor(ui->mActiveTrack), 0);
    } else {
        lv_label_set_text(label, "Latch: OFF");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), 0);
    }
    ui->updateArpConfig();
}

void UIManager::strumArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    lv_label_set_text_fmt(label, "%" PRId32 "%%", val);
    ui->mEngine.setArpStrum(ui->mActiveTrack, (float)val / 100.0f);
}

void UIManager::probArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    lv_label_set_text_fmt(label, "%" PRId32 "%%", val);
    ui->updateArpConfig();
}

void UIManager::chEnBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    bool isChecked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    if (isChecked) {
        lv_label_set_text(label, "Chord Gen: ON");
        lv_obj_set_style_bg_color(btn, ui->getTrackColor(ui->mActiveTrack), 0);
    } else {
        lv_label_set_text(label, "Chord Gen: OFF");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), 0);
    }
    ui->updateChordConfig();
}

void UIManager::inversionsSliderEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = (lv_obj_t*)lv_obj_get_user_data(slider);
    int32_t val = lv_slider_get_value(slider);
    lv_label_set_text_fmt(label, "Inversions: %+" PRId32, val);
    ui->updateArpConfig();
}

void UIManager::octavesSliderEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = (lv_obj_t*)lv_obj_get_user_data(slider);
    int32_t val = lv_slider_get_value(slider);
    lv_label_set_text_fmt(label, "Octaves: %+" PRId32, val);
    ui->updateArpConfig();
}

void UIManager::updateArpConfig() {
    if (mActiveTrack < 0) return;
    int mode = 0;
    if (mArpToggleBtn && lv_obj_has_state(mArpToggleBtn, LV_STATE_CHECKED)) {
        if (mArpPatternDd) {
            mode = lv_dropdown_get_selected(mArpPatternDd) + 1;
        } else {
            mode = 1;
        }
    }
    int octaves = 0;
    if (mArpOctavesSlider) {
        octaves = lv_slider_get_value(mArpOctavesSlider);
    }
    int inversion = 0;
    if (mArpInversionsSlider) {
        inversion = lv_slider_get_value(mArpInversionsSlider);
    }
    bool isLatched = false;
    if (mArpLatchBtn) {
        isLatched = lv_obj_has_state(mArpLatchBtn, LV_STATE_CHECKED);
    }
    float probability = 1.0f;
    if (mArpProbArc) {
        probability = (float)lv_arc_get_value(mArpProbArc) / 100.0f;
    }
    std::vector<std::vector<bool>> rhythms(4, std::vector<bool>(16, false));
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (mArpButtons[r][c]) {
                rhythms[r][c] = lv_obj_has_state(mArpButtons[r][c], LV_STATE_CHECKED);
            }
        }
    }
    mEngine.setArpConfig(mActiveTrack, mode, octaves, inversion, isLatched, false,
                         rhythms, {}, std::vector<float>(16, 0.5f), probability, 0.0f);
}

void UIManager::updateChordConfig() {
    if (mActiveTrack < 0) return;
    bool enabled = false;
    if (mArpChordGenBtn) {
        enabled = lv_obj_has_state(mArpChordGenBtn, LV_STATE_CHECKED);
    }
    int mood = 0;
    if (mArpChordMoodDd) {
        mood = lv_dropdown_get_selected(mArpChordMoodDd);
    }
    int complexity = 0;
    if (mArpChordComplexityDd) {
        complexity = lv_dropdown_get_selected(mArpChordComplexityDd);
    }
    mEngine.setChordProgConfig(mActiveTrack, enabled, mood, complexity);
}

void UIManager::arpPatternDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->updateArpConfig();
}

void UIManager::arpRateDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui->mArpRateDd) return;
    int idx = lv_dropdown_get_selected(ui->mArpRateDd);
    float rate = 1.0f;
    int divisionMode = 0;
    switch (idx) {
        case 0: rate = 8.0f; divisionMode = 1; break;
        case 1: rate = 8.0f; divisionMode = 0; break;
        case 2: rate = 8.0f; divisionMode = 2; break;
        case 3: rate = 4.0f; divisionMode = 1; break;
        case 4: rate = 4.0f; divisionMode = 0; break;
        case 5: rate = 4.0f; divisionMode = 2; break;
        case 6: rate = 2.0f; divisionMode = 1; break;
        case 7: rate = 2.0f; divisionMode = 0; break;
        case 8: rate = 2.0f; divisionMode = 2; break;
        case 9: rate = 1.0f; divisionMode = 1; break;
        case 10: rate = 1.0f; divisionMode = 0; break;
        case 11: rate = 1.0f; divisionMode = 2; break;
        case 12: rate = 0.5f; divisionMode = 1; break;
        case 13: rate = 0.5f; divisionMode = 0; break;
        case 14: rate = 0.5f; divisionMode = 2; break;
        case 15: rate = 0.5f; divisionMode = 2; break;
        case 16: rate = 0.25f; divisionMode = 0; break;
        default: rate = 1.0f; divisionMode = 0; break;
    }
    ui->mEngine.setArpRate(ui->mActiveTrack, rate, divisionMode);
}

void UIManager::arpChordMoodDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->updateChordConfig();
}

void UIManager::arpChordComplexityDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->updateChordConfig();
}

void UIManager::arpToggleBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    bool isChecked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    if (isChecked) {
        lv_label_set_text(label, "Arpeggiator: ON");
        lv_obj_set_style_bg_color(btn, ui->getTrackColor(ui->mActiveTrack), 0);
    } else {
        lv_label_set_text(label, "Arpeggiator: OFF");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), 0);
    }
    ui->updateArpConfig();
}

// =========================================================================
// --- Pattern Grid Event Callbacks & Helpers ---
// =========================================================================

void UIManager::randRhythmBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->randomizeRhythm();
}

void UIManager::randNotesBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->randomizeNotes();
}

void UIManager::arpButtonEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    uintptr_t id = (uintptr_t)lv_obj_get_user_data(btn);
    int row = id / 16;
    int col = id % 16;
    bool checked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    std::cout << "Arp Grid toggled: Row " << row << ", Col " << col << ", State " << checked << std::endl;
    ui->updateArpConfig();
}

void UIManager::randomizeRhythm() {
    for (int col = 0; col < 16; ++col) {
        for (int row = 0; row < 4; ++row) {
            lv_obj_remove_state(mArpButtons[row][col], LV_STATE_CHECKED);
        }
        if ((rand() % 100) < 35) {
            lv_obj_add_state(mArpButtons[2][col], LV_STATE_CHECKED); // Root note row is row index 2
        }
    }
}

void UIManager::randomizeNotes() {
    for (int col = 0; col < 16; ++col) {
        for (int row = 0; row < 4; ++row) {
            lv_obj_remove_state(mArpButtons[row][col], LV_STATE_CHECKED);
        }
        if ((rand() % 100) < 35) {
            int activeRow = rand() % 4;
            lv_obj_add_state(mArpButtons[activeRow][col], LV_STATE_CHECKED);
        }
    }
}

void UIManager::populateSettingsScreen() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Create outer container
    lv_obj_t* container = lv_obj_create(mCenterArea);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_add_event_cb(container, settingsScreenDeleteEventCb, LV_EVENT_DELETE, this);

    // Tabview replaces old "LOOM WORKSTATION SETTINGS" header
    mSettingsTabview = lv_tabview_create(container);
    lv_tabview_set_tab_bar_position(mSettingsTabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(mSettingsTabview, 44);
    lv_obj_set_size(mSettingsTabview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(mSettingsTabview, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(mSettingsTabview, 0, 0);

    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(mSettingsTabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(tab_bar, 1, LV_PART_MAIN);

    lv_obj_t* tab1 = lv_tabview_add_tab(mSettingsTabview, "General");
    lv_obj_t* tab2 = lv_tabview_add_tab(mSettingsTabview, "MIDI Pads");
    lv_obj_t* tab3 = lv_tabview_add_tab(mSettingsTabview, "Knobs/Faders");
    lv_obj_t* tab4 = lv_tabview_add_tab(mSettingsTabview, "System");

    // Style the individual tab buttons in the tab bar
    for(uint32_t i = 0; i < lv_obj_get_child_count(tab_bar); i++) {
        lv_obj_t* btn = lv_obj_get_child(tab_bar, i);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_color(btn, trackColor, LV_STATE_CHECKED);
    }

    lv_obj_set_style_pad_all(tab1, 8, 0);
    lv_obj_set_style_pad_all(tab2, 8, 0);
    lv_obj_set_style_pad_all(tab3, 8, 0);
    lv_obj_set_style_pad_all(tab4, 8, 0);
    lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tab3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tab4, LV_OBJ_FLAG_SCROLLABLE);

    populateSettingsGeneralTab(tab1);
    populateSettingsMidiPadsTab(tab2);
    populateSettingsKnobsFadersTab(tab3);
    populateSettingsSystemTab(tab4);

    if (mSettingsActiveTabIdx > 0 && mSettingsActiveTabIdx < 4) {
        lv_tabview_set_active(mSettingsTabview, mSettingsActiveTabIdx, LV_ANIM_OFF);
    }
}

// ==========================================================================
// Tab 1: General
// ==========================================================================
void UIManager::populateSettingsGeneralTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto applyCardStyle = [trackColor](lv_obj_t* card) {
        lv_obj_set_size(card, 260, 480);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 15, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 10, 0);
    };

    // --- Column 1: Audio Engine ---
    lv_obj_t* audioCard = lv_obj_create(tab);
    applyCardStyle(audioCard);

    lv_obj_t* audioTitle = lv_label_create(audioCard);
    lv_label_set_text(audioTitle, "AUDIO ENGINE");
    lv_obj_set_style_text_font(audioTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(audioTitle, trackColor, 0);

    lv_obj_t* deviceLabel = lv_label_create(audioCard);
    lv_label_set_text(deviceLabel, "Device: SDL Default");
    lv_obj_set_style_text_font(deviceLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(deviceLabel, lv_color_hex(0xBBBBBB), 0);

    lv_obj_t* srLbl = lv_label_create(audioCard);
    lv_label_set_text(srLbl, "Sample Rate:");
    lv_obj_set_style_text_font(srLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* srDd = lv_dropdown_create(audioCard);
    lv_obj_set_size(srDd, 180, 36);
    lv_dropdown_set_options(srDd, "44100 Hz\n48000 Hz");
    lv_obj_set_style_bg_color(srDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(srDd, 1, 0);
    lv_obj_set_style_text_font(srDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(srDd, 6, 0);
    float currentSr = mEngine.getSampleRate();
    lv_dropdown_set_selected(srDd, (std::abs(currentSr - 48000.0f) < 100.0f) ? 1 : 0);
    lv_obj_add_event_cb(srDd, settingsSampleRateDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* bufferLabel = lv_label_create(audioCard);
    lv_label_set_text(bufferLabel, "Buffer: 256 samples\nLatency: ~5.3 ms");
    lv_obj_set_style_text_font(bufferLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bufferLabel, lv_color_hex(0xBBBBBB), 0);

    mCpuLoadLabel = lv_label_create(audioCard);
    lv_label_set_text_fmt(mCpuLoadLabel, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
    lv_obj_set_style_text_font(mCpuLoadLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mCpuLoadLabel, lv_color_hex(0x00FFCC), 0);

    // PANIC button
    lv_obj_t* panicBtn = lv_button_create(audioCard);
    lv_obj_set_size(panicBtn, 180, 36);
    lv_obj_set_style_bg_color(panicBtn, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_border_color(panicBtn, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_border_width(panicBtn, 1, 0);
    lv_obj_set_style_radius(panicBtn, 8, 0);
    lv_obj_t* panicLbl = lv_label_create(panicBtn);
    lv_label_set_text(panicLbl, "PANIC ALL OFF");
    lv_obj_set_style_text_font(panicLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(panicLbl);
    lv_obj_add_event_cb(panicBtn, settingsPanicBtnEventCb, LV_EVENT_CLICKED, this);

    // RESET MIDI / PATCHING button
    lv_obj_t* resetMidiBtn = lv_button_create(audioCard);
    lv_obj_set_size(resetMidiBtn, 180, 36);
    lv_obj_set_style_bg_color(resetMidiBtn, lv_color_hex(0x996633), 0);
    lv_obj_set_style_border_color(resetMidiBtn, lv_color_hex(0xCC9944), 0);
    lv_obj_set_style_border_width(resetMidiBtn, 1, 0);
    lv_obj_set_style_radius(resetMidiBtn, 8, 0);
    lv_obj_t* resetMidiLbl = lv_label_create(resetMidiBtn);
    lv_label_set_text(resetMidiLbl, "RESET MIDI / PATCHING");
    lv_obj_set_style_text_font(resetMidiLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(resetMidiLbl);
    lv_obj_add_event_cb(resetMidiBtn, settingsResetMidiBtnEventCb, LV_EVENT_CLICKED, this);

    // Audio Output Mode Dropdown
    lv_obj_t* outModeLbl = lv_label_create(audioCard);
    lv_label_set_text(outModeLbl, "Audio Output Mode:");
    lv_obj_set_style_text_font(outModeLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* outModeDd = lv_dropdown_create(audioCard);
    lv_obj_set_size(outModeDd, 180, 36);
    lv_dropdown_set_options(outModeDd, "Stereo\nMono (L-Only)\nPseudo-Stereo\nPhase-Invert");
    lv_obj_set_style_bg_color(outModeDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(outModeDd, 1, 0);
    lv_obj_set_style_text_font(outModeDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(outModeDd, 6, 0);
    lv_dropdown_set_selected(outModeDd, mEngine.getAudioOutputMode());

    // Description of selected mode
    lv_obj_t* outModeDesc = lv_label_create(audioCard);
    lv_obj_set_style_text_font(outModeDesc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(outModeDesc, lv_color_hex(0x888888), 0);
    
    // Store label pointer in the dropdown so the callback can access it without capturing
    lv_obj_set_user_data(outModeDd, outModeDesc);

    auto updateOutModeDesc = [](lv_obj_t* label, int mode) {
        switch (mode) {
            case 0:
                lv_label_set_text(label, "Stereo: Standard output\nfor headphones / stereo.");
                break;
            case 1:
                lv_label_set_text(label, "Mono (L-Only): Sums to L,\nmutes R. Prevents cancellation\non mono mixer channels.");
                break;
            case 2:
                lv_label_set_text(label, "Pseudo-Stereo: Adds 4ms\ndelay to R. Stereo width,\nno balanced cancellation.");
                break;
            case 3:
                lv_label_set_text(label, "Phase-Invert: Inverts R.\nDoubles volume on balanced\nmono TRS-to-TRS cables.");
                break;
        }
    };

    updateOutModeDesc(outModeDesc, mEngine.getAudioOutputMode());

    // Event callback for dropdown (capture-free, converts to standard C pointer)
    auto outModeDdCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* descLabel = (lv_obj_t*)lv_obj_get_user_data(dd);
        int selected = lv_dropdown_get_selected(dd);
        
        ui->mEngine.setAudioOutputMode(selected);
        
        if (descLabel) {
            switch (selected) {
                case 0:
                    lv_label_set_text(descLabel, "Stereo: Standard output\nfor headphones / stereo.");
                    break;
                case 1:
                    lv_label_set_text(descLabel, "Mono (L-Only): Sums to L,\nmutes R. Prevents cancellation\non mono mixer channels.");
                    break;
                case 2:
                    lv_label_set_text(descLabel, "Pseudo-Stereo: Adds 4ms\ndelay to R. Stereo width,\nno balanced cancellation.");
                    break;
                case 3:
                    lv_label_set_text(descLabel, "Phase-Invert: Inverts R.\nDoubles volume on balanced\nmono TRS-to-TRS cables.");
                    break;
            }
        }
    };
    
    lv_obj_add_event_cb(outModeDd, outModeDdCb, LV_EVENT_VALUE_CHANGED, this);

    // --- Column 2: MIDI Routing ---
    lv_obj_t* midiCard = lv_obj_create(tab);
    applyCardStyle(midiCard);

    lv_obj_t* midiTitle = lv_label_create(midiCard);
    lv_label_set_text(midiTitle, "MIDI ROUTING");
    lv_obj_set_style_text_font(midiTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(midiTitle, trackColor, 0);

    // Track selector dropdown
    lv_obj_t* trkLbl = lv_label_create(midiCard);
    lv_label_set_text(trkLbl, "Configure Track:");
    lv_obj_set_style_text_font(trkLbl, &lv_font_montserrat_10, 0);

    mSettingsMidiTrackDd = lv_dropdown_create(midiCard);
    lv_obj_set_size(mSettingsMidiTrackDd, 180, 36);
    lv_dropdown_set_options(mSettingsMidiTrackDd, "Track 1\nTrack 2\nTrack 3\nTrack 4\nTrack 5\nTrack 6\nTrack 7\nTrack 8");
    lv_obj_set_style_bg_color(mSettingsMidiTrackDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(mSettingsMidiTrackDd, 1, 0);
    lv_obj_set_style_text_font(mSettingsMidiTrackDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(mSettingsMidiTrackDd, 6, 0);
    mSettingsMidiTrackSelect = mActiveTrack;
    lv_dropdown_set_selected(mSettingsMidiTrackDd, mSettingsMidiTrackSelect);
    lv_obj_add_event_cb(mSettingsMidiTrackDd, settingsMidiTrackSelectDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // MIDI IN Dropdown
    lv_obj_t* midiInLbl = lv_label_create(midiCard);
    lv_label_set_text(midiInLbl, "MIDI Input Channel:");
    lv_obj_set_style_text_font(midiInLbl, &lv_font_montserrat_10, 0);

    mSettingsMidiInDd = lv_dropdown_create(midiCard);
    lv_obj_set_size(mSettingsMidiInDd, 180, 36);
    lv_dropdown_set_options(mSettingsMidiInDd, "NONE\nChannel 1\nChannel 2\nChannel 3\nChannel 4\nChannel 5\nChannel 6\nChannel 7\nChannel 8\nChannel 9\nChannel 10\nChannel 11\nChannel 12\nChannel 13\nChannel 14\nChannel 15\nChannel 16\nALL");
    lv_obj_set_style_bg_color(mSettingsMidiInDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(mSettingsMidiInDd, 1, 0);
    lv_obj_set_style_text_font(mSettingsMidiInDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(mSettingsMidiInDd, 6, 0);
    int currentInChan = mEngine.getTracks()[mSettingsMidiTrackSelect].midiInChannel;
    if (currentInChan >= 0 && currentInChan <= 17) {
        lv_dropdown_set_selected(mSettingsMidiInDd, currentInChan);
    }
    lv_obj_add_event_cb(mSettingsMidiInDd, settingsMidiInChannelDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Routing description
    lv_obj_t* routeDesc = lv_label_create(midiCard);
    lv_label_set_text(routeDesc, "ALL = respond when\ntrack is selected.\nSpecific ch = always\nrespond on that ch.");
    lv_obj_set_style_text_font(routeDesc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(routeDesc, lv_color_hex(0x888888), 0);

    // MIDI OUT Dropdown
    lv_obj_t* midiOutLbl = lv_label_create(midiCard);
    lv_label_set_text(midiOutLbl, "MIDI Output Channel:");
    lv_obj_set_style_text_font(midiOutLbl, &lv_font_montserrat_10, 0);

    mSettingsMidiOutDd = lv_dropdown_create(midiCard);
    lv_obj_set_size(mSettingsMidiOutDd, 180, 36);
    lv_dropdown_set_options(mSettingsMidiOutDd, "NONE\nChannel 1\nChannel 2\nChannel 3\nChannel 4\nChannel 5\nChannel 6\nChannel 7\nChannel 8\nChannel 9\nChannel 10\nChannel 11\nChannel 12\nChannel 13\nChannel 14\nChannel 15\nChannel 16");
    lv_obj_set_style_bg_color(mSettingsMidiOutDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(mSettingsMidiOutDd, 1, 0);
    lv_obj_set_style_text_font(mSettingsMidiOutDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(mSettingsMidiOutDd, 6, 0);
    int currentOutChan = mEngine.getTracks()[mSettingsMidiTrackSelect].midiOutChannel;
    if (currentOutChan >= 0 && currentOutChan <= 16) {
        lv_dropdown_set_selected(mSettingsMidiOutDd, currentOutChan);
    }
    lv_obj_add_event_cb(mSettingsMidiOutDd, settingsMidiOutChannelDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Velocity Sensitivity switch
    lv_obj_t* velSensRow = lv_obj_create(midiCard);
    lv_obj_set_size(velSensRow, 220, 36);
    lv_obj_set_style_bg_opa(velSensRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(velSensRow, 0, 0);
    lv_obj_set_style_pad_all(velSensRow, 0, 0);
    lv_obj_remove_flag(velSensRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(velSensRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(velSensRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(velSensRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* velSensLbl = lv_label_create(velSensRow);
    lv_label_set_text(velSensLbl, "VELOCITY SENS:");
    lv_obj_set_style_text_font(velSensLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(velSensLbl, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t* velSensSw = lv_switch_create(velSensRow);
    lv_obj_set_size(velSensSw, 40, 20);
    if (mEngine.getVelocitySensitivityEnabled()) {
        lv_obj_add_state(velSensSw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(velSensSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    
    auto velSensCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        bool isChecked = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        ui->mEngine.setVelocitySensitivityEnabled(isChecked);
    };
    lv_obj_add_event_cb(velSensSw, velSensCb, LV_EVENT_VALUE_CHANGED, this);

    // --- Column 3: Project & System ---
    lv_obj_t* systemCard = lv_obj_create(tab);
    applyCardStyle(systemCard);

    lv_obj_t* systemTitle = lv_label_create(systemCard);
    lv_label_set_text(systemTitle, "PROJECT FILES");
    lv_obj_set_style_text_font(systemTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(systemTitle, trackColor, 0);

    // Stacked buttons for project management in the top half
    auto makeFileBtn = [trackColor](lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* userData) {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, 180, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(btn, trackColor, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
        return btn;
    };

    makeFileBtn(systemCard, LV_SYMBOL_FILE " NEW PROJECT", settingsNewBtnEventCb, this);
    makeFileBtn(systemCard, LV_SYMBOL_SAVE " SAVE PROJECT", settingsSaveBtnEventCb, this);
    makeFileBtn(systemCard, LV_SYMBOL_DIRECTORY " LOAD PROJECT", settingsLoadBtnEventCb, this);

    // Small space for QWERTY keyboard mode
    lv_obj_t* kbTitle = lv_label_create(systemCard);
    lv_label_set_text(kbTitle, "QWERTY KEYBOARD");
    lv_obj_set_style_text_font(kbTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(kbTitle, trackColor, 0);
    lv_obj_set_style_margin_top(kbTitle, 10, 0);

    lv_obj_t* kbRow = lv_obj_create(systemCard);
    lv_obj_set_size(kbRow, 220, 36);
    lv_obj_set_style_bg_opa(kbRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kbRow, 0, 0);
    lv_obj_set_style_pad_all(kbRow, 0, 0);
    lv_obj_remove_flag(kbRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(kbRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(kbRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(kbRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* kbLbl = lv_label_create(kbRow);
    lv_label_set_text(kbLbl, "KEYBOARD MODE:");
    lv_obj_set_style_text_font(kbLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(kbLbl, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t* kbSw = lv_switch_create(kbRow);
    lv_obj_set_size(kbSw, 40, 20);
    if (mSettingsKeyboardMode) lv_obj_add_state(kbSw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kbSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(kbSw, settingsKeyboardModeSwitchEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Fast Granular (Linear) switch
    lv_obj_t* fastGranRow = lv_obj_create(systemCard);
    lv_obj_set_size(fastGranRow, 220, 36);
    lv_obj_set_style_bg_opa(fastGranRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fastGranRow, 0, 0);
    lv_obj_set_style_pad_all(fastGranRow, 0, 0);
    lv_obj_remove_flag(fastGranRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(fastGranRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(fastGranRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fastGranRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* fastGranLbl = lv_label_create(fastGranRow);
    lv_label_set_text(fastGranLbl, "FAST GRANULAR:");
    lv_obj_set_style_text_font(fastGranLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(fastGranLbl, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t* fastGranSw = lv_switch_create(fastGranRow);
    lv_obj_set_size(fastGranSw, 40, 20);
    if (mEngine.getFastGranularEnabled()) lv_obj_add_state(fastGranSw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(fastGranSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    
    auto fastGranSwCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        bool isChecked = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        ui->mEngine.setFastGranularEnabled(isChecked);
    };
    lv_obj_add_event_cb(fastGranSw, fastGranSwCb, LV_EVENT_VALUE_CHANGED, this);

    // Credits/Privacy button at the bottom right/center
    lv_obj_t* credBtn = lv_button_create(systemCard);
    lv_obj_set_size(credBtn, 180, 36);
    lv_obj_set_style_bg_color(credBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(credBtn, trackColor, 0);
    lv_obj_set_style_border_width(credBtn, 1, 0);
    lv_obj_set_style_radius(credBtn, 8, 0);
    lv_obj_set_style_margin_top(credBtn, 10, 0);
    lv_obj_t* credBtnLbl = lv_label_create(credBtn);
    lv_label_set_text(credBtnLbl, LV_SYMBOL_LIST " CREDITS / PRIVACY");
    lv_obj_set_style_text_font(credBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(credBtnLbl);
    lv_obj_add_event_cb(credBtn, settingsCreditsBtnEventCb, LV_EVENT_CLICKED, this);
}

// ==========================================================================
// Tab 2: MIDI Pads
// ==========================================================================

static const char* kFxNames[] = {
    "REVERB", "DELAY", "CHORUS", "PHASER", "OVERDRIVE", "BITCRUSHER",
    "COMPRESSOR", "FLANGER", "TAPE ECHO", "TAPE WOBBLE", "SLICER",
    "LP LFO", "HP LFO", "FILTER 1", "FILTER 2", "FILTER 3", "OCTAVER", "EQ"
};
static const int kFxSendParamIds[] = {
    2060, 2050, 2020, 2030, 2000, 2010,
    2080, 2110, 2130, 2040, 2070,
    2100, 2090, 2120, 2150, 2160, 2140, 1535
};
static const int kNumFxSlots = 18;

static const char* kFmDrumNames[] = { "KICK", "SNARE", "TOM", "HIHAT", "HIHAT OP", "CYMBAL", "PERC", "NOISE" };
static const char* kAnalogDrumNames[] = { "KICK", "SNARE", "CLAP", "HIHAT CL", "HIHAT OP", "CYMBAL", "PERC", "NOISE" };
static const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void UIManager::populateSettingsMidiPadsTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab, 8, 0);

    // --- Top config row ---
    lv_obj_t* configRow = lv_obj_create(tab);
    lv_obj_set_size(configRow, lv_pct(100), 50);
    lv_obj_set_style_bg_color(configRow, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(configRow, LV_OPA_80, 0);
    lv_obj_set_style_border_color(configRow, trackColor, 0);
    lv_obj_set_style_border_width(configRow, 1, 0);
    lv_obj_set_style_radius(configRow, 8, 0);
    lv_obj_set_style_pad_all(configRow, 6, 0);
    lv_obj_remove_flag(configRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(configRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(configRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(configRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Pad count
    lv_obj_t* padCountLbl = lv_label_create(configRow);
    lv_label_set_text(padCountLbl, "Pads:");
    lv_obj_set_style_text_font(padCountLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* padCountDd = lv_dropdown_create(configRow);
    lv_obj_set_size(padCountDd, 80, 36);
    lv_dropdown_set_options(padCountDd, "4\n8\n12\n16\n20\n24");
    lv_obj_set_style_bg_color(padCountDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(padCountDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(padCountDd, 6, 0);
    int countIdx = (mSettingsPadCount == 4) ? 0 : (mSettingsPadCount == 8) ? 1 : (mSettingsPadCount == 12) ? 2 : (mSettingsPadCount == 16) ? 3 : (mSettingsPadCount == 20) ? 4 : 5;
    lv_dropdown_set_selected(padCountDd, countIdx);
    lv_obj_add_event_cb(padCountDd, settingsPadCountDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Pad mode
    lv_obj_t* padModeLbl = lv_label_create(configRow);
    lv_label_set_text(padModeLbl, "Mode:");
    lv_obj_set_style_text_font(padModeLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* padModeDd = lv_dropdown_create(configRow);
    lv_obj_set_size(padModeDd, 140, 36);
    lv_dropdown_set_options(padModeDd, "Keyboard\nFX\nScales\nFM Drum\nAnalogue Drum\nSlices");
    lv_obj_set_style_bg_color(padModeDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(padModeDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(padModeDd, 6, 0);
    lv_dropdown_set_selected(padModeDd, mSettingsPadMode);
    lv_obj_add_event_cb(padModeDd, settingsPadModeDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Root Key
    lv_obj_t* rootKeyLbl = lv_label_create(configRow);
    lv_label_set_text(rootKeyLbl, "Root:");
    lv_obj_set_style_text_font(rootKeyLbl, &lv_font_montserrat_10, 0);

    mSettingsRootDd = lv_dropdown_create(configRow);
    lv_obj_set_size(mSettingsRootDd, 70, 36);
    lv_dropdown_set_options(mSettingsRootDd, "C\nC#\nD\nD#\nE\nF\nF#\nG\nG#\nA\nA#\nB");
    lv_obj_set_style_bg_color(mSettingsRootDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(mSettingsRootDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(mSettingsRootDd, 6, 0);
    int currentRoot = mEngine.getScaleRoot();
    lv_dropdown_set_selected(mSettingsRootDd, currentRoot);
    lv_obj_add_event_cb(mSettingsRootDd, settingsScaleDropdownEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Scale type
    lv_obj_t* scaleTypeLbl = lv_label_create(configRow);
    lv_label_set_text(scaleTypeLbl, "Scale:");
    lv_obj_set_style_text_font(scaleTypeLbl, &lv_font_montserrat_10, 0);

    mSettingsScaleBtn = lv_button_create(configRow);
    lv_obj_set_size(mSettingsScaleBtn, 140, 36);
    lv_obj_set_style_bg_color(mSettingsScaleBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(mSettingsScaleBtn, 6, 0);
    lv_obj_add_event_cb(mSettingsScaleBtn, settingsScaleBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* scaleBtnLbl = lv_label_create(mSettingsScaleBtn);
    static const char* kScaleNames[40] = {
        "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
        "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
        "Phrygian Dom.", "Lydian Dom.", "Pentatonic Maj", "Pentatonic Min", "Blues", "Blues Major",
        "Bebop Major", "Bebop Dominant", "Bebop Minor", "Whole Tone",
        "Diminished HW", "Diminished WH", "Augmented", "Double Harmonic", "Hungarian Minor",
        "Neapolitan Maj", "Neapolitan Min", "Persian", "Arabian", "Hirajoshi",
        "In-Sen", "Yo", "Iwato", "Chinese", "Egyptian",
        "Prometheus", "Tritone", "Enigmatic", "Super Locrian", "Acoustic"
    };
    if (mSelectedScaleIdx < 0 || mSelectedScaleIdx >= 40) mSelectedScaleIdx = 0;
    lv_label_set_text(scaleBtnLbl, kScaleNames[mSelectedScaleIdx]);
    lv_obj_center(scaleBtnLbl);

    // Octave Offset
    lv_obj_t* octaveLbl = lv_label_create(configRow);
    lv_label_set_text(octaveLbl, "Octave:");
    lv_obj_set_style_text_font(octaveLbl, &lv_font_montserrat_10, 0);

    mSettingsOctaveDd = lv_dropdown_create(configRow);
    lv_obj_set_size(mSettingsOctaveDd, 70, 36);
    lv_dropdown_set_options(mSettingsOctaveDd, "-5\n-4\n-3\n-2\n-1\n0\n+1\n+2\n+3\n+4\n+5");
    lv_obj_set_style_bg_color(mSettingsOctaveDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(mSettingsOctaveDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(mSettingsOctaveDd, 6, 0);
    lv_dropdown_set_selected(mSettingsOctaveDd, mSettingsOctaveOffset + 5);
    lv_obj_add_event_cb(mSettingsOctaveDd, settingsOctaveDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // FX behavior toggle (only visible in FX mode, which is index 1 now)
    if (mSettingsPadMode == 1) {
        lv_obj_t* fxBehLbl = lv_label_create(configRow);
        lv_label_set_text(fxBehLbl, mSettingsFxPadMomentary ? "Momentary" : "Toggle");
        lv_obj_set_style_text_font(fxBehLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(fxBehLbl, lv_color_hex(0x00CCAA), 0);

        lv_obj_t* fxBehSw = lv_switch_create(configRow);
        lv_obj_set_size(fxBehSw, 50, 24);
        if (!mSettingsFxPadMomentary) lv_obj_add_state(fxBehSw, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(fxBehSw, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_color(fxBehSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_user_data(fxBehSw, fxBehLbl);
        lv_obj_add_event_cb(fxBehSw, settingsFxPadBehaviorSwitchEventCb, LV_EVENT_VALUE_CHANGED, this);
    }

    // --- Pad Grid Area ---
    mSettingsPadGrid = lv_obj_create(tab);
    lv_obj_set_size(mSettingsPadGrid, lv_pct(100), 465);
    lv_obj_set_style_bg_opa(mSettingsPadGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mSettingsPadGrid, 0, 0);
    lv_obj_set_style_pad_all(mSettingsPadGrid, 8, 0);
    lv_obj_remove_flag(mSettingsPadGrid, LV_OBJ_FLAG_SCROLLABLE);

    // Pad Learn Button in the lower right
    lv_obj_t* padLearnBtn = lv_button_create(tab);
    lv_obj_set_size(padLearnBtn, 150, 40);
    lv_obj_add_flag(padLearnBtn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(padLearnBtn, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    if (mPadLearnActive) {
        lv_obj_set_style_bg_color(padLearnBtn, trackColor, 0);
    } else {
        lv_obj_set_style_bg_color(padLearnBtn, lv_color_hex(0x2D2D2D), 0);
    }
    lv_obj_set_style_radius(padLearnBtn, 6, 0);
    
    mPadLearnBtnLabel = lv_label_create(padLearnBtn);
    if (mPadLearnActive) {
        if (mPadLearnTarget >= 0) {
            lv_label_set_text_fmt(mPadLearnBtnLabel, "LEARNING PAD %d", mPadLearnTarget + 1);
        } else {
            lv_label_set_text(mPadLearnBtnLabel, "LEARN: TAP PAD");
        }
    } else {
        lv_label_set_text(mPadLearnBtnLabel, "PAD LEARN");
    }
    lv_obj_set_style_text_font(mPadLearnBtnLabel, &lv_font_montserrat_10, 0);
    lv_obj_center(mPadLearnBtnLabel);
    
    auto padLearnCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->mPadLearnActive = !ui->mPadLearnActive;
        ui->mPadLearnTarget = -1;
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        if (ui->mPadLearnActive) {
            lv_label_set_text(ui->mPadLearnBtnLabel, "LEARN: TAP PAD");
            lv_obj_set_style_bg_color(btn, ui->getTrackColor(ui->mActiveTrack), 0);
        } else {
            lv_label_set_text(ui->mPadLearnBtnLabel, "PAD LEARN");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
        }
    };
    lv_obj_add_event_cb(padLearnBtn, padLearnCb, LV_EVENT_CLICKED, this);

    rebuildPadGrid();
}

void UIManager::rebuildPadGrid() {
    if (!mSettingsPadGrid) return;
    lv_obj_clean(mSettingsPadGrid);

    lv_color_t trackColor = getTrackColor(mActiveTrack);
    int cols = 4;
    int rows = (mSettingsPadCount + cols - 1) / cols;
    int padW = 180;
    int padH = (rows <= 1) ? 420 : (rows <= 2) ? 210 : (rows <= 3) ? 140 : (rows <= 4) ? 105 : 70;
    int gapX = 10;
    int gapY = 8;

    for (int i = 0; i < mSettingsPadCount; ++i) {
        int r = (mSettingsPadMode == 5) ? (i / cols) : (rows - 1 - (i / cols));
        int c = i % cols;
        int x = c * (padW + gapX);
        int y = r * (padH + gapY);

        int noteMapVal = mSettingsPadNoteMap[i];
        std::string noteMapName = std::string(kNoteNames[noteMapVal % 12]) + std::to_string(noteMapVal / 12 - 1);
        bool isLearnTarget = (mPadLearnActive && mPadLearnTarget == i);

        lv_obj_t* pad = lv_obj_create(mSettingsPadGrid);
        lv_obj_set_size(pad, padW, padH);
        lv_obj_set_pos(pad, x, y);
        lv_obj_set_style_bg_color(pad, isLearnTarget ? lv_color_hex(0x884400) : lv_color_hex(0x222222), 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(pad, trackColor, 0);
        lv_obj_set_style_border_width(pad, 2, 0);
        lv_obj_set_style_radius(pad, 10, 0);
        lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);

        // Store pad index in user_data
        lv_obj_set_user_data(pad, (void*)(intptr_t)i);
        lv_obj_add_event_cb(pad, settingsPadBtnEventCb, LV_EVENT_CLICKED, this);

        // Label based on mode
        lv_obj_t* label = lv_label_create(pad);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);

        switch (mSettingsPadMode) {
            case 0: { // Keyboard
                int noteVal = 60 + mSettingsOctaveOffset * 12 + i;
                if (noteVal < 0) noteVal = 0;
                if (noteVal > 127) noteVal = 127;
                std::string noteName = std::string(kNoteNames[noteVal % 12]) + std::to_string(noteVal / 12 - 1);
                lv_label_set_text_fmt(label, "PAD %d\n(%s)\n[%s]", i + 1, noteName.c_str(), noteMapName.c_str());
                break;
            }
            case 1: { // FX
                int fxIdx = mSettingsPadFxAssign[i] % kNumFxSlots;
                lv_label_set_text_fmt(label, "%s\n[%s]", kFxNames[fxIdx], noteMapName.c_str());
                if (mSettingsPadFxToggleState[i] && !mSettingsFxPadMomentary) {
                    lv_obj_set_style_bg_color(pad, lv_color_hex(0x335544), 0);
                }
                break;
            }
            case 2: { // Scales
                int rootKey = mSettingsRootDd ? lv_dropdown_get_selected(mSettingsRootDd) : 0;
                int scaleIdx = mSelectedScaleIdx;
                // Build intervals from scale table
                static const int kPadScaleIntervals[40][12] = {
                    {0,1,2,3,4,5,6,7,8,9,10,11},   // 0  Chromatic
                    {0,2,4,5,7,9,11,-1,-1,-1,-1,-1}, // 1  Major
                    {0,2,3,5,7,8,10,-1,-1,-1,-1,-1}, // 2  Natural Minor
                    {0,2,3,5,7,8,11,-1,-1,-1,-1,-1}, // 3  Harmonic Minor
                    {0,2,3,5,7,9,11,-1,-1,-1,-1,-1}, // 4  Melodic Minor
                    {0,2,3,5,7,9,10,-1,-1,-1,-1,-1}, // 5  Dorian
                    {0,1,3,5,7,8,10,-1,-1,-1,-1,-1}, // 6  Phrygian
                    {0,2,4,6,7,9,11,-1,-1,-1,-1,-1}, // 7  Lydian
                    {0,2,4,5,7,9,10,-1,-1,-1,-1,-1}, // 8  Mixolydian
                    {0,1,3,5,6,8,10,-1,-1,-1,-1,-1}, // 9  Locrian
                    {0,1,4,5,7,8,10,-1,-1,-1,-1,-1}, // 10
                    {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 11
                    {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1},// 12
                    {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 13
                    {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1},// 14
                    {0,2,3,4,7,9,-1,-1,-1,-1,-1,-1}, // 15
                    {0,2,4,5,7,8,9,11,-1,-1,-1,-1},  // 16
                    {0,2,4,5,7,9,10,11,-1,-1,-1,-1}, // 17
                    {0,2,3,5,7,8,9,10,-1,-1,-1,-1},  // 18
                    {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1},// 19
                    {0,1,3,4,6,7,9,10,-1,-1,-1,-1},  // 20
                    {0,2,3,5,6,8,9,11,-1,-1,-1,-1},  // 21
                    {0,3,4,7,8,11,-1,-1,-1,-1,-1,-1},// 22
                    {0,1,4,5,7,8,11,-1,-1,-1,-1,-1}, // 23
                    {0,2,3,6,7,8,11,-1,-1,-1,-1,-1}, // 24
                    {0,1,3,5,7,9,11,-1,-1,-1,-1,-1}, // 25
                    {0,1,3,5,7,8,11,-1,-1,-1,-1,-1}, // 26
                    {0,1,4,5,6,8,11,-1,-1,-1,-1,-1}, // 27
                    {0,2,4,5,6,8,10,-1,-1,-1,-1,-1}, // 28
                    {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1},// 29
                    {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 30
                    {0,2,5,7,9,-1,-1,-1,-1,-1,-1,-1},// 31
                    {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1},// 32
                    {0,4,6,7,11,-1,-1,-1,-1,-1,-1,-1},// 33
                    {0,2,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 34
                    {0,2,4,6,9,10,-1,-1,-1,-1,-1,-1},// 35
                    {0,1,4,6,7,10,-1,-1,-1,-1,-1,-1},// 36
                    {0,1,4,6,8,10,11,-1,-1,-1,-1,-1},// 37
                    {0,1,3,4,6,8,10,-1,-1,-1,-1,-1}, // 38
                    {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 39
                };
                std::vector<int> intervals;
                {
                    int idx = (scaleIdx >= 0 && scaleIdx < 40) ? scaleIdx : 1;
                    const int* row = kPadScaleIntervals[idx];
                    for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                    if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                }
                // Assign note for this pad
                int baseNote = 60 + rootKey + mSettingsOctaveOffset * 12;
                int octShift = i / (int)intervals.size();
                int degIdx = i % (int)intervals.size();
                int note = baseNote + octShift * 12 + intervals[degIdx];
                int noteName = note % 12;
                int octave = (note / 12) - 1;
                lv_label_set_text_fmt(label, "%s%d\n[%s]", kNoteNames[noteName], octave, noteMapName.c_str());
                break;
            }
            case 3: { // FM Drum
                int drumIdx = mSettingsPadDrumAssign[i] % 8;
                lv_label_set_text_fmt(label, "%s\n[%s]", kFmDrumNames[drumIdx], noteMapName.c_str());
                break;
            }
            case 4: { // Analogue Drum
                int drumIdx = mSettingsPadDrumAssign[i] % 8;
                lv_label_set_text_fmt(label, "%s\n[%s]", kAnalogDrumNames[drumIdx], noteMapName.c_str());
                break;
            }
            case 5: { // Slices
                std::vector<float> slicePoints = mEngine.getSamplerSlicePoints(mActiveTrack);
                int numSlices = (int)slicePoints.size();
                if (numSlices <= 0) numSlices = 1;
                int sliceIdx = i % numSlices;
                lv_label_set_text_fmt(label, "PAD %d\nSlice %d\n[%s]", i + 1, sliceIdx + 1, noteMapName.c_str());
                break;
            }
        }
    }
}

std::string UIManager::detectChordName(const int* notes, int count) {
    if (count <= 0) return "Empty";
    if (count == 1) {
        int n = notes[0] % 12;
        int oct = (notes[0] / 12) - 1;
        return std::string(kNoteNames[n]) + std::to_string(oct);
    }
    // Simple chord detection
    int root = notes[0] % 12;
    std::string rootName = kNoteNames[root];
    // Collect intervals relative to root
    std::vector<int> ints;
    for (int i = 1; i < count; ++i) {
        int interval = ((notes[i] % 12) - root + 12) % 12;
        ints.push_back(interval);
    }
    // Check for common chords
    bool has3 = false, has4 = false, has7 = false, has10 = false, has11 = false;
    for (int iv : ints) {
        if (iv == 3) has3 = true;
        if (iv == 4) has4 = true;
        if (iv == 7) has7 = true;
        if (iv == 10) has10 = true;
        if (iv == 11) has11 = true;
    }
    if (has4 && has7 && has11) return rootName + "maj7";
    if (has3 && has7 && has10) return rootName + "m7";
    if (has4 && has7 && has10) return rootName + "7";
    if (has4 && has7) return rootName + "maj";
    if (has3 && has7) return rootName + "m";
    return rootName + " (" + std::to_string(count) + " notes)";
}

// ==========================================================================
// Tab 3: Knobs & Faders Mappings Table
// ==========================================================================
void UIManager::populateSettingsKnobsFadersTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab, 12, 0);

    // 1. Controls config row (Knobs and Sliders counts)
    lv_obj_t* controlsRow = lv_obj_create(tab);
    lv_obj_set_size(controlsRow, lv_pct(100), 50);
    lv_obj_set_style_bg_color(controlsRow, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(controlsRow, LV_OPA_80, 0);
    lv_obj_set_style_border_color(controlsRow, trackColor, 0);
    lv_obj_set_style_border_width(controlsRow, 1, 0);
    lv_obj_set_style_radius(controlsRow, 8, 0);
    lv_obj_set_style_pad_all(controlsRow, 6, 0);
    lv_obj_remove_flag(controlsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(controlsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controlsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controlsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Knob count dropdown
    lv_obj_t* knobCountLbl = lv_label_create(controlsRow);
    lv_label_set_text(knobCountLbl, "Hardware Knobs:");
    lv_obj_set_style_text_font(knobCountLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* knobCountDd = lv_dropdown_create(controlsRow);
    lv_obj_set_size(knobCountDd, 80, 36);
    lv_dropdown_set_options(knobCountDd, "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24");
    lv_obj_set_style_bg_color(knobCountDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(knobCountDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(knobCountDd, 6, 0);
    lv_dropdown_set_selected(knobCountDd, mSettingsKnobCount - 2);
    lv_obj_add_event_cb(knobCountDd, settingsKnobCountDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Slider count dropdown
    lv_obj_t* sliderCountLbl = lv_label_create(controlsRow);
    lv_label_set_text(sliderCountLbl, "Hardware Sliders:");
    lv_obj_set_style_text_font(sliderCountLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* sliderCountDd = lv_dropdown_create(controlsRow);
    lv_obj_set_size(sliderCountDd, 80, 36);
    lv_dropdown_set_options(sliderCountDd, "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24");
    lv_obj_set_style_bg_color(sliderCountDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(sliderCountDd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_radius(sliderCountDd, 6, 0);
    lv_dropdown_set_selected(sliderCountDd, mSettingsSliderCount - 2);
    lv_obj_add_event_cb(sliderCountDd, settingsSliderCountDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Reset Defaults Button
    lv_obj_t* resetBtn = lv_button_create(controlsRow);
    lv_obj_set_size(resetBtn, 130, 36);
    lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0xE06C75), 0); // Crimson Red
    lv_obj_set_style_radius(resetBtn, 6, 0);
    lv_obj_t* resetLbl = lv_label_create(resetBtn);
    lv_label_set_text(resetLbl, "Reset CC Defaults");
    lv_obj_set_style_text_font(resetLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(resetLbl);

    auto resetMidiCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        for (int t = 0; t < 8; ++t) {
            ui->applyDefaultMidiMappings(t, ui->mEngine.getTracks()[t].engineType);
        }
        ui->mNeedsScreenRebuild = true;
        std::cout << "MIDI CC mappings reset to default values (Knobs 70-81, Faders 12-15)" << std::endl;
    };
    lv_obj_add_event_cb(resetBtn, resetMidiCb, LV_EVENT_CLICKED, this);

    // 2. CC Mapping Table (scrollable container)
    lv_obj_t* tableContainer = lv_obj_create(tab);
    lv_obj_set_size(tableContainer, lv_pct(100), 465);
    lv_obj_set_style_bg_color(tableContainer, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(tableContainer, LV_OPA_80, 0);
    lv_obj_set_style_border_color(tableContainer, trackColor, 0);
    lv_obj_set_style_border_width(tableContainer, 1, 0);
    lv_obj_set_style_radius(tableContainer, 10, 0);
    lv_obj_set_style_pad_all(tableContainer, 10, 0);
    lv_obj_set_layout(tableContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tableContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tableContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(tableContainer, 12, 0);
    lv_obj_set_style_pad_row(tableContainer, 6, 0);

    // Title label for mappings (Make it spans full width by setting width pct 100)
    lv_obj_t* mappingTitle = lv_label_create(tableContainer);
    lv_obj_set_width(mappingTitle, lv_pct(100));
    lv_label_set_text(mappingTitle, "CUSTOM HARDWARE MIDI CC ASSIGNMENTS");
    lv_obj_set_style_text_font(mappingTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mappingTitle, trackColor, 0);

    // Generate option list for 0-127
    std::string ccOptions = "";
    for (int c = 0; c <= 127; ++c) {
        ccOptions += "CC " + std::to_string(c) + "\n";
    }
    if (!ccOptions.empty()) ccOptions.pop_back(); // remove last newline

    struct MappingChangeData {
        UIManager* ui;
        int idx;
        bool isKnob;
    };

    // Hardware CC mapping table list
    for (int k = 0; k < mSettingsKnobCount; ++k) {
        lv_obj_t* row = lv_obj_create(tableContainer);
        lv_obj_set_size(row, 340, 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* nameLbl = lv_label_create(row);
        lv_label_set_text_fmt(nameLbl, "KNOB %d (CC %d)", k + 1, mSeqMidiKnobCC[mActiveTrack][k]);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(0xCCCCCC), 0);

        // Dropdown to change CC number directly
        lv_obj_t* ccDd = lv_dropdown_create(row);
        lv_obj_set_size(ccDd, 110, 32);
        lv_dropdown_set_options(ccDd, ccOptions.c_str());
        lv_dropdown_set_selected(ccDd, mSeqMidiKnobCC[mActiveTrack][k]);
        lv_obj_set_style_bg_color(ccDd, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_text_font(ccDd, &lv_font_montserrat_12, 0);
        
        MappingChangeData* data = new MappingChangeData{this, k, true};
        auto mappingClickCb = [](lv_event_t* e) {
            MappingChangeData* d = (MappingChangeData*)lv_event_get_user_data(e);
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            int selected = lv_dropdown_get_selected(dd);
            if (d->isKnob) {
                d->ui->mSeqMidiKnobCC[d->ui->mActiveTrack][d->idx] = selected;
            } else {
                d->ui->mSeqMidiFaderCC[d->ui->mActiveTrack][d->idx] = selected;
            }
        };
        lv_obj_add_event_cb(ccDd, mappingClickCb, LV_EVENT_VALUE_CHANGED, data);
        
        auto freeCb = [](lv_event_t* e) {
            MappingChangeData* d = (MappingChangeData*)lv_event_get_user_data(e);
            delete d;
        };
        lv_obj_add_event_cb(ccDd, freeCb, LV_EVENT_DELETE, data);
    }

    for (int f = 0; f < mSettingsSliderCount; ++f) {
        lv_obj_t* row = lv_obj_create(tableContainer);
        lv_obj_set_size(row, 340, 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* nameLbl = lv_label_create(row);
        lv_label_set_text_fmt(nameLbl, "SLIDER %d (CC %d)", f + 1, mSeqMidiFaderCC[mActiveTrack][f]);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(0xCCCCCC), 0);

        // Dropdown to change CC number directly
        lv_obj_t* ccDd = lv_dropdown_create(row);
        lv_obj_set_size(ccDd, 110, 32);
        lv_dropdown_set_options(ccDd, ccOptions.c_str());
        lv_dropdown_set_selected(ccDd, mSeqMidiFaderCC[mActiveTrack][f]);
        lv_obj_set_style_bg_color(ccDd, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_text_font(ccDd, &lv_font_montserrat_12, 0);
        
        MappingChangeData* data = new MappingChangeData{this, f, false};
        auto mappingClickCb = [](lv_event_t* e) {
            MappingChangeData* d = (MappingChangeData*)lv_event_get_user_data(e);
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            int selected = lv_dropdown_get_selected(dd);
            if (d->isKnob) {
                d->ui->mSeqMidiKnobCC[d->ui->mActiveTrack][d->idx] = selected;
            } else {
                d->ui->mSeqMidiFaderCC[d->ui->mActiveTrack][d->idx] = selected;
            }
        };
        lv_obj_add_event_cb(ccDd, mappingClickCb, LV_EVENT_VALUE_CHANGED, data);
        
        auto freeCb = [](lv_event_t* e) {
            MappingChangeData* d = (MappingChangeData*)lv_event_get_user_data(e);
            delete d;
        };
        lv_obj_add_event_cb(ccDd, freeCb, LV_EVENT_DELETE, data);
    }

    // Add a divider and title for Transport / System CCs
    lv_obj_t* transTitle = lv_label_create(tableContainer);
    lv_obj_set_width(transTitle, lv_pct(100));
    lv_label_set_text(transTitle, "SYSTEM & TRANSPORT CC ASSIGNMENTS");
    lv_obj_set_style_text_font(transTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(transTitle, trackColor, 0);
    lv_obj_set_style_pad_top(transTitle, 15, 0);

    struct TransportMapping {
        std::string name;
        int* pCcVal;
    };
    std::vector<TransportMapping> transMap = {
        {"PLAY / TOGGLE", &mCcPlay},
        {"STOP (IF DISCRETE)", &mCcStop},
        {"RECORD", &mCcRecord},
        {"CLEAR SEQUENCE", &mCcClear},
        {"PREV TRACK", &mCcPrevTrack},
        {"NEXT TRACK", &mCcNextTrack}
    };

    for (size_t i = 0; i < transMap.size(); ++i) {
        lv_obj_t* row = lv_obj_create(tableContainer);
        lv_obj_set_size(row, 340, 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* nameLbl = lv_label_create(row);
        lv_label_set_text_fmt(nameLbl, "%s (CC %d)", transMap[i].name.c_str(), *(transMap[i].pCcVal));
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(0xCCCCCC), 0);

        lv_obj_t* ccDd = lv_dropdown_create(row);
        lv_obj_set_size(ccDd, 110, 32);
        lv_dropdown_set_options(ccDd, ccOptions.c_str());
        lv_dropdown_set_selected(ccDd, *(transMap[i].pCcVal));
        lv_obj_set_style_bg_color(ccDd, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_text_font(ccDd, &lv_font_montserrat_12, 0);

        struct TransChangeData {
            UIManager* ui;
            int* pCcVal;
            lv_obj_t* lbl;
            std::string name;
        };
        TransChangeData* tData = new TransChangeData{this, transMap[i].pCcVal, nameLbl, transMap[i].name};
        
        auto transClickCb = [](lv_event_t* e) {
            TransChangeData* d = (TransChangeData*)lv_event_get_user_data(e);
            lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
            int selected = lv_dropdown_get_selected(dd);
            *(d->pCcVal) = selected;
            lv_label_set_text_fmt(d->lbl, "%s (CC %d)", d->name.c_str(), selected);
        };
        lv_obj_add_event_cb(ccDd, transClickCb, LV_EVENT_VALUE_CHANGED, tData);

        auto transFreeCb = [](lv_event_t* e) {
            TransChangeData* d = (TransChangeData*)lv_event_get_user_data(e);
            delete d;
        };
        lv_obj_add_event_cb(ccDd, transFreeCb, LV_EVENT_DELETE, tData);
    }
}

void UIManager::populateSettingsSystemTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto applyCardStyle = [trackColor](lv_obj_t* card) {
        lv_obj_set_size(card, 240, 460);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(card, 10, 0);
    };

    // --- Column 1: Audio Configuration ---
    lv_obj_t* audioCard = lv_obj_create(tab);
    applyCardStyle(audioCard);

    lv_obj_t* audioTitle = lv_label_create(audioCard);
    lv_label_set_text(audioTitle, "AUDIO DEVICE CONFIG");
    lv_obj_set_style_text_font(audioTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(audioTitle, trackColor, 0);

    // Audio Output Mode selector
    lv_obj_t* outputModeLbl = lv_label_create(audioCard);
    lv_label_set_text(outputModeLbl, "Output Channel Mode:");
    lv_obj_set_style_text_font(outputModeLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* outputModeDd = lv_dropdown_create(audioCard);
    lv_obj_set_size(outputModeDd, 180, 36);
    lv_dropdown_set_options(outputModeDd, "Stereo\nMono (L Only)\nPseudo-Stereo (Delay)\nPhase-Inverted");
    lv_obj_set_style_bg_color(outputModeDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(outputModeDd, &lv_font_montserrat_12, 0);
    lv_dropdown_set_selected(outputModeDd, mEngine.getAudioOutputMode());
    
    auto outModeCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        int selected = lv_dropdown_get_selected(dd);
        ui->mEngine.setAudioOutputMode(selected);
        std::cout << "System: Audio Output Mode set to " << selected << std::endl;
    };
    lv_obj_add_event_cb(outputModeDd, outModeCb, LV_EVENT_VALUE_CHANGED, this);

    // Audio Device Dropdown
    lv_obj_t* deviceLbl = lv_label_create(audioCard);
    lv_label_set_text(deviceLbl, "Active SDL Audio Device:");
    lv_obj_set_style_text_font(deviceLbl, &lv_font_montserrat_10, 0);

    std::string deviceOptions = "Default\n";
    int numDevs = SDL_GetNumAudioDevices(0);
    std::vector<std::string> devNames;
    devNames.push_back("Default");
    int activeIdx = 0;
    
    for (int i = 0; i < numDevs; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name) {
            deviceOptions += std::string(name) + "\n";
            devNames.push_back(name);
            if (gCurrentAudioDevice == name) {
                activeIdx = devNames.size() - 1;
            }
        }
    }
    if (!deviceOptions.empty() && deviceOptions.back() == '\n') {
        deviceOptions.pop_back();
    }

    lv_obj_t* deviceDd = lv_dropdown_create(audioCard);
    lv_obj_set_size(deviceDd, 200, 36);
    lv_dropdown_set_options(deviceDd, deviceOptions.c_str());
    lv_obj_set_style_bg_color(deviceDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(deviceDd, &lv_font_montserrat_12, 0);
    lv_dropdown_set_selected(deviceDd, activeIdx);

    struct DeviceChangeData {
        UIManager* ui;
        std::vector<std::string> names;
    };
    DeviceChangeData* devData = new DeviceChangeData{this, devNames};

    auto devCb = [](lv_event_t* e) {
        DeviceChangeData* d = (DeviceChangeData*)lv_event_get_user_data(e);
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        int selected = lv_dropdown_get_selected(dd);
        if (selected >= 0 && selected < (int)d->names.size()) {
            std::string selectedName = d->names[selected];
            bool success = switchAudioDevice(selectedName);
            if (success) {
                d->ui->mSettingsAudioDevice = selectedName;
                d->ui->saveSettings(d->ui->mSettingsFilePath);
            }
        }
    };
    lv_obj_add_event_cb(deviceDd, devCb, LV_EVENT_VALUE_CHANGED, devData);

    auto devFreeCb = [](lv_event_t* e) {
        DeviceChangeData* d = (DeviceChangeData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(deviceDd, devFreeCb, LV_EVENT_DELETE, devData);

    // Panic Button and Reset MIDI
    lv_obj_t* actionLbl = lv_label_create(audioCard);
    lv_label_set_text(actionLbl, "System Actions:");
    lv_obj_set_style_text_font(actionLbl, &lv_font_montserrat_10, 0);

    lv_obj_t* panicBtn = lv_button_create(audioCard);
    lv_obj_set_size(panicBtn, 200, 36);
    lv_obj_set_style_bg_color(panicBtn, lv_color_hex(0xE06C75), 0);
    lv_obj_t* panicLbl = lv_label_create(panicBtn);
    lv_label_set_text(panicLbl, "AUDIO PANIC (ALL NOTES OFF)");
    lv_obj_set_style_text_font(panicLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(panicLbl);
    lv_obj_add_event_cb(panicBtn, settingsPanicBtnEventCb, LV_EVENT_CLICKED, this);


    // --- Column 2: System Performance & Updater ---
    lv_obj_t* perfCard = lv_obj_create(tab);
    applyCardStyle(perfCard);

    lv_obj_t* perfTitle = lv_label_create(perfCard);
    lv_label_set_text(perfTitle, "PERFORMANCE STATS");
    lv_obj_set_style_text_font(perfTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(perfTitle, trackColor, 0);

    mCpuLoadLabelSystem = lv_label_create(perfCard);
    lv_label_set_text_fmt(mCpuLoadLabelSystem, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
    lv_obj_set_style_text_font(mCpuLoadLabelSystem, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mCpuLoadLabelSystem, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t* sampleRateLbl = lv_label_create(perfCard);
    lv_label_set_text_fmt(sampleRateLbl, "Sample Rate: %d Hz", (int)mEngine.getSampleRate());
    lv_obj_set_style_text_font(sampleRateLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sampleRateLbl, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t* bufferSizeLbl = lv_label_create(perfCard);
    lv_label_set_text(bufferSizeLbl, "Buffer Size: 256 samples");
    lv_obj_set_style_text_font(bufferSizeLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bufferSizeLbl, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t* ipAddressLbl = lv_label_create(perfCard);
    std::string ip = getLocalIPAddress();
    lv_label_set_text_fmt(ipAddressLbl, "IP Address: %s", ip.c_str());
    lv_obj_set_style_text_font(ipAddressLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ipAddressLbl, lv_color_hex(0x00FFCC), 0); // Cool teal accent for visibility

    // Separator line
    lv_obj_t* sepLine = lv_obj_create(perfCard);
    lv_obj_set_size(sepLine, 210, 1);
    lv_obj_set_style_bg_color(sepLine, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(sepLine, 0, 0);

    // System Updater Sub-section
    lv_obj_t* updateTitle = lv_label_create(perfCard);
    lv_label_set_text(updateTitle, "SYSTEM UPDATER");
    lv_obj_set_style_text_font(updateTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(updateTitle, trackColor, 0);

    mSettingsUpdateStatus = lv_label_create(perfCard);
    lv_label_set_text(mSettingsUpdateStatus, "Status: Idle");
    lv_obj_set_style_text_font(mSettingsUpdateStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mSettingsUpdateStatus, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(mSettingsUpdateStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mSettingsUpdateStatus, 210);

    lv_obj_t* updateBtn = lv_button_create(perfCard);
    lv_obj_set_size(updateBtn, 180, 34);
    lv_obj_set_style_bg_color(updateBtn, trackColor, 0);
    lv_obj_t* updateBtnLbl = lv_label_create(updateBtn);
    lv_label_set_text(updateBtnLbl, "CHECK & UPDATE");
    lv_obj_set_style_text_font(updateBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(updateBtnLbl);
    lv_obj_add_event_cb(updateBtn, settingsUpdateBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* restartBtn = lv_button_create(perfCard);
    lv_obj_set_size(restartBtn, 180, 34);
    lv_obj_set_style_bg_color(restartBtn, lv_color_hex(0xE06C75), 0);
    lv_obj_t* restartBtnLbl = lv_label_create(restartBtn);
    lv_label_set_text(restartBtnLbl, "RESTART LOOM");
    lv_obj_set_style_text_font(restartBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(restartBtnLbl);
    lv_obj_add_event_cb(restartBtn, settingsRestartBtnEventCb, LV_EVENT_CLICKED, this);


    // --- Column 3: USB/MIDI Diagnostic Monitor ---
    lv_obj_t* diagCard = lv_obj_create(tab);
    applyCardStyle(diagCard);

    lv_obj_t* diagTitle = lv_label_create(diagCard);
    lv_label_set_text(diagTitle, "USB/MIDI DIAGNOSTICS");
    lv_obj_set_style_text_font(diagTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diagTitle, trackColor, 0);

    lv_obj_t* listTitle = lv_label_create(diagCard);
    lv_label_set_text(listTitle, "Detected USB & MIDI:");
    lv_obj_set_style_text_font(listTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(listTitle, lv_color_hex(0x888888), 0);

    mMidiDeviceListLabel = lv_label_create(diagCard);
    lv_label_set_text(mMidiDeviceListLabel, "Scanning...");
    lv_obj_set_style_text_font(mMidiDeviceListLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mMidiDeviceListLabel, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_long_mode(mMidiDeviceListLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mMidiDeviceListLabel, 210);

    lv_obj_t* monitorTitle = lv_label_create(diagCard);
    lv_label_set_text(monitorTitle, "Real-time MIDI Log:");
    lv_obj_set_style_text_font(monitorTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(monitorTitle, lv_color_hex(0x888888), 0);

    mMidiMonitorConsoleLabel = lv_label_create(diagCard);
    lv_label_set_text(mMidiMonitorConsoleLabel, "(No MIDI events yet)");
    lv_obj_set_style_text_font(mMidiMonitorConsoleLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mMidiMonitorConsoleLabel, lv_color_hex(0x00FF88), 0); // Retro green console text
    lv_label_set_long_mode(mMidiMonitorConsoleLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mMidiMonitorConsoleLabel, 210);

    // Separator line
    lv_obj_t* btSepLine = lv_obj_create(diagCard);
    lv_obj_set_size(btSepLine, 210, 1);
    lv_obj_set_style_bg_color(btSepLine, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(btSepLine, 0, 0);

    // Bluetooth button
    lv_obj_t* btBtn = lv_button_create(diagCard);
    lv_obj_set_size(btBtn, 200, 34);
    lv_obj_set_style_bg_color(btBtn, trackColor, 0);
    lv_obj_t* btBtnLbl = lv_label_create(btBtn);
    lv_label_set_text(btBtnLbl, "PAIR BLUETOOTH");
    lv_obj_set_style_text_font(btBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(btBtnLbl);
    lv_obj_add_event_cb(btBtn, settingsBtPairBtnEventCb, LV_EVENT_CLICKED, this);
}

// ==========================================================================
// ==========================================================================
// Settings Callbacks
// ==========================================================================

void UIManager::settingsSampleRateDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    float sr = (sel == 1) ? 48000.0f : 44100.0f;
    ui->mEngine.updateSampleRate(sr);
    std::cout << "Settings: Sample Rate updated to " << sr << " Hz" << std::endl;
}

void UIManager::settingsPanicBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.panic();
    std::cout << "Settings: PANIC! All notes killed." << std::endl;
}

void UIManager::settingsResetMidiBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.resetMidiPatching();
    // Refresh the MIDI dropdowns to show reset values
    if (ui->mSettingsMidiInDd) {
        lv_dropdown_set_selected(ui->mSettingsMidiInDd, 17); // ALL
    }
    if (ui->mSettingsMidiOutDd) {
        lv_dropdown_set_selected(ui->mSettingsMidiOutDd, 0); // NONE
    }
    std::cout << "Settings: MIDI and Patching reset." << std::endl;
}

void UIManager::settingsMidiTrackSelectDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    ui->mSettingsMidiTrackSelect = lv_dropdown_get_selected(dd);
    // Update MIDI In/Out dropdowns to reflect newly selected track
    int trackIdx = ui->mSettingsMidiTrackSelect;
    if (ui->mSettingsMidiInDd) {
        int inCh = ui->mEngine.getTracks()[trackIdx].midiInChannel;
        if (inCh >= 0 && inCh <= 17) lv_dropdown_set_selected(ui->mSettingsMidiInDd, inCh);
    }
    if (ui->mSettingsMidiOutDd) {
        int outCh = ui->mEngine.getTracks()[trackIdx].midiOutChannel;
        if (outCh >= 0 && outCh <= 16) lv_dropdown_set_selected(ui->mSettingsMidiOutDd, outCh);
    }
}

void UIManager::settingsMidiInChannelDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd); // 0=NONE, 1-16=Ch 1-16, 17=ALL
    ui->mEngine.setParameter(ui->mSettingsMidiTrackSelect, 800, (float)sel);
    std::cout << "Settings: Track " << ui->mSettingsMidiTrackSelect + 1 << " MIDI In set to " << sel << std::endl;
}

void UIManager::settingsMidiOutChannelDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd); // 0=NONE, 1-16=Ch 1-16
    ui->mEngine.setParameter(ui->mSettingsMidiTrackSelect, 801, (float)sel);
    std::cout << "Settings: Track " << ui->mSettingsMidiTrackSelect + 1 << " MIDI Out set to " << sel << std::endl;
}

void UIManager::settingsScaleDropdownEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui || !ui->mSettingsRootDd || !ui->mSettingsScaleDd) return;

    int rootIdx = lv_dropdown_get_selected(ui->mSettingsRootDd);
    int scaleIdx = lv_dropdown_get_selected(ui->mSettingsScaleDd);

    // Build intervals directly from the kScaleIntervals table (40 scales, defined in MidiInput.h)
    // For UIManager we inline the same table to avoid including MidiInput.h here.
    static const int kUIScaleIntervals[40][12] = {
        {0,1,2,3,4,5,6,7,8,9,10,11},   // 0  Chromatic
        {0,2,4,5,7,9,11,-1,-1,-1,-1,-1}, // 1  Major
        {0,2,3,5,7,8,10,-1,-1,-1,-1,-1}, // 2  Natural Minor
        {0,2,3,5,7,8,11,-1,-1,-1,-1,-1}, // 3  Harmonic Minor
        {0,2,3,5,7,9,11,-1,-1,-1,-1,-1}, // 4  Melodic Minor
        {0,2,3,5,7,9,10,-1,-1,-1,-1,-1}, // 5  Dorian
        {0,1,3,5,7,8,10,-1,-1,-1,-1,-1}, // 6  Phrygian
        {0,2,4,6,7,9,11,-1,-1,-1,-1,-1}, // 7  Lydian
        {0,2,4,5,7,9,10,-1,-1,-1,-1,-1}, // 8  Mixolydian
        {0,1,3,5,6,8,10,-1,-1,-1,-1,-1}, // 9  Locrian
        {0,1,4,5,7,8,10,-1,-1,-1,-1,-1}, // 10 Phrygian Dominant
        {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 11 Lydian Dominant
        {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1},// 12 Pentatonic Major
        {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 13 Pentatonic Minor
        {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1},// 14 Blues
        {0,2,3,4,7,9,-1,-1,-1,-1,-1,-1}, // 15 Blues Major
        {0,2,4,5,7,8,9,11,-1,-1,-1,-1},  // 16 Bebop Major
        {0,2,4,5,7,9,10,11,-1,-1,-1,-1}, // 17 Bebop Dominant
        {0,2,3,5,7,8,9,10,-1,-1,-1,-1},  // 18 Bebop Minor
        {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1},// 19 Whole Tone
        {0,1,3,4,6,7,9,10,-1,-1,-1,-1},  // 20 Diminished HW
        {0,2,3,5,6,8,9,11,-1,-1,-1,-1},  // 21 Diminished WH
        {0,3,4,7,8,11,-1,-1,-1,-1,-1,-1},// 22 Augmented
        {0,1,4,5,7,8,11,-1,-1,-1,-1,-1}, // 23 Double Harmonic
        {0,2,3,6,7,8,11,-1,-1,-1,-1,-1}, // 24 Hungarian Minor
        {0,1,3,5,7,9,11,-1,-1,-1,-1,-1}, // 25 Neapolitan Major
        {0,1,3,5,7,8,11,-1,-1,-1,-1,-1}, // 26 Neapolitan Minor
        {0,1,4,5,6,8,11,-1,-1,-1,-1,-1}, // 27 Persian
        {0,2,4,5,6,8,10,-1,-1,-1,-1,-1}, // 28 Arabian
        {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1},// 29 Hirajoshi
        {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 30 In-Sen
        {0,2,5,7,9,-1,-1,-1,-1,-1,-1,-1},// 31 Yo
        {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1},// 32 Iwato
        {0,4,6,7,11,-1,-1,-1,-1,-1,-1,-1},// 33 Chinese
        {0,2,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 34 Egyptian
        {0,2,4,6,9,10,-1,-1,-1,-1,-1,-1},// 35 Prometheus
        {0,1,4,6,7,10,-1,-1,-1,-1,-1,-1},// 36 Tritone
        {0,1,4,6,8,10,11,-1,-1,-1,-1,-1},// 37 Enigmatic
        {0,1,3,4,6,8,10,-1,-1,-1,-1,-1}, // 38 Super Locrian
        {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 39 Acoustic
    };

    std::vector<int> intervals;
    if (scaleIdx >= 0 && scaleIdx < 40) {
        const int* row = kUIScaleIntervals[scaleIdx];
        for (int i = 0; i < 12 && row[i] >= 0; ++i)
            intervals.push_back(row[i]);
    }
    if (intervals.empty()) intervals = {0,1,2,3,4,5,6,7,8,9,10,11};

    ui->mEngine.setScaleConfig(rootIdx, intervals);
    // Rebuild pad grid if in Scales mode
    if (ui->mSettingsPadMode == 2) {
        ui->rebuildPadGrid();
    }
    std::cout << "Settings: Scale idx=" << scaleIdx << " Root=" << rootIdx << " intervals=" << intervals.size() << std::endl;
}

// --- MIDI Pads Callbacks ---

void UIManager::settingsPadCountDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    int counts[] = {4, 8, 12, 16, 20, 24};
    ui->mSettingsPadCount = counts[sel];
    ui->rebuildPadGrid();
}

void UIManager::settingsKnobCountDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    ui->mSettingsKnobCount = sel + 2;
    ui->mNeedsScreenRebuild = true;
    std::cout << "Settings: Knob Count set to " << ui->mSettingsKnobCount << std::endl;
}

void UIManager::settingsSliderCountDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    ui->mSettingsSliderCount = sel + 2;
    ui->mNeedsScreenRebuild = true;
    std::cout << "Settings: Slider Count set to " << ui->mSettingsSliderCount << std::endl;
}

void UIManager::settingsOctaveDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int sel = lv_dropdown_get_selected(dd);
    ui->mSettingsOctaveOffset = sel - 5;
    ui->rebuildPadGrid();
    std::cout << "Settings: Pad Octave Offset set to " << ui->mSettingsOctaveOffset << std::endl;
}

void UIManager::settingsPadModeDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    ui->mSettingsPadMode = lv_dropdown_get_selected(dd);
    ui->mSettingsChordRecordingPad = -1;
    // Re-render entire tab to show/hide FX behavior toggle
    ui->createCenterContentArea();
}

void UIManager::settingsFxPadBehaviorSwitchEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* label = (lv_obj_t*)lv_obj_get_user_data(sw);
    bool isToggle = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui->mSettingsFxPadMomentary = !isToggle;
    if (label) {
        lv_label_set_text(label, ui->mSettingsFxPadMomentary ? "Momentary" : "Toggle");
    }
    // Reset toggle states when switching
    for (int i = 0; i < 24; ++i) ui->mSettingsPadFxToggleState[i] = false;
}

void UIManager::settingsPadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* pad = (lv_obj_t*)lv_event_get_target(e);
    int padIdx = (int)(intptr_t)lv_obj_get_user_data(pad);
    if (padIdx < 0 || padIdx >= 24) return;

    if (ui->mPadLearnActive) {
        ui->mPadLearnTarget = padIdx;
        if (ui->mPadLearnBtnLabel) {
            lv_label_set_text_fmt(ui->mPadLearnBtnLabel, "LEARNING PAD %d", padIdx + 1);
        }
        ui->rebuildPadGrid();
        return;
    }

    switch (ui->mSettingsPadMode) {
        case 0: { // Keyboard - trigger note
            int noteVal = 60 + ui->mSettingsOctaveOffset * 12 + padIdx;
            if (noteVal < 0) noteVal = 0;
            if (noteVal > 127) noteVal = 127;
            ui->mEngine.triggerNote(ui->mActiveTrack, noteVal, 100);
            break;
        }
        case 1: { // FX - open selection popup
            ui->openSettingsFxSelectPopup(padIdx);
            break;
        }
        case 2: { // Scales - trigger note
            int rootKey = ui->mSettingsRootDd ? lv_dropdown_get_selected(ui->mSettingsRootDd) : 0;
            int scaleIdx = ui->mSelectedScaleIdx;
            
            static const int kPadScaleIntervals[40][12] = {
                {0,1,2,3,4,5,6,7,8,9,10,11},   // 0  Chromatic
                {0,2,4,5,7,9,11,-1,-1,-1,-1,-1}, // 1  Major
                {0,2,3,5,7,8,10,-1,-1,-1,-1,-1}, // 2  Natural Minor
                {0,2,3,5,7,8,11,-1,-1,-1,-1,-1}, // 3  Harmonic Minor
                {0,2,3,5,7,9,11,-1,-1,-1,-1,-1}, // 4  Melodic Minor
                {0,2,3,5,7,9,10,-1,-1,-1,-1,-1}, // 5  Dorian
                {0,1,3,5,7,8,10,-1,-1,-1,-1,-1}, // 6  Phrygian
                {0,2,4,6,7,9,11,-1,-1,-1,-1,-1}, // 7  Lydian
                {0,2,4,5,7,9,10,-1,-1,-1,-1,-1}, // 8  Mixolydian
                {0,1,3,5,6,8,10,-1,-1,-1,-1,-1}, // 9  Locrian
                {0,1,4,5,7,8,10,-1,-1,-1,-1,-1}, // 10 Phrygian Dominant
                {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 11 Lydian Dominant
                {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1},// 12 Pentatonic Major
                {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 13 Pentatonic Minor
                {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1},// 14 Blues
                {0,2,3,4,7,9,-1,-1,-1,-1,-1,-1}, // 15 Blues Major
                {0,2,4,5,7,8,9,11,-1,-1,-1,-1},  // 16 Bebop Major
                {0,2,4,5,7,9,10,11,-1,-1,-1,-1}, // 17 Bebop Dominant
                {0,2,3,5,7,8,9,10,-1,-1,-1,-1},  // 18 Bebop Minor
                {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1},// 19 Whole Tone
                {0,1,3,4,6,7,9,10,-1,-1,-1,-1},  // 20 Diminished HW
                {0,2,3,5,6,8,9,11,-1,-1,-1,-1},  // 21 Diminished WH
                {0,3,4,7,8,11,-1,-1,-1,-1,-1,-1},// 22 Augmented
                {0,1,4,5,7,8,11,-1,-1,-1,-1,-1}, // 23 Double Harmonic
                {0,2,3,6,7,8,11,-1,-1,-1,-1,-1}, // 24 Hungarian Minor
                {0,1,3,5,7,9,11,-1,-1,-1,-1,-1}, // 25 Neapolitan Major
                {0,1,3,5,7,8,11,-1,-1,-1,-1,-1}, // 26 Neapolitan Minor
                {0,1,4,5,6,8,11,-1,-1,-1,-1,-1}, // 27 Persian
                {0,2,4,5,6,8,10,-1,-1,-1,-1,-1}, // 28 Arabian
                {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1},// 29 Hirajoshi
                {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 30 In-Sen
                {0,2,5,7,9,-1,-1,-1,-1,-1,-1,-1},// 31 Yo
                {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1},// 32 Iwato
                {0,4,6,7,11,-1,-1,-1,-1,-1,-1,-1},// 33 Chinese
                {0,2,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 34 Egyptian
                {0,2,4,6,9,10,-1,-1,-1,-1,-1,-1},// 35 Prometheus
                {0,1,4,6,7,10,-1,-1,-1,-1,-1,-1},// 36 Tritone
                {0,1,4,6,8,10,11,-1,-1,-1,-1,-1},// 37 Enigmatic
                {0,1,3,4,6,8,10,-1,-1,-1,-1,-1}, // 38 Super Locrian
                {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 39 Acoustic
            };

            std::vector<int> intervals;
            if (scaleIdx >= 0 && scaleIdx < 40) {
                const int* row = kPadScaleIntervals[scaleIdx];
                for (int i = 0; i < 12 && row[i] >= 0; ++i)
                    intervals.push_back(row[i]);
            }
            if (intervals.empty()) intervals = {0,1,2,3,4,5,6,7,8,9,10,11};

            int baseNote = 60 + rootKey + ui->mSettingsOctaveOffset * 12;
            int octShift = padIdx / (int)intervals.size();
            int degIdx = padIdx % (int)intervals.size();
            int note = baseNote + octShift * 12 + intervals[degIdx];
            ui->mEngine.triggerNote(ui->mActiveTrack, note, 100);
            break;
        }
        case 3: { // FM Drum - cycle voice or trigger
            ui->mSettingsPadDrumAssign[padIdx] = (ui->mSettingsPadDrumAssign[padIdx] + 1) % 8;
            ui->rebuildPadGrid();
            break;
        }
        case 4: { // Analogue Drum - cycle voice or trigger
            ui->mSettingsPadDrumAssign[padIdx] = (ui->mSettingsPadDrumAssign[padIdx] + 1) % 8;
            ui->rebuildPadGrid();
            break;
        }
        case 5: { // Slices - trigger slice note on active track
            std::vector<float> slicePoints = ui->mEngine.getSamplerSlicePoints(ui->mActiveTrack);
            int numSlices = (int)slicePoints.size();
            if (numSlices <= 0) numSlices = 1;
            int sliceIdx = padIdx % numSlices;
            ui->mEngine.triggerNote(ui->mActiveTrack, 60 + sliceIdx, 100);
            break;
        }
    }
}

void UIManager::settingsPadReassignDdEventCb(lv_event_t* e) {
    // Placeholder for future dropdown-based reassignment
}

void UIManager::openSettingsFxSelectPopup(int padIdx) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Create full-screen overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add click event to background to close the modal if tapped outside
    lv_obj_add_event_cb(overlay, settingsFxSelectCloseEventCb, LV_EVENT_CLICKED, this);
    mSettingsFxSelectModal = overlay;

    // Dialog card
    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 580, 440);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, trackColor, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    // Prevent clicking the card from dismissing the modal
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    // Close button (top right)
    lv_obj_t* closeBtn = lv_button_create(card);
    lv_obj_set_size(closeBtn, 36, 36);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(closeBtn, 18, 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, settingsFxSelectCloseEventCb, LV_EVENT_CLICKED, this);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text_fmt(title, "SELECT FX PEDAL FOR PAD %d", padIdx + 1);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    // Grid Container
    lv_obj_t* gridContainer = lv_obj_create(card);
    lv_obj_set_size(gridContainer, 540, 340);
    lv_obj_align(gridContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(gridContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gridContainer, 0, 0);
    lv_obj_set_layout(gridContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(gridContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(gridContainer, 4, 0);
    lv_obj_set_style_pad_row(gridContainer, 10, 0);
    lv_obj_set_style_pad_column(gridContainer, 10, 0);
    lv_obj_remove_flag(gridContainer, LV_OBJ_FLAG_SCROLLABLE);

    int currentFx = mSettingsPadFxAssign[padIdx] % kNumFxSlots;

    // Populate buttons for all 18 FX
    for (int i = 0; i < kNumFxSlots; ++i) {
        lv_obj_t* btn = lv_button_create(gridContainer);
        lv_obj_set_size(btn, 160, 42);
        
        // Pack padIdx (high 16 bits) and fxIdx (low 16 bits) into user_data
        uintptr_t packedData = ((uintptr_t)padIdx << 16) | (uintptr_t)i;
        lv_obj_set_user_data(btn, (void*)packedData);
        lv_obj_add_event_cb(btn, settingsFxSelectBtnEventCb, LV_EVENT_CLICKED, this);

        // Styling
        if (i == currentFx) {
            // Highlight currently selected pedal
            lv_obj_set_style_bg_color(btn, trackColor, 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x262626), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
        }
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t* btnLbl = lv_label_create(btn);
        lv_label_set_text(btnLbl, kFxNames[i]);
        lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_12, 0);
        if (i == currentFx) {
            lv_obj_set_style_text_color(btnLbl, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_text_color(btnLbl, lv_color_hex(0xDDDDDD), 0);
        }
        lv_obj_center(btnLbl);
    }
}

void UIManager::settingsFxSelectBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    if (!ui || !btn) return;

    uintptr_t packedData = (uintptr_t)lv_obj_get_user_data(btn);
    int padIdx = (packedData >> 16) & 0xFFFF;
    int fxIdx = packedData & 0xFFFF;

    if (padIdx >= 0 && padIdx < 24 && fxIdx >= 0 && fxIdx < kNumFxSlots) {
        ui->mSettingsPadFxAssign[padIdx] = fxIdx;
        std::cout << "Settings: Pad " << padIdx + 1 << " reassigned to FX " << kFxNames[fxIdx] << std::endl;
        
        // Rebuild pad grid so new name shows up
        ui->rebuildPadGrid();
    }

    // Dismiss modal
    if (ui->mSettingsFxSelectModal) {
        lv_obj_delete(ui->mSettingsFxSelectModal);
        ui->mSettingsFxSelectModal = nullptr;
    }
}

void UIManager::settingsFxSelectCloseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* currentTarget = (lv_obj_t*)lv_event_get_current_target(e);
    
    if (ui && ui->mSettingsFxSelectModal) {
        // Dismiss if we clicked the close button or clicked the overlay background directly
        if (target == currentTarget || target == ui->mSettingsFxSelectModal) {
            lv_obj_delete(ui->mSettingsFxSelectModal);
            ui->mSettingsFxSelectModal = nullptr;
        }
    }
}

void UIManager::openScalePickerModal() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Create full-screen overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add click event to background to close the modal if tapped outside
    lv_obj_add_event_cb(overlay, settingsScaleSelectCloseEventCb, LV_EVENT_CLICKED, this);
    mSettingsScaleModal = overlay;

    // Dialog card
    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 720, 480);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, trackColor, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    // Prevent clicking the card from dismissing the modal
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    // Close button (top right)
    lv_obj_t* closeBtn = lv_button_create(card);
    lv_obj_set_size(closeBtn, 36, 36);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(closeBtn, 18, 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, settingsScaleSelectCloseEventCb, LV_EVENT_CLICKED, this);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "SELECT SNAP SCALE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    // Scrollable Grid Container for scales
    lv_obj_t* gridContainer = lv_obj_create(card);
    lv_obj_set_size(gridContainer, 680, 380);
    lv_obj_align(gridContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(gridContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gridContainer, 0, 0);
    lv_obj_set_layout(gridContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(gridContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(gridContainer, 4, 0);
    lv_obj_set_style_pad_row(gridContainer, 10, 0);
    lv_obj_set_style_pad_column(gridContainer, 10, 0);
    // Allow scroll for scales list
    lv_obj_add_flag(gridContainer, LV_OBJ_FLAG_SCROLLABLE);

    static const char* kScaleNames[40] = {
        "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
        "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
        "Phrygian Dominant", "Lydian Dominant", "Pentatonic Major", "Pentatonic Minor", "Blues",
        "Blues Major", "Bebop Major", "Bebop Dominant", "Bebop Minor", "Whole Tone",
        "Diminished HW", "Diminished WH", "Augmented", "Double Harmonic", "Hungarian Minor",
        "Neapolitan Major", "Neapolitan Minor", "Persian", "Arabian", "Hirajoshi",
        "In-Sen", "Yo", "Iwato", "Chinese", "Egyptian",
        "Prometheus", "Tritone", "Enigmatic", "Super Locrian", "Acoustic"
    };

    for (int i = 0; i < 40; ++i) {
        lv_obj_t* btn = lv_button_create(gridContainer);
        lv_obj_set_size(btn, 200, 42);
        
        lv_obj_set_user_data(btn, (void*)(uintptr_t)i);
        lv_obj_add_event_cb(btn, settingsScaleSelectBtnEventCb, LV_EVENT_CLICKED, this);

        // Styling
        if (i == mSelectedScaleIdx) {
            lv_obj_set_style_bg_color(btn, trackColor, 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x262626), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
        }
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t* btnLbl = lv_label_create(btn);
        lv_label_set_text(btnLbl, kScaleNames[i]);
        lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_12, 0);
        if (i == mSelectedScaleIdx) {
            lv_obj_set_style_text_color(btnLbl, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_text_color(btnLbl, lv_color_hex(0xDDDDDD), 0);
        }
        lv_obj_center(btnLbl);
    }
}

void UIManager::settingsScaleSelectBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    if (!ui || !btn) return;

    int scaleIdx = (int)(uintptr_t)lv_obj_get_user_data(btn);
    if (scaleIdx >= 0 && scaleIdx < 40) {
        ui->mSelectedScaleIdx = scaleIdx;
        static const char* kScaleNames[40] = {
            "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
            "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
            "Phrygian Dominant", "Lydian Dominant", "Pentatonic Major", "Pentatonic Minor", "Blues",
            "Blues Major", "Bebop Major", "Bebop Dominant", "Bebop Minor", "Whole Tone",
            "Diminished HW", "Diminished WH", "Augmented", "Double Harmonic", "Hungarian Minor",
            "Neapolitan Major", "Neapolitan Minor", "Persian", "Arabian", "Hirajoshi",
            "In-Sen", "Yo", "Iwato", "Chinese", "Egyptian",
            "Prometheus", "Tritone", "Enigmatic", "Super Locrian", "Acoustic"
        };
        std::cout << "Settings: Selected scale " << kScaleNames[scaleIdx] << std::endl;
        
        // Update the button label text
        if (ui->mSettingsScaleBtn) {
            lv_obj_t* lbl = lv_obj_get_child(ui->mSettingsScaleBtn, 0);
            if (lbl) {
                lv_label_set_text_fmt(lbl, "SCALE: %s", kScaleNames[scaleIdx]);
            }
        }
        
        // Rebuild pad grid so new notes snap
        ui->rebuildPadGrid();
    }

    // Dismiss modal
    if (ui->mSettingsScaleModal) {
        lv_obj_delete(ui->mSettingsScaleModal);
        ui->mSettingsScaleModal = nullptr;
    }
}

void UIManager::settingsScaleSelectCloseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* currentTarget = (lv_obj_t*)lv_event_get_current_target(e);
    
    if (ui && ui->mSettingsScaleModal) {
        if (target == currentTarget || target == ui->mSettingsScaleModal) {
            lv_obj_delete(ui->mSettingsScaleModal);
            ui->mSettingsScaleModal = nullptr;
        }
    }
}

void UIManager::settingsScaleBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui) {
        ui->openScalePickerModal();
    }
}

// --- System Tab Callbacks ---

void UIManager::settingsSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsProject = true;
    ui->mFileBrowserIsSave = true;
    ui->openFileBrowser(true);
    std::cout << "Settings: Save button pressed." << std::endl;
}

void UIManager::settingsNewBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.newProject();
    ui->createCenterContentArea();
    std::cout << "Settings: New project created." << std::endl;
}

void UIManager::settingsLoadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsProject = true;
    ui->mFileBrowserIsSave = false;
    ui->openFileBrowser(false);
    std::cout << "Settings: Load button pressed." << std::endl;
}

void UIManager::settingsCreditsBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);

    // Create full-screen overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    ui->mSettingsCreditsModal = overlay;

    // Credits card
    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 600, 440);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x00CCAA), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 20, 0);

    // Close button
    lv_obj_t* closeBtn = lv_button_create(card);
    lv_obj_set_size(closeBtn, 36, 36);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(closeBtn, 18, 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, settingsCreditsCloseEventCb, LV_EVENT_CLICKED, ui);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "LOOM - CREDITS & PRIVACY");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00CCAA), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Credits text
    lv_obj_t* credText = lv_label_create(card);
    lv_obj_set_width(credText, 550);
    lv_label_set_long_mode(credText, LV_LABEL_LONG_WRAP);
    lv_label_set_text(credText,
        "CREDITS\n\n"
        "SoundFont Playback Engine:\n"
        "  TinySoundFont v0.9 by Bernhard Schelling\n"
        "  MIT License - github.com/schellingb/TinySoundFont\n"
        "  Based on SFZero by Steve Folta\n\n"
        "Bundled Instrument Library:\n"
        "  GeneralUser GS SoundFont\n"
        "  Assembled by S. Christian Collins\n\n"
        "UI Framework:\n"
        "  LVGL v9.1 - MIT License\n\n"
        "Audio & Windowing:\n"
        "  SDL2 - zlib License\n\n"
        "---\n\n"
        "PRIVACY POLICY\n\n"
        "Loom does not require an account or sign-in.\n"
        "Loom will never ask for or collect any\n"
        "personal data."
    );
    lv_obj_set_style_text_font(credText, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(credText, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(credText, LV_ALIGN_TOP_LEFT, 0, 30);
}

void UIManager::settingsCreditsCloseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mSettingsCreditsModal) {
        lv_obj_delete(ui->mSettingsCreditsModal);
        ui->mSettingsCreditsModal = nullptr;
    }
}

void UIManager::settingsKeyboardModeSwitchEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    ui->mSettingsKeyboardMode = lv_obj_has_state(sw, LV_STATE_CHECKED);
    std::cout << "Settings: Keyboard Mode " << (ui->mSettingsKeyboardMode ? "ON" : "OFF") << std::endl;
}

void UIManager::settingsScreenDeleteEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mCpuLoadLabel = nullptr;
    ui->mCpuLoadLabelSystem = nullptr;
    ui->mSettingsRootDd = nullptr;
    ui->mSettingsScaleDd = nullptr;
    ui->mSettingsScaleBtn = nullptr;
    ui->mSettingsTabview = nullptr;
    ui->mSettingsPadGrid = nullptr;
    ui->mSettingsMidiTrackDd = nullptr;
    ui->mSettingsMidiInDd = nullptr;
    ui->mSettingsMidiOutDd = nullptr;
    ui->mMidiDeviceListLabel = nullptr;
    ui->mMidiMonitorConsoleLabel = nullptr;
    ui->mSettingsUpdateStatus = nullptr;
}

static void setBacklightPower(bool on) {
    if (on) {
        std::system("sudo sh -c 'echo 0 > /sys/class/backlight/rpi_backlight/bl_power' 2>/dev/null");
        std::system("vcgencmd display_power 1 2>/dev/null");
    } else {
        std::system("sudo sh -c 'echo 1 > /sys/class/backlight/rpi_backlight/bl_power' 2>/dev/null");
        std::system("vcgencmd display_power 0 2>/dev/null");
    }
}

void UIManager::update() {

    if (mNeedsScreenRebuild) {
        mNeedsScreenRebuild = false;
        createCenterContentArea();
        updateHighlighting();
    }

    if (mActiveNav == 0) {
        // Sync Synthesis Parameters in real time!
        std::lock_guard<std::recursive_mutex> lock(mEngine.getLock());
        for (const auto& w : mActiveParamWidgets) {
            if (w.widget) {
                float rawVal = mEngine.getTracks()[mActiveTrack].appliedParameters[w.paramId];
                float normalized = 0.0f;
                if (w.maxVal > w.minVal) {
                    normalized = mapNonLinearToLinear(rawVal, w.minVal, w.maxVal, w.labelText);
                }
                int lvVal = (int)(normalized * 1000.0f);
                if (lvVal < 0) lvVal = 0;
                if (lvVal > 1000) lvVal = 1000;

                if (lv_obj_check_type(w.widget, &lv_arc_class)) {
                    if (lv_arc_get_value(w.widget) != lvVal) {
                        lv_arc_set_value(w.widget, lvVal);
                    }
                } else {
                    if (lv_slider_get_value(w.widget) != lvVal) {
                        lv_slider_set_value(w.widget, lvVal, LV_ANIM_OFF);
                    }
                }

                if (w.valLbl) {
                    if (w.isPercent) {
                        int percentVal = (int)(normalized * 100.0f);
                        lv_label_set_text_fmt(w.valLbl, "%d%%", percentVal);
                    } else if (w.paramId == 300) {
                        int st = (int)roundf((rawVal - 0.5f) * 48.0f);
                        lv_label_set_text_fmt(w.valLbl, "%+dst", st);
                    } else if (w.paramId == 301) {
                        lv_label_set_text_fmt(w.valLbl, "%.2fx", rawVal * 4.0f);
                    } else if (w.paramId == 302) {
                        float speed = powf(rawVal, 3.0f) * 9.99f + 0.01f;
                        lv_label_set_text_fmt(w.valLbl, "%.2fx", speed);
                    } else if (w.paramId == 340 || w.paramId == 341) {
                        lv_label_set_text_fmt(w.valLbl, "%d", (int)(rawVal * 15.0f) + 1);
                    } else if (w.paramId == 320) {
                        const char* modeStr = "1-HIT";
                        if (rawVal < 0.16f) modeStr = "1-HIT";
                        else if (rawVal < 0.33f) modeStr = "SUSTN";
                        else if (rawVal < 0.50f) modeStr = "LOOP";
                        else if (rawVal < 0.66f) modeStr = "CHOP";
                        else if (rawVal < 0.83f) modeStr = "1-CHP";
                        else if (rawVal < 0.95f) modeStr = "L-CHP";
                        else modeStr = "SCRUB";
                        lv_label_set_text(w.valLbl, modeStr);
                    } else if (w.paramId == 150) {
                        int algo = (int)(rawVal * 31.99f);
                        lv_label_set_text_fmt(w.valLbl, "%d", algo);
                    } else if (w.paramId == 418) {
                        int count = (int)(rawVal * 95.0f + 5.0f);
                        lv_label_set_text_fmt(w.valLbl, "%d", count);
                    } else if (w.decimals == 0) {
                        lv_label_set_text_fmt(w.valLbl, "%d", (int)rawVal);
                    } else if (w.decimals == 1) {
                        lv_label_set_text_fmt(w.valLbl, "%.1f", rawVal);
                    } else if (w.decimals == 2) {
                        lv_label_set_text_fmt(w.valLbl, "%.2f", rawVal);
                    } else if (w.decimals == 3) {
                        lv_label_set_text_fmt(w.valLbl, "%.3f", rawVal);
                    } else {
                        lv_label_set_text_fmt(w.valLbl, "%.2f", rawVal);
                    }
                }
            }
        }

        // Sync SoundFont text display in real time if visible
        int engineType = mEngine.getTracks()[mActiveTrack].engineType;
        if (engineType == 9) {
            if (mSoundFontActivePresetLbl) {
                int activeP = mEngine.getTracks()[mActiveTrack].soundFontEngine.getPresetIndex();
                std::string pName = mEngine.getSoundFontPresetName(mActiveTrack, activeP);
                if (pName.empty()) pName = "General User GS Default";
                
                char expectedBuf[256];
                snprintf(expectedBuf, sizeof(expectedBuf), "PRESET: %d - %s", activeP, pName.c_str());
                const char* currentText = lv_label_get_text(mSoundFontActivePresetLbl);
                if (strcmp(currentText, expectedBuf) != 0) {
                    lv_label_set_text(mSoundFontActivePresetLbl, expectedBuf);
                }
            }
            if (mSoundFontActiveBankLbl) {
                std::string bankName = mEngine.getTracks()[mActiveTrack].lastSamplePath;
                size_t lastSlash = bankName.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    bankName = bankName.substr(lastSlash + 1);
                }
                if (bankName.empty()) bankName = "Default";
                char expectedBuf[256];
                snprintf(expectedBuf, sizeof(expectedBuf), "ACTIVE BANK: %s", bankName.c_str());
                const char* currentText = lv_label_get_text(mSoundFontActiveBankLbl);
                if (strcmp(currentText, expectedBuf) != 0) {
                    lv_label_set_text(mSoundFontActiveBankLbl, expectedBuf);
                }
            }
        } else if (engineType == 1) {
            if (mFmActivePresetLbl) {
                int activeP = mEngine.getTracks()[mActiveTrack].activeFmPreset;
                s_activeFmPreset[mActiveTrack] = activeP;
                const auto& custom = mEngine.getTracks()[mActiveTrack].fmEngine.mCustomPresets;
                std::string pName = (activeP < 32) ? FM_PRESET_NAMES[activeP] : (activeP - 32 < (int)custom.size() ? custom[activeP - 32].name : "Unknown");
                
                char expectedBuf[256];
                snprintf(expectedBuf, sizeof(expectedBuf), "PRESET: %d - %s", activeP, pName.c_str());
                const char* currentText = lv_label_get_text(mFmActivePresetLbl);
                if (strcmp(currentText, expectedBuf) != 0) {
                    lv_label_set_text(mFmActivePresetLbl, expectedBuf);
                }
            }
        }
    } else if (mActiveNav == 3) {
        // Sync FX Parameters in real time!
        std::lock_guard<std::recursive_mutex> lock(mEngine.getLock());
        for (const auto& w : mActiveFxWidgets) {
            if (w.widget) {
                float rawVal = mEngine.getTracks()[mActiveTrack].appliedParameters[w.paramId];
                float normalized = 0.0f;
                if (w.maxVal > w.minVal) {
                    normalized = (rawVal - w.minVal) / (w.maxVal - w.minVal);
                }
                int lvVal = (int)(normalized * 1000.0f);
                if (lvVal < 0) lvVal = 0;
                if (lvVal > 1000) lvVal = 1000;

                if (lv_obj_check_type(w.widget, &lv_arc_class)) {
                    if (lv_arc_get_value(w.widget) != lvVal) {
                        lv_arc_set_value(w.widget, lvVal);
                    }
                } else {
                    if (lv_slider_get_value(w.widget) != lvVal) {
                        lv_slider_set_value(w.widget, lvVal, LV_ANIM_OFF);
                    }
                }

                if (w.valLbl) {
                    if (w.isPercent) {
                        int percentVal = (int)(normalized * 100.0f);
                        lv_label_set_text_fmt(w.valLbl, "%d%%", percentVal);
                    } else if (w.decimals == 0) {
                        lv_label_set_text_fmt(w.valLbl, "%d", (int)rawVal);
                    } else if (w.decimals == 1) {
                        lv_label_set_text_fmt(w.valLbl, "%.1f", rawVal);
                    } else if (w.decimals == 2) {
                        lv_label_set_text_fmt(w.valLbl, "%.2f", rawVal);
                    } else if (w.decimals == 3) {
                        lv_label_set_text_fmt(w.valLbl, "%.3f", rawVal);
                    } else {
                        lv_label_set_text_fmt(w.valLbl, "%.2f", rawVal);
                    }
                }
            }
        }
    }

    if (mActiveNav == 6) {
        updateTransportVisuals();
        for (int i = 0; i < 8; ++i) {
            if (mMixerVolSliders[i]) {
                float vol = 0.0f;
                {
                    std::lock_guard<std::recursive_mutex> lock(mEngine.getLock());
                    vol = mEngine.getTracks()[i].volume;
                }
                int sliderVal = (int)(vol * 100.0f);
                if (lv_slider_get_value(mMixerVolSliders[i]) != sliderVal) {
                    lv_slider_set_value(mMixerVolSliders[i], sliderVal, LV_ANIM_OFF);
                    if (mMixerVolLabels[i]) {
                        lv_label_set_text_fmt(mMixerVolLabels[i], "%d%%", sliderVal);
                    }
                }
            }
        }
    } else if (mActiveNav == 4 && mAssignActiveTabIdx == 0) {
        // Sync hardware controls screen in real time
        for (int k = 0; k < mSettingsKnobCount; ++k) {
            if (mAssignKnobArcs[k]) {
                int val = (int)(mSeqMidiKnobValue[mActiveTrack][k] * 100);
                if (lv_arc_get_value(mAssignKnobArcs[k]) != val) {
                    lv_arc_set_value(mAssignKnobArcs[k], val);
                    if (mAssignKnobValLabels[k]) {
                        lv_label_set_text_fmt(mAssignKnobValLabels[k], "%d%%", val);
                    }
                }
            }
        }
        for (int f = 0; f < mSettingsSliderCount; ++f) {
            if (mAssignFaderSliders[f]) {
                int val = (int)(mSeqMidiFaderValue[mActiveTrack][f] * 100);
                if (lv_slider_get_value(mAssignFaderSliders[f]) != val) {
                    lv_slider_set_value(mAssignFaderSliders[f], val, LV_ANIM_OFF);
                    if (mAssignFaderValLabels[f]) {
                        lv_label_set_text_fmt(mAssignFaderValLabels[f], "%d%%", val);
                    }
                }
            }
        }
    } else if (mActiveNav == 5) {
        static uint32_t lastCpuUpdateMs = 0;
        uint32_t now = SDL_GetTicks();
        if (now - lastCpuUpdateMs > 500 || lastCpuUpdateMs == 0) {
            lastCpuUpdateMs = now;
            if (mCpuLoadLabel != nullptr) {
                lv_label_set_text_fmt(mCpuLoadLabel, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
            }
            if (mCpuLoadLabelSystem != nullptr) {
                lv_label_set_text_fmt(mCpuLoadLabelSystem, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
            }
        }

        // Rate-limit USB/MIDI device scanning to once per second
        static uint32_t lastScanMs = 0;
        now = SDL_GetTicks();
        if (now - lastScanMs > 1000 || lastScanMs == 0) {
            lastScanMs = now;
            if (mMidiDeviceListLabel != nullptr) {
                std::vector<std::string> midiDevs = getSystemConnectedMidiInputs();
                std::vector<std::string> joyDevs = getSystemConnectedJoysticks();
                
                std::string listStr = "MIDI IN:\n";
                for (const auto& dev : midiDevs) {
                    listStr += "- " + dev + "\n";
                }
                listStr += "\nUSB CONTROLLERS:\n";
                for (const auto& dev : joyDevs) {
                    listStr += "- " + dev + "\n";
                }
                if (!listStr.empty() && listStr.back() == '\n') {
                    listStr.pop_back();
                }
                lv_label_set_text(mMidiDeviceListLabel, listStr.c_str());
            }
        }

        // Update Updater status
        if (mSettingsUpdateStatus != nullptr) {
            if (mUpdateInstallActive) {
                lv_label_set_text_fmt(mSettingsUpdateStatus, "Status: %s\nProgress: %d%%", 
                                      mUpdateInstallStatusStr.c_str(), mUpdateInstallProgressPercent);
            } else if (mUpdateInstallFinished) {
                lv_label_set_text_fmt(mSettingsUpdateStatus, "Status: Finished\n%s", mUpdateInstallStatusStr.c_str());
            } else {
                lv_label_set_text(mSettingsUpdateStatus, "Status: Idle\nReady to update.");
            }
        }

        // Update MIDI console monitor
        if (mMidiMonitorConsoleLabel != nullptr) {
            std::lock_guard<std::mutex> lock(mMidiLogMutex);
            if (mMidiLog.empty()) {
                lv_label_set_text(mMidiMonitorConsoleLabel, "(No MIDI events yet)");
            } else {
                std::string consoleText = "";
                // Display latest events at the bottom, or reverse to show newest at top (newest at top is better for scrolling)
                for (auto it = mMidiLog.rbegin(); it != mMidiLog.rend(); ++it) {
                    if (it->typeStr == "CC") {
                        consoleText += "Ch " + std::to_string(it->channel) + ": CC " + std::to_string(it->data1) + 
                                       " (Val " + std::to_string(it->data2) + ")\n";
                    } else if (it->typeStr == "Note On") {
                        consoleText += "Ch " + std::to_string(it->channel) + ": Note On " + std::to_string(it->data1) + 
                                       " (Vel " + std::to_string(it->data2) + ")\n";
                    } else if (it->typeStr == "Note Off") {
                        consoleText += "Ch " + std::to_string(it->channel) + ": Note Off " + std::to_string(it->data1) + "\n";
                    }
                }
                if (!consoleText.empty() && consoleText.back() == '\n') {
                    consoleText.pop_back();
                }
                lv_label_set_text(mMidiMonitorConsoleLabel, consoleText.c_str());
            }
        }
    } else if (mActiveNav == 2) {
        // Sync sequencer UI step buttons state with engine state (crucial for live recording!)
        bool isDrum = false;
        int activeDrumIdx = -1;
        int numDrumLanes = 0;
        int engineType = mEngine.getTracks()[mActiveTrack].engineType;
        bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.getPlayMode() >= 3);
        if (engineType == 5 || engineType == 6 || isSamplerChops) {
            isDrum = true;
            activeDrumIdx = mActiveDrumIdx;
            numDrumLanes = isSamplerChops ? (int)mEngine.getTracks()[mActiveTrack].samplerEngine.getSlices().size() : 8;
            if (numDrumLanes > 16) numDrumLanes = 16;
        }

        std::vector<Step> steps;
        if (isDrum) {
            steps = mEngine.getDrumSequencerSteps(mActiveTrack, activeDrumIdx);
        } else {
            steps = mEngine.getSequencerSteps(mActiveTrack);
        }

        // Cache the active states of OTHER drum lanes for the 15% opacity highlight
        bool otherLanesActive[64] = {false};
        if (isDrum) {
            for (int d = 0; d < numDrumLanes; ++d) {
                if (d == activeDrumIdx) continue;
                for (int i = 0; i < 64; ++i) {
                    if (mEngine.getStepActive(mActiveTrack, i, d)) {
                        otherLanesActive[i] = true;
                    }
                }
            }
        }

        lv_color_t trackColor = getTrackColor(mActiveTrack);
        int seqLength = mSeqTrackLength[mActiveTrack];

        for (int i = 0; i < (int)steps.size() && i < 64; ++i) {
            mSeqTrackSteps[mActiveTrack][i] = steps[i].active;
            if (mSeqStepButtons[i]) {
                bool isBtnChecked = lv_obj_has_state(mSeqStepButtons[i], LV_STATE_CHECKED);
                if (isBtnChecked != steps[i].active) {
                    if (steps[i].active) {
                        lv_obj_add_state(mSeqStepButtons[i], LV_STATE_CHECKED);
                    } else {
                        lv_obj_clear_state(mSeqStepButtons[i], LV_STATE_CHECKED);
                    }
                }

                // If not active on current lane, check if active on other lanes for 15% opacity highlight
                if (!steps[i].active) {
                    if (otherLanesActive[i]) {
                        lv_obj_set_style_bg_color(mSeqStepButtons[i], trackColor, 0);
                        lv_obj_set_style_bg_opa(mSeqStepButtons[i], 38, 0);
                    } else {
                        bool withinLength = (i < seqLength);
                        lv_color_t inactiveBg = withinLength ? lv_color_hex(0x2A2A2A) : lv_color_hex(0x1A1A1A);
                        lv_obj_set_style_bg_color(mSeqStepButtons[i], inactiveBg, 0);
                        lv_obj_set_style_bg_opa(mSeqStepButtons[i], LV_OPA_COVER, 0);
                    }
                } else {
                    // Reset styling for checked state (full cover highlight)
                    lv_obj_set_style_bg_color(mSeqStepButtons[i], trackColor, 0);
                    lv_obj_set_style_bg_opa(mSeqStepButtons[i], LV_OPA_COVER, 0);
                }
            }
        }
        
        // Highlight active playing step (Playhead Tracking)
        int currentStep = mEngine.getIsPlaying() ? mEngine.getCurrentStep(mActiveTrack, isDrum ? activeDrumIdx : -1) : -1;
        bool isRecording = mEngine.getIsRecording();
        lv_color_t playheadColor = isRecording ? lv_color_hex(0xEF4444) : lv_color_hex(0xFFFFFF);
        for (int i = 0; i < 64; ++i) {
            if (mSeqStepButtons[i]) {
                if (i == currentStep) {
                    lv_obj_set_style_border_width(mSeqStepButtons[i], 3, 0);
                    lv_obj_set_style_border_color(mSeqStepButtons[i], playheadColor, 0);
                } else {
                    lv_obj_set_style_border_width(mSeqStepButtons[i], 0, 0);
                }
            }
        }
    } else if (mActiveNav == 0) {
        if (mEngine.getTracks()[mActiveTrack].engineType == 2) {
            updateSamplerWaveformPreview();
        } else if (mEngine.getTracks()[mActiveTrack].engineType == 3) {
            updateGranularWaveformPreview();
        }
    }

    // Real-time Bluetooth UI update
    if (mBtModal != nullptr) {
        if (mBtStatusChanged) {
            mBtStatusChanged = false;
            if (mBtStatusLabel) {
                lv_label_set_text_fmt(mBtStatusLabel, "Status: %s", mBtStatusStr.c_str());
            }
        }

        if (mBtDeviceListChanged) {
            mBtDeviceListChanged = false;
            if (mBtListContainer) {
                // Clear all children first
                lv_obj_clean(mBtListContainer);
                
                std::lock_guard<std::mutex> lock(mBtMutex);
                if (mBtDevices.empty()) {
                    lv_obj_t* emptyLbl = lv_label_create(mBtListContainer);
                    lv_label_set_text(emptyLbl, "No devices found.");
                    lv_obj_set_style_text_font(emptyLbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(emptyLbl, lv_color_hex(0x666666), 0);
                } else {
                    struct BtDeviceSelectData {
                        UIManager* ui;
                        std::string mac;
                    };

                    for (const auto& dev : mBtDevices) {
                        lv_obj_t* btn = lv_button_create(mBtListContainer);
                        lv_obj_set_size(btn, 480, 40);
                        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
                        lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
                        lv_obj_set_style_border_width(btn, 1, 0);
                        
                        lv_obj_t* btnLbl = lv_label_create(btn);
                        lv_label_set_text_fmt(btnLbl, "%s   [%s]", dev.name.c_str(), dev.mac.c_str());
                        lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_12, 0);
                        lv_obj_center(btnLbl);
                        
                        BtDeviceSelectData* selectData = new BtDeviceSelectData{this, dev.mac};
                        lv_obj_add_event_cb(btn, btDeviceSelectEventCb, LV_EVENT_CLICKED, selectData);
                        
                        auto selectFreeCb = [](lv_event_t* e) {
                            BtDeviceSelectData* d = (BtDeviceSelectData*)lv_event_get_user_data(e);
                            delete d;
                        };
                        lv_obj_add_event_cb(btn, selectFreeCb, LV_EVENT_DELETE, selectData);
                    }
                }
            }
        }
    }
}

// =========================================================================
// --- Sequencer Screen ---
// =========================================================================

// Clock div labels (index 0-6): div3, div2, div1.5, x1, x1.5, x2, x3
static const char* kClkLabels[] = { "div3", "div2", "div1.5", "x1", "x1.5", "x2", "x3" };

void UIManager::populateSeqScreen() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);
    // Outer horizontal flex: [left tabview panel] [right side panel]
    lv_obj_t* outerRow = lv_obj_create(mCenterArea);
    lv_obj_set_size(outerRow, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(outerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outerRow, 0, 0);
    lv_obj_set_style_pad_all(outerRow, 0, 0);
    lv_obj_set_layout(outerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(outerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(outerRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(outerRow, LV_OBJ_FLAG_SCROLLABLE);

    // =========================================================================
    // LEFT PANEL: tabview with Sequencer + Pattern Chain tabs
    // =========================================================================
    lv_obj_t* tabview = lv_tabview_create(outerRow);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);
    lv_obj_set_flex_grow(tabview, 1);
    lv_obj_set_height(tabview, 600);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tabview, 0, 0);

    lv_obj_t* seqTab   = lv_tabview_add_tab(tabview, "Sequencer");
    lv_obj_t* chainTab = lv_tabview_add_tab(tabview, "Pattern Chain");

    lv_obj_set_style_pad_all(seqTab,   8, 0);
    lv_obj_set_style_pad_all(chainTab, 12, 0);
    lv_obj_remove_flag(seqTab,   LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(chainTab, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Sequencer Tab: grid container (rebuilt by rebuildSeqGrid) ----
    lv_obj_set_layout(seqTab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(seqTab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(seqTab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1. Check if drum/chop engine to build the horizontal tab row
    int engineType = mEngine.getTracks()[mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.getPlayMode() >= 3);
    
    if (engineType == 5 || engineType == 6 || isSamplerChops) {
        lv_obj_t* drumTabRow = lv_obj_create(seqTab);
        lv_obj_set_size(drumTabRow, lv_pct(100), 40);
        lv_obj_set_style_bg_opa(drumTabRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(drumTabRow, 0, 0);
        lv_obj_set_style_pad_all(drumTabRow, 0, 0);
        lv_obj_set_layout(drumTabRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(drumTabRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(drumTabRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(drumTabRow, 4, 0);
        lv_obj_remove_flag(drumTabRow, LV_OBJ_FLAG_SCROLLABLE);

        int numTabs = 8;
        if (isSamplerChops) {
            numTabs = (int)mEngine.getTracks()[mActiveTrack].samplerEngine.getSlices().size();
            if (numTabs > 16) numTabs = 16;
            if (numTabs < 1) numTabs = 1;
        }

        const char* fmDrumNames[] = { "KICK", "SNARE", "TOM", "HIHAT", "OHH", "CYMB", "PERC", "NOISE" };
        const char* analogDrumNames[] = { "KICK", "SNARE", "CLAP", "HAT C", "HAT O", "CYMB", "PERC", "NOISE" };

        for (int d = 0; d < numTabs; ++d) {
            lv_obj_t* tabBtn = lv_button_create(drumTabRow);
            lv_obj_set_size(tabBtn, isSamplerChops ? 42 : 74, 30);
            lv_obj_set_style_radius(tabBtn, 15, 0);
            lv_obj_set_style_pad_all(tabBtn, 2, 0);
            
            // Text label
            lv_obj_t* btnLbl = lv_label_create(tabBtn);
            if (engineType == 5) {
                lv_label_set_text(btnLbl, fmDrumNames[d]);
            } else if (engineType == 6) {
                lv_label_set_text(btnLbl, analogDrumNames[d]);
            } else {
                lv_label_set_text_fmt(btnLbl, "SL%d", d + 1);
            }
            lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(btnLbl);

            // Set active visual styling
            if (d == mActiveDrumIdx) {
                lv_obj_set_style_bg_color(tabBtn, trackColor, 0);
                lv_obj_set_style_bg_opa(tabBtn, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(btnLbl, lv_color_hex(0xFFFFFF), 0);
            } else {
                lv_obj_set_style_bg_color(tabBtn, lv_color_hex(0x1F1F1F), 0);
                lv_obj_set_style_bg_opa(tabBtn, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(btnLbl, lv_color_hex(0x888888), 0);
            }

            struct DrumTabClickData {
                UIManager* ui;
                int drumIdx;
            };
            DrumTabClickData* data = new DrumTabClickData{this, d};
            lv_obj_add_event_cb(tabBtn, seqDrumTabClickEventCb, LV_EVENT_CLICKED, data);

            // Register delete event callback to free user data
            auto dataFreeCb = [](lv_event_t* e) {
                DrumTabClickData* d = (DrumTabClickData*)lv_event_get_user_data(e);
                delete d;
            };
            lv_obj_add_event_cb(tabBtn, dataFreeCb, LV_EVENT_DELETE, data);
        }
    }

    // Grid container placeholder — rebuildSeqGrid() fills this
    mSeqGridContainer = lv_obj_create(seqTab);
    lv_obj_set_style_bg_opa(mSeqGridContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mSeqGridContainer, 0, 0);
    lv_obj_set_style_pad_all(mSeqGridContainer, 0, 0);
    lv_obj_remove_flag(mSeqGridContainer, LV_OBJ_FLAG_SCROLLABLE);

    rebuildSeqGrid();

    // ---- Pattern Chain Tab ----
    lv_obj_set_layout(chainTab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chainTab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chainTab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(chainTab, 8, 0);
    lv_obj_set_style_pad_column(chainTab, 8, 0);

    // 24 chain slot boxes in a 6-per-row wrap
    lv_obj_set_flex_flow(chainTab, LV_FLEX_FLOW_ROW_WRAP);

    static const char* kStubNames[] = { "", "Seq A", "Seq B", "Seq C" };

    for (int i = 0; i < 25; ++i) {
        lv_obj_t* box = lv_obj_create(chainTab);
        lv_obj_set_size(box, 108, 98);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_radius(box, 8, 0);
        lv_obj_set_style_pad_all(box, 6, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Slot number
        lv_obj_t* numLbl = lv_label_create(box);
        lv_label_set_text_fmt(numLbl, "%d", i + 1);
        lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(numLbl, lv_color_hex(0x555555), 0);

        // Filename label (empty until assigned)
        lv_obj_t* fileLbl = lv_label_create(box);
        const std::string& slot = mSeqChainSlots[i];
        if (slot.empty()) {
            lv_label_set_text(fileLbl, "—");
            lv_obj_set_style_text_color(fileLbl, lv_color_hex(0x444444), 0);
        } else {
            std::string disp = slot.length() > 12 ? slot.substr(0, 11) + "\xE2\x80\xA6" : slot;
            lv_label_set_text(fileLbl, disp.c_str());
            lv_obj_set_style_text_color(fileLbl, lv_color_hex(0xEEEEEE), 0);
            lv_obj_set_style_bg_color(box, lv_color_hex(0x242424), 0);
            lv_obj_set_style_border_color(box, getTrackColor(mActiveTrack), 0);
        }
        lv_obj_set_style_text_font(fileLbl, &lv_font_montserrat_12, 0);

        lv_obj_add_event_cb(box, seqChainBoxEventCb, LV_EVENT_CLICKED, this);
        // Pack slot index as user data via a static offset trick
        lv_obj_set_user_data(box, (void*)(uintptr_t)i);
    }

    // =========================================================================
    // RIGHT SIDE PANEL: controls
    // =========================================================================
    lv_obj_t* sidePanel = lv_obj_create(outerRow);
    lv_obj_set_size(sidePanel, 215, 600);
    lv_obj_set_style_bg_color(sidePanel, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(sidePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sidePanel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(sidePanel, 1, 0);
    lv_obj_set_style_pad_hor(sidePanel, 10, 0);
    lv_obj_set_style_pad_ver(sidePanel, 8, 0);
    lv_obj_remove_flag(sidePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sidePanel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sidePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidePanel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Helper: build a small group container for label + control inside sidePanel
    auto makeSideGroup = [&](lv_obj_t* parent, int h) -> lv_obj_t* {
        lv_obj_t* grp = lv_obj_create(parent);
        lv_obj_set_size(grp, 193, h);
        lv_obj_set_style_bg_opa(grp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grp, 0, 0);
        lv_obj_set_style_pad_all(grp, 0, 0);
        lv_obj_remove_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(grp, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return grp;
    };

    // Helper: standard section label
    auto makeSideLabel = [&](lv_obj_t* parent, const char* text) -> lv_obj_t* {
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        return lbl;
    };

    // --- 1. 4×4 / 8×8 toggle & Play Order side-by-side ---
    lv_obj_t* toggleGrp = makeSideGroup(sidePanel, 44);
    lv_obj_set_flex_flow(toggleGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggleGrp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* toggleBtn = lv_button_create(toggleGrp);
    lv_obj_set_size(toggleBtn, 90, 38);
    lv_obj_add_flag(toggleBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(toggleBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(toggleBtn, trackColor, LV_STATE_CHECKED);
    lv_obj_set_style_radius(toggleBtn, 8, 0);

    bool is4x4 = mSeqTrackIs4x4[mActiveTrack];
    if (is4x4) {
        lv_obj_add_state(toggleBtn, LV_STATE_CHECKED);
    }

    lv_obj_t* toggleLbl = lv_label_create(toggleBtn);
    lv_label_set_text(toggleLbl, is4x4 ? "8x8 View" : "4x4 View");
    lv_obj_set_style_text_font(toggleLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(toggleLbl);
    lv_obj_add_event_cb(toggleBtn, seqGridToggleBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* playOrderBtn = lv_button_create(toggleGrp);
    lv_obj_set_size(playOrderBtn, 90, 38);
    lv_obj_set_style_bg_color(playOrderBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(playOrderBtn, 8, 0);

    lv_obj_t* playOrderLbl = lv_label_create(playOrderBtn);
    int trackDir = mEngine.getPlaybackDirection(mActiveTrack);
    bool isRnd = mEngine.getIsRandomOrder(mActiveTrack);
    const char* dirText = "Fwd";
    if (isRnd) {
        dirText = "Rnd";
    } else if (trackDir == 1) {
        dirText = "Rev";
    } else if (trackDir == 2) {
        dirText = "P-P";
    }
    lv_label_set_text(playOrderLbl, dirText);
    lv_obj_set_style_text_font(playOrderLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(playOrderLbl);
    lv_obj_add_event_cb(playOrderBtn, seqPlayOrderBtnEventCb, LV_EVENT_CLICKED, this);

    // --- 2. Knob grid: Length, Humanize, Probability, Clock Div in a 2x2 layout ---
    lv_obj_t* arcGrid = lv_obj_create(sidePanel);
    lv_obj_set_size(arcGrid, 193, 158);
    lv_obj_set_style_bg_opa(arcGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arcGrid, 0, 0);
    lv_obj_set_style_pad_all(arcGrid, 0, 0);
    lv_obj_remove_flag(arcGrid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(arcGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(arcGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(arcGrid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_row(arcGrid, 6, 0);
    lv_obj_set_style_pad_column(arcGrid, 4, 0);

    // Helper: make a single arc cell (arc on top, text label below)
    auto makeArcCell = [&](lv_obj_t* parent) -> lv_obj_t* {
        lv_obj_t* cell = lv_obj_create(parent);
        lv_obj_set_size(cell, 88, 76);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 3, 0);
        return cell;
    };

    // Cell 1: Length
    lv_obj_t* lenCell = makeArcCell(arcGrid);
    lv_obj_t* lenArc = lv_arc_create(lenCell);
    lv_obj_set_size(lenArc, 46, 46);
    lv_arc_set_range(lenArc, 1, 64);
    lv_arc_set_value(lenArc, mSeqTrackLength[mActiveTrack]);
    lv_obj_set_style_arc_color(lenArc, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(lenArc, trackColor, LV_PART_KNOB);
    lv_obj_set_style_pad_all(lenArc, 0, 0);
    lv_obj_add_event_cb(lenArc, seqLengthArcEventCb, LV_EVENT_VALUE_CHANGED, this);
    mSeqLengthLbl = makeSideLabel(lenCell, "");
    lv_label_set_text_fmt(mSeqLengthLbl, "Length: %d", mSeqTrackLength[mActiveTrack]);

    // Cell 2: Humanize
    lv_obj_t* humCell = makeArcCell(arcGrid);
    lv_obj_t* humArc = lv_arc_create(humCell);
    lv_obj_set_size(humArc, 46, 46);
    lv_arc_set_range(humArc, 0, 100);
    lv_arc_set_value(humArc, mSeqTrackHumanize[mActiveTrack]);
    lv_obj_set_style_arc_color(humArc, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(humArc, trackColor, LV_PART_KNOB);
    lv_obj_set_style_pad_all(humArc, 0, 0);
    mSeqHumanValLbl = lv_label_create(humArc);
    lv_label_set_text_fmt(mSeqHumanValLbl, "%d%%", mSeqTrackHumanize[mActiveTrack]);
    lv_obj_set_style_text_font(mSeqHumanValLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(mSeqHumanValLbl);
    lv_obj_add_event_cb(humArc, seqHumanizeArcEventCb, LV_EVENT_VALUE_CHANGED, this);
    makeSideLabel(humCell, "Humanize");

    // Cell 3: Probability
    lv_obj_t* probCell = makeArcCell(arcGrid);
    lv_obj_t* probArc = lv_arc_create(probCell);
    lv_obj_set_size(probArc, 46, 46);
    lv_arc_set_range(probArc, 0, 100);
    lv_arc_set_value(probArc, mSeqTrackProbability[mActiveTrack]);
    lv_obj_set_style_arc_color(probArc, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(probArc, trackColor, LV_PART_KNOB);
    lv_obj_set_style_pad_all(probArc, 0, 0);
    mSeqProbLbl = lv_label_create(probArc);
    lv_label_set_text_fmt(mSeqProbLbl, "%d%%", mSeqTrackProbability[mActiveTrack]);
    lv_obj_set_style_text_font(mSeqProbLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(mSeqProbLbl);
    lv_obj_add_event_cb(probArc, seqProbArcEventCb, LV_EVENT_VALUE_CHANGED, this);
    makeSideLabel(probCell, "Prob");

    // Cell 4: Clock Div
    lv_obj_t* clkCell = makeArcCell(arcGrid);
    lv_obj_t* clkArc = lv_arc_create(clkCell);
    lv_obj_set_size(clkArc, 46, 46);
    lv_arc_set_range(clkArc, 0, 6);
    lv_arc_set_value(clkArc, mSeqTrackClockDivIndex[mActiveTrack]);
    lv_obj_set_style_arc_color(clkArc, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(clkArc, trackColor, LV_PART_KNOB);
    lv_obj_set_style_pad_all(clkArc, 0, 0);
    mSeqClockLbl = lv_label_create(clkArc);
    lv_label_set_text(mSeqClockLbl, kClkLabels[mSeqTrackClockDivIndex[mActiveTrack]]);
    lv_obj_set_style_text_font(mSeqClockLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(mSeqClockLbl);
    lv_obj_add_event_cb(clkArc, seqClockDivArcEventCb, LV_EVENT_VALUE_CHANGED, this);
    makeSideLabel(clkCell, "Clock Div");


    // --- 5. Transpose row ---
    lv_obj_t* transpGrp = makeSideGroup(sidePanel, 60);
    makeSideLabel(transpGrp, "Transpose");
    lv_obj_t* transpRow = lv_obj_create(transpGrp);
    lv_obj_set_size(transpRow, 190, 34);
    lv_obj_set_style_bg_opa(transpRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(transpRow, 0, 0);
    lv_obj_set_style_pad_all(transpRow, 0, 0);
    lv_obj_set_layout(transpRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(transpRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transpRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* tDnBtn = lv_button_create(transpRow);
    lv_obj_set_size(tDnBtn, 50, 30);
    lv_obj_set_style_radius(tDnBtn, 6, 0);
    lv_obj_set_style_bg_color(tDnBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* tDnLbl = lv_label_create(tDnBtn); lv_label_set_text(tDnLbl, "-"); lv_obj_center(tDnLbl);
    lv_obj_add_event_cb(tDnBtn, seqTransposeBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(tDnBtn, (void*)(uintptr_t)0); // 0 = decrement

    mSeqTransposeLbl = lv_label_create(transpRow);
    char tBuf[8];
    snprintf(tBuf, sizeof(tBuf), "%+d", mSeqTrackTranspose[mActiveTrack]);
    lv_label_set_text(mSeqTransposeLbl, tBuf);
    lv_obj_set_style_text_font(mSeqTransposeLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSeqTransposeLbl, trackColor, 0);

    lv_obj_t* tUpBtn = lv_button_create(transpRow);
    lv_obj_set_size(tUpBtn, 50, 30);
    lv_obj_set_style_radius(tUpBtn, 6, 0);
    lv_obj_set_style_bg_color(tUpBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* tUpLbl = lv_label_create(tUpBtn); lv_label_set_text(tUpLbl, "+"); lv_obj_center(tUpLbl);
    lv_obj_add_event_cb(tUpBtn, seqTransposeBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(tUpBtn, (void*)(uintptr_t)1); // 1 = increment

    // --- 6. Octave row ---
    lv_obj_t* octGrp = makeSideGroup(sidePanel, 60);
    makeSideLabel(octGrp, "Octave");
    lv_obj_t* octRow = lv_obj_create(octGrp);
    lv_obj_set_size(octRow, 190, 34);
    lv_obj_set_style_bg_opa(octRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(octRow, 0, 0);
    lv_obj_set_style_pad_all(octRow, 0, 0);
    lv_obj_set_layout(octRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(octRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(octRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* oDnBtn = lv_button_create(octRow);
    lv_obj_set_size(oDnBtn, 50, 30);
    lv_obj_set_style_radius(oDnBtn, 6, 0);
    lv_obj_set_style_bg_color(oDnBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* oDnLbl2 = lv_label_create(oDnBtn); lv_label_set_text(oDnLbl2, "-"); lv_obj_center(oDnLbl2);
    lv_obj_add_event_cb(oDnBtn, seqOctaveBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(oDnBtn, (void*)(uintptr_t)0);

    mSeqOctaveLbl = lv_label_create(octRow);
    char oBuf[8];
    snprintf(oBuf, sizeof(oBuf), "%+d", mSeqTrackOctave[mActiveTrack]);
    lv_label_set_text(mSeqOctaveLbl, oBuf);
    lv_obj_set_style_text_font(mSeqOctaveLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSeqOctaveLbl, trackColor, 0);

    lv_obj_t* oUpBtn = lv_button_create(octRow);
    lv_obj_set_size(oUpBtn, 50, 30);
    lv_obj_set_style_radius(oUpBtn, 6, 0);
    lv_obj_set_style_bg_color(oUpBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* oUpLbl2 = lv_label_create(oUpBtn); lv_label_set_text(oUpLbl2, "+"); lv_obj_center(oUpLbl2);
    lv_obj_add_event_cb(oUpBtn, seqOctaveBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(oUpBtn, (void*)(uintptr_t)1);

    // --- 7. Copy / Paste / Clear buttons ---
    lv_obj_t* cpGrp = makeSideGroup(sidePanel, 38);
    lv_obj_set_flex_flow(cpGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cpGrp, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* copyBtn = lv_button_create(cpGrp);
    lv_obj_set_size(copyBtn, 56, 30);
    lv_obj_set_style_bg_color(copyBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(copyBtn, trackColor, 0);
    lv_obj_set_style_border_width(copyBtn, 1, 0);
    lv_obj_set_style_radius(copyBtn, 6, 0);
    lv_obj_t* copyLbl = lv_label_create(copyBtn); lv_label_set_text(copyLbl, "Copy");
    lv_obj_set_style_text_font(copyLbl, &lv_font_montserrat_12, 0); lv_obj_center(copyLbl);
    lv_obj_add_event_cb(copyBtn, seqCopyBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* pasteBtn = lv_button_create(cpGrp);
    lv_obj_set_size(pasteBtn, 56, 30);
    lv_obj_set_style_bg_color(pasteBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(pasteBtn, trackColor, 0);
    lv_obj_set_style_border_width(pasteBtn, 1, 0);
    lv_obj_set_style_radius(pasteBtn, 6, 0);
    lv_obj_t* pasteLbl = lv_label_create(pasteBtn); lv_label_set_text(pasteLbl, "Paste");
    lv_obj_set_style_text_font(pasteLbl, &lv_font_montserrat_12, 0); lv_obj_center(pasteLbl);
    lv_obj_add_event_cb(pasteBtn, seqPasteBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* clearBtn = lv_button_create(cpGrp);
    lv_obj_set_size(clearBtn, 56, 30);
    lv_obj_set_style_bg_color(clearBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(clearBtn, trackColor, 0);
    lv_obj_set_style_border_width(clearBtn, 1, 0);
    lv_obj_set_style_radius(clearBtn, 6, 0);
    lv_obj_t* clearLbl = lv_label_create(clearBtn); lv_label_set_text(clearLbl, "Clear");
    lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_12, 0); lv_obj_center(clearLbl);
    lv_obj_add_event_cb(clearBtn, seqClearBtnEventCb, LV_EVENT_CLICKED, this);

    // --- 8. Save / Load buttons ---
    lv_obj_t* slGrp = makeSideGroup(sidePanel, 38);
    lv_obj_set_flex_flow(slGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slGrp, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* saveBtn = lv_button_create(slGrp);
    lv_obj_set_size(saveBtn, 84, 30);
    lv_obj_set_style_bg_color(saveBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(saveBtn, trackColor, 0);
    lv_obj_set_style_border_width(saveBtn, 1, 0);
    lv_obj_set_style_radius(saveBtn, 6, 0);
    lv_obj_t* saveLbl = lv_label_create(saveBtn); lv_label_set_text(saveLbl, "Save");
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0); lv_obj_center(saveLbl);
    lv_obj_add_event_cb(saveBtn, seqSaveBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* loadBtn = lv_button_create(slGrp);
    lv_obj_set_size(loadBtn, 84, 30);
    lv_obj_set_style_bg_color(loadBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(loadBtn, trackColor, 0);
    lv_obj_set_style_border_width(loadBtn, 1, 0);
    lv_obj_set_style_radius(loadBtn, 6, 0);
    lv_obj_t* loadLbl = lv_label_create(loadBtn); lv_label_set_text(loadLbl, "Load");
    lv_obj_set_style_text_font(loadLbl, &lv_font_montserrat_12, 0); lv_obj_center(loadLbl);
    lv_obj_add_event_cb(loadBtn, seqLoadBtnEventCb, LV_EVENT_CLICKED, this);
}

// ---- rebuildSeqGrid: renders 8×8 or 4×4 step grid ----
void UIManager::rebuildSeqGrid() {
    if (!mSeqGridContainer) return;
    lv_obj_clean(mSeqGridContainer);

    bool is4x4 = mSeqTrackIs4x4[mActiveTrack];
    int cols = is4x4 ? 4 : 8;
    int rows = is4x4 ? 4 : 8;
    int btnW = is4x4 ? 128 : 62;
    int btnH = is4x4 ? 108 : 62;
    int gap  = is4x4 ? 6   : 4;

    int gridW = cols * btnW + (cols - 1) * gap;
    int gridH = rows * btnH + (rows - 1) * gap;
    lv_obj_set_size(mSeqGridContainer, gridW, gridH);

    // Use LVGL grid layout with explicit column/row descriptors
    static lv_coord_t colDesc8[9];
    static lv_coord_t colDesc4[5];
    static lv_coord_t rowDesc8[9];
    static lv_coord_t rowDesc4[5];

    lv_coord_t* colDesc = is4x4 ? colDesc4 : colDesc8;
    lv_coord_t* rowDesc = is4x4 ? rowDesc4 : rowDesc8;

    for (int i = 0; i < cols; ++i) colDesc[i] = btnW;
    colDesc[cols] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < rows; ++i) rowDesc[i] = btnH;
    rowDesc[rows] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_layout(mSeqGridContainer, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(mSeqGridContainer, colDesc, 0);
    lv_obj_set_style_grid_row_dsc_array(mSeqGridContainer, rowDesc, 0);
    lv_obj_set_style_pad_column(mSeqGridContainer, gap, 0);
    lv_obj_set_style_pad_row(mSeqGridContainer, gap, 0);

    int totalSteps = is4x4 ? 16 : 64;
    int seqLength = mSeqTrackLength[mActiveTrack];
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Query active steps on other lanes for 15% opacity highlight
    bool otherLanesActive[64] = {false};
    bool isDrum = false;
    int activeDrumIdx = -1;
    int numDrumLanes = 0;
    int engineType = mEngine.getTracks()[mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.getPlayMode() >= 3);
    if (engineType == 5 || engineType == 6 || isSamplerChops) {
        isDrum = true;
        activeDrumIdx = mActiveDrumIdx;
        numDrumLanes = isSamplerChops ? (int)mEngine.getTracks()[mActiveTrack].samplerEngine.getSlices().size() : 8;
        if (numDrumLanes > 16) numDrumLanes = 16;
    }

    if (isDrum) {
        for (int d = 0; d < numDrumLanes; ++d) {
            if (d == activeDrumIdx) continue;
            for (int i = 0; i < 64; ++i) {
                if (mEngine.getStepActive(mActiveTrack, i, d)) {
                    otherLanesActive[i] = true;
                }
            }
        }
    }

    for (int i = 0; i < totalSteps; ++i) {
        int col = i % cols;
        int row = i / cols;

        lv_obj_t* btn = lv_button_create(mSeqGridContainer);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, col, 1,
                                   LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        bool withinLength = (i < seqLength);
        lv_color_t inactiveBg = withinLength ? lv_color_hex(0x2A2A2A) : lv_color_hex(0x1A1A1A);
        lv_obj_set_style_bg_color(btn, inactiveBg, 0);
        lv_obj_set_style_bg_color(btn, trackColor, LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);

        if (!withinLength) {
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }

        // Restore step state from active track's sequence
        bool activeOnCurrent = false;
        if (isDrum) {
            activeOnCurrent = mEngine.getStepActive(mActiveTrack, i, activeDrumIdx);
        } else {
            activeOnCurrent = mSeqTrackSteps[mActiveTrack][i];
        }

        if (activeOnCurrent) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        } else if (otherLanesActive[i]) {
            lv_obj_set_style_bg_color(btn, trackColor, 0);
            lv_obj_set_style_bg_opa(btn, 38, 0);
        }

        lv_obj_set_user_data(btn, (void*)(uintptr_t)i);
        lv_obj_add_event_cb(btn, seqGridBtnEventCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(btn, seqStepLongPressEventCb, LV_EVENT_LONG_PRESSED, this);
        mSeqStepButtons[i] = btn;
    }
    // Clear remaining slots beyond totalSteps
    for (int i = totalSteps; i < 64; ++i) {
        mSeqStepButtons[i] = nullptr;
    }
}

// ---- File Browser Modal ----
void UIManager::openFileBrowser(bool isSave) {
    if (mSeqModal) closeFileBrowser();
    mFileBrowserIsSave = isSave;

    // Full-screen dimmed overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    mSeqModal = overlay;

    // Modal card
    lv_obj_t* card = lv_obj_create(overlay);
    if (isSave) {
        lv_obj_set_size(card, 560, 260); // Compact height to avoid virtual keyboard overlap on 800x480 screens
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 5);
    } else {
        lv_obj_set_size(card, 560, 420); // Taller height to show more files comfortably without clipping bottom buttons
        lv_obj_center(card);
    }
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, isSave ? 6 : 8, 0);

    // Title
    lv_obj_t* titleLbl = lv_label_create(card);
    const char* titleText = "Load Sequence";
    if (mFileBrowserIsProject) titleText = isSave ? "Save Project" : "Load Project";
    else if (mFileBrowserIsFmImport) titleText = "Import FM Preset";
    else if (mFileBrowserIsWtSelect) titleText = "Select Wavetable";
    else if (mFileBrowserIsWtImport) titleText = "Import WAV File";
    else if (mFileBrowserIsSfSelect) titleText = "Select SoundFont";
    else if (mFileBrowserIsSfImport) titleText = "Import SoundFont";
    else if (mFileBrowserIsSampleLoad) titleText = "Load Sample";
    else if (mFileBrowserIsSampleSave) titleText = "Save Sample";
    else if (mFileBrowserIsPresetLoad) titleText = "Load Preset";
    else if (mFileBrowserIsPresetSave) titleText = "Save Preset";
    else if (isSave) titleText = "Save Sequence";
    lv_label_set_text(titleLbl, titleText);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xEEEEEE), 0);

    // Initialize current path if empty
    const char* browseDir = getenv("HOME");
    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
    
    // Ensure all target engine directories exist
    mkdir(homeStr.c_str(), 0777); // Create ~/Loom parent first
    mkdir((homeStr + "/samples").c_str(), 0777);
    mkdir((homeStr + "/wavetables").c_str(), 0777);
    mkdir((homeStr + "/granular").c_str(), 0777);
    mkdir((homeStr + "/soundfonts").c_str(), 0777);
    mkdir((homeStr + "/sequences").c_str(), 0777);
    mkdir((homeStr + "/presets").c_str(), 0777);
    mkdir((homeStr + "/presets/fm").c_str(), 0777); // Ensure fm subfolder exists
    mkdir((homeStr + "/projects").c_str(), 0777);

    if (mFileBrowserCurrentPath.empty()) {
        if (mFileBrowserIsProject) {
            mFileBrowserCurrentPath = homeStr + "/projects";
        } else if (mFileBrowserIsFmImport) {
            mFileBrowserCurrentPath = homeStr + "/presets/fm";
        } else if (mFileBrowserIsPresetLoad || mFileBrowserIsPresetSave) {
            mFileBrowserCurrentPath = homeStr + "/presets";
        } else if (mFileBrowserIsWtSelect) {
            mFileBrowserCurrentPath = homeStr + "/wavetables";
        } else if (mFileBrowserIsSfSelect || mFileBrowserIsSfImport) {
            mFileBrowserCurrentPath = homeStr + "/soundfonts";
        } else if (mFileBrowserIsSampleLoad || mFileBrowserIsSampleSave) {
            mFileBrowserCurrentPath = homeStr + "/samples";
        } else {
            mFileBrowserCurrentPath = homeStr + "/sequences";
        }
    }

    // Current path label
    lv_obj_t* pathLbl = lv_label_create(card);
    std::string displayPath = mFileBrowserCurrentPath;
    if (browseDir && displayPath.rfind(browseDir, 0) == 0) {
        displayPath = "~" + displayPath.substr(strlen(browseDir));
    }
    lv_label_set_text(pathLbl, displayPath.c_str());
    lv_obj_set_style_text_font(pathLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pathLbl, lv_color_hex(0x888888), 0);

    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Dynamic Shortcut Buttons for Audio Loaders and Presets
    bool showShortcuts = !isSave && (mFileBrowserIsSampleLoad || mFileBrowserIsWtSelect || mFileBrowserIsWtImport || mFileBrowserIsSfSelect || mFileBrowserIsSfImport || mFileBrowserIsFmImport || mFileBrowserIsPresetLoad);
    if (showShortcuts) {
        lv_obj_t* folderRow = lv_obj_create(card);
        lv_obj_set_size(folderRow, 528, 40);
        lv_obj_set_style_bg_opa(folderRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(folderRow, 0, 0);
        lv_obj_set_style_pad_all(folderRow, 0, 0);
        lv_obj_remove_flag(folderRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(folderRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(folderRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(folderRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        struct ShortcutData {
            UIManager* ui;
            std::string path;
        };

        auto createShortcutBtn = [this, trackColor, folderRow](const char* name, const std::string& targetPath) {
            lv_obj_t* btn = lv_btn_create(folderRow);
            lv_obj_set_size(btn, (mFileBrowserIsFmImport || mFileBrowserIsPresetLoad) ? 250 : 120, 30);
            
            bool isActive = (mFileBrowserCurrentPath == targetPath);
            if (isActive) {
                lv_obj_set_style_bg_color(btn, trackColor, 0);
                lv_obj_set_style_border_color(btn, trackColor, 0);
            } else {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
            }
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, name);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(lbl);
            
            ShortcutData* data = new ShortcutData{this, targetPath};
            
            auto shortcutCb = [](lv_event_t* e) {
                ShortcutData* d = (ShortcutData*)lv_event_get_user_data(e);
                if (d) {
                    UIManager* ui = d->ui;
                    std::string targetPath = d->path;
                    ui->closeFileBrowser();
                    ui->mFileBrowserCurrentPath = targetPath;
                    ui->openFileBrowser(false);
                }
            };
            
            lv_obj_add_event_cb(btn, shortcutCb, LV_EVENT_CLICKED, data);
            
            auto freeShortcutCb = [](lv_event_t* e) {
                ShortcutData* d = (ShortcutData*)lv_event_get_user_data(e);
                delete d;
            };
            lv_obj_add_event_cb(btn, freeShortcutCb, LV_EVENT_DELETE, data);
        };

        if (mFileBrowserIsFmImport || mFileBrowserIsPresetLoad) {
            createShortcutBtn("PRESETS", homeStr + "/presets");
            createShortcutBtn("FM PRESETS", homeStr + "/presets/fm");
        } else {
            createShortcutBtn("SAMPLES", homeStr + "/samples");
            createShortcutBtn("WAVETABLES", homeStr + "/wavetables");
            createShortcutBtn("GRANULAR", homeStr + "/granular");
            createShortcutBtn("SOUNDFONTS", homeStr + "/soundfonts");
        }
    }

    // Scrollable file list
    lv_obj_t* fileList = lv_list_create(card);
    lv_obj_set_size(fileList, 528, isSave ? 75 : (showShortcuts ? 220 : 280));
    lv_obj_set_style_bg_color(fileList, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(fileList, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(fileList, 1, 0);
    lv_obj_set_style_radius(fileList, 6, 0);

    // Populate with files from working directory
    std::string dirPath = mFileBrowserCurrentPath;
    bool anyFile = false;

    // Add "📁 .. (Parent Folder)" if not at the root Loom directory
    if (mFileBrowserCurrentPath != homeStr) {
        lv_obj_t* item = lv_list_add_button(fileList, nullptr, "📁 .. (Parent Folder)");
        lv_obj_set_style_text_font(lv_obj_get_child(item, -1), &lv_font_montserrat_12, 0);
        
        auto upFolderCb = [](lv_event_t* e) {
            UIManager* ui = (UIManager*)lv_event_get_user_data(e);
            if (!ui) return;
            size_t lastSlash = ui->mFileBrowserCurrentPath.find_last_of("/\\");
            if (lastSlash != std::string::npos && lastSlash > 0) {
                ui->mFileBrowserCurrentPath = ui->mFileBrowserCurrentPath.substr(0, lastSlash);
                ui->openFileBrowser(ui->mFileBrowserIsSave);
            }
        };
        lv_obj_add_event_cb(item, upFolderCb, LV_EVENT_CLICKED, this);
        anyFile = true;
    }

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) dir = opendir("."); // fallback to cwd
    if (dir) {
        struct dirent* entry;
        std::vector<std::string> subdirs;
        std::vector<std::string> matchingFiles;

        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name.rfind(".", 0) == 0) continue;
            
            std::string fullItemPath = dirPath + "/" + name;
            struct stat st;
            bool isDir = (entry->d_type == DT_DIR);
            if (entry->d_type == DT_UNKNOWN) {
                if (stat(fullItemPath.c_str(), &st) == 0) {
                    isDir = S_ISDIR(st.st_mode);
                }
            }

            if (isDir) {
                subdirs.push_back(name);
                continue;
            }

            std::string lowerPath = dirPath;
            for (char &c : lowerPath) c = std::tolower((unsigned char)c);
            bool isSoundFontsFolder = (lowerPath.find("soundfonts") != std::string::npos);

            if (mFileBrowserIsFmImport) {
                bool matched = false;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".fmp") matched = true;
                else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".syx") matched = true;
                else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".sys") matched = true;
                else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".bin") matched = true;
                else if (lowerName.length() >= 6 && lowerName.substr(lowerName.length() - 6) == ".sysex") matched = true;
                if (matched) matchingFiles.push_back(name);
            } else if (isSoundFontsFolder || mFileBrowserIsSfSelect || mFileBrowserIsSfImport) {
                bool matched = false;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".sf2") matched = true;
                else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".sf3") matched = true;
                if (matched) matchingFiles.push_back(name);
            } else if (mFileBrowserIsWtSelect || mFileBrowserIsWtImport || mFileBrowserIsSampleLoad || mFileBrowserIsSampleSave) {
                bool matched = false;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".wav") matched = true;
                else if (lowerName.length() >= 3 && lowerName.substr(lowerName.length() - 3) == ".wt") matched = true;
                else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".aif") matched = true;
                else if (lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".aiff") matched = true;
                if (matched) matchingFiles.push_back(name);
            } else if (mFileBrowserIsPresetLoad || mFileBrowserIsPresetSave) {
                bool matched = false;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".gbs") matched = true;
                if (matched) matchingFiles.push_back(name);
            } else if (mFileBrowserIsProject) {
                bool matched = false;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".loom") matched = true;
                if (matched) matchingFiles.push_back(name);
            } else {
                if (name.rfind(".seq", name.size() - 4) != std::string::npos ||
                    name.rfind(".json", name.size() - 5) != std::string::npos) {
                    matchingFiles.push_back(name);
                }
            }
        }
        closedir(dir);

        // Sort subdirectories alphabetically and add them first
        std::sort(subdirs.begin(), subdirs.end());
        for (const auto& s : subdirs) {
            std::string dispName = "📁 " + s;
            lv_obj_t* item = lv_list_add_button(fileList, nullptr, dispName.c_str());
            lv_obj_set_style_text_font(lv_obj_get_child(item, -1), &lv_font_montserrat_12, 0);
            
            struct DirClickData {
                UIManager* ui;
                std::string folderName;
            };
            DirClickData* clickData = new DirClickData{this, s};
            
            auto dirClickCb = [](lv_event_t* e) {
                DirClickData* d = (DirClickData*)lv_event_get_user_data(e);
                if (d && d->ui) {
                    d->ui->mFileBrowserCurrentPath = d->ui->mFileBrowserCurrentPath + "/" + d->folderName;
                    d->ui->openFileBrowser(d->ui->mFileBrowserIsSave);
                }
            };
            lv_obj_add_event_cb(item, dirClickCb, LV_EVENT_CLICKED, clickData);
            
            auto dirFreeCb = [](lv_event_t* e) {
                DirClickData* d = (DirClickData*)lv_event_get_user_data(e);
                delete d;
            };
            lv_obj_add_event_cb(item, dirFreeCb, LV_EVENT_DELETE, clickData);
            anyFile = true;
        }

        // Sort matching files alphabetically and add them next
        std::sort(matchingFiles.begin(), matchingFiles.end());
        for (const auto& f : matchingFiles) {
            lv_obj_t* item = lv_list_add_button(fileList, nullptr, f.c_str());
            lv_obj_set_style_text_font(lv_obj_get_child(item, -1), &lv_font_montserrat_12, 0);
            lv_obj_add_event_cb(item, fileBrowserItemEventCb, LV_EVENT_CLICKED, this);
            anyFile = true;
        }
    }
    if (!anyFile) {
        std::string emptyMsg = "(no sequences found)";
        if (mFileBrowserIsFmImport) emptyMsg = "(no presets found)";
        else if (mFileBrowserIsPresetLoad || mFileBrowserIsPresetSave) emptyMsg = "(no track presets found)";
        else if (mFileBrowserIsProject) emptyMsg = "(no projects found)";
        else {
            std::string lowerPath = dirPath;
            for (char &c : lowerPath) c = std::tolower((unsigned char)c);
            if (lowerPath.find("soundfonts") != std::string::npos) emptyMsg = "(no soundfonts found)";
            else if (lowerPath.find("wavetables") != std::string::npos) emptyMsg = "(no wavetables found)";
            else if (lowerPath.find("granular") != std::string::npos) emptyMsg = "(no granular source found)";
            else emptyMsg = "(no samples found)";
        }
        lv_obj_t* emptyLbl = lv_list_add_button(fileList, nullptr, emptyMsg.c_str());
        lv_obj_set_style_text_color(lv_obj_get_child(emptyLbl, -1), lv_color_hex(0x555555), 0);
    }

    // If saving: show a text area for filename
    if (isSave) {
        lv_obj_t* ta = lv_textarea_create(card);
        lv_obj_set_size(ta, 528, 36);
        if (mFileBrowserIsSampleSave) {
            lv_textarea_set_placeholder_text(ta, "sample.wav");
        } else if (mFileBrowserIsPresetSave) {
            lv_textarea_set_placeholder_text(ta, "patch.gbs");
        } else {
            lv_textarea_set_placeholder_text(ta, "filename.seq");
        }
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(ta, lv_color_hex(0x252525), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(ta, 1, 0);

        // Add a virtual keyboard to the overlay for touchscreen/mouse entry accessibility!
        lv_obj_t* kb = lv_keyboard_create(overlay);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_set_size(kb, 1000, 200);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    // Button row container
    lv_obj_t* dialogBtnRow = lv_obj_create(card);
    lv_obj_set_size(dialogBtnRow, 528, 45);
    lv_obj_set_style_bg_opa(dialogBtnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dialogBtnRow, 0, 0);
    lv_obj_set_style_pad_all(dialogBtnRow, 0, 0);
    lv_obj_set_layout(dialogBtnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dialogBtnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dialogBtnRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dialogBtnRow, 10, 0);

    // Cancel button
    lv_obj_t* cancelBtn = lv_button_create(dialogBtnRow);
    lv_obj_set_size(cancelBtn, 100, 34);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(cancelBtn, 6, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);
    lv_obj_add_event_cb(cancelBtn, fileBrowserCancelEventCb, LV_EVENT_CLICKED, this);

    // Save button (only if saving)
    if (isSave) {
        if (mFileBrowserIsPresetSave) {
            // Save as Default button
            lv_obj_t* defBtn = lv_button_create(dialogBtnRow);
            lv_obj_set_size(defBtn, 150, 34);
            lv_obj_set_style_bg_color(defBtn, lv_color_hex(0x444444), 0);
            lv_obj_set_style_border_color(defBtn, trackColor, 0);
            lv_obj_set_style_border_width(defBtn, 1, 0);
            lv_obj_set_style_radius(defBtn, 6, 0);
            
            lv_obj_t* defLbl = lv_label_create(defBtn);
            lv_label_set_text(defLbl, "Save as Default");
            lv_obj_set_style_text_font(defLbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(defLbl, lv_color_hex(0xCCCCCC), 0);
            lv_obj_center(defLbl);
            
            auto saveAsDefaultBtnCb = [](lv_event_t* e) {
                UIManager* ui = (UIManager*)lv_event_get_user_data(e);
                ui->mEngine.saveTrackPreset(ui->mActiveTrack);
                std::cout << "Preset saved as default for engine type: " 
                          << ui->mEngine.getTracks()[ui->mActiveTrack].engineType << std::endl;
                ui->closeFileBrowser();
                if (ui->mActiveNav == 0) {
                    ui->createCenterContentArea();
                }
            };
            lv_obj_add_event_cb(defBtn, saveAsDefaultBtnCb, LV_EVENT_CLICKED, this);
        }

        lv_obj_t* saveBtn = lv_button_create(dialogBtnRow);
        lv_obj_set_size(saveBtn, 100, 34);
        lv_obj_set_style_bg_color(saveBtn, trackColor, 0);
        lv_obj_set_style_radius(saveBtn, 6, 0);
        lv_obj_t* saveLbl = lv_label_create(saveBtn);
        lv_label_set_text(saveLbl, "Save");
        lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0);
        lv_obj_center(saveLbl);
        lv_obj_add_event_cb(saveBtn, fileBrowserSaveBtnEventCb, LV_EVENT_CLICKED, this);
    }
}

void UIManager::closeFileBrowser() {
    if (mSeqModal) {
        lv_obj_delete(mSeqModal);
        mSeqModal = nullptr;
    }
    mFileBrowserCurrentPath = "";
    mFileBrowserIsProject = false;
}

// =========================================================================
// --- Sequencer Callbacks ---
// =========================================================================

void UIManager::seqGridBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int stepIdx = (int)(uintptr_t)lv_obj_get_user_data(btn);
    if (stepIdx >= 0 && stepIdx < 64) {
        bool active = lv_obj_has_state(btn, LV_STATE_CHECKED);
        ui->mSeqTrackSteps[ui->mActiveTrack][stepIdx] = active;
        
        int engineType = ui->mEngine.getTracks()[ui->mActiveTrack].engineType;
        bool isSamplerChops = (engineType == 2 && ui->mEngine.getTracks()[ui->mActiveTrack].samplerEngine.getPlayMode() >= 3);
        bool isDrum = (engineType == 5 || engineType == 6 || isSamplerChops);
        
        std::vector<Step> currentSteps;
        if (isDrum) {
            currentSteps = ui->mEngine.getDrumSequencerSteps(ui->mActiveTrack, ui->mActiveDrumIdx);
        } else {
            currentSteps = ui->mEngine.getSequencerSteps(ui->mActiveTrack);
        }

        if (stepIdx < (int)currentSteps.size()) {
            Step s = currentSteps[stepIdx];
            s.active = active;
            std::vector<int> rawNotes;
            if (s.notes.empty()) {
                rawNotes = {isDrum ? (60 + ui->mActiveDrumIdx) : 60};
            } else {
                for (const auto& n : s.notes) {
                    rawNotes.push_back(n.note);
                }
            }
            float velocity = s.notes.empty() ? 0.8f : s.notes[0].velocity;
            
            ui->mEngine.setStep(ui->mActiveTrack, stepIdx, active, rawNotes, velocity,
                                s.ratchet, s.punch, s.probability, s.gate, s.isSkipped);
        } else {
            ui->mEngine.setStep(ui->mActiveTrack, stepIdx, active, {isDrum ? (60 + ui->mActiveDrumIdx) : 60});
        }
    }
}

void UIManager::seqPlayOrderBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);

    int trackDir = ui->mEngine.getPlaybackDirection(ui->mActiveTrack);
    bool isRnd = ui->mEngine.getIsRandomOrder(ui->mActiveTrack);

    int nextDir = 0;
    bool nextRnd = false;
    const char* dirText = "Fwd";

    if (isRnd) {
        nextDir = 0;
        nextRnd = false;
        dirText = "Fwd";
    } else if (trackDir == 0) {
        nextDir = 1;
        nextRnd = false;
        dirText = "Rev";
    } else if (trackDir == 1) {
        nextDir = 2;
        nextRnd = false;
        dirText = "P-P";
    } else if (trackDir == 2) {
        nextDir = 0;
        nextRnd = true;
        dirText = "Rnd";
    }

    ui->mEngine.setPlaybackDirection(ui->mActiveTrack, nextDir);
    ui->mEngine.setIsRandomOrder(ui->mActiveTrack, nextRnd);
    lv_label_set_text(lbl, dirText);
}

void UIManager::seqStepLongPressEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int stepIdx = (int)(uintptr_t)lv_obj_get_user_data(btn);
    if (stepIdx >= 0 && stepIdx < 64) {
        ui->openSeqStepModal(stepIdx);
    }
}

void UIManager::openSeqStepModal(int stepIdx) {
    if (mStepModal) {
        lv_obj_delete(mStepModal);
        mStepModal = nullptr;
    }
    mEditingStepIdx = stepIdx;
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Full-screen dimmed overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    mStepModal = overlay;

    // Modal card
    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 540, 540);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, trackColor, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 12, 0);

    // Header Row
    lv_obj_t* headerRow = lv_obj_create(card);
    lv_obj_set_size(headerRow, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(headerRow, 0, 0);
    lv_obj_set_style_pad_all(headerRow, 0, 0);
    lv_obj_set_layout(headerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* titleLbl = lv_label_create(headerRow);
    lv_label_set_text_fmt(titleLbl, "Track %d - Step %d Settings", mActiveTrack + 1, stepIdx + 1);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xEEEEEE), 0);

    lv_obj_t* closeBtn = lv_button_create(headerRow);
    lv_obj_set_size(closeBtn, 36, 36);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(closeBtn, 18, 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, "X");
    lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, stepModalCloseEventCb, LV_EVENT_CLICKED, this);

    // Body Columns Container
    lv_obj_t* bodyContainer = lv_obj_create(card);
    lv_obj_set_size(bodyContainer, lv_pct(100), 450);
    lv_obj_set_style_bg_opa(bodyContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bodyContainer, 0, 0);
    lv_obj_set_style_pad_all(bodyContainer, 0, 0);
    lv_obj_set_layout(bodyContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bodyContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bodyContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(bodyContainer, 16, 0);

    // Fetch existing step details
    int engineType = mEngine.getTracks()[mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.getPlayMode() >= 3);
    bool isDrum = (engineType == 5 || engineType == 6 || isSamplerChops);

    std::vector<Step> currentSteps;
    if (isDrum) {
        currentSteps = mEngine.getDrumSequencerSteps(mActiveTrack, mActiveDrumIdx);
    } else {
        currentSteps = mEngine.getSequencerSteps(mActiveTrack);
    }

    Step stepObj;
    if (stepIdx < (int)currentSteps.size()) {
        stepObj = currentSteps[stepIdx];
    } else {
        stepObj.active = mSeqTrackSteps[mActiveTrack][stepIdx];
    }

    // Left Column
    lv_obj_t* leftCol = lv_obj_create(bodyContainer);
    lv_obj_set_size(leftCol, 240, 440);
    lv_obj_set_style_bg_color(leftCol, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(leftCol, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(leftCol, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(leftCol, 1, 0);
    lv_obj_set_style_radius(leftCol, 12, 0);
    lv_obj_set_style_pad_all(leftCol, 10, 0);
    lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(leftCol, 8, 0);
    lv_obj_remove_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Ratchet Dropdown
    lv_obj_t* ratchetRow = lv_obj_create(leftCol);
    lv_obj_set_size(ratchetRow, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(ratchetRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ratchetRow, 0, 0);
    lv_obj_set_style_pad_all(ratchetRow, 0, 0);
    lv_obj_set_layout(ratchetRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ratchetRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ratchetRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ratchetLbl = lv_label_create(ratchetRow);
    lv_label_set_text(ratchetLbl, "Ratchet:");
    lv_obj_set_style_text_font(ratchetLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ratchetLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalRatchetDd = lv_dropdown_create(ratchetRow);
    lv_obj_set_size(mStepModalRatchetDd, 100, 32);
    lv_dropdown_set_options(mStepModalRatchetDd, "1x\n2x\n3x\n4x\n8x");
    int ratchetSel = 0;
    if (stepObj.ratchet == 2) ratchetSel = 1;
    else if (stepObj.ratchet == 3) ratchetSel = 2;
    else if (stepObj.ratchet == 4) ratchetSel = 3;
    else if (stepObj.ratchet == 8) ratchetSel = 4;
    lv_dropdown_set_selected(mStepModalRatchetDd, ratchetSel);
    lv_obj_set_style_text_font(mStepModalRatchetDd, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(mStepModalRatchetDd, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalRatchetDd, (void*)(uintptr_t)10);

    // 2. Note Slider with notation helper
    auto getNoteName = [](int n) -> std::string {
        static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int octave = (n / 12) - 1;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%d (%d)", noteNames[n % 12], octave, n);
        return std::string(buf);
    };

    int currentNoteVal = isDrum ? (60 + mActiveDrumIdx) : 60;
    if (!stepObj.notes.empty()) {
        currentNoteVal = stepObj.notes[0].note;
    }

    lv_obj_t* noteRow = lv_obj_create(leftCol);
    lv_obj_set_size(noteRow, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(noteRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(noteRow, 0, 0);
    lv_obj_set_style_pad_all(noteRow, 0, 0);
    lv_obj_set_layout(noteRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(noteRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(noteRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(noteRow, 4, 0);

    lv_obj_t* noteLbl = lv_label_create(noteRow);
    lv_label_set_text_fmt(noteLbl, "Note: %s", getNoteName(currentNoteVal).c_str());
    lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(noteLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalNoteSlider = lv_slider_create(noteRow);
    lv_obj_set_size(mStepModalNoteSlider, lv_pct(100), 12);
    lv_slider_set_range(mStepModalNoteSlider, 24, 108);
    lv_slider_set_value(mStepModalNoteSlider, currentNoteVal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalNoteSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalNoteSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalNoteSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalNoteSlider, (void*)(uintptr_t)11);

    // 3. Punch Toggle Switch
    lv_obj_t* punchRow = lv_obj_create(leftCol);
    lv_obj_set_size(punchRow, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(punchRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(punchRow, 0, 0);
    lv_obj_set_style_pad_all(punchRow, 0, 0);
    lv_obj_set_layout(punchRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(punchRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(punchRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* punchLbl = lv_label_create(punchRow);
    lv_label_set_text(punchLbl, "Punch (1.1x / OD):");
    lv_obj_set_style_text_font(punchLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(punchLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalPunchSw = lv_switch_create(punchRow);
    lv_obj_set_size(mStepModalPunchSw, 46, 24);
    if (stepObj.punch) {
        lv_obj_add_state(mStepModalPunchSw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(mStepModalPunchSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(mStepModalPunchSw, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalPunchSw, (void*)(uintptr_t)12);

    // 4. Probability Slider
    lv_obj_t* probRow = lv_obj_create(leftCol);
    lv_obj_set_size(probRow, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(probRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(probRow, 0, 0);
    lv_obj_set_style_pad_all(probRow, 0, 0);
    lv_obj_set_layout(probRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(probRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(probRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(probRow, 4, 0);

    lv_obj_t* probLbl = lv_label_create(probRow);
    int probVal = (int)(stepObj.probability * 100.0f);
    lv_label_set_text_fmt(probLbl, "Probability: %d%%", probVal);
    lv_obj_set_style_text_font(probLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(probLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalProbSlider = lv_slider_create(probRow);
    lv_obj_set_size(mStepModalProbSlider, lv_pct(100), 12);
    lv_slider_set_range(mStepModalProbSlider, 0, 100);
    lv_slider_set_value(mStepModalProbSlider, probVal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalProbSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalProbSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalProbSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalProbSlider, (void*)(uintptr_t)13);

    // 5. Gate Slider
    lv_obj_t* gateRow = lv_obj_create(leftCol);
    lv_obj_set_size(gateRow, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(gateRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gateRow, 0, 0);
    lv_obj_set_style_pad_all(gateRow, 0, 0);
    lv_obj_set_layout(gateRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gateRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gateRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(gateRow, 4, 0);

    lv_obj_t* gateLbl = lv_label_create(gateRow);
    int gateVal = (int)(stepObj.gate * 100.0f);
    lv_label_set_text_fmt(gateLbl, "Gate Length: %d%%", gateVal);
    lv_obj_set_style_text_font(gateLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gateLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalGateSlider = lv_slider_create(gateRow);
    lv_obj_set_size(mStepModalGateSlider, lv_pct(100), 12);
    lv_slider_set_range(mStepModalGateSlider, 0, 100);
    lv_slider_set_value(mStepModalGateSlider, gateVal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalGateSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalGateSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalGateSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalGateSlider, (void*)(uintptr_t)14);

    // 6. Skip Step Switch
    lv_obj_t* skipRow = lv_obj_create(leftCol);
    lv_obj_set_size(skipRow, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(skipRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(skipRow, 0, 0);
    lv_obj_set_style_pad_all(skipRow, 0, 0);
    lv_obj_set_layout(skipRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(skipRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(skipRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* skipLbl = lv_label_create(skipRow);
    lv_label_set_text(skipLbl, "Skip Step:");
    lv_obj_set_style_text_font(skipLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(skipLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalSkipSw = lv_switch_create(skipRow);
    lv_obj_set_size(mStepModalSkipSw, 46, 24);
    if (stepObj.isSkipped) {
        lv_obj_add_state(mStepModalSkipSw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(mStepModalSkipSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(mStepModalSkipSw, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalSkipSw, (void*)(uintptr_t)15);

    // Right Column (Parameter Locking Dashboard)
    lv_obj_t* rightCol = lv_obj_create(bodyContainer);
    lv_obj_set_size(rightCol, 240, 440);
    lv_obj_set_style_bg_color(rightCol, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(rightCol, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(rightCol, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(rightCol, 1, 0);
    lv_obj_set_style_radius(rightCol, 12, 0);
    lv_obj_set_style_pad_all(rightCol, 10, 0);
    lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rightCol, 8, 0);
    lv_obj_remove_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);

    // Parameter Dropdown Options
    auto params = getTrackParamOptions(mActiveTrack);
    std::string paramOptionsStr = "";
    for (size_t i = 0; i < params.size(); ++i) {
        paramOptionsStr += params[i].second;
        if (i < params.size() - 1) {
            paramOptionsStr += "\n";
        }
    }

    mStepModalPLockDd = lv_dropdown_create(rightCol);
    lv_obj_set_size(mStepModalPLockDd, lv_pct(100), 32);
    lv_dropdown_set_options(mStepModalPLockDd, paramOptionsStr.c_str());
    lv_obj_set_style_text_font(mStepModalPLockDd, &lv_font_montserrat_12, 0);

    // Lock Value slider
    lv_obj_t* pLockValRow = lv_obj_create(rightCol);
    lv_obj_set_size(pLockValRow, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(pLockValRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pLockValRow, 0, 0);
    lv_obj_set_style_pad_all(pLockValRow, 0, 0);
    lv_obj_set_layout(pLockValRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pLockValRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pLockValRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(pLockValRow, 4, 0);

    lv_obj_t* pLockValLbl = lv_label_create(pLockValRow);
    lv_label_set_text(pLockValLbl, "Lock Value: 50%");
    lv_obj_set_style_text_font(pLockValLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pLockValLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalPLockSlider = lv_slider_create(pLockValRow);
    lv_obj_set_size(mStepModalPLockSlider, lv_pct(100), 12);
    lv_slider_set_range(mStepModalPLockSlider, 0, 100);
    lv_slider_set_value(mStepModalPLockSlider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalPLockSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalPLockSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalPLockSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalPLockSlider, (void*)(uintptr_t)20);

    // Buttons Row
    lv_obj_t* pLockBtnRow = lv_obj_create(rightCol);
    lv_obj_set_size(pLockBtnRow, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(pLockBtnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pLockBtnRow, 0, 0);
    lv_obj_set_style_pad_all(pLockBtnRow, 0, 0);
    lv_obj_set_layout(pLockBtnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pLockBtnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pLockBtnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* addLockBtn = lv_button_create(pLockBtnRow);
    lv_obj_set_size(addLockBtn, 105, 34);
    lv_obj_set_style_bg_color(addLockBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(addLockBtn, trackColor, 0);
    lv_obj_set_style_border_width(addLockBtn, 1, 0);
    lv_obj_set_style_radius(addLockBtn, 6, 0);
    lv_obj_t* addLockLbl = lv_label_create(addLockBtn);
    lv_label_set_text(addLockLbl, "Add Lock");
    lv_obj_set_style_text_font(addLockLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(addLockLbl);
    lv_obj_add_event_cb(addLockBtn, stepModalAddLockEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* clearLocksBtn = lv_button_create(pLockBtnRow);
    lv_obj_set_size(clearLocksBtn, 105, 34);
    lv_obj_set_style_bg_color(clearLocksBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(clearLocksBtn, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_border_width(clearLocksBtn, 1, 0);
    lv_obj_set_style_radius(clearLocksBtn, 6, 0);
    lv_obj_t* clearLocksLbl = lv_label_create(clearLocksBtn);
    lv_label_set_text(clearLocksLbl, "Clear All");
    lv_obj_set_style_text_font(clearLocksLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(clearLocksLbl);
    lv_obj_add_event_cb(clearLocksBtn, stepModalClearLocksEventCb, LV_EVENT_CLICKED, this);

    // Scrollable locks list view
    mStepModalActiveLocksList = lv_obj_create(rightCol);
    lv_obj_set_size(mStepModalActiveLocksList, lv_pct(100), 160);
    lv_obj_set_style_bg_color(mStepModalActiveLocksList, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(mStepModalActiveLocksList, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mStepModalActiveLocksList, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(mStepModalActiveLocksList, 1, 0);
    lv_obj_set_style_radius(mStepModalActiveLocksList, 8, 0);
    lv_obj_set_style_pad_all(mStepModalActiveLocksList, 6, 0);
    lv_obj_set_layout(mStepModalActiveLocksList, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mStepModalActiveLocksList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mStepModalActiveLocksList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(mStepModalActiveLocksList, 4, 0);
    lv_obj_add_flag(mStepModalActiveLocksList, LV_OBJ_FLAG_SCROLLABLE);

    refreshStepModalLocksList();
}

void UIManager::refreshStepModalLocksList() {
    if (!mStepModalActiveLocksList || mEditingStepIdx < 0) return;

    lv_obj_clean(mStepModalActiveLocksList);

    std::vector<Step> currentSteps = mEngine.getSequencerSteps(mActiveTrack);
    if (mEditingStepIdx >= (int)currentSteps.size()) return;

    const Step& s = currentSteps[mEditingStepIdx];
    auto params = getTrackParamOptions(mActiveTrack);
    
    auto getParamName = [&](int pid) -> std::string {
        for (const auto& p : params) {
            if (p.first == pid) return p.second;
        }
        return "Param " + std::to_string(pid);
    };

    if (s.parameterLocks.empty()) {
        lv_obj_t* noLocksLbl = lv_label_create(mStepModalActiveLocksList);
        lv_label_set_text(noLocksLbl, "No active locks");
        lv_obj_set_style_text_font(noLocksLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(noLocksLbl, lv_color_hex(0x666666), 0);
        lv_obj_set_style_pad_all(noLocksLbl, 6, 0);
        return;
    }

    for (const auto& lockPair : s.parameterLocks) {
        int pid = lockPair.first;
        float val = lockPair.second;

        lv_obj_t* itemRow = lv_obj_create(mStepModalActiveLocksList);
        lv_obj_set_size(itemRow, lv_pct(100), 28);
        lv_obj_set_style_bg_opa(itemRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(itemRow, 0, 0);
        lv_obj_set_style_pad_all(itemRow, 0, 0);
        lv_obj_set_layout(itemRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(itemRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(itemRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* lockLbl = lv_label_create(itemRow);
        lv_label_set_text_fmt(lockLbl, "%s: %d%%", getParamName(pid).c_str(), (int)(val * 100.0f));
        lv_obj_set_style_text_font(lockLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lockLbl, lv_color_hex(0xEEEEEE), 0);

        // Delete button for this lock
        lv_obj_t* delBtn = lv_button_create(itemRow);
        lv_obj_set_size(delBtn, 20, 20);
        lv_obj_set_style_bg_color(delBtn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_radius(delBtn, 4, 0);
        lv_obj_t* delLbl = lv_label_create(delBtn);
        lv_label_set_text(delLbl, "x");
        lv_obj_set_style_text_font(delLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(delLbl);
        
        lv_obj_set_user_data(delBtn, (void*)(uintptr_t)pid);

        lv_obj_add_event_cb(delBtn, [](lv_event_t* ev) {
            UIManager* ui = (UIManager*)lv_event_get_user_data(ev);
            lv_obj_t* b = (lv_obj_t*)lv_event_get_target(ev);
            int pidToDelete = (int)(uintptr_t)lv_obj_get_user_data(b);

            // Fetch and reconstruct locks list safely to clear a single lock
            std::vector<Step> currentSteps = ui->mEngine.getSequencerSteps(ui->mActiveTrack);
            if (ui->mEditingStepIdx >= 0 && ui->mEditingStepIdx < (int)currentSteps.size()) {
                const Step& s = currentSteps[ui->mEditingStepIdx];
                std::vector<std::pair<int, float>> remainingLocks;
                for (const auto& lp : s.parameterLocks) {
                    if (lp.first != pidToDelete) {
                        remainingLocks.push_back(lp);
                    }
                }
                
                // Clear all locks first
                ui->mEngine.clearParameterLocks(ui->mActiveTrack, ui->mEditingStepIdx);
                
                // Re-add remaining locks
                for (const auto& lp : remainingLocks) {
                    ui->mEngine.setParameterLock(ui->mActiveTrack, ui->mEditingStepIdx, lp.first, lp.second);
                }
            }

            ui->refreshStepModalLocksList();
        }, LV_EVENT_CLICKED, this);
    }
}

void UIManager::stepModalControlEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    uintptr_t code = (uintptr_t)lv_obj_get_user_data(target);

    if (ui->mEditingStepIdx < 0) return;

    if (code == 11) { // Note Slider notation update
        lv_obj_t* parent = lv_obj_get_parent(target);
        lv_obj_t* label = lv_obj_get_child(parent, 0);
        int noteVal = lv_slider_get_value(target);
        
        static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int octave = (noteVal / 12) - 1;
        lv_label_set_text_fmt(label, "Note: %s%d (%d)", noteNames[noteVal % 12], octave, noteVal);
    } else if (code == 13) { // Probability Slider
        lv_obj_t* parent = lv_obj_get_parent(target);
        lv_obj_t* label = lv_obj_get_child(parent, 0);
        int val = lv_slider_get_value(target);
        lv_label_set_text_fmt(label, "Probability: %d%%", val);
    } else if (code == 14) { // Gate Slider
        lv_obj_t* parent = lv_obj_get_parent(target);
        lv_obj_t* label = lv_obj_get_child(parent, 0);
        int val = lv_slider_get_value(target);
        lv_label_set_text_fmt(label, "Gate Length: %d%%", val);
    } else if (code == 20) { // Lock Value Slider
        lv_obj_t* parent = lv_obj_get_parent(target);
        lv_obj_t* label = lv_obj_get_child(parent, 0);
        int val = lv_slider_get_value(target);
        lv_label_set_text_fmt(label, "Lock Value: %d%%", val);
    }

    // Read all values and update AudioEngine step config in real-time
    int ratchetVal = 1;
    int selectedRatchetIdx = lv_dropdown_get_selected(ui->mStepModalRatchetDd);
    if (selectedRatchetIdx == 1) ratchetVal = 2;
    else if (selectedRatchetIdx == 2) ratchetVal = 3;
    else if (selectedRatchetIdx == 3) ratchetVal = 4;
    else if (selectedRatchetIdx == 4) ratchetVal = 8;

    int noteVal = lv_slider_get_value(ui->mStepModalNoteSlider);
    bool punchVal = lv_obj_has_state(ui->mStepModalPunchSw, LV_STATE_CHECKED);
    float probVal = lv_slider_get_value(ui->mStepModalProbSlider) / 100.0f;
    float gateVal = lv_slider_get_value(ui->mStepModalGateSlider) / 100.0f;
    bool skipVal = lv_obj_has_state(ui->mStepModalSkipSw, LV_STATE_CHECKED);

    ui->mSeqTrackSteps[ui->mActiveTrack][ui->mEditingStepIdx] = !skipVal;

    ui->mEngine.setStep(ui->mActiveTrack, ui->mEditingStepIdx, true, {noteVal}, 0.8f,
                        ratchetVal, punchVal, probVal, gateVal, skipVal);
}

void UIManager::stepModalAddLockEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mEditingStepIdx < 0) return;

    auto params = ui->getTrackParamOptions(ui->mActiveTrack);
    int selectedParamIdx = lv_dropdown_get_selected(ui->mStepModalPLockDd);
    if (selectedParamIdx < 0 || selectedParamIdx >= (int)params.size()) return;

    int paramId = params[selectedParamIdx].first;
    float lockVal = lv_slider_get_value(ui->mStepModalPLockSlider) / 100.0f;

    ui->mEngine.setParameterLock(ui->mActiveTrack, ui->mEditingStepIdx, paramId, lockVal);
    ui->refreshStepModalLocksList();
}

void UIManager::stepModalClearLocksEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mEditingStepIdx < 0) return;

    ui->mEngine.clearParameterLocks(ui->mActiveTrack, ui->mEditingStepIdx);
    ui->refreshStepModalLocksList();
}

void UIManager::stepModalCloseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mStepModal) {
        lv_obj_delete(ui->mStepModal);
        ui->mStepModal = nullptr;
    }
    ui->mStepModalActiveLocksList = nullptr;
    ui->mStepModalRatchetDd = nullptr;
    ui->mStepModalNoteSlider = nullptr;
    ui->mStepModalPunchSw = nullptr;
    ui->mStepModalProbSlider = nullptr;
    ui->mStepModalGateSlider = nullptr;
    ui->mStepModalSkipSw = nullptr;
    ui->mStepModalPLockDd = nullptr;
    ui->mStepModalPLockSlider = nullptr;
    ui->mEditingStepIdx = -1;

    ui->rebuildSeqGrid();
}

void UIManager::seqGridToggleBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    bool is4x4 = lv_obj_has_state(btn, LV_STATE_CHECKED);
    ui->mSeqTrackIs4x4[ui->mActiveTrack] = is4x4;
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    lv_label_set_text(lbl, is4x4 ? "8x8 View" : "4x4 View");
    ui->rebuildSeqGrid();
}

void UIManager::seqLengthArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    ui->mSeqTrackLength[ui->mActiveTrack] = (int)lv_arc_get_value(arc);
    if (ui->mSeqLengthLbl)
        lv_label_set_text_fmt(ui->mSeqLengthLbl, "Length: %d", ui->mSeqTrackLength[ui->mActiveTrack]);
    
    // Set pattern length thread-safely in the engine
    ui->mEngine.setPatternLength(ui->mActiveTrack, ui->mSeqTrackLength[ui->mActiveTrack]);
    
    ui->rebuildSeqGrid();
}

void UIManager::seqDrumTabClickEventCb(lv_event_t* e) {
    struct DrumTabClickData {
        UIManager* ui;
        int drumIdx;
    };
    DrumTabClickData* data = (DrumTabClickData*)lv_event_get_user_data(e);
    if (!data || !data->ui) return;

    data->ui->mActiveDrumIdx = data->drumIdx;
    
    // Reconstruct the sequencer page view dynamically
    data->ui->populateSeqScreen();
}

void UIManager::seqHumanizeArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    ui->mSeqTrackHumanize[ui->mActiveTrack] = val;
    if (ui->mSeqHumanValLbl)
        lv_label_set_text_fmt(ui->mSeqHumanValLbl, "%" PRId32 "%%", val);
}

void UIManager::seqClockDivArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    ui->mSeqTrackClockDivIndex[ui->mActiveTrack] = (int)lv_arc_get_value(arc);
    if (ui->mSeqClockLbl)
        lv_label_set_text(ui->mSeqClockLbl, kClkLabels[ui->mSeqTrackClockDivIndex[ui->mActiveTrack]]);
}

void UIManager::seqProbArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    ui->mSeqTrackProbability[ui->mActiveTrack] = val;
    if (ui->mSeqProbLbl)
        lv_label_set_text_fmt(ui->mSeqProbLbl, "%" PRId32 "%%", val);

    {
        std::lock_guard<std::recursive_mutex> lock(ui->mEngine.getLock());
        ui->mEngine.getTracks()[ui->mActiveTrack].sequencerProbability = (float)val / 100.0f;
    }
}

void UIManager::seqTransposeBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int dir = (uintptr_t)lv_obj_get_user_data(btn) == 1 ? 1 : -1;
    int& val = ui->mSeqTrackTranspose[ui->mActiveTrack];
    val = std::max(-24, std::min(24, val + dir));
    if (ui->mSeqTransposeLbl) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%+d", val);
        lv_label_set_text(ui->mSeqTransposeLbl, buf);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(ui->mEngine.getLock());
        ui->mEngine.getTracks()[ui->mActiveTrack].transpose = val;
    }
}

void UIManager::seqOctaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int dir = (uintptr_t)lv_obj_get_user_data(btn) == 1 ? 1 : -1;
    int& val = ui->mSeqTrackOctave[ui->mActiveTrack];
    val = std::max(-3, std::min(3, val + dir));
    if (ui->mSeqOctaveLbl) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%+d", val);
        lv_label_set_text(ui->mSeqOctaveLbl, buf);
    }
}

void UIManager::seqCopyBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    int total = ui->mSeqTrackIs4x4[ui->mActiveTrack] ? 16 : 64;
    ui->mSeqClipboard.resize(total);
    for (int i = 0; i < total; ++i) {
        ui->mSeqClipboard[i] = ui->mSeqTrackSteps[ui->mActiveTrack][i];
    }
}

void UIManager::seqPasteBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    int total = ui->mSeqTrackIs4x4[ui->mActiveTrack] ? 16 : 64;
    for (int i = 0; i < (int)ui->mSeqClipboard.size() && i < total; ++i) {
        ui->mSeqTrackSteps[ui->mActiveTrack][i] = ui->mSeqClipboard[i];
    }
    ui->rebuildSeqGrid();
}

void UIManager::seqClearBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.clearSequencer(ui->mActiveTrack);
    for (int i = 0; i < 64; ++i) {
        ui->mSeqTrackSteps[ui->mActiveTrack][i] = false;
    }
    ui->rebuildSeqGrid();
}

void UIManager::seqSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->openFileBrowser(true);
}

void UIManager::seqLoadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->openFileBrowser(false);
}

void UIManager::seqChainBoxEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* box = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(uintptr_t)lv_obj_get_user_data(box);
    if (idx < 0 || idx >= 25) return;

    // Cycle stub: Empty → Seq A → Seq B → Seq C → Empty
    static const char* kStubNames[] = { "", "Seq A", "Seq B", "Seq C" };
    ui->mSeqChainCycleIndex[idx] = (ui->mSeqChainCycleIndex[idx] + 1) % 4;
    ui->mSeqChainSlots[idx] = kStubNames[ui->mSeqChainCycleIndex[idx]];

    // Update box appearance
    const std::string& slot = ui->mSeqChainSlots[idx];
    bool assigned = !slot.empty();
    lv_obj_set_style_bg_color(box, assigned ? lv_color_hex(0x242424) : lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(box, assigned ? ui->getTrackColor(ui->mActiveTrack) : lv_color_hex(0x2D2D2D), 0);

    // Update the filename label (child index 1)
    lv_obj_t* fileLbl = lv_obj_get_child(box, 1);
    if (fileLbl) {
        if (slot.empty()) {
            lv_label_set_text(fileLbl, "\xe2\x80\x94"); // em dash
            lv_obj_set_style_text_color(fileLbl, lv_color_hex(0x444444), 0);
        } else {
            std::string disp = slot.length() > 12 ? slot.substr(0, 11) + "\xE2\x80\xA6" : slot;
            lv_label_set_text(fileLbl, disp.c_str());
            lv_obj_set_style_text_color(fileLbl, lv_color_hex(0xEEEEEE), 0);
        }
    }
}

void UIManager::fileBrowserItemEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, -1);
    if (lbl) {
        const char* filename = lv_label_get_text(lbl);
        std::cout << (ui->mFileBrowserIsFmImport ? "Import FM Preset: " : (ui->mFileBrowserIsSave ? "Save to: " : "Load: ")) << filename << std::endl;
        
        std::string fullPath;
        if (!ui->mFileBrowserCurrentPath.empty()) {
            fullPath = ui->mFileBrowserCurrentPath + "/" + filename;
        } else {
            const char* browseDir = getenv("HOME");
            std::string dirPath = browseDir ? std::string(browseDir) + "/Loom/sequences/" : "./Loom/sequences/";
            fullPath = dirPath + filename;
        }

        std::string lowerFilename = filename;
        for (char &c : lowerFilename) c = std::tolower((unsigned char)c);
        bool isSoundFont = (lowerFilename.length() >= 4 && lowerFilename.substr(lowerFilename.length() - 4) == ".sf2") ||
                           (lowerFilename.length() >= 4 && lowerFilename.substr(lowerFilename.length() - 4) == ".sf3");

        if (ui->mFileBrowserIsSfSelect) {
            ui->mEngine.loadSoundFont(ui->mActiveTrack, fullPath);
            std::cout << "Loaded SoundFont: " << fullPath << std::endl;
            if (ui->mSoundFontActiveBankLbl) {
                lv_label_set_text_fmt(ui->mSoundFontActiveBankLbl, "ACTIVE BANK: %s", filename);
            }
            if (ui->mSoundFontActivePresetLbl) {
                std::string pName = ui->mEngine.getSoundFontPresetName(ui->mActiveTrack, 0);
                if (pName.empty()) pName = "Preset 0";
                lv_label_set_text_fmt(ui->mSoundFontActivePresetLbl, "PRESET: 0 - %s", pName.c_str());
            }
            ui->mFileBrowserIsSfSelect = false;
        } else if (ui->mFileBrowserIsSfImport) {
            const char* browseDir = getenv("HOME");
            std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
            std::string destPath = homeStr + "/soundfonts/" + filename;
            std::ifstream src(fullPath, std::ios::binary);
            std::ofstream dest(destPath, std::ios::binary);
            if (src && dest) {
                dest << src.rdbuf();
                ui->mEngine.loadSoundFont(ui->mActiveTrack, destPath);
                ui->mEngine.setSoundFontPreset(ui->mActiveTrack, 0);
                std::cout << "Imported and loaded SoundFont: " << destPath << std::endl;
                if (ui->mSoundFontActiveBankLbl) {
                    lv_label_set_text_fmt(ui->mSoundFontActiveBankLbl, "ACTIVE BANK: %s", filename);
                }
                if (ui->mSoundFontActivePresetLbl) {
                    std::string pName = ui->mEngine.getSoundFontPresetName(ui->mActiveTrack, 0);
                    if (pName.empty()) pName = "Preset 0";
                    lv_label_set_text_fmt(ui->mSoundFontActivePresetLbl, "PRESET: 0 - %s", pName.c_str());
                }
            } else {
                std::cerr << "SoundFont import copy failed!" << std::endl;
            }
            ui->mFileBrowserIsSfImport = false;
        } else if (isSoundFont) {
            ui->mEngine.loadSoundFont(ui->mActiveTrack, fullPath);
            std::cout << "Loaded SoundFont: " << fullPath << std::endl;
            ui->mEngine.getTracks()[ui->mActiveTrack].lastSamplePath = filename;
            
            ui->mFileBrowserIsSampleLoad = false;
            ui->mFileBrowserIsWtSelect = false;
            ui->mFileBrowserIsWtImport = false;
        } else if (ui->mFileBrowserIsFmImport) {
            auto& fmEngine = ui->mEngine.getTracks()[ui->mActiveTrack].fmEngine;
            bool ok = fmEngine.importPreset(fullPath);
            if (ok) {
                int newPresetId = 100 + (int)fmEngine.mCustomPresets.size() - 1;
                ui->mEngine.loadFmPreset(ui->mActiveTrack, newPresetId);
                const char* browseDir = getenv("HOME");
                std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
                std::string presetsFmDir = homeStr + "/presets/fm";
                fmEngine.saveAllCustomPresets(presetsFmDir);
                std::cout << "Import success: loaded custom preset ID " << newPresetId << std::endl;
            } else {
                std::cerr << "Import failed for preset: " << fullPath << std::endl;
            }
            ui->mFileBrowserIsFmImport = false;
        } else if (ui->mFileBrowserIsWtImport) {
            const char* browseDir = getenv("HOME");
            std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
            std::string destPath = homeStr + "/wavetables/" + filename;
            std::ifstream src(fullPath, std::ios::binary);
            std::ofstream dest(destPath, std::ios::binary);
            if (src && dest) {
                dest << src.rdbuf();
                ui->mEngine.loadWavetable(ui->mActiveTrack, destPath);
                ui->mEngine.getTracks()[ui->mActiveTrack].lastSamplePath = filename;
                std::cout << "Imported and loaded wavetable: " << destPath << std::endl;
                if (ui->mWtActiveNameLbl) {
                    lv_label_set_text_fmt(ui->mWtActiveNameLbl, "ACTIVE: %s", filename);
                }
            } else {
                std::cerr << "Wavetable import copy failed!" << std::endl;
            }
            ui->mFileBrowserIsWtImport = false;
        } else if (ui->mFileBrowserIsWtSelect) {
            ui->mEngine.loadWavetable(ui->mActiveTrack, fullPath);
            std::cout << "Loaded wavetable: " << fullPath << std::endl;
            
            ui->mEngine.getTracks()[ui->mActiveTrack].lastSamplePath = filename;
            if (ui->mWtActiveNameLbl) {
                lv_label_set_text_fmt(ui->mWtActiveNameLbl, "ACTIVE: %s", filename);
            }
            
            ui->mFileBrowserIsWtSelect = false;
        } else if (ui->mFileBrowserIsSampleLoad) {
            ui->mEngine.loadSample(ui->mActiveTrack, fullPath);
            std::cout << "Loaded sample: " << fullPath << std::endl;
            
            ui->mEngine.getTracks()[ui->mActiveTrack].lastSamplePath = filename;
            ui->mFileBrowserIsSampleLoad = false;
        } else if (ui->mFileBrowserIsSampleSave) {
            ui->mEngine.saveSample(ui->mActiveTrack, fullPath);
            std::cout << "Saved sample: " << fullPath << std::endl;
            
            ui->mFileBrowserIsSampleSave = false;
        } else if (ui->mFileBrowserIsPresetLoad) {
            ui->mEngine.loadTrackPresetFromPath(ui->mActiveTrack, fullPath);
            std::cout << "Loaded track preset from path: " << fullPath << std::endl;
            ui->mFileBrowserIsPresetLoad = false;
        } else if (ui->mFileBrowserIsPresetSave) {
            ui->mEngine.saveTrackPresetToPath(ui->mActiveTrack, fullPath);
            std::cout << "Saved track preset to path: " << fullPath << std::endl;
            ui->mFileBrowserIsPresetSave = false;
        } else {
            if (ui->mFileBrowserIsSave) {
                ui->mEngine.saveProject(fullPath);
                ui->mSettingsFilePath = fullPath + ".settings";
                ui->saveSettings(ui->mSettingsFilePath);
                std::cout << "Project saved: " << fullPath << std::endl;
            } else {
                ui->mEngine.loadProject(fullPath);
                ui->mSettingsFilePath = fullPath + ".settings";
                ui->loadSettings(ui->mSettingsFilePath);
                ui->mNeedsScreenRebuild = true;
                std::cout << "Project loaded: " << fullPath << std::endl;
            }
        }
    }
    ui->closeFileBrowser();
    if (ui->mActiveNav == 0) {
        ui->createCenterContentArea();
    }
}

void UIManager::fileBrowserSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* btnRow = lv_obj_get_parent(btn);
    lv_obj_t* card = lv_obj_get_parent(btnRow);
    
    lv_obj_t* ta = nullptr;
    uint32_t child_cnt = lv_obj_get_child_cnt(card);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(card, i);
        if (lv_obj_check_type(child, &lv_textarea_class)) {
            ta = child;
            break;
        }
    }
    
    if (ta) {
        const char* filename = lv_textarea_get_text(ta);
        if (filename && strlen(filename) > 0) {
            std::string nameStr(filename);
            const char* browseDir = getenv("HOME");
            std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
            if (ui->mFileBrowserIsSampleSave) {
                std::string dirPath = homeStr + "/samples/";
                std::string fullPath = dirPath + nameStr;
                if (nameStr.find(".wav") == std::string::npos && nameStr.find(".WAV") == std::string::npos) {
                    fullPath += ".wav";
                }
                ui->mEngine.saveSample(ui->mActiveTrack, fullPath);
                std::cout << "Saved sample to text input: " << fullPath << std::endl;
                ui->mFileBrowserIsSampleSave = false;
            } else if (ui->mFileBrowserIsPresetSave) {
                std::string dirPath = homeStr + "/presets/";
                std::string fullPath = dirPath + nameStr;
                if (nameStr.find(".gbs") == std::string::npos) {
                    fullPath += ".gbs";
                }
                ui->mEngine.saveTrackPresetToPath(ui->mActiveTrack, fullPath);
                std::cout << "Saved track preset to text input: " << fullPath << std::endl;
                ui->mFileBrowserIsPresetSave = false;
            } else if (ui->mFileBrowserIsProject) {
                std::string dirPath = homeStr + "/projects/";
                std::string finalName = nameStr;
                std::string lowerName = finalName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName == "init" || lowerName == "init.loom") {
                    finalName = "Init.loom";
                }
                std::string fullPath = dirPath + finalName;
                if (finalName.find(".loom") == std::string::npos) {
                    fullPath += ".loom";
                }
                if (ui->mFileBrowserIsSave) {
                    ui->mEngine.saveProject(fullPath);
                    ui->mSettingsFilePath = fullPath + ".settings";
                    ui->saveSettings(ui->mSettingsFilePath);
                    std::cout << "Project saved to text input: " << fullPath << std::endl;
                }
            } else {
                std::string dirPath = browseDir ? std::string(browseDir) + "/sequences/" : "./sequences/";
                std::string fullPath = dirPath + nameStr;
                if (nameStr.find(".seq") == std::string::npos && nameStr.find(".json") == std::string::npos) {
                    fullPath += ".seq";
                }
                if (ui->mFileBrowserIsSave) {
                    ui->mEngine.saveProject(fullPath);
                    std::cout << "Project saved to text input: " << fullPath << std::endl;
                }
            }
        }
    }
    ui->closeFileBrowser();
    if (ui->mActiveNav == 0) {
        ui->createCenterContentArea();
    }
}

void UIManager::fileBrowserCancelEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->closeFileBrowser();
}

// =========================================================================
// --- Assign Screen (Patch Bay, LFOs, FX Chain, Controller Map) ---
// =========================================================================

struct DeleteRouteData {
    UIManager* ui;
    int destTrack;
    int sourceTrack;
    int source;
    int dest;
    int destParamId;
};

struct ModDestModalData {
    UIManager* ui;
    int callerType; // 1 = Macro, 2 = LFO
    int callerIdx;
    int slot = 0;
};

struct MacroArcCallbackData {
    UIManager* ui;
    int macroIdx;
    int slot = 0;
    lv_obj_t* valLbl;
};

struct MacroDdCallbackData {
    UIManager* ui;
    int macroIdx;
    lv_obj_t* srcDd;
};

static void deleteRouteDataFreeCb(lv_event_t* e) {
    DeleteRouteData* data = (DeleteRouteData*)lv_event_get_user_data(e);
    delete data;
}

const char* getParamName(int paramId) {
    switch (paramId) {
        case 0: return "Volume";
        case 1: return "Cutoff";
        case 2: return "Resonance";
        case 9: return "Pan";
        case 100: return "Attack";
        case 101: return "Decay";
        case 102: return "Sustain";
        case 103: return "Release";
        default: return "Param";
    }
}

const char* getSourceLabel(int src) {
    switch (src) {
        case 0: return "None";
        case 1: return "Track Output";
        case 2: return "LFO 1";
        case 3: return "LFO 2";
        case 4: return "LFO 3";
        case 5: return "LFO 4";
        case 6: return "LFO 5";
        case 7: return "LFO 6";
        case 8: return "Envelope";
        case 9: return "Sidechain";
        case 10: return "Macro 1";
        case 11: return "Macro 2";
        case 12: return "Macro 3";
        case 13: return "Macro 4";
        case 14: return "Macro 5";
        case 15: return "Macro 6";
        case 16: return "Macro 7";
        case 17: return "Macro 8";
        case 27: return "Aftertouch";
        default: return "Unknown";
    }
}

const char* getDestLabel(int dest) {
    switch (dest) {
        case 0: return "None";
        case 1: return "Volume";
        case 2: return "Cutoff";
        case 3: return "Pitch";
        case 4: return "WT Pos";
        case 5: return "Parameter";
        default: return "Unknown";
    }
}

const char* getLfoSyncLabel(int syncIdx) {
    switch (syncIdx) {
        case 0: return "8/1";
        case 1: return "6/1";
        case 2: return "4/1";
        case 3: return "3/1";
        case 4: return "2/1";
        case 5: return "1/1";
        case 6: return "1/2";
        case 7: return "1/3";
        case 8: return "1/4";
        case 9: return "1/6";
        case 10: return "1/8";
        case 11: return "1/12";
        case 12: return "1/16";
        case 13: return "1/24";
        case 14: return "1/32";
        case 15: return "1/48";
        case 16: return "1/64";
        case 17: return "1/72";
        case 18: return "1/96";
        default: return "1/4";
    }
}

std::string UIManager::getParameterNameString(int trackIdx, int paramId, AudioEngine* engine) {
    if (paramId >= 490 && paramId < 600) {
        if (paramId == 500) return "Reverb Size";
        if (paramId == 501) return "Reverb Damp";
        if (paramId == 502) return "Reverb Mod";
        if (paramId == 503) return "Reverb Mix";
        if (paramId == 510) return "Chorus Rate";
        if (paramId == 511) return "Chorus Depth";
        if (paramId == 512) return "Chorus Mix";
        if (paramId == 520) return "Delay Time";
        if (paramId == 521) return "Delay Feedback";
        return "Global FX " + std::to_string(paramId);
    }
    if (paramId >= 2200 && paramId < 2215) {
        if (paramId == 2200) return "Filter 1 Cutoff";
        if (paramId == 2201) return "Filter 1 Reson";
        if (paramId == 2202) return "Filter 1 Mode";
        if (paramId == 2205) return "Filter 2 Cutoff";
        if (paramId == 2206) return "Filter 2 Reson";
        if (paramId == 2207) return "Filter 2 Mode";
        if (paramId == 2210) return "Filter 3 Cutoff";
        if (paramId == 2211) return "Filter 3 Reson";
        if (paramId == 2212) return "Filter 3 Mode";
        return "Global Filter " + std::to_string(paramId);
    }
    std::string prefix = "Track " + std::to_string(trackIdx + 1) + " ";
    if (paramId == 0) return prefix + "Volume";
    if (paramId == 9) return prefix + "Pan";
    
    int engineType = 0; // Default: Subtractive
    if (engine && trackIdx >= 0 && trackIdx < 8) {
        engineType = engine->getTracks()[trackIdx].engineType;
    }

    if (paramId == 180 && engineType == 9) return prefix + "SF Preset";
    if (paramId == 196 && engineType == 1) return prefix + "FM Preset";
    if (paramId == 181 && engineType == 9) return prefix + "SF Bank";

    // Dynamic Filter Cutoff & Resonance naming based on engine type
    if (paramId == 1) {
        if (engineType == 4) return prefix + "WT Cutoff";
        return prefix + "Cutoff";
    }
    if (paramId == 2) {
        if (engineType == 4) return prefix + "WT Reson";
        return prefix + "Reson";
    }

    // Subtractive / SoundFont / Audio In / Master Envelope parameters
    if (engineType == 0 || engineType == 8 || engineType == 9) {
        if (paramId == 100) return prefix + "Attack";
        if (paramId == 101) return prefix + "Decay";
        if (paramId == 102) return prefix + "Sustain";
        if (paramId == 103) return prefix + "Release";
    }

    // Sampler Engine envelope parameters
    if (engineType == 2) {
        if (paramId == 310) return prefix + "Amp A";
        if (paramId == 311) return prefix + "Amp D";
        if (paramId == 312) return prefix + "Amp S";
        if (paramId == 313) return prefix + "Amp R";
        if (paramId == 314) return prefix + "Env Amt";
    }

    // Granular Engine envelope parameters
    if (engineType == 3) {
        if (paramId == 425) return prefix + "Amp A";
        if (paramId == 426) return prefix + "Amp D";
        if (paramId == 427) return prefix + "Amp S";
        if (paramId == 428) return prefix + "Amp R";
    }

    // Wavetable Engine envelope parameters
    if (engineType == 4) {
        if (paramId == 454) return prefix + "Amp A";
        if (paramId == 455) return prefix + "Amp D";
        if (paramId == 456) return prefix + "Amp S";
        if (paramId == 457) return prefix + "Amp R";
        if (paramId == 471) return prefix + "Filt A";
        if (paramId == 472) return prefix + "Filt D";
        if (paramId == 473) return prefix + "Filt S";
        if (paramId == 474) return prefix + "Filt R";
    }

    // Default fallback naming for standard parameter slots
    if (paramId == 100) return prefix + "Attack";
    if (paramId == 101) return prefix + "Decay";
    if (paramId == 102) return prefix + "Sustain";
    if (paramId == 103) return prefix + "Release";

    // Wavetable general parameter slots (that do not conflict with envelope parameters)
    if (paramId == 310) return prefix + "WT Pos";
    if (paramId == 311) return prefix + "WT Morph";

    if (paramId == 107) return prefix + "Osc1 Vol";
    if (paramId == 108) return prefix + "Osc2 Vol";
    if (paramId == 109) return prefix + "Osc3 Vol";
    if (paramId == 110) return prefix + "Noise Lvl";
    if (paramId == 6) return prefix + "Detune";
    if (paramId == 7) return prefix + "LFO Rate";
    if (paramId == 8) return prefix + "LFO Depth";
    if (paramId == 156) return prefix + "Algorithm";
    if (paramId == 160) return prefix + "Op1 Level";
    if (paramId == 166) return prefix + "Op2 Level";
    
    // Pitch and Stretch for Sampler
    if (paramId == 300) {
        if (engineType == 2) return prefix + "Pitch";
        return prefix + "WT Morph"; // Fallback for wavetable
    }
    if (paramId == 301) {
        if (engineType == 2) return prefix + "Stretch";
        return prefix + "WT Detune"; // Fallback
    }

    if (paramId == 320) return prefix + "Play Mode";
    if (paramId == 330) return prefix + "Start Pnt";
    if (paramId == 331) return prefix + "End Point";
    if (paramId == 302) return prefix + "Speed";
    if (paramId == 341) return prefix + "Slice Select";
    if (paramId == 342) return prefix + "Slice Lock";
    if (paramId == 360) return prefix + "Scrub Pos";
    if (paramId == 400) return prefix + "Grain Size";
    if (paramId == 401) return prefix + "Density";
    if (paramId == 402) return prefix + "Jitter";
    if (paramId == 403) return prefix + "Spread";

    if (paramId >= 2000 && paramId < 2180) {
        int fxIdx = (paramId - 2000) / 10;
        const char* FX_NAMES[17] = {
            "Overdrive", "Bitcrusher", "Chorus", "Phaser", "Tape Wobble",
            "Delay", "Reverb", "Slicer", "Compressor", "HP LFO", "LP LFO",
            "Flanger", "Filter 1", "Tape Echo", "Octaver", "Filter 2", "Filter 3"
        };
        if (fxIdx >= 0 && fxIdx < 17) {
            return prefix + FX_NAMES[fxIdx] + " Send";
        }
    }
    return prefix + "Param " + std::to_string(paramId);
}

std::string UIManager::getCompactDestName(int trackIdx, int paramId, AudioEngine* engine) {
    if (paramId == -1) return "Dest";
    std::string name = getParameterNameString(trackIdx, paramId, engine);
    if (name.rfind("Track ", 0) == 0) {
        size_t spacePos = name.find(' ', 6); // find second space
        if (spacePos != std::string::npos) {
            std::string num = name.substr(6, spacePos - 6);
            name = "T" + num + " " + name.substr(spacePos + 1);
        }
    }
    return name;
}

void UIManager::populateAssignScreen() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Scan routing matrix to pre-populate local states for Macros and LFOs
    for (int i = 0; i < 8; ++i) {
        for (int d = 0; d < 2; ++d) {
            mMacroDestParamId[i][d] = -1;
            mMacroDestTrack[i][d] = 0;
            mMacroDestType[i][d] = 5;
        }
    }
    for (int i = 0; i < 6; ++i) {
        mLfoDestParamId[i] = -1;
        mLfoDestTrack[i] = 0;
        mLfoDestType[i] = 5;
    }
    for (int t = 0; t < 8; ++t) {
        auto conns = mEngine.mRoutingMatrix.getConnections(t);
        std::vector<int> macroConnsCount(8, 0);
        for (const auto& conn : conns) {
            int srcIdx = static_cast<int>(conn.source);
            if (srcIdx >= 10 && srcIdx <= 17) { // Macro 1-8
                int m = srcIdx - 10;
                int d = macroConnsCount[m];
                if (d < 2) {
                    mMacroDestParamId[m][d] = conn.destParamId;
                    mMacroDestTrack[m][d] = t;
                    mMacroDestType[m][d] = static_cast<int>(conn.destination);
                    macroConnsCount[m]++;
                }
            } else if (srcIdx >= 2 && srcIdx <= 7) { // LFO 1-6
                int l = srcIdx - 2;
                mLfoDestParamId[l] = conn.destParamId;
                mLfoDestTrack[l] = t;
                mLfoDestType[l] = static_cast<int>(conn.destination);
            }
        }
    }

    lv_obj_t* tabview = lv_tabview_create(mCenterArea);
    mAssignTabview = tabview;
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);

    // Modern dark styling for tabview
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tabview, 0, 0);

    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(tab_bar, 1, LV_PART_MAIN);

    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "Controller Map");
    lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "Macros & Patch");
    lv_obj_t* tab3 = lv_tabview_add_tab(tabview, "LFOs");
    lv_obj_t* tab4 = lv_tabview_add_tab(tabview, "FX Chain");

    // Style the individual tab buttons in the tab bar
    for(uint32_t i = 0; i < lv_obj_get_child_count(tab_bar); i++) {
        lv_obj_t* btn = lv_obj_get_child(tab_bar, i);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_color(btn, trackColor, LV_STATE_CHECKED);
    }

    lv_tabview_set_active(tabview, mAssignActiveTabIdx, LV_ANIM_OFF);

    // Initialize real-time tracking pointers to nullptr
    for (int i = 0; i < 12; ++i) {
        mAssignKnobArcs[i] = nullptr;
        mAssignKnobValLabels[i] = nullptr;
        mAssignFaderSliders[i] = nullptr;
        mAssignFaderValLabels[i] = nullptr;
    }

    lv_obj_set_style_pad_all(tab1, 10, 0);
    lv_obj_set_style_pad_all(tab2, 10, 0);
    lv_obj_set_style_pad_all(tab3, 10, 0);
    lv_obj_set_style_pad_all(tab4, 10, 0);

    auto applyCardStyle = [](lv_obj_t* card) {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    };

    // =========================================================================
    // --- Tab 1: Controller Map ---
    // =========================================================================
    lv_obj_set_flex_flow(tab1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab1, 15, 0);

    // Section 1: 8 Circular Knobs
    lv_obj_t* knobsHeader = lv_label_create(tab1);
    lv_label_set_text(knobsHeader, "ASSIGNABLE PHYSICAL MIDI CC KNOBS");
    lv_obj_set_style_text_font(knobsHeader, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(knobsHeader, trackColor, 0);

    lv_obj_t* knobsRow = lv_obj_create(tab1);
    lv_obj_set_size(knobsRow, 814, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(knobsRow, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(knobsRow, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(knobsRow, 1, 0);
    lv_obj_set_style_radius(knobsRow, 12, 0);
    lv_obj_set_style_pad_all(knobsRow, 10, 0);
    lv_obj_set_style_pad_row(knobsRow, 10, 0);
    lv_obj_set_style_pad_column(knobsRow, 10, 0);
    lv_obj_set_layout(knobsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobsRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(knobsRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int k = 0; k < mSettingsKnobCount; ++k) {
        int paramId = mSeqMidiKnobParam[mActiveTrack][k];

        lv_obj_t* kCard = lv_obj_create(knobsRow);
        lv_obj_set_size(kCard, 95, 175);
        applyCardStyle(kCard);
        lv_obj_set_style_pad_all(kCard, 5, 0);
        lv_obj_set_layout(kCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(kCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(kCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* paramLbl = lv_label_create(kCard);
        if (paramId == -1) {
            lv_label_set_text(paramLbl, "None");
        } else {
            std::string name = getCompactDestName(mActiveTrack, paramId, &mEngine);
            std::string trackPrefix = "T" + std::to_string(mActiveTrack + 1) + " ";
            if (name.rfind(trackPrefix, 0) == 0) {
                name = name.substr(trackPrefix.length());
            }
            lv_label_set_text(paramLbl, name.c_str());
        }
        lv_obj_set_style_text_font(paramLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(paramLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* arc = lv_arc_create(kCard);
        mAssignKnobArcs[k] = arc;
        lv_obj_set_size(arc, 62, 62);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, (int)(mSeqMidiKnobValue[mActiveTrack][k] * 100));
        lv_obj_set_style_arc_color(arc, trackColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(arc, trackColor, LV_PART_KNOB);

        lv_obj_t* valLbl = lv_label_create(arc);
        mAssignKnobValLabels[k] = valLbl;
        lv_label_set_text_fmt(valLbl, "%d%%", (int)(mSeqMidiKnobValue[mActiveTrack][k] * 100));
        lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(valLbl);

        lv_obj_t* ccLbl = lv_label_create(kCard);
        lv_label_set_text_fmt(ccLbl, "CC %d", mSeqMidiKnobCC[mActiveTrack][k]);
        lv_obj_set_style_text_font(ccLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ccLbl, trackColor, 0);

        // Click on knob mapping text to remap
        lv_obj_add_flag(kCard, LV_OBJ_FLAG_CLICKABLE);
        struct RemapEventData {
            UIManager* ui;
            int targetIdx; // 0-7 knobs, 8-15 faders
        };
        RemapEventData* remapData = new RemapEventData{this, k};
        lv_obj_add_event_cb(kCard, openRemapModalEventCb, LV_EVENT_CLICKED, remapData);

        struct KnobEventData {
            UIManager* ui;
            int knobIdx;
            lv_obj_t* valLbl;
        };
        KnobEventData* kEvData = new KnobEventData{this, k, valLbl};
        lv_obj_add_event_cb(arc, physicalControlEventCb, LV_EVENT_VALUE_CHANGED, kEvData);

        auto remapDataFreeCb = [](lv_event_t* e) {
            RemapEventData* data = (RemapEventData*)lv_event_get_user_data(e);
            delete data;
        };
        auto kEvDataFreeCb = [](lv_event_t* e) {
            KnobEventData* data = (KnobEventData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(kCard, remapDataFreeCb, LV_EVENT_DELETE, remapData);
        lv_obj_add_event_cb(arc, kEvDataFreeCb, LV_EVENT_DELETE, kEvData);
    }

    // Section 2: 8 Vertical Faders
    lv_obj_t* fadersHeader = lv_label_create(tab1);
    lv_label_set_text(fadersHeader, "ASSIGNABLE PHYSICAL MIDI CC FADERS");
    lv_obj_set_style_text_font(fadersHeader, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(fadersHeader, trackColor, 0);

    lv_obj_t* fadersRow = lv_obj_create(tab1);
    lv_obj_set_size(fadersRow, 814, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(fadersRow, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(fadersRow, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(fadersRow, 1, 0);
    lv_obj_set_style_radius(fadersRow, 12, 0);
    lv_obj_set_style_pad_all(fadersRow, 10, 0);
    lv_obj_set_style_pad_row(fadersRow, 10, 0);
    lv_obj_set_style_pad_column(fadersRow, 10, 0);
    lv_obj_set_layout(fadersRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(fadersRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(fadersRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int f = 0; f < mSettingsSliderCount; ++f) {
        int paramId = mSeqMidiFaderParam[mActiveTrack][f];

        lv_obj_t* fCard = lv_obj_create(fadersRow);
        lv_obj_set_size(fCard, 95, 175);
        applyCardStyle(fCard);
        lv_obj_set_style_pad_all(fCard, 5, 0);
        lv_obj_set_layout(fCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(fCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(fCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* paramLbl = lv_label_create(fCard);
        if (paramId == -1) {
            lv_label_set_text(paramLbl, "None");
        } else {
            std::string name = getCompactDestName(mActiveTrack, paramId, &mEngine);
            std::string trackPrefix = "T" + std::to_string(mActiveTrack + 1) + " ";
            if (name.rfind(trackPrefix, 0) == 0) {
                name = name.substr(trackPrefix.length());
            }
            lv_label_set_text(paramLbl, name.c_str());
        }
        lv_obj_set_style_text_font(paramLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(paramLbl, lv_color_hex(0x888888), 0);

        // Vertical slider (fader)
        lv_obj_t* fader = lv_slider_create(fCard);
        mAssignFaderSliders[f] = fader;
        lv_obj_set_size(fader, 14, 88);
        lv_slider_set_range(fader, 0, 100);
        lv_slider_set_value(fader, (int)(mSeqMidiFaderValue[mActiveTrack][f] * 100), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(fader, trackColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(fader, trackColor, LV_PART_KNOB);

        lv_obj_t* valLbl = lv_label_create(fCard);
        mAssignFaderValLabels[f] = valLbl;
        lv_label_set_text_fmt(valLbl, "%d%%", (int)(mSeqMidiFaderValue[mActiveTrack][f] * 100));
        lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* ccLbl = lv_label_create(fCard);
        lv_label_set_text_fmt(ccLbl, "CC %d", mSeqMidiFaderCC[mActiveTrack][f]);
        lv_obj_set_style_text_font(ccLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ccLbl, trackColor, 0);

        // Click fader card to remap
        lv_obj_add_flag(fCard, LV_OBJ_FLAG_CLICKABLE);
        struct RemapEventData {
            UIManager* ui;
            int targetIdx; // 12-23 for faders
        };
        RemapEventData* remapData = new RemapEventData{this, f + 12};
        lv_obj_add_event_cb(fCard, openRemapModalEventCb, LV_EVENT_CLICKED, remapData);

        struct FaderEventData {
            UIManager* ui;
            int faderIdx;
            lv_obj_t* valLbl;
        };
        FaderEventData* fEvData = new FaderEventData{this, f, valLbl};
        lv_obj_add_event_cb(fader, physicalControlEventCb, LV_EVENT_VALUE_CHANGED, fEvData);

        auto remapDataFreeCb = [](lv_event_t* e) {
            RemapEventData* data = (RemapEventData*)lv_event_get_user_data(e);
            delete data;
        };
        auto fEvDataFreeCb = [](lv_event_t* e) {
            FaderEventData* data = (FaderEventData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(fCard, remapDataFreeCb, LV_EVENT_DELETE, remapData);
        lv_obj_add_event_cb(fader, fEvDataFreeCb, LV_EVENT_DELETE, fEvData);
    }

    // =========================================================================
    // --- Tab 2: Macros & Patch Bay ---
    // =========================================================================
    lv_obj_t* tab2Container = lv_obj_create(tab2);
    lv_obj_set_size(tab2Container, 814, 500);
    lv_obj_set_style_bg_opa(tab2Container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab2Container, 0, 0);
    lv_obj_set_style_pad_all(tab2Container, 0, 0);
    lv_obj_set_layout(tab2Container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab2Container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab2Container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(tab2Container, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: Macros Grid (2 rows of 4 Macros)
    lv_obj_t* macrosGrid = lv_obj_create(tab2Container);
    lv_obj_set_size(macrosGrid, 534, 500);
    lv_obj_set_style_bg_opa(macrosGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(macrosGrid, 0, 0);
    lv_obj_set_style_pad_all(macrosGrid, 0, 0);
    lv_obj_set_layout(macrosGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(macrosGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(macrosGrid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_row(macrosGrid, 10, 0);
    lv_obj_set_style_pad_column(macrosGrid, 10, 0);
    lv_obj_remove_flag(macrosGrid, LV_OBJ_FLAG_SCROLLABLE);

    // Right side: Active Connections List
    lv_obj_t* listCard = lv_obj_create(tab2Container);
    lv_obj_set_size(listCard, 270, 500);
    applyCardStyle(listCard);
    lv_obj_set_layout(listCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(listCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(listCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* listTitle = lv_label_create(listCard);
    lv_label_set_text(listTitle, "ACTIVE PATCH MATRIX");
    lv_obj_set_style_text_font(listTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(listTitle, trackColor, 0);

    // Aftertouch Destination Selection Card
    lv_obj_t* atCard = lv_obj_create(listCard);
    lv_obj_set_size(atCard, 246, 65);
    lv_obj_set_style_bg_color(atCard, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(atCard, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_border_width(atCard, 1, 0);
    lv_obj_set_style_radius(atCard, 8, 0);
    lv_obj_set_style_pad_all(atCard, 8, 0);
    lv_obj_set_layout(atCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(atCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(atCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(atCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* atTitle = lv_label_create(atCard);
    lv_label_set_text(atTitle, "TRACK AFTERTOUCH MODULATION");
    lv_obj_set_style_text_font(atTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(atTitle, trackColor, 0);

    lv_obj_t* atDestBtn = lv_button_create(atCard);
    lv_obj_set_size(atDestBtn, 230, 24);
    lv_obj_set_style_bg_color(atDestBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(atDestBtn, 4, 0);

    lv_obj_t* atDestLbl = lv_label_create(atDestBtn);
    mAftertouchDestBtnLabel[mActiveTrack] = atDestLbl;

    if (mAftertouchDestParamId[mActiveTrack] != -1) {
        std::string destName = getParameterNameString(mActiveTrack, mAftertouchDestParamId[mActiveTrack], &mEngine);
        lv_label_set_text(atDestLbl, destName.c_str());
    } else {
        lv_label_set_text(atDestLbl, "Select Destination");
    }
    lv_obj_set_style_text_font(atDestLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(atDestLbl);

    ModDestModalData* atClickData = new ModDestModalData{this, 3, mActiveTrack};
    lv_obj_add_event_cb(atDestBtn, openModDestModalEventCb, LV_EVENT_CLICKED, atClickData);

    auto atClickDataFreeCb = [](lv_event_t* e) {
        ModDestModalData* data = (ModDestModalData*)lv_event_get_user_data(e);
        delete data;
    };
    lv_obj_add_event_cb(atDestBtn, atClickDataFreeCb, LV_EVENT_DELETE, atClickData);

    mActiveRoutingsContainer = lv_obj_create(listCard);
    lv_obj_set_size(mActiveRoutingsContainer, 246, 370);
    lv_obj_set_style_bg_opa(mActiveRoutingsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mActiveRoutingsContainer, 0, 0);
    lv_obj_set_style_pad_all(mActiveRoutingsContainer, 0, 0);
    lv_obj_set_layout(mActiveRoutingsContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mActiveRoutingsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mActiveRoutingsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(mActiveRoutingsContainer, LV_OBJ_FLAG_SCROLLABLE);

    rebuildActiveRoutings(mActiveRoutingsContainer);

    for (int m = 0; m < 8; ++m) {
        AudioEngine::MacroModule& macro = mEngine.mMacros[m];

        lv_obj_t* macroCard = lv_obj_create(macrosGrid);
        lv_obj_set_size(macroCard, 126, 240);
        applyCardStyle(macroCard);
        lv_obj_set_style_pad_all(macroCard, 5, 0);
        lv_obj_set_layout(macroCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(macroCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(macroCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Header: Macro Name & Learn Button Row
        lv_obj_t* headerRow = lv_obj_create(macroCard);
        lv_obj_set_size(headerRow, 116, 22);
        lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(headerRow, 0, 0);
        lv_obj_set_style_pad_all(headerRow, 0, 0);
        lv_obj_set_layout(headerRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* mLbl = lv_label_create(headerRow);
        lv_label_set_text_fmt(mLbl, "MACRO %d", m + 1);
        lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(mLbl, trackColor, 0);

        lv_obj_t* learnBtn = lv_button_create(headerRow);
        lv_obj_set_size(learnBtn, 46, 18);
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(learnBtn, 3, 0);
        lv_obj_t* learnLbl = lv_label_create(learnBtn);
        lv_label_set_text(learnLbl, "Learn");
        lv_obj_set_style_text_font(learnLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(learnLbl);
        
        struct LearnBtnData {
            UIManager* ui;
            int macroIdx;
        };
        LearnBtnData* learnData = new LearnBtnData{this, m};
        lv_obj_add_event_cb(learnBtn, macroSourceBtnEventCb, LV_EVENT_CLICKED, learnData);
        auto learnFreeCb = [](lv_event_t* e) { delete (LearnBtnData*)lv_event_get_user_data(e); };
        lv_obj_add_event_cb(learnBtn, learnFreeCb, LV_EVENT_DELETE, learnData);

        // Fetch routing amount from matrix if exists for Destination 1 (or fallback)
        float amt1 = 0.0f;
        float amt2 = 0.0f;
        for (int t = 0; t < 8; ++t) {
            auto conns = mEngine.mRoutingMatrix.getConnections(t);
            int slotCount = 0;
            for (const auto& conn : conns) {
                if (conn.source == static_cast<ModSource>(10 + m)) {
                    if (slotCount == 0) {
                        amt1 = conn.amount;
                        slotCount++;
                    } else if (slotCount == 1) {
                        amt2 = conn.amount;
                        break;
                    }
                }
            }
        }

        // Source Dropdown (placed above controls for optimal spacing)
        lv_obj_t* srcDd = lv_dropdown_create(macroCard);
        lv_obj_set_size(srcDd, 116, 26);
        lv_dropdown_set_options(srcDd, "Source\nTrack Out\nLFO 1\nLFO 2\nLFO 3\nLFO 4\nLFO 5\nLFO 6\nEnvelope\nSidechain\nMacro 1\nMacro 2\nMacro 3\nMacro 4\nMacro 5\nMacro 6\nMacro 7\nMacro 8");
        lv_obj_set_style_text_font(srcDd, &lv_font_montserrat_10, 0);

        int selIdx = 0;
        if (macro.sourceType == 1) selIdx = 1;
        else if (macro.sourceType == 3 && macro.sourceIndex >= 0 && macro.sourceIndex < 6) selIdx = 2 + macro.sourceIndex;
        else if (macro.sourceType == 4) selIdx = 8;
        else if (macro.sourceType == 5) selIdx = 9;
        else if (macro.sourceType == 2 && macro.sourceIndex >= 0 && macro.sourceIndex < 8) selIdx = 10 + macro.sourceIndex;
        lv_dropdown_set_selected(srcDd, selIdx);

        MacroDdCallbackData* srcData = new MacroDdCallbackData{this, m, srcDd};
        lv_obj_add_event_cb(srcDd, macroDropdownEventCb, LV_EVENT_VALUE_CHANGED, srcData);
        auto srcDataFreeCb = [](lv_event_t* e) { delete (MacroDdCallbackData*)lv_event_get_user_data(e); };
        lv_obj_add_event_cb(srcDd, srcDataFreeCb, LV_EVENT_DELETE, srcData);

        // Row of 2 columns, grouping each Arc with its Destination button
        lv_obj_t* controlsRow = lv_obj_create(macroCard);
        lv_obj_set_size(controlsRow, 116, 115);
        lv_obj_set_style_bg_opa(controlsRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(controlsRow, 0, 0);
        lv_obj_set_style_pad_all(controlsRow, 0, 0);
        lv_obj_set_layout(controlsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(controlsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(controlsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (int d = 0; d < 2; ++d) {
            // Column for Arc + Dest Button (more compact, fits card perfectly)
            lv_obj_t* col = lv_obj_create(controlsRow);
            lv_obj_set_size(col, 55, 105);
            lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(col, 0, 0);
            lv_obj_set_style_pad_all(col, 0, 0);
            lv_obj_set_layout(col, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Bipolar Arc
            int arcVal = (int)((d == 0 ? amt1 : amt2) * 100.0f);
            lv_obj_t* mArc = lv_arc_create(col);
            lv_obj_set_size(mArc, 48, 48);
            lv_arc_set_range(mArc, -100, 100);
            lv_arc_set_value(mArc, arcVal);
            lv_obj_set_style_arc_color(mArc, trackColor, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(mArc, trackColor, LV_PART_KNOB);
            
            // Thin arc styling & small handle scaling to prevent text/label clipping
            lv_obj_set_style_arc_width(mArc, 3, LV_PART_MAIN);
            lv_obj_set_style_arc_width(mArc, 3, LV_PART_INDICATOR);
            lv_obj_set_style_width(mArc, 6, LV_PART_KNOB);
            lv_obj_set_style_height(mArc, 6, LV_PART_KNOB);
            lv_obj_set_style_pad_all(mArc, 0, LV_PART_KNOB);

            mMacroArc[m][d] = mArc;

            lv_obj_t* mValLbl = lv_label_create(mArc);
            lv_label_set_text_fmt(mValLbl, "%s%d%%", arcVal > 0 ? "+" : "", arcVal);
            lv_obj_set_style_text_font(mValLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(mValLbl);

            MacroArcCallbackData* arcData = new MacroArcCallbackData{this, m, d, mValLbl};
            lv_obj_add_event_cb(mArc, macroValueArcEventCb, LV_EVENT_VALUE_CHANGED, arcData);
            auto arcDataFreeCb = [](lv_event_t* e) { delete (MacroArcCallbackData*)lv_event_get_user_data(e); };
            lv_obj_add_event_cb(mArc, arcDataFreeCb, LV_EVENT_DELETE, arcData);

            // Destination Button directly underneath
            lv_obj_t* destBtn = lv_button_create(col);
            lv_obj_set_size(destBtn, 55, 34); // Sized taller to support wrapped labels
            lv_obj_set_style_bg_color(destBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_radius(destBtn, 4, 0);
            lv_obj_set_style_pad_all(destBtn, 2, 0); // Minimize internal padding to maximize text space

            lv_obj_t* destLbl = lv_label_create(destBtn);
            mMacroDestBtnLabel[m][d] = destLbl;
            lv_obj_set_width(destLbl, 51); // Give it a fixed width for wrapping
            lv_label_set_long_mode(destLbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(destLbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(destLbl, lv_color_hex(0xFFFFFF), 0); // Explicit high-contrast white text

            if (mMacroDestParamId[m][d] != -1) {
                std::string destName = getCompactDestName(mMacroDestTrack[m][d], mMacroDestParamId[m][d], &mEngine);
                lv_label_set_text(destLbl, destName.c_str());
            } else {
                lv_label_set_text(destLbl, "Dest");
            }
            lv_obj_set_style_text_font(destLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(destLbl);

            ModDestModalData* clickData = new ModDestModalData{this, 1, m, d};
            lv_obj_add_event_cb(destBtn, openModDestModalEventCb, LV_EVENT_CLICKED, clickData);
            auto clickDataFreeCb = [](lv_event_t* e) { delete (ModDestModalData*)lv_event_get_user_data(e); };
            lv_obj_add_event_cb(destBtn, clickDataFreeCb, LV_EVENT_DELETE, clickData);
        }
    }

    // =========================================================================
    // --- Tab 3: Bank of LFOs ---
    // =========================================================================
    lv_obj_set_flex_flow(tab3, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tab3, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);

    for (int l = 0; l < 6; ++l) {
        LfoEngine& lfo = mEngine.mLfos[l];

        lv_obj_t* lfoCard = lv_obj_create(tab3);
        lv_obj_set_size(lfoCard, 240, 235); // Sized to fit 3 columns and 2 rows perfectly!
        applyCardStyle(lfoCard);
        lv_obj_set_layout(lfoCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(lfoCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(lfoCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Header with active track colored label
        lv_obj_t* lfoHeader = lv_obj_create(lfoCard);
        lv_obj_set_size(lfoHeader, 216, 30);
        lv_obj_set_style_bg_opa(lfoHeader, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lfoHeader, 0, 0);
        lv_obj_set_style_pad_all(lfoHeader, 0, 0);
        lv_obj_set_layout(lfoHeader, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(lfoHeader, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(lfoHeader, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* lfoTitle = lv_label_create(lfoHeader);
        lv_label_set_text_fmt(lfoTitle, "LFO %d", l + 1);
        lv_obj_set_style_text_font(lfoTitle, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lfoTitle, trackColor, 0);

        // Sync Switch
        lv_obj_t* syncBtn = lv_button_create(lfoHeader);
        lv_obj_set_size(syncBtn, 65, 26);
        lv_obj_add_flag(syncBtn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_radius(syncBtn, 6, 0);
        lv_obj_t* syncLbl = lv_label_create(syncBtn);
        lv_label_set_text(syncLbl, lfo.getSync() ? "SYNC" : "FREE");
        lv_obj_set_style_text_font(syncLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(syncLbl);
        if (lfo.getSync()) {
            lv_obj_add_state(syncBtn, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(syncBtn, trackColor, 0);
        } else {
            lv_obj_set_style_bg_color(syncBtn, lv_color_hex(0x444444), 0);
        }

        // Shape selector dropdown
        lv_obj_t* shapeDd = lv_dropdown_create(lfoCard);
        lv_obj_set_size(shapeDd, 216, 32);
        lv_dropdown_set_options(shapeDd, "Sine Wave\nTriangle\nSquare\nSawtooth\nRandom / S&H");
        lv_dropdown_set_selected(shapeDd, lfo.getShape());
        lv_obj_set_style_text_font(shapeDd, &lv_font_montserrat_10, 0);

        // Arcs row
        lv_obj_t* lfoControlsRow = lv_obj_create(lfoCard);
        lv_obj_set_size(lfoControlsRow, 216, 95);
        lv_obj_set_style_bg_opa(lfoControlsRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lfoControlsRow, 0, 0);
        lv_obj_set_style_pad_all(lfoControlsRow, 0, 0);
        lv_obj_set_layout(lfoControlsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(lfoControlsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(lfoControlsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // 1. Depth Arc
        lv_obj_t* depthGrp = lv_obj_create(lfoControlsRow);
        lv_obj_set_size(depthGrp, 103, 95);
        lv_obj_set_style_bg_opa(depthGrp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(depthGrp, 0, 0);
        lv_obj_set_style_pad_all(depthGrp, 0, 0);
        lv_obj_set_layout(depthGrp, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(depthGrp, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(depthGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* dLbl = lv_label_create(depthGrp);
        lv_label_set_text(dLbl, "Depth");
        lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(dLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* dArc = lv_arc_create(depthGrp);
        lv_obj_set_size(dArc, 62, 62);
        lv_arc_set_range(dArc, 0, 100);
        lv_arc_set_value(dArc, (int)(lfo.getDepth() * 100));
        lv_obj_set_style_arc_color(dArc, trackColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(dArc, trackColor, LV_PART_KNOB);

        lv_obj_t* dVal = lv_label_create(dArc);
        lv_label_set_text_fmt(dVal, "%d%%", (int)(lfo.getDepth() * 100));
        lv_obj_set_style_text_font(dVal, &lv_font_montserrat_10, 0);
        lv_obj_center(dVal);

        // 2. Rate Arc
        lv_obj_t* rateGrp = lv_obj_create(lfoControlsRow);
        lv_obj_set_size(rateGrp, 103, 95);
        lv_obj_set_style_bg_opa(rateGrp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(rateGrp, 0, 0);
        lv_obj_set_style_pad_all(rateGrp, 0, 0);
        lv_obj_set_layout(rateGrp, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(rateGrp, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(rateGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* rLbl = lv_label_create(rateGrp);
        lv_label_set_text(rLbl, "Rate");
        lv_obj_set_style_text_font(rLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(rLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* rArc = lv_arc_create(rateGrp);
        lv_obj_set_size(rArc, 62, 62);
        lv_arc_set_range(rArc, 0, 100);
        lv_arc_set_value(rArc, (int)(lfo.getUiRate() * 100));
        lv_obj_set_style_arc_color(rArc, trackColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(rArc, trackColor, LV_PART_KNOB);

        lv_obj_t* rVal = lv_label_create(rArc);
        if (lfo.getSync()) {
            int syncIdx = (int)(lfo.getUiRate() * 18.99f);
            lv_label_set_text(rVal, getLfoSyncLabel(syncIdx));
        } else {
            float hz = 0.01f * powf(10.0f, lfo.getUiRate() * 3.47712f);
            if (hz < 1.0f) {
                lv_label_set_text_fmt(rVal, "%.2fHz", hz);
            } else {
                lv_label_set_text_fmt(rVal, "%.1fHz", hz);
            }
        }
        lv_obj_set_style_text_font(rVal, &lv_font_montserrat_10, 0);
        lv_obj_center(rVal);

        // Hook up callback structures
        struct LfoCallbackData {
            UIManager* ui;
            int lfoIdx;
            lv_obj_t* valLbl;
            lv_obj_t* syncBtn;
            lv_obj_t* syncValLbl;
        };
        LfoCallbackData* lData = new LfoCallbackData{this, l, dVal, syncBtn, rVal};
        lv_obj_add_event_cb(dArc, lfoDepthArcEventCb, LV_EVENT_VALUE_CHANGED, lData);
        
        LfoCallbackData* lRateData = new LfoCallbackData{this, l, rVal, syncBtn, rVal};
        lv_obj_add_event_cb(rArc, lfoRateArcEventCb, LV_EVENT_VALUE_CHANGED, lRateData);

        struct LfoSyncCallbackData {
            UIManager* ui;
            int lfoIdx;
            lv_obj_t* syncBtn;
            lv_obj_t* syncLbl;
            lv_obj_t* rateArc;
            lv_obj_t* rateValLbl;
            lv_color_t trackColor;
        };
        LfoSyncCallbackData* sData = new LfoSyncCallbackData{this, l, syncBtn, syncLbl, rArc, rVal, trackColor};
        lv_obj_add_event_cb(syncBtn, lfoSyncBtnEventCb, LV_EVENT_CLICKED, sData);

        struct LfoShapeCallbackData {
            UIManager* ui;
            int lfoIdx;
        };
        LfoShapeCallbackData* shData = new LfoShapeCallbackData{this, l};
        lv_obj_add_event_cb(shapeDd, lfoShapeDdEventCb, LV_EVENT_VALUE_CHANGED, shData);

        auto lfoDataFreeCb = [](lv_event_t* e) {
            LfoCallbackData* data = (LfoCallbackData*)lv_event_get_user_data(e);
            delete data;
        };
        auto lfoSyncDataFreeCb = [](lv_event_t* e) {
            LfoSyncCallbackData* data = (LfoSyncCallbackData*)lv_event_get_user_data(e);
            delete data;
        };
        auto lfoShapeDataFreeCb = [](lv_event_t* e) {
            LfoShapeCallbackData* data = (LfoShapeCallbackData*)lv_event_get_user_data(e);
            delete data;
        };

        lv_obj_add_event_cb(dArc, lfoDataFreeCb, LV_EVENT_DELETE, lData);
        lv_obj_add_event_cb(rArc, lfoDataFreeCb, LV_EVENT_DELETE, lRateData);
        lv_obj_add_event_cb(syncBtn, lfoSyncDataFreeCb, LV_EVENT_DELETE, sData);
        lv_obj_add_event_cb(shapeDd, lfoShapeDataFreeCb, LV_EVENT_DELETE, shData);

        // Destination Button
        lv_obj_t* destBtn = lv_button_create(lfoCard);
        lv_obj_set_size(destBtn, 216, 26);
        lv_obj_set_style_bg_color(destBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(destBtn, 4, 0);

        lv_obj_t* destLbl = lv_label_create(destBtn);
        mLfoDestBtnLabel[l] = destLbl;

        if (mLfoDestParamId[l] != -1) {
            std::string destName = getParameterNameString(mLfoDestTrack[l], mLfoDestParamId[l], &mEngine);
            lv_label_set_text(destLbl, destName.c_str());
        } else {
            lv_label_set_text(destLbl, "Destination");
        }
        lv_obj_set_style_text_font(destLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(destLbl);

        ModDestModalData* clickData = new ModDestModalData{this, 2, l};
        lv_obj_add_event_cb(destBtn, openModDestModalEventCb, LV_EVENT_CLICKED, clickData);

        auto clickDataFreeCb = [](lv_event_t* e) {
            ModDestModalData* data = (ModDestModalData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(destBtn, clickDataFreeCb, LV_EVENT_DELETE, clickData);
    }

    // =========================================================================
    // --- Tab 4: FX Pedal Serial Chaining ---
    // =========================================================================
    lv_obj_set_flex_flow(tab4, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab4, 15, 0);

    const char* FX_NAMES[17] = {
        "Overdrive", "Bitcrusher", "Chorus", "Phaser", "Tape Wobble",
        "Delay", "Reverb", "Slicer", "Compressor", "HP LFO Filter",
        "LP LFO Filter", "Flanger", "Filter Pedal 1", "Tape Echo", "Octaver",
        "Filter Pedal 2", "Filter Pedal 3"
    };

    for (int chainIdx = 0; chainIdx < 2; ++chainIdx) {
        lv_obj_t* chainCard = lv_obj_create(tab4);
        lv_obj_set_size(chainCard, 790, 185);
        applyCardStyle(chainCard);
        lv_obj_set_layout(chainCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(chainCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chainCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* chainTitle = lv_label_create(chainCard);
        lv_label_set_text_fmt(chainTitle, "SERIAL FX CHAIN %d", chainIdx + 1);
        lv_obj_set_style_text_font(chainTitle, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(chainTitle, trackColor, 0);

        lv_obj_t* slotsRow = lv_obj_create(chainCard);
        lv_obj_set_size(slotsRow, 766, 120);
        lv_obj_set_style_bg_opa(slotsRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(slotsRow, 0, 0);
        lv_obj_set_style_pad_all(slotsRow, 0, 0);
        lv_obj_set_layout(slotsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(slotsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(slotsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (int slotIdx = 0; slotIdx < 5; ++slotIdx) {
            int pedalId = mFxChainPedals[chainIdx][slotIdx];

            lv_obj_t* slotBtn = lv_button_create(slotsRow);
            lv_obj_set_size(slotBtn, 128, 100);
            lv_obj_set_style_radius(slotBtn, 10, 0);

            if (pedalId >= 0 && pedalId < 17) {
                lv_obj_set_style_bg_color(slotBtn, lv_color_hex(0x222222), 0);
                lv_obj_set_style_border_color(slotBtn, trackColor, 0);
                lv_obj_set_style_border_width(slotBtn, 1, 0);
            } else {
                lv_obj_set_style_bg_color(slotBtn, lv_color_hex(0x151515), 0);
                lv_obj_set_style_border_color(slotBtn, lv_color_hex(0x444444), 0);
                lv_obj_set_style_border_width(slotBtn, 1, 0);
            }

            lv_obj_t* slotLbl = lv_label_create(slotBtn);
            lv_label_set_text_fmt(slotLbl, "SLOT %d", slotIdx + 1);
            lv_obj_set_style_text_font(slotLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(slotLbl, lv_color_hex(0x888888), 0);
            lv_obj_set_align(slotLbl, LV_ALIGN_TOP_MID);

            lv_obj_t* nameLbl = lv_label_create(slotBtn);
            if (pedalId >= 0 && pedalId < 17) {
                lv_label_set_text(nameLbl, FX_NAMES[pedalId]);
                lv_obj_set_style_text_color(nameLbl, trackColor, 0);
            } else {
                lv_label_set_text(nameLbl, "---");
                lv_obj_set_style_text_color(nameLbl, lv_color_hex(0x555555), 0);
            }
            lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
            lv_obj_center(nameLbl);

            struct PedalSlotClickData {
                UIManager* ui;
                int chainIdx;
                int slotIdx;
            };
            PedalSlotClickData* psData = new PedalSlotClickData{this, chainIdx, slotIdx};
            lv_obj_add_event_cb(slotBtn, pedalSlotClickEventCb, LV_EVENT_CLICKED, psData);

            auto psFreeCb = [](lv_event_t* e) {
                PedalSlotClickData* data = (PedalSlotClickData*)lv_event_get_user_data(e);
                delete data;
            };
            lv_obj_add_event_cb(slotBtn, psFreeCb, LV_EVENT_DELETE, psData);

            // Draw a subtle connecting arrow inside the row except after the last slot
            if (slotIdx < 4) {
                lv_obj_t* arrow = lv_label_create(slotsRow);
                lv_label_set_text(arrow, "\xe2\x86\x92"); // right arrow
                lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(arrow, lv_color_hex(0x444444), 0);
            }
        }
    }
}

void UIManager::rebuildActiveRoutings(lv_obj_t* parent) {
    lv_obj_clean(parent);
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Local struct to represent a row in the unified active patch matrix
    struct ActiveRouteRow {
        bool isMatrix; // true = Routing Matrix connection, false = Knob/Fader direct CC mapping
        int type; // if isMatrix: ModSource. if not isMatrix: 0 = Knob, 1 = Fader
        int knobFaderIdx; // 0-11 index of Knob or Fader if not isMatrix
        
        std::string sourceAbbr; // e.g., "AT", "L1", "M5", "K2", "F4"
        std::string paramName;  // e.g., "Cutoff"
        bool polarity;          // true = positive (+), false = negative (-)
        float amount;           // 0.0 to 1.0 (attenuation/value)
        
        // Matrix route reference details
        ModSource matrixSource;
        ModDestination matrixDest;
        int matrixDestParamId;
        int matrixSourceTrack;
    };

    std::vector<ActiveRouteRow> rows;

    // Helper lambda to get source abbreviations
    auto getAbbreviation = [](int srcIdx) -> std::string {
        if (srcIdx == 18 || srcIdx == 27) return "AT";
        if (srcIdx >= 2 && srcIdx <= 7) return "L" + std::to_string(srcIdx - 1);
        if (srcIdx == 8) return "ENV";
        if (srcIdx == 9) return "SC";
        if (srcIdx >= 10 && srcIdx <= 17) return "M" + std::to_string(srcIdx - 9);
        return "??";
    };

    // 1. Fetch LFOs, Macros, and Aftertouch connections from the matrix
    auto connections = mEngine.mRoutingMatrix.getConnections(mActiveTrack);
    for (const auto& conn : connections) {
        ActiveRouteRow r;
        r.isMatrix = true;
        r.type = static_cast<int>(conn.source);
        r.knobFaderIdx = -1;
        r.sourceAbbr = getAbbreviation(r.type);
        
        if (conn.destination == ModDestination::Parameter) {
            r.paramName = getCompactDestName(mActiveTrack, conn.destParamId, &mEngine);
        } else {
            r.paramName = getDestLabel(static_cast<int>(conn.destination));
        }
        
        r.polarity = (conn.amount >= 0.0f);
        r.amount = std::abs(conn.amount);
        
        r.matrixSource = conn.source;
        r.matrixDest = conn.destination;
        r.matrixDestParamId = conn.destParamId;
        r.matrixSourceTrack = conn.sourceTrack;
        
        rows.push_back(r);
    }

    if (rows.empty()) {
        lv_obj_t* placeholder = lv_label_create(parent);
        lv_label_set_text(placeholder, "No active patch mappings.\nMap Aftertouch, LFOs, or Macros!");
        lv_obj_set_style_text_color(placeholder, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(placeholder);
        return;
    }

    int parentWidth = lv_obj_get_width(parent);
    if (parentWidth <= 0) {
        parentWidth = (parent == mActiveRoutingsContainer) ? 230 : 780;
    }

    // Structure for carrying actions to dynamic callbacks
    struct RouteActionData {
        UIManager* ui;
        ActiveRouteRow route;
        lv_obj_t* slider;
        lv_obj_t* button;
    };

    for (const auto& rRow : rows) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, parentWidth > 300 ? 780 : 230, 40);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Abbreviation Label
        lv_obj_t* abbrLbl = lv_label_create(row);
        lv_label_set_text(abbrLbl, rRow.sourceAbbr.c_str());
        lv_obj_set_style_text_font(abbrLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(abbrLbl, trackColor, 0);

        // Arrow separator
        lv_obj_t* arrLbl = lv_label_create(row);
        lv_label_set_text(arrLbl, "\xe2\x86\x92"); // →
        lv_obj_set_style_text_font(arrLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(arrLbl, lv_color_hex(0x555555), 0);

        // Parameter/Destination Label
        lv_obj_t* paramLbl = lv_label_create(row);
        lv_label_set_text(paramLbl, rRow.paramName.c_str());
        lv_obj_set_style_text_font(paramLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_width(paramLbl, 52);
        lv_label_set_long_mode(paramLbl, LV_LABEL_LONG_CLIP);

        // Attenuation Slider (hidden by default)
        lv_obj_t* attSlider = lv_slider_create(row);
        lv_obj_set_size(attSlider, 40, 8);
        lv_slider_set_range(attSlider, 0, 100);
        lv_slider_set_value(attSlider, (int)(rRow.amount * 100.0f), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(attSlider, trackColor, LV_PART_INDICATOR);
        lv_obj_add_flag(attSlider, LV_OBJ_FLAG_HIDDEN);

        // Polarity Button
        lv_obj_t* polBtn = lv_button_create(row);
        lv_obj_set_size(polBtn, 22, 24);
        lv_obj_set_style_bg_color(polBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(polBtn, 4, 0);
        lv_obj_set_style_pad_all(polBtn, 0, 0);
        lv_obj_t* polLbl = lv_label_create(polBtn);
        lv_label_set_text(polLbl, rRow.polarity ? "+" : "-");
        lv_obj_set_style_text_font(polLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(polLbl);

        // Attenuation Button (toggles slider visibility)
        lv_obj_t* attBtn = lv_button_create(row);
        lv_obj_set_size(attBtn, 30, 24);
        lv_obj_set_style_bg_color(attBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(attBtn, 4, 0);
        lv_obj_set_style_pad_all(attBtn, 0, 0);
        lv_obj_t* attLbl = lv_label_create(attBtn);
        lv_label_set_text(attLbl, "AMT");
        lv_obj_set_style_text_font(attLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(attLbl);

        // Attenuation toggle event
        auto attToggleCb = [](lv_event_t* e) {
            lv_obj_t* slider = (lv_obj_t*)lv_event_get_user_data(e);
            if (lv_obj_has_flag(slider, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_remove_flag(slider, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(slider, LV_OBJ_FLAG_HIDDEN);
            }
        };
        lv_obj_add_event_cb(attBtn, attToggleCb, LV_EVENT_CLICKED, attSlider);

        // Delete button
        lv_obj_t* delBtn = lv_button_create(row);
        lv_obj_set_size(delBtn, 22, 24);
        lv_obj_set_style_bg_color(delBtn, lv_color_hex(0xAA3333), 0);
        lv_obj_set_style_radius(delBtn, 4, 0);
        lv_obj_set_style_pad_all(delBtn, 0, 0);
        lv_obj_t* delLbl = lv_label_create(delBtn);
        lv_label_set_text(delLbl, "X");
        lv_obj_set_style_text_font(delLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(delLbl);

        // Actions Callback Data
        RouteActionData* actData = new RouteActionData{this, rRow, attSlider, polBtn};

        // Polarity Toggle Click Callback
        auto polarityClickCb = [](lv_event_t* e) {
            RouteActionData* data = (RouteActionData*)lv_event_get_user_data(e);
            UIManager* ui = data->ui;
            lv_obj_t* label = lv_obj_get_child(data->button, 0);
            
            if (data->route.isMatrix) {
                auto currentConns = ui->mEngine.mRoutingMatrix.getConnections(ui->mActiveTrack);
                float amt = 0.0f;
                for (const auto& conn : currentConns) {
                    if (conn.source == data->route.matrixSource &&
                        conn.destination == data->route.matrixDest &&
                        conn.destParamId == data->route.matrixDestParamId &&
                        conn.sourceTrack == data->route.matrixSourceTrack) {
                        amt = conn.amount;
                        break;
                    }
                }
                float newAmt = -amt;
                ui->mEngine.setRouting(ui->mActiveTrack, data->route.matrixSourceTrack,
                                       static_cast<int>(data->route.matrixSource), static_cast<int>(data->route.matrixDest),
                                       newAmt, data->route.matrixDestParamId);
                lv_label_set_text(label, newAmt >= 0 ? "+" : "-");
            } else {
                if (data->route.type == 0) { // Knob
                    ui->mSeqMidiKnobInverted[ui->mActiveTrack][data->route.knobFaderIdx] = !ui->mSeqMidiKnobInverted[ui->mActiveTrack][data->route.knobFaderIdx];
                    lv_label_set_text(label, ui->mSeqMidiKnobInverted[ui->mActiveTrack][data->route.knobFaderIdx] ? "-" : "+");
                } else { // Fader
                    ui->mSeqMidiFaderInverted[ui->mActiveTrack][data->route.knobFaderIdx] = !ui->mSeqMidiFaderInverted[ui->mActiveTrack][data->route.knobFaderIdx];
                    lv_label_set_text(label, ui->mSeqMidiFaderInverted[ui->mActiveTrack][data->route.knobFaderIdx] ? "-" : "+");
                }
            }
        };
        lv_obj_add_event_cb(polBtn, polarityClickCb, LV_EVENT_CLICKED, actData);

        // Attenuation Slider Drag Callback
        auto sliderChangeCb = [](lv_event_t* e) {
            RouteActionData* data = (RouteActionData*)lv_event_get_user_data(e);
            UIManager* ui = data->ui;
            float normVal = lv_slider_get_value(data->slider) / 100.0f;
            
            if (data->route.isMatrix) {
                auto currentConns = ui->mEngine.mRoutingMatrix.getConnections(ui->mActiveTrack);
                float amt = 0.0f;
                for (const auto& conn : currentConns) {
                    if (conn.source == data->route.matrixSource &&
                        conn.destination == data->route.matrixDest &&
                        conn.destParamId == data->route.matrixDestParamId &&
                        conn.sourceTrack == data->route.matrixSourceTrack) {
                        amt = conn.amount;
                        break;
                    }
                }
                float sign = (amt >= 0.0f) ? 1.0f : -1.0f;
                float newAmt = sign * normVal;
                ui->mEngine.setRouting(ui->mActiveTrack, data->route.matrixSourceTrack,
                                       static_cast<int>(data->route.matrixSource), static_cast<int>(data->route.matrixDest),
                                       newAmt, data->route.matrixDestParamId);
            } else {
                if (data->route.type == 0) { // Knob
                    ui->mSeqMidiKnobValue[ui->mActiveTrack][data->route.knobFaderIdx] = normVal;
                    int paramId = ui->mSeqMidiKnobParam[ui->mActiveTrack][data->route.knobFaderIdx];
                    if (paramId >= 0) {
                        float finalVal = ui->mSeqMidiKnobInverted[ui->mActiveTrack][data->route.knobFaderIdx] ? (1.0f - normVal) : normVal;
                        ui->mEngine.setParameter(ui->mActiveTrack, paramId, finalVal);
                    }
                } else { // Fader
                    ui->mSeqMidiFaderValue[ui->mActiveTrack][data->route.knobFaderIdx] = normVal;
                    int paramId = ui->mSeqMidiFaderParam[ui->mActiveTrack][data->route.knobFaderIdx];
                    if (paramId >= 0) {
                        float finalVal = ui->mSeqMidiFaderInverted[ui->mActiveTrack][data->route.knobFaderIdx] ? (1.0f - normVal) : normVal;
                        ui->mEngine.setParameter(ui->mActiveTrack, paramId, finalVal);
                    }
                }
                ui->mNeedsScreenRebuild = true;
            }
        };
        lv_obj_add_event_cb(attSlider, sliderChangeCb, LV_EVENT_VALUE_CHANGED, actData);

        // Delete Row Click Callback
        auto deleteClickCb = [](lv_event_t* e) {
            RouteActionData* data = (RouteActionData*)lv_event_get_user_data(e);
            UIManager* ui = data->ui;
            if (data->route.isMatrix) {
                ui->mEngine.setRouting(ui->mActiveTrack, data->route.matrixSourceTrack,
                                       static_cast<int>(data->route.matrixSource), static_cast<int>(data->route.matrixDest),
                                       0.0f, data->route.matrixDestParamId);
            } else {
                if (data->route.type == 0) { // Knob
                    ui->mSeqMidiKnobParam[ui->mActiveTrack][data->route.knobFaderIdx] = -1;
                } else { // Fader
                    ui->mSeqMidiFaderParam[ui->mActiveTrack][data->route.knobFaderIdx] = -1;
                }
            }
            ui->rebuildActiveRoutings(ui->mActiveRoutingsContainer);
            ui->mNeedsScreenRebuild = true;
        };
        lv_obj_add_event_cb(delBtn, deleteClickCb, LV_EVENT_CLICKED, actData);

        // Auto-free action data when the row is deleted
        auto freeActionDataCb = [](lv_event_t* e) {
            RouteActionData* data = (RouteActionData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(row, freeActionDataCb, LV_EVENT_DELETE, actData);
    }
}

// =========================================================================
// --- Callbacks Implementation ---
// =========================================================================

void UIManager::addRouteBtnEventCb(lv_event_t* e) {
    struct AddRouteUIData {
        UIManager* ui;
        lv_obj_t* srcDd;
        lv_obj_t* amtSlider;
    };
    AddRouteUIData* data = (AddRouteUIData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    int srcVal = lv_dropdown_get_selected(data->srcDd);
    int amtPercent = lv_slider_get_value(data->amtSlider);
    float amount = amtPercent / 100.0f;

    if (srcVal == 0) {
        std::cout << "Invalid source for route" << std::endl;
        return;
    }

    // destTrack: ui->mModDestTrack
    // sourceTrack: ui->mActiveTrack
    // dest: ModDestination::Parameter = 5
    // destParamId: ui->mModDestParamId
    ui->mEngine.setRouting(ui->mModDestTrack, ui->mActiveTrack, srcVal, 5, amount, ui->mModDestParamId);
    ui->rebuildActiveRoutings(ui->mActiveRoutingsContainer);
}

void UIManager::deleteRouteBtnEventCb(lv_event_t* e) {
    DeleteRouteData* data = (DeleteRouteData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    ui->mEngine.setRouting(data->destTrack, data->sourceTrack, data->source, data->dest, 0.0f, data->destParamId);

    int srcIdx = data->source;
    if (srcIdx >= 10 && srcIdx <= 17) { // Macro 1-8
        int m = srcIdx - 10;
        for (int d = 0; d < 2; ++d) {
            if (ui->mMacroDestParamId[m][d] == data->destParamId && ui->mMacroDestTrack[m][d] == data->destTrack) {
                ui->mMacroDestParamId[m][d] = -1;
                if (ui->mMacroDestBtnLabel[m][d]) {
                    lv_label_set_text(ui->mMacroDestBtnLabel[m][d], "Dest");
                }
            }
        }
    } else if (srcIdx >= 2 && srcIdx <= 7) { // LFO 1-6
        int l = srcIdx - 2;
        ui->mLfoDestParamId[l] = -1;
        if (ui->mLfoDestBtnLabel[l]) {
            lv_label_set_text(ui->mLfoDestBtnLabel[l], "Destination");
        }
    }

    ui->rebuildActiveRoutings(ui->mActiveRoutingsContainer);
}

void UIManager::macroValueArcEventCb(lv_event_t* e) {
    struct MacroArcCallbackData {
        UIManager* ui;
        int macroIdx;
        int slot;
        lv_obj_t* valLbl;
    };
    MacroArcCallbackData* data = (MacroArcCallbackData*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int percent = lv_arc_get_value(arc);

    lv_label_set_text_fmt(data->valLbl, "%s%d%%", percent > 0 ? "+" : "", percent);
    
    int m = data->macroIdx;
    int d = data->slot;
    data->ui->mMacroDestAmount[m][d] = percent / 100.0f;

    if (data->ui->mMacroDestParamId[m][d] != -1) {
        int destTrack = data->ui->mMacroDestTrack[m][d];
        int paramId = data->ui->mMacroDestParamId[m][d];
        float amount = percent / 100.0f;
        data->ui->mEngine.setRouting(destTrack, data->ui->mActiveTrack, 10 + m, 5, amount, paramId);
        data->ui->rebuildActiveRoutings(data->ui->mActiveRoutingsContainer);
    }
}

void UIManager::macroDropdownEventCb(lv_event_t* e) {
    struct MacroDdCallbackData {
        UIManager* ui;
        int macroIdx;
        lv_obj_t* srcDd;
    };
    MacroDdCallbackData* data = (MacroDdCallbackData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;
    int m = data->macroIdx;
    int idx = lv_dropdown_get_selected(data->srcDd);

    int sourceType = 0;
    int sourceIndex = -1;

    if (idx == 0) {
        sourceType = 0;
        sourceIndex = -1;
    } else if (idx == 1) {
        sourceType = 1;
        sourceIndex = 0;
    } else if (idx >= 2 && idx <= 7) {
        sourceType = 3;
        sourceIndex = idx - 2;
    } else if (idx == 8) {
        sourceType = 4;
        sourceIndex = 0;
    } else if (idx == 9) {
        sourceType = 5;
        sourceIndex = 0;
    } else if (idx >= 10 && idx <= 17) {
        sourceType = 2;
        sourceIndex = idx - 10;
    }

    ui->mEngine.setMacroSource(m, sourceType, sourceIndex, ui->mActiveTrack);
}

void UIManager::macroSourceBtnEventCb(lv_event_t* e) {
    struct LearnBtnData {
        UIManager* ui;
        int macroIdx;
    };
    LearnBtnData* data = (LearnBtnData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;
    int m = data->macroIdx;

    ui->mActiveMacroLearnIdx = m;

    // Create the Learn Overlay Modal
    ui->mMacroLearnModal = lv_obj_create(ui->mMainScreen);
    lv_obj_set_size(ui->mMacroLearnModal, 500, 360);
    lv_obj_center(ui->mMacroLearnModal);
    lv_obj_set_style_bg_color(ui->mMacroLearnModal, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_border_color(ui->mMacroLearnModal, lv_color_hex(0xAA3333), 0); // Red border to indicate learning!
    lv_obj_set_style_border_width(ui->mMacroLearnModal, 2, 0);
    lv_obj_set_style_radius(ui->mMacroLearnModal, 16, 0);
    lv_obj_set_style_pad_all(ui->mMacroLearnModal, 20, 0);
    lv_obj_set_layout(ui->mMacroLearnModal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->mMacroLearnModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->mMacroLearnModal, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title = lv_label_create(ui->mMacroLearnModal);
    lv_label_set_text_fmt(title, "MACRO %d SOURCE LEARN ACTIVE", m + 1);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);

    lv_obj_t* desc = lv_label_create(ui->mMacroLearnModal);
    lv_label_set_text(desc, "Move any synthesizer knob, slider, or pad to bind it as source,\nor select a modulator source below:");
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

    // Grid for static modulators
    lv_obj_t* grid = lv_obj_create(ui->mMacroLearnModal);
    lv_obj_set_size(grid, 460, 160);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);

    struct StaticSourceInfo {
        const char* name;
        int type;
        int idx;
    };
    std::vector<StaticSourceInfo> sources = {
        {"Track Out", 1, 0},
        {"LFO 1", 3, 0}, {"LFO 2", 3, 1}, {"LFO 3", 3, 2},
        {"LFO 4", 3, 3}, {"LFO 5", 3, 4}, {"LFO 6", 3, 5},
        {"Envelope", 4, 0}, {"Sidechain", 5, 0}
    };

    struct StaticClickData {
        UIManager* ui;
        int macroIdx;
        int type;
        int idx;
    };

    for (const auto& src : sources) {
        lv_obj_t* btn = lv_button_create(grid);
        lv_obj_set_size(btn, 90, 32);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, src.name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_center(lbl);

        StaticClickData* clickData = new StaticClickData{ui, m, src.type, src.idx};
        lv_obj_add_event_cb(btn, macroStaticSourceSelectEventCb, LV_EVENT_CLICKED, clickData);
        auto clickDataFree = [](lv_event_t* e) { delete (StaticClickData*)lv_event_get_user_data(e); };
        lv_obj_add_event_cb(btn, clickDataFree, LV_EVENT_DELETE, clickData);
    }

    // Cancel Button
    lv_obj_t* cancelBtn = lv_button_create(ui->mMacroLearnModal);
    lv_obj_set_size(cancelBtn, 120, 36);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "Cancel Learn");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);

    struct CancelData {
        UIManager* ui;
    };
    CancelData* cData = new CancelData{ui};
    auto cancelCb = [](lv_event_t* e) {
        CancelData* d = (CancelData*)lv_event_get_user_data(e);
        d->ui->mActiveMacroLearnIdx = -1;
        if (d->ui->mMacroLearnModal) {
            lv_obj_delete_async(d->ui->mMacroLearnModal);
            d->ui->mMacroLearnModal = nullptr;
        }
    };
    auto cancelFree = [](lv_event_t* e) { delete (CancelData*)lv_event_get_user_data(e); };
    lv_obj_add_event_cb(cancelBtn, cancelCb, LV_EVENT_CLICKED, cData);
    lv_obj_add_event_cb(cancelBtn, cancelFree, LV_EVENT_DELETE, cData);
}

void UIManager::macroStaticSourceSelectEventCb(lv_event_t* e) {
    struct StaticClickData {
        UIManager* ui;
        int macroIdx;
        int type;
        int idx;
    };
    StaticClickData* data = (StaticClickData*)lv_event_get_user_data(e);
    data->ui->assignMacroSourceStatic(data->macroIdx, data->type, data->idx);

    data->ui->mActiveMacroLearnIdx = -1;
    if (data->ui->mMacroLearnModal) {
        lv_obj_delete_async(data->ui->mMacroLearnModal);
        data->ui->mMacroLearnModal = nullptr;
    }
}

void UIManager::assignMacroSourceLearned(int macroIdx, int paramId) {
    mEngine.setMacroSource(macroIdx, 2, paramId, mActiveTrack);

    mActiveMacroLearnIdx = -1;
    if (mMacroLearnModal) {
        lv_obj_delete_async(mMacroLearnModal);
        mMacroLearnModal = nullptr;
    }
    
    populateAssignScreen();
}

void UIManager::assignMacroSourceStatic(int macroIdx, int sourceType, int sourceIndex) {
    mEngine.setMacroSource(macroIdx, sourceType, sourceIndex, mActiveTrack);
    populateAssignScreen();
}

void UIManager::lfoShapeDdEventCb(lv_event_t* e) {
    struct LfoShapeCallbackData {
        UIManager* ui;
        int lfoIdx;
    };
    LfoShapeCallbackData* data = (LfoShapeCallbackData*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int shape = lv_dropdown_get_selected(dd);

    data->ui->mEngine.setGenericLfoParam(data->lfoIdx, 2, (float)shape);
}

void UIManager::lfoDepthArcEventCb(lv_event_t* e) {
    struct LfoCallbackData {
        UIManager* ui;
        int lfoIdx;
        lv_obj_t* valLbl;
        lv_obj_t* syncBtn;
        lv_obj_t* syncValLbl;
    };
    LfoCallbackData* data = (LfoCallbackData*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int val = lv_arc_get_value(arc);

    lv_label_set_text_fmt(data->valLbl, "%d%%", val);
    data->ui->mEngine.setGenericLfoParam(data->lfoIdx, 1, val / 100.0f);

    int l = data->lfoIdx;
    if (data->ui->mLfoDestParamId[l] != -1) {
        int destTrack = data->ui->mLfoDestTrack[l];
        int paramId = data->ui->mLfoDestParamId[l];
        float depth = val / 100.0f;
        data->ui->mEngine.setRouting(destTrack, data->ui->mActiveTrack, 2 + l, 5, depth, paramId);
        data->ui->rebuildActiveRoutings(data->ui->mActiveRoutingsContainer);
    }
}

void UIManager::lfoRateArcEventCb(lv_event_t* e) {
    struct LfoCallbackData {
        UIManager* ui;
        int lfoIdx;
        lv_obj_t* valLbl;
        lv_obj_t* syncBtn;
        lv_obj_t* syncValLbl;
    };
    LfoCallbackData* data = (LfoCallbackData*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int val = lv_arc_get_value(arc);

    float raw = val / 100.0f;
    bool sync = lv_obj_has_state(data->syncBtn, LV_STATE_CHECKED);

    if (sync) {
        int syncIdx = (int)(raw * 18.99f);
        lv_label_set_text(data->valLbl, getLfoSyncLabel(syncIdx));
    } else {
        float hz = 0.01f * powf(10.0f, raw * 3.47712f);
        if (hz < 1.0f) {
            lv_label_set_text_fmt(data->valLbl, "%.2fHz", hz);
        } else {
            lv_label_set_text_fmt(data->valLbl, "%.1fHz", hz);
        }
    }

    data->ui->mEngine.setGenericLfoParam(data->lfoIdx, 0, raw);
}

void UIManager::lfoSyncBtnEventCb(lv_event_t* e) {
    struct LfoSyncCallbackData {
        UIManager* ui;
        int lfoIdx;
        lv_obj_t* syncBtn;
        lv_obj_t* syncLbl;
        lv_obj_t* rateArc;
        lv_obj_t* rateValLbl;
        lv_color_t trackColor;
    };
    LfoSyncCallbackData* data = (LfoSyncCallbackData*)lv_event_get_user_data(e);
    bool sync = lv_obj_has_state(data->syncBtn, LV_STATE_CHECKED);

    if (sync) {
        lv_label_set_text(data->syncLbl, "SYNC");
        lv_obj_set_style_bg_color(data->syncBtn, data->trackColor, 0);
    } else {
        lv_label_set_text(data->syncLbl, "FREE");
        lv_obj_set_style_bg_color(data->syncBtn, lv_color_hex(0x444444), 0);
    }

    // Write Sync State
    data->ui->mEngine.setGenericLfoParam(data->lfoIdx, 3, sync ? 1.0f : 0.0f);

    // Force value refresh on Rate text
    int rawVal = lv_arc_get_value(data->rateArc);
    float raw = rawVal / 100.0f;
    if (sync) {
        int syncIdx = (int)(raw * 18.99f);
        lv_label_set_text(data->rateValLbl, getLfoSyncLabel(syncIdx));
    } else {
        float hz = 0.01f * powf(10.0f, raw * 3.47712f);
        if (hz < 1.0f) {
            lv_label_set_text_fmt(data->rateValLbl, "%.2fHz", hz);
        } else {
            lv_label_set_text_fmt(data->rateValLbl, "%.1fHz", hz);
        }
    }
}

void UIManager::fxChainDdEventCb(lv_event_t* e) {
    struct FxChainCallbackData {
        UIManager* ui;
        int sourceFx;
    };
    FxChainCallbackData* data = (FxChainCallbackData*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int destIndex = lv_dropdown_get_selected(dd);

    // Option index 0 = Master Mix (maps to -1), 1 = Overdrive (maps to 0), etc.
    int destFx = destIndex - 1;
    data->ui->mEngine.setFxChain(data->sourceFx, destFx);
}

void UIManager::physicalControlEventCb(lv_event_t* e) {
    lv_obj_t* widget = (lv_obj_t*)lv_event_get_target(e);
    int val = lv_slider_get_value(widget); // works for both slider and arc

    // Determine target based on callback struct
    struct GenericControlData {
        UIManager* ui;
        int idx;
        lv_obj_t* valLbl;
    };
    GenericControlData* data = (GenericControlData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    lv_label_set_text_fmt(data->valLbl, "%d%%", val);

    float floatVal = val / 100.0f;

    bool isFader = (lv_obj_get_class(widget) == &lv_slider_class);

    if (isFader) {
        int faderIdx = data->idx;
        ui->mSeqMidiFaderValue[ui->mActiveTrack][faderIdx] = floatVal;
        
        // Fader is mapped to Track Volume by default
        int paramId = ui->mSeqMidiFaderParam[ui->mActiveTrack][faderIdx];
        ui->mEngine.setParameter(faderIdx, paramId, floatVal); // fader index corresponds to Track volumes by default
    } else {
        int knobIdx = data->idx;
        ui->mSeqMidiKnobValue[ui->mActiveTrack][knobIdx] = floatVal;

        int paramId = ui->mSeqMidiKnobParam[ui->mActiveTrack][knobIdx];
        ui->mEngine.setParameter(ui->mActiveTrack, paramId, floatVal);
    }
}

// Helper callback for selecting param buttons inside Remap Modal
void UIManager::paramSelectorClickEventCb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    struct ParamBtnData {
        UIManager* ui;
        int paramId;
        lv_obj_t* container;
    };
    ParamBtnData* data = (ParamBtnData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;
    ui->mRemapSelectedParamId = data->paramId;

    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    uint32_t count = lv_obj_get_child_count(data->container);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(data->container, i);
        if (child == btn) {
            lv_obj_set_style_bg_color(child, trackColor, 0);
            lv_obj_set_style_border_color(child, trackColor, 0);
        } else {
            lv_obj_set_style_bg_color(child, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_color(child, lv_color_hex(0x444444), 0);
        }
    }
}

// =========================================================================
// --- Remapping Modal Popup ---
// =========================================================================

void UIManager::openRemapModalEventCb(lv_event_t* e) {
    struct RemapEventData {
        UIManager* ui;
        int targetIdx; // 0-7 knobs, 8-15 faders
    };
    RemapEventData* data = (RemapEventData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    ui->mRemapTargetIndex = data->targetIdx;

    if (ui->mControllerSetupActive) {
        ui->mControllerSetupTargetIndex = data->targetIdx;
        if (ui->mControllerSetupBtnLabel) {
            bool isKnob = (data->targetIdx < 12);
            int idx = isKnob ? data->targetIdx : (data->targetIdx - 12);
            lv_label_set_text_fmt(ui->mControllerSetupBtnLabel, "WIGGLE HARDWARE CONTROLLER TO ASSIGN TO %s %d", 
                                  isKnob ? "KNOB" : "SLIDER", idx + 1);
        }
        return;
    }

    // Renders custom remapping modal
    ui->mRemapModal = lv_obj_create(ui->mMainScreen);
    lv_obj_set_size(ui->mRemapModal, 420, 460);
    lv_obj_center(ui->mRemapModal);
    lv_obj_set_style_bg_color(ui->mRemapModal, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_border_color(ui->mRemapModal, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_border_width(ui->mRemapModal, 2, 0);
    lv_obj_set_style_radius(ui->mRemapModal, 16, 0);
    lv_obj_set_style_pad_all(ui->mRemapModal, 15, 0);
    lv_obj_set_layout(ui->mRemapModal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->mRemapModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->mRemapModal, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    lv_obj_t* title = lv_label_create(ui->mRemapModal);
    if (ui->mRemapTargetIndex < 12) {
        lv_label_set_text_fmt(title, "REMAP KNOB %d", ui->mRemapTargetIndex + 1);
    } else {
        lv_label_set_text_fmt(title, "REMAP FADER %d", ui->mRemapTargetIndex - 11);
    }
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

    // CC number input spinner row
    lv_obj_t* ccRow = lv_obj_create(ui->mRemapModal);
    lv_obj_set_size(ccRow, 380, 45);
    lv_obj_set_style_bg_opa(ccRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ccRow, 0, 0);
    lv_obj_set_style_pad_all(ccRow, 0, 0);
    lv_obj_set_layout(ccRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ccRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ccRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ccLbl = lv_label_create(ccRow);
    lv_label_set_text(ccLbl, "MIDI CC (0-127)");
    lv_obj_set_style_text_font(ccLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ccLbl, lv_color_hex(0x888888), 0);

    // Control container for decrement/slider/increment controls
    lv_obj_t* ccCtrlCont = lv_obj_create(ccRow);
    lv_obj_set_size(ccCtrlCont, 220, 36);
    lv_obj_set_style_bg_opa(ccCtrlCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ccCtrlCont, 0, 0);
    lv_obj_set_style_pad_all(ccCtrlCont, 0, 0);
    lv_obj_set_layout(ccCtrlCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ccCtrlCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ccCtrlCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ccCtrlCont, 6, 0);

    // Decrement Button [-]
    lv_obj_t* decBtn = lv_button_create(ccCtrlCont);
    lv_obj_set_size(decBtn, 28, 28);
    lv_obj_set_style_bg_color(decBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(decBtn, 4, 0);
    lv_obj_t* decLbl = lv_label_create(decBtn);
    lv_label_set_text(decLbl, "-");
    lv_obj_set_style_text_font(decLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(decLbl);

    // Slider representing CC number spinner (inside ccCtrlCont)
    ui->mRemapCcSpinner = lv_slider_create(ccCtrlCont);
    lv_obj_set_size(ui->mRemapCcSpinner, 90, 10);
    lv_slider_set_range(ui->mRemapCcSpinner, 0, 127);
    
    int currentCc = (ui->mRemapTargetIndex < 12) ? ui->mSeqMidiKnobCC[ui->mActiveTrack][ui->mRemapTargetIndex]
                                               : ui->mSeqMidiFaderCC[ui->mActiveTrack][ui->mRemapTargetIndex - 12];
    lv_slider_set_value(ui->mRemapCcSpinner, currentCc, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->mRemapCcSpinner, trackColor, LV_PART_INDICATOR);

    // Increment Button [+]
    lv_obj_t* incBtn = lv_button_create(ccCtrlCont);
    lv_obj_set_size(incBtn, 28, 28);
    lv_obj_set_style_bg_color(incBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(incBtn, 4, 0);
    lv_obj_t* incLbl = lv_label_create(incBtn);
    lv_label_set_text(incLbl, "+");
    lv_obj_set_style_text_font(incLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(incLbl);

    lv_obj_t* ccValLbl = lv_label_create(ccCtrlCont);
    lv_label_set_text_fmt(ccValLbl, "%d", currentCc);
    lv_obj_set_style_text_font(ccValLbl, &lv_font_montserrat_12, 0);

    auto spinnerCb = [](lv_event_t* e) {
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* valLbl = (lv_obj_t*)lv_event_get_user_data(e);
        int val = lv_slider_get_value(slider);
        lv_label_set_text_fmt(valLbl, "%d", val);
    };
    lv_obj_add_event_cb(ui->mRemapCcSpinner, spinnerCb, LV_EVENT_VALUE_CHANGED, ccValLbl);

    struct CcBtnData {
        lv_obj_t* slider;
        lv_obj_t* valLbl;
        int delta;
    };
    CcBtnData* decData = new CcBtnData{ui->mRemapCcSpinner, ccValLbl, -1};
    CcBtnData* incData = new CcBtnData{ui->mRemapCcSpinner, ccValLbl, 1};

    auto btnClickCb = [](lv_event_t* e) {
        CcBtnData* d = (CcBtnData*)lv_event_get_user_data(e);
        int val = lv_slider_get_value(d->slider);
        val += d->delta;
        if (val < 0) val = 0;
        if (val > 127) val = 127;
        lv_slider_set_value(d->slider, val, LV_ANIM_OFF);
        lv_label_set_text_fmt(d->valLbl, "%d", val);
    };

    lv_obj_add_event_cb(decBtn, btnClickCb, LV_EVENT_CLICKED, decData);
    lv_obj_add_event_cb(incBtn, btnClickCb, LV_EVENT_CLICKED, incData);

    auto freeDecCb = [](lv_event_t* e) {
        CcBtnData* d = (CcBtnData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(decBtn, freeDecCb, LV_EVENT_DELETE, decData);
    lv_obj_add_event_cb(incBtn, freeDecCb, LV_EVENT_DELETE, incData);

    // Mapped parameter scrolling list instead of dropdown
    lv_obj_t* paramTitleRow = lv_obj_create(ui->mRemapModal);
    lv_obj_set_size(paramTitleRow, 380, 24);
    lv_obj_set_style_bg_opa(paramTitleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(paramTitleRow, 0, 0);
    lv_obj_set_style_pad_all(paramTitleRow, 0, 0);
    lv_obj_t* paramTitleLbl = lv_label_create(paramTitleRow);
    lv_label_set_text(paramTitleLbl, "Assign Parameter");
    lv_obj_set_style_text_font(paramTitleLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(paramTitleLbl, lv_color_hex(0x888888), 0);

    lv_obj_t* listContainer = lv_obj_create(ui->mRemapModal);
    lv_obj_set_size(listContainer, 380, 200);
    lv_obj_set_style_bg_color(listContainer, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(listContainer, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(listContainer, 1, 0);
    lv_obj_set_style_radius(listContainer, 8, 0);
    lv_obj_set_style_pad_all(listContainer, 8, 0);
    lv_obj_set_layout(listContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(listContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(listContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(listContainer, 8, 0);
    lv_obj_set_style_pad_row(listContainer, 8, 0);
    lv_obj_add_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);

    int currentParam = (ui->mRemapTargetIndex < 12) ? ui->mSeqMidiKnobParam[ui->mActiveTrack][ui->mRemapTargetIndex]
                                                  : ui->mSeqMidiFaderParam[ui->mActiveTrack][ui->mRemapTargetIndex - 12];
    ui->mRemapSelectedParamId = currentParam;

    auto options = ui->getTrackParamOptions(ui->mActiveTrack);
    struct ParamBtnData {
        UIManager* ui;
        int paramId;
        lv_obj_t* container;
    };

    for (const auto& opt : options) {
        lv_obj_t* btn = lv_button_create(listContainer);
        lv_obj_set_size(btn, 110, 36);
        lv_obj_set_style_radius(btn, 6, 0);

        if (opt.first == currentParam) {
            lv_obj_set_style_bg_color(btn, trackColor, 0);
            lv_obj_set_style_border_color(btn, trackColor, 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
        }

        lv_obj_t* btnLbl = lv_label_create(btn);
        lv_label_set_text(btnLbl, opt.second.c_str());
        lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(btnLbl);

        ParamBtnData* btnData = new ParamBtnData{ui, opt.first, listContainer};
        lv_obj_add_event_cb(btn, paramSelectorClickEventCb, LV_EVENT_CLICKED, btnData);

        auto paramBtnDataFreeCb = [](lv_event_t* e) {
            ParamBtnData* data = (ParamBtnData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(btn, paramBtnDataFreeCb, LV_EVENT_DELETE, btnData);
    }

    // Save & Cancel button row
    lv_obj_t* btnRow = lv_obj_create(ui->mRemapModal);
    lv_obj_set_size(btnRow, 380, 45);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* cancelBtn = lv_button_create(btnRow);
    lv_obj_set_size(cancelBtn, 140, 36);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "CANCEL");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);
    lv_obj_add_event_cb(cancelBtn, closeRemapModalEventCb, LV_EVENT_CLICKED, ui);

    lv_obj_t* saveBtn = lv_button_create(btnRow);
    lv_obj_set_size(saveBtn, 140, 36);
    lv_obj_set_style_bg_color(saveBtn, trackColor, 0);
    lv_obj_set_style_radius(saveBtn, 8, 0);
    lv_obj_t* saveLbl = lv_label_create(saveBtn);
    lv_label_set_text(saveLbl, "SAVE");
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(saveLbl);
    lv_obj_add_event_cb(saveBtn, saveRemapModalEventCb, LV_EVENT_CLICKED, ui);
}

void UIManager::saveRemapModalEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    
    int newCc = lv_slider_get_value(ui->mRemapCcSpinner);
    int paramId = ui->mRemapSelectedParamId;

    if (ui->mRemapTargetIndex < 12) {
        ui->mSeqMidiKnobCC[ui->mActiveTrack][ui->mRemapTargetIndex] = newCc;
        ui->mSeqMidiKnobParam[ui->mActiveTrack][ui->mRemapTargetIndex] = paramId;
    } else {
        ui->mSeqMidiFaderCC[ui->mActiveTrack][ui->mRemapTargetIndex - 12] = newCc;
        ui->mSeqMidiFaderParam[ui->mActiveTrack][ui->mRemapTargetIndex - 12] = paramId;
    }

    // Refresh center screen
    ui->createCenterContentArea();

    // Close Modal
    ui->closeRemapModalEventCb(e);
}

void UIManager::closeRemapModalEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui && ui->mRemapModal) {
        lv_obj_delete(ui->mRemapModal);
        ui->mRemapModal = nullptr;
    }
}

// =========================================================================
// --- Modulation Destination Picker Modal ---
// =========================================================================

struct ToggleClickData {
    UIManager* ui;
    bool isFxMode;
    lv_obj_t* tracksBtn;
    lv_obj_t* fxBtn;
    lv_obj_t* leftCol;
    lv_obj_t* rightCol;
};

struct CategoryClickData {
    UIManager* ui;
    int categoryIdx;
    lv_obj_t* leftCol;
    lv_obj_t* rightCol;
};

void UIManager::populateModDestCategories(UIManager* ui, bool isFxMode, lv_obj_t* leftCol, lv_obj_t* rightCol, int initialCat) {
    lv_obj_clean(leftCol);
    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    int startCat = isFxMode ? 9 : 0;
    int endCat = isFxMode ? 26 : 7;

    for (int c = startCat; c <= endCat; ++c) {
        lv_obj_t* catBtn = lv_button_create(leftCol);
        lv_obj_set_size(catBtn, 140, 32);
        lv_obj_set_style_radius(catBtn, 6, 0);

        // Highlight active category
        bool isSelected = (initialCat >= 0) ? (c == initialCat) : (c == startCat);
        bool isAtDisabledTrack = (ui->mModDestModalCallerType == 3 && !isFxMode && c != ui->mActiveTrack);
        
        if (isSelected && !isAtDisabledTrack) {
            lv_obj_set_style_bg_color(catBtn, trackColor, 0);
        } else if (isAtDisabledTrack) {
            lv_obj_add_state(catBtn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(catBtn, lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_border_color(catBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_width(catBtn, 1, 0);
            lv_obj_remove_flag(catBtn, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_set_style_bg_color(catBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_color(catBtn, lv_color_hex(0x444444), 0);
            lv_obj_set_style_border_width(catBtn, 1, 0);
        }

        lv_obj_t* catLbl = lv_label_create(catBtn);
        if (c < 8) {
            lv_label_set_text_fmt(catLbl, "TRACK %d", c + 1);
        } else {
            switch (c) {
                case 9:  lv_label_set_text(catLbl, "REVERB"); break;
                case 10: lv_label_set_text(catLbl, "DELAY"); break;
                case 11: lv_label_set_text(catLbl, "CHORUS"); break;
                case 12: lv_label_set_text(catLbl, "PHASER"); break;
                case 13: lv_label_set_text(catLbl, "OVERDRIVE"); break;
                case 14: lv_label_set_text(catLbl, "BITCRUSHER"); break;
                case 15: lv_label_set_text(catLbl, "COMPRESSOR"); break;
                case 16: lv_label_set_text(catLbl, "FLANGER"); break;
                case 17: lv_label_set_text(catLbl, "TAPE ECHO"); break;
                case 18: lv_label_set_text(catLbl, "TAPE WOBBLE"); break;
                case 19: lv_label_set_text(catLbl, "SLICER"); break;
                case 20: lv_label_set_text(catLbl, "LP LFO"); break;
                case 21: lv_label_set_text(catLbl, "HP LFO"); break;
                case 22: lv_label_set_text(catLbl, "FILTER 1"); break;
                case 23: lv_label_set_text(catLbl, "FILTER 2"); break;
                case 24: lv_label_set_text(catLbl, "FILTER 3"); break;
                case 25: lv_label_set_text(catLbl, "OCTAVER"); break;
                case 26: lv_label_set_text(catLbl, "EQ"); break;
                default: lv_label_set_text(catLbl, "FX"); break;
            }
        }
        lv_obj_set_style_text_font(catLbl, &lv_font_montserrat_10, 0);
        if (isAtDisabledTrack) {
            lv_obj_set_style_text_color(catLbl, lv_color_hex(0x555555), 0);
        }
        lv_obj_center(catLbl);

        CategoryClickData* cData = new CategoryClickData{ui, c, leftCol, rightCol};
        lv_obj_add_event_cb(catBtn, UIManager::modDestCategoryClickEventCb, LV_EVENT_CLICKED, cData);

        auto cFreeCb = [](lv_event_t* e) {
            CategoryClickData* d = (CategoryClickData*)lv_event_get_user_data(e);
            delete d;
        };
        lv_obj_add_event_cb(catBtn, cFreeCb, LV_EVENT_DELETE, cData);
    }
}

void UIManager::modDestToggleClickEventCb(lv_event_t* e) {
    ToggleClickData* data = (ToggleClickData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;
    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    // Update active highlight style for toggle buttons
    if (data->isFxMode) {
        lv_obj_set_style_bg_color(data->fxBtn, trackColor, 0);
        lv_obj_set_style_border_width(data->fxBtn, 0, 0);

        lv_obj_set_style_bg_color(data->tracksBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(data->tracksBtn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(data->tracksBtn, 1, 0);
    } else {
        lv_obj_set_style_bg_color(data->tracksBtn, trackColor, 0);
        lv_obj_set_style_border_width(data->tracksBtn, 0, 0);

        lv_obj_set_style_bg_color(data->fxBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(data->fxBtn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(data->fxBtn, 1, 0);
    }

    // Rebuild left column categories
    populateModDestCategories(ui, data->isFxMode, data->leftCol, data->rightCol);

    // Rebuild right column parameters (defaulting to the first item in the new category list)
    int firstCat = data->isFxMode ? 9 : 0;
    populateModDestParams(ui, firstCat, data->rightCol);
}

void UIManager::populateModDestParams(UIManager* ui, int categoryIdx, lv_obj_t* rightCol) {
    lv_obj_clean(rightCol);
    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    struct ParamClickData {
        UIManager* ui;
        int trackIdx;
        int paramId;
    };

    if (categoryIdx < 8) {
        // Track Parameters
        auto params = ui->getTrackParamOptions(categoryIdx);
        
        // Append 17 Sends (2000 + i * 10) for track sends
        const char* FX_NAMES[17] = {
            "Overdrive", "Bitcrusher", "Chorus", "Phaser", "Tape Wobble",
            "Delay", "Reverb", "Slicer", "Compressor", "HP LFO", "LP LFO",
            "Flanger", "Filter 1", "Tape Echo", "Octaver", "Filter 2", "Filter 3"
        };
        for (int i = 0; i < 17; ++i) {
            int sendParamId = 2000 + i * 10;
            std::string sendName = std::string(FX_NAMES[i]) + " Send";
            params.push_back({sendParamId, sendName});
        }

        for (const auto& p : params) {
            lv_obj_t* pBtn = lv_button_create(rightCol);
            lv_obj_set_size(pBtn, 110, 36);
            lv_obj_set_style_radius(pBtn, 6, 0);

            bool isSelected = (ui->mModDestType == 5 && ui->mModDestTrack == categoryIdx && ui->mModDestParamId == p.first);
            if (isSelected) {
                lv_obj_set_style_bg_color(pBtn, trackColor, 0);
            } else {
                lv_obj_set_style_bg_color(pBtn, lv_color_hex(0x2D2D2D), 0);
                lv_obj_set_style_border_color(pBtn, lv_color_hex(0x444444), 0);
                lv_obj_set_style_border_width(pBtn, 1, 0);
            }

            lv_obj_t* pLbl = lv_label_create(pBtn);
            lv_label_set_text(pLbl, p.second.c_str());
            lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(pLbl);

            ParamClickData* pData = new ParamClickData{ui, categoryIdx, p.first};
            lv_obj_add_event_cb(pBtn, UIManager::modDestParamClickEventCb, LV_EVENT_CLICKED, pData);

            auto pFreeCb = [](lv_event_t* e) {
                ParamClickData* d = (ParamClickData*)lv_event_get_user_data(e);
                delete d;
            };
            lv_obj_add_event_cb(pBtn, pFreeCb, LV_EVENT_DELETE, pData);
        }
    } else {
        // Global FX Options filtered by category index
        std::vector<std::pair<int, std::string>> globalParams;
        if (categoryIdx == 9) { // Reverb
            globalParams = { {500, "Size"}, {501, "Damp"}, {504, "PreDelay"}, {506, "Tone"}, {502, "Mod Depth"}, {503, "Mix"} };
        } else if (categoryIdx == 10) { // Delay
            globalParams = { {520, "Time"}, {521, "Feedback"}, {523, "Cutoff"}, {524, "Filt Res"}, {522, "Mix"} };
        } else if (categoryIdx == 11) { // Chorus
            globalParams = { {510, "Rate"}, {511, "Depth"}, {513, "Voices"}, {512, "Mix"} };
        } else if (categoryIdx == 12) { // Phaser
            globalParams = { {550, "Rate"}, {551, "Depth"}, {553, "Feedback"}, {552, "Mix"} };
        } else if (categoryIdx == 13) { // Overdrive
            globalParams = { {540, "Drive"}, {541, "Dist"}, {543, "Tone"}, {542, "Level"} };
        } else if (categoryIdx == 14) { // Bitcrusher
            globalParams = { {530, "Bits"}, {531, "Rate"}, {532, "Mix"} };
        } else if (categoryIdx == 15) { // Compressor
            globalParams = { {580, "Thresh"}, {581, "Ratio"}, {582, "Attack"}, {583, "Release"}, {584, "Makeup"}, {586, "SC Drum"} };
        } else if (categoryIdx == 16) { // Flanger
            globalParams = { {1500, "Rate"}, {1501, "Depth"}, {1503, "Feedback"}, {1504, "Delay"}, {1502, "Mix"} };
        } else if (categoryIdx == 17) { // Tape Echo
            globalParams = { {1510, "Time"}, {1511, "Feedback"}, {1513, "Drive"}, {1514, "Wow"}, {1515, "Flutter"}, {1512, "Mix"} };
        } else if (categoryIdx == 18) { // Tape Wobble
            globalParams = { {1520, "Rate"}, {1521, "Depth"}, {1522, "Mix"} };
        } else if (categoryIdx == 19) { // Slicer
            globalParams = { {570, "Rate 1"}, {571, "Rate 2"}, {572, "Rate 3"}, {573, "Pattern"}, {574, "Mix"} };
        } else if (categoryIdx == 20) { // LP LFO
            globalParams = { {490, "Rate"}, {491, "Depth"}, {492, "Shape"}, {493, "Cutoff"}, {494, "Reson"} };
        } else if (categoryIdx == 21) { // HP LFO
            globalParams = { {1590, "Rate"}, {1591, "Depth"}, {1592, "Shape"}, {1593, "Cutoff"}, {1594, "Reson"} };
        } else if (categoryIdx == 22) { // Filter 1
            globalParams = { {2200, "Cutoff"}, {2201, "Reson"}, {2202, "Mode"} };
        } else if (categoryIdx == 23) { // Filter 2
            globalParams = { {2205, "Cutoff"}, {2206, "Reson"}, {2207, "Mode"} };
        } else if (categoryIdx == 24) { // Filter 3
            globalParams = { {2210, "Cutoff"}, {2211, "Reson"}, {2212, "Mode"} };
        } else if (categoryIdx == 25) { // Octaver
            globalParams = { {1530, "Mix"}, {1531, "Oct1"}, {1532, "Oct2"} };
        } else if (categoryIdx == 26) { // EQ
            globalParams = { {1536, "Bass"}, {1537, "Mid"}, {1538, "Treble"}, {1539, "MidFreq"} };
        } else { // Fallback/All Global FX
            globalParams = {
                {500, "Reverb Size"}, {501, "Reverb Damp"}, {502, "Reverb Mod"}, {503, "Reverb Mix"},
                {510, "Chorus Rate"}, {511, "Chorus Depth"}, {512, "Chorus Mix"},
                {520, "Delay Time"},  {521, "Delay Feedbk"},
                {2200, "Filt1 Cutoff"}, {2201, "Filt1 Reson"}, {2202, "Filt1 Mode"},
                {2205, "Filt2 Cutoff"}, {2206, "Filt2 Reson"}, {2207, "Filt2 Mode"},
                {2210, "Filt3 Cutoff"}, {2211, "Filt3 Reson"}, {2212, "Filt3 Mode"}
            };
        }

        for (const auto& p : globalParams) {
            lv_obj_t* pBtn = lv_button_create(rightCol);
            lv_obj_set_size(pBtn, 110, 36);
            lv_obj_set_style_radius(pBtn, 6, 0);

            bool isSelected = (ui->mModDestParamId == p.first);
            if (isSelected) {
                lv_obj_set_style_bg_color(pBtn, trackColor, 0);
            } else {
                lv_obj_set_style_bg_color(pBtn, lv_color_hex(0x2D2D2D), 0);
                lv_obj_set_style_border_color(pBtn, lv_color_hex(0x444444), 0);
                lv_obj_set_style_border_width(pBtn, 1, 0);
            }

            lv_obj_t* pLbl = lv_label_create(pBtn);
            lv_label_set_text(pLbl, p.second.c_str());
            lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(pLbl);

            ParamClickData* pData = new ParamClickData{ui, 0, p.first};
            lv_obj_add_event_cb(pBtn, UIManager::modDestParamClickEventCb, LV_EVENT_CLICKED, pData);

            auto pFreeCb = [](lv_event_t* e) {
                ParamClickData* d = (ParamClickData*)lv_event_get_user_data(e);
                delete d;
            };
            lv_obj_add_event_cb(pBtn, pFreeCb, LV_EVENT_DELETE, pData);
        }
    }
}

void UIManager::openModDestModalEventCb(lv_event_t* e) {
    ModDestModalData* data = (ModDestModalData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    ui->mModDestModalCallerType = data->callerType;
    ui->mModDestModalCallerIdx = data->callerIdx;
    ui->mModDestModalCallerSlot = data->slot;

    if (data->callerType == 1) {
        ui->mModDestBtnLabel = ui->mMacroDestBtnLabel[data->callerIdx][data->slot];
        ui->mModDestTrack = ui->mMacroDestTrack[data->callerIdx][data->slot];
        ui->mModDestParamId = ui->mMacroDestParamId[data->callerIdx][data->slot];
    } else if (data->callerType == 2) {
        ui->mModDestBtnLabel = ui->mLfoDestBtnLabel[data->callerIdx];
        ui->mModDestTrack = ui->mLfoDestTrack[data->callerIdx];
        ui->mModDestParamId = ui->mLfoDestParamId[data->callerIdx];
    } else if (data->callerType == 3) {
        ui->mModDestBtnLabel = ui->mAftertouchDestBtnLabel[ui->mActiveTrack];
        ui->mModDestTrack = ui->mActiveTrack;
        ui->mModDestParamId = ui->mAftertouchDestParamId[ui->mActiveTrack];
    } else {
        ui->mModDestBtnLabel = nullptr;
    }

    ui->mModDestModal = lv_obj_create(ui->mMainScreen);
    lv_obj_set_size(ui->mModDestModal, 600, 440);
    lv_obj_center(ui->mModDestModal);
    lv_obj_set_style_bg_color(ui->mModDestModal, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_border_color(ui->mModDestModal, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_border_width(ui->mModDestModal, 2, 0);
    lv_obj_set_style_radius(ui->mModDestModal, 16, 0);
    lv_obj_set_style_pad_all(ui->mModDestModal, 15, 0);
    lv_obj_set_layout(ui->mModDestModal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->mModDestModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->mModDestModal, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    // Title
    lv_obj_t* title = lv_label_create(ui->mModDestModal);
    lv_label_set_text(title, "SELECT MODULATION DESTINATION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

    // Split Row
    lv_obj_t* splitRow = lv_obj_create(ui->mModDestModal);
    lv_obj_set_size(splitRow, 560, 310);
    lv_obj_set_style_bg_opa(splitRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(splitRow, 0, 0);
    lv_obj_set_style_pad_all(splitRow, 0, 0);
    lv_obj_set_layout(splitRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(splitRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(splitRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left Container (Holds Toggle Row + Left Col Category List)
    lv_obj_t* leftContainer = lv_obj_create(splitRow);
    lv_obj_set_size(leftContainer, 170, 300);
    lv_obj_set_style_bg_opa(leftContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftContainer, 0, 0);
    lv_obj_set_style_pad_all(leftContainer, 0, 0);
    lv_obj_set_layout(leftContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Toggle Row (Tracks / FX)
    lv_obj_t* toggleRow = lv_obj_create(leftContainer);
    lv_obj_set_size(toggleRow, 170, 30);
    lv_obj_set_style_bg_opa(toggleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(toggleRow, 0, 0);
    lv_obj_set_style_pad_all(toggleRow, 0, 0);
    lv_obj_set_layout(toggleRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(toggleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggleRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* tracksToggleBtn = lv_button_create(toggleRow);
    lv_obj_set_size(tracksToggleBtn, 82, 26);
    lv_obj_set_style_radius(tracksToggleBtn, 6, 0);
    lv_obj_t* tracksToggleLbl = lv_label_create(tracksToggleBtn);
    lv_label_set_text(tracksToggleLbl, "TRACKS");
    lv_obj_set_style_text_font(tracksToggleLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(tracksToggleLbl);

    lv_obj_t* fxToggleBtn = lv_button_create(toggleRow);
    lv_obj_set_size(fxToggleBtn, 82, 26);
    lv_obj_set_style_radius(fxToggleBtn, 6, 0);
    lv_obj_t* fxToggleLbl = lv_label_create(fxToggleBtn);
    lv_label_set_text(fxToggleLbl, "FX");
    lv_obj_set_style_text_font(fxToggleLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(fxToggleLbl);

    // Left Column: Category List (scrolling vertical column)
    lv_obj_t* leftCol = lv_obj_create(leftContainer);
    lv_obj_set_size(leftCol, 170, 265);
    lv_obj_set_style_bg_color(leftCol, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(leftCol, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(leftCol, 1, 0);
    lv_obj_set_style_radius(leftCol, 8, 0);
    lv_obj_set_style_pad_all(leftCol, 5, 0);
    lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(leftCol, 5, 0);
    lv_obj_add_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);

    // Right Column: Parameters scrolling grid
    lv_obj_t* rightCol = lv_obj_create(splitRow);
    lv_obj_set_size(rightCol, 370, 300);
    lv_obj_set_style_bg_color(rightCol, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(rightCol, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(rightCol, 1, 0);
    lv_obj_set_style_radius(rightCol, 8, 0);
    lv_obj_set_style_pad_all(rightCol, 8, 0);
    lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(rightCol, 8, 0);
    lv_obj_set_style_pad_row(rightCol, 8, 0);
    lv_obj_add_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);

    // Determine initial view mode based on current selection
    bool initialFxMode = ((ui->mModDestParamId >= 490 && ui->mModDestParamId < 600) || 
                          (ui->mModDestParamId >= 1500 && ui->mModDestParamId < 1600) || 
                          (ui->mModDestParamId >= 2200 && ui->mModDestParamId < 2215));

    // Highlight initial toggle buttons
    if (initialFxMode) {
        lv_obj_set_style_bg_color(fxToggleBtn, trackColor, 0);
        lv_obj_set_style_bg_color(tracksToggleBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(tracksToggleBtn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(tracksToggleBtn, 1, 0);
    } else {
        lv_obj_set_style_bg_color(tracksToggleBtn, trackColor, 0);
        lv_obj_set_style_bg_color(fxToggleBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(fxToggleBtn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(fxToggleBtn, 1, 0);
    }

    // Bind event callbacks to toggle buttons
    ToggleClickData* tTracksData = new ToggleClickData{ui, false, tracksToggleBtn, fxToggleBtn, leftCol, rightCol};
    lv_obj_add_event_cb(tracksToggleBtn, UIManager::modDestToggleClickEventCb, LV_EVENT_CLICKED, tTracksData);

    ToggleClickData* tFxData = new ToggleClickData{ui, true, tracksToggleBtn, fxToggleBtn, leftCol, rightCol};
    lv_obj_add_event_cb(fxToggleBtn, UIManager::modDestToggleClickEventCb, LV_EVENT_CLICKED, tFxData);

    auto tFreeCb = [](lv_event_t* e) {
        ToggleClickData* d = (ToggleClickData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(tracksToggleBtn, tFreeCb, LV_EVENT_DELETE, tTracksData);
    lv_obj_add_event_cb(fxToggleBtn, tFreeCb, LV_EVENT_DELETE, tFxData);

    // Initial Category and Parameter lists population
    int initialCat = 0;
    if (initialFxMode) {
        int pId = ui->mModDestParamId;
        if (pId >= 500 && pId < 508) initialCat = 9;       // Reverb
        else if (pId >= 520 && pId < 526) initialCat = 10;  // Delay
        else if (pId >= 510 && pId < 515) initialCat = 11;  // Chorus
        else if (pId >= 550 && pId < 555) initialCat = 12;  // Phaser
        else if (pId >= 540 && pId < 545) initialCat = 13;  // Overdrive
        else if (pId >= 530 && pId < 535) initialCat = 14;  // Bitcrusher
        else if (pId >= 580 && pId < 588) initialCat = 15;  // Compressor
        else if (pId >= 1500 && pId < 1505) initialCat = 16;// Flanger
        else if (pId >= 1510 && pId < 1517) initialCat = 17;// Tape Echo
        else if (pId >= 1520 && pId < 1525) initialCat = 18;// Tape Wobble
        else if (pId >= 570 && pId < 576) initialCat = 19;  // Slicer
        else if (pId >= 490 && pId < 496) initialCat = 20;  // LP LFO
        else if (pId >= 1590 && pId < 1596) initialCat = 21;// HP LFO
        else if (pId >= 2200 && pId < 2204) initialCat = 22;// Filter 1
        else if (pId >= 2205 && pId < 2209) initialCat = 23;// Filter 2
        else if (pId >= 2210 && pId < 2214) initialCat = 24;// Filter 3
        else if (pId >= 1530 && pId < 1534) initialCat = 25;// Octaver
        else if (pId >= 1535 && pId < 1540) initialCat = 26;// EQ
        else initialCat = 9;
    } else {
        initialCat = ui->mModDestTrack;
    }

    populateModDestCategories(ui, initialFxMode, leftCol, rightCol, initialCat);
    populateModDestParams(ui, initialCat, rightCol);

    // Cancel Button
    lv_obj_t* cancelBtn = lv_button_create(ui->mModDestModal);
    lv_obj_set_size(cancelBtn, 140, 36);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "CANCEL");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);
    lv_obj_add_event_cb(cancelBtn, closeModDestModalEventCb, LV_EVENT_CLICKED, ui);
}

void UIManager::modDestCategoryClickEventCb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    struct CategoryClickData {
        UIManager* ui;
        int categoryIdx;
        lv_obj_t* leftCol;
        lv_obj_t* rightCol;
    };
    CategoryClickData* data = (CategoryClickData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    // Update highlights in left column
    uint32_t count = lv_obj_get_child_count(data->leftCol);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(data->leftCol, i);
        if (child == btn) {
            lv_obj_set_style_bg_color(child, trackColor, 0);
        } else {
            lv_obj_set_style_bg_color(child, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_color(child, lv_color_hex(0x444444), 0);
        }
    }

    // Populate right column options
    populateModDestParams(ui, data->categoryIdx, data->rightCol);
}

void UIManager::modDestParamClickEventCb(lv_event_t* e) {
    struct ParamClickData {
        UIManager* ui;
        int trackIdx;
        int paramId;
    };
    ParamClickData* data = (ParamClickData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    ui->mModDestTrack = data->trackIdx;
    ui->mModDestParamId = data->paramId;
    bool isFx = ((data->paramId >= 490 && data->paramId < 600) || 
                 (data->paramId >= 1500 && data->paramId < 1600) || 
                 (data->paramId >= 2200 && data->paramId < 2215));
    ui->mModDestType = isFx ? 6 : 5;

    // Update parent page label and DSP Routing
    std::string currentDestName = getParameterNameString(ui->mModDestTrack, ui->mModDestParamId, &(ui->mEngine));
    if (ui->mModDestBtnLabel) {
        if (ui->mModDestModalCallerType == 3) {
            lv_label_set_text_fmt(ui->mModDestBtnLabel, "AFTERTOUCH DEST: %s (TAP TO CHANGE)", currentDestName.c_str());
        } else if (ui->mModDestModalCallerType == 1) {
            std::string compactName = getCompactDestName(ui->mModDestTrack, ui->mModDestParamId, &(ui->mEngine));
            lv_label_set_text(ui->mModDestBtnLabel, compactName.c_str());
        } else {
            lv_label_set_text(ui->mModDestBtnLabel, currentDestName.c_str());
        }
    }

    if (ui->mModDestModalCallerType == 1) {
        int m = ui->mModDestModalCallerIdx;
        int d = ui->mModDestModalCallerSlot;
        ui->mMacroDestParamId[m][d] = data->paramId;
        ui->mMacroDestTrack[m][d] = data->trackIdx;
        ui->mMacroDestType[m][d] = 5;
        int percent = lv_arc_get_value(ui->mMacroArc[m][d]);
        float amount = percent / 100.0f;
        ui->mEngine.setRouting(data->trackIdx, ui->mActiveTrack, 10 + m, 5, amount, data->paramId);
    } else if (ui->mModDestModalCallerType == 2) {
        int l = ui->mModDestModalCallerIdx;
        ui->mLfoDestParamId[l] = data->paramId;
        ui->mLfoDestTrack[l] = data->trackIdx;
        ui->mLfoDestType[l] = 5;
        float depth = ui->mEngine.mLfos[l].getDepth();
        ui->mEngine.setRouting(data->trackIdx, ui->mActiveTrack, 2 + l, 5, depth, data->paramId);
    } else if (ui->mModDestModalCallerType == 3) {
        ui->mAftertouchDestParamId[ui->mActiveTrack] = data->paramId;
        ui->mEngine.setRouting(data->trackIdx, ui->mActiveTrack, 27, 5, 1.0f, data->paramId);
    }

    ui->rebuildActiveRoutings(ui->mActiveRoutingsContainer);

    // Close Modal
    if (ui->mModDestModal) {
        lv_obj_delete(ui->mModDestModal);
        ui->mModDestModal = nullptr;
    }
}

void UIManager::closeModDestModalEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui && ui->mModDestModal) {
        lv_obj_delete(ui->mModDestModal);
        ui->mModDestModal = nullptr;
    }
}

// =========================================================================
// --- FX Chain Pedal Picker Modal ---
// =========================================================================

void UIManager::pedalSlotClickEventCb(lv_event_t* e) {
    struct PedalSlotClickData {
        UIManager* ui;
        int chainIdx;
        int slotIdx;
    };
    PedalSlotClickData* psData = (PedalSlotClickData*)lv_event_get_user_data(e);
    UIManager* ui = psData->ui;
    int chainIdx = psData->chainIdx;
    int slotIdx = psData->slotIdx;

    ui->mSelectedChainIdx = chainIdx;
    ui->mSelectedSlotIdx = slotIdx;

    ui->mPedalPickerModal = lv_obj_create(ui->mMainScreen);
    lv_obj_set_size(ui->mPedalPickerModal, 700, 480);
    lv_obj_center(ui->mPedalPickerModal);
    lv_obj_set_style_bg_color(ui->mPedalPickerModal, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_border_color(ui->mPedalPickerModal, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_border_width(ui->mPedalPickerModal, 2, 0);
    lv_obj_set_style_radius(ui->mPedalPickerModal, 16, 0);
    lv_obj_set_style_pad_all(ui->mPedalPickerModal, 15, 0);
    lv_obj_set_layout(ui->mPedalPickerModal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->mPedalPickerModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui->mPedalPickerModal, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    // Title
    lv_obj_t* title = lv_label_create(ui->mPedalPickerModal);
    lv_label_set_text_fmt(title, "ASSIGN FX PEDAL (CHAIN %d, SLOT %d)", chainIdx + 1, slotIdx + 1);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

    // Scrolling Grid Container for Pedals
    lv_obj_t* gridContainer = lv_obj_create(ui->mPedalPickerModal);
    lv_obj_set_size(gridContainer, 640, 330);
    lv_obj_set_style_bg_color(gridContainer, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(gridContainer, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(gridContainer, 1, 0);
    lv_obj_set_style_radius(gridContainer, 8, 0);
    lv_obj_set_style_pad_all(gridContainer, 10, 0);
    lv_obj_set_layout(gridContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(gridContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(gridContainer, 10, 0);
    lv_obj_set_style_pad_row(gridContainer, 10, 0);
    lv_obj_add_flag(gridContainer, LV_OBJ_FLAG_SCROLLABLE);

    const char* FX_NAMES[17] = {
        "Overdrive", "Bitcrusher", "Chorus", "Phaser", "Tape Wobble",
        "Delay", "Reverb", "Slicer", "Compressor", "HP LFO Filter",
        "LP LFO Filter", "Flanger", "Filter Pedal 1", "Tape Echo", "Octaver",
        "Filter Pedal 2", "Filter Pedal 3"
    };

    // Calculate assigned pedals in either chain
    bool assigned[17] = {false};
    for (int c = 0; c < 2; ++c) {
        for (int s = 0; s < 5; ++s) {
            if (c == chainIdx && s == slotIdx) continue;
            int pId = ui->mFxChainPedals[c][s];
            if (pId >= 0 && pId < 17) {
                assigned[pId] = true;
            }
        }
    }

    struct PedalBtnData {
        UIManager* ui;
        int pedalId;
    };

    for (int p = 0; p < 17; ++p) {
        lv_obj_t* pBtn = lv_button_create(gridContainer);
        lv_obj_set_size(pBtn, 140, 38);
        lv_obj_set_style_radius(pBtn, 6, 0);

        if (assigned[p]) {
            lv_obj_add_state(pBtn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(pBtn, lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_border_color(pBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_width(pBtn, 1, 0);
        } else {
            bool isCurrent = (ui->mFxChainPedals[chainIdx][slotIdx] == p);
            if (isCurrent) {
                lv_obj_set_style_bg_color(pBtn, trackColor, 0);
            } else {
                lv_obj_set_style_bg_color(pBtn, lv_color_hex(0x2D2D2D), 0);
                lv_obj_set_style_border_color(pBtn, lv_color_hex(0x444444), 0);
                lv_obj_set_style_border_width(pBtn, 1, 0);
            }
        }

        lv_obj_t* pLbl = lv_label_create(pBtn);
        lv_label_set_text(pLbl, FX_NAMES[p]);
        lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_10, 0);
        if (assigned[p]) {
            lv_obj_set_style_text_color(pLbl, lv_color_hex(0x555555), 0);
        } else {
            lv_obj_set_style_text_color(pLbl, lv_color_hex(0xFFFFFF), 0);
        }
        lv_obj_center(pLbl);

        if (!assigned[p]) {
            PedalBtnData* pData = new PedalBtnData{ui, p};
            lv_obj_add_event_cb(pBtn, assignPedalBtnEventCb, LV_EVENT_CLICKED, pData);

            auto pFreeCb = [](lv_event_t* e) {
                PedalBtnData* d = (PedalBtnData*)lv_event_get_user_data(e);
                delete d;
            };
            lv_obj_add_event_cb(pBtn, pFreeCb, LV_EVENT_DELETE, pData);
        }
    }

    // Bottom Action Buttons: CANCEL & REMOVE
    lv_obj_t* actionRow = lv_obj_create(ui->mPedalPickerModal);
    lv_obj_set_size(actionRow, 640, 45);
    lv_obj_set_style_bg_opa(actionRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionRow, 0, 0);
    lv_obj_set_style_pad_all(actionRow, 0, 0);
    lv_obj_set_layout(actionRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actionRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* cancelBtn = lv_button_create(actionRow);
    lv_obj_set_size(cancelBtn, 140, 36);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "CANCEL");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);
    
    auto closePickerCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        if (ui->mPedalPickerModal) {
            lv_obj_delete(ui->mPedalPickerModal);
            ui->mPedalPickerModal = nullptr;
        }
    };
    lv_obj_add_event_cb(cancelBtn, closePickerCb, LV_EVENT_CLICKED, ui);

    lv_obj_t* removeBtn = lv_button_create(actionRow);
    lv_obj_set_size(removeBtn, 140, 36);
    lv_obj_set_style_bg_color(removeBtn, lv_color_hex(0xAA3333), 0);
    lv_obj_set_style_radius(removeBtn, 8, 0);
    lv_obj_t* removeLbl = lv_label_create(removeBtn);
    lv_label_set_text(removeLbl, "REMOVE PEDAL");
    lv_obj_set_style_text_font(removeLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(removeLbl);
    lv_obj_add_event_cb(removeBtn, removePedalBtnEventCb, LV_EVENT_CLICKED, ui);
}

void UIManager::assignPedalBtnEventCb(lv_event_t* e) {
    struct PedalBtnData {
        UIManager* ui;
        int pedalId;
    };
    PedalBtnData* data = (PedalBtnData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    if (ui->mSelectedChainIdx >= 0 && ui->mSelectedChainIdx < 2 &&
        ui->mSelectedSlotIdx >= 0 && ui->mSelectedSlotIdx < 5) {
        ui->mFxChainPedals[ui->mSelectedChainIdx][ui->mSelectedSlotIdx] = data->pedalId;
        ui->updateAudioEngineFxChains();
    }

    // Refresh center screen
    ui->createCenterContentArea();

    // Close Modal
    if (ui->mPedalPickerModal) {
        lv_obj_delete(ui->mPedalPickerModal);
        ui->mPedalPickerModal = nullptr;
    }
}

void UIManager::removePedalBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);

    if (ui->mSelectedChainIdx >= 0 && ui->mSelectedChainIdx < 2 &&
        ui->mSelectedSlotIdx >= 0 && ui->mSelectedSlotIdx < 5) {
        ui->mFxChainPedals[ui->mSelectedChainIdx][ui->mSelectedSlotIdx] = -1;
        ui->updateAudioEngineFxChains();
    }

    // Refresh center screen
    ui->createCenterContentArea();

    // Close Modal
    if (ui->mPedalPickerModal) {
        lv_obj_delete(ui->mPedalPickerModal);
        ui->mPedalPickerModal = nullptr;
    }
}

std::vector<std::pair<int, std::string>> UIManager::getTrackParamOptions(int trackIdx) {
    int engineType = mEngine.getTracks()[trackIdx].engineType;
    std::vector<std::pair<int, std::string>> params = {
        {0, "Volume"}, {9, "Pan"}
    };

    if (engineType != 4 && engineType != 5 && engineType != 6) {
        params.push_back({1, "Cutoff"});
        params.push_back({2, "Resonance"});
    }

    if (engineType == 0 || engineType == 8 || engineType == 9) {
        params.push_back({100, "Attack"});
        params.push_back({101, "Decay"});
        params.push_back({102, "Sustain"});
        params.push_back({103, "Release"});
    }

    if (engineType == 0) { // Subtractive (31 synthesis parameters)
        params.push_back({1, "Cutoff"});
        params.push_back({2, "Resonance"});
        params.push_back({118, "Env Amount"});
        params.push_back({7, "LFO Rate"});
        params.push_back({8, "LFO Depth"});
        params.push_back({160, "Osc1 Pitch"});
        params.push_back({104, "Osc1 Morph"});
        params.push_back({170, "Osc1 Drive"});
        params.push_back({180, "Osc1 Fold"});
        params.push_back({107, "Osc1 Vol"});
        params.push_back({161, "Osc2 Pitch"});
        params.push_back({105, "Osc2 Morph"});
        params.push_back({171, "Osc2 Drive"});
        params.push_back({181, "Osc2 Fold"});
        params.push_back({108, "Osc2 Vol"});
        params.push_back({162, "Sub Pitch"});
        params.push_back({155, "Sub Morph"});
        params.push_back({172, "Sub Drive"});
        params.push_back({182, "Sub Fold"});
        params.push_back({109, "Sub Vol"});
        params.push_back({106, "Detune"});
        params.push_back({110, "Noise Vol"});
        params.push_back({355, "Glide"});
        params.push_back({100, "Amp A"});
        params.push_back({101, "Amp D"});
        params.push_back({102, "Amp S"});
        params.push_back({103, "Amp R"});
        params.push_back({114, "Filt A"});
        params.push_back({115, "Filt D"});
        params.push_back({116, "Filt S"});
        params.push_back({117, "Filt R"});
    } else if (engineType == 1) { // FM (52 parameters total)
        params.push_back({151, "Cutoff"});
        params.push_back({152, "Resonance"});
        params.push_back({118, "Env Amt"});
        params.push_back({150, "Algorithm"});
        params.push_back({154, "Feedback"});
        params.push_back({159, "Drive"});
        params.push_back({157, "Brightness"});
        params.push_back({355, "Glide"});
        params.push_back({196, "FM Preset"});
        
        // 6 Operators: 6 params each
        const char* OP_NAMES[6] = {"Op1", "Op2", "Op3", "Op4", "Op5", "Op6"};
        for (int op = 0; op < 6; ++op) {
            int base = 160 + op * 6;
            std::string opPrefix = OP_NAMES[op];
            params.push_back({base + 0, opPrefix + " Level"});
            params.push_back({base + 1, opPrefix + " Attack"});
            params.push_back({base + 2, opPrefix + " Decay"});
            params.push_back({base + 3, opPrefix + " Sustain"});
            params.push_back({base + 4, opPrefix + " Release"});
            params.push_back({base + 5, opPrefix + " Ratio"});
        }
    } else if (engineType == 2) { // Sampler (16 synthesis parameters)
        params.push_back({320, "Play Mode"});
        params.push_back({330, "Start Pnt"});
        params.push_back({331, "End Point"});
        params.push_back({302, "Speed"});
        params.push_back({300, "Pitch"});
        params.push_back({301, "Stretch"});
        params.push_back({360, "Scrub Position"});
        params.push_back({355, "Glide"});
        params.push_back({340, "Slices"});
        params.push_back({341, "Slice Select"});
        params.push_back({342, "Slice Lock"});
        params.push_back({310, "Amp A"});
        params.push_back({311, "Amp D"});
        params.push_back({312, "Amp S"});
        params.push_back({313, "Amp R"});
        params.push_back({314, "Env Amt"});
    } else if (engineType == 3) { // Granular (12 synthesis parameters)
        params.push_back({400, "Grain Size"});
        params.push_back({401, "Density"});
        params.push_back({402, "Jitter"});
        params.push_back({403, "Spread"});
        params.push_back({330, "Position"});
        params.push_back({355, "Glide"});
        params.push_back({425, "Amp A"});
        params.push_back({426, "Amp D"});
        params.push_back({427, "Amp S"});
        params.push_back({428, "Amp R"});
    } else if (engineType == 4) { // Wavetable (19 synthesis parameters)
        params.push_back({450, "Morph"});
        params.push_back({465, "Warp"});
        params.push_back({466, "Crush"});
        params.push_back({467, "Drive"});
        params.push_back({451, "Detune"});
        params.push_back({355, "Glide"});
        params.push_back({475, "Bitrate"});
        params.push_back({476, "Samplerate"});
        params.push_back({458, "Cutoff"});
        params.push_back({459, "Resonance"});
        params.push_back({464, "Env Amt"});
        params.push_back({454, "Amp A"});
        params.push_back({455, "Amp D"});
        params.push_back({456, "Amp S"});
        params.push_back({457, "Amp R"});
        params.push_back({471, "Filt A"});
        params.push_back({472, "Filt D"});
        params.push_back({473, "Filt S"});
        params.push_back({474, "Filt R"});
    } else if (engineType == 5) { // FM Drum (32 synthesis parameters)
        const char* DRUM_NAMES[8] = {"BD", "SD", "TOM", "CH", "OH", "CYMB", "PERC", "NOISE"};
        for (int i = 0; i < 8; ++i) {
            int base = 200 + i * 10;
            std::string dPrefix = DRUM_NAMES[i];
            params.push_back({base + 0, dPrefix + " Pitch"});
            params.push_back({base + 1, dPrefix + " Snap"});
            params.push_back({base + 2, dPrefix + " Decay"});
            params.push_back({base + 5, dPrefix + " Level"});
        }
    } else if (engineType == 6) { // Analogue Drum (22 synthesis parameters)
        // BD
        params.push_back({600, "BD Decay"});
        params.push_back({601, "BD Tone"});
        params.push_back({602, "BD Tune"});
        params.push_back({605, "BD Gain"});
        // SD
        params.push_back({610, "SD Decay"});
        params.push_back({613, "SD Snap"});
        params.push_back({612, "SD Tune"});
        params.push_back({615, "SD Gain"});
        // RIM
        params.push_back({620, "RIM Decay"});
        params.push_back({621, "RIM Col"});
        params.push_back({622, "RIM Tune"});
        params.push_back({625, "RIM Gain"});
        // HAT C
        params.push_back({630, "HAT C Decay"});
        params.push_back({631, "HAT C Col"});
        params.push_back({635, "HAT C Gain"});
        // HAT O
        params.push_back({640, "HAT O Decay"});
        params.push_back({641, "HAT O Col"});
        params.push_back({645, "HAT O Gain"});
        // CYMBAL
        params.push_back({653, "CYM Atk"});
        params.push_back({650, "CYM Decay"});
        params.push_back({651, "CYM Col"});
        params.push_back({655, "CYM Gain"});
    } else if (engineType == 9) { // SoundFont
        params.push_back({180, "SF Preset"});
        params.push_back({181, "SF Bank"});
        params.push_back({7, "LFO Rate"});
        params.push_back({8, "LFO Depth"});
    }
    return params;
}

void UIManager::updateAudioEngineFxChains() {
    // First, set all 18 FX destinations to -1 (Master Mix)
    for (int f = 0; f < 18; ++f) {
        mEngine.setFxChain(f, -1);
    }

    // Chain 1 serial routing
    std::vector<int> chain1;
    for (int i = 0; i < 5; ++i) {
        if (mFxChainPedals[0][i] >= 0 && mFxChainPedals[0][i] < 17) {
            chain1.push_back(mFxChainPedals[0][i]);
        }
    }
    for (size_t i = 0; i < chain1.size(); ++i) {
        if (i + 1 < chain1.size()) {
            mEngine.setFxChain(chain1[i], chain1[i+1]);
        } else {
            mEngine.setFxChain(chain1[i], -1); // last goes to Master
        }
    }

    // Chain 2 serial routing
    std::vector<int> chain2;
    for (int i = 0; i < 5; ++i) {
        if (mFxChainPedals[1][i] >= 0 && mFxChainPedals[1][i] < 17) {
            chain2.push_back(mFxChainPedals[1][i]);
        }
    }
    for (size_t i = 0; i < chain2.size(); ++i) {
        if (i + 1 < chain2.size()) {
            mEngine.setFxChain(chain2[i], chain2[i+1]);
        } else {
            mEngine.setFxChain(chain2[i], -1); // last goes to Master
        }
    }
}

// =========================================================================
// --- FX Screen ---
// =========================================================================

void UIManager::populateFxScreen() {
    lv_obj_t* tabview = lv_tabview_create(mCenterArea);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);
    
    // Set modern dark look for the tabview
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tabview, 0, 0);

    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "Gain & Dyn");
    lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "Modulation");
    lv_obj_t* tab3 = lv_tabview_add_tab(tabview, "Time & Space");
    lv_obj_t* tab4 = lv_tabview_add_tab(tabview, "Basic Filters");
    lv_obj_t* tab5 = lv_tabview_add_tab(tabview, "Mod Filters");
    lv_obj_t* tab6 = lv_tabview_add_tab(tabview, "Pitch & EQ");

    // Clear standard tab padding
    lv_obj_t* tabs[6] = {tab1, tab2, tab3, tab4, tab5, tab6};
    for (int i = 0; i < 6; ++i) {
        lv_obj_set_style_pad_all(tabs[i], 10, 0);
        lv_obj_remove_flag(tabs[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(tabs[i], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tabs[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tabs[i], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(tabs[i], 10, 0);
    }

    // Tab 1: Gain & Dyn
    {
        lv_obj_t* od = createPedalCard(tab1, "OVERDRIVE", lv_color_hex(0x22C55E)); // Green
        addPedalKnob(od, "Drive", 540, 0.0f, 1.0f);
        addPedalKnob(od, "Dist", 541, 0.0f, 1.0f);
        addPedalKnob(od, "Tone", 543, 0.0f, 1.0f);
        addPedalSpacer(od, 15);
        addPedalKnob(od, "Level", 542, 0.0f, 1.0f);
        addPedalKnob(od, "Send", 2000, 0.0f, 1.0f);

        lv_obj_t* bc = createPedalCard(tab1, "BITCRUSHER", lv_color_hex(0xEAB308)); // Yellow
        addPedalKnob(bc, "Bits", 530, 1.0f, 16.0f, 0);
        addPedalKnob(bc, "Rate", 531, 100.0f, 22000.0f, 0);
        addPedalKnob(bc, "Mix", 532, 0.0f, 1.0f);
        addPedalKnob(bc, "Send", 2010, 0.0f, 1.0f);

        lv_obj_t* comp = createPedalCard(tab1, "COMPRESSOR", lv_color_hex(0x3B82F6)); // Blue
        addPedalKnob(comp, "Thresh", 580, 0.0f, 1.0f);
        addPedalKnob(comp, "Ratio", 581, 1.0f, 20.0f, 1);
        addPedalKnob(comp, "Attack", 582, 0.001f, 0.2f);
        addPedalKnob(comp, "Release", 583, 0.01f, 1.0f);
        addPedalKnob(comp, "Makeup", 584, 0.0f, 2.0f);
        addPedalDropdown(comp, "SC Track", 585, "None\nTrack 1\nTrack 2\nTrack 3\nTrack 4\nTrack 5\nTrack 6\nTrack 7\nTrack 8");
        addPedalKnob(comp, "SC Drum", 586, 0.0f, 15.0f, 0);
        addPedalKnob(comp, "Send", 2080, 0.0f, 1.0f);
    }

    // Tab 2: Modulation
    {
        lv_obj_t* cho = createPedalCard(tab2, "CHORUS", lv_color_hex(0xA855F7)); // Purple
        addPedalKnob(cho, "Rate", 510, 0.1f, 10.0f);
        addPedalKnob(cho, "Depth", 511, 0.0f, 1.0f);
        addPedalKnob(cho, "Voices", 513, 1.0f, 8.0f, 0);
        addPedalSpacer(cho, 15);
        addPedalKnob(cho, "Mix", 512, 0.0f, 1.0f);
        addPedalKnob(cho, "Send", 2020, 0.0f, 1.0f);

        lv_obj_t* pha = createPedalCard(tab2, "PHASER", lv_color_hex(0xEC4899)); // Pink
        addPedalKnob(pha, "Rate", 550, 0.1f, 10.0f);
        addPedalKnob(pha, "Depth", 551, 0.0f, 1.0f);
        addPedalKnob(pha, "Feedback", 553, 0.0f, 0.95f);
        addPedalSpacer(pha, 15);
        addPedalKnob(pha, "Mix", 552, 0.0f, 1.0f);
        addPedalKnob(pha, "Send", 2030, 0.0f, 1.0f);

        lv_obj_t* fla = createPedalCard(tab2, "FLANGER", lv_color_hex(0x06B6D4)); // Cyan
        addPedalKnob(fla, "Rate", 1500, 0.05f, 5.0f);
        addPedalKnob(fla, "Depth", 1501, 0.0f, 1.0f);
        addPedalKnob(fla, "Feedback", 1503, 0.0f, 0.95f);
        addPedalSpacer(fla, 15);
        addPedalKnob(fla, "Delay", 1504, 0.0f, 1.0f);
        addPedalKnob(fla, "Mix", 1502, 0.0f, 1.0f);
        addPedalKnob(fla, "Send", 2110, 0.0f, 1.0f);
    }

    // Tab 3: Time & Space
    {
        lv_obj_t* echo = createPedalCard(tab3, "TAPE ECHO", lv_color_hex(0xF97316)); // Warm Orange
        addPedalKnob(echo, "Time", 1510, 0.01f, 2.0f);
        addPedalKnob(echo, "Feedback", 1511, 0.0f, 0.99f);
        addPedalKnob(echo, "Drive", 1513, 0.0f, 2.0f);
        addPedalKnob(echo, "Wow", 1514, 0.0f, 1.0f);
        addPedalKnob(echo, "Flutter", 1515, 0.0f, 1.0f);
        addPedalKnob(echo, "Mix", 1512, 0.0f, 1.0f);
        addPedalKnob(echo, "Send", 2130, 0.0f, 1.0f);

        lv_obj_t* del = createPedalCard(tab3, "DELAY", lv_color_hex(0xE0F2FE)); // Light Blue
        addPedalKnob(del, "Time", 520, 0.01f, 2.0f);
        addPedalKnob(del, "Feedback", 521, 0.0f, 0.99f);
        addPedalDropdown(del, "Type", 525, "Digital\nAnalog\nTape\nPingPong");
        addPedalKnob(del, "Cutoff", 523, 0.0f, 1.0f);
        addPedalKnob(del, "Filt Res", 524, 0.0f, 0.95f);
        addPedalDropdown(del, "Filt Mode", 526, "LP\nHP\nBP");
        addPedalKnob(del, "Mix", 522, 0.0f, 1.0f);
        addPedalKnob(del, "Send", 2050, 0.0f, 1.0f);

        lv_obj_t* rev = createPedalCard(tab3, "REVERB", lv_color_hex(0x6366F1)); // Indigo
        addPedalKnob(rev, "Size", 500, 0.1f, 0.99f);
        addPedalKnob(rev, "Damp", 501, 0.0f, 1.0f);
        addPedalKnob(rev, "PreDelay", 504, 0.0f, 0.2f);
        addPedalKnob(rev, "Tone", 506, 0.0f, 1.0f);
        addPedalDropdown(rev, "Type", 505, "Room\nHall\nPlate\nCathedr");
        addPedalKnob(rev, "Mod Depth", 502, 0.0f, 1.0f);
        addPedalKnob(rev, "Mix", 503, 0.0f, 1.0f);
        addPedalKnob(rev, "Send", 2060, 0.0f, 1.0f);
    }

    // Tab 4: Basic Filters
    {
        lv_obj_t* f1 = createPedalCard(tab4, "FILTER 1", lv_color_hex(0x059669)); // Dark Green
        addPedalKnob(f1, "Cutoff", 2200, 20.0f, 20000.0f, 0);
        addPedalKnob(f1, "Reson", 2201, 0.0f, 0.99f);
        addPedalDropdown(f1, "Mode", 2202, "LP\nHP\nBP");
        addPedalKnob(f1, "Send", 2120, 0.0f, 1.0f);

        lv_obj_t* f2 = createPedalCard(tab4, "FILTER 2", lv_color_hex(0x0D9488)); // Dark Teal
        addPedalKnob(f2, "Cutoff", 2205, 20.0f, 20000.0f, 0);
        addPedalKnob(f2, "Reson", 2206, 0.0f, 0.99f);
        addPedalDropdown(f2, "Mode", 2207, "LP\nHP\nBP");
        addPedalKnob(f2, "Send", 2150, 0.0f, 1.0f);

        lv_obj_t* f3 = createPedalCard(tab4, "FILTER 3", lv_color_hex(0x2563EB)); // Royal Blue
        addPedalKnob(f3, "Cutoff", 2210, 20.0f, 20000.0f, 0);
        addPedalKnob(f3, "Reson", 2211, 0.0f, 0.99f);
        addPedalDropdown(f3, "Mode", 2212, "LP\nHP\nBP");
        addPedalKnob(f3, "Send", 2160, 0.0f, 1.0f);
    }

    // Tab 5: Mod Filters
    {
        lv_obj_t* lplfo = createPedalCard(tab5, "LP LFO FILTER", lv_color_hex(0xFB7185)); // Soft Rose
        addPedalKnob(lplfo, "Rate", 490, 0.05f, 20.0f);
        addPedalKnob(lplfo, "Depth", 491, 0.0f, 1.0f);
        addPedalDropdown(lplfo, "Shape", 492, "Sine\nTri\nSaw\nSquare\nS&H");
        addPedalKnob(lplfo, "Cutoff", 493, 20.0f, 20000.0f, 0);
        addPedalKnob(lplfo, "Reson", 494, 0.0f, 0.95f);
        addPedalKnob(lplfo, "Send", 2100, 0.0f, 1.0f);

        lv_obj_t* hplfo = createPedalCard(tab5, "HP LFO FILTER", lv_color_hex(0xF43F5E)); // Vivid Rose
        addPedalKnob(hplfo, "Rate", 1590, 0.05f, 20.0f);
        addPedalKnob(hplfo, "Depth", 1591, 0.0f, 1.0f);
        addPedalDropdown(hplfo, "Shape", 1592, "Sine\nTri\nSaw\nSquare\nS&H");
        addPedalKnob(hplfo, "Cutoff", 1593, 20.0f, 20000.0f, 0);
        addPedalKnob(hplfo, "Reson", 1594, 0.0f, 0.95f);
        addPedalKnob(hplfo, "Send", 2090, 0.0f, 1.0f);

        lv_obj_t* slicer = createPedalCard(tab5, "SLICER", lv_color_hex(0x10B981)); // Emerald
        addPedalDropdown(slicer, "Rate 1", 570, "1/64\n1/32\n1/16\n1/8\n1/4\n1/2\n1/1\n2/1");
        addPedalDropdown(slicer, "Rate 2", 571, "1/64\n1/32\n1/16\n1/8\n1/4\n1/2\n1/1\n2/1");
        addPedalDropdown(slicer, "Rate 3", 572, "1/64\n1/32\n1/16\n1/8\n1/4\n1/2\n1/1\n2/1");
        addPedalToggle(slicer, "Act 1", 573);
        addPedalToggle(slicer, "Act 2", 574);
        addPedalToggle(slicer, "Act 3", 575);
        addPedalKnob(slicer, "Depth", 576, 0.0f, 1.0f);
        addPedalKnob(slicer, "Send", 2070, 0.0f, 1.0f);
    }

    // Tab 6: Pitch & EQ
    {
        lv_obj_t* oct = createPedalCard(tab6, "OCTAVER", lv_color_hex(0xD946EF)); // Magenta
        addPedalKnob(oct, "Mix", 1520, 0.0f, 1.0f);
        addPedalDropdown(oct, "Mode", 1521, "-1 Oct\n-2 Oct\n+1 Oct\nSubSynth");
        addPedalKnob(oct, "Unison", 1522, 0.0f, 1.0f);
        addPedalSpacer(oct, 15);
        addPedalKnob(oct, "Detune", 1523, 0.0f, 1.0f);
        addPedalKnob(oct, "Send", 2140, 0.0f, 1.0f);

        lv_obj_t* wobble = createPedalCard(tab6, "TAPE WOBBLE", lv_color_hex(0x65A30D)); // Lime
        addPedalKnob(wobble, "Rate", 560, 0.1f, 15.0f);
        addPedalKnob(wobble, "Depth", 561, 0.0f, 1.0f);
        addPedalKnob(wobble, "Sat", 562, 0.0f, 2.0f);
        addPedalSpacer(wobble, 15);
        addPedalKnob(wobble, "Mix", 563, 0.0f, 1.0f);
        addPedalKnob(wobble, "Send", 2040, 0.0f, 1.0f);

        // EQ Pedal
        lv_obj_t* eq = createPedalCard(tab6, "5-BAND EQ", lv_color_hex(0xF59E0B)); // Orange
        lv_obj_set_layout(eq, 0); // Remove standard layout flow to align EQ custom areas

        // Inner Sliders Container
        lv_obj_t* slidersCont = lv_obj_create(eq);
        lv_obj_set_size(slidersCont, 245, 180);
        lv_obj_set_style_bg_opa(slidersCont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(slidersCont, 0, 0);
        lv_obj_set_style_pad_all(slidersCont, 0, 0);
        lv_obj_set_layout(slidersCont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(slidersCont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(slidersCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(slidersCont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(slidersCont, LV_ALIGN_TOP_MID, 0, 10);
        
        addPedalSlider(slidersCont, "80Hz", 1530, 0.0f, 1.0f);
        addPedalSlider(slidersCont, "240Hz", 1531, 0.0f, 1.0f);
        addPedalSlider(slidersCont, "750Hz", 1532, 0.0f, 1.0f);
        addPedalSlider(slidersCont, "2.2kHz", 1533, 0.0f, 1.0f);
        addPedalSlider(slidersCont, "6.8kHz", 1534, 0.0f, 1.0f);

        // Separator line
        lv_obj_t* eqLine = lv_obj_create(eq);
        lv_obj_set_size(eqLine, 245, 2);
        lv_obj_set_style_bg_color(eqLine, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_bg_opa(eqLine, LV_OPA_20, 0);
        lv_obj_set_style_border_width(eqLine, 0, 0);
        lv_obj_align(eqLine, LV_ALIGN_TOP_MID, 0, 200);

        // Bottom knobs container: Mix & Send Knobs side-by-side
        lv_obj_t* mixCont = lv_obj_create(eq);
        lv_obj_set_size(mixCont, 245, 180);
        lv_obj_set_style_bg_opa(mixCont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(mixCont, 0, 0);
        lv_obj_set_style_pad_all(mixCont, 0, 0);
        lv_obj_set_layout(mixCont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(mixCont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(mixCont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(mixCont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(mixCont, LV_ALIGN_TOP_MID, 0, 210);

        addPedalKnob(mixCont, "Mix", 1539, 0.0f, 1.0f);
        addPedalKnob(mixCont, "Send", 2170, 0.0f, 1.0f);
    }

    // Create floating action bar container in the bottom right corner of mCenterArea for FX MIDI Learn
    lv_color_t trackColor = getTrackColor(mActiveTrack);
    lv_obj_t* actionBar = lv_obj_create(mCenterArea);
    lv_obj_set_size(actionBar, 130, 46);
    lv_obj_align(actionBar, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_opa(actionBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionBar, 0, 0);
    lv_obj_set_style_pad_all(actionBar, 0, 0);
    lv_obj_remove_flag(actionBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(actionBar, LV_OBJ_FLAG_FLOATING);

    lv_obj_set_layout(actionBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actionBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionBar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* learnBtn = lv_button_create(actionBar);
    lv_obj_set_size(learnBtn, 120, 36);
    if (mMidiLearnActive) {
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0xD32F2F), 0); // Active Red
        lv_obj_set_style_border_color(learnBtn, lv_color_hex(0xFF5252), 0);
    } else {
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(learnBtn, trackColor, 0);
    }
    lv_obj_set_style_border_width(learnBtn, 1, 0);
    lv_obj_set_style_radius(learnBtn, 6, 0);

    lv_obj_t* learnLbl = lv_label_create(learnBtn);
    mMidiLearnBtnLabel = learnLbl;
    if (mMidiLearnActive) {
        if (mMidiLearnTargetParamId >= 0) {
            lv_label_set_text(learnLbl, "TAP & WIGGLE");
        } else {
            lv_label_set_text(learnLbl, "TAP PARAMETER");
        }
    } else {
        lv_label_set_text(learnLbl, "MIDI LEARN");
    }
    lv_obj_set_style_text_font(learnLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(learnLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(learnLbl);

    auto learnClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->mMidiLearnActive = !ui->mMidiLearnActive;
        if (!ui->mMidiLearnActive) {
            ui->mMidiLearnTargetParamId = -1;
        }
        ui->createCenterContentArea(); // Rebuild center content area to update colors and state
    };
    lv_obj_add_event_cb(learnBtn, learnClickCb, LV_EVENT_CLICKED, this);
}

lv_obj_t* UIManager::createPedalCard(lv_obj_t* parent, const char* name, lv_color_t accentColor) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 265, 490);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, accentColor, 0);
    
    int sendIdx = -1;
    std::string nStr = name;
    if (nStr == "OVERDRIVE") sendIdx = 0;
    else if (nStr == "BITCRUSHER") sendIdx = 1;
    else if (nStr == "CHORUS") sendIdx = 2;
    else if (nStr == "PHASER") sendIdx = 3;
    else if (nStr == "TAPE WOBBLE") sendIdx = 4;
    else if (nStr == "DELAY") sendIdx = 5;
    else if (nStr == "REVERB") sendIdx = 6;
    else if (nStr == "SLICER") sendIdx = 7;
    else if (nStr == "COMPRESSOR") sendIdx = 8;
    else if (nStr == "HP LFO FILTER" || nStr == "HP LFO") sendIdx = 9;
    else if (nStr == "LP LFO FILTER" || nStr == "LP LFO") sendIdx = 10;
    else if (nStr == "FLANGER") sendIdx = 11;
    else if (nStr == "FILTER 1") sendIdx = 12;
    else if (nStr == "TAPE ECHO") sendIdx = 13;
    else if (nStr == "OCTAVER") sendIdx = 14;
    else if (nStr == "FILTER 2") sendIdx = 15;
    else if (nStr == "FILTER 3") sendIdx = 16;
    else if (nStr == "5-BAND EQ" || nStr == "EQ") sendIdx = 17;

    bool isActive = false;
    if (mActiveTrack >= 0 && mActiveTrack < (int)mEngine.getTracks().size()) {
        if (sendIdx >= 0 && sendIdx < 18) {
            float sendAmt = mEngine.getTracks()[mActiveTrack].fxSends[sendIdx];
            if (sendAmt > 0.001f) {
                isActive = true;
            }
        }
    }
    
    int borderWidth = isActive ? 8 : 2;
    lv_obj_set_style_border_width(card, borderWidth, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 8, 0);

    // Title label
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, accentColor, 0);
    
    // Separator line
    lv_obj_t* line = lv_obj_create(card);
    lv_obj_set_size(line, 245, 2);
    lv_obj_set_style_bg_color(line, accentColor, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    lv_obj_set_style_border_width(line, 0, 0);

    // Controls Body container
    lv_obj_t* body = lv_obj_create(card);
    lv_obj_set_size(body, 245, 420);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(body, 12, 0);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    return body;
}

void UIManager::addPedalKnob(lv_obj_t* pedal, const char* labelText, int paramId, float minVal, float maxVal, int decimals) {
    lv_obj_t* container = lv_obj_create(pedal);
    lv_obj_set_size(container, 68, 95);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Mini Arc
    lv_obj_t* arc = lv_arc_create(container);
    lv_obj_set_size(arc, 56, 56);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    
    // Get accent color of the selected track
    lv_color_t accentColor = getTrackColor(mActiveTrack);
    lv_obj_set_style_arc_color(arc, accentColor, LV_PART_INDICATOR);

    // Retrieve initial value from AudioEngine
    float currentVal = 0.0f;
    if (paramId >= 2000 && paramId < 2180) {
        int fxIndex = (paramId - 2000) / 10;
        currentVal = mEngine.getTracks()[mActiveTrack].fxSends[fxIndex];
    } else {
        currentVal = mEngine.getTracks()[0].parameters[paramId];
    }

    int arcVal = 0;
    if (maxVal > minVal) {
        arcVal = (int)(((currentVal - minVal) / (maxVal - minVal)) * 100.0f);
    }
    if (arcVal < 0) arcVal = 0;
    if (arcVal > 100) arcVal = 100;
    lv_arc_set_value(arc, arcVal);

    // Value Label inside the arc
    lv_obj_t* valLbl = lv_label_create(arc);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(valLbl, lv_color_hex(0xCCCCCC), 0);
    if (decimals == 0) {
        lv_label_set_text_fmt(valLbl, "%d", (int)currentVal);
    } else if (decimals == 1) {
        lv_label_set_text_fmt(valLbl, "%.1f", currentVal);
    } else {
        lv_label_set_text_fmt(valLbl, "%.2f", currentVal);
    }
    lv_obj_center(valLbl);

    // Title label at the bottom
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    // Setup Event Callback Data
    struct FxParamData {
        UIManager* ui;
        int paramId;
        float minVal;
        float maxVal;
        int decimals;
        lv_obj_t* valLbl;
    };
    FxParamData* data = new FxParamData{this, paramId, minVal, maxVal, decimals, valLbl};
    lv_obj_add_event_cb(arc, UIManager::fxControlEventCb, LV_EVENT_VALUE_CHANGED, data);
    lv_obj_add_event_cb(arc, UIManager::paramMidiLearnClickEventCb, LV_EVENT_PRESSED, data);

    mActiveFxWidgets.push_back(FxWidgetTracking{paramId, arc, valLbl, minVal, maxVal, decimals});

    auto freeCb = [](lv_event_t* e) {
        FxParamData* d = (FxParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(arc, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::addPedalSlider(lv_obj_t* pedal, const char* labelText, int paramId, float minVal, float maxVal) {
    lv_obj_t* container = lv_obj_create(pedal);
    lv_obj_set_size(container, 36, 160);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title label at top
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    // Vertical Slider
    lv_obj_t* slider = lv_slider_create(container);
    lv_obj_set_size(slider, 12, 110);
    
    // Get accent color of selected track
    lv_color_t accentColor = getTrackColor(mActiveTrack);
    lv_obj_set_style_bg_color(slider, accentColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
    
    // Grab value
    float currentVal = 0.0f;
    if (paramId >= 2000 && paramId < 2180) {
        int fxIndex = (paramId - 2000) / 10;
        currentVal = mEngine.getTracks()[mActiveTrack].fxSends[fxIndex];
    } else {
        currentVal = mEngine.getTracks()[0].parameters[paramId];
    }

    int sliderVal = (int)(((currentVal - minVal) / (maxVal - minVal)) * 100.0f);
    if (sliderVal < 0) sliderVal = 0;
    if (sliderVal > 100) sliderVal = 100;
    lv_slider_set_value(slider, sliderVal, LV_ANIM_OFF);

    // Value label at bottom
    lv_obj_t* valLbl = lv_label_create(container);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(valLbl, lv_color_hex(0xCCCCCC), 0);
    
    // Calculate gain in dB to display
    float dbVal = (currentVal - 0.5f) * 48.0f;
    lv_label_set_text_fmt(valLbl, "%d", (int)dbVal);

    // Callback data
    struct FxParamData {
        UIManager* ui;
        int paramId;
        float minVal;
        float maxVal;
        int decimals;
        lv_obj_t* valLbl;
    };
    FxParamData* data = new FxParamData{this, paramId, minVal, maxVal, -1, valLbl};
    lv_obj_add_event_cb(slider, UIManager::fxControlEventCb, LV_EVENT_VALUE_CHANGED, data);
    lv_obj_add_event_cb(slider, UIManager::paramMidiLearnClickEventCb, LV_EVENT_PRESSED, data);

    mActiveFxWidgets.push_back(FxWidgetTracking{paramId, slider, valLbl, minVal, maxVal, -1});

    auto freeCb = [](lv_event_t* e) {
        FxParamData* d = (FxParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(slider, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::addPedalToggle(lv_obj_t* pedal, const char* labelText, int paramId) {
    lv_obj_t* container = lv_obj_create(pedal);
    lv_obj_set_size(container, 68, 55);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Switch
    lv_obj_t* sw = lv_switch_create(container);
    lv_obj_set_size(sw, 44, 22);

    lv_color_t accentColor = getTrackColor(mActiveTrack);
    lv_obj_set_style_bg_color(sw, accentColor, LV_PART_INDICATOR | LV_STATE_CHECKED);

    float currentVal = 0.0f;
    if (paramId >= 2000 && paramId < 2180) {
        int fxIndex = (paramId - 2000) / 10;
        currentVal = mEngine.getTracks()[mActiveTrack].fxSends[fxIndex];
    } else {
        currentVal = mEngine.getTracks()[0].parameters[paramId];
    }

    if (currentVal > 0.5f) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    // Label
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    struct FxParamData {
        UIManager* ui;
        int paramId;
        float minVal;
        float maxVal;
        int decimals;
        lv_obj_t* valLbl;
    };
    FxParamData* data = new FxParamData{this, paramId, 0.0f, 1.0f, -2, nullptr};
    lv_obj_add_event_cb(sw, UIManager::fxControlEventCb, LV_EVENT_VALUE_CHANGED, data);

    auto freeCb = [](lv_event_t* e) {
        FxParamData* d = (FxParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(sw, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::addPedalDropdown(lv_obj_t* pedal, const char* labelText, int paramId, const char* options) {
    lv_obj_t* container = lv_obj_create(pedal);
    lv_obj_set_size(container, 95, 58);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Dropdown
    lv_obj_t* dd = lv_dropdown_create(container);
    lv_obj_set_size(dd, 90, 30);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_all(dd, 3, 0);

    float currentVal = 0.0f;
    if (paramId >= 2000 && paramId < 2180) {
        int fxIndex = (paramId - 2000) / 10;
        currentVal = mEngine.getTracks()[mActiveTrack].fxSends[fxIndex];
    } else {
        currentVal = mEngine.getTracks()[0].parameters[paramId];
    }

    if (paramId == 585) {
        // SC Track mappings: -1 is mapped to 0 (None), Tracks 0-7 mapped to 1-8
        int idx = (int)currentVal + 1;
        if (idx < 0) idx = 0;
        lv_dropdown_set_selected(dd, idx);
    } else {
        lv_dropdown_set_selected(dd, (int)currentVal);
    }

    // Label at bottom
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    struct FxParamData {
        UIManager* ui;
        int paramId;
        float minVal;
        float maxVal;
        int decimals;
        lv_obj_t* valLbl;
    };
    FxParamData* data = new FxParamData{this, paramId, 0.0f, 10.0f, -3, nullptr};
    lv_obj_add_event_cb(dd, UIManager::fxControlEventCb, LV_EVENT_VALUE_CHANGED, data);

    auto freeCb = [](lv_event_t* e) {
        FxParamData* d = (FxParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(dd, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::addPedalSpacer(lv_obj_t* pedal, int height) {
    lv_obj_t* spacer = lv_obj_create(pedal);
    lv_obj_set_size(spacer, 240, height);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
}

void UIManager::fxControlEventCb(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    struct FxParamData {
        UIManager* ui;
        int paramId;
        float minVal;
        float maxVal;
        int decimals;
        lv_obj_t* valLbl;
    };
    FxParamData* d = (FxParamData*)lv_event_get_user_data(e);
    if (!d) return;

    float rawVal = 0.0f;
    if (d->decimals == -2) {
        // Switch
        rawVal = lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1.0f : 0.0f;
    } else if (d->decimals == -3) {
        // Dropdown
        rawVal = (float)lv_dropdown_get_selected(obj);
        if (d->paramId == 585) {
            // Compressor sidechain track: 0 is None (-1), 1-8 are Tracks 0-7
            rawVal -= 1.0f;
        }
    } else if (d->decimals == -1) {
        // EQ Slider
        int v = lv_slider_get_value(obj);
        rawVal = d->minVal + ((float)v / 100.0f) * (d->maxVal - d->minVal);
    } else {
        // Arc (Knob)
        int v = lv_arc_get_value(obj);
        rawVal = d->minVal + ((float)v / 100.0f) * (d->maxVal - d->minVal);
    }

    // Update the AudioEngine parameter via setParameter to support track-specific routing
    d->ui->mEngine.setParameter(d->ui->mActiveTrack, d->paramId, rawVal);

    // Update UI labels if needed
    if (d->valLbl) {
        if (d->decimals == -1) {
            float db = (rawVal - 0.5f) * 48.0f;
            lv_label_set_text_fmt(d->valLbl, "%d", (int)db);
        } else if (d->decimals == 0) {
            lv_label_set_text_fmt(d->valLbl, "%d", (int)rawVal);
        } else if (d->decimals == 1) {
            lv_label_set_text_fmt(d->valLbl, "%.1f", rawVal);
        } else {
            lv_label_set_text_fmt(d->valLbl, "%.2f", rawVal);
        }
    }
}

// =========================================================================
// --- Mixer Popup Menu ---
// =========================================================================

struct MixerControlData {
    UIManager* ui;
    int trackIdx;
    lv_obj_t* label;
};

void UIManager::openMixerPopup(int trackIdx) {
    if (mMixerModal) {
        lv_obj_del(mMixerModal);
        mMixerModal = nullptr;
    }

    mMixerModal = lv_obj_create(mMainScreen);
    lv_obj_set_size(mMixerModal, 420, 380);
    lv_obj_center(mMixerModal);
    lv_obj_set_style_bg_color(mMixerModal, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(mMixerModal, LV_OPA_90, 0);
    
    lv_color_t trackColor = getTrackColor(trackIdx);
    lv_obj_set_style_border_color(mMixerModal, trackColor, 0);
    lv_obj_set_style_border_width(mMixerModal, 2, 0);
    lv_obj_set_style_radius(mMixerModal, 16, 0);
    lv_obj_set_style_pad_all(mMixerModal, 15, 0);
    
    lv_obj_set_layout(mMixerModal, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mMixerModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mMixerModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(mMixerModal, 12, 0);

    // Lambda to free control data
    auto freeMixerData = [](lv_event_t* e) {
        MixerControlData* data = (MixerControlData*)lv_event_get_user_data(e);
        delete data;
    };

    // Header Row
    lv_obj_t* headerRow = lv_obj_create(mMixerModal);
    lv_obj_set_size(headerRow, LV_PCT(100), 35);
    lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(headerRow, 0, 0);
    lv_obj_set_style_pad_all(headerRow, 0, 0);
    lv_obj_set_layout(headerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title Label
    lv_obj_t* title = lv_label_create(headerRow);
    lv_label_set_text_fmt(title, "TRACK %d MIXER", trackIdx + 1);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

    // Close Button
    lv_obj_t* closeBtn = lv_btn_create(headerRow);
    lv_obj_set_size(closeBtn, 28, 28);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(closeBtn, 14, 0);
    lv_obj_set_style_pad_all(closeBtn, 0, 0);
    
    lv_obj_t* closeLabel = lv_label_create(closeBtn);
    lv_label_set_text(closeLabel, "X");
    lv_obj_set_style_text_font(closeLabel, &lv_font_montserrat_12, 0);
    lv_obj_center(closeLabel);
    
    lv_obj_add_event_cb(closeBtn, UIManager::closeMixerPopupEventCb, LV_EVENT_CLICKED, this);

    // --- 1. Engine Dropdown Row (Placed at the top below header) ---
    lv_obj_t* engRow = lv_obj_create(mMixerModal);
    lv_obj_set_size(engRow, LV_PCT(100), 45);
    lv_obj_set_style_bg_opa(engRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(engRow, 0, 0);
    lv_obj_set_style_pad_all(engRow, 0, 0);
    lv_obj_set_layout(engRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(engRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(engRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* engTitle = lv_label_create(engRow);
    lv_label_set_text(engTitle, "Engine");
    lv_obj_set_style_text_font(engTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(engTitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(engTitle, 60);

    lv_obj_t* engDd = lv_dropdown_create(engRow);
    lv_obj_set_size(engDd, 290, 32);
    lv_dropdown_set_options(engDd, 
        LV_SYMBOL_KEYBOARD " Subtractive\n"
        LV_SYMBOL_BELL " FM Synth\n"
        LV_SYMBOL_LOOP " Sampler\n"
        LV_SYMBOL_SHUFFLE " Granular\n"
        LV_SYMBOL_TINT " Wavetable\n"
        LV_SYMBOL_WARNING " FM Drum\n"
        LV_SYMBOL_CHARGE " Analog Drum\n"
        LV_SYMBOL_PLAY " Audio In\n"
        LV_SYMBOL_AUDIO " SoundFont"
    );
    lv_obj_set_style_bg_color(engDd, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(engDd, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(engDd, 1, 0);
    lv_obj_set_style_text_color(engDd, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(engDd, &lv_font_montserrat_12, 0);

    // Style the dropdown list dynamically so it fits 9 items on screen perfectly without scrolling
    lv_obj_t* engList = lv_dropdown_get_list(engDd);
    lv_obj_set_style_max_height(engList, 300, 0);
    lv_obj_set_style_bg_color(engList, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(engList, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(engList, 1, 0);
    lv_obj_set_style_text_color(engList, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(engList, &lv_font_montserrat_12, 0);

    int currentEngineType = mEngine.getTracks()[trackIdx].engineType;
    int ddIndex = 0;
    if (currentEngineType >= 0 && currentEngineType <= 6) {
        ddIndex = currentEngineType;
    } else if (currentEngineType == 8) {
        ddIndex = 7;
    } else if (currentEngineType == 9) {
        ddIndex = 8;
    }
    lv_dropdown_set_selected(engDd, ddIndex);

    MixerControlData* ddData = new MixerControlData{this, trackIdx, nullptr};
    lv_obj_add_event_cb(engDd, UIManager::mixerEngineDdEventCb, LV_EVENT_VALUE_CHANGED, ddData);
    lv_obj_add_event_cb(engDd, freeMixerData, LV_EVENT_DELETE, ddData);

    // --- 2. Volume Slider Row ---
    lv_obj_t* volRow = lv_obj_create(mMixerModal);
    lv_obj_set_size(volRow, LV_PCT(100), 45);
    lv_obj_set_style_bg_opa(volRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volRow, 0, 0);
    lv_obj_set_style_pad_all(volRow, 0, 0);
    lv_obj_set_layout(volRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(volRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* volTitle = lv_label_create(volRow);
    lv_label_set_text(volTitle, "Volume");
    lv_obj_set_style_text_font(volTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(volTitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(volTitle, 60);

    lv_obj_t* volSlider = lv_slider_create(volRow);
    lv_obj_set_size(volSlider, 240, 12);
    lv_slider_set_range(volSlider, 0, 100);
    lv_obj_set_style_bg_color(volSlider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_radius(volSlider, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(volSlider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_radius(volSlider, 6, LV_PART_KNOB);

    float currentVol = mEngine.getTracks()[trackIdx].volume;
    lv_slider_set_value(volSlider, (int)(currentVol * 100.0f), LV_ANIM_OFF);

    lv_obj_t* volValLbl = lv_label_create(volRow);
    lv_label_set_text_fmt(volValLbl, "%d%%", (int)(currentVol * 100.0f));
    lv_obj_set_style_text_font(volValLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(volValLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(volValLbl, 40);
    lv_obj_set_style_text_align(volValLbl, LV_TEXT_ALIGN_RIGHT, 0);

    MixerControlData* volData = new MixerControlData{this, trackIdx, volValLbl};
    lv_obj_add_event_cb(volSlider, UIManager::mixerVolumeSliderEventCb, LV_EVENT_VALUE_CHANGED, volData);
    lv_obj_add_event_cb(volSlider, freeMixerData, LV_EVENT_DELETE, volData);

    // --- 3. Panning Slider Row ---
    lv_obj_t* panRow = lv_obj_create(mMixerModal);
    lv_obj_set_size(panRow, LV_PCT(100), 45);
    lv_obj_set_style_bg_opa(panRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panRow, 0, 0);
    lv_obj_set_style_pad_all(panRow, 0, 0);
    lv_obj_set_layout(panRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* panTitle = lv_label_create(panRow);
    lv_label_set_text(panTitle, "Panning");
    lv_obj_set_style_text_font(panTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(panTitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(panTitle, 60);

    lv_obj_t* panSlider = lv_slider_create(panRow);
    lv_obj_set_size(panSlider, 240, 12);
    lv_slider_set_range(panSlider, 0, 100);
    lv_obj_set_style_bg_color(panSlider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(panSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(panSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_radius(panSlider, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(panSlider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_radius(panSlider, 6, LV_PART_KNOB);

    float currentPan = mEngine.getTracks()[trackIdx].pan;
    int panVal = (int)(currentPan * 100.0f);
    lv_slider_set_value(panSlider, panVal, LV_ANIM_OFF);

    lv_obj_t* panValLbl = lv_label_create(panRow);
    if (panVal == 50) {
        lv_label_set_text(panValLbl, "C");
    } else if (panVal < 50) {
        lv_label_set_text_fmt(panValLbl, "L%d", 50 - panVal);
    } else {
        lv_label_set_text_fmt(panValLbl, "R%d", panVal - 50);
    }
    lv_obj_set_style_text_font(panValLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(panValLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(panValLbl, 40);
    lv_obj_set_style_text_align(panValLbl, LV_TEXT_ALIGN_RIGHT, 0);

    MixerControlData* panData = new MixerControlData{this, trackIdx, panValLbl};
    lv_obj_add_event_cb(panSlider, UIManager::mixerPanSliderEventCb, LV_EVENT_VALUE_CHANGED, panData);
    lv_obj_add_event_cb(panSlider, freeMixerData, LV_EVENT_DELETE, panData);

    // --- 4. Mute / Solo / Active Button Row ---
    lv_obj_t* btnRow = lv_obj_create(mMixerModal);
    lv_obj_set_size(btnRow, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int btnWidth = 115;
    int btnHeight = 35;

    // Mute Button
    lv_obj_t* muteBtn = lv_btn_create(btnRow);
    lv_obj_set_size(muteBtn, btnWidth, btnHeight);
    lv_obj_add_flag(muteBtn, LV_OBJ_FLAG_CHECKABLE);
    
    bool initiallyMuted = mEngine.getTracks()[trackIdx].isMuted;
    if (initiallyMuted) {
        lv_obj_add_state(muteBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(muteBtn, lv_color_hex(0xE06C75), 0);
    } else {
        lv_obj_clear_state(muteBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(muteBtn, lv_color_hex(0x2A2A2A), 0);
    }
    lv_obj_set_style_radius(muteBtn, 8, 0);

    lv_obj_t* muteLabel = lv_label_create(muteBtn);
    lv_label_set_text(muteLabel, "MUTE");
    lv_obj_set_style_text_font(muteLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(muteLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(muteLabel);

    MixerControlData* muteData = new MixerControlData{this, trackIdx, nullptr};
    lv_obj_add_event_cb(muteBtn, UIManager::mixerMuteBtnEventCb, LV_EVENT_VALUE_CHANGED, muteData);
    lv_obj_add_event_cb(muteBtn, freeMixerData, LV_EVENT_DELETE, muteData);

    // Solo Button
    lv_obj_t* soloBtn = lv_btn_create(btnRow);
    lv_obj_set_size(soloBtn, btnWidth, btnHeight);
    lv_obj_add_flag(soloBtn, LV_OBJ_FLAG_CHECKABLE);
    
    bool initiallySoloed = mEngine.getTracks()[trackIdx].isSoloed;
    if (initiallySoloed) {
        lv_obj_add_state(soloBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(soloBtn, lv_color_hex(0xE5C07B), 0);
    } else {
        lv_obj_clear_state(soloBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(soloBtn, lv_color_hex(0x2A2A2A), 0);
    }
    lv_obj_set_style_radius(soloBtn, 8, 0);

    lv_obj_t* soloLabel = lv_label_create(soloBtn);
    lv_label_set_text(soloLabel, "SOLO");
    lv_obj_set_style_text_font(soloLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(soloLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(soloLabel);

    MixerControlData* soloData = new MixerControlData{this, trackIdx, nullptr};
    lv_obj_add_event_cb(soloBtn, UIManager::mixerSoloBtnEventCb, LV_EVENT_VALUE_CHANGED, soloData);
    lv_obj_add_event_cb(soloBtn, freeMixerData, LV_EVENT_DELETE, soloData);

    // Active Button
    lv_obj_t* activeBtn = lv_btn_create(btnRow);
    lv_obj_set_size(activeBtn, btnWidth, btnHeight);
    lv_obj_add_flag(activeBtn, LV_OBJ_FLAG_CHECKABLE);

    bool initiallyActive = mTrackEnabled[trackIdx];
    if (initiallyActive) {
        lv_obj_add_state(activeBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(activeBtn, trackColor, 0);
    } else {
        lv_obj_clear_state(activeBtn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(activeBtn, lv_color_hex(0x2A2A2A), 0);
    }
    lv_obj_set_style_radius(activeBtn, 8, 0);

    lv_obj_t* activeLabel = lv_label_create(activeBtn);
    lv_label_set_text(activeLabel, "ACTIVE");
    lv_obj_set_style_text_font(activeLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(activeLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(activeLabel);

    MixerControlData* activeData = new MixerControlData{this, trackIdx, nullptr};
    lv_obj_add_event_cb(activeBtn, UIManager::mixerActiveBtnEventCb, LV_EVENT_VALUE_CHANGED, activeData);
    lv_obj_add_event_cb(activeBtn, freeMixerData, LV_EVENT_DELETE, activeData);
}

void UIManager::closeMixerPopupEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui && ui->mMixerModal) {
        lv_obj_del(ui->mMixerModal);
        ui->mMixerModal = nullptr;
    }
}

void UIManager::mixerVolumeSliderEventCb(lv_event_t* e) {
    MixerControlData* d = (MixerControlData*)lv_event_get_user_data(e);
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    
    if (d->label) {
        lv_label_set_text_fmt(d->label, "%d%%", value);
    }
    d->ui->mEngine.setTrackVolume(d->trackIdx, (float)value / 100.0f);
}

void UIManager::mixerPanSliderEventCb(lv_event_t* e) {
    MixerControlData* d = (MixerControlData*)lv_event_get_user_data(e);
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    
    if (d->label) {
        if (value == 50) {
            lv_label_set_text(d->label, "C");
        } else if (value < 50) {
            lv_label_set_text_fmt(d->label, "L%d", 50 - value);
        } else {
            lv_label_set_text_fmt(d->label, "R%d", value - 50);
        }
    }
    d->ui->mEngine.setTrackPan(d->trackIdx, (float)value / 100.0f);
}

void UIManager::mixerEngineDdEventCb(lv_event_t* e) {
    MixerControlData* d = (MixerControlData*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int selected = lv_dropdown_get_selected(dd);
    
    int engineType = 0;
    if (selected >= 0 && selected <= 6) {
        engineType = selected;
    } else if (selected == 7) {
        engineType = 8;
    } else if (selected == 8) {
        engineType = 9;
    }
    d->ui->mEngine.setEngineType(d->trackIdx, engineType);
    d->ui->applyDefaultMidiMappings(d->trackIdx, engineType);
    d->ui->updateHighlighting();
}

void UIManager::mixerMuteBtnEventCb(lv_event_t* e) {
    MixerControlData* d = (MixerControlData*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    bool isChecked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    
    if (isChecked) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE06C75), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    }
    d->ui->mEngine.setTrackMute(d->trackIdx, isChecked);
}

void UIManager::mixerSoloBtnEventCb(lv_event_t* e) {
    MixerControlData* d = (MixerControlData*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    bool isChecked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    
    if (isChecked) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE5C07B), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    }
    d->ui->mEngine.setTrackSolo(d->trackIdx, isChecked);
}

void UIManager::mixerActiveBtnEventCb(lv_event_t* e) {
    MixerControlData* d = (MixerControlData*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    bool isChecked = lv_obj_has_state(btn, LV_STATE_CHECKED);
    
    if (isChecked) {
        lv_color_t trackColor = d->ui->getTrackColor(d->trackIdx);
        lv_obj_set_style_bg_color(btn, trackColor, 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), 0);
    }
    
    d->ui->mTrackEnabled[d->trackIdx] = isChecked;
    d->ui->mEngine.setTrackActive(d->trackIdx, isChecked);
    d->ui->updateHighlighting();
}

// =========================================================================
// --- Mix/Rec Screen ---
// =========================================================================
void UIManager::populateMixRecScreen() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_t* tabview = lv_tabview_create(mCenterArea);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);

    // Modern dark styling for tabview
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tabview, 0, 0);

    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(tab_bar, 1, LV_PART_MAIN);

    // Style the individual tab buttons in the tab bar
    for(uint32_t i = 0; i < lv_obj_get_child_count(tab_bar); i++) {
        lv_obj_t* btn = lv_obj_get_child(tab_bar, i);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_color(btn, trackColor, LV_STATE_CHECKED);
    }

    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "Transport & Rec");
    lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "Mixer");

    lv_obj_set_style_pad_all(tab1, 10, 0);
    lv_obj_set_style_pad_all(tab2, 10, 0);

    // Register a delete event callback on the tabview container to safely clean up references
    lv_obj_add_event_cb(tabview, mixRecScreenDeleteEventCb, LV_EVENT_DELETE, this);

    // Glassmorphic Card Styling helper
    auto applyCardStyle = [](lv_obj_t* card) {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 20, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    };

    // --- TAB 1: TRANSPORT & REC ---
    lv_obj_t* outerRow = lv_obj_create(tab1);
    lv_obj_set_size(outerRow, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(outerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outerRow, 0, 0);
    lv_obj_set_style_pad_all(outerRow, 5, 0);
    lv_obj_set_layout(outerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(outerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(outerRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(outerRow, LV_OBJ_FLAG_SCROLLABLE);

    // --- LEFT COLUMN: Tempo & Swing ---
    lv_obj_t* col1 = lv_obj_create(outerRow);
    lv_obj_set_size(col1, 380, 480);
    applyCardStyle(col1);

    lv_obj_t* title1 = lv_label_create(col1);
    lv_label_set_text(title1, "TEMPO & TIMING");
    lv_obj_set_style_text_font(title1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title1, lv_color_hex(0x888888), 0);

    // Flex row inside left column to house the BPM & Swing knobs side-by-side
    lv_obj_t* knobRow = lv_obj_create(col1);
    lv_obj_set_size(knobRow, 350, 190);
    lv_obj_set_style_bg_opa(knobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobRow, 0, 0);
    lv_obj_set_style_pad_all(knobRow, 0, 0);
    lv_obj_set_layout(knobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(knobRow, LV_OBJ_FLAG_SCROLLABLE);

    // 1. BPM Arc/Knob
    lv_obj_t* bpmCont = lv_obj_create(knobRow);
    lv_obj_set_size(bpmCont, 150, 180);
    lv_obj_set_style_bg_opa(bpmCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bpmCont, 0, 0);
    lv_obj_set_style_pad_all(bpmCont, 0, 0);
    lv_obj_set_layout(bpmCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bpmCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bpmCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bpmCont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bpmArc = lv_arc_create(bpmCont);
    lv_obj_set_size(bpmArc, 125, 125);
    lv_arc_set_rotation(bpmArc, 135);
    lv_arc_set_bg_angles(bpmArc, 0, 270);
    lv_arc_set_range(bpmArc, 12, 300);
    lv_obj_set_style_bg_opa(bpmArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(bpmArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(bpmArc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(bpmArc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(bpmArc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(bpmArc, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    lv_obj_set_style_arc_color(bpmArc, trackColor, LV_PART_INDICATOR);

    int currentBpm = (int)mEngine.mBpm;
    if (currentBpm < 12) currentBpm = 12;
    if (currentBpm > 300) currentBpm = 300;
    lv_arc_set_value(bpmArc, currentBpm);

    lv_obj_t* bpmValLbl = lv_label_create(bpmArc);
    lv_obj_set_style_text_font(bpmValLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bpmValLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(bpmValLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(bpmValLbl, "%d\nBPM", currentBpm);
    lv_obj_center(bpmValLbl);

    lv_obj_add_event_cb(bpmArc, mixRecBpmKnobEventCb, LV_EVENT_VALUE_CHANGED, this);
    mMixRecBpmArc = bpmArc;

    lv_obj_t* bpmSubTitle = lv_label_create(bpmCont);
    lv_label_set_text(bpmSubTitle, "TEMPO");
    lv_obj_set_style_text_font(bpmSubTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bpmSubTitle, lv_color_hex(0x888888), 0);

    // 2. Swing Arc/Knob
    lv_obj_t* swingCont = lv_obj_create(knobRow);
    lv_obj_set_size(swingCont, 150, 180);
    lv_obj_set_style_bg_opa(swingCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swingCont, 0, 0);
    lv_obj_set_style_pad_all(swingCont, 0, 0);
    lv_obj_set_layout(swingCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(swingCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(swingCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(swingCont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* swingArc = lv_arc_create(swingCont);
    lv_obj_set_size(swingArc, 125, 125);
    lv_arc_set_rotation(swingArc, 135);
    lv_arc_set_bg_angles(swingArc, 0, 270);
    lv_arc_set_range(swingArc, 0, 100);
    lv_obj_set_style_bg_opa(swingArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(swingArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(swingArc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(swingArc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(swingArc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(swingArc, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    lv_obj_set_style_arc_color(swingArc, trackColor, LV_PART_INDICATOR);

    int currentSwingPercent = (int)(mEngine.mSwing / 0.50f * 100.0f);
    if (currentSwingPercent < 0) currentSwingPercent = 0;
    if (currentSwingPercent > 100) currentSwingPercent = 100;
    lv_arc_set_value(swingArc, currentSwingPercent);

    lv_obj_t* swingValLbl = lv_label_create(swingArc);
    lv_obj_set_style_text_font(swingValLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(swingValLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(swingValLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(swingValLbl, "%d%%\nSWING", currentSwingPercent);
    lv_obj_center(swingValLbl);

    lv_obj_add_event_cb(swingArc, mixRecSwingKnobEventCb, LV_EVENT_VALUE_CHANGED, this);
    mMixRecSwingArc = swingArc;

    lv_obj_t* swingSubTitle = lv_label_create(swingCont);
    lv_label_set_text(swingSubTitle, "SWING");
    lv_obj_set_style_text_font(swingSubTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(swingSubTitle, lv_color_hex(0x888888), 0);

    // Subtle horizontal divider line
    lv_obj_t* spacer = lv_obj_create(col1);
    lv_obj_set_size(spacer, 300, 2);
    lv_obj_set_style_bg_color(spacer, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);

    // TAP TEMPO Button
    lv_obj_t* tapBtn = lv_button_create(col1);
    lv_obj_set_size(tapBtn, 300, 50);
    lv_obj_set_style_bg_color(tapBtn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(tapBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tapBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(tapBtn, 1, 0);
    lv_obj_set_style_radius(tapBtn, 8, 0);

    lv_obj_t* tapBtnLbl = lv_label_create(tapBtn);
    lv_label_set_text(tapBtnLbl, "TAP TEMPO");
    lv_obj_set_style_text_font(tapBtnLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tapBtnLbl, trackColor, 0);
    lv_obj_center(tapBtnLbl);

    lv_obj_add_event_cb(tapBtn, mixRecBpmTapEventCb, LV_EVENT_CLICKED, this);

    // --- RIGHT COLUMN: Master Volume & Transport ---
    lv_obj_t* col2 = lv_obj_create(outerRow);
    lv_obj_set_size(col2, 380, 480);
    applyCardStyle(col2);

    lv_obj_t* title2 = lv_label_create(col2);
    lv_label_set_text(title2, "MASTER & TRANSPORT");
    lv_obj_set_style_text_font(title2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title2, lv_color_hex(0x888888), 0);

    // Master Volume Arc Container
    lv_obj_t* volCont = lv_obj_create(col2);
    lv_obj_set_size(volCont, 180, 190);
    lv_obj_set_style_bg_opa(volCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volCont, 0, 0);
    lv_obj_set_style_pad_all(volCont, 0, 0);
    lv_obj_set_layout(volCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(volCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(volCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(volCont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* volArc = lv_arc_create(volCont);
    lv_obj_set_size(volArc, 140, 140);
    lv_arc_set_rotation(volArc, 135);
    lv_arc_set_bg_angles(volArc, 0, 270);
    lv_arc_set_range(volArc, 0, 100);
    lv_obj_set_style_bg_opa(volArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(volArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(volArc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(volArc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(volArc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(volArc, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    lv_obj_set_style_arc_color(volArc, trackColor, LV_PART_INDICATOR);

    int currentVolPercent = (int)(mEngine.mMasterVolume / 1.5f * 100.0f);
    if (currentVolPercent < 0) currentVolPercent = 0;
    if (currentVolPercent > 100) currentVolPercent = 100;
    lv_arc_set_value(volArc, currentVolPercent);

    lv_obj_t* volValLbl = lv_label_create(volArc);
    lv_obj_set_style_text_font(volValLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(volValLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(volValLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(volValLbl, "%d%%\nVOLUME", currentVolPercent);
    lv_obj_center(volValLbl);

    lv_obj_add_event_cb(volArc, mixRecMasterVolKnobEventCb, LV_EVENT_VALUE_CHANGED, this);
    mMixRecMasterVolArc = volArc;

    lv_obj_t* volSubTitle = lv_label_create(volCont);
    lv_label_set_text(volSubTitle, "MASTER VOLUME");
    lv_obj_set_style_text_font(volSubTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(volSubTitle, lv_color_hex(0x888888), 0);

    // Subtle divider
    lv_obj_t* spacer2 = lv_obj_create(col2);
    lv_obj_set_size(spacer2, 300, 2);
    lv_obj_set_style_bg_color(spacer2, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(spacer2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(spacer2, 0, 0);
    lv_obj_set_style_pad_all(spacer2, 0, 0);

    // Transport buttons flex row
    lv_obj_t* transRow = lv_obj_create(col2);
    lv_obj_set_size(transRow, 300, 65);
    lv_obj_set_style_bg_opa(transRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(transRow, 0, 0);
    lv_obj_set_style_pad_all(transRow, 0, 0);
    lv_obj_set_layout(transRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(transRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(transRow, LV_OBJ_FLAG_SCROLLABLE);

    auto applyTransBtnStyle = [](lv_obj_t* btn) {
        lv_obj_set_size(btn, 85, 50);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
    };

    // Play Button
    lv_obj_t* playBtn = lv_button_create(transRow);
    applyTransBtnStyle(playBtn);
    lv_obj_t* playLbl = lv_label_create(playBtn);
    lv_label_set_text(playLbl, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(playLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(playLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(playLbl);
    lv_obj_add_event_cb(playBtn, mixRecPlayBtnEventCb, LV_EVENT_CLICKED, this);
    mMixRecPlayBtn = playBtn;

    // Stop Button
    lv_obj_t* stopBtn = lv_button_create(transRow);
    applyTransBtnStyle(stopBtn);
    lv_obj_t* stopLbl = lv_label_create(stopBtn);
    lv_label_set_text(stopLbl, LV_SYMBOL_STOP);
    lv_obj_set_style_text_font(stopLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stopLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(stopLbl);
    lv_obj_add_event_cb(stopBtn, mixRecStopBtnEventCb, LV_EVENT_CLICKED, this);
    mMixRecStopBtn = stopBtn;

    // Record Button
    lv_obj_t* recBtn = lv_button_create(transRow);
    applyTransBtnStyle(recBtn);
    
    // Perfectly round centered red circle indicator
    lv_obj_t* recCircle = lv_obj_create(recBtn);
    lv_obj_set_size(recCircle, 14, 14);
    lv_obj_set_style_bg_color(recCircle, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_bg_opa(recCircle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(recCircle, 0, 0);
    lv_obj_set_style_radius(recCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(recCircle);
    lv_obj_remove_flag(recCircle, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_add_event_cb(recBtn, mixRecRecordBtnEventCb, LV_EVENT_CLICKED, this);
    mMixRecRecordBtn = recBtn;

    // --- TAB 2: MIXER ---
    lv_obj_t* mixerContainer = lv_obj_create(tab2);
    lv_obj_set_size(mixerContainer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(mixerContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mixerContainer, 0, 0);
    lv_obj_set_style_pad_all(mixerContainer, 5, 0);
    lv_obj_set_layout(mixerContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mixerContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mixerContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(mixerContainer, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 8; ++i) {
        lv_color_t tColor = getTrackColor(i);

        lv_obj_t* sliderCard = lv_obj_create(mixerContainer);
        mMixerCards[i] = sliderCard;
        lv_obj_set_size(sliderCard, 90, 480);
        applyCardStyle(sliderCard);
        lv_obj_set_style_pad_all(sliderCard, 5, 0);

        // Track title
        lv_obj_t* trackTitle = lv_label_create(sliderCard);
        lv_label_set_text_fmt(trackTitle, "Track %d", i + 1);
        lv_obj_set_style_text_font(trackTitle, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(trackTitle, tColor, 0);

        // Parameter Value Label (0% to 150%)
        lv_obj_t* valLbl = lv_label_create(sliderCard);
        mMixerVolLabels[i] = valLbl;
        float currentVol = mEngine.getTracks()[i].volume;
        int pctVal = (int)(currentVol * 100.0f);
        lv_label_set_text_fmt(valLbl, "%d%%", pctVal);
        lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(valLbl, lv_color_hex(0xFFFFFF), 0);

        // Vertical Slider (fader)
        lv_obj_t* slider = lv_slider_create(sliderCard);
        mMixerVolSliders[i] = slider;
        lv_obj_set_size(slider, 16, 320); // Nice tall vertical slider!
        lv_slider_set_range(slider, 0, 150);
        lv_slider_set_value(slider, pctVal, LV_ANIM_OFF);
        
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, tColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 8, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 2, LV_PART_KNOB);

        struct MixerSliderData {
            UIManager* ui;
            int trackIdx;
            lv_obj_t* label;
        };
        MixerSliderData* sData = new MixerSliderData{this, i, valLbl};
        lv_obj_add_event_cb(slider, mixerSliderEventCb, LV_EVENT_VALUE_CHANGED, sData);

        auto sFreeCb = [](lv_event_t* e) {
            MixerSliderData* d = (MixerSliderData*)lv_event_get_user_data(e);
            delete d;
        };
        lv_obj_add_event_cb(slider, sFreeCb, LV_EVENT_DELETE, sData);
    }

    // Initialize transport visuals based on live engine state
    updateTransportVisuals();
}

void UIManager::updateTransportVisuals() {
    if (!mMixRecPlayBtn || !mMixRecStopBtn || !mMixRecRecordBtn) return;

    bool isPlaying = mEngine.getIsPlaying();
    bool isRecording = mEngine.getIsRecording();

    // Play button: Green (0x32CD32) when playing, otherwise dark gray (0x2A2A2A)
    if (isPlaying) {
        lv_obj_set_style_bg_color(mMixRecPlayBtn, lv_color_hex(0x32CD32), 0);
        lv_obj_set_style_bg_opa(mMixRecPlayBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mMixRecPlayBtn, lv_color_hex(0x32CD32), 0);
    } else {
        lv_obj_set_style_bg_color(mMixRecPlayBtn, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(mMixRecPlayBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mMixRecPlayBtn, lv_color_hex(0x444444), 0);
    }

    // Stop button: Red (0xE06C75) when NOT playing, otherwise dark gray (0x2A2A2A)
    if (!isPlaying) {
        lv_obj_set_style_bg_color(mMixRecStopBtn, lv_color_hex(0xE06C75), 0);
        lv_obj_set_style_bg_opa(mMixRecStopBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mMixRecStopBtn, lv_color_hex(0xE06C75), 0);
    } else {
        lv_obj_set_style_bg_color(mMixRecStopBtn, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(mMixRecStopBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mMixRecStopBtn, lv_color_hex(0x444444), 0);
    }

    // Record button: Amber/Red (0xFF5533) when recording, otherwise dark gray (0x2A2A2A)
    if (isRecording) {
        lv_obj_set_style_bg_color(mMixRecRecordBtn, lv_color_hex(0xFF5533), 0);
        lv_obj_set_style_bg_opa(mMixRecRecordBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mMixRecRecordBtn, lv_color_hex(0xFF5533), 0);
    } else {
        lv_obj_set_style_bg_color(mMixRecRecordBtn, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(mMixRecRecordBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mMixRecRecordBtn, lv_color_hex(0x444444), 0);
    }
}

void UIManager::mixRecBpmKnobEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    ui->mEngine.setTempo((float)val);

    lv_obj_t* label = lv_obj_get_child(arc, 0);
    if (label) {
        lv_label_set_text_fmt(label, "%d\nBPM", (int)val);
    }
}

void UIManager::mixRecBpmTapEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    uint32_t now = lv_tick_get();

    // Delay threshold: reset if interval exceeds 6 seconds (supporting tempos down to 10 BPM)
    if (!ui->mTapTimestamps.empty()) {
        uint32_t last = ui->mTapTimestamps.back();
        if (now - last > 6000) {
            ui->mTapTimestamps.clear();
        }
    }

    ui->mTapTimestamps.push_back(now);

    // Keep history down to last 5 tap timestamps
    if (ui->mTapTimestamps.size() > 5) {
        ui->mTapTimestamps.erase(ui->mTapTimestamps.begin());
    }

    if (ui->mTapTimestamps.size() >= 2) {
        float totalDiff = 0.0f;
        for (size_t i = 1; i < ui->mTapTimestamps.size(); ++i) {
            totalDiff += (float)(ui->mTapTimestamps[i] - ui->mTapTimestamps[i - 1]);
        }
        float avgDiffMs = totalDiff / (float)(ui->mTapTimestamps.size() - 1);
        if (avgDiffMs > 0.0f) {
            float bpm = 60000.0f / avgDiffMs;

            // Bound calculated BPM securely in 12 - 300 range
            if (bpm >= 12.0f && bpm <= 300.0f) {
                ui->mEngine.setTempo(bpm);

                if (ui->mMixRecBpmArc) {
                    lv_arc_set_value(ui->mMixRecBpmArc, (int32_t)bpm);
                    lv_obj_t* label = lv_obj_get_child(ui->mMixRecBpmArc, 0);
                    if (label) {
                        lv_label_set_text_fmt(label, "%d\nBPM", (int)bpm);
                    }
                }
            }
        }
    }
}

void UIManager::mixRecSwingKnobEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    
    // Scale swing percentage (0-100%) to core swing factors (0.0f - 0.50f)
    float swingVal = (float)val / 100.0f * 0.50f;
    ui->mEngine.setSwing(swingVal);

    lv_obj_t* label = lv_obj_get_child(arc, 0);
    if (label) {
        lv_label_set_text_fmt(label, "%d%%\nSWING", (int)val);
    }
}

void UIManager::mixRecMasterVolKnobEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int32_t val = lv_arc_get_value(arc);
    
    // Scale master volume input (0-100%) to normalized volume (0.0f - 1.0f)
    float volVal = (float)val / 100.0f;
    ui->mEngine.setMasterVolume(volVal);

    lv_obj_t* label = lv_obj_get_child(arc, 0);
    if (label) {
        lv_label_set_text_fmt(label, "%d%%\nVOLUME", (int)val);
    }
}

void UIManager::mixRecPlayBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.setPlaying(true);
    ui->updateTransportVisuals();
}

void UIManager::mixRecStopBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.setPlaying(false);
    ui->mEngine.setIsRecording(false);
    ui->updateTransportVisuals();
}

void UIManager::mixRecRecordBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    bool recState = !ui->mEngine.getIsRecording();
    ui->mEngine.setIsRecording(recState);

    // If recording, auto-start playback so core sequencer tracks start ticking immediately
    if (recState) {
        ui->mEngine.setPlaying(true);
    }

    ui->updateTransportVisuals();
}

void UIManager::mixRecScreenDeleteEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    // Nullify all Mix/Rec widget references so background updates in update() safely bypass them
    ui->mMixRecBpmArc = nullptr;
    ui->mMixRecSwingArc = nullptr;
    ui->mMixRecMasterVolArc = nullptr;
    ui->mMixRecPlayBtn = nullptr;
    ui->mMixRecStopBtn = nullptr;
    ui->mMixRecRecordBtn = nullptr;
    for (int i = 0; i < 8; ++i) {
        ui->mMixerVolSliders[i] = nullptr;
        ui->mMixerVolLabels[i] = nullptr;
        ui->mMixerCards[i] = nullptr;
    }
}

void UIManager::mixerSliderEventCb(lv_event_t* e) {
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    struct MixerSliderData {
        UIManager* ui;
        int trackIdx;
        lv_obj_t* label;
    };
    MixerSliderData* sData = (MixerSliderData*)lv_event_get_user_data(e);
    if (!sData) return;
    
    int val = lv_slider_get_value(slider);
    float vol = (float)val / 100.0f;
    
    {
        std::lock_guard<std::recursive_mutex> lock(sData->ui->mEngine.getLock());
        sData->ui->mEngine.getTracks()[sData->trackIdx].volume = vol;
    }
    
    if (sData->label) {
        lv_label_set_text_fmt(sData->label, "%d%%", val);
    }
}


// =========================================================================
// --- Parameters Screen Implementation ---
// =========================================================================

void UIManager::populateParamScreen() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);
    int engineType = mEngine.getTracks()[mActiveTrack].engineType;
    
    // Placeholder guard if active track is not using Subtractive (0), FM (1), Sampler (2), Granular (3), Wavetable (4), SoundFont (9), Audio In (8), FM Drum (5), or Analogue Drum (6) synthesis
    if (engineType != 0 && engineType != 1 && engineType != 2 && engineType != 3 && engineType != 4 && engineType != 9 && engineType != 8 && engineType != 5 && engineType != 6) {
        lv_obj_t* placeholder = lv_obj_create(mCenterArea);
        lv_obj_set_size(placeholder, 770, 470);
        lv_obj_align(placeholder, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(placeholder, lv_color_hex(0x151515), 0);
        lv_obj_set_style_bg_opa(placeholder, LV_OPA_90, 0);
        lv_obj_set_style_border_color(placeholder, trackColor, 0);
        lv_obj_set_style_border_width(placeholder, 2, 0);
        lv_obj_set_style_radius(placeholder, 16, 0);
        lv_obj_remove_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t* title = lv_label_create(placeholder);
        lv_label_set_text(title, "SYNTHESIS PARAMETERS");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);
        
        lv_obj_t* desc = lv_label_create(placeholder);
        lv_label_set_text(desc, "This track is not using the Subtractive, FM, Sampler, Granular, or Wavetable synthesis engine.\n\nPlease open the Mixer (Mix/Rec) screen and change the engine\ntype of this track to 'Subtractive', 'FM', 'Sampler', 'Granular', or 'Wavetable' to enable synthesis controls.");
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(0xCCCCCC), 0);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(desc, 600);
        lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(desc, LV_ALIGN_CENTER, 0, 20);
        
        return;
    }

    if (engineType == 0) {
        // Subtractive Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 480);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "OSCILLATORS");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "FILTER & LFO");
        lv_obj_t* tab3 = lv_tabview_add_tab(tabview, "ENVELOPES");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        lv_obj_set_style_pad_all(tab3, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab3, LV_OBJ_FLAG_SCROLLABLE);

        populateParamSubtractiveOscTab(tab1);
        populateParamSubtractiveFilterTab(tab2);
        populateParamSubtractiveEnvTab(tab3);
    } else if (engineType == 1) {
        // FM Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 480);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "OPERATORS");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "ROUTING");
        lv_obj_t* tab3 = lv_tabview_add_tab(tabview, "FILTER & ENVELOPES");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        lv_obj_set_style_pad_all(tab3, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab3, LV_OBJ_FLAG_SCROLLABLE);

        populateParamFmTab(tab1, tab2, tab3);
    } else if (engineType == 2) {
        // Sampler Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 480);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "SAMPLING");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "SYNTHESIS & ENVELOPES");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamSamplerTab(tab1);
        populateParamSamplerSynthesisTab(tab2);
    } else if (engineType == 3) {
        // Granular Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 550);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "SAMPLING");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "SYNTHESIS");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamGranularSamplingTab(tab1);
        populateParamGranularSynthTab(tab2);
    } else if (engineType == 4) {
        // Wavetable Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 480);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "WAVETABLES");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "FILTER & ENVELOPES");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamWavetableTab(tab1);
        populateParamWavetableFilterTab(tab2);
    } else if (engineType == 9) {
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 550);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "LIBRARY");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "SYNTHESIS");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamSoundFontLibraryTab(tab1);
        populateParamSoundFontSynthTab(tab2);
    } else if (engineType == 8) {
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 550);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "AUDIO INPUT");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "FILTER & ENVELOPE");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamAudioInTab(tab1);
        populateParamAudioInFilterEnvTab(tab2);
    } else if (engineType == 5) {
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 550);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "MAIN DRUMS");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "CYMBALS & PERC");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamFmDrumTab1(tab1);
        populateParamFmDrumTab2(tab2);
    } else if (engineType == 6) {
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, 790, 550);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "MAIN DRUMS");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "CYMBALS & HATS");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamAnalogDrumTab1(tab1);
        populateParamAnalogDrumTab2(tab2);
    }

    if (mParamTabview) {
        lv_tabview_set_active(mParamTabview, mParamActiveTabIdx, LV_ANIM_OFF);
    }

    // Create floating action bar container in the bottom right corner of mCenterArea
    lv_obj_t* actionBar = lv_obj_create(mCenterArea);
    lv_obj_set_size(actionBar, 460, 46);
    lv_obj_align(actionBar, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_opa(actionBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionBar, 0, 0);
    lv_obj_set_style_pad_all(actionBar, 0, 0);
    lv_obj_remove_flag(actionBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(actionBar, LV_OBJ_FLAG_FLOATING);

    lv_obj_set_layout(actionBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actionBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Add MIDI LEARN button on the left of this row!
    lv_obj_t* learnBtn = lv_button_create(actionBar);
    lv_obj_set_size(learnBtn, 120, 36);
    if (mMidiLearnActive) {
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0xD32F2F), 0); // Active Red
        lv_obj_set_style_border_color(learnBtn, lv_color_hex(0xFF5252), 0);
    } else {
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(learnBtn, trackColor, 0);
    }
    lv_obj_set_style_border_width(learnBtn, 1, 0);
    lv_obj_set_style_radius(learnBtn, 6, 0);

    lv_obj_t* learnLbl = lv_label_create(learnBtn);
    mMidiLearnBtnLabel = learnLbl;
    if (mMidiLearnActive) {
        if (mMidiLearnTargetParamId >= 0) {
            lv_label_set_text(learnLbl, "TAP & WIGGLE");
        } else {
            lv_label_set_text(learnLbl, "TAP PARAMETER");
        }
    } else {
        lv_label_set_text(learnLbl, "MIDI LEARN");
    }
    lv_obj_set_style_text_font(learnLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(learnLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(learnLbl);

    auto learnClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->mMidiLearnActive = !ui->mMidiLearnActive;
        if (!ui->mMidiLearnActive) {
            ui->mMidiLearnTargetParamId = -1;
        }
        ui->createCenterContentArea(); // Rebuild center content area to update colors and state
    };
    lv_obj_add_event_cb(learnBtn, learnClickCb, LV_EVENT_CLICKED, this);

    auto createActionBtn = [this, trackColor](lv_obj_t* parent, const char* text, int width, lv_event_cb_t cb) -> lv_obj_t* {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, width, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_border_color(btn, trackColor, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
        return btn;
    };

    createActionBtn(actionBar, "DEFAULT", 80, UIManager::defaultPatchBtnEventCb);
    createActionBtn(actionBar, "LOAD", 65, UIManager::loadPatchBtnEventCb);
    createActionBtn(actionBar, "SAVE", 65, UIManager::savePatchBtnEventCb);
    createActionBtn(actionBar, "RANDOM", 85, UIManager::randomizeParamsBtnEventCb);

    // Disable horizontal swipe-to-switch-tabs on the Param tabview content container
    if (lv_obj_get_child_cnt(mCenterArea) > 0) {
        lv_obj_t* firstChild = lv_obj_get_child(mCenterArea, 0);
        if (firstChild) {
            lv_obj_t* content = lv_tabview_get_content(firstChild);
            if (content) {
                lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
            }
        }
    }
}

void UIManager::populateParamSubtractiveOscTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createSynthCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 410);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    auto createKnobGrid = [](lv_obj_t* parent) -> lv_obj_t* {
        lv_obj_t* grid = lv_obj_create(parent);
        lv_obj_set_size(grid, 160, 240);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
        lv_obj_set_style_pad_row(grid, 12, 0);
        lv_obj_set_style_pad_column(grid, 4, 0);
        return grid;
    };

    // --- OSC 1 ---
    lv_obj_t* card1 = createSynthCard(tab, "OSCILLATOR 1", 182);
    lv_obj_t* grid1 = createKnobGrid(card1);
    addSynthKnob(grid1, "PITCH", 160, 0.0f, 1.0f, 2, false);
    addSynthKnob(grid1, "MORPH", 104, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid1, "DRIVE", 170, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid1, "FOLD", 180, 0.0f, 1.0f, 2, true);
    addSynthKnob(card1, "VOLUME", 107, 0.0f, 1.0f, 2, true);

    // --- OSC 2 ---
    lv_obj_t* card2 = createSynthCard(tab, "OSCILLATOR 2", 182);
    lv_obj_t* grid2 = createKnobGrid(card2);
    addSynthKnob(grid2, "PITCH", 161, 0.0f, 1.0f, 2, false);
    addSynthKnob(grid2, "MORPH", 105, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid2, "DRIVE", 171, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid2, "FOLD", 181, 0.0f, 1.0f, 2, true);
    addSynthKnob(card2, "VOLUME", 108, 0.0f, 1.0f, 2, true);

    // --- SUB OSC ---
    lv_obj_t* card3 = createSynthCard(tab, "SUB OSCILLATOR", 182);
    lv_obj_t* grid3 = createKnobGrid(card3);
    addSynthKnob(grid3, "PITCH", 162, 0.0f, 1.0f, 2, false);
    addSynthKnob(grid3, "MORPH", 155, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid3, "DRIVE", 172, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid3, "FOLD", 182, 0.0f, 1.0f, 2, true);
    addSynthKnob(card3, "VOLUME", 109, 0.0f, 1.0f, 2, true);

    // --- TUNE & UTILITIES ---
    lv_obj_t* card4 = createSynthCard(tab, "TUNE & UTILITIES", 182);
    lv_obj_t* grid4 = createKnobGrid(card4);
    addSynthKnob(grid4, "DETUNE", 106, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid4, "NOISE VOL", 110, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid4, "GLIDE", 355, 0.0f, 1.0f, 2, true);

    // Flex switches row inside card 4
    lv_obj_t* swRow = lv_obj_create(card4);
    lv_obj_set_size(swRow, 160, 60);
    lv_obj_set_style_bg_opa(swRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swRow, 0, 0);
    lv_obj_set_style_pad_all(swRow, 0, 0);
    lv_obj_remove_flag(swRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(swRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(swRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(swRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto addSynthSwitch = [this, trackColor](lv_obj_t* parent, const char* labelText, int paramId) {
        lv_obj_t* cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 72, 55);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* lbl = lv_label_create(cont);
        lv_label_set_text(lbl, labelText);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        
        lv_obj_t* sw = lv_switch_create(cont);
        lv_obj_set_size(sw, 45, 22);
        
        // Grab value from AudioEngine
        float currentVal = mEngine.getTracks()[mActiveTrack].parameters[paramId];
        if (currentVal > 0.5f) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_set_style_bg_color(sw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
        
        SynthParamData* d = new SynthParamData{this, paramId, nullptr, 0.0f, 1.0f, -2, false};
        lv_obj_add_event_cb(sw, UIManager::synthParamSliderEventCb, LV_EVENT_VALUE_CHANGED, d);
        
        auto freeCb = [](lv_event_t* e) {
            SynthParamData* data = (SynthParamData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(sw, freeCb, LV_EVENT_DELETE, d);
    };

    addSynthSwitch(swRow, "SYNC", 150);
    addSynthSwitch(swRow, "RING MOD", 151);
}

void UIManager::populateParamSubtractiveFilterTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createFilterCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 410);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 15, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    // --- STATE VARIABLE FILTER CARD ---
    lv_obj_t* filterCard = createFilterCard(tab, "STATE VARIABLE FILTER", 365);
    
    // Add dropdown at top
    addSynthDropdown(filterCard, "FILTER MODE", 157, "LowPass\nHighPass\nBandPass\nNotch\nPeak", 0, true);

    // Row for the 3 knobs below
    lv_obj_t* knobRow1 = lv_obj_create(filterCard);
    lv_obj_set_size(knobRow1, 335, 120);
    lv_obj_set_style_bg_opa(knobRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobRow1, 0, 0);
    lv_obj_set_style_pad_all(knobRow1, 0, 0);
    lv_obj_remove_flag(knobRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobRow1, "CUTOFF", 1, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobRow1, "RESONANCE", 2, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobRow1, "ENV AMOUNT", 118, 0.0f, 1.0f, 2, true);

    // Spacer or filler to balance vertical layouts
    lv_obj_t* spacer = lv_obj_create(filterCard);
    lv_obj_set_size(spacer, 10, 40);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    // --- SYNTH LFO CARD ---
    lv_obj_t* lfoCard = createFilterCard(tab, "SYNTH LFO", 365);

    // Flex row for shape and destination dropdowns side-by-side
    lv_obj_t* ddRow = lv_obj_create(lfoCard);
    lv_obj_set_size(ddRow, 335, 60);
    lv_obj_set_style_bg_opa(ddRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ddRow, 0, 0);
    lv_obj_set_style_pad_all(ddRow, 0, 0);
    lv_obj_remove_flag(ddRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ddRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ddRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ddRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Add Shape dropdown
    lv_obj_t* shapeCont = lv_obj_create(ddRow);
    lv_obj_set_size(shapeCont, 160, 52);
    lv_obj_set_style_bg_opa(shapeCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shapeCont, 0, 0);
    lv_obj_set_style_pad_all(shapeCont, 0, 0);
    addSynthDropdown(shapeCont, "LFO SHAPE", 154, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false);

    // Add Destination dropdown
    lv_obj_t* destCont = lv_obj_create(ddRow);
    lv_obj_set_size(destCont, 160, 52);
    lv_obj_set_style_bg_opa(destCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(destCont, 0, 0);
    lv_obj_set_style_pad_all(destCont, 0, 0);
    addSynthDropdown(destCont, "LFO DESTINATION", 153, "Cutoff\nPitch\nMorph 1\nMorph 2\nFold 1\nFold 2\nVol 1\nVol 2", 0, false);

    // Row for the 2 LFO knobs below
    lv_obj_t* knobRow2 = lv_obj_create(lfoCard);
    lv_obj_set_size(knobRow2, 335, 120);
    lv_obj_set_style_bg_opa(knobRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobRow2, 0, 0);
    lv_obj_set_style_pad_all(knobRow2, 0, 0);
    lv_obj_remove_flag(knobRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobRow2, "LFO RATE", 7, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobRow2, "LFO DEPTH", 8, 0.0f, 1.0f, 2, true);

    // Spacer or filler to balance vertical layouts
    lv_obj_t* spacer2 = lv_obj_create(lfoCard);
    lv_obj_set_size(spacer2, 10, 40);
    lv_obj_set_style_bg_opa(spacer2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer2, 0, 0);
}

void UIManager::populateParamSubtractiveEnvTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createEnvCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 418);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    // --- AMPLITUDE ENVELOPE CARD ---
    lv_obj_t* ampCard = createEnvCard(tab, "AMPLITUDE ENVELOPE", 365);

    // Bypass Env row at the top
    lv_obj_t* bypassRow = lv_obj_create(ampCard);
    lv_obj_set_size(bypassRow, 330, 30);
    lv_obj_set_style_bg_opa(bypassRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bypassRow, 0, 0);
    lv_obj_set_style_pad_all(bypassRow, 0, 0);
    lv_obj_remove_flag(bypassRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bypassRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bypassRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bypassRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bypassRow, 8, 0);

    lv_obj_t* bpLabel = lv_label_create(bypassRow);
    lv_label_set_text(bpLabel, "USE AMP ENVELOPE");
    lv_obj_set_style_text_font(bpLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bpLabel, lv_color_hex(0x888888), 0);

    lv_obj_t* bpSw = lv_switch_create(bypassRow);
    lv_obj_set_size(bpSw, 40, 20);
    
    // Grab value from parameters ID 350
    float useEnvVal = mEngine.getTracks()[mActiveTrack].parameters[350];
    if (useEnvVal > 0.5f) {
        lv_obj_add_state(bpSw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(bpSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);

    SynthParamData* bpData = new SynthParamData{this, 350, nullptr, 0.0f, 1.0f, -2, false};
    lv_obj_add_event_cb(bpSw, UIManager::synthParamSliderEventCb, LV_EVENT_VALUE_CHANGED, bpData);
    lv_obj_add_event_cb(bpSw, [](lv_event_t* e) {
        SynthParamData* data = (SynthParamData*)lv_event_get_user_data(e);
        delete data;
    }, LV_EVENT_DELETE, bpData);

    // Row for the 4 ADSR sliders
    lv_obj_t* faderRow1 = lv_obj_create(ampCard);
    lv_obj_set_size(faderRow1, 335, 330);
    lv_obj_set_style_bg_opa(faderRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow1, 0, 0);
    lv_obj_set_style_pad_all(faderRow1, 0, 0);
    lv_obj_remove_flag(faderRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow1, "A", 100, 0.001f, 4.0f, 2, false);
    addSynthSlider(faderRow1, "D", 101, 0.0f, 4.0f, 2, false);
    addSynthSlider(faderRow1, "S", 102, 0.0f, 1.0f, 2, true);
    addSynthSlider(faderRow1, "R", 103, 0.001f, 4.0f, 2, false);

    // --- FILTER ENVELOPE CARD ---
    lv_obj_t* filterCard = createEnvCard(tab, "FILTER ENVELOPE", 365);

    // Row for the 4 filter ADSR sliders
    lv_obj_t* faderRow2 = lv_obj_create(filterCard);
    lv_obj_set_size(faderRow2, 335, 350);
    lv_obj_set_style_bg_opa(faderRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow2, 0, 0);
    lv_obj_set_style_pad_all(faderRow2, 0, 0);
    lv_obj_remove_flag(faderRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow2, "A", 114, 0.001f, 4.0f, 2, false);
    addSynthSlider(faderRow2, "D", 115, 0.0f, 4.0f, 2, false);
    addSynthSlider(faderRow2, "S", 116, 0.0f, 1.0f, 2, true);
    addSynthSlider(faderRow2, "R", 117, 0.001f, 4.0f, 2, false);
}

void UIManager::addSynthKnob(lv_obj_t* parent, const char* labelText, int paramId, float minVal, float maxVal, int decimals, bool isPercent) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, 74, 95);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_top(container, 3, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Mini Arc
    lv_obj_t* arc = lv_arc_create(container);
    lv_obj_set_size(arc, 58, 58);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 1000);
    
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    
    // Get accent color of the selected track
    lv_color_t accentColor = getTrackColor(mActiveTrack);
    lv_obj_set_style_arc_color(arc, accentColor, LV_PART_INDICATOR);

    // Retrieve initial value from AudioEngine Track Parameters
    float currentVal = mEngine.getTracks()[mActiveTrack].parameters[paramId];

    // Scale to range [0, 1000] for LVGL arc
    int arcVal = 0;
    if (maxVal > minVal) {
        float norm = mapNonLinearToLinear(currentVal, minVal, maxVal, labelText);
        arcVal = (int)(norm * 1000.0f);
    }
    lv_arc_set_value(arc, arcVal);

    // Value Label inside the arc
    lv_obj_t* valLbl = lv_label_create(arc);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(valLbl, lv_color_hex(0xCCCCCC), 0);
    
    if (isPercent) {
        // Display value in percentage
        int pct = (int)(currentVal * 100.0f + 0.5f);
        lv_label_set_text_fmt(valLbl, "%d%%", pct);
    } else if (paramId == 300) {
        int st = (int)roundf((currentVal - 0.5f) * 48.0f);
        lv_label_set_text_fmt(valLbl, "%+dst", st);
    } else if (paramId == 301) {
        lv_label_set_text_fmt(valLbl, "%.2fx", currentVal * 4.0f);
    } else if (paramId == 302) {
        float speed = powf(currentVal, 3.0f) * 9.99f + 0.01f;
        lv_label_set_text_fmt(valLbl, "%.2fx", speed);
    } else if (paramId == 340 || paramId == 341) {
        lv_label_set_text_fmt(valLbl, "%d", (int)(currentVal * 15.0f) + 1);
    } else if (paramId == 320) {
        const char* modeStr = "1-HIT";
        if (currentVal < 0.16f) modeStr = "1-HIT";
        else if (currentVal < 0.33f) modeStr = "SUSTN";
        else if (currentVal < 0.50f) modeStr = "LOOP";
        else if (currentVal < 0.66f) modeStr = "CHOP";
        else if (currentVal < 0.83f) modeStr = "1-CHP";
        else if (currentVal < 0.95f) modeStr = "L-CHP";
        else modeStr = "SCRUB";
        lv_label_set_text(valLbl, modeStr);
    } else if (paramId == 150) {
        int algo = (int)(currentVal * 31.99f);
        lv_label_set_text_fmt(valLbl, "%d", algo);
    } else if (paramId == 418) {
        int count = (int)(currentVal * 95.0f + 5.0f);
        lv_label_set_text_fmt(valLbl, "%d", count);
    } else if (paramId >= 165 && paramId <= 195 && (paramId - 160) % 6 == 5) {
        lv_label_set_text_fmt(valLbl, "%.1fx", currentVal * 16.0f);
    } else {
        if (paramId == 160 || paramId == 161 || paramId == 162) {
            // Display Pitch multiplier nicely (x1.00, x2.00, etc.)
            float mult = currentVal;
            if (paramId == 162) mult *= 2.0f;
            else mult *= 4.0f;
            lv_label_set_text_fmt(valLbl, "x%.2f", mult);
        } else {
            if (decimals == 0) {
                lv_label_set_text_fmt(valLbl, "%d", (int)currentVal);
            } else if (decimals == 1) {
                lv_label_set_text_fmt(valLbl, "%.1f", currentVal);
            } else {
                lv_label_set_text_fmt(valLbl, "%.2f", currentVal);
            }
        }
    }
    lv_obj_center(valLbl);

    // Title label at the bottom
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    // Setup Event Callback Data
    SynthParamData* data = new SynthParamData{this, paramId, valLbl, minVal, maxVal, decimals, isPercent, labelText};
    lv_obj_add_event_cb(arc, UIManager::synthParamSliderEventCb, LV_EVENT_VALUE_CHANGED, data);
    lv_obj_add_event_cb(arc, UIManager::paramMidiLearnClickEventCb, LV_EVENT_PRESSED, data);

    mActiveParamWidgets.push_back(ParamWidgetTracking{paramId, arc, valLbl, minVal, maxVal, decimals, isPercent, labelText});

    auto freeCb = [](lv_event_t* e) {
        SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(arc, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::addSynthSlider(lv_obj_t* parent, const char* labelText, int paramId, float minVal, float maxVal, int decimals, bool isPercent, int height) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, 74, height);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Value label at the top
    lv_obj_t* valLbl = lv_label_create(container);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(valLbl, lv_color_hex(0xCCCCCC), 0);

    // Retrieve initial value from AudioEngine Track Parameters
    float currentVal = mEngine.getTracks()[mActiveTrack].parameters[paramId];

    if (isPercent) {
        int pct = (int)(currentVal * 100.0f + 0.5f);
        lv_label_set_text_fmt(valLbl, "%d%%", pct);
    } else {
        if (decimals == 0) {
            lv_label_set_text_fmt(valLbl, "%d", (int)currentVal);
        } else if (decimals == 1) {
            lv_label_set_text_fmt(valLbl, "%.1f", currentVal);
        } else {
            lv_label_set_text_fmt(valLbl, "%.2f", currentVal);
        }
    }

    // Vertical Slider
    lv_obj_t* slider = lv_slider_create(container);
    lv_obj_set_size(slider, 14, height - 80);
    lv_slider_set_range(slider, 0, 1000);
    
    // Get accent color of the selected track
    lv_color_t accentColor = getTrackColor(mActiveTrack);
    lv_obj_set_style_bg_color(slider, accentColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
    
    // Position knob (small track-colored knob)
    lv_obj_set_style_bg_color(slider, accentColor, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 6, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 2, LV_PART_KNOB);

    // Scale to range [0, 1000] for LVGL slider
    int sliderVal = 0;
    if (maxVal > minVal) {
        float norm = mapNonLinearToLinear(currentVal, minVal, maxVal, labelText);
        sliderVal = (int)(norm * 1000.0f);
    }
    lv_slider_set_value(slider, sliderVal, LV_ANIM_OFF);

    // Title label at the bottom
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    // Setup Event Callback Data
    SynthParamData* data = new SynthParamData{this, paramId, valLbl, minVal, maxVal, decimals, isPercent, labelText};
    lv_obj_add_event_cb(slider, UIManager::synthParamSliderEventCb, LV_EVENT_VALUE_CHANGED, data);
    lv_obj_add_event_cb(slider, UIManager::paramMidiLearnClickEventCb, LV_EVENT_PRESSED, data);

    mActiveParamWidgets.push_back(ParamWidgetTracking{paramId, slider, valLbl, minVal, maxVal, decimals, isPercent, labelText});

    auto freeCb = [](lv_event_t* e) {
        SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(slider, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::addSynthDropdown(lv_obj_t* parent, const char* labelText, int paramId, const char* options, int initialSel, bool isFilterMode, int width) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_size(container, width, 52);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Label at top
    lv_obj_t* lbl = lv_label_create(container);
    lv_label_set_text(lbl, labelText);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);

    // Dropdown
    lv_obj_t* dd = lv_dropdown_create(container);
    lv_obj_set_size(dd, width, 32);
    lv_dropdown_set_options(dd, options);
    
    // Style dropdown
    lv_color_t accentColor = getTrackColor(mActiveTrack);
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x242424), 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x3E3E3E), 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 6, 0);
    lv_obj_set_style_text_color(dd, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_10, 0);
    
    // Set selected index
    int selected = initialSel;
    if (paramId >= 0 && paramId < 2500) {
        if (isFilterMode) {
            selected = mEngine.getTracks()[mActiveTrack].subtractiveEngine.getFilterMode();
        } else {
            selected = (int)mEngine.getTracks()[mActiveTrack].parameters[paramId];
        }
    }
    lv_dropdown_set_selected(dd, selected);

    // Setup Callback Data
    SynthParamData* data = new SynthParamData{this, paramId, nullptr, 0.0f, 10.0f, isFilterMode ? -4 : -3, false};
    lv_obj_add_event_cb(dd, UIManager::synthParamDropdownEventCb, LV_EVENT_VALUE_CHANGED, data);

    auto freeCb = [](lv_event_t* e) {
        SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(dd, freeCb, LV_EVENT_DELETE, data);
}

void UIManager::synthParamSliderEventCb(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
    if (!d) return;

    if (d->ui->mActiveMacroLearnIdx >= 0) {
        int macroIdx = d->ui->mActiveMacroLearnIdx;
        d->ui->assignMacroSourceLearned(macroIdx, d->paramId);
        return;
    }

    float rawVal = 0.0f;
    if (d->decimals == -2) {
        // Switch
        rawVal = lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1.0f : 0.0f;
    } else {
        // Slider or Arc (0 to 1000)
        int v = 0;
        if (lv_obj_check_type(obj, &lv_arc_class)) {
            v = lv_arc_get_value(obj);
        } else {
            v = lv_slider_get_value(obj);
        }
        float norm = (float)v / 1000.0f;
        rawVal = mapLinearToNonLinear(norm, d->minVal, d->maxVal, d->labelText);
    }

    // Set parameter in Engine
    d->ui->mEngine.setParameter(d->ui->mActiveTrack, d->paramId, rawVal);

    // Update label text
    if (d->valLabel) {
        if (d->isPercent) {
            int pct = (int)(rawVal * 100.0f + 0.5f);
            lv_label_set_text_fmt(d->valLabel, "%d%%", pct);
        } else if (d->paramId == 300) {
            int st = (int)roundf((rawVal - 0.5f) * 48.0f);
            lv_label_set_text_fmt(d->valLabel, "%+dst", st);
        } else if (d->paramId == 301) {
            lv_label_set_text_fmt(d->valLabel, "%.2fx", rawVal * 4.0f);
        } else if (d->paramId == 302) {
            float speed = powf(rawVal, 3.0f) * 9.99f + 0.01f;
            lv_label_set_text_fmt(d->valLabel, "%.2fx", speed);
        } else if (d->paramId == 340 || d->paramId == 341) {
            lv_label_set_text_fmt(d->valLabel, "%d", (int)(rawVal * 15.0f) + 1);
        } else if (d->paramId == 320) {
            const char* modeStr = "1-HIT";
            if (rawVal < 0.16f) modeStr = "1-HIT";
            else if (rawVal < 0.33f) modeStr = "SUSTN";
            else if (rawVal < 0.50f) modeStr = "LOOP";
            else if (rawVal < 0.66f) modeStr = "CHOP";
            else if (rawVal < 0.83f) modeStr = "1-CHP";
            else if (rawVal < 0.95f) modeStr = "L-CHP";
            else modeStr = "SCRUB";
            lv_label_set_text(d->valLabel, modeStr);
        } else if (d->paramId == 150) {
            int algo = (int)(rawVal * 31.99f);
            lv_label_set_text_fmt(d->valLabel, "%d", algo);
        } else if (d->paramId == 418) {
            int count = (int)(rawVal * 95.0f + 5.0f);
            lv_label_set_text_fmt(d->valLabel, "%d", count);
        } else if (d->paramId >= 165 && d->paramId <= 195 && (d->paramId - 160) % 6 == 5) {
            lv_label_set_text_fmt(d->valLabel, "%.1fx", rawVal * 16.0f);
        } else {
            if (d->paramId == 160 || d->paramId == 161 || d->paramId == 162) {
                float mult = rawVal;
                if (d->paramId == 162) mult *= 2.0f;
                else mult *= 4.0f;
                lv_label_set_text_fmt(d->valLabel, "x%.2f", mult);
            } else {
                if (d->decimals == 0) {
                    lv_label_set_text_fmt(d->valLabel, "%d", (int)rawVal);
                } else if (d->decimals == 1) {
                    lv_label_set_text_fmt(d->valLabel, "%.1f", rawVal);
                } else {
                    lv_label_set_text_fmt(d->valLabel, "%.2f", rawVal);
                }
            }
        }
    }
}

void UIManager::synthParamDropdownEventCb(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
    if (!d) return;

    if (d->ui->mActiveMacroLearnIdx >= 0) {
        int macroIdx = d->ui->mActiveMacroLearnIdx;
        d->ui->assignMacroSourceLearned(macroIdx, d->paramId);
        return;
    }

    int sel = lv_dropdown_get_selected(obj);

    if (d->decimals == -4) {
        // Filter Mode
        d->ui->mEngine.setFilterMode(d->ui->mActiveTrack, sel);
        // Persist filter mode parameter to ID 157
        d->ui->mEngine.setParameter(d->ui->mActiveTrack, 157, (float)sel);
        std::cout << "SynthParamDropdown: Track " << d->ui->mActiveTrack + 1 << " Filter Mode set to " << sel << std::endl;
    } else {
        // Standard synthesis dropdown
        d->ui->mEngine.setParameter(d->ui->mActiveTrack, d->paramId, (float)sel);
        std::cout << "SynthParamDropdown: Track " << d->ui->mActiveTrack + 1 << " Param " << d->paramId << " set to " << sel << std::endl;
    }
}

// =========================================================================
// --- FM Synthesis Parameters Screen Tab Implementation ---
// =========================================================================

void UIManager::populateParamFmTab(lv_obj_t* tab1, lv_obj_t* tab2, lv_obj_t* tab3) {
    populateParamFmOperatorsTab(tab1);
    populateParamFmRoutingTab(tab2);
    populateParamFmFilterTab(tab3);
}

void UIManager::populateParamFmOperatorsTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 6-operator block buttons container
    lv_obj_t* gridRow = lv_obj_create(tab);
    lv_obj_set_size(gridRow, 760, 75);
    lv_obj_set_style_bg_opa(gridRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gridRow, 0, 0);
    lv_obj_set_style_pad_all(gridRow, 0, 0);
    lv_obj_remove_flag(gridRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(gridRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gridRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int activeMask = (int)mEngine.getTracks()[mActiveTrack].parameters[155];
    int carrierMask = (int)mEngine.getTracks()[mActiveTrack].parameters[153];

    for (int i = 0; i < 6; ++i) {
        bool isActive = (activeMask & (1 << i)) != 0;
        bool isCarrier = (carrierMask & (1 << i)) != 0;

        lv_color_t btnColor;
        if (!isActive) {
            btnColor = lv_color_hex(0x242424); // Dark Gray (Off)
        } else if (!isCarrier) {
            btnColor = lv_color_hex(0x5E35B1); // Purple (Modulator)
        } else {
            btnColor = trackColor; // Accent Color (Carrier)
        }

        lv_obj_t* opBtn = lv_button_create(gridRow);
        lv_obj_set_size(opBtn, 115, 65);
        lv_obj_set_style_bg_color(opBtn, btnColor, 0);
        lv_obj_set_style_radius(opBtn, 8, 0);

        if (i == mSelectedOpIdx) {
            lv_obj_set_style_border_color(opBtn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_width(opBtn, 2, 0);
        } else {
            lv_obj_set_style_border_width(opBtn, 0, 0);
        }

        lv_obj_t* opLbl = lv_label_create(opBtn);
        lv_label_set_text_fmt(opLbl, "OP %d", i + 1);
        lv_obj_set_style_text_font(opLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(opLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(opLbl, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t* stateLbl = lv_label_create(opBtn);
        const char* stateStr = !isActive ? "OFF" : (!isCarrier ? "MODULATOR" : "CARRIER");
        lv_label_set_text(stateLbl, stateStr);
        lv_obj_set_style_text_font(stateLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(stateLbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_align(stateLbl, LV_ALIGN_BOTTOM_MID, 0, -8);

        FmOpClickData* clickData = new FmOpClickData{this, i, tab};
        lv_obj_add_event_cb(opBtn, UIManager::fmOpBlockEventCb, LV_EVENT_CLICKED, clickData);
        lv_obj_add_event_cb(opBtn, [](lv_event_t* e) {
            FmOpClickData* d = (FmOpClickData*)lv_event_get_user_data(e);
            delete d;
        }, LV_EVENT_DELETE, clickData);
    }

    // Detail card for the selected operator
    lv_obj_t* detailCard = lv_obj_create(tab);
    lv_obj_set_size(detailCard, 760, 315);
    lv_obj_set_style_bg_color(detailCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(detailCard, LV_OPA_90, 0);
    lv_obj_set_style_border_color(detailCard, trackColor, 0);
    lv_obj_set_style_border_width(detailCard, 2, 0);
    lv_obj_set_style_radius(detailCard, 12, 0);
    lv_obj_set_style_pad_all(detailCard, 10, 0);
    lv_obj_remove_flag(detailCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(detailCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(detailCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detailCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left Column: Selector & Mode Dropdown
    lv_obj_t* leftCol = lv_obj_create(detailCard);
    lv_obj_set_size(leftCol, 180, 285);
    lv_obj_set_style_bg_opa(leftCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftCol, 0, 0);
    lv_obj_set_style_pad_all(leftCol, 5, 0);
    lv_obj_remove_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* detTitle = lv_label_create(leftCol);
    lv_label_set_text_fmt(detTitle, "OPERATOR %d", mSelectedOpIdx + 1);
    lv_obj_set_style_text_font(detTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(detTitle, trackColor, 0);

    bool selActive = (activeMask & (1 << mSelectedOpIdx)) != 0;
    bool selCarrier = (carrierMask & (1 << mSelectedOpIdx)) != 0;
    int currentSel = !selActive ? 0 : (!selCarrier ? 1 : 2);

    lv_obj_t* stLbl = lv_label_create(leftCol);
    const char* stStr = (currentSel == 0) ? "STATE: OFF" : ((currentSel == 1) ? "STATE: MODULATOR" : "STATE: CARRIER");
    lv_label_set_text(stLbl, stStr);
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(stLbl, lv_color_hex(0x888888), 0);

    lv_obj_t* ddLbl = lv_label_create(leftCol);
    lv_label_set_text(ddLbl, "SELECT MODE");
    lv_obj_set_style_text_font(ddLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ddLbl, lv_color_hex(0x888888), 0);

    lv_obj_t* modeDd = lv_dropdown_create(leftCol);
    lv_obj_set_size(modeDd, 150, 32);
    lv_dropdown_set_options(modeDd, "Off\nModulator\nCarrier");
    lv_dropdown_set_selected(modeDd, currentSel);

    lv_obj_set_style_bg_color(modeDd, lv_color_hex(0x242424), 0);
    lv_obj_set_style_border_color(modeDd, lv_color_hex(0x3E3E3E), 0);
    lv_obj_set_style_border_width(modeDd, 1, 0);
    lv_obj_set_style_radius(modeDd, 6, 0);
    lv_obj_set_style_text_color(modeDd, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_text_font(modeDd, &lv_font_montserrat_10, 0);

    FmOpClickData* ddData = new FmOpClickData{this, mSelectedOpIdx, tab};
    lv_obj_add_event_cb(modeDd, UIManager::fmOpModeDropdownEventCb, LV_EVENT_VALUE_CHANGED, ddData);
    lv_obj_add_event_cb(modeDd, [](lv_event_t* e) {
        FmOpClickData* d = (FmOpClickData*)lv_event_get_user_data(e);
        delete d;
    }, LV_EVENT_DELETE, ddData);

    // Middle Column: ADSR Sliders
    lv_obj_t* midCol = lv_obj_create(detailCard);
    lv_obj_set_size(midCol, 335, 285);
    lv_obj_set_style_bg_opa(midCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(midCol, 0, 0);
    lv_obj_set_style_pad_all(midCol, 0, 0);
    lv_obj_remove_flag(midCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(midCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(midCol, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(midCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int base = 160 + mSelectedOpIdx * 6;
    addSynthSlider(midCol, "A", base + 1, 0.001f, 4.0f, 2, false);
    addSynthSlider(midCol, "D", base + 2, 0.0f, 4.0f, 2, false);
    addSynthSlider(midCol, "S", base + 3, 0.0f, 1.0f, 2, true);
    addSynthSlider(midCol, "R", base + 4, 0.001f, 4.0f, 2, false);

    // Right Column: Level & Ratio Knobs
    lv_obj_t* rightCol = lv_obj_create(detailCard);
    lv_obj_set_size(rightCol, 180, 285);
    lv_obj_set_style_bg_opa(rightCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightCol, 0, 0);
    lv_obj_set_style_pad_all(rightCol, 0, 0);
    lv_obj_remove_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(rightCol, "LEVEL", base + 0, 0.0f, 1.0f, 2, true);
    addSynthKnob(rightCol, "RATIO", base + 5, 0.0f, 1.0f, 1, false);
}

void UIManager::populateParamFmRoutingTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createRoutingCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 410);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    // --- PRESET MANAGEMENT CARD ---
    lv_obj_t* leftCard = createRoutingCard(tab, "PRESET MANAGEMENT", 365);

    int currentSel = s_activeFmPreset[mActiveTrack];
    const auto& custom = mEngine.getTracks()[mActiveTrack].fmEngine.mCustomPresets;
    int totalCount = 32 + (int)custom.size();
    if (currentSel >= totalCount) {
        currentSel = 0;
        s_activeFmPreset[mActiveTrack] = 0;
    }
    std::string currentPresetName = (currentSel < 32) ? FM_PRESET_NAMES[currentSel] : (currentSel - 32 < (int)custom.size() ? custom[currentSel - 32].name : "Unknown");

    mFmActivePresetLbl = lv_label_create(leftCard);
    lv_label_set_text_fmt(mFmActivePresetLbl, "PRESET: %d - %s", currentSel, currentPresetName.c_str());
    lv_obj_set_style_text_font(mFmActivePresetLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mFmActivePresetLbl, trackColor, 0);
    lv_label_set_long_mode(mFmActivePresetLbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(mFmActivePresetLbl, 330);

    // Select Preset Button
    lv_obj_t* selectPresetBtn = lv_button_create(leftCard);
    lv_obj_set_size(selectPresetBtn, 330, 40);
    lv_obj_set_style_bg_color(selectPresetBtn, lv_color_hex(0x242424), 0);
    lv_obj_set_style_border_color(selectPresetBtn, lv_color_hex(0x3E3E3E), 0);
    lv_obj_set_style_border_width(selectPresetBtn, 1, 0);
    lv_obj_set_style_radius(selectPresetBtn, 8, 0);

    lv_obj_t* selectPresetLbl = lv_label_create(selectPresetBtn);
    lv_label_set_text(selectPresetLbl, "SELECT PRESET");
    lv_obj_set_style_text_font(selectPresetLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(selectPresetLbl, lv_color_hex(0xEEEEEE), 0);
    lv_obj_center(selectPresetLbl);

    lv_obj_add_event_cb(selectPresetBtn, UIManager::fmPresetSelectCb, LV_EVENT_CLICKED, this);

    // Import Button
    lv_obj_t* importBtn = lv_button_create(leftCard);
    lv_obj_set_size(importBtn, 330, 45);
    lv_obj_set_style_bg_color(importBtn, trackColor, 0);
    lv_obj_set_style_radius(importBtn, 8, 0);

    lv_obj_t* importLbl = lv_label_create(importBtn);
    lv_label_set_text(importLbl, "IMPORT CUSTOM PRESET (.fmp, .syx)");
    lv_obj_set_style_text_font(importLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(importLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(importLbl);

    lv_obj_add_event_cb(importBtn, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        if (!ui) return;
        ui->mFileBrowserIsFmImport = true;
        ui->openFileBrowser(false);
    }, LV_EVENT_CLICKED, this);

    // Preset selection knob directly in Preset Management card
    addSynthKnob(leftCard, "PRESET", 196, 0.0f, 1.0f, 0, false);

    // --- ROUTING & ALGORITHM CARD ---
    lv_obj_t* rightCard = createRoutingCard(tab, "ROUTING & ALGORITHM", 365);

    lv_obj_t* row1 = lv_obj_create(rightCard);
    lv_obj_set_size(row1, 330, 110);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(row1, "ALGO", 150, 0.0f, 1.0f, 0, false);
    addSynthKnob(row1, "FEEDBACK", 154, 0.0f, 1.0f, 2, true);
    addSynthKnob(row1, "DRIVE", 159, 0.0f, 1.0f, 2, true);

    lv_obj_t* row2 = lv_obj_create(rightCard);
    lv_obj_set_size(row2, 330, 110);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_remove_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(row2, "BRIGHTNESS", 157, 0.0f, 1.0f, 2, true);
    addSynthKnob(row2, "GLIDE", 355, 0.0f, 1.0f, 2, false);
    addSynthKnob(row2, "PRESET", 196, 0.0f, 1.0f, 0, false);
}

void UIManager::populateParamFmFilterTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createFmCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 410);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    // --- FILTER CONFIGURATION CARD ---
    lv_obj_t* filterCard = createFmCard(tab, "FILTER", 172);

    lv_obj_t* knobCol = lv_obj_create(filterCard);
    lv_obj_set_size(knobCol, 160, 360);
    lv_obj_set_style_bg_opa(knobCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobCol, 0, 0);
    lv_obj_set_style_pad_all(knobCol, 0, 0);
    lv_obj_remove_flag(knobCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(knobCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobCol, "CUTOFF", 151, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobCol, "RES", 152, 0.0f, 1.0f, 2, true);

    addSynthDropdown(knobCol, "FILTER TYPE", 156, "Lowpass\nBandpass\nHighpass\nBypass", (int)mEngine.getTracks()[mActiveTrack].parameters[156], false);

    // --- FILTER ENVELOPE CARD ---
    lv_obj_t* filterEnvCard = createFmCard(tab, "FILTER ENVELOPE", 294);

    lv_obj_t* faderRow1 = lv_obj_create(filterEnvCard);
    lv_obj_set_size(faderRow1, 282, 280);
    lv_obj_set_style_bg_opa(faderRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow1, 0, 0);
    lv_obj_set_style_pad_all(faderRow1, 0, 0);
    lv_obj_remove_flag(faderRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow1, "A", 114, 0.001f, 4.0f, 2, false);
    addSynthSlider(faderRow1, "D", 115, 0.0f, 4.0f, 2, false);
    addSynthSlider(faderRow1, "S", 116, 0.0f, 1.0f, 2, true);
    addSynthSlider(faderRow1, "R", 117, 0.001f, 4.0f, 2, false);

    lv_obj_t* bottomAmt = lv_obj_create(filterEnvCard);
    lv_obj_set_size(bottomAmt, 282, 85);
    lv_obj_set_style_bg_opa(bottomAmt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomAmt, 0, 0);
    lv_obj_set_style_pad_all(bottomAmt, 0, 0);
    lv_obj_remove_flag(bottomAmt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomAmt, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomAmt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottomAmt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(bottomAmt, "ENV AMT", 118, 0.0f, 1.0f, 2, true);

    // --- AMPLITUDE ENVELOPE CARD ---
    lv_obj_t* ampEnvCard = createFmCard(tab, "AMPLITUDE ENVELOPE", 294);

    lv_obj_t* faderRow2 = lv_obj_create(ampEnvCard);
    lv_obj_set_size(faderRow2, 282, 280);
    lv_obj_set_style_bg_opa(faderRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow2, 0, 0);
    lv_obj_set_style_pad_all(faderRow2, 0, 0);
    lv_obj_remove_flag(faderRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow2, "A", 100, 0.001f, 4.0f, 2, false);
    addSynthSlider(faderRow2, "D", 101, 0.0f, 4.0f, 2, false);
    addSynthSlider(faderRow2, "S", 102, 0.0f, 1.0f, 2, true);
    addSynthSlider(faderRow2, "R", 103, 0.001f, 4.0f, 2, false);

    // Spacer block to keep symmetry
    lv_obj_t* spacer = lv_obj_create(ampEnvCard);
    lv_obj_set_size(spacer, 282, 85);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
}

void UIManager::fmOpBlockEventCb(lv_event_t* e) {
    FmOpClickData* data = (FmOpClickData*)lv_event_get_user_data(e);
    if (!data) return;

    UIManager* ui = data->ui;
    int opIdx = data->opIdx;
    lv_obj_t* tab = data->tab;

    if (ui->mSelectedOpIdx != opIdx) {
        ui->mSelectedOpIdx = opIdx;
    } else {
        // Cycle the state: Off -> Modulator -> Carrier -> Off
        int activeMask = (int)ui->mEngine.getTracks()[ui->mActiveTrack].parameters[155];
        int carrierMask = (int)ui->mEngine.getTracks()[ui->mActiveTrack].parameters[153];

        bool isActive = (activeMask & (1 << opIdx)) != 0;
        bool isCarrier = (carrierMask & (1 << opIdx)) != 0;

        if (!isActive) {
            // Off -> Modulator
            activeMask |= (1 << opIdx);
            carrierMask &= ~(1 << opIdx);
        } else if (!isCarrier) {
            // Modulator -> Carrier
            activeMask |= (1 << opIdx);
            carrierMask |= (1 << opIdx);
        } else {
            // Carrier -> Off
            activeMask &= ~(1 << opIdx);
            carrierMask &= ~(1 << opIdx);
        }

        ui->mEngine.setParameter(ui->mActiveTrack, 155, (float)activeMask);
        ui->mEngine.setParameter(ui->mActiveTrack, 153, (float)carrierMask);
    }

    // Safely request a full screen rebuild on the next frame to avoid use-after-free or event dispatch crash
    ui->mNeedsScreenRebuild = true;
}

void UIManager::fmOpModeDropdownEventCb(lv_event_t* e) {
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    FmOpClickData* data = (FmOpClickData*)lv_event_get_user_data(e);
    if (!data) return;

    UIManager* ui = data->ui;
    int opIdx = data->opIdx;
    lv_obj_t* tab = data->tab;

    int sel = lv_dropdown_get_selected(obj);

    int activeMask = (int)ui->mEngine.getTracks()[ui->mActiveTrack].parameters[155];
    int carrierMask = (int)ui->mEngine.getTracks()[ui->mActiveTrack].parameters[153];

    if (sel == 0) {
        // Off
        activeMask &= ~(1 << opIdx);
        carrierMask &= ~(1 << opIdx);
    } else if (sel == 1) {
        // Modulator
        activeMask |= (1 << opIdx);
        carrierMask &= ~(1 << opIdx);
    } else if (sel == 2) {
        // Carrier
        activeMask |= (1 << opIdx);
        carrierMask |= (1 << opIdx);
    }

    ui->mEngine.setParameter(ui->mActiveTrack, 155, (float)activeMask);
    ui->mEngine.setParameter(ui->mActiveTrack, 153, (float)carrierMask);

    // Safely request a full screen rebuild on the next frame to avoid use-after-free or event dispatch crash
    ui->mNeedsScreenRebuild = true;
}

// =========================================================================
// --- Wavetable Synthesis Parameters Screen Implementation ---
// =========================================================================

void UIManager::wtSelectBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsWtSelect = true;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;
    ui->openFileBrowser(false);
}

void UIManager::wtImportBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = true;
    ui->mFileBrowserIsFmImport = false;
    ui->openFileBrowser(false);
}

void UIManager::populateParamWavetableTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    // Row 1: Character & Unison/Lofi cards
    lv_obj_t* row1 = lv_obj_create(tab);
    lv_obj_set_size(row1, 760, 185);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createSmallCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 180);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    auto createKnobRow = [](lv_obj_t* parent) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, 355, 125);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return row;
    };

    // --- CHARACTER CARD ---
    lv_obj_t* charCard = createSmallCard(row1, "CHARACTER", 372);
    lv_obj_t* charKnobs = createKnobRow(charCard);
    addSynthKnob(charKnobs, "MORPH", 450, 0.0f, 1.0f, 2, true);
    addSynthKnob(charKnobs, "WARP", 465, -1.0f, 1.0f, 2, false);
    addSynthKnob(charKnobs, "CRUSH", 466, 0.0f, 1.0f, 2, true);
    addSynthKnob(charKnobs, "DRIVE", 467, 0.0f, 1.0f, 2, true);

    // --- UNISON & LOFI CARD ---
    lv_obj_t* unisonCard = createSmallCard(row1, "UNISON & LOFI", 372);
    lv_obj_t* unisonKnobs = createKnobRow(unisonCard);
    addSynthKnob(unisonKnobs, "DETUNE", 451, 0.0f, 1.0f, 2, true);
    addSynthKnob(unisonKnobs, "GLIDE", 355, 0.0f, 1.0f, 2, true);
    addSynthKnob(unisonKnobs, "BITRATE", 475, 0.0f, 1.0f, 2, true);
    addSynthKnob(unisonKnobs, "SAMPLERATE", 476, 0.0f, 1.0f, 2, true);

    // Row 2: Select Wavetable card
    lv_obj_t* selectCard = lv_obj_create(tab);
    lv_obj_set_size(selectCard, 760, 225);
    lv_obj_set_style_bg_color(selectCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(selectCard, LV_OPA_90, 0);
    lv_obj_set_style_border_color(selectCard, trackColor, 0);
    lv_obj_set_style_border_width(selectCard, 2, 0);
    lv_obj_set_style_radius(selectCard, 12, 0);
    lv_obj_set_style_pad_all(selectCard, 12, 0);
    lv_obj_remove_flag(selectCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(selectCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selectCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(selectCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* selectTitle = lv_label_create(selectCard);
    lv_label_set_text(selectTitle, "WAVETABLE SELECTION");
    lv_obj_set_style_text_font(selectTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(selectTitle, trackColor, 0);

    // Active wavetable text box
    lv_obj_t* activeBox = lv_obj_create(selectCard);
    lv_obj_set_size(activeBox, 720, 50);
    lv_obj_set_style_bg_color(activeBox, lv_color_hex(0x0F0F0F), 0);
    lv_obj_set_style_border_color(activeBox, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(activeBox, 1, 0);
    lv_obj_set_style_radius(activeBox, 8, 0);
    lv_obj_remove_flag(activeBox, LV_OBJ_FLAG_SCROLLABLE);

    mWtActiveNameLbl = lv_label_create(activeBox);
    std::string activeWt = mEngine.getTracks()[mActiveTrack].lastSamplePath;
    if (activeWt.empty()) {
        lv_label_set_text(mWtActiveNameLbl, "ACTIVE: (Default Sine)");
    } else {
        lv_label_set_text_fmt(mWtActiveNameLbl, "ACTIVE: %s", activeWt.c_str());
    }
    lv_obj_set_style_text_font(mWtActiveNameLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mWtActiveNameLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(mWtActiveNameLbl, LV_ALIGN_CENTER, 0, 0);

    // Action buttons row
    lv_obj_t* btnRow = lv_obj_create(selectCard);
    lv_obj_set_size(btnRow, 720, 56);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_remove_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto addActionButton = [this, trackColor](lv_obj_t* parent, const char* labelText, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, 210, 44);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(btn, trackColor, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labelText);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
    };

    addActionButton(btnRow, "SELECT WAVETABLE", wtSelectBtnEventCb);
    addActionButton(btnRow, "IMPORT WAV FILE", wtImportBtnEventCb);
    
    auto defaultCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->mEngine.loadDefaultWavetable(ui->mActiveTrack);
        ui->mEngine.getTracks()[ui->mActiveTrack].lastSamplePath = "";
        if (ui->mWtActiveNameLbl) {
            lv_label_set_text(ui->mWtActiveNameLbl, "ACTIVE: (Default Sine)");
        }
        std::cout << "Restored default sine wavetable." << std::endl;
    };
    addActionButton(btnRow, "RESTORE DEFAULT", defaultCb);
}

void UIManager::populateParamWavetableFilterTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createEnvCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 418);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    // --- FILTER CARD ---
    lv_obj_t* filterCard = createEnvCard(tab, "FILTER", 170);
    
    // Filter dropdown at top
    addSynthDropdown(filterCard, "FILTER TYPE", 470, "LowPass\nHighPass\nBandPass\nNotch\nPeak", 0, false);

    // Flex wrapping grid for Cutoff, Resonance, and Filter Env Amt
    lv_obj_t* filterGrid = lv_obj_create(filterCard);
    lv_obj_set_size(filterGrid, 150, 300);
    lv_obj_set_style_bg_opa(filterGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterGrid, 0, 0);
    lv_obj_set_style_pad_all(filterGrid, 0, 0);
    lv_obj_remove_flag(filterGrid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterGrid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(filterGrid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(filterGrid, "CUTOFF", 458, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterGrid, "RESONANCE", 459, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterGrid, "ENV AMT", 464, 0.0f, 1.0f, 2, true);

    // Spacer to balance height
    lv_obj_t* spacer = lv_obj_create(filterCard);
    lv_obj_set_size(spacer, 10, 10);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    // --- AMP ENVELOPE CARD ---
    lv_obj_t* ampCard = createEnvCard(tab, "AMP ENVELOPE", 290);
    
    lv_obj_t* ampRow = lv_obj_create(ampCard);
    lv_obj_set_size(ampRow, 275, 350);
    lv_obj_set_style_bg_opa(ampRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ampRow, 0, 0);
    lv_obj_set_style_pad_all(ampRow, 0, 0);
    lv_obj_remove_flag(ampRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ampRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ampRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ampRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(ampRow, "A", 454, 0.001f, 4.0f, 2, false);
    addSynthSlider(ampRow, "D", 455, 0.0f, 4.0f, 2, false);
    addSynthSlider(ampRow, "S", 456, 0.0f, 1.0f, 2, true);
    addSynthSlider(ampRow, "R", 457, 0.001f, 4.0f, 2, false);

    // --- FILTER ENVELOPE CARD ---
    lv_obj_t* filterEnvCard = createEnvCard(tab, "FILTER ENVELOPE", 290);

    lv_obj_t* filterEnvRow = lv_obj_create(filterEnvCard);
    lv_obj_set_size(filterEnvRow, 275, 350);
    lv_obj_set_style_bg_opa(filterEnvRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterEnvRow, 0, 0);
    lv_obj_set_style_pad_all(filterEnvRow, 0, 0);
    lv_obj_remove_flag(filterEnvRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterEnvRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterEnvRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterEnvRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(filterEnvRow, "A", 471, 0.001f, 4.0f, 2, false);
    addSynthSlider(filterEnvRow, "D", 472, 0.0f, 4.0f, 2, false);
    addSynthSlider(filterEnvRow, "S", 473, 0.0f, 1.0f, 2, true);
    addSynthSlider(filterEnvRow, "R", 474, 0.001f, 4.0f, 2, false);
}

void UIManager::populateParamSamplerTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 12, 0);

    // Row 1: Action Row (Latch, Record, Trim, Load, Save)
    lv_obj_t* topRow = lv_obj_create(tab);
    lv_obj_set_size(topRow, 760, 45);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_remove_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(topRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1. LATCH button
    mSamplerLatchBtn = lv_btn_create(topRow);
    lv_obj_set_size(mSamplerLatchBtn, 100, 36);
    lv_obj_add_flag(mSamplerLatchBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(mSamplerLatchBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(mSamplerLatchBtn, trackColor, LV_STATE_CHECKED);
    lv_obj_set_style_radius(mSamplerLatchBtn, 8, 0);
    lv_obj_set_style_border_width(mSamplerLatchBtn, 1, 0);
    lv_obj_set_style_border_color(mSamplerLatchBtn, lv_color_hex(0x444444), 0);
    
    lv_obj_t* latchLbl = lv_label_create(mSamplerLatchBtn);
    lv_label_set_text(latchLbl, "LATCH");
    lv_obj_set_style_text_font(latchLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(latchLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(latchLbl);
    
    lv_obj_add_event_cb(mSamplerLatchBtn, UIManager::samplerLatchBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // 2. RECORD button
    mSamplerRecordBtn = lv_btn_create(topRow);
    lv_obj_set_size(mSamplerRecordBtn, 120, 36);
    lv_obj_set_style_bg_color(mSamplerRecordBtn, lv_color_hex(0x2A1515), 0);
    lv_obj_set_style_bg_color(mSamplerRecordBtn, lv_color_hex(0x881111), LV_STATE_PRESSED);
    lv_obj_set_style_radius(mSamplerRecordBtn, 8, 0);
    lv_obj_set_style_border_color(mSamplerRecordBtn, lv_color_hex(0x552222), 0);
    lv_obj_set_style_border_width(mSamplerRecordBtn, 1, 0);
    
    lv_obj_t* recDot = lv_obj_create(mSamplerRecordBtn);
    lv_obj_set_size(recDot, 10, 10);
    lv_obj_set_style_bg_color(recDot, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_radius(recDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(recDot, 0, 0);
    lv_obj_align(recDot, LV_ALIGN_LEFT_MID, 18, 0);
    
    lv_obj_t* recLbl = lv_label_create(mSamplerRecordBtn);
    lv_label_set_text(recLbl, "RECORD");
    lv_obj_set_style_text_font(recLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(recLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(recLbl, LV_ALIGN_LEFT_MID, 38, 0);

    lv_obj_add_event_cb(mSamplerRecordBtn, UIManager::samplerRecordBtnEventCb, LV_EVENT_ALL, this);

    // 3. TRIM button
    lv_obj_t* trimBtn = lv_btn_create(topRow);
    lv_obj_set_size(trimBtn, 100, 36);
    lv_obj_set_style_bg_color(trimBtn, lv_color_hex(0x2A2215), 0);
    lv_obj_set_style_bg_color(trimBtn, lv_color_hex(0x885511), LV_STATE_PRESSED);
    lv_obj_set_style_radius(trimBtn, 8, 0);
    lv_obj_set_style_border_color(trimBtn, lv_color_hex(0x554422), 0);
    lv_obj_set_style_border_width(trimBtn, 1, 0);
    
    lv_obj_t* trimLbl = lv_label_create(trimBtn);
    lv_label_set_text(trimLbl, "TRIM");
    lv_obj_set_style_text_font(trimLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(trimLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(trimLbl);
    
    lv_obj_add_event_cb(trimBtn, UIManager::samplerTrimBtnEventCb, LV_EVENT_CLICKED, this);

    // 4. LOAD button
    lv_obj_t* loadBtn = lv_btn_create(topRow);
    lv_obj_set_size(loadBtn, 100, 36);
    lv_obj_set_style_bg_color(loadBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(loadBtn, 8, 0);
    lv_obj_set_style_border_color(loadBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(loadBtn, 1, 0);
    
    lv_obj_t* loadLbl = lv_label_create(loadBtn);
    lv_label_set_text(loadLbl, "LOAD");
    lv_obj_set_style_text_font(loadLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(loadLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(loadLbl);
    
    lv_obj_add_event_cb(loadBtn, UIManager::samplerLoadBtnEventCb, LV_EVENT_CLICKED, this);

    // 5. SAVE button
    lv_obj_t* saveBtn = lv_btn_create(topRow);
    lv_obj_set_size(saveBtn, 100, 36);
    lv_obj_set_style_bg_color(saveBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(saveBtn, 8, 0);
    lv_obj_set_style_border_color(saveBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(saveBtn, 1, 0);
    
    lv_obj_t* saveLbl = lv_label_create(saveBtn);
    lv_label_set_text(saveLbl, "SAVE");
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(saveLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(saveLbl);
    
    lv_obj_add_event_cb(saveBtn, UIManager::samplerSaveBtnEventCb, LV_EVENT_CLICKED, this);

    // Source Selector Dropdown
    lv_obj_t* srcDd = lv_dropdown_create(topRow);
    lv_obj_set_size(srcDd, 120, 36);
    lv_dropdown_set_options(srcDd, "MIC\nLINE-IN\nRESAMPLE");
    int currentSrc = mEngine.mRecordingSource.load();
    if (currentSrc > 2) currentSrc = 0;
    lv_dropdown_set_selected(srcDd, currentSrc);
    lv_obj_set_style_bg_color(srcDd, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(srcDd, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(srcDd, 1, 0);
    lv_obj_set_style_radius(srcDd, 8, 0);
    lv_obj_set_style_text_font(srcDd, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(srcDd, lv_color_hex(0xEEEEEE), 0);
    lv_obj_add_event_cb(srcDd, UIManager::audioInSourceDropdownEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Row 2: Waveform Container
    mSamplerWaveformContainer = lv_obj_create(tab);
    lv_obj_set_size(mSamplerWaveformContainer, 750, 220);
    lv_obj_set_style_bg_color(mSamplerWaveformContainer, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(mSamplerWaveformContainer, trackColor, 0);
    lv_obj_set_style_border_width(mSamplerWaveformContainer, 2, 0);
    lv_obj_set_style_radius(mSamplerWaveformContainer, 12, 0);
    lv_obj_set_layout(mSamplerWaveformContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mSamplerWaveformContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mSamplerWaveformContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(mSamplerWaveformContainer, 4, 0);
    lv_obj_set_style_pad_ver(mSamplerWaveformContainer, 10, 0);
    lv_obj_remove_flag(mSamplerWaveformContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Create 150 vertical bars representing amplitude
    for (int i = 0; i < 150; ++i) {
        mSamplerWaveformBars[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_set_size(mSamplerWaveformBars[i], 3, 2);
        lv_obj_set_style_bg_color(mSamplerWaveformBars[i], lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(mSamplerWaveformBars[i], LV_OPA_40, 0);
        lv_obj_set_style_border_width(mSamplerWaveformBars[i], 0, 0);
        lv_obj_set_style_pad_all(mSamplerWaveformBars[i], 0, 0);
        lv_obj_set_style_radius(mSamplerWaveformBars[i], 1, 0);
        lv_obj_remove_flag(mSamplerWaveformBars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // Green Start Marker
    mSamplerStartLine = lv_obj_create(mSamplerWaveformContainer);
    lv_obj_add_flag(mSamplerStartLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mSamplerStartLine, 2, 220);
    lv_obj_set_style_bg_color(mSamplerStartLine, lv_color_hex(0x00FF66), 0);
    lv_obj_set_style_border_width(mSamplerStartLine, 0, 0);
    lv_obj_set_style_radius(mSamplerStartLine, 0, 0);
    lv_obj_remove_flag(mSamplerStartLine, LV_OBJ_FLAG_SCROLLABLE);

    // Red End Marker
    mSamplerEndLine = lv_obj_create(mSamplerWaveformContainer);
    lv_obj_add_flag(mSamplerEndLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mSamplerEndLine, 2, 220);
    lv_obj_set_style_bg_color(mSamplerEndLine, lv_color_hex(0xFF3366), 0);
    lv_obj_set_style_border_width(mSamplerEndLine, 0, 0);
    lv_obj_set_style_radius(mSamplerEndLine, 0, 0);
    lv_obj_remove_flag(mSamplerEndLine, LV_OBJ_FLAG_SCROLLABLE);

    // Playhead Shades (25% opacity)
    for (int i = 0; i < 16; ++i) {
        mSamplerPlayheadShades[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerPlayheadShades[i], 0, 220);
        lv_obj_set_style_bg_color(mSamplerPlayheadShades[i], trackColor, 0);
        lv_obj_set_style_bg_opa(mSamplerPlayheadShades[i], 64, 0);
        lv_obj_set_style_border_width(mSamplerPlayheadShades[i], 0, 0);
        lv_obj_set_style_radius(mSamplerPlayheadShades[i], 0, 0);
        lv_obj_remove_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Playhead Lines (track color)
    for (int i = 0; i < 16; ++i) {
        mSamplerPlayheadLines[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerPlayheadLines[i], 2, 220);
        lv_obj_set_style_bg_color(mSamplerPlayheadLines[i], trackColor, 0);
        lv_obj_set_style_border_width(mSamplerPlayheadLines[i], 0, 0);
        lv_obj_set_style_radius(mSamplerPlayheadLines[i], 0, 0);
        lv_obj_remove_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Scrub Playhead Handle (opposite of trackColor)
    mSamplerScrubHandle = lv_obj_create(mSamplerWaveformContainer);
    lv_obj_add_flag(mSamplerScrubHandle, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mSamplerScrubHandle, 24, 24);
    lv_obj_set_style_radius(mSamplerScrubHandle, LV_RADIUS_CIRCLE, 0);
    lv_color_t oppositeColor = lv_color_hex(0xFFFFFF - lv_color_to_u32(trackColor));
    lv_obj_set_style_bg_color(mSamplerScrubHandle, oppositeColor, 0);
    lv_obj_set_style_border_color(mSamplerScrubHandle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(mSamplerScrubHandle, 2, 0);
    lv_obj_remove_flag(mSamplerScrubHandle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mSamplerScrubHandle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(mSamplerScrubHandle, samplerScrubHandleEventCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(mSamplerScrubHandle, samplerScrubHandleEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(mSamplerScrubHandle, samplerScrubHandleEventCb, LV_EVENT_RELEASED, this);

    // 16 Slices Lines & Rounded Handles
    for (int i = 0; i < 16; ++i) {
        mSamplerSliceLines[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerSliceLines[i], 2, 220);
        lv_obj_set_style_bg_color(mSamplerSliceLines[i], lv_color_hex(0x00D2FF), 0); // Cyan
        lv_obj_set_style_border_width(mSamplerSliceLines[i], 0, 0);
        lv_obj_set_style_radius(mSamplerSliceLines[i], 0, 0);
        lv_obj_remove_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_HIDDEN);

        mSamplerSliceHandles[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerSliceHandles[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerSliceHandles[i], 16, 16);
        lv_obj_set_style_bg_color(mSamplerSliceHandles[i], lv_color_hex(0x00D2FF), 0);
        lv_obj_set_style_radius(mSamplerSliceHandles[i], 4, 0);
        lv_obj_set_style_border_color(mSamplerSliceHandles[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(mSamplerSliceHandles[i], 1, 0);
        lv_obj_remove_flag(mSamplerSliceHandles[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(mSamplerSliceHandles[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(mSamplerSliceHandles[i], (void*)(uintptr_t)i);
        lv_obj_add_event_cb(mSamplerSliceHandles[i], samplerSliceHandleEventCb, LV_EVENT_PRESSING, this);
    }

    // Row 3: Sample Edits (Bottom Row)
    lv_obj_t* bottomRow = lv_obj_create(tab);
    lv_obj_set_size(bottomRow, 760, 115);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_remove_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(bottomRow, "START", 330, 0.0f, 1.0f, 2, true);
    addSynthKnob(bottomRow, "END", 331, 0.0f, 1.0f, 2, true);
    addSynthKnob(bottomRow, "SLICES", 340, 0.0f, 1.0f, 0, false);
    addSynthKnob(bottomRow, "MODE", 320, 0.0f, 1.0f, 0, false);
    addSynthKnob(bottomRow, "SLICE SEL", 341, 0.0f, 1.0f, 0, false);

    // Symmetrical Buttons Column (Reverse and Lock)
    lv_obj_t* btnCol = lv_obj_create(bottomRow);
    lv_obj_set_size(btnCol, 110, 100);
    lv_obj_set_style_bg_opa(btnCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnCol, 0, 0);
    lv_obj_set_style_pad_all(btnCol, 0, 0);
    lv_obj_remove_flag(btnCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btnCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btnCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // REVERSE Button
    lv_obj_t* revBtn = lv_btn_create(btnCol);
    lv_obj_set_size(revBtn, 100, 36);
    lv_obj_add_flag(revBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(revBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(revBtn, trackColor, LV_STATE_CHECKED);
    lv_obj_set_style_radius(revBtn, 8, 0);
    lv_obj_set_style_border_color(revBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(revBtn, 1, 0);
    
    bool isReversed = mEngine.getTracks()[mActiveTrack].parameters[351] > 0.5f;
    if (isReversed) {
        lv_obj_add_state(revBtn, LV_STATE_CHECKED);
    }

    lv_obj_t* revLbl = lv_label_create(revBtn);
    lv_label_set_text(revLbl, "REVERSE");
    lv_obj_set_style_text_font(revLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(revLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(revLbl);

    SynthParamData* revData = new SynthParamData{this, 351, nullptr, 0.0f, 1.0f, -2, false};
    lv_obj_add_event_cb(revBtn, UIManager::synthParamSliderEventCb, LV_EVENT_VALUE_CHANGED, revData);
    auto freeRevCb = [](lv_event_t* e) {
        SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(revBtn, freeRevCb, LV_EVENT_DELETE, revData);

    // LOCK Button
    lv_obj_t* lockBtn = lv_btn_create(btnCol);
    lv_obj_set_size(lockBtn, 100, 36);
    lv_obj_add_flag(lockBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(lockBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(lockBtn, trackColor, LV_STATE_CHECKED);
    lv_obj_set_style_radius(lockBtn, 8, 0);
    lv_obj_set_style_border_color(lockBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(lockBtn, 1, 0);

    bool isLocked = mEngine.getTracks()[mActiveTrack].parameters[342] > 0.5f;
    if (isLocked) {
        lv_obj_add_state(lockBtn, LV_STATE_CHECKED);
    }

    lv_obj_t* lockLbl = lv_label_create(lockBtn);
    lv_label_set_text(lockLbl, "SLICE LOCK");
    lv_obj_set_style_text_font(lockLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lockLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lockLbl);

    SynthParamData* lockData = new SynthParamData{this, 342, nullptr, 0.0f, 1.0f, -2, false};
    lv_obj_add_event_cb(lockBtn, UIManager::synthParamSliderEventCb, LV_EVENT_VALUE_CHANGED, lockData);
    auto freeLockCb = [](lv_event_t* e) {
        SynthParamData* d = (SynthParamData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(lockBtn, freeLockCb, LV_EVENT_DELETE, lockData);

    // Trigger initial preview draw
    updateSamplerWaveformPreview();
}

void UIManager::populateParamSamplerSynthesisTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tab, 16, 0);
    lv_obj_set_style_pad_all(tab, 8, 0);

    auto createSynthCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 380);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    // --- SYNTHESIS CARD (Left) ---
    lv_obj_t* synthCard = createSynthCard(tab, "SYNTHESIS", 370);

    lv_obj_t* rowsContainer = lv_obj_create(synthCard);
    lv_obj_set_size(rowsContainer, 350, 320);
    lv_obj_set_style_bg_opa(rowsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rowsContainer, 0, 0);
    lv_obj_set_style_pad_all(rowsContainer, 0, 0);
    lv_obj_remove_flag(rowsContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rowsContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rowsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rowsContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createSynthRow = [](lv_obj_t* parent, int height) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, 350, height);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return row;
    };

    lv_obj_t* row1 = createSynthRow(rowsContainer, 100);
    lv_obj_t* row2 = createSynthRow(rowsContainer, 100);
    lv_obj_t* row3 = createSynthRow(rowsContainer, 100);

    // Row 1: Pitch, Speed, Stretch
    addSynthKnob(row1, "PITCH", 300, 0.0f, 1.0f, 0, false);
    addSynthKnob(row1, "SPEED", 302, 0.0f, 1.0f, 2, false);
    addSynthKnob(row1, "STRETCH", 301, 0.0f, 1.0f, 2, false);

    // Row 2: Cutoff, Resonance, Filter Type
    addSynthKnob(row2, "CUTOFF", 303, 0.0f, 1.0f, 2, true);
    addSynthKnob(row2, "RES", 304, 0.0f, 1.0f, 2, true);
    addSynthDropdown(row2, "FILTER TYPE", 305, "LowPass\nHighPass\nBandPass\nNotch\nPeak", 0, false);

    // Row 3: Glide
    addSynthKnob(row3, "GLIDE", 355, 0.0f, 1.0f, 2, true);

    // --- ENVELOPE CARD (Right) ---
    lv_obj_t* envCard = createSynthCard(tab, "ENVELOPE", 370);

    // ADSR sliders row
    lv_obj_t* adsrRow = lv_obj_create(envCard);
    lv_obj_set_size(adsrRow, 350, 230);
    lv_obj_set_style_bg_opa(adsrRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(adsrRow, 0, 0);
    lv_obj_set_style_pad_all(adsrRow, 0, 0);
    lv_obj_remove_flag(adsrRow, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_layout(adsrRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(adsrRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(adsrRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(adsrRow, "A", 310, 0.001f, 4.0f, 2, false, 230);
    addSynthSlider(adsrRow, "D", 311, 0.0f, 4.0f, 2, false, 230);
    addSynthSlider(adsrRow, "S", 312, 0.0f, 1.0f, 2, true, 230);
    addSynthSlider(adsrRow, "R", 313, 0.001f, 4.0f, 2, false, 230);

    // Envelope modulation intensity knob below
    addSynthKnob(envCard, "ENV AMT", 314, 0.0f, 1.0f, 2, true);
}

void UIManager::samplerLatchBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* latchBtn = (lv_obj_t*)lv_event_get_target(e);
    bool isLatched = lv_obj_has_state(latchBtn, LV_STATE_CHECKED);
    
    if (ui->mSamplerRecordBtn) {
        if (isLatched) {
            lv_obj_add_flag(ui->mSamplerRecordBtn, LV_OBJ_FLAG_CHECKABLE);
            lv_obj_set_style_bg_color(ui->mSamplerRecordBtn, lv_color_hex(0x881111), LV_STATE_CHECKED);
        } else {
            if (lv_obj_has_state(ui->mSamplerRecordBtn, LV_STATE_CHECKED)) {
                lv_obj_clear_state(ui->mSamplerRecordBtn, LV_STATE_CHECKED);
                ui->mEngine.stopRecordingSample(ui->mActiveTrack);
            }
            lv_obj_remove_flag(ui->mSamplerRecordBtn, LV_OBJ_FLAG_CHECKABLE);
        }
    }
}

void UIManager::samplerRecordBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    bool isLatched = ui->mSamplerLatchBtn ? lv_obj_has_state(ui->mSamplerLatchBtn, LV_STATE_CHECKED) : false;
    
    if (isLatched) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool active = lv_obj_has_state(btn, LV_STATE_CHECKED);
            if (active) {
                ui->mEngine.startRecordingSample(ui->mActiveTrack);
                std::cout << "Latched Recording Started on Track " << ui->mActiveTrack + 1 << std::endl;
            } else {
                ui->mEngine.stopRecordingSample(ui->mActiveTrack);
                std::cout << "Latched Recording Stopped on Track " << ui->mActiveTrack + 1 << std::endl;
            }
        }
    } else {
        if (code == LV_EVENT_PRESSED) {
            ui->mEngine.startRecordingSample(ui->mActiveTrack);
            std::cout << "Momentary Recording Started on Track " << ui->mActiveTrack + 1 << std::endl;
        } else if (code == LV_EVENT_RELEASED) {
            ui->mEngine.stopRecordingSample(ui->mActiveTrack);
            std::cout << "Momentary Recording Stopped on Track " << ui->mActiveTrack + 1 << std::endl;
        }
    }
}

void UIManager::samplerLoadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsSampleLoad = true;
    ui->mFileBrowserIsSampleSave = false;
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;
    ui->openFileBrowser(false);
}

void UIManager::samplerSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsSampleLoad = false;
    ui->mFileBrowserIsSampleSave = true;
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;
    ui->openFileBrowser(true);
}

void UIManager::samplerTrimBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.trimSample(ui->mActiveTrack);
    ui->mEngine.setParameter(ui->mActiveTrack, 330, 0.0f);
    ui->mEngine.setParameter(ui->mActiveTrack, 331, 1.0f);
    ui->createCenterContentArea();
    std::cout << "Trimmed sample and reset start/end markers." << std::endl;
}

void UIManager::samplerScrubHandleEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        ui->mEngine.setParameter(ui->mActiveTrack, 361, 1.0f); // mScrubGate
    } else if (code == LV_EVENT_RELEASED) {
        ui->mEngine.setParameter(ui->mActiveTrack, 361, 0.0f);
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            
            lv_area_t container_area;
            lv_obj_get_coords(ui->mSamplerWaveformContainer, &container_area);
            
            int container_w = lv_area_get_width(&container_area);
            int local_x = p.x - container_area.x1;
            
            float scrubPos = (float)local_x / (float)(container_w > 0 ? container_w : 1);
            if (scrubPos < 0.0f) scrubPos = 0.0f;
            if (scrubPos > 1.0f) scrubPos = 1.0f;
            
            ui->mEngine.setParameter(ui->mActiveTrack, 360, scrubPos); // mScrubPosition
        }
    }
}

void UIManager::samplerSliceHandleEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* handle = (lv_obj_t*)lv_event_get_target(e);
    int sliceIdx = (int)(uintptr_t)lv_obj_get_user_data(handle);
    
    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        
        lv_area_t container_area;
        lv_obj_get_coords(ui->mSamplerWaveformContainer, &container_area);
        
        int container_w = lv_area_get_width(&container_area);
        int local_x = p.x - container_area.x1;
        
        float pos = (float)local_x / (float)(container_w > 0 ? container_w : 1);
        if (pos < 0.0f) pos = 0.0f;
        if (pos > 1.0f) pos = 1.0f;
        
        ui->mEngine.setSlicePosition(ui->mActiveTrack, sliceIdx, pos);
        ui->updateSamplerWaveformPreview();
    }
}

void UIManager::updateSamplerWaveformPreview() {
    if (mActiveTrack < 0 || mActiveTrack >= (int)mEngine.getTracks().size()) return;
    if (mEngine.getTracks()[mActiveTrack].engineType != 2) return;
    if (!mSamplerWaveformContainer) return;

    std::vector<float> peaks = mEngine.getSamplerWaveform(mActiveTrack, 150);
    if (peaks.size() < 150) {
        peaks.resize(150, 0.0f);
    }

    float startPnt = mEngine.getTracks()[mActiveTrack].parameters[330];
    float endPnt = mEngine.getTracks()[mActiveTrack].parameters[331];

    lv_color_t trackColor = getTrackColor(mActiveTrack);

    for (int i = 0; i < 150; ++i) {
        lv_obj_t* bar = mSamplerWaveformBars[i];
        if (!bar) continue;

        float amp = peaks[i];
        if (amp < 0.0f) amp = 0.0f;
        if (amp > 1.0f) amp = 1.0f;

        int h = (int)(amp * 200.0f);
        if (h < 2) h = 2;
        lv_obj_set_height(bar, h);

        float pos = (float)i / 150.0f;
        if (pos >= startPnt && pos <= endPnt) {
            lv_obj_set_style_bg_color(bar, trackColor, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x444444), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
        }
    }

    if (mSamplerStartLine) {
        int x = (int)(startPnt * 746.0f);
        lv_obj_align(mSamplerStartLine, LV_ALIGN_LEFT_MID, x, 0);
    }
    if (mSamplerEndLine) {
        int x = (int)(endPnt * 746.0f);
        lv_obj_align(mSamplerEndLine, LV_ALIGN_LEFT_MID, x, 0);
    }

    // 1. Scrub Mode vs Normal Playhead
    bool isScrubMode = (mEngine.getTracks()[mActiveTrack].parameters[320] >= 0.95f);
    
    // Query active playheads and update playhead line & shade
    GranularEngine::PlayheadInfo playheads[16];
    mEngine.getGranularPlayheads(mActiveTrack, playheads, 16);
    
    if (isScrubMode) {
        // Hide all playhead shades and other playhead lines
        for (int i = 0; i < 16; ++i) {
            if (mSamplerPlayheadShades[i]) {
                lv_obj_add_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_HIDDEN);
            }
            if (i > 0 && mSamplerPlayheadLines[i]) {
                lv_obj_add_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        if (mSamplerPlayheadLines[0]) {
            lv_color_t oppositeColor = lv_color_hex(0xFFFFFF - lv_color_to_u32(trackColor));
            lv_obj_set_style_bg_color(mSamplerPlayheadLines[0], oppositeColor, 0);
            
            float pos = mEngine.getTracks()[mActiveTrack].parameters[360]; // fallback to last scrub position
            for (int i = 0; i < 16; ++i) {
                if (playheads[i].pos >= 0.0f) {
                    pos = playheads[i].pos;
                    break;
                }
            }
            int x = (int)(pos * 746.0f);
            lv_obj_align(mSamplerPlayheadLines[0], LV_ALIGN_LEFT_MID, x, 0);
            lv_obj_clear_flag(mSamplerPlayheadLines[0], LV_OBJ_FLAG_HIDDEN);
            
            if (mSamplerScrubHandle) {
                lv_obj_set_style_bg_color(mSamplerScrubHandle, oppositeColor, 0);
                lv_obj_align(mSamplerScrubHandle, LV_ALIGN_BOTTOM_LEFT, x - 12, 10);
                lv_obj_clear_flag(mSamplerScrubHandle, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        if (mSamplerScrubHandle) {
            lv_obj_add_flag(mSamplerScrubHandle, LV_OBJ_FLAG_HIDDEN);
        }
        
        for (int i = 0; i < 16; ++i) {
            if (playheads[i].pos >= 0.0f) {
                if (mSamplerPlayheadLines[i]) {
                    lv_obj_set_style_bg_color(mSamplerPlayheadLines[i], trackColor, 0);
                    int x = (int)(playheads[i].pos * 746.0f);
                    lv_obj_align(mSamplerPlayheadLines[i], LV_ALIGN_LEFT_MID, x, 0);
                    lv_obj_clear_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (mSamplerPlayheadShades[i]) {
                    lv_obj_set_style_bg_color(mSamplerPlayheadShades[i], trackColor, 0);
                    lv_obj_set_style_bg_opa(mSamplerPlayheadShades[i], 64, 0); // 25% opacity
                    float s = std::min(playheads[i].start, playheads[i].pos);
                    float e = std::max(playheads[i].start, playheads[i].pos);
                    int xs = (int)(s * 746.0f);
                    int ws = (int)((e - s) * 746.0f);
                    if (ws < 0) ws = 0;
                    lv_obj_set_width(mSamplerPlayheadShades[i], ws);
                    lv_obj_align(mSamplerPlayheadShades[i], LV_ALIGN_LEFT_MID, xs, 0);
                    lv_obj_clear_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (mSamplerPlayheadLines[i]) {
                    lv_obj_add_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (mSamplerPlayheadShades[i]) {
                    lv_obj_add_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // 2. Sampler Slice Visual Markers
    std::vector<float> slicePoints = mEngine.getSamplerSlicePoints(mActiveTrack);
    for (int i = 0; i < 16; ++i) {
        if (i < (int)slicePoints.size()) {
            float p = slicePoints[i];
            int x = (int)(p * 746.0f);
            if (mSamplerSliceLines[i]) {
                lv_obj_align(mSamplerSliceLines[i], LV_ALIGN_LEFT_MID, x, 0);
                lv_obj_clear_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_HIDDEN);
            }
            if (mSamplerSliceHandles[i]) {
                lv_obj_align(mSamplerSliceHandles[i], LV_ALIGN_BOTTOM_LEFT, x - 8, 6);
                lv_obj_clear_flag(mSamplerSliceHandles[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (mSamplerSliceLines[i]) {
                lv_obj_add_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_HIDDEN);
            }
            if (mSamplerSliceHandles[i]) {
                lv_obj_add_flag(mSamplerSliceHandles[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (mSamplerRecordBtn) {
        bool isRec = mEngine.getIsRecordingSample();
        if (isRec) {
            lv_obj_set_style_bg_color(mSamplerRecordBtn, lv_color_hex(0xFF3333), 0);
            lv_obj_t* recDot = lv_obj_get_child(mSamplerRecordBtn, 0);
            if (recDot) {
                lv_obj_set_style_bg_color(recDot, lv_color_hex(0xFFFFFF), 0);
            }
        } else {
            bool isLatched = mSamplerLatchBtn ? lv_obj_has_state(mSamplerLatchBtn, LV_STATE_CHECKED) : false;
            if (isLatched) {
                lv_obj_set_style_bg_color(mSamplerRecordBtn, lv_obj_has_state(mSamplerRecordBtn, LV_STATE_CHECKED) ? lv_color_hex(0x881111) : lv_color_hex(0x2A1515), 0);
            } else {
                lv_obj_set_style_bg_color(mSamplerRecordBtn, lv_color_hex(0x2A1515), 0);
            }
            lv_obj_t* recDot = lv_obj_get_child(mSamplerRecordBtn, 0);
            if (recDot) {
                lv_obj_set_style_bg_color(recDot, lv_color_hex(0xFF3333), 0);
            }
        }
    }
}

void UIManager::populateParamGranularSamplingTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 12, 0);

    // Row 1: Action Row (Latch, Record, Trim, Load, Save)
    lv_obj_t* topRow = lv_obj_create(tab);
    lv_obj_set_size(topRow, 760, 45);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_remove_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(topRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1. LATCH button
    mGranularLatchBtn = lv_btn_create(topRow);
    lv_obj_set_size(mGranularLatchBtn, 100, 36);
    lv_obj_add_flag(mGranularLatchBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(mGranularLatchBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_color(mGranularLatchBtn, trackColor, LV_STATE_CHECKED);
    lv_obj_set_style_radius(mGranularLatchBtn, 8, 0);
    lv_obj_set_style_border_width(mGranularLatchBtn, 1, 0);
    lv_obj_set_style_border_color(mGranularLatchBtn, lv_color_hex(0x444444), 0);
    
    lv_obj_t* latchLbl = lv_label_create(mGranularLatchBtn);
    lv_label_set_text(latchLbl, "LATCH");
    lv_obj_set_style_text_font(latchLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(latchLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(latchLbl);
    
    lv_obj_add_event_cb(mGranularLatchBtn, UIManager::granularLatchBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // 2. RECORD button
    mGranularRecordBtn = lv_btn_create(topRow);
    lv_obj_set_size(mGranularRecordBtn, 120, 36);
    lv_obj_set_style_bg_color(mGranularRecordBtn, lv_color_hex(0x2A1515), 0);
    lv_obj_set_style_bg_color(mGranularRecordBtn, lv_color_hex(0x881111), LV_STATE_PRESSED);
    lv_obj_set_style_radius(mGranularRecordBtn, 8, 0);
    lv_obj_set_style_border_color(mGranularRecordBtn, lv_color_hex(0x552222), 0);
    lv_obj_set_style_border_width(mGranularRecordBtn, 1, 0);
    
    lv_obj_t* recDot = lv_obj_create(mGranularRecordBtn);
    lv_obj_set_size(recDot, 10, 10);
    lv_obj_set_style_bg_color(recDot, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_radius(recDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(recDot, 0, 0);
    lv_obj_align(recDot, LV_ALIGN_LEFT_MID, 18, 0);
    
    lv_obj_t* recLbl = lv_label_create(mGranularRecordBtn);
    lv_label_set_text(recLbl, "RECORD");
    lv_obj_set_style_text_font(recLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(recLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(recLbl, LV_ALIGN_LEFT_MID, 38, 0);

    lv_obj_add_event_cb(mGranularRecordBtn, UIManager::granularRecordBtnEventCb, LV_EVENT_ALL, this);

    // 3. TRIM button
    lv_obj_t* trimBtn = lv_btn_create(topRow);
    lv_obj_set_size(trimBtn, 100, 36);
    lv_obj_set_style_bg_color(trimBtn, lv_color_hex(0x2A2215), 0);
    lv_obj_set_style_bg_color(trimBtn, lv_color_hex(0x885511), LV_STATE_PRESSED);
    lv_obj_set_style_radius(trimBtn, 8, 0);
    lv_obj_set_style_border_color(trimBtn, lv_color_hex(0x554422), 0);
    lv_obj_set_style_border_width(trimBtn, 1, 0);
    
    lv_obj_t* trimLbl = lv_label_create(trimBtn);
    lv_label_set_text(trimLbl, "TRIM");
    lv_obj_set_style_text_font(trimLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(trimLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(trimLbl);
    
    lv_obj_add_event_cb(trimBtn, UIManager::granularTrimBtnEventCb, LV_EVENT_CLICKED, this);

    // 4. LOAD button
    lv_obj_t* loadBtn = lv_btn_create(topRow);
    lv_obj_set_size(loadBtn, 100, 36);
    lv_obj_set_style_bg_color(loadBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(loadBtn, 8, 0);
    lv_obj_set_style_border_color(loadBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(loadBtn, 1, 0);
    
    lv_obj_t* loadLbl = lv_label_create(loadBtn);
    lv_label_set_text(loadLbl, "LOAD");
    lv_obj_set_style_text_font(loadLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(loadLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(loadLbl);
    
    lv_obj_add_event_cb(loadBtn, UIManager::granularLoadBtnEventCb, LV_EVENT_CLICKED, this);

    // 5. SAVE button
    lv_obj_t* saveBtn = lv_btn_create(topRow);
    lv_obj_set_size(saveBtn, 100, 36);
    lv_obj_set_style_bg_color(saveBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(saveBtn, 8, 0);
    lv_obj_set_style_border_color(saveBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(saveBtn, 1, 0);
    
    lv_obj_t* saveLbl = lv_label_create(saveBtn);
    lv_label_set_text(saveLbl, "SAVE");
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(saveLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(saveLbl);
    
    lv_obj_add_event_cb(saveBtn, UIManager::granularSaveBtnEventCb, LV_EVENT_CLICKED, this);

    // Source Selector Dropdown
    lv_obj_t* srcDd = lv_dropdown_create(topRow);
    lv_obj_set_size(srcDd, 120, 36);
    lv_dropdown_set_options(srcDd, "MIC\nLINE-IN\nRESAMPLE");
    int currentSrc = mEngine.mRecordingSource.load();
    if (currentSrc > 2) currentSrc = 0;
    lv_dropdown_set_selected(srcDd, currentSrc);
    lv_obj_set_style_bg_color(srcDd, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(srcDd, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(srcDd, 1, 0);
    lv_obj_set_style_radius(srcDd, 8, 0);
    lv_obj_set_style_text_font(srcDd, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(srcDd, lv_color_hex(0xEEEEEE), 0);
    lv_obj_add_event_cb(srcDd, UIManager::audioInSourceDropdownEventCb, LV_EVENT_VALUE_CHANGED, this);

    // 6. SLICE LOCK button removed for Granular Engine
    mGranularLockBtn = nullptr;

    // Row 2: Waveform Container
    mGranularWaveformContainer = lv_obj_create(tab);
    lv_obj_set_size(mGranularWaveformContainer, 750, 140);
    lv_obj_set_style_bg_color(mGranularWaveformContainer, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(mGranularWaveformContainer, trackColor, 0);
    lv_obj_set_style_border_width(mGranularWaveformContainer, 2, 0);
    lv_obj_set_style_radius(mGranularWaveformContainer, 12, 0);
    lv_obj_set_layout(mGranularWaveformContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mGranularWaveformContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mGranularWaveformContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(mGranularWaveformContainer, 4, 0);
    lv_obj_set_style_pad_ver(mGranularWaveformContainer, 10, 0);
    lv_obj_remove_flag(mGranularWaveformContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Create 150 vertical bars representing amplitude
    for (int i = 0; i < 150; ++i) {
        mGranularWaveformBars[i] = lv_obj_create(mGranularWaveformContainer);
        lv_obj_set_size(mGranularWaveformBars[i], 3, 2);
        lv_obj_set_style_bg_color(mGranularWaveformBars[i], lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(mGranularWaveformBars[i], LV_OPA_40, 0);
        lv_obj_set_style_border_width(mGranularWaveformBars[i], 0, 0);
        lv_obj_set_style_pad_all(mGranularWaveformBars[i], 0, 0);
        lv_obj_set_style_radius(mGranularWaveformBars[i], 1, 0);
        lv_obj_remove_flag(mGranularWaveformBars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // Green Start Marker
    mGranularStartLine = lv_obj_create(mGranularWaveformContainer);
    lv_obj_add_flag(mGranularStartLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mGranularStartLine, 2, 140);
    lv_obj_set_style_bg_color(mGranularStartLine, lv_color_hex(0x00FF66), 0);
    lv_obj_set_style_border_width(mGranularStartLine, 0, 0);
    lv_obj_set_style_radius(mGranularStartLine, 0, 0);
    lv_obj_remove_flag(mGranularStartLine, LV_OBJ_FLAG_SCROLLABLE);

    // Red End Marker
    mGranularEndLine = lv_obj_create(mGranularWaveformContainer);
    lv_obj_add_flag(mGranularEndLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mGranularEndLine, 2, 140);
    lv_obj_set_style_bg_color(mGranularEndLine, lv_color_hex(0xFF3366), 0);
    lv_obj_set_style_border_width(mGranularEndLine, 0, 0);
    lv_obj_set_style_radius(mGranularEndLine, 0, 0);
    lv_obj_remove_flag(mGranularEndLine, LV_OBJ_FLAG_SCROLLABLE);

    // Playhead Shade (25% opacity)
    mGranularPlayheadShade = lv_obj_create(mGranularWaveformContainer);
    lv_obj_add_flag(mGranularPlayheadShade, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mGranularPlayheadShade, 0, 140);
    lv_obj_set_style_bg_color(mGranularPlayheadShade, trackColor, 0);
    lv_obj_set_style_bg_opa(mGranularPlayheadShade, 64, 0);
    lv_obj_set_style_border_width(mGranularPlayheadShade, 0, 0);
    lv_obj_set_style_radius(mGranularPlayheadShade, 0, 0);
    lv_obj_remove_flag(mGranularPlayheadShade, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mGranularPlayheadShade, LV_OBJ_FLAG_HIDDEN);

    // Playhead Line (track color)
    mGranularPlayheadLine = lv_obj_create(mGranularWaveformContainer);
    lv_obj_add_flag(mGranularPlayheadLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mGranularPlayheadLine, 2, 140);
    lv_obj_set_style_bg_color(mGranularPlayheadLine, trackColor, 0);
    lv_obj_set_style_border_width(mGranularPlayheadLine, 0, 0);
    lv_obj_set_style_radius(mGranularPlayheadLine, 0, 0);
    lv_obj_remove_flag(mGranularPlayheadLine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mGranularPlayheadLine, LV_OBJ_FLAG_HIDDEN);

    // Row 3: Bottom Row: Two Boxes (Cloud and Motion) side-by-side (2 Rows)
    lv_obj_t* bottomRow = lv_obj_create(tab);
    lv_obj_set_size(bottomRow, 760, 280);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_remove_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1. CLOUD Card Box
    lv_obj_t* cloudCard = lv_obj_create(bottomRow);
    lv_obj_set_size(cloudCard, 370, 270);
    lv_obj_set_style_bg_color(cloudCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(cloudCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(cloudCard, 1, 0);
    lv_obj_set_style_radius(cloudCard, 10, 0);
    lv_obj_set_style_pad_all(cloudCard, 8, 0);
    lv_obj_remove_flag(cloudCard, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add title
    lv_obj_t* cloudTitle = lv_label_create(cloudCard);
    lv_label_set_text(cloudTitle, "CLOUD");
    lv_obj_set_style_text_font(cloudTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(cloudTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(cloudTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* cloudGrid = lv_obj_create(cloudCard);
    lv_obj_set_size(cloudGrid, 350, 230);
    lv_obj_set_style_bg_opa(cloudGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cloudGrid, 0, 0);
    lv_obj_set_style_pad_all(cloudGrid, 0, 0);
    lv_obj_align(cloudGrid, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_remove_flag(cloudGrid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(cloudGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cloudGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cloudGrid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cloudGrid, 6, 0);
    lv_obj_set_style_pad_column(cloudGrid, 4, 0);

    addSynthKnob(cloudGrid, "POSITION", 400, 0.0f, 1.0f, 2, true);
    addSynthKnob(cloudGrid, "SIZE", 406, 0.005f, 1.0f, 2, false);
    addSynthKnob(cloudGrid, "DENSITY", 407, 0.01f, 1.0f, 2, true);
    addSynthKnob(cloudGrid, "SPRAY", 415, 0.0f, 1.0f, 2, true);
    addSynthKnob(cloudGrid, "COUNT", 418, 0.0f, 1.0f, 0, false);

    // 2. MOTION Card Box
    lv_obj_t* motionCard = lv_obj_create(bottomRow);
    lv_obj_set_size(motionCard, 370, 270);
    lv_obj_set_style_bg_color(motionCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(motionCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(motionCard, 1, 0);
    lv_obj_set_style_radius(motionCard, 10, 0);
    lv_obj_set_style_pad_all(motionCard, 8, 0);
    lv_obj_remove_flag(motionCard, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add title
    lv_obj_t* motionTitle = lv_label_create(motionCard);
    lv_label_set_text(motionTitle, "MOTION");
    lv_obj_set_style_text_font(motionTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(motionTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(motionTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* motionGrid = lv_obj_create(motionCard);
    lv_obj_set_size(motionGrid, 350, 230);
    lv_obj_set_style_bg_opa(motionGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(motionGrid, 0, 0);
    lv_obj_set_style_pad_all(motionGrid, 0, 0);
    lv_obj_align(motionGrid, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_remove_flag(motionGrid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(motionGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(motionGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(motionGrid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(motionGrid, 6, 0);
    lv_obj_set_style_pad_column(motionGrid, 4, 0);

    addSynthKnob(motionGrid, "SPEED", 401, 0.0f, 4.0f, 2, false);
    addSynthKnob(motionGrid, "JITTER", 417, 0.0f, 1.0f, 2, true);
    addSynthKnob(motionGrid, "REV PROB", 420, 0.0f, 1.0f, 2, true);
    addSynthKnob(motionGrid, "WIDTH", 419, 0.0f, 1.0f, 2, true);

    updateGranularWaveformPreview();
}

void UIManager::populateParamGranularSynthTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);

    // Row 1: LFO Row: Three Modulators side-by-side
    lv_obj_t* lfoRow = lv_obj_create(tab);
    lv_obj_set_size(lfoRow, 760, 205);
    lv_obj_set_style_bg_opa(lfoRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoRow, 0, 0);
    lv_obj_set_style_pad_all(lfoRow, 0, 0);
    lv_obj_remove_flag(lfoRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfoRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // --- LFO 1 CARD ---
    lv_obj_t* lfo1Card = lv_obj_create(lfoRow);
    lv_obj_set_size(lfo1Card, 246, 200);
    lv_obj_set_style_bg_color(lfo1Card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(lfo1Card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(lfo1Card, 1, 0);
    lv_obj_set_style_radius(lfo1Card, 10, 0);
    lv_obj_set_style_pad_all(lfo1Card, 8, 0);
    lv_obj_remove_flag(lfo1Card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lfo1Title = lv_label_create(lfo1Card);
    lv_label_set_text(lfo1Title, "LFO 1 (PNT & SPD)");
    lv_obj_set_style_text_font(lfo1Title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lfo1Title, lv_color_hex(0x888888), 0);
    lv_obj_align(lfo1Title, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* lfo1Content = lv_obj_create(lfo1Card);
    lv_obj_set_size(lfo1Content, 230, 165);
    lv_obj_set_style_bg_opa(lfo1Content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo1Content, 0, 0);
    lv_obj_set_style_pad_all(lfo1Content, 0, 0);
    lv_obj_align(lfo1Content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(lfo1Content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo1Content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo1Content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lfo1Content, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lfo1DropdownRow = lv_obj_create(lfo1Content);
    lv_obj_set_size(lfo1DropdownRow, 230, 52);
    lv_obj_set_style_bg_opa(lfo1DropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo1DropdownRow, 0, 0);
    lv_obj_set_style_pad_all(lfo1DropdownRow, 0, 0);
    lv_obj_remove_flag(lfo1DropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo1DropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo1DropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfo1DropdownRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(lfo1DropdownRow, "SHAPE", 402, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false, 105);
    addSynthDropdown(lfo1DropdownRow, "TARGET", 405, "NONE\nPOSITION\nSPEED", 0, false, 105);

    lv_obj_t* lfo1KnobRow = lv_obj_create(lfo1Content);
    lv_obj_set_size(lfo1KnobRow, 230, 96);
    lv_obj_set_style_bg_opa(lfo1KnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo1KnobRow, 0, 0);
    lv_obj_set_style_pad_all(lfo1KnobRow, 0, 0);
    lv_obj_remove_flag(lfo1KnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo1KnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo1KnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfo1KnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(lfo1KnobRow, "RATE", 403, 0.01f, 20.0f, 2, false);
    addSynthKnob(lfo1KnobRow, "DEPTH", 404, 0.0f, 1.0f, 2, true);

    // --- LFO 2 CARD ---
    lv_obj_t* lfo2Card = lv_obj_create(lfoRow);
    lv_obj_set_size(lfo2Card, 246, 200);
    lv_obj_set_style_bg_color(lfo2Card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(lfo2Card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(lfo2Card, 1, 0);
    lv_obj_set_style_radius(lfo2Card, 10, 0);
    lv_obj_set_style_pad_all(lfo2Card, 8, 0);
    lv_obj_remove_flag(lfo2Card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lfo2Title = lv_label_create(lfo2Card);
    lv_label_set_text(lfo2Title, "LFO 2 (SZ & PCH)");
    lv_obj_set_style_text_font(lfo2Title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lfo2Title, lv_color_hex(0x888888), 0);
    lv_obj_align(lfo2Title, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* lfo2Content = lv_obj_create(lfo2Card);
    lv_obj_set_size(lfo2Content, 230, 165);
    lv_obj_set_style_bg_opa(lfo2Content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo2Content, 0, 0);
    lv_obj_set_style_pad_all(lfo2Content, 0, 0);
    lv_obj_align(lfo2Content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(lfo2Content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo2Content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo2Content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lfo2Content, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lfo2DropdownRow = lv_obj_create(lfo2Content);
    lv_obj_set_size(lfo2DropdownRow, 230, 52);
    lv_obj_set_style_bg_opa(lfo2DropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo2DropdownRow, 0, 0);
    lv_obj_set_style_pad_all(lfo2DropdownRow, 0, 0);
    lv_obj_remove_flag(lfo2DropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo2DropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo2DropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfo2DropdownRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(lfo2DropdownRow, "SHAPE", 411, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false, 105);
    addSynthDropdown(lfo2DropdownRow, "TARGET", 414, "NONE\nSIZE\nPITCH", 0, false, 105);

    lv_obj_t* lfo2KnobRow = lv_obj_create(lfo2Content);
    lv_obj_set_size(lfo2KnobRow, 230, 96);
    lv_obj_set_style_bg_opa(lfo2KnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo2KnobRow, 0, 0);
    lv_obj_set_style_pad_all(lfo2KnobRow, 0, 0);
    lv_obj_remove_flag(lfo2KnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo2KnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo2KnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfo2KnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(lfo2KnobRow, "RATE", 412, 0.01f, 20.0f, 2, false);
    addSynthKnob(lfo2KnobRow, "DEPTH", 413, 0.0f, 1.0f, 2, true);

    // --- LFO 3 CARD ---
    lv_obj_t* lfo3Card = lv_obj_create(lfoRow);
    lv_obj_set_size(lfo3Card, 246, 200);
    lv_obj_set_style_bg_color(lfo3Card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(lfo3Card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(lfo3Card, 1, 0);
    lv_obj_set_style_radius(lfo3Card, 10, 0);
    lv_obj_set_style_pad_all(lfo3Card, 8, 0);
    lv_obj_remove_flag(lfo3Card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lfo3Title = lv_label_create(lfo3Card);
    lv_label_set_text(lfo3Title, "LFO 3");
    lv_obj_set_style_text_font(lfo3Title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lfo3Title, lv_color_hex(0x888888), 0);
    lv_obj_align(lfo3Title, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* lfo3Content = lv_obj_create(lfo3Card);
    lv_obj_set_size(lfo3Content, 230, 165);
    lv_obj_set_style_bg_opa(lfo3Content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo3Content, 0, 0);
    lv_obj_set_style_pad_all(lfo3Content, 0, 0);
    lv_obj_align(lfo3Content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(lfo3Content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo3Content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo3Content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lfo3Content, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lfo3DropdownRow = lv_obj_create(lfo3Content);
    lv_obj_set_size(lfo3DropdownRow, 230, 52);
    lv_obj_set_style_bg_opa(lfo3DropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo3DropdownRow, 0, 0);
    lv_obj_set_style_pad_all(lfo3DropdownRow, 0, 0);
    lv_obj_remove_flag(lfo3DropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo3DropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo3DropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfo3DropdownRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(lfo3DropdownRow, "SHAPE", 421, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false, 105);
    addSynthDropdown(lfo3DropdownRow, "TARGET", 424, "NONE", 0, false, 105);

    lv_obj_t* lfo3KnobRow = lv_obj_create(lfo3Content);
    lv_obj_set_size(lfo3KnobRow, 230, 96);
    lv_obj_set_style_bg_opa(lfo3KnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfo3KnobRow, 0, 0);
    lv_obj_set_style_pad_all(lfo3KnobRow, 0, 0);
    lv_obj_remove_flag(lfo3KnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfo3KnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfo3KnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfo3KnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(lfo3KnobRow, "RATE", 422, 0.01f, 20.0f, 2, false);
    addSynthKnob(lfo3KnobRow, "DEPTH", 423, 0.0f, 1.0f, 2, true);

    // Row 2: Envelope & Pitch/Randomness cards side-by-side
    lv_obj_t* bottomRow = lv_obj_create(tab);
    lv_obj_set_size(bottomRow, 760, 240);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_remove_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // --- ENVELOPE CARD ---
    auto createEnvCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 235);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* title = lv_label_create(card);
        lv_label_set_text(title, name);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title, trackColor, 0);
        
        return card;
    };

    lv_obj_t* ampCard = createEnvCard(bottomRow, "AMP ENVELOPE", 370);

    lv_obj_t* ampRow = lv_obj_create(ampCard);
    lv_obj_set_size(ampRow, 350, 185);
    lv_obj_set_style_bg_opa(ampRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ampRow, 0, 0);
    lv_obj_set_style_pad_all(ampRow, 0, 0);
    lv_obj_remove_flag(ampRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ampRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ampRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ampRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(ampRow, "A", 425, 0.001f, 4.0f, 2, false, 140);
    addSynthSlider(ampRow, "D", 426, 0.0f, 4.0f, 2, false, 140);
    addSynthSlider(ampRow, "S", 427, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(ampRow, "R", 428, 0.001f, 4.0f, 2, false, 140);

    // --- PITCH & RANDOMNESS CARD ---
    lv_obj_t* pitchCard = createEnvCard(bottomRow, "PITCH & RANDOM", 370);

    lv_obj_t* pitchContent = lv_obj_create(pitchCard);
    lv_obj_set_size(pitchContent, 350, 192);
    lv_obj_set_style_bg_opa(pitchContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pitchContent, 0, 0);
    lv_obj_set_style_pad_all(pitchContent, 0, 0);
    lv_obj_remove_flag(pitchContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(pitchContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pitchContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pitchContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Row 1 of Pitch Grid (height 95px matches knobs perfectly to prevent clipping)
    lv_obj_t* pRow1 = lv_obj_create(pitchContent);
    lv_obj_set_size(pRow1, 350, 95);
    lv_obj_set_style_bg_opa(pRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pRow1, 0, 0);
    lv_obj_set_style_pad_all(pRow1, 0, 0);
    lv_obj_remove_flag(pRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(pRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(pRow1, "PITCH", 410, 0.25f, 4.0f, 2, false);
    addSynthKnob(pRow1, "DETUNE", 416, 0.0f, 1.0f, 2, true);
    addSynthKnob(pRow1, "RANDOM", 417, 0.0f, 1.0f, 2, true);
    addSynthKnob(pRow1, "GLIDE", 355, 0.0f, 1.0f, 2, true);

    // Row 2 of Pitch Grid (height 95px matches knobs perfectly to prevent clipping)
    lv_obj_t* pRow2 = lv_obj_create(pitchContent);
    lv_obj_set_size(pRow2, 350, 95);
    lv_obj_set_style_bg_opa(pRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pRow2, 0, 0);
    lv_obj_set_style_pad_all(pRow2, 0, 0);
    lv_obj_remove_flag(pRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(pRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(pRow2, "GRAIN EA", 408, 0.001f, 1.0f, 2, false);
    addSynthKnob(pRow2, "GRAIN ED", 409, 0.001f, 1.0f, 2, false);
    addSynthKnob(pRow2, "PROB", 430, 0.0f, 1.0f, 2, true);
}

void UIManager::granularLatchBtnEventCb(lv_event_t* e) {
    lv_obj_t* latchBtn = (lv_obj_t*)lv_event_get_target(e);
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    bool isLatched = lv_obj_has_state(latchBtn, LV_STATE_CHECKED);
    
    if (ui->mGranularRecordBtn) {
        if (isLatched) {
            lv_obj_add_flag(ui->mGranularRecordBtn, LV_OBJ_FLAG_CHECKABLE);
            lv_obj_set_style_bg_color(ui->mGranularRecordBtn, lv_color_hex(0x881111), LV_STATE_CHECKED);
        } else {
            if (lv_obj_has_state(ui->mGranularRecordBtn, LV_STATE_CHECKED)) {
                lv_obj_clear_state(ui->mGranularRecordBtn, LV_STATE_CHECKED);
                ui->mEngine.stopRecordingSample(ui->mActiveTrack);
            }
            lv_obj_remove_flag(ui->mGranularRecordBtn, LV_OBJ_FLAG_CHECKABLE);
        }
    }
}

void UIManager::granularRecordBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    bool isLatched = ui->mGranularLatchBtn ? lv_obj_has_state(ui->mGranularLatchBtn, LV_STATE_CHECKED) : false;
    
    if (isLatched) {
        if (code == LV_EVENT_VALUE_CHANGED) {
            bool active = lv_obj_has_state(btn, LV_STATE_CHECKED);
            if (active) {
                ui->mEngine.startRecordingSample(ui->mActiveTrack);
                std::cout << "Latched Recording Started on Track " << ui->mActiveTrack + 1 << std::endl;
            } else {
                ui->mEngine.stopRecordingSample(ui->mActiveTrack);
                std::cout << "Latched Recording Stopped on Track " << ui->mActiveTrack + 1 << std::endl;
            }
        }
    } else {
        if (code == LV_EVENT_PRESSED) {
            ui->mEngine.startRecordingSample(ui->mActiveTrack);
            std::cout << "Momentary Recording Started on Track " << ui->mActiveTrack + 1 << std::endl;
        } else if (code == LV_EVENT_RELEASED) {
            ui->mEngine.stopRecordingSample(ui->mActiveTrack);
            std::cout << "Momentary Recording Stopped on Track " << ui->mActiveTrack + 1 << std::endl;
        }
    }
}

void UIManager::granularLoadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsSampleLoad = true;
    ui->mFileBrowserIsSampleSave = false;
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;
    ui->openFileBrowser(false);
}

void UIManager::granularSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsSampleLoad = false;
    ui->mFileBrowserIsSampleSave = true;
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;
    ui->openFileBrowser(true);
}

void UIManager::granularTrimBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.trimSample(ui->mActiveTrack);
    ui->mEngine.setParameter(ui->mActiveTrack, 400, 0.5f);
    ui->createCenterContentArea();
}

void UIManager::updateGranularWaveformPreview() {
    if (!mGranularWaveformContainer) return;

    std::vector<float> peaks = mEngine.getSamplerWaveform(mActiveTrack, 150);
    if (peaks.size() < 150) {
        peaks.resize(150, 0.0f);
    }

    float posPnt = mEngine.getTracks()[mActiveTrack].parameters[400];
    float sprayPnt = mEngine.getTracks()[mActiveTrack].parameters[415];
    
    float startPnt = posPnt - sprayPnt * 0.5f;
    float endPnt = posPnt + sprayPnt * 0.5f;
    if (startPnt < 0.0f) startPnt = 0.0f;
    if (endPnt > 1.0f) endPnt = 1.0f;

    lv_color_t trackColor = getTrackColor(mActiveTrack);

    for (int i = 0; i < 150; ++i) {
        lv_obj_t* bar = mGranularWaveformBars[i];
        if (!bar) continue;

        float amp = peaks[i];
        if (amp < 0.0f) amp = 0.0f;
        if (amp > 1.0f) amp = 1.0f;

        int h = (int)(amp * 120.0f);
        if (h < 2) h = 2;
        lv_obj_set_height(bar, h);

        float pos = (float)i / 150.0f;
        if (pos >= startPnt && pos <= endPnt) {
            lv_obj_set_style_bg_color(bar, trackColor, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(bar, lv_color_hex(0x444444), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
        }
    }

    if (mGranularStartLine) {
        int x = (int)(startPnt * 746.0f);
        lv_obj_align(mGranularStartLine, LV_ALIGN_LEFT_MID, x, 0);
    }
    if (mGranularEndLine) {
        int x = (int)(endPnt * 746.0f);
        lv_obj_align(mGranularEndLine, LV_ALIGN_LEFT_MID, x, 0);
    }

    GranularEngine::PlayheadInfo playheads[16];
    mEngine.getGranularPlayheads(mActiveTrack, playheads, 16);
    
    float activePos = -1.0f;
    float startPos = 0.0f;
    float endPos = 1.0f;
    
    for (int i = 0; i < 16; ++i) {
        if (playheads[i].pos >= 0.0f) {
            activePos = playheads[i].pos;
            startPos = playheads[i].start;
            endPos = playheads[i].end;
            break;
        }
    }
    
    if (activePos >= 0.0f) {
        if (mGranularPlayheadLine) {
            lv_obj_set_style_bg_color(mGranularPlayheadLine, trackColor, 0);
            int x = (int)(activePos * 746.0f);
            lv_obj_align(mGranularPlayheadLine, LV_ALIGN_LEFT_MID, x, 0);
            lv_obj_clear_flag(mGranularPlayheadLine, LV_OBJ_FLAG_HIDDEN);
        }
        if (mGranularPlayheadShade) {
            lv_obj_set_style_bg_color(mGranularPlayheadShade, trackColor, 0);
            lv_obj_set_style_bg_opa(mGranularPlayheadShade, 64, 0);
            float s = std::min(startPos, activePos);
            float e = std::max(startPos, activePos);
            int xs = (int)(s * 746.0f);
            int ws = (int)((e - s) * 746.0f);
            if (ws < 0) ws = 0;
            lv_obj_set_width(mGranularPlayheadShade, ws);
            lv_obj_align(mGranularPlayheadShade, LV_ALIGN_LEFT_MID, xs, 0);
            lv_obj_clear_flag(mGranularPlayheadShade, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (mGranularPlayheadLine) {
            lv_obj_add_flag(mGranularPlayheadLine, LV_OBJ_FLAG_HIDDEN);
        }
        if (mGranularPlayheadShade) {
            lv_obj_add_flag(mGranularPlayheadShade, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (mGranularRecordBtn) {
        bool isRec = mEngine.getIsRecordingSample();
        if (isRec) {
            lv_obj_set_style_bg_color(mGranularRecordBtn, lv_color_hex(0xFF3333), 0);
            lv_obj_t* recDot = lv_obj_get_child(mGranularRecordBtn, 0);
            if (recDot) {
                lv_obj_set_style_bg_color(recDot, lv_color_hex(0xFFFFFF), 0);
            }
        } else {
            bool isLatched = mGranularLatchBtn ? lv_obj_has_state(mGranularLatchBtn, LV_STATE_CHECKED) : false;
            if (isLatched) {
                lv_obj_set_style_bg_color(mGranularRecordBtn, lv_obj_has_state(mGranularRecordBtn, LV_STATE_CHECKED) ? lv_color_hex(0x881111) : lv_color_hex(0x2A1515), 0);
            } else {
                lv_obj_set_style_bg_color(mGranularRecordBtn, lv_color_hex(0x2A1515), 0);
            }
            lv_obj_t* recDot = lv_obj_get_child(mGranularRecordBtn, 0);
            if (recDot) {
                lv_obj_set_style_bg_color(recDot, lv_color_hex(0xFF3333), 0);
            }
        }
    }
}

void UIManager::populateParamSoundFontLibraryTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_row(tab, 16, 0);

    // 1. Actions Row (Top Bar)
    lv_obj_t* actionsRow = lv_obj_create(tab);
    lv_obj_set_size(actionsRow, 760, 50);
    lv_obj_set_style_bg_opa(actionsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionsRow, 0, 0);
    lv_obj_set_style_pad_all(actionsRow, 0, 0);
    lv_obj_remove_flag(actionsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(actionsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actionsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createActionBtn = [this, trackColor](lv_obj_t* parent, const char* labelText, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, 180, 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
        lv_obj_set_style_bg_color(btn, trackColor, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, labelText);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xEEEEEE), 0);
        lv_obj_center(label);
        
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
    };

    createActionBtn(actionsRow, "LOAD BANK", UIManager::soundfontLoadBtnCb);
    createActionBtn(actionsRow, "IMPORT BANK", UIManager::soundfontImportBtnCb);
    createActionBtn(actionsRow, "SELECT PRESET", UIManager::soundfontPresetSelectCb);

    // 2. Center Info Status Card
    lv_obj_t* infoCard = lv_obj_create(tab);
    lv_obj_set_size(infoCard, 760, 240);
    lv_obj_set_style_bg_color(infoCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(infoCard, trackColor, 0);
    lv_obj_set_style_border_width(infoCard, 1, 0);
    lv_obj_set_style_radius(infoCard, 16, 0);
    lv_obj_set_style_pad_all(infoCard, 16, 0);
    lv_obj_remove_flag(infoCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(infoCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(infoCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(infoCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(infoCard, 8, 0);

    // active bank label
    std::string bankPath = mEngine.getTracks()[mActiveTrack].lastSamplePath;
    std::string bankName = bankPath.empty() ? "None (Default GS)" : bankPath;
    size_t lastSlash = bankName.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        bankName = bankName.substr(lastSlash + 1);
    }

    mSoundFontActiveBankLbl = lv_label_create(infoCard);
    lv_label_set_text_fmt(mSoundFontActiveBankLbl, "ACTIVE BANK: %s", bankName.c_str());
    lv_obj_set_style_text_font(mSoundFontActiveBankLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mSoundFontActiveBankLbl, lv_color_hex(0xFFFFFF), 0);

    // active preset label
    int activeP = mEngine.getTracks()[mActiveTrack].soundFontEngine.getPresetIndex();
    std::string pName = mEngine.getSoundFontPresetName(mActiveTrack, activeP);
    if (pName.empty()) pName = "General User GS Default";

    mSoundFontActivePresetLbl = lv_label_create(infoCard);
    lv_label_set_text_fmt(mSoundFontActivePresetLbl, "PRESET: %d - %s", activeP, pName.c_str());
    lv_obj_set_style_text_font(mSoundFontActivePresetLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSoundFontActivePresetLbl, trackColor, 0);

    // 2.3 Row for encoder/knob parameters 180 and 181
    lv_obj_t* knobsRow = lv_obj_create(infoCard);
    lv_obj_set_size(knobsRow, 300, 100);
    lv_obj_set_style_bg_opa(knobsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobsRow, 0, 0);
    lv_obj_set_style_pad_all(knobsRow, 0, 0);
    lv_obj_remove_flag(knobsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobsRow, "PRESET", 180, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobsRow, "BANK", 181, 0.0f, 1.0f, 2, true);
}

void UIManager::populateParamSoundFontSynthTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);

    // Row 1: side-by-side LFO and Filter Cards
    lv_obj_t* row1 = lv_obj_create(tab);
    lv_obj_set_size(row1, 760, 205);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1.1 LFO Card
    lv_obj_t* lfoCard = lv_obj_create(row1);
    lv_obj_set_size(lfoCard, 370, 200);
    lv_obj_set_style_bg_color(lfoCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(lfoCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(lfoCard, 1, 0);
    lv_obj_set_style_radius(lfoCard, 10, 0);
    lv_obj_set_style_pad_all(lfoCard, 8, 0);
    lv_obj_remove_flag(lfoCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lfoTitle = lv_label_create(lfoCard);
    lv_label_set_text(lfoTitle, "FILTER LFO MODULATION");
    lv_obj_set_style_text_font(lfoTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lfoTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(lfoTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* lfoContent = lv_obj_create(lfoCard);
    lv_obj_set_size(lfoContent, 350, 165);
    lv_obj_set_style_bg_opa(lfoContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoContent, 0, 0);
    lv_obj_set_style_pad_all(lfoContent, 0, 0);
    lv_obj_align(lfoContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(lfoContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lfoContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lfoDropdownRow = lv_obj_create(lfoContent);
    lv_obj_set_size(lfoDropdownRow, 350, 52);
    lv_obj_set_style_bg_opa(lfoDropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoDropdownRow, 0, 0);
    lv_obj_set_style_pad_all(lfoDropdownRow, 0, 0);
    lv_obj_remove_flag(lfoDropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoDropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoDropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfoDropdownRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(lfoDropdownRow, "LFO SHAPE", 114, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false, 105);

    lv_obj_t* lfoKnobRow = lv_obj_create(lfoContent);
    lv_obj_set_size(lfoKnobRow, 350, 96);
    lv_obj_set_style_bg_opa(lfoKnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoKnobRow, 0, 0);
    lv_obj_set_style_pad_all(lfoKnobRow, 0, 0);
    lv_obj_remove_flag(lfoKnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoKnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoKnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfoKnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(lfoKnobRow, "LFO RATE", 7, 0.0f, 1.0f, 2, true);
    addSynthKnob(lfoKnobRow, "LFO DEPTH", 8, 0.0f, 1.0f, 2, true);

    // 1.2 Filter Card
    lv_obj_t* filterCard = lv_obj_create(row1);
    lv_obj_set_size(filterCard, 370, 200);
    lv_obj_set_style_bg_color(filterCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(filterCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(filterCard, 1, 0);
    lv_obj_set_style_radius(filterCard, 10, 0);
    lv_obj_set_style_pad_all(filterCard, 8, 0);
    lv_obj_remove_flag(filterCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* filterTitle = lv_label_create(filterCard);
    lv_label_set_text(filterTitle, "FILTER CONTROLS");
    lv_obj_set_style_text_font(filterTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(filterTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(filterTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* filterContent = lv_obj_create(filterCard);
    lv_obj_set_size(filterContent, 350, 165);
    lv_obj_set_style_bg_opa(filterContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterContent, 0, 0);
    lv_obj_set_style_pad_all(filterContent, 0, 0);
    lv_obj_align(filterContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(filterContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(filterContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* filterDropdownRow = lv_obj_create(filterContent);
    lv_obj_set_size(filterDropdownRow, 350, 52);
    lv_obj_set_style_bg_opa(filterDropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterDropdownRow, 0, 0);
    lv_obj_set_style_pad_all(filterDropdownRow, 0, 0);
    lv_obj_remove_flag(filterDropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterDropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterDropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterDropdownRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(filterDropdownRow, "FILTER TYPE", 20, "LowPass\nHighPass\nBandPass\nBypass", 0, false, 105);

    lv_obj_t* filterKnobRow = lv_obj_create(filterContent);
    lv_obj_set_size(filterKnobRow, 350, 96);
    lv_obj_set_style_bg_opa(filterKnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterKnobRow, 0, 0);
    lv_obj_set_style_pad_all(filterKnobRow, 0, 0);
    lv_obj_remove_flag(filterKnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterKnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterKnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterKnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(filterKnobRow, "CUTOFF", 112, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobRow, "RESONANCE", 113, 0.0f, 1.0f, 2, true);

    // Row 2: ADSR card
    lv_obj_t* adsrCard = lv_obj_create(tab);
    lv_obj_set_size(adsrCard, 760, 235);
    lv_obj_set_style_bg_color(adsrCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(adsrCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(adsrCard, 1, 0);
    lv_obj_set_style_radius(adsrCard, 10, 0);
    lv_obj_set_style_pad_all(adsrCard, 8, 0);
    lv_obj_remove_flag(adsrCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* adsrTitle = lv_label_create(adsrCard);
    lv_label_set_text(adsrTitle, "ADSR ENVELOPE CONTROLS");
    lv_obj_set_style_text_font(adsrTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(adsrTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(adsrTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* faderRow = lv_obj_create(adsrCard);
    lv_obj_set_size(faderRow, 740, 195);
    lv_obj_set_style_bg_opa(faderRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow, 0, 0);
    lv_obj_set_style_pad_all(faderRow, 0, 0);
    lv_obj_remove_flag(faderRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(faderRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(faderRow, "ATTACK", 100, 0.001f, 4.0f, 2, false, 140);
    addSynthSlider(faderRow, "DECAY", 101, 0.0f, 4.0f, 2, false, 140);
    addSynthSlider(faderRow, "SUSTAIN", 102, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(faderRow, "RELEASE", 103, 0.001f, 4.0f, 2, false, 140);
}

void UIManager::soundfontLoadBtnCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsSfSelect = true;
    ui->mFileBrowserIsSfImport = false;
    ui->mFileBrowserIsSampleLoad = false;
    ui->mFileBrowserIsSampleSave = false;
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;

    const char* browseDir = getenv("HOME");
    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
    ui->mFileBrowserCurrentPath = homeStr + "/soundfonts";
    ui->openFileBrowser(false);
}

void UIManager::soundfontImportBtnCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsSfSelect = false;
    ui->mFileBrowserIsSfImport = true;
    ui->mFileBrowserIsSampleLoad = false;
    ui->mFileBrowserIsSampleSave = false;
    ui->mFileBrowserIsWtSelect = false;
    ui->mFileBrowserIsWtImport = false;
    ui->mFileBrowserIsFmImport = false;

    const char* browseDir = getenv("HOME");
    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
    ui->mFileBrowserCurrentPath = homeStr;
    ui->openFileBrowser(false);
}

void UIManager::soundfontPresetSelectCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    int presetCount = ui->mEngine.getSoundFontPresetCount(ui->mActiveTrack);
    if (presetCount <= 0) {
        lv_obj_t* overlay = lv_obj_create(lv_screen_active());
        lv_obj_set_size(overlay, 1024, 600);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
        ui->mSoundFontPresetModal = overlay;

        lv_obj_t* card = lv_obj_create(overlay);
        lv_obj_set_size(card, 400, 200);
        lv_obj_center(card);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, "NO SOUNDFONT BANK LOADED\n\nPlease load a SoundFont bank first using the 'LOAD BANK' or 'IMPORT BANK' buttons.");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_width(lbl, 340);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -20);

        lv_obj_t* closeBtn = lv_button_create(card);
        lv_obj_set_size(closeBtn, 100, 36);
        lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333333), 0);
        lv_obj_t* closeLbl = lv_label_create(closeBtn);
        lv_label_set_text(closeLbl, "OK");
        lv_obj_center(closeLbl);

        auto dismissCb = [](lv_event_t* ev) {
            UIManager* u = (UIManager*)lv_event_get_user_data(ev);
            if (u->mSoundFontPresetModal) {
                lv_obj_delete(u->mSoundFontPresetModal);
                u->mSoundFontPresetModal = nullptr;
            }
        };
        lv_obj_add_event_cb(closeBtn, dismissCb, LV_EVENT_CLICKED, ui);
        return;
    }

    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    ui->mSoundFontPresetModal = overlay;

    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 600, 480);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(card, trackColor, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(card);
    lv_obj_set_size(header, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "SELECT PRESET");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xEEEEEE), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* closeBtn = lv_button_create(header);
    lv_obj_set_size(closeBtn, 36, 36);
    lv_obj_align(closeBtn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(closeBtn, 18, 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, "X");
    lv_obj_center(closeLbl);

    auto dismissCb = [](lv_event_t* ev) {
        UIManager* u = (UIManager*)lv_event_get_user_data(ev);
        if (u->mSoundFontPresetModal) {
            lv_obj_delete(u->mSoundFontPresetModal);
            u->mSoundFontPresetModal = nullptr;
        }
    };
    lv_obj_add_event_cb(closeBtn, dismissCb, LV_EVENT_CLICKED, ui);

    lv_obj_t* gridCont = lv_obj_create(card);
    lv_obj_set_size(gridCont, lv_pct(100), 390);
    lv_obj_align(gridCont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(gridCont, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_color(gridCont, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(gridCont, 1, 0);
    lv_obj_set_style_pad_all(gridCont, 8, 0);
    lv_obj_set_layout(gridCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridCont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(gridCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(gridCont, 8, 0);
    lv_obj_set_style_pad_row(gridCont, 8, 0);

    int activeP = ui->mEngine.getTracks()[ui->mActiveTrack].soundFontEngine.getPresetIndex();

    for (int p = 0; p < presetCount; ++p) {
        std::string name = ui->mEngine.getSoundFontPresetName(ui->mActiveTrack, p);
        if (name.empty()) name = "Preset " + std::to_string(p);

        lv_obj_t* btn = lv_button_create(gridCont);
        lv_obj_set_size(btn, 130, 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d: %s", p, name.c_str());
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(label, 114);
        lv_obj_center(label);

        if (p == activeP) {
            lv_obj_set_style_border_color(btn, trackColor, 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_text_color(label, trackColor, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
        }

        lv_obj_set_user_data(btn, (void*)(uintptr_t)p);
        lv_obj_add_event_cb(btn, UIManager::soundfontPresetItemSelectCb, LV_EVENT_CLICKED, ui);
    }
}

void UIManager::soundfontPresetItemSelectCb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    uintptr_t p = (uintptr_t)lv_obj_get_user_data(btn);

    ui->mEngine.setSoundFontPreset(ui->mActiveTrack, (int)p);

    int presetCount = ui->mEngine.getSoundFontPresetCount(ui->mActiveTrack);
    if (presetCount > 1) {
        float presetVal = (float)p / (presetCount - 1);
        ui->mEngine.setParameter(ui->mActiveTrack, 180, presetVal, true);
    } else {
        ui->mEngine.setParameter(ui->mActiveTrack, 180, 0.0f, true);
    }

    if (ui->mSoundFontActivePresetLbl) {
        std::string pName = ui->mEngine.getSoundFontPresetName(ui->mActiveTrack, (int)p);
        lv_label_set_text_fmt(ui->mSoundFontActivePresetLbl, "PRESET: %d - %s", (int)p, pName.c_str());
    }

    if (ui->mSoundFontPresetModal) {
        lv_obj_delete(ui->mSoundFontPresetModal);
        ui->mSoundFontPresetModal = nullptr;
    }
}

void UIManager::fmPresetSelectCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_color_t trackColor = ui->getTrackColor(ui->mActiveTrack);

    // Overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    ui->mFmPresetModal = overlay;

    // Card
    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 640, 480);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(card, trackColor, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t* header = lv_obj_create(card);
    lv_obj_set_size(header, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "SELECT FM PRESET");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xEEEEEE), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* closeBtn = lv_button_create(header);
    lv_obj_set_size(closeBtn, 36, 36);
    lv_obj_align(closeBtn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(closeBtn, 18, 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, "X");
    lv_obj_center(closeLbl);

    auto dismissCb = [](lv_event_t* ev) {
        UIManager* u = (UIManager*)lv_event_get_user_data(ev);
        if (u->mFmPresetModal) {
            lv_obj_delete(u->mFmPresetModal);
            u->mFmPresetModal = nullptr;
        }
    };
    lv_obj_add_event_cb(closeBtn, dismissCb, LV_EVENT_CLICKED, ui);

    // Scrollable grid container
    lv_obj_t* gridCont = lv_obj_create(card);
    lv_obj_set_size(gridCont, lv_pct(100), 390);
    lv_obj_align(gridCont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(gridCont, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_color(gridCont, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(gridCont, 1, 0);
    lv_obj_set_style_pad_all(gridCont, 8, 0);
    lv_obj_set_layout(gridCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridCont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(gridCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(gridCont, 8, 0);
    lv_obj_set_style_pad_row(gridCont, 8, 0);

    int activePresetIdx = s_activeFmPreset[ui->mActiveTrack];
    const auto& custom = ui->mEngine.getTracks()[ui->mActiveTrack].fmEngine.mCustomPresets;
    int totalCount = 32 + (int)custom.size();

    for (int p = 0; p < totalCount; ++p) {
        std::string name = (p < 32) ? FM_PRESET_NAMES[p] : custom[p - 32].name;

        if (p < 32) {
            lv_obj_t* btn = lv_button_create(gridCont);
            lv_obj_set_size(btn, 134, 40);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text_fmt(label, "%d: %s", p, name.c_str());
            lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_width(label, 118);
            lv_obj_center(label);

            if (p == activePresetIdx) {
                lv_obj_set_style_border_color(btn, trackColor, 0);
                lv_obj_set_style_border_width(btn, 2, 0);
                lv_obj_set_style_text_color(label, trackColor, 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
            }

            lv_obj_set_user_data(btn, (void*)(uintptr_t)p);
            lv_obj_add_event_cb(btn, UIManager::fmPresetItemSelectCb, LV_EVENT_CLICKED, ui);
        } else {
            // Container for custom preset + delete button
            lv_obj_t* cont = lv_obj_create(gridCont);
            lv_obj_set_size(cont, 134, 40);
            lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(cont, 0, 0);
            lv_obj_set_style_pad_all(cont, 0, 0);
            lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

            // Select button
            lv_obj_t* btn = lv_button_create(cont);
            lv_obj_set_size(btn, 98, 40);
            lv_obj_align(btn, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text_fmt(label, "%d: %s", p, name.c_str());
            lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_width(label, 82);
            lv_obj_center(label);

            if (p == activePresetIdx) {
                lv_obj_set_style_border_color(btn, trackColor, 0);
                lv_obj_set_style_border_width(btn, 2, 0);
                lv_obj_set_style_text_color(label, trackColor, 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
            }

            lv_obj_set_user_data(btn, (void*)(uintptr_t)p);
            lv_obj_add_event_cb(btn, UIManager::fmPresetItemSelectCb, LV_EVENT_CLICKED, ui);

            // Delete button
            lv_obj_t* delBtn = lv_button_create(cont);
            lv_obj_set_size(delBtn, 32, 40);
            lv_obj_align(delBtn, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_style_bg_color(delBtn, lv_color_hex(0x661111), 0);
            lv_obj_set_style_radius(delBtn, 6, 0);
            lv_obj_t* delLbl = lv_label_create(delBtn);
            lv_label_set_text(delLbl, "X");
            lv_obj_center(delLbl);

            struct FmDeleteData {
                UIManager* ui;
                int customIdx;
            };
            FmDeleteData* fDel = new FmDeleteData{ui, p - 32};

            auto fmDelCb = [](lv_event_t* ev) {
                FmDeleteData* d = (FmDeleteData*)lv_event_get_user_data(ev);
                if (d) {
                    UIManager* ui = d->ui;
                    const char* browseDir = getenv("HOME");
                    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
                    std::string presetsFmDir = homeStr + "/presets/fm";
                    ui->mEngine.getTracks()[ui->mActiveTrack].fmEngine.deleteCustomPreset(d->customIdx, presetsFmDir);
                    
                    if (ui->mFmPresetModal) {
                        lv_obj_delete(ui->mFmPresetModal);
                        ui->mFmPresetModal = nullptr;
                    }
                    // Call fmPresetSelectCb with nullptr event to recreate
                    lv_event_t dummyEvent;
                    memset(&dummyEvent, 0, sizeof(dummyEvent));
                    dummyEvent.user_data = ui;
                    ui->fmPresetSelectCb(&dummyEvent);
                }
            };
            lv_obj_add_event_cb(delBtn, fmDelCb, LV_EVENT_CLICKED, fDel);

            auto freeFmDelCb = [](lv_event_t* ev) {
                FmDeleteData* d = (FmDeleteData*)lv_event_get_user_data(ev);
                delete d;
            };
            lv_obj_add_event_cb(delBtn, freeFmDelCb, LV_EVENT_DELETE, fDel);
        }
    }
}

void UIManager::fmPresetItemSelectCb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    uintptr_t p = (uintptr_t)lv_obj_get_user_data(btn);

    s_activeFmPreset[ui->mActiveTrack] = (int)p;
    ui->mEngine.loadFmPreset(ui->mActiveTrack, (int)p);

    if (ui->mFmActivePresetLbl) {
        const auto& custom = ui->mEngine.getTracks()[ui->mActiveTrack].fmEngine.mCustomPresets;
        std::string pName = ((int)p < 32) ? FM_PRESET_NAMES[(int)p] : custom[(int)p - 32].name;
        lv_label_set_text_fmt(ui->mFmActivePresetLbl, "PRESET: %d - %s", (int)p, pName.c_str());
    }

    if (ui->mFmPresetModal) {
        lv_obj_delete(ui->mFmPresetModal);
        ui->mFmPresetModal = nullptr;
    }

    ui->mNeedsScreenRebuild = true;
}

void UIManager::populateParamAudioInTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);

    // Row 1: Source / Gain / Fold card
    lv_obj_t* ioCard = lv_obj_create(tab);
    lv_obj_set_size(ioCard, 760, 200);
    lv_obj_set_style_bg_color(ioCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(ioCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(ioCard, 1, 0);
    lv_obj_set_style_radius(ioCard, 10, 0);
    lv_obj_set_style_pad_all(ioCard, 8, 0);
    lv_obj_remove_flag(ioCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ioTitle = lv_label_create(ioCard);
    lv_label_set_text(ioTitle, "AUDIO INPUT CONTROL");
    lv_obj_set_style_text_font(ioTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ioTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(ioTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* ioContent = lv_obj_create(ioCard);
    lv_obj_set_size(ioContent, 740, 165);
    lv_obj_set_style_bg_opa(ioContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ioContent, 0, 0);
    lv_obj_set_style_pad_all(ioContent, 0, 0);
    lv_obj_align(ioContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(ioContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ioContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ioContent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ioContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left half: Source and Gate Mode Selectors
    lv_obj_t* selectorsCol = lv_obj_create(ioContent);
    lv_obj_set_size(selectorsCol, 360, 150);
    lv_obj_set_style_bg_opa(selectorsCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(selectorsCol, 0, 0);
    lv_obj_set_style_pad_all(selectorsCol, 0, 0);
    lv_obj_remove_flag(selectorsCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(selectorsCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selectorsCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(selectorsCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Source Selector Dropdown
    lv_obj_t* sourceRow = lv_obj_create(selectorsCol);
    lv_obj_set_size(sourceRow, 360, 60);
    lv_obj_set_style_bg_opa(sourceRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sourceRow, 0, 0);
    lv_obj_set_style_pad_all(sourceRow, 0, 0);
    lv_obj_remove_flag(sourceRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sourceRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sourceRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sourceRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* srcLbl = lv_label_create(sourceRow);
    lv_label_set_text(srcLbl, "INPUT SELECTOR");
    lv_obj_set_style_text_font(srcLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(srcLbl, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t* srcDd = lv_dropdown_create(sourceRow);
    lv_obj_set_size(srcDd, 180, 36);
    lv_dropdown_set_options(srcDd, "MICROPHONE\nLINE-IN\nRESAMPLING");
    int currentSrc = mEngine.mRecordingSource.load();
    if (currentSrc > 2) currentSrc = 0;
    lv_dropdown_set_selected(srcDd, currentSrc);
    lv_obj_set_style_bg_color(srcDd, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(srcDd, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(srcDd, 1, 0);
    lv_obj_set_style_radius(srcDd, 6, 0);
    lv_obj_set_style_text_font(srcDd, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(srcDd, lv_color_hex(0xEEEEEE), 0);
    lv_obj_add_event_cb(srcDd, UIManager::audioInSourceDropdownEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Gate mode selector
    lv_obj_t* gateRow = lv_obj_create(selectorsCol);
    lv_obj_set_size(gateRow, 360, 60);
    lv_obj_set_style_bg_opa(gateRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gateRow, 0, 0);
    lv_obj_set_style_pad_all(gateRow, 0, 0);
    lv_obj_remove_flag(gateRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(gateRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gateRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gateRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* gateLbl = lv_label_create(gateRow);
    lv_label_set_text(gateLbl, "MODE / GATE TYPE");
    lv_obj_set_style_text_font(gateLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(gateLbl, lv_color_hex(0xCCCCCC), 0);

    addSynthDropdown(gateRow, "GATE MODE", 120, "GATED\nOPEN", 0, false, 180);

    // Right half: Gain & Fold Knobs
    lv_obj_t* knobsRow = lv_obj_create(ioContent);
    lv_obj_set_size(knobsRow, 340, 150);
    lv_obj_set_style_bg_opa(knobsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobsRow, 0, 0);
    lv_obj_set_style_pad_all(knobsRow, 0, 0);
    lv_obj_remove_flag(knobsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobsRow, "GAIN", 121, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobsRow, "FOLD", 122, 0.0f, 1.0f, 2, true);

    // Row 2: Character EQ card
    lv_obj_t* eqCard = lv_obj_create(tab);
    lv_obj_set_size(eqCard, 760, 235);
    lv_obj_set_style_bg_color(eqCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(eqCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(eqCard, 1, 0);
    lv_obj_set_style_radius(eqCard, 10, 0);
    lv_obj_set_style_pad_all(eqCard, 8, 0);
    lv_obj_remove_flag(eqCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* eqTitle = lv_label_create(eqCard);
    lv_label_set_text(eqTitle, "CHARACTER EQ CONTROLS (+/- 12dB)");
    lv_obj_set_style_text_font(eqTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(eqTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(eqTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* eqFadersRow = lv_obj_create(eqCard);
    lv_obj_set_size(eqFadersRow, 740, 195);
    lv_obj_set_style_bg_opa(eqFadersRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eqFadersRow, 0, 0);
    lv_obj_set_style_pad_all(eqFadersRow, 0, 0);
    lv_obj_remove_flag(eqFadersRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(eqFadersRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(eqFadersRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eqFadersRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(eqFadersRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(eqFadersRow, "LOW", 1530, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(eqFadersRow, "L-MID", 1531, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(eqFadersRow, "MID", 1532, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(eqFadersRow, "H-MID", 1533, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(eqFadersRow, "HIGH", 1534, 0.0f, 1.0f, 2, true, 140);
}

void UIManager::populateParamAudioInFilterEnvTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);

    // Row 1: Filter Card
    lv_obj_t* filterCard = lv_obj_create(tab);
    lv_obj_set_size(filterCard, 760, 200);
    lv_obj_set_style_bg_color(filterCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(filterCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(filterCard, 1, 0);
    lv_obj_set_style_radius(filterCard, 10, 0);
    lv_obj_set_style_pad_all(filterCard, 8, 0);
    lv_obj_remove_flag(filterCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* filterTitle = lv_label_create(filterCard);
    lv_label_set_text(filterTitle, "FILTER CONTROLS");
    lv_obj_set_style_text_font(filterTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(filterTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(filterTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* filterContent = lv_obj_create(filterCard);
    lv_obj_set_size(filterContent, 740, 165);
    lv_obj_set_style_bg_opa(filterContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterContent, 0, 0);
    lv_obj_set_style_pad_all(filterContent, 0, 0);
    lv_obj_align(filterContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(filterContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterContent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left side: Type Selector
    lv_obj_t* typeBox = lv_obj_create(filterContent);
    lv_obj_set_size(typeBox, 220, 150);
    lv_obj_set_style_bg_opa(typeBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(typeBox, 0, 0);
    lv_obj_set_style_pad_all(typeBox, 0, 0);
    lv_obj_remove_flag(typeBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(typeBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(typeBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(typeBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(typeBox, "FILTER MODE", 123, "LP (Lowpass)\nHP (Highpass)\nBP (Bandpass)", 0, false, 180);

    // Right side: Cutoff, Resonance, Env Amount Knobs
    lv_obj_t* filterKnobs = lv_obj_create(filterContent);
    lv_obj_set_size(filterKnobs, 480, 150);
    lv_obj_set_style_bg_opa(filterKnobs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterKnobs, 0, 0);
    lv_obj_set_style_pad_all(filterKnobs, 0, 0);
    lv_obj_remove_flag(filterKnobs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterKnobs, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterKnobs, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterKnobs, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(filterKnobs, "CUTOFF", 112, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobs, "RESONANCE", 113, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobs, "ENV AMOUNT", 118, -1.0f, 1.0f, 2, false);

    // Row 2: Twin Envelope Cards
    lv_obj_t* envsRow = lv_obj_create(tab);
    lv_obj_set_size(envsRow, 760, 235);
    lv_obj_set_style_bg_opa(envsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(envsRow, 0, 0);
    lv_obj_set_style_pad_all(envsRow, 0, 0);
    lv_obj_remove_flag(envsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(envsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(envsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(envsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 2.1 Amp Envelope Card
    lv_obj_t* ampCard = lv_obj_create(envsRow);
    lv_obj_set_size(ampCard, 370, 235);
    lv_obj_set_style_bg_color(ampCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(ampCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(ampCard, 1, 0);
    lv_obj_set_style_radius(ampCard, 10, 0);
    lv_obj_set_style_pad_all(ampCard, 8, 0);
    lv_obj_remove_flag(ampCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ampTitle = lv_label_create(ampCard);
    lv_label_set_text(ampTitle, "AMP ENVELOPE (ADSR)");
    lv_obj_set_style_text_font(ampTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ampTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(ampTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* ampRow = lv_obj_create(ampCard);
    lv_obj_set_size(ampRow, 350, 195);
    lv_obj_set_style_bg_opa(ampRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ampRow, 0, 0);
    lv_obj_set_style_pad_all(ampRow, 0, 0);
    lv_obj_remove_flag(ampRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ampRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ampRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ampRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(ampRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(ampRow, "A", 100, 0.001f, 4.0f, 2, false, 140);
    addSynthSlider(ampRow, "D", 101, 0.0f, 4.0f, 2, false, 140);
    addSynthSlider(ampRow, "S", 102, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(ampRow, "R", 103, 0.001f, 8.0f, 2, false, 140);

    // 2.2 Filter Envelope Card
    lv_obj_t* filterEnvCard = lv_obj_create(envsRow);
    lv_obj_set_size(filterEnvCard, 370, 235);
    lv_obj_set_style_bg_color(filterEnvCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(filterEnvCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(filterEnvCard, 1, 0);
    lv_obj_set_style_radius(filterEnvCard, 10, 0);
    lv_obj_set_style_pad_all(filterEnvCard, 8, 0);
    lv_obj_remove_flag(filterEnvCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* filterEnvTitle = lv_label_create(filterEnvCard);
    lv_label_set_text(filterEnvTitle, "FILTER ENVELOPE (ADSR)");
    lv_obj_set_style_text_font(filterEnvTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(filterEnvTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(filterEnvTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* filterEnvRow = lv_obj_create(filterEnvCard);
    lv_obj_set_size(filterEnvRow, 350, 195);
    lv_obj_set_style_bg_opa(filterEnvRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterEnvRow, 0, 0);
    lv_obj_set_style_pad_all(filterEnvRow, 0, 0);
    lv_obj_remove_flag(filterEnvRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterEnvRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterEnvRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterEnvRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(filterEnvRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(filterEnvRow, "A", 114, 0.001f, 4.0f, 2, false, 140);
    addSynthSlider(filterEnvRow, "D", 115, 0.001f, 4.0f, 2, false, 140);
    addSynthSlider(filterEnvRow, "S", 116, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(filterEnvRow, "R", 117, 0.001f, 8.0f, 2, false, 140);
}

void UIManager::audioInSourceDropdownEventCb(lv_event_t* e) {
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    int sel = lv_dropdown_get_selected(dd);
    ui->mEngine.setRecordingSource(sel); // 0 = MIC, 1 = LINE_IN, 2 = RESAMPLE
    const char* srcNames[] = {"MIC", "LINE-IN", "RESAMPLE"};
    std::cout << "Audio Input Source changed to: " << srcNames[sel % 3] << std::endl;
}

void UIManager::populateParamFmDrumTab1(lv_obj_t* tab) {
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    addDrumVoiceStrip(tab, "KICK", 0);
    addDrumVoiceStrip(tab, "SNARE", 1);
    addDrumVoiceStrip(tab, "TOM", 2);
    addDrumVoiceStrip(tab, "HIHAT", 3);
}

void UIManager::populateParamFmDrumTab2(lv_obj_t* tab) {
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    addDrumVoiceStrip(tab, "OHH", 4);
    addDrumVoiceStrip(tab, "CYMB", 5);
    addDrumVoiceStrip(tab, "PERC", 6);
    addDrumVoiceStrip(tab, "NOISE", 7);
}

void UIManager::addDrumVoiceStrip(lv_obj_t* parent, const char* name, int drumIdx) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 175, 450);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 12, 0);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    // 4 Knobs (Pitch, Snap, Decay, Level)
    int baseParam = 200 + drumIdx * 10;
    
    auto addMiniDrumKnob = [this, card](const char* lblText, int paramId) {
        addSynthKnob(card, lblText, paramId, 0.0f, 1.0f, 2, true);
    };

    addMiniDrumKnob("PITCH", baseParam + 0);
    addMiniDrumKnob("SNAP", baseParam + 1);
    addMiniDrumKnob("DECAY", baseParam + 2);
    addMiniDrumKnob("LVL", baseParam + 5);
}

void UIManager::populateParamAnalogDrumTab1(lv_obj_t* tab) {
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    addAnalogDrumVoiceStrip(tab, "KICK", 0);
    addAnalogDrumVoiceStrip(tab, "SNARE", 1);
    addAnalogDrumVoiceStrip(tab, "RIM", 2);
}

void UIManager::populateParamAnalogDrumTab2(lv_obj_t* tab) {
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    addAnalogDrumVoiceStrip(tab, "HAT C", 3);
    addAnalogDrumVoiceStrip(tab, "HAT O", 4);
    addAnalogDrumVoiceStrip(tab, "CYMBAL", 5);
}

void UIManager::addAnalogDrumVoiceStrip(lv_obj_t* parent, const char* name, int drumIdx) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 230, 450);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 10, 0);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    // 4 Knobs (or 3 for Hats)
    int baseParam = 600 + drumIdx * 10;
    
    auto addMiniDrumKnob = [this, card](const char* lblText, int paramId) {
        addSynthKnob(card, lblText, paramId, 0.0f, 1.0f, 2, true);
    };

    if (drumIdx == 0) { // KICK: DCY (0), TONE (1), TUNE (2), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("TONE", baseParam + 1);
        addMiniDrumKnob("TUNE", baseParam + 2);
        addMiniDrumKnob("GAIN", baseParam + 5);
    } else if (drumIdx == 1) { // SNARE: DCY (0), SNAP (3), TUNE (2), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("SNAP", baseParam + 3);
        addMiniDrumKnob("TUNE", baseParam + 2);
        addMiniDrumKnob("GAIN", baseParam + 5);
    } else if (drumIdx == 2) { // RIM: DCY (0), COL (1), TUNE (2), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("COL", baseParam + 1);
        addMiniDrumKnob("TUNE", baseParam + 2);
        addMiniDrumKnob("GAIN", baseParam + 5);
    } else if (drumIdx == 3 || drumIdx == 4) { // HAT C & HAT O: DCY (0), COL (1), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("COL", baseParam + 1);
        addMiniDrumKnob("GAIN", baseParam + 5);
        
        // Add a spacer to keep layouts perfectly aligned!
        lv_obj_t* spacer = lv_obj_create(card);
        lv_obj_set_size(spacer, 74, 95);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
    } else if (drumIdx == 5) { // CYMBAL: ATK (3), DCY (0), COL (1), GAIN (5)
        addMiniDrumKnob("ATK", baseParam + 3);
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("COL", baseParam + 1);
        addMiniDrumKnob("GAIN", baseParam + 5);
    }
}

void UIManager::randomizeParamsBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    int activeTrk = ui->mActiveTrack;
    if (activeTrk < 0 || activeTrk >= (int)ui->mEngine.getTracks().size()) return;

    int engineType = ui->mEngine.getTracks()[activeTrk].engineType;

    // Guard: MIDI Out (engineType 7) has no sound params — skip
    if (engineType == 7) {
        std::cout << "UIManager: Skipping randomize for MIDI Out engine" << std::endl;
        return;
    }

    auto r = []() -> float {
        return (float)rand() / (float)RAND_MAX;
    };

    auto rRange = [](float minV, float maxV) -> float {
        return minV + ((float)rand() / (float)RAND_MAX) * (maxV - minV);
    };

    // Subtractive (0)
    if (engineType == 0) {
        ui->mEngine.setParameter(activeTrk, 1, r());      // Detune 1
        ui->mEngine.setParameter(activeTrk, 3, r());      // Detune 2
        ui->mEngine.setParameter(activeTrk, 4, rRange(0.0f, 1.0f)); // Semi 2
        ui->mEngine.setParameter(activeTrk, 5, r());      // Osc Mix
        ui->mEngine.setParameter(activeTrk, 10, (float)(rand() % 4) / 3.0f); // Osc 1 Shape
        ui->mEngine.setParameter(activeTrk, 11, (float)(rand() % 4) / 3.0f); // Osc 2 Shape
        ui->mEngine.setParameter(activeTrk, 12, rRange(0.15f, 0.95f)); // Filter Cutoff
        ui->mEngine.setParameter(activeTrk, 13, rRange(0.0f, 0.85f));  // Resonance
        ui->mEngine.setParameter(activeTrk, 20, (float)(rand() % 4) / 3.0f); // Filter Type (LP, HP, BP, Bypass)
        ui->mEngine.setParameter(activeTrk, 18, rRange(-1.0f, 1.0f)); // Filter Env Amt
        
        // Amp ADSR
        ui->mEngine.setParameter(activeTrk, 100, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 101, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 102, rRange(0.1f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 3.0f));  // R

        // Filter ADSR
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 3.0f));  // R

        // LFO
        ui->mEngine.setParameter(activeTrk, 7, rRange(0.05f, 0.8f));    // LFO Rate
        ui->mEngine.setParameter(activeTrk, 8, rRange(0.0f, 0.7f));     // LFO Depth
        ui->mEngine.setParameter(activeTrk, 19, (float)(rand() % 5) / 4.0f); // LFO Shape
    }
    // FM (1)
    else if (engineType == 1) {
        ui->mEngine.setParameter(activeTrk, 150, (float)(rand() % 6)); // Algorithm (0-5)
        ui->mEngine.setParameter(activeTrk, 151, rRange(0.15f, 0.95f)); // Cutoff
        ui->mEngine.setParameter(activeTrk, 152, rRange(0.0f, 0.85f));  // Resonance
        ui->mEngine.setParameter(activeTrk, 154, rRange(0.0f, 0.85f));  // Feedback
        ui->mEngine.setParameter(activeTrk, 157, rRange(0.1f, 0.9f));   // Brightness
        ui->mEngine.setParameter(activeTrk, 158, rRange(0.0f, 0.5f));   // Detune
        ui->mEngine.setParameter(activeTrk, 159, rRange(0.0f, 0.8f));   // Feedback Drive

        // Main Amp ADSR
        ui->mEngine.setParameter(activeTrk, 100, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 101, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 102, rRange(0.2f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 3.0f));  // R

        // Filter EG
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 3.0f));  // R
        ui->mEngine.setParameter(activeTrk, 118, rRange(-1.0f, 1.0f)); // Env Amt

        // Active/Carrier masks (Ensure at least Op 1 is active)
        int activeMask = 1 | (rand() % 64);
        int carrierMask = 1 | (rand() % activeMask);
        ui->mEngine.setParameter(activeTrk, 155, (float)activeMask);
        ui->mEngine.setParameter(activeTrk, 153, (float)carrierMask);

        // Randomize all 6 operators
        for (int op = 0; op < 6; ++op) {
            int base = 160 + op * 6;
            ui->mEngine.setParameter(activeTrk, base + 0, rRange(0.0f, 1.0f));   // Level
            ui->mEngine.setParameter(activeTrk, base + 1, rRange(0.001f, 2.0f)); // Attack
            ui->mEngine.setParameter(activeTrk, base + 2, rRange(0.01f, 3.0f));  // Decay
            ui->mEngine.setParameter(activeTrk, base + 3, rRange(0.0f, 1.0f));   // Sustain
            ui->mEngine.setParameter(activeTrk, base + 4, rRange(0.01f, 4.0f));  // Release
            
            float ratioOptions[] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f};
            float selRatio = ratioOptions[rand() % 10];
            ui->mEngine.setParameter(activeTrk, base + 5, selRatio / 16.0f);    // Ratio normalized
        }
    }
    // Sampler (2)
    else if (engineType == 2) {
        ui->mEngine.setParameter(activeTrk, 355, rRange(0.0f, 0.5f));   // Glide
        ui->mEngine.setParameter(activeTrk, 20, (float)(rand() % 4) / 3.0f); // Filter Type
        ui->mEngine.setParameter(activeTrk, 112, rRange(0.15f, 0.95f)); // Cutoff
        ui->mEngine.setParameter(activeTrk, 113, rRange(0.0f, 0.85f));  // Resonance
        ui->mEngine.setParameter(activeTrk, 118, rRange(-1.0f, 1.0f)); // Filter Env Amt

        // Amp ADSR
        ui->mEngine.setParameter(activeTrk, 100, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 101, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 102, rRange(0.1f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 3.0f));  // R

        // Filter ADSR
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 3.0f));  // R
    }
    // Granular (3)
    else if (engineType == 3) {
        ui->mEngine.setParameter(activeTrk, 400, rRange(0.0f, 0.8f));   // Pitch jitter
        ui->mEngine.setParameter(activeTrk, 401, rRange(0.0f, 0.8f));   // Spray
        ui->mEngine.setParameter(activeTrk, 402, rRange(0.1f, 0.9f));   // Density
        ui->mEngine.setParameter(activeTrk, 403, rRange(0.1f, 0.9f));   // Size
        ui->mEngine.setParameter(activeTrk, 404, rRange(0.0f, 1.0f));   // Envelope
        ui->mEngine.setParameter(activeTrk, 405, rRange(0.05f, 0.85f)); // Speed
        ui->mEngine.setParameter(activeTrk, 406, (float)(rand() % 3) / 2.0f); // Direction
        ui->mEngine.setParameter(activeTrk, 407, rRange(0.1f, 0.9f));   // Overlap
        ui->mEngine.setParameter(activeTrk, 408, rRange(0.0f, 1.0f));   // Scan rate
    }
    // Wavetable (4)
    else if (engineType == 4) {
        ui->mEngine.setParameter(activeTrk, 450, rRange(0.0f, 1.0f));   // Position
        ui->mEngine.setParameter(activeTrk, 451, rRange(0.05f, 0.85f)); // Speed
        ui->mEngine.setParameter(activeTrk, 452, (float)(rand() % 6) / 5.0f); // Warp mode
        ui->mEngine.setParameter(activeTrk, 453, rRange(0.0f, 1.0f));   // Warp amount
        ui->mEngine.setParameter(activeTrk, 454, rRange(0.0f, 0.6f));   // Detune
        ui->mEngine.setParameter(activeTrk, 455, (float)(rand() % 8) / 7.0f); // Unison voices
        ui->mEngine.setParameter(activeTrk, 456, rRange(0.0f, 0.8f));   // Unison spread
        ui->mEngine.setParameter(activeTrk, 20, (float)(rand() % 4) / 3.0f); // Filter Type
        ui->mEngine.setParameter(activeTrk, 112, rRange(0.15f, 0.95f)); // Cutoff
        ui->mEngine.setParameter(activeTrk, 113, rRange(0.0f, 0.85f));  // Resonance
    }
    // FM Drum (5)
    else if (engineType == 5) {
        for (int d = 0; d < 8; ++d) {
            int base = 200 + d * 10;
            ui->mEngine.setParameter(activeTrk, base + 0, rRange(0.15f, 0.85f)); // Pitch
            ui->mEngine.setParameter(activeTrk, base + 1, rRange(0.0f, 1.0f));   // Snap
            ui->mEngine.setParameter(activeTrk, base + 2, rRange(0.05f, 0.65f)); // Decay
            ui->mEngine.setParameter(activeTrk, base + 4, rRange(0.0f, 0.75f));  // Overdrive
        }
    }
    // Analogue Drum (6)
    else if (engineType == 6) {
        for (int d = 0; d < 6; ++d) {
            int base = 600 + d * 10;
            ui->mEngine.setParameter(activeTrk, base + 0, rRange(0.05f, 0.65f)); // Decay
            ui->mEngine.setParameter(activeTrk, base + 1, rRange(0.0f, 1.0f));   // Tone/Color
            ui->mEngine.setParameter(activeTrk, base + 2, rRange(0.15f, 0.85f)); // Tune
            ui->mEngine.setParameter(activeTrk, base + 3, rRange(0.0f, 1.0f));   // Attack/Snap
            ui->mEngine.setParameter(activeTrk, base + 4, rRange(0.0f, 1.0f));   // Color/Metal
        }
    }
    // Audio In (8)
    else if (engineType == 8) {
        ui->mEngine.setParameter(activeTrk, 112, rRange(0.15f, 0.95f)); // Cutoff
        ui->mEngine.setParameter(activeTrk, 113, rRange(0.0f, 0.85f));  // Resonance
        ui->mEngine.setParameter(activeTrk, 123, (float)(rand() % 3) / 2.0f); // Filter Mode (LP, HP, BP)
        ui->mEngine.setParameter(activeTrk, 122, rRange(0.0f, 0.85f));  // Fold
        
        // Amp ADSR
        ui->mEngine.setParameter(activeTrk, 100, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 101, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 102, rRange(0.1f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 3.0f));  // R

        // Filter ADSR
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 3.0f));  // R

        // EQ Bands
        ui->mEngine.setParameter(activeTrk, 1530, rRange(0.2f, 0.8f)); // Low
        ui->mEngine.setParameter(activeTrk, 1531, rRange(0.2f, 0.8f)); // L-Mid
        ui->mEngine.setParameter(activeTrk, 1532, rRange(0.2f, 0.8f)); // Mid
        ui->mEngine.setParameter(activeTrk, 1533, rRange(0.2f, 0.8f)); // H-Mid
        ui->mEngine.setParameter(activeTrk, 1534, rRange(0.2f, 0.8f)); // High
    }
    // SoundFont (9)
    else if (engineType == 9) {
        ui->mEngine.setParameter(activeTrk, 112, rRange(0.15f, 0.95f)); // Cutoff
        ui->mEngine.setParameter(activeTrk, 113, rRange(0.0f, 0.85f));  // Resonance
        ui->mEngine.setParameter(activeTrk, 20, (float)(rand() % 4) / 3.0f); // Filter Type
        
        // Amp ADSR
        ui->mEngine.setParameter(activeTrk, 100, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 101, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 102, rRange(0.1f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 3.0f));  // R

        // LFO
        ui->mEngine.setParameter(activeTrk, 7, rRange(0.05f, 0.8f));    // LFO Rate
        ui->mEngine.setParameter(activeTrk, 8, rRange(0.0f, 0.7f));     // LFO Depth
        ui->mEngine.setParameter(activeTrk, 114, (float)(rand() % 5) / 4.0f); // LFO Shape
    }

    std::cout << "UIManager: Randomized sound parameters on Track " << activeTrk << std::endl;

    // Refresh UI to display new values asynchronously on the next frame update
    ui->mNeedsScreenRebuild = true;
}

void UIManager::defaultPatchBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mEngine.restoreTrackPreset(ui->mActiveTrack);
    std::cout << "UIManager: Restored default patch for engine type: " 
              << ui->mEngine.getTracks()[ui->mActiveTrack].engineType << std::endl;
    // Refresh UI to display new values asynchronously on the next frame update
    ui->mNeedsScreenRebuild = true;
}

void UIManager::loadPatchBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsPresetLoad = true;
    ui->mFileBrowserIsPresetSave = false;
    ui->mFileBrowserIsSave = false;
    ui->openFileBrowser(false);
}

void UIManager::savePatchBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mFileBrowserIsPresetLoad = false;
    ui->mFileBrowserIsPresetSave = true;
    ui->openFileBrowser(true);
}

void UIManager::paramMidiLearnClickEventCb(lv_event_t* e) {
    struct GenericParamData {
        UIManager* ui;
        int paramId;
    };
    GenericParamData* data = (GenericParamData*)lv_event_get_user_data(e);
    if (!data || !data->ui) return;
    UIManager* ui = data->ui;

    if (ui->mMidiLearnActive) {
        ui->mMidiLearnTargetParamId = data->paramId;
        ui->mMidiLearnTargetTrack = ui->mActiveTrack;

        if (ui->mMidiLearnBtnLabel) {
            std::string pName = getParameterNameString(ui->mActiveTrack, data->paramId, &(ui->mEngine));
            lv_label_set_text_fmt(ui->mMidiLearnBtnLabel, "LEARN: MOVE CC CONTROL TO MAP '%s'", pName.c_str());
        }
    }
}

void UIManager::createMidiLearnButton() {
    // Integrated into populateParamScreen and populateFxScreen action bars
}

void UIManager::applyDefaultMidiMappings(int trackIdx, int engineType) {
    if (trackIdx < 0 || trackIdx >= 8) return;

    // Apply default CC IDs: Knobs 70-93, Faders 12-35
    for (int k = 0; k < 24; ++k) {
        mSeqMidiKnobCC[trackIdx][k] = 70 + k;
        mSeqMidiKnobParam[trackIdx][k] = -1;
        mSeqMidiKnobValue[trackIdx][k] = 0.5f;
        mSeqMidiKnobInverted[trackIdx][k] = false;
    }
    for (int f = 0; f < 24; ++f) {
        mSeqMidiFaderCC[trackIdx][f] = 12 + f;
        mSeqMidiFaderParam[trackIdx][f] = -1;
        mSeqMidiFaderValue[trackIdx][f] = 0.8f;
        mSeqMidiFaderInverted[trackIdx][f] = false;
    }

    // Apply engine-specific parameter assignments to first 4 Knobs and first 4 Faders
    if (engineType == 0) { // Subtractive
        // Knobs 1-4: Cutoff, Resonance, LFO Rate, LFO Depth
        mSeqMidiKnobParam[trackIdx][0] = 1; // Cutoff
        mSeqMidiKnobParam[trackIdx][1] = 2; // Resonance
        mSeqMidiKnobParam[trackIdx][2] = 7;   // LFO Rate
        mSeqMidiKnobParam[trackIdx][3] = 8;   // LFO Depth

        // Faders 1-4: Attack, Decay, Sustain, Release
        mSeqMidiFaderParam[trackIdx][0] = 100;  // Amp Attack
        mSeqMidiFaderParam[trackIdx][1] = 101;  // Amp Decay
        mSeqMidiFaderParam[trackIdx][2] = 102;  // Amp Sustain
        mSeqMidiFaderParam[trackIdx][3] = 103;  // Amp Release
    } else if (engineType == 1) { // FM
        // Knobs 1-4: Cutoff, Resonance, Op2 Coarse Ratio, Mod Index
        mSeqMidiKnobParam[trackIdx][0] = 1; // Cutoff
        mSeqMidiKnobParam[trackIdx][1] = 2; // Resonance
        mSeqMidiKnobParam[trackIdx][2] = 171; // Op2 Ratio
        mSeqMidiKnobParam[trackIdx][3] = 166; // Op2 Level

        // Faders 1-4: Mod Attack, Mod Decay, Amp Attack, Amp Release
        mSeqMidiFaderParam[trackIdx][0] = 167; // Op2 Attack (Mod Attack)
        mSeqMidiFaderParam[trackIdx][1] = 168; // Op2 Decay (Mod Decay)
        mSeqMidiFaderParam[trackIdx][2] = 100;  // Amp Attack
        mSeqMidiFaderParam[trackIdx][3] = 103;  // Amp Release
    } else if (engineType == 2) { // Sampler
        // Knobs 1-4: Speed/Pitch, Start Pos, End Pos, Cutoff
        mSeqMidiKnobParam[trackIdx][0] = 302; // Speed/Pitch
        mSeqMidiKnobParam[trackIdx][1] = 330; // Start Pos
        mSeqMidiKnobParam[trackIdx][2] = 331; // End Pos
        mSeqMidiKnobParam[trackIdx][3] = 1; // Cutoff

        // Faders 1-4: Attack, Decay, Sustain, Release
        mSeqMidiFaderParam[trackIdx][0] = 310;  // Amp Attack (310)
        mSeqMidiFaderParam[trackIdx][1] = 311;  // Amp Decay (311)
        mSeqMidiFaderParam[trackIdx][2] = 312;  // Amp Sustain (312)
        mSeqMidiFaderParam[trackIdx][3] = 313;  // Amp Release (313)
    } else if (engineType == 3) { // Granular
        // Knobs 1-4: Grain Size, Density, Position, Spray
        mSeqMidiKnobParam[trackIdx][0] = 400; // Grain Size
        mSeqMidiKnobParam[trackIdx][1] = 401; // Density
        mSeqMidiKnobParam[trackIdx][2] = 330; // Position
        mSeqMidiKnobParam[trackIdx][3] = 403; // Spray

        // Faders 1-4: Attack, Decay, Sustain, Release
        mSeqMidiFaderParam[trackIdx][0] = 425;  // Amp Attack (425)
        mSeqMidiFaderParam[trackIdx][1] = 426;  // Amp Decay (426)
        mSeqMidiFaderParam[trackIdx][2] = 427;  // Amp Sustain (427)
        mSeqMidiFaderParam[trackIdx][3] = 428;  // Amp Release (428)
    } else if (engineType == 4) { // Wavetable
        // Knobs 1-4: WT Pos, Cutoff, LFO Depth, LFO Rate
        mSeqMidiKnobParam[trackIdx][0] = 310; // WT Pos
        mSeqMidiKnobParam[trackIdx][1] = 1; // Cutoff
        mSeqMidiKnobParam[trackIdx][2] = 8;   // LFO Depth
        mSeqMidiKnobParam[trackIdx][3] = 7;   // LFO Rate

        // Faders 1-4: Attack, Decay, Sustain, Release
        mSeqMidiFaderParam[trackIdx][0] = 454;  // Amp Attack (454)
        mSeqMidiFaderParam[trackIdx][1] = 455;  // Amp Decay (455)
        mSeqMidiFaderParam[trackIdx][2] = 456;  // Amp Sustain (456)
        mSeqMidiFaderParam[trackIdx][3] = 457;  // Amp Release (457)
    } else if (engineType == 5 || engineType == 6) { // FM Drum & Analogue Drum
        // Knobs 1-4: BD Decay, SD Decay, CH Decay, Cutoff
        mSeqMidiKnobParam[trackIdx][0] = 201; // BD Decay
        mSeqMidiKnobParam[trackIdx][1] = 211; // SD Decay
        mSeqMidiKnobParam[trackIdx][2] = 221; // CH Decay
        mSeqMidiKnobParam[trackIdx][3] = 1; // Cutoff

        // Faders 1-4: BD Tune, SD Tune, CH Tune, OH Tune
        mSeqMidiFaderParam[trackIdx][0] = 200; // BD Tune
        mSeqMidiFaderParam[trackIdx][1] = 210; // SD Tune
        mSeqMidiFaderParam[trackIdx][2] = 220; // CH Tune
        mSeqMidiFaderParam[trackIdx][3] = 240; // OH Tune
    } else { // Fallback/General
        mSeqMidiKnobParam[trackIdx][0] = 1; // Cutoff
        mSeqMidiKnobParam[trackIdx][1] = 2; // Resonance
        mSeqMidiKnobParam[trackIdx][2] = 7;   // LFO Rate
        mSeqMidiKnobParam[trackIdx][3] = 8;   // LFO Depth

        mSeqMidiFaderParam[trackIdx][0] = 100;  // Amp Attack
        mSeqMidiFaderParam[trackIdx][1] = 101;  // Amp Decay
        mSeqMidiFaderParam[trackIdx][2] = 102;  // Amp Sustain
        mSeqMidiFaderParam[trackIdx][3] = 103;  // Amp Release
    }
}

#include <sstream>

void UIManager::saveSettings(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to save settings to: " << path << std::endl;
        return;
    }
    file << "LOOM_SETTINGS_V1\n";
    file << "PAD_COUNT:" << mSettingsPadCount << "\n";
    file << "PAD_MODE:" << mSettingsPadMode << "\n";
    file << "OCTAVE_OFFSET:" << mSettingsOctaveOffset << "\n";
    file << "MOMENTARY:" << (mSettingsFxPadMomentary ? 1 : 0) << "\n";
    file << "KEYBOARD_MODE:" << (mSettingsKeyboardMode ? 1 : 0) << "\n";
    file << "VELOCITY_SENSITIVITY:" << (mEngine.getVelocitySensitivityEnabled() ? 1 : 0) << "\n";
    file << "FAST_GRANULAR:" << (mEngine.getFastGranularEnabled() ? 1 : 0) << "\n";
    file << "AUDIO_OUTPUT_MODE:" << mEngine.getAudioOutputMode() << "\n";
    file << "AUDIO_DEVICE:" << mSettingsAudioDevice << "\n";

    // Transport & custom configurations
    file << "PLAY_CC:" << mCcPlay << "\n";
    file << "STOP_CC:" << mCcStop << "\n";
    file << "RECORD_CC:" << mCcRecord << "\n";
    file << "CLEAR_CC:" << mCcClear << "\n";
    file << "PREV_TRACK_CC:" << mCcPrevTrack << "\n";
    file << "NEXT_TRACK_CC:" << mCcNextTrack << "\n";
    file << "PAD_NOTE_MAP:";
    for (int i = 0; i < 24; ++i) file << mSettingsPadNoteMap[i] << (i < 23 ? " " : "");
    file << "\n";

    file << "PAD_FX_ASSIGN:";
    for (int i = 0; i < 24; ++i) file << mSettingsPadFxAssign[i] << (i < 23 ? " " : "");
    file << "\n";

    file << "PAD_DRUM_ASSIGN:";
    for (int i = 0; i < 24; ++i) file << mSettingsPadDrumAssign[i] << (i < 23 ? " " : "");
    file << "\n";

    for (int i = 0; i < 24; ++i) {
        file << "PAD_CHORD:" << i << ":" << mSettingsPadChordCount[i];
        for (int n = 0; n < mSettingsPadChordCount[i]; ++n) {
            file << " " << mSettingsPadChordNotes[i][n];
        }
        file << "\n";
    }

    for (int t = 0; t < 8; ++t) {
        for (int k = 0; k < 24; ++k) {
            file << "KNOB_MAP:" << t << ":" << k << ":" << mSeqMidiKnobCC[t][k] << ":" << mSeqMidiKnobParam[t][k] << ":" << mSeqMidiKnobValue[t][k] << ":" << (mSeqMidiKnobInverted[t][k] ? 1 : 0) << "\n";
        }
        for (int f = 0; f < 24; ++f) {
            file << "FADER_MAP:" << t << ":" << f << ":" << mSeqMidiFaderCC[t][f] << ":" << mSeqMidiFaderParam[t][f] << ":" << mSeqMidiFaderValue[t][f] << ":" << (mSeqMidiFaderInverted[t][f] ? 1 : 0) << "\n";
        }
        file << "AFTERTOUCH_MAP:" << t << ":" << mAftertouchDestParamId[t] << "\n";
    }

    for (int m = 0; m < 8; ++m) {
        for (int s = 0; s < 2; ++s) {
            file << "MACRO_MAP:" << m << ":" << s << ":" << mMacroDestParamId[m][s] << ":" << mMacroDestTrack[m][s] << ":" << mMacroDestType[m][s] << ":" << mMacroDestAmount[m][s] << "\n";
        }
    }

    for (int c = 0; c < 2; ++c) {
        file << "FX_CHAIN:" << c << ":";
        for (int s = 0; s < 5; ++s) {
            file << mFxChainPedals[c][s] << (s < 4 ? " " : "");
        }
        file << "\n";
    }

    file.close();
    std::cout << "Settings saved successfully to: " << path << std::endl;
}

void UIManager::loadSettings(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to load settings from: " << path << std::endl;
        return;
    }
    std::string line;
    if (!std::getline(file, line) || line != "LOOM_SETTINGS_V1") {
        std::cerr << "Invalid settings file header." << std::endl;
        file.close();
        return;
    }

    while (std::getline(file, line)) {
        try {
            size_t pos = line.find(':');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);

            if (key == "PAD_COUNT") mSettingsPadCount = std::stoi(val);
            else if (key == "PAD_MODE") mSettingsPadMode = std::stoi(val);
            else if (key == "OCTAVE_OFFSET") mSettingsOctaveOffset = std::stoi(val);
            else if (key == "MOMENTARY") mSettingsFxPadMomentary = std::stoi(val) != 0;
            else if (key == "KEYBOARD_MODE") mSettingsKeyboardMode = std::stoi(val) != 0;
            else if (key == "VELOCITY_SENSITIVITY") mEngine.setVelocitySensitivityEnabled(std::stoi(val) != 0);
            else if (key == "FAST_GRANULAR") mEngine.setFastGranularEnabled(std::stoi(val) != 0);
            else if (key == "AUDIO_OUTPUT_MODE") mEngine.setAudioOutputMode(std::stoi(val));
            else if (key == "AUDIO_DEVICE") mSettingsAudioDevice = val;
            else if (key == "PLAY_CC") mCcPlay = std::stoi(val);
            else if (key == "STOP_CC") mCcStop = std::stoi(val);
            else if (key == "RECORD_CC") mCcRecord = std::stoi(val);
            else if (key == "CLEAR_CC") mCcClear = std::stoi(val);
            else if (key == "PREV_TRACK_CC") mCcPrevTrack = std::stoi(val);
            else if (key == "NEXT_TRACK_CC") mCcNextTrack = std::stoi(val);
            else if (key == "PAD_NOTE_MAP") {
                std::stringstream ss(val);
                for (int i = 0; i < 24; ++i) {
                    if (ss >> mSettingsPadNoteMap[i]) {}
                }
            } else if (key == "PAD_FX_ASSIGN") {
                std::stringstream ss(val);
                for (int i = 0; i < 24; ++i) {
                    if (ss >> mSettingsPadFxAssign[i]) {}
                }
            } else if (key == "PAD_DRUM_ASSIGN") {
                std::stringstream ss(val);
                for (int i = 0; i < 24; ++i) {
                    if (ss >> mSettingsPadDrumAssign[i]) {}
                }
            } else if (key == "PAD_CHORD") {
                size_t p2 = val.find(':');
                if (p2 != std::string::npos) {
                    int padIdx = std::stoi(val.substr(0, p2));
                    std::stringstream ss(val.substr(p2 + 1));
                    int noteCount = 0;
                    ss >> noteCount;
                    if (padIdx >= 0 && padIdx < 24) {
                        mSettingsPadChordCount[padIdx] = noteCount;
                        for (int n = 0; n < noteCount; ++n) {
                            ss >> mSettingsPadChordNotes[padIdx][n];
                        }
                    }
                }
            } else if (key == "KNOB_MAP") {
                std::stringstream ss(val);
                int t, k, cc, p;
                float v;
                int inv = 0;
                char colon;
                ss >> t >> colon >> k >> colon >> cc >> colon >> p >> colon >> v;
                if (ss.peek() == ':') {
                    ss >> colon >> inv;
                }
                if (t >= 0 && t < 8 && k >= 0 && k < 24) {
                    mSeqMidiKnobCC[t][k] = cc;
                    int engineType = mEngine.getTracks()[t].engineType;
                    if (engineType == 2 && p >= 100 && p <= 103) {
                        p = 310 + (p - 100);
                    } else if (engineType == 3 && p >= 100 && p <= 103) {
                        p = 425 + (p - 100);
                    } else if (engineType == 4 && p >= 100 && p <= 103) {
                        p = 454 + (p - 100);
                    }
                    mSeqMidiKnobParam[t][k] = p;
                    mSeqMidiKnobValue[t][k] = v;
                    mSeqMidiKnobInverted[t][k] = (inv != 0);
                }
            } else if (key == "FADER_MAP") {
                std::stringstream ss(val);
                int t, f, cc, p;
                float v;
                int inv = 0;
                char colon;
                ss >> t >> colon >> f >> colon >> cc >> colon >> p >> colon >> v;
                if (ss.peek() == ':') {
                    ss >> colon >> inv;
                }
                if (t >= 0 && t < 8 && f >= 0 && f < 24) {
                    mSeqMidiFaderCC[t][f] = cc;
                    int engineType = mEngine.getTracks()[t].engineType;
                    if (engineType == 2 && p >= 100 && p <= 103) {
                        p = 310 + (p - 100);
                    } else if (engineType == 3 && p >= 100 && p <= 103) {
                        p = 425 + (p - 100);
                    } else if (engineType == 4 && p >= 100 && p <= 103) {
                        p = 454 + (p - 100);
                    }
                    mSeqMidiFaderParam[t][f] = p;
                    mSeqMidiFaderValue[t][f] = v;
                    mSeqMidiFaderInverted[t][f] = (inv != 0);
                }
            } else if (key == "AFTERTOUCH_MAP") {
                std::stringstream ss(val);
                int t, p;
                char colon;
                ss >> t >> colon >> p;
                if (t >= 0 && t < 8) {
                    mAftertouchDestParamId[t] = p;
                }
            } else if (key == "MACRO_MAP") {
                std::stringstream ss(val);
                int m, s, p, trk, typ;
                float amt;
                char colon;
                ss >> m >> colon >> s >> colon >> p >> colon >> trk >> colon >> typ >> colon >> amt;
                if (m >= 0 && m < 8 && s >= 0 && s < 2) {
                    mMacroDestParamId[m][s] = p;
                    mMacroDestTrack[m][s] = trk;
                    mMacroDestType[m][s] = typ;
                    mMacroDestAmount[m][s] = amt;
                }
            } else if (key == "FX_CHAIN") {
                size_t p2 = val.find(':');
                if (p2 != std::string::npos) {
                    int chainIdx = std::stoi(val.substr(0, p2));
                    std::stringstream ss(val.substr(p2 + 1));
                    if (chainIdx >= 0 && chainIdx < 2) {
                        for (int s = 0; s < 5; ++s) {
                            ss >> mFxChainPedals[chainIdx][s];
                        }
                    }
                }
            }
        } catch (...) {}
    }
}

void UIManager::addMidiLog(const std::string& type, int channel, int d1, int d2) {
    // Reset inactivity timer on MIDI event
    lv_display_trigger_activity(nullptr);
    if (mSleepOverlay != nullptr) {
        lv_obj_delete(mSleepOverlay);
        mSleepOverlay = nullptr;
        setBacklightPower(true);
    }

    std::lock_guard<std::mutex> lock(mMidiLogMutex);
    MidiLogMessage msg;
    msg.typeStr = type;
    msg.channel = channel;
    msg.data1 = d1;
    msg.data2 = d2;
    mMidiLog.push_back(msg);
    if (mMidiLog.size() > 12) {
        mMidiLog.erase(mMidiLog.begin());
    }
}

void UIManager::settingsUpdateBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->mUpdateInstallActive) return;

    ui->mUpdateInstallActive = true;
    ui->mUpdateInstallFinished = false;
    ui->mUpdateInstallStatusStr = "Checking for updates...";
    ui->mUpdateInstallProgressPercent = 10;

    std::thread updateThread([ui]() {
        int ret = std::system("git fetch origin main");
        ui->mUpdateInstallProgressPercent = 25;
        ui->mUpdateInstallStatusStr = "Pulling updates...";
        
        ret = std::system("git pull origin main");
        if (ret != 0) {
            ui->mUpdateInstallStatusStr = "Git pull failed.";
            ui->mUpdateInstallFinished = true;
            ui->mUpdateInstallActive = false;
            return;
        }

        ui->mUpdateInstallProgressPercent = 50;
        ui->mUpdateInstallStatusStr = "Generating build configuration...";
        ret = std::system("cmake -Bbuild -DCMAKE_BUILD_TYPE=Release");
        if (ret != 0) {
            ui->mUpdateInstallStatusStr = "CMake build generation failed.";
            ui->mUpdateInstallFinished = true;
            ui->mUpdateInstallActive = false;
            return;
        }

        ui->mUpdateInstallProgressPercent = 70;
        ui->mUpdateInstallStatusStr = "Compiling system (takes ~1 min)...";
        ret = std::system("cmake --build build --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)");
        if (ret != 0) {
            ui->mUpdateInstallStatusStr = "Compilation failed.";
            ui->mUpdateInstallFinished = true;
            ui->mUpdateInstallActive = false;
            return;
        }

        ui->mUpdateInstallProgressPercent = 100;
        ui->mUpdateInstallStatusStr = "Success! Restart Loom to apply.";
        ui->mUpdateInstallFinished = true;
        ui->mUpdateInstallActive = false;
    });
    updateThread.detach();
}

void UIManager::settingsRestartBtnEventCb(lv_event_t* e) {
    std::cout << "Settings: Restart requested. Exiting process..." << std::endl;
    std::exit(0);
}

// =========================================================================
// --- Bluetooth Pairing Manager ---
// =========================================================================

static bool parseBtLine(const std::string& line, std::string& mac, std::string& name) {
    if (line.length() < 17) return false;
    for (size_t i = 0; i <= line.length() - 17; ++i) {
        bool isMac = true;
        for (int j = 0; j < 17; ++j) {
            char c = line[i + j];
            if (j == 2 || j == 5 || j == 8 || j == 11 || j == 14) {
                if (c != ':') { isMac = false; break; }
            } else {
                if (!std::isxdigit(static_cast<unsigned char>(c))) { isMac = false; break; }
            }
        }
        if (isMac) {
            mac = line.substr(i, 17);
            size_t nameStart = i + 18;
            if (nameStart < line.length()) {
                name = line.substr(nameStart);
                if (name.rfind("Name: ", 0) == 0) {
                    name = name.substr(6);
                }
                name.erase(name.begin(), std::find_if(name.begin(), name.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                name.erase(std::find_if(name.rbegin(), name.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), name.end());
            } else {
                name = "";
            }
            return true;
        }
    }
    return false;
}

static std::vector<std::string> runCommandAndGetLines(const std::string& cmd) {
    std::vector<std::string> lines;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return lines;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    pclose(fp);
    return lines;
}

void UIManager::settingsBtPairBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui) ui->openBtPairModal();
}

void UIManager::btCloseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->mBtModal) {
        lv_obj_delete(ui->mBtModal);
        ui->mBtModal = nullptr;
        ui->mBtListContainer = nullptr;
        ui->mBtStatusLabel = nullptr;
    }
}

struct BtDeviceSelectData {
    UIManager* ui;
    std::string mac;
};

void UIManager::btDeviceSelectEventCb(lv_event_t* e) {
    BtDeviceSelectData* data = (BtDeviceSelectData*)lv_event_get_user_data(e);
    if (!data || !data->ui) return;
    data->ui->connectBluetoothDevice(data->mac);
}

void UIManager::openBtPairModal() {
    if (mBtModal) {
        lv_obj_delete(mBtModal);
        mBtModal = nullptr;
    }

    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, 1024, 600);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    mBtModal = overlay;

    lv_obj_t* card = lv_obj_create(overlay);
    lv_obj_set_size(card, 560, 420);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 10, 0);

    // Title
    lv_obj_t* titleLbl = lv_label_create(card);
    lv_label_set_text(titleLbl, "BLUETOOTH DEVICE MANAGER");
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, getTrackColor(mActiveTrack), 0);

    // Status Label
    mBtStatusLabel = lv_label_create(card);
    lv_label_set_text(mBtStatusLabel, "Status: Idle");
    lv_obj_set_style_text_font(mBtStatusLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mBtStatusLabel, lv_color_hex(0xAAAAAA), 0);

    // List Container
    mBtListContainer = lv_obj_create(card);
    lv_obj_set_size(mBtListContainer, 500, 240);
    lv_obj_set_style_bg_color(mBtListContainer, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_color(mBtListContainer, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(mBtListContainer, 1, 0);
    lv_obj_set_style_radius(mBtListContainer, 8, 0);
    lv_obj_set_style_pad_all(mBtListContainer, 8, 0);
    lv_obj_set_layout(mBtListContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mBtListContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mBtListContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(mBtListContainer, 6, 0);

    // Instructions/Scan placeholder
    lv_obj_t* placeholder = lv_label_create(mBtListContainer);
    lv_label_set_text(placeholder, "Press SCAN to search for devices...");
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x666666), 0);
    lv_obj_align(placeholder, LV_ALIGN_CENTER, 0, 0);

    // Bottom Action buttons row
    lv_obj_t* btnRow = lv_obj_create(card);
    lv_obj_set_size(btnRow, 500, 50);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // SCAN Button
    lv_obj_t* scanBtn = lv_button_create(btnRow);
    lv_obj_set_size(scanBtn, 140, 36);
    lv_obj_set_style_bg_color(scanBtn, getTrackColor(mActiveTrack), 0);
    lv_obj_t* scanLbl = lv_label_create(scanBtn);
    lv_label_set_text(scanLbl, "SCAN");
    lv_obj_set_style_text_font(scanLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(scanLbl);
    
    auto scanClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        if (ui) ui->startBluetoothScan();
    };
    lv_obj_add_event_cb(scanBtn, scanClickCb, LV_EVENT_CLICKED, this);

    // CLOSE Button
    lv_obj_t* closeBtn = lv_button_create(btnRow);
    lv_obj_set_size(closeBtn, 140, 36);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x444444), 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, "CLOSE");
    lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, btCloseEventCb, LV_EVENT_CLICKED, this);
}

void UIManager::startBluetoothScan() {
    if (mBtScanning) return;
    mBtScanning = true;
    mBtStatusStr = "Scanning for devices...";
    mBtStatusChanged = true;

    std::thread scanThread([this]() {
        // Unblock bluetooth hardware/software RF kill switch
        std::system("rfkill unblock bluetooth 2>/dev/null");
        
        // Ensure Bluetooth power is ON
        std::system("bluetoothctl power on");

        // Verify if a controller is available and powered
        auto showLines = runCommandAndGetLines("bluetoothctl show");
        bool controllerFound = false;
        bool poweredOn = false;
        for (const auto& line : showLines) {
            if (line.find("Controller") != std::string::npos) {
                controllerFound = true;
            }
            if (line.find("Powered: yes") != std::string::npos) {
                poweredOn = true;
            }
        }

        if (!controllerFound) {
            mBtStatusStr = "Error: No Bluetooth controller found!";
            mBtStatusChanged = true;
            mBtScanning = false;
            return;
        } else if (!poweredOn) {
            mBtStatusStr = "Error: Bluetooth controller powered OFF.";
            mBtStatusChanged = true;
            mBtScanning = false;
            return;
        }

        // Register default agent
        std::system("bluetoothctl agent on 2>/dev/null");
        std::system("bluetoothctl default-agent 2>/dev/null");

        // Run scan with line-buffering to ensure output is written to file before process is terminated
        std::system("timeout 6 stdbuf -oL bluetoothctl scan on > /tmp/bt_scan.log 2>&1");

        std::vector<BtDevice> foundDevices;
        
        // Retrieve all known/paired/discovered devices after scanning (updates the cache)
        auto pairedLines = runCommandAndGetLines("bluetoothctl devices");
        for (const auto& line : pairedLines) {
            std::string mac, name;
            if (parseBtLine(line, mac, name)) {
                if (!name.empty() && name != mac) {
                    foundDevices.push_back({mac, name});
                }
            }
        }

        // Also parse scan log for any newly resolved names
        auto scanLines = runCommandAndGetLines("cat /tmp/bt_scan.log");
        for (const auto& line : scanLines) {
            std::string mac, name;
            if (parseBtLine(line, mac, name)) {
                if (!name.empty() && name != mac) {
                    // Check if MAC is already in list
                    auto it = std::find_if(foundDevices.begin(), foundDevices.end(), [&](const BtDevice& d) {
                        return d.mac == mac;
                    });
                    if (it == foundDevices.end()) {
                        foundDevices.push_back({mac, name});
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mBtMutex);
            mBtDevices = std::move(foundDevices);
            mBtDeviceListChanged = true;
        }

        mBtScanning = false;
        mBtStatusStr = "Scan finished.";
        mBtStatusChanged = true;
    });
    scanThread.detach();
}

void UIManager::connectBluetoothDevice(const std::string& mac) {
    mBtStatusStr = "Pairing " + mac + "...";
    mBtStatusChanged = true;

    std::thread connThread([this, mac]() {
        std::string cmdPair = "bluetoothctl pair " + mac;
        std::string cmdTrust = "bluetoothctl trust " + mac;
        std::string cmdConnect = "bluetoothctl connect " + mac;

        std::cout << "BT: Executing: " << cmdPair << std::endl;
        std::system(cmdPair.c_str());
        
        std::cout << "BT: Executing: " << cmdTrust << std::endl;
        std::system(cmdTrust.c_str());
        
        std::cout << "BT: Executing: " << cmdConnect << std::endl;
        int retConnect = std::system(cmdConnect.c_str());

        if (retConnect == 0) {
            mBtStatusStr = "Connected successfully!";
        } else {
            mBtStatusStr = "Connection failed.";
        }
        mBtStatusChanged = true;
    });
    connThread.detach();
}


