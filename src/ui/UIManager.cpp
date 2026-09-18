#include "UIManager.h"
#include "HardwareDisplay.h"
#include <SDL.h>
#include <iostream>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#ifndef __APPLE__
#include <alsa/asoundlib.h>
#endif
#include "../HardwareIntegration.h"
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
extern std::string gCurrentCaptureDevice;
extern bool switchCaptureDevice(const std::string& deviceName);

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
    
    float midVal = 1.00f;
    if (type == "D") {
        midVal = 0.80f;
    } else if (type == "R") {
        midVal = 0.80f;
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
    
    float midVal = 1.00f;
    if (type == "D") {
        midVal = 0.80f;
    } else if (type == "R") {
        midVal = 0.80f;
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
    // Attack parameters across all engines (0.001f to 15.0f)
    if (paramId == 100 || paramId == 114 || paramId == 310 || paramId == 425 || paramId == 454 || paramId == 471 ||
        paramId == 161 || paramId == 167 || paramId == 173 || paramId == 179 || paramId == 185 || paramId == 191) {
        return mapLinearToNonLinear(normValue, 0.001f, 15.0f, "A");
    }
    // Decay parameters across all engines (0.0f to 15.0f)
    if (paramId == 101 || paramId == 115 || paramId == 311 || paramId == 426 || paramId == 455 || paramId == 472 ||
        paramId == 162 || paramId == 168 || paramId == 174 || paramId == 180 || paramId == 186 || paramId == 192) {
        return mapLinearToNonLinear(normValue, 0.0f, 15.0f, "D");
    }
    // Release parameters across all engines (0.001f to 15.0f)
    if (paramId == 103 || paramId == 117 || paramId == 313 || paramId == 428 || paramId == 457 || paramId == 474 ||
        paramId == 164 || paramId == 170 || paramId == 176 || paramId == 182 || paramId == 188 || paramId == 194) {
        return mapLinearToNonLinear(normValue, 0.001f, 15.0f, "R");
    }
    if (paramId >= 2410 && paramId <= 2417) {
        return 20.0f + normValue * 100.0f;
    }
    return normValue;
}

float UIManager::normalizeParamValue(int paramId, float scaledValue) {
    if (paramId >= 2410 && paramId <= 2417) {
        return std::max(0.0f, std::min(1.0f, (scaledValue - 20.0f) / 100.0f));
    }
    // Attack parameters across all engines (0.001f to 15.0f)
    if (paramId == 100 || paramId == 114 || paramId == 310 || paramId == 425 || paramId == 454 || paramId == 471 ||
        paramId == 161 || paramId == 167 || paramId == 173 || paramId == 179 || paramId == 185 || paramId == 191) {
        return mapNonLinearToLinear(scaledValue, 0.001f, 15.0f, "A");
    }
    // Decay parameters across all engines (0.0f to 15.0f)
    if (paramId == 101 || paramId == 115 || paramId == 311 || paramId == 426 || paramId == 455 || paramId == 472 ||
        paramId == 162 || paramId == 168 || paramId == 174 || paramId == 180 || paramId == 186 || paramId == 192) {
        return mapNonLinearToLinear(scaledValue, 0.0f, 15.0f, "D");
    }
    // Release parameters across all engines (0.001f to 15.0f)
    if (paramId == 103 || paramId == 117 || paramId == 313 || paramId == 428 || paramId == 457 || paramId == 474 ||
        paramId == 164 || paramId == 170 || paramId == 176 || paramId == 182 || paramId == 188 || paramId == 194) {
        return mapNonLinearToLinear(scaledValue, 0.001f, 15.0f, "R");
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
    mSettingsAudioDevice = gCurrentAudioDevice;
    mSettingsAudioMicDevice = gCurrentCaptureDevice;
    mSettingsAudioLineInDevice = gCurrentCaptureDevice;
    mSettingsScreenTimeoutSec = 0;
    mLastActivityTicks = SDL_GetTicks();
    mScreenIsSleeping = false;
    
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

    for (int c = 0; c < 16; ++c) {
        mArpColumns[c] = nullptr;
        for (int r = 0; r < 4; ++r) {
            mArpButtons[r][c] = nullptr;
        }
    }
}

UIManager::~UIManager() {}

void UIManager::init() {
    mMainScreen = lv_screen_active();
    
    // Set a dark background for the shell
    lv_obj_set_style_bg_color(mMainScreen, lv_color_hex(0x121212), 0);

    // Create the main flex container that holds the 3 columns
    lv_obj_t* mainFlex = lv_obj_create(mMainScreen);
    lv_obj_set_size(mainFlex, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(mainFlex, 0, 0);
    lv_obj_set_style_border_width(mainFlex, 0, 0);
    lv_obj_set_style_bg_opa(mainFlex, LV_OPA_TRANSP, 0);
    
    // Use Flex row layout: [Left Bar] [Center Area] [Right Bar]
    lv_obj_set_layout(mainFlex, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mainFlex, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mainFlex, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // 1. Create Left Mixer Bar (Fixed Width 90px, Full 800px Height)
    mLeftBar = lv_obj_create(mainFlex);
    lv_obj_set_size(mLeftBar, 90, SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(mLeftBar, 6, 0);
    lv_obj_set_style_border_width(mLeftBar, 0, 0);
    lv_obj_set_style_bg_color(mLeftBar, lv_color_hex(0x1e1e1e), 0);
    lv_obj_set_style_radius(mLeftBar, 0, 0);
    lv_obj_set_layout(mLeftBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mLeftBar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mLeftBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 2. Create Center Content Area (Flexible Width, Full 800px Height)
    mCenterArea = lv_obj_create(mainFlex);
    lv_obj_set_flex_grow(mCenterArea, 1); // Grow to take remaining space (~1090px)
    lv_obj_set_height(mCenterArea, SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(mCenterArea, 0, 0);
    lv_obj_set_style_border_width(mCenterArea, 0, 0);
    lv_obj_set_style_bg_color(mCenterArea, lv_color_hex(0x121212), 0); // Pure dark mode
    lv_obj_set_style_radius(mCenterArea, 0, 0);

    // 3. Create Right Nav Bar (Fixed Width 100px, Full 800px Height)
    mRightBar = lv_obj_create(mainFlex);
    lv_obj_set_size(mRightBar, 100, SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(mRightBar, 6, 0);
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
    std::vector<std::string> searchPaths;
    searchPaths.push_back(homeStr + "/projects/Init.loom");
    searchPaths.push_back(homeStr + "/projects/init.loom");
    searchPaths.push_back("/home/loom/Loom/projects/Init.loom");
    searchPaths.push_back("/home/loom/Loom/projects/init.loom");
    searchPaths.push_back("/home/pi/Loom/projects/Init.loom");
    searchPaths.push_back("/home/pi/Loom/projects/init.loom");
    searchPaths.push_back("./Loom/projects/Init.loom");
    searchPaths.push_back("./Loom/projects/init.loom");

    std::string initLoomPath = "";
    for (const auto& path : searchPaths) {
        std::ifstream f(path);
        if (f.good()) {
            f.close();
            initLoomPath = path;
            break;
        }
    }

    mSettingsPadCount = 16;
    if (!initLoomPath.empty()) {
        mSettingsFilePath = initLoomPath + ".settings";
        std::cout << "Auto-loading Init project: " << initLoomPath << std::endl;
        mEngine.loadProject(initLoomPath);
        loadSettings(mSettingsFilePath);
    } else {
        mSettingsFilePath = homeStr + "/projects/Init.loom.settings";
    }

    // Switch to correct physical device depending on initial recording source
    int activeSrc = mEngine.mRecordingSource.load();
    if (activeSrc == 0) { // MIC
        switchCaptureDevice(mSettingsAudioMicDevice);
    } else if (activeSrc == 1) { // LINE_IN
        switchCaptureDevice(mSettingsAudioLineInDevice);
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

    // 2-second vector/text-based splash screen overlay
    lv_obj_t* splash = lv_obj_create(lv_screen_active());
    lv_obj_set_size(splash, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(splash, 0, 0);
    lv_obj_set_style_bg_color(splash, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash, 0, 0);
    lv_obj_remove_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(splash, LV_OBJ_FLAG_FLOATING);

    // Event callback to free dynamically allocated point arrays
    auto lineDeleteCb = [](lv_event_t* e) {
        lv_point_t* pts = (lv_point_t*)lv_event_get_user_data(e);
        delete[] pts;
    };

    // Color palette for the grid (cyan to red spectrum)
    lv_color_t colors[6] = {
        lv_color_hex(0x00E5FF), // Cyan
        lv_color_hex(0xBD00FF), // Purple
        lv_color_hex(0xFF007F), // Pink/Magenta
        lv_color_hex(0x39FF14), // Lime Green
        lv_color_hex(0xFFD700), // Yellow/Gold
        lv_color_hex(0xFF3300)  // Orange/Red
    };

    // Center coords for grid (centered at x=640, y=320 on 1280x800 display)
    int gridSizeX = 720;
    int gridSizeY = 260;
    int gridX = (SCREEN_WIDTH - gridSizeX) / 2; // 280
    int gridY = 170;
    int stepSizeX = gridSizeX / 5; // 144
    int stepSizeY = gridSizeY / 5; // 52
    int numPts = 12;
    float squigglyLen = 75.0f;

    // Draw the 6 horizontal grid lines with a nice gradient
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* hLine = lv_obj_create(splash);
        lv_obj_set_size(hLine, gridSizeX, 3);
        lv_obj_set_pos(hLine, gridX, gridY + i * stepSizeY);
        lv_obj_set_style_bg_color(hLine, colors[0], 0);
        lv_obj_set_style_bg_grad_color(hLine, colors[5], 0);
        lv_obj_set_style_bg_grad_dir(hLine, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(hLine, 0, 0);
    }

    // Draw the 6 vertical grid lines
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* vLine = lv_obj_create(splash);
        lv_obj_set_size(vLine, 3, gridSizeY);
        lv_obj_set_pos(vLine, gridX + i * stepSizeX, gridY);
        lv_obj_set_style_bg_color(vLine, colors[i], 0);
        lv_obj_set_style_bg_opa(vLine, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vLine, 0, 0);
    }

    // Draw squiggly lines extending from the edges (top, bottom, left, right)
    for (int i = 0; i < 6; ++i) {
        int x_val = gridX + i * stepSizeX;
        int y_val = gridY + i * stepSizeY;

        // 1. Top curves (extend upwards from gridY)
        {
            lv_point_precise_t* pts = new lv_point_precise_t[numPts];
            float direction = (i < 3) ? -1.0f : 1.0f;
            float amp = 20.0f + i * 6.0f;
            for (int p = 0; p < numPts; ++p) {
                float t = (float)p / (numPts - 1);
                pts[p].x = x_val + (int)(sinf(t * 3.14159f) * direction * amp + t * direction * 35.0f);
                pts[p].y = gridY - (int)(t * squigglyLen);
            }
            lv_obj_t* line = lv_line_create(splash);
            lv_line_set_points(line, pts, numPts);
            lv_obj_set_style_line_color(line, colors[i], 0);
            lv_obj_set_style_line_width(line, 3, 0);
            lv_obj_add_event_cb(line, lineDeleteCb, LV_EVENT_DELETE, pts);
        }

        // 2. Bottom curves (extend downwards from gridY + gridSizeY)
        {
            lv_point_precise_t* pts = new lv_point_precise_t[numPts];
            float direction = (i < 3) ? -1.0f : 1.0f;
            float amp = 20.0f + (5 - i) * 6.0f;
            for (int p = 0; p < numPts; ++p) {
                float t = (float)p / (numPts - 1);
                pts[p].x = x_val + (int)(sinf(t * 3.14159f) * direction * amp + t * direction * 35.0f);
                pts[p].y = (gridY + gridSizeY) + (int)(t * squigglyLen);
            }
            lv_obj_t* line = lv_line_create(splash);
            lv_line_set_points(line, pts, numPts);
            lv_obj_set_style_line_color(line, colors[i], 0);
            lv_obj_set_style_line_width(line, 3, 0);
            lv_obj_add_event_cb(line, lineDeleteCb, LV_EVENT_DELETE, pts);
        }

        // 3. Left curves (extend leftwards from gridX)
        {
            lv_point_precise_t* pts = new lv_point_precise_t[numPts];
            float direction = (i < 3) ? -1.0f : 1.0f;
            float amp = 20.0f + i * 6.0f;
            for (int p = 0; p < numPts; ++p) {
                float t = (float)p / (numPts - 1);
                pts[p].x = gridX - (int)(t * squigglyLen);
                pts[p].y = y_val + (int)(sinf(t * 3.14159f) * direction * amp + t * direction * 35.0f);
            }
            lv_obj_t* line = lv_line_create(splash);
            lv_line_set_points(line, pts, numPts);
            lv_obj_set_style_line_color(line, colors[i], 0);
            lv_obj_set_style_line_width(line, 3, 0);
            lv_obj_add_event_cb(line, lineDeleteCb, LV_EVENT_DELETE, pts);
        }

        // 4. Right curves (extend rightwards from gridX + gridSizeX)
        {
            lv_point_precise_t* pts = new lv_point_precise_t[numPts];
            float direction = (i < 3) ? -1.0f : 1.0f;
            float amp = 20.0f + (5 - i) * 6.0f;
            for (int p = 0; p < numPts; ++p) {
                float t = (float)p / (numPts - 1);
                pts[p].x = (gridX + gridSizeX) + (int)(t * squigglyLen);
                pts[p].y = y_val + (int)(sinf(t * 3.14159f) * direction * amp + t * direction * 35.0f);
            }
            lv_obj_t* line = lv_line_create(splash);
            lv_line_set_points(line, pts, numPts);
            lv_obj_set_style_line_color(line, colors[i], 0);
            lv_obj_set_style_line_width(line, 3, 0);
            lv_obj_add_event_cb(line, lineDeleteCb, LV_EVENT_DELETE, pts);
        }
    }

    // Text labels container below the graphic
    lv_obj_t* textCont = lv_obj_create(splash);
    lv_obj_set_size(textCont, 600, 160);
    lv_obj_align(textCont, LV_ALIGN_TOP_MID, 0, 540);
    lv_obj_set_style_bg_opa(textCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(textCont, 0, 0);
    lv_obj_remove_flag(textCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(textCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(textCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(textCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Glowing orange accent line under text
    lv_obj_t* bar = lv_obj_create(textCont);
    lv_obj_set_size(bar, 140, 4);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF4500), 0); // Loom Orange
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);

    lv_obj_t* title = lv_label_create(textCont);
    lv_label_set_text(title, "LOOM");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(title, 18, 0);

    lv_obj_t* subtitle = lv_label_create(textCont);
    lv_label_set_text(subtitle, "G R O O V E B O X");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);

    // LVGL timer to auto-delete splash screen after 2 seconds
    lv_timer_create([](lv_timer_t* timer) {
        lv_obj_t* splashObj = (lv_obj_t*)timer->user_data;
        lv_obj_delete(splashObj);
        lv_timer_delete(timer);
    }, 2000, splash);
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
        lv_obj_set_size(btn, 78, 88); // Enlarged for 10" 800px vertical space
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
    const char* navLabels[] = {"Param", "FX", "Seq", "Arp", "Assign", LV_SYMBOL_SETTINGS, "Mix/Rec", "Play"};
    for (int i = 0; i < 8; ++i) {
        lv_obj_t* btn = lv_button_create(mRightBar);
        lv_obj_set_size(btn, 88, 86); // Enlarged for 10" 800px vertical space
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        
        // Setup border for highlighting
        lv_obj_set_style_border_color(btn, lv_color_hex(0x44AAFF), 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, navLabels[i]);
        if (i == 5) {
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        } else {
            lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
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

        lv_obj_set_style_bg_color(mTrackButtons[i], getTrackColor(i), 0);

        if (i == mActiveTrack) {
            lv_obj_set_style_border_width(mTrackButtons[i], 3, 0);
            lv_obj_set_style_border_color(mTrackButtons[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_opa(mTrackButtons[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_opa(mTrackButtons[i], LV_OPA_COVER, 0);
        } else if (isPlaying) {
            float speedMultiplier = 1.0f;
            if (track.arpeggiator.getMode() != ArpMode::OFF) {
                speedMultiplier = track.arpeggiator.getSpeedMultiplier();
            } else {
                speedMultiplier = track.mClockMultiplier;
            }
            if (speedMultiplier < 0.01f) speedMultiplier = 1.0f;

            // 0.5x speed (pulse period is 2 beats)
            float periodMs = 2.0f * (60000.0f / (bpm * speedMultiplier));
            uint32_t cycleTime = tMs % (uint32_t)periodMs;
            bool isOn = (cycleTime < (periodMs / 2.0f));

            if (isOn) {
                lv_obj_set_style_border_width(mTrackButtons[i], 1, 0);
                lv_obj_set_style_border_color(mTrackButtons[i], lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_border_opa(mTrackButtons[i], LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_border_width(mTrackButtons[i], 0, 0);
            }
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
                case 10: icon = LV_SYMBOL_USB;     break; // MIDI Engine (USB symbol 🔌)
                default: icon = LV_SYMBOL_KEYBOARD; break;
            }
            lv_label_set_text(iconLabel, icon);
        }
    }
    
    for (int i = 0; i < 8; ++i) {
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
        if (ui->mPlayModXTrack == ui->mActiveTrack || ui->mPlayModXTrack < 0 || ui->mPlayModXTrack >= 8) {
            ui->mPlayModXTrack = clickedTrack;
        }
        if (ui->mPlayModYTrack == ui->mActiveTrack || ui->mPlayModYTrack < 0 || ui->mPlayModYTrack >= 8) {
            ui->mPlayModYTrack = clickedTrack;
        }
        ui->mActiveTrack = clickedTrack;
        ui->updateHighlighting();
        // Refresh the active screen so step colors + themed elements match the new track
        if (ui->mActiveNav >= 0 && ui->mActiveNav < 8) {
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
    for (int i = 0; i < 8; ++i) {
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

    for (int c = 0; c < 16; ++c) {
        mArpColumns[c] = nullptr;
        for (int r = 0; r < 7; ++r) {
            mArpButtons[r][c] = nullptr;
        }
    }

    // Clear existing center area
    lv_obj_clean(mCenterArea);
    mIpAddressLbl = nullptr;

    mSettingsTabview = nullptr;
    mAssignTabview = nullptr;
    mParamTabview = nullptr;
    mActiveRoutingsContainer = nullptr;

    mSamplerWaveformContainer = nullptr;
    mSamplerStartLine = nullptr;
    mSamplerEndLine = nullptr;
    mSamplerRecordBtn = nullptr;
    mSamplerLatchBtn = nullptr;
    mSamplerScrubHandle = nullptr;
    mSamplerPlayBtn = nullptr;
    mSamplerPlayBtnLabel = nullptr;
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

    if (mActiveNav == 7) { // Play
        populatePlayScreen();
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
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_t* arpRoot = lv_obj_create(mCenterArea);
    lv_obj_set_size(arpRoot, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(arpRoot, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(arpRoot, 0, 0);
    lv_obj_set_style_pad_all(arpRoot, 8, 0);
    lv_obj_remove_flag(arpRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(arpRoot, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(arpRoot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(arpRoot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const auto& arp = mEngine.getTracks()[mActiveTrack].arpeggiator;
    bool isArpOn = arp.getMode() != ArpMode::OFF;

    // Card background & border style helper
    auto applyCardStyle = [trackColor](lv_obj_t* card) {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    };

    // =========================================================================
    // --- ROW 1: TOP SETTINGS CARDS (Height: 215px) ---
    // =========================================================================
    lv_obj_t* topSettingsRow = lv_obj_create(arpRoot);
    lv_obj_set_size(topSettingsRow, lv_pct(100), 215);
    lv_obj_set_style_bg_opa(topSettingsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topSettingsRow, 0, 0);
    lv_obj_set_style_pad_all(topSettingsRow, 0, 0);
    lv_obj_remove_flag(topSettingsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(topSettingsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topSettingsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topSettingsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // --- Card 1: Note Arpeggiator ---
    lv_obj_t* col1 = lv_obj_create(topSettingsRow);
    lv_obj_set_size(col1, 350, 215);
    applyCardStyle(col1);

    // Header row: Title + Arp On/Off Button
    lv_obj_t* c1Header = lv_obj_create(col1);
    lv_obj_set_size(c1Header, 335, 34);
    lv_obj_set_style_bg_opa(c1Header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c1Header, 0, 0);
    lv_obj_set_style_pad_all(c1Header, 0, 0);
    lv_obj_remove_flag(c1Header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(c1Header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c1Header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c1Header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title1 = lv_label_create(c1Header);
    lv_label_set_text(title1, "NOTE ARPEGGIATOR");
    lv_obj_set_style_text_font(title1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title1, trackColor, 0);

    mArpToggleBtn = lv_button_create(c1Header);
    lv_obj_set_size(mArpToggleBtn, 120, 30);
    lv_obj_add_flag(mArpToggleBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(mArpToggleBtn, 6, 0);
    lv_obj_t* toggleLbl = lv_label_create(mArpToggleBtn);
    if (isArpOn) {
        lv_obj_add_state(mArpToggleBtn, LV_STATE_CHECKED);
        lv_label_set_text(toggleLbl, "Arp: ON");
        lv_obj_set_style_bg_color(mArpToggleBtn, trackColor, 0);
    } else {
        lv_obj_set_style_bg_color(mArpToggleBtn, lv_color_hex(0x444444), 0);
        lv_label_set_text(toggleLbl, "Arp: OFF");
    }
    lv_obj_set_style_text_font(toggleLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(toggleLbl);
    lv_obj_add_event_cb(mArpToggleBtn, arpToggleBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Dropdowns Row (Pattern + Rate)
    lv_obj_t* c1DdRow = lv_obj_create(col1);
    lv_obj_set_size(c1DdRow, 335, 78);
    lv_obj_set_style_bg_opa(c1DdRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c1DdRow, 0, 0);
    lv_obj_set_style_pad_all(c1DdRow, 0, 0);
    lv_obj_remove_flag(c1DdRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(c1DdRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c1DdRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c1DdRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* patternGrp = lv_obj_create(c1DdRow);
    lv_obj_set_size(patternGrp, 162, 75);
    lv_obj_set_style_bg_opa(patternGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(patternGrp, 0, 0);
    lv_obj_set_style_pad_all(patternGrp, 0, 0);
    lv_obj_set_layout(patternGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(patternGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(patternGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* patternLbl = lv_label_create(patternGrp);
    lv_label_set_text(patternLbl, "Pattern");
    lv_obj_set_style_text_font(patternLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(patternLbl, lv_color_hex(0x888888), 0);
    
    mArpPatternDd = lv_dropdown_create(patternGrp);
    lv_dropdown_set_options(mArpPatternDd, "Up\nDown\nUp/Down\nStagger Up\nStagger Down\nRandom\nBach\nBrownian\nConverge\nDiverge");
    lv_obj_set_width(mArpPatternDd, 155);
    lv_obj_t* patternList = lv_dropdown_get_list(mArpPatternDd);
    lv_obj_set_style_max_height(patternList, 200, 0);
    int activeMode = static_cast<int>(arp.getMode());
    if (activeMode > 0) {
        lv_dropdown_set_selected(mArpPatternDd, activeMode - 1);
    } else {
        lv_dropdown_set_selected(mArpPatternDd, 0);
    }
    lv_obj_add_event_cb(mArpPatternDd, arpPatternDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* rateGrp = lv_obj_create(c1DdRow);
    lv_obj_set_size(rateGrp, 162, 75);
    lv_obj_set_style_bg_opa(rateGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rateGrp, 0, 0);
    lv_obj_set_style_pad_all(rateGrp, 0, 0);
    lv_obj_set_layout(rateGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rateGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rateGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* rateLbl = lv_label_create(rateGrp);
    lv_label_set_text(rateLbl, "Rate / Division");
    lv_obj_set_style_text_font(rateLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rateLbl, lv_color_hex(0x888888), 0);

    mArpRateDd = lv_dropdown_create(rateGrp);
    lv_dropdown_set_options(mArpRateDd, "1/2.\n1/2\n1/2T\n1/4.\n1/4\n1/4T\n1/8.\n1/8\n1/8T\n1/16.\n1/16\n1/16T\n1/32.\n1/32\n1/32T\n1/48\n1/64");
    lv_obj_set_width(mArpRateDd, 155);
    const auto& trackRef = mEngine.getTracks()[mActiveTrack];
    float curRate = trackRef.mArpRate;
    int curDivMode = trackRef.mArpDivisionMode;
    int selectedRateIdx = 4; // Default 1/4
    if (std::abs(curRate - 8.0f) < 0.01f) {
        selectedRateIdx = (curDivMode == 1) ? 0 : (curDivMode == 2 ? 2 : 1);
    } else if (std::abs(curRate - 4.0f) < 0.01f) {
        selectedRateIdx = (curDivMode == 1) ? 3 : (curDivMode == 2 ? 5 : 4);
    } else if (std::abs(curRate - 2.0f) < 0.01f) {
        selectedRateIdx = (curDivMode == 1) ? 6 : (curDivMode == 2 ? 8 : 7);
    } else if (std::abs(curRate - 1.0f) < 0.01f) {
        selectedRateIdx = (curDivMode == 1) ? 9 : (curDivMode == 2 ? 11 : 10);
    } else if (std::abs(curRate - 0.5f) < 0.01f) {
        selectedRateIdx = (curDivMode == 1) ? 12 : (curDivMode == 2 ? 14 : 13);
    } else if (std::abs(curRate - 0.25f) < 0.01f) {
        selectedRateIdx = 16;
    }
    lv_dropdown_set_selected(mArpRateDd, selectedRateIdx);
    lv_obj_t* rateList = lv_dropdown_get_list(mArpRateDd);
    lv_obj_set_style_max_height(rateList, 200, 0);
    lv_obj_add_event_cb(mArpRateDd, arpRateDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Octaves Slider Row
    lv_obj_t* octGrp = lv_obj_create(col1);
    lv_obj_set_size(octGrp, 335, 52);
    lv_obj_set_style_bg_opa(octGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(octGrp, 0, 0);
    lv_obj_set_style_pad_all(octGrp, 0, 0);
    lv_obj_set_layout(octGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(octGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(octGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* octLbl = lv_label_create(octGrp);
    int activeOctaves = arp.getOctaves();
    lv_label_set_text_fmt(octLbl, "Octaves: %+d", activeOctaves);
    lv_obj_set_style_text_font(octLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(octLbl, lv_color_hex(0x888888), 0);

    mArpOctavesSlider = lv_slider_create(octGrp);
    lv_obj_set_size(mArpOctavesSlider, 280, 12);
    lv_slider_set_range(mArpOctavesSlider, -3, 3);
    lv_slider_set_value(mArpOctavesSlider, activeOctaves, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mArpOctavesSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mArpOctavesSlider, trackColor, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(mArpOctavesSlider, 8, 0);
    lv_obj_set_user_data(mArpOctavesSlider, octLbl);
    lv_obj_add_event_cb(mArpOctavesSlider, octavesSliderEventCb, LV_EVENT_VALUE_CHANGED, this);

    // --- Card 2: Playback & Rhythm ---
    lv_obj_t* col2 = lv_obj_create(topSettingsRow);
    lv_obj_set_size(col2, 350, 215);
    applyCardStyle(col2);

    // Header row: Title + Latch Toggle
    lv_obj_t* c2Header = lv_obj_create(col2);
    lv_obj_set_size(c2Header, 335, 34);
    lv_obj_set_style_bg_opa(c2Header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c2Header, 0, 0);
    lv_obj_set_style_pad_all(c2Header, 0, 0);
    lv_obj_remove_flag(c2Header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(c2Header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c2Header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c2Header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title2 = lv_label_create(c2Header);
    lv_label_set_text(title2, "PLAYBACK & TIMING");
    lv_obj_set_style_text_font(title2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title2, trackColor, 0);

    mArpLatchBtn = lv_button_create(c2Header);
    lv_obj_set_size(mArpLatchBtn, 120, 30);
    lv_obj_add_flag(mArpLatchBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(mArpLatchBtn, 6, 0);
    lv_obj_t* latchLbl = lv_label_create(mArpLatchBtn);
    if (arp.isLatched()) {
        lv_obj_add_state(mArpLatchBtn, LV_STATE_CHECKED);
        lv_label_set_text(latchLbl, "Latch: ON");
        lv_obj_set_style_bg_color(mArpLatchBtn, trackColor, 0);
    } else {
        lv_obj_set_style_bg_color(mArpLatchBtn, lv_color_hex(0x444444), 0);
        lv_label_set_text(latchLbl, "Latch: OFF");
    }
    lv_obj_set_style_text_font(latchLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(latchLbl);
    lv_obj_add_event_cb(mArpLatchBtn, latchBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Arcs Row (Strum + Probability)
    lv_obj_t* c2ArcsRow = lv_obj_create(col2);
    lv_obj_set_size(c2ArcsRow, 335, 140);
    lv_obj_set_style_bg_opa(c2ArcsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c2ArcsRow, 0, 0);
    lv_obj_set_style_pad_all(c2ArcsRow, 0, 0);
    lv_obj_remove_flag(c2ArcsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(c2ArcsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c2ArcsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c2ArcsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* strumGrp = lv_obj_create(c2ArcsRow);
    lv_obj_set_size(strumGrp, 140, 135);
    lv_obj_set_style_bg_opa(strumGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(strumGrp, 0, 0);
    lv_obj_set_style_pad_all(strumGrp, 0, 0);
    lv_obj_set_layout(strumGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(strumGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(strumGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* strumLbl = lv_label_create(strumGrp);
    lv_label_set_text(strumLbl, "Strum");
    lv_obj_set_style_text_font(strumLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(strumLbl, lv_color_hex(0x888888), 0);

    mArpStrumArc = lv_arc_create(strumGrp);
    lv_obj_set_size(mArpStrumArc, 80, 80);
    lv_arc_set_range(mArpStrumArc, 0, 100);
    lv_obj_set_style_arc_color(mArpStrumArc, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mArpStrumArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(mArpStrumArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mArpStrumArc, 0, LV_PART_KNOB);
    int activeStrum = static_cast<int>(arp.getStrum() * 100.0f);
    lv_arc_set_value(mArpStrumArc, activeStrum);
    
    lv_obj_t* strumValLbl = lv_label_create(mArpStrumArc);
    lv_label_set_text_fmt(strumValLbl, "%d%%", activeStrum);
    lv_obj_set_style_text_font(strumValLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(strumValLbl);
    lv_obj_set_user_data(mArpStrumArc, strumValLbl);
    lv_obj_add_event_cb(mArpStrumArc, strumArcEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* probGrp = lv_obj_create(c2ArcsRow);
    lv_obj_set_size(probGrp, 140, 135);
    lv_obj_set_style_bg_opa(probGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(probGrp, 0, 0);
    lv_obj_set_style_pad_all(probGrp, 0, 0);
    lv_obj_set_layout(probGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(probGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(probGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* probLbl = lv_label_create(probGrp);
    lv_label_set_text(probLbl, "Probability");
    lv_obj_set_style_text_font(probLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(probLbl, lv_color_hex(0x888888), 0);

    mArpProbArc = lv_arc_create(probGrp);
    lv_obj_set_size(mArpProbArc, 80, 80);
    lv_arc_set_range(mArpProbArc, 0, 100);
    lv_obj_set_style_arc_color(mArpProbArc, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(mArpProbArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(mArpProbArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mArpProbArc, 0, LV_PART_KNOB);
    int activeProb = static_cast<int>(arp.getProbability() * 100.0f);
    lv_arc_set_value(mArpProbArc, activeProb);

    lv_obj_t* probValLbl = lv_label_create(mArpProbArc);
    lv_label_set_text_fmt(probValLbl, "%d%%", activeProb);
    lv_obj_set_style_text_font(probValLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(probValLbl);
    lv_obj_set_user_data(mArpProbArc, probValLbl);
    lv_obj_add_event_cb(mArpProbArc, probArcEventCb, LV_EVENT_VALUE_CHANGED, this);

    // --- Card 3: Chord Generator ---
    lv_obj_t* col3 = lv_obj_create(topSettingsRow);
    lv_obj_set_size(col3, 350, 215);
    applyCardStyle(col3);

    // Header row: Title + Chord Gen Button
    lv_obj_t* c3Header = lv_obj_create(col3);
    lv_obj_set_size(c3Header, 335, 34);
    lv_obj_set_style_bg_opa(c3Header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c3Header, 0, 0);
    lv_obj_set_style_pad_all(c3Header, 0, 0);
    lv_obj_remove_flag(c3Header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(c3Header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c3Header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c3Header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title3 = lv_label_create(c3Header);
    lv_label_set_text(title3, "CHORD GENERATOR");
    lv_obj_set_style_text_font(title3, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title3, trackColor, 0);

    mArpChordGenBtn = lv_button_create(c3Header);
    lv_obj_set_size(mArpChordGenBtn, 120, 30);
    lv_obj_add_flag(mArpChordGenBtn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(mArpChordGenBtn, 6, 0);
    lv_obj_t* chEnLbl = lv_label_create(mArpChordGenBtn);
    if (arp.isChordProgEnabled()) {
        lv_obj_add_state(mArpChordGenBtn, LV_STATE_CHECKED);
        lv_label_set_text(chEnLbl, "Chord: ON");
        lv_obj_set_style_bg_color(mArpChordGenBtn, trackColor, 0);
    } else {
        lv_obj_set_style_bg_color(mArpChordGenBtn, lv_color_hex(0x444444), 0);
        lv_label_set_text(chEnLbl, "Chord: OFF");
    }
    lv_obj_set_style_text_font(chEnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(chEnLbl);
    lv_obj_add_event_cb(mArpChordGenBtn, chEnBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Dropdowns Row (Mood + Complexity)
    lv_obj_t* c3DdRow = lv_obj_create(col3);
    lv_obj_set_size(c3DdRow, 335, 78);
    lv_obj_set_style_bg_opa(c3DdRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c3DdRow, 0, 0);
    lv_obj_set_style_pad_all(c3DdRow, 0, 0);
    lv_obj_remove_flag(c3DdRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(c3DdRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(c3DdRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c3DdRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* moodGrp = lv_obj_create(c3DdRow);
    lv_obj_set_size(moodGrp, 162, 75);
    lv_obj_set_style_bg_opa(moodGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(moodGrp, 0, 0);
    lv_obj_set_style_pad_all(moodGrp, 0, 0);
    lv_obj_set_layout(moodGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(moodGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(moodGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* moodLbl = lv_label_create(moodGrp);
    lv_label_set_text(moodLbl, "Mood");
    lv_obj_set_style_text_font(moodLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(moodLbl, lv_color_hex(0x888888), 0);

    mArpChordMoodDd = lv_dropdown_create(moodGrp);
    lv_dropdown_set_options(mArpChordMoodDd, "Calm\nHappy\nSad\nSpooky\nAngry\nExcited\nGrandiose\nTense\nEthereal\nRomantic\nMysterious\nUplifting\nMelancholy\nDark\nDreamy\nMajestic");
    lv_obj_set_width(mArpChordMoodDd, 155);
    lv_dropdown_set_selected(mArpChordMoodDd, arp.getChordProgMood());
    lv_obj_t* chMoodList = lv_dropdown_get_list(mArpChordMoodDd);
    lv_obj_set_style_max_height(chMoodList, 200, 0);
    lv_obj_add_event_cb(mArpChordMoodDd, arpChordMoodDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* compGrp = lv_obj_create(c3DdRow);
    lv_obj_set_size(compGrp, 162, 75);
    lv_obj_set_style_bg_opa(compGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compGrp, 0, 0);
    lv_obj_set_style_pad_all(compGrp, 0, 0);
    lv_obj_set_layout(compGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(compGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(compGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* compLbl = lv_label_create(compGrp);
    lv_label_set_text(compLbl, "Complexity");
    lv_obj_set_style_text_font(compLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(compLbl, lv_color_hex(0x888888), 0);

    mArpChordComplexityDd = lv_dropdown_create(compGrp);
    lv_dropdown_set_options(mArpChordComplexityDd, "Simple\nComplex\nColtrane");
    lv_obj_set_width(mArpChordComplexityDd, 155);
    lv_dropdown_set_selected(mArpChordComplexityDd, arp.getChordProgComplexity());
    lv_obj_t* chCompList = lv_dropdown_get_list(mArpChordComplexityDd);
    lv_obj_set_style_max_height(chCompList, 200, 0);
    lv_obj_add_event_cb(mArpChordComplexityDd, arpChordComplexityDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Inversions Slider Row
    lv_obj_t* invGrp = lv_obj_create(col3);
    lv_obj_set_size(invGrp, 335, 52);
    lv_obj_set_style_bg_opa(invGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(invGrp, 0, 0);
    lv_obj_set_style_pad_all(invGrp, 0, 0);
    lv_obj_set_layout(invGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(invGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(invGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* invLbl = lv_label_create(invGrp);
    int activeInversion = arp.getInversion();
    lv_label_set_text_fmt(invLbl, "Inversions: %+d", activeInversion);
    lv_obj_set_style_text_font(invLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(invLbl, lv_color_hex(0x888888), 0);

    mArpInversionsSlider = lv_slider_create(invGrp);
    lv_obj_set_size(mArpInversionsSlider, 280, 12);
    lv_slider_set_range(mArpInversionsSlider, -3, 3);
    lv_slider_set_value(mArpInversionsSlider, activeInversion, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mArpInversionsSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mArpInversionsSlider, trackColor, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(mArpInversionsSlider, 8, 0);
    lv_obj_set_user_data(mArpInversionsSlider, invLbl);
    lv_obj_add_event_cb(mArpInversionsSlider, inversionsSliderEventCb, LV_EVENT_VALUE_CHANGED, this);

    // =========================================================================
    // --- ROW 2: 16-STEP PATTERN GRID (Height: ~430px) ---
    // =========================================================================
    lv_obj_t* gridRow = lv_obj_create(arpRoot);
    lv_obj_set_size(gridRow, 1060, 440);
    lv_obj_set_style_bg_opa(gridRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gridRow, 0, 0);
    lv_obj_set_style_pad_all(gridRow, 0, 0);
    lv_obj_set_style_pad_column(gridRow, 4, 0);
    lv_obj_remove_flag(gridRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(gridRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gridRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gridRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Row Label Column (Rt, +1, -1, etc.)
    lv_obj_t* lblCol = lv_obj_create(gridRow);
    lv_obj_set_size(lblCol, 40, 435);
    lv_obj_set_style_bg_opa(lblCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lblCol, 0, 0);
    lv_obj_set_style_pad_all(lblCol, 0, 0);
    lv_obj_remove_flag(lblCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lblCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lblCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lblCol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* topSpacer = lv_label_create(lblCol);
    lv_label_set_text(topSpacer, " ");
    lv_obj_set_style_text_font(topSpacer, &lv_font_montserrat_12, 0);

    const char* rowLabels[] = {"+3", "+2", "+1", "Rt", "-1", "-2", "-3"};
    for (int r = 0; r < 7; ++r) {
        lv_obj_t* rl = lv_label_create(lblCol);
        lv_label_set_text(rl, rowLabels[r]);
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rl, lv_color_hex(0xCCCCCC), 0);
    }

    // 16 Step Columns (Button size: 48x48px)
    for (int c = 0; c < 16; ++c) {
        lv_obj_t* colCont = lv_obj_create(gridRow);
        mArpColumns[c] = colCont;
        lv_obj_set_size(colCont, 56, 435);
        lv_obj_set_style_bg_opa(colCont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(colCont, 0, 0);
        lv_obj_set_style_pad_all(colCont, 0, 0);
        lv_obj_remove_flag(colCont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(colCont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(colCont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(colCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Column label 1-16
        lv_obj_t* colNum = lv_label_create(colCont);
        lv_label_set_text_fmt(colNum, "%d", c + 1);
        bool isBeatStart = (c == 0 || c == 4 || c == 8 || c == 12);
        lv_obj_set_style_text_font(colNum, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(colNum, isBeatStart ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x777777), 0);

        // 7 buttons for the 7 rows
        for (int r = 0; r < 7; ++r) {
            lv_obj_t* btn = lv_button_create(colCont);
            lv_obj_set_size(btn, 48, 48);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_border_width(btn, 0, 0);

            // Inactive (default) background
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x242424), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

            // Row-specific checked colors
            lv_color_t checkedColor;
            if (r == 0) checkedColor = lv_color_hex(0x4B0082);      // +3 (Indigo)
            else if (r == 1) checkedColor = lv_color_hex(0x8A2BE2); // +2 (Blue Violet)
            else if (r == 2) checkedColor = lv_color_hex(0x32CD32); // +1 (Lime Green)
            else if (r == 3) checkedColor = lv_color_hex(0x1E90FF); // Rt (Dodger Blue)
            else if (r == 4) checkedColor = lv_color_hex(0xFF8C00); // -1 (Dark Orange)
            else if (r == 5) checkedColor = lv_color_hex(0xFF4500); // -2 (Orange Red)
            else checkedColor = lv_color_hex(0xDC143C);             // -3 (Crimson)

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

            lv_obj_set_user_data(btn, (void*)(uintptr_t)(r * 16 + c));
            lv_obj_add_event_cb(btn, arpButtonEventCb, LV_EVENT_VALUE_CHANGED, this);
            
            mArpButtons[r][c] = btn;
        }
    }

    // =========================================================================
    // --- ROW 3: BOTTOM ACTIONS (Height: ~45px) ---
    // =========================================================================
    lv_obj_t* bottomRow = lv_obj_create(arpRoot);
    lv_obj_set_size(bottomRow, lv_pct(100), 45);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_remove_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left: Legend
    lv_obj_t* legendCont = lv_obj_create(bottomRow);
    lv_obj_set_size(legendCont, 420, 40);
    lv_obj_set_style_bg_opa(legendCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legendCont, 0, 0);
    lv_obj_set_style_pad_all(legendCont, 0, 0);
    lv_obj_remove_flag(legendCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(legendCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(legendCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legendCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(legendCont, 8, 0);

    const char* legendTexts[] = {"-3", "-2", "-1", "Rt", "+1", "+2", "+3"};
    lv_color_t legendColors[] = {
        lv_color_hex(0xDC143C),
        lv_color_hex(0xFF4500),
        lv_color_hex(0xFF8C00),
        lv_color_hex(0x1E90FF),
        lv_color_hex(0x32CD32),
        lv_color_hex(0x8A2BE2),
        lv_color_hex(0x4B0082)
    };

    for (int i = 0; i < 7; ++i) {
        lv_obj_t* legItem = lv_obj_create(legendCont);
        lv_obj_set_size(legItem, 52, 34);
        lv_obj_set_style_bg_opa(legItem, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(legItem, 0, 0);
        lv_obj_set_style_pad_all(legItem, 0, 0);
        lv_obj_remove_flag(legItem, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(legItem, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(legItem, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(legItem, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(legItem, 4, 0);

        lv_obj_t* dot = lv_obj_create(legItem);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_bg_color(dot, legendColors[i], 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);

        lv_obj_t* txt = lv_label_create(legItem);
        lv_label_set_text(txt, legendTexts[i]);
        lv_obj_set_style_text_font(txt, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(txt, legendColors[i], 0);
    }

    // Center/Right Controls: Copy To Dropdown, Rand Rhythm, Rand Notes, MIDI Learn
    lv_obj_t* rightActCont = lv_obj_create(bottomRow);
    lv_obj_set_size(rightActCont, 620, 42);
    lv_obj_set_style_bg_opa(rightActCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightActCont, 0, 0);
    lv_obj_set_style_pad_all(rightActCont, 0, 0);
    lv_obj_remove_flag(rightActCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rightActCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightActCont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rightActCont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rightActCont, 10, 0);

    // "Copy To..." Track Dropdown
    mArpCopyToDd = lv_dropdown_create(rightActCont);
    lv_dropdown_set_options(mArpCopyToDd, "Copy To...\nTrack 1\nTrack 2\nTrack 3\nTrack 4\nTrack 5\nTrack 6\nTrack 7\nTrack 8");
    lv_obj_set_size(mArpCopyToDd, 120, 36);
    lv_obj_set_style_text_font(mArpCopyToDd, &lv_font_montserrat_10, 0);
    lv_dropdown_set_selected(mArpCopyToDd, 0);
    lv_obj_add_event_cb(mArpCopyToDd, arpCopyToDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Rand Rhythm Button
    lv_obj_t* randRhyBtn = lv_button_create(rightActCont);
    lv_obj_set_size(randRhyBtn, 110, 36);
    lv_obj_set_style_bg_color(randRhyBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(randRhyBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(randRhyBtn, 1, 0);
    lv_obj_set_style_radius(randRhyBtn, 6, 0);
    lv_obj_t* randRhyLbl = lv_label_create(randRhyBtn);
    lv_label_set_text(randRhyLbl, "Rand Rhythm");
    lv_obj_set_style_text_font(randRhyLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(randRhyLbl);
    lv_obj_add_event_cb(randRhyBtn, randRhythmBtnEventCb, LV_EVENT_CLICKED, this);

    // Rand Notes Button
    lv_obj_t* randNotBtn = lv_button_create(rightActCont);
    lv_obj_set_size(randNotBtn, 110, 36);
    lv_obj_set_style_bg_color(randNotBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(randNotBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(randNotBtn, 1, 0);
    lv_obj_set_style_radius(randNotBtn, 6, 0);
    lv_obj_t* randNotLbl = lv_label_create(randNotBtn);
    lv_label_set_text(randNotLbl, "Rand Notes");
    lv_obj_set_style_text_font(randNotLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(randNotLbl);
    lv_obj_add_event_cb(randNotBtn, randNotesBtnEventCb, LV_EVENT_CLICKED, this);

    // MIDI Learn Button for Arpeggiator
    lv_obj_t* learnBtn = lv_button_create(rightActCont);
    lv_obj_set_size(learnBtn, 120, 36);
    if (mMidiLearnActive) {
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0xD32F2F), 0);
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
        ui->createCenterContentArea();
    };
    lv_obj_add_event_cb(learnBtn, learnClickCb, LV_EVENT_CLICKED, this);
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
    std::vector<std::vector<bool>> rhythms(7, std::vector<bool>(16, false));
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (mArpButtons[r][c]) {
                rhythms[r][c] = lv_obj_has_state(mArpButtons[r][c], LV_STATE_CHECKED);
            }
        }
    }
    const auto& currentArp = mEngine.getTracks()[mActiveTrack].arpeggiator;
    mEngine.setArpConfig(mActiveTrack, mode, octaves, inversion, isLatched, false,
                         rhythms, currentArp.getRandomSequence(),
                         currentArp.getGateLengths().empty() ? std::vector<float>(16, 0.5f) : currentArp.getGateLengths(),
                         probability, currentArp.getWeird());
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
        for (int row = 0; row < 7; ++row) {
            lv_obj_remove_state(mArpButtons[row][col], LV_STATE_CHECKED);
        }
        if ((rand() % 100) < 35) {
            lv_obj_add_state(mArpButtons[3][col], LV_STATE_CHECKED); // Root note row is row index 3
        }
    }
    updateArpConfig();
}

void UIManager::randomizeNotes() {
    for (int col = 0; col < 16; ++col) {
        for (int row = 0; row < 7; ++row) {
            lv_obj_remove_state(mArpButtons[row][col], LV_STATE_CHECKED);
        }
        if ((rand() % 100) < 35) {
            int activeRow = rand() % 7;
            lv_obj_add_state(mArpButtons[activeRow][col], LV_STATE_CHECKED);
        }
    }
    updateArpConfig();
}

void UIManager::arpCopyToDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int selected = lv_dropdown_get_selected(dd);
    if (selected <= 0 || selected > 8) return; // 0 is "Copy To..." placeholder
    int targetTrack = selected - 1;

    // Reset dropdown back to 0
    lv_dropdown_set_selected(dd, 0);

    if (targetTrack == ui->mActiveTrack) return;

    // Deep copy entire arpeggiator (including held notes, generated progressions, rates, state) to target track
    ui->mEngine.copyArpeggiator(ui->mActiveTrack, targetTrack);
    ui->mTrackEnabled[targetTrack] = true;
    ui->updateHighlighting();

    std::cout << "Copied Arp & Chord settings and playback from Track " << (ui->mActiveTrack + 1)
              << " to Track " << (targetTrack + 1) << std::endl;
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

    lv_obj_t* tab1 = lv_tabview_add_tab(mSettingsTabview, "System & Audio");
    lv_obj_t* tab2 = lv_tabview_add_tab(mSettingsTabview, "MIDI Pads");
    lv_obj_t* tab3 = lv_tabview_add_tab(mSettingsTabview, "Knobs/Faders");
    lv_obj_t* tab4 = lv_tabview_add_tab(mSettingsTabview, "USB MIDI");

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
    populateSettingsUsbMidiTab(tab4);

    if (mSettingsActiveTabIdx > 0 && mSettingsActiveTabIdx < 4) {
        lv_tabview_set_active(mSettingsTabview, mSettingsActiveTabIdx, LV_ANIM_OFF);
    }
}

// ==========================================================================
// Tab 1: System & Audio (Unified)
// ==========================================================================
void UIManager::populateSettingsGeneralTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto applyCardStyle = [trackColor](lv_obj_t* card) {
        lv_obj_set_size(card, 260, 680);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, trackColor, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, 14, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(card, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    };

    auto makeSectionDivider = [](lv_obj_t* parent, const char* title) {
        lv_obj_t* cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 232, 18);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cont, 8, 0);

        lv_obj_t* lbl = lv_label_create(cont);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x777777), 0);

        lv_obj_t* line = lv_obj_create(cont);
        lv_obj_set_size(line, LV_PCT(100), 1);
        lv_obj_set_style_bg_color(line, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_set_flex_grow(line, 1);
        return cont;
    };

    auto makeLabeledDropdown = [](lv_obj_t* parent, const char* labelText) -> std::pair<lv_obj_t*, lv_obj_t*> {
        lv_obj_t* cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 232, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(cont, 4, 0);

        lv_obj_t* lbl = lv_label_create(cont);
        lv_label_set_text(lbl, labelText);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xBBBBBB), 0);

        lv_obj_t* dd = lv_dropdown_create(cont);
        lv_obj_set_size(dd, 232, 36);
        lv_obj_set_style_bg_color(dd, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(dd, 1, 0);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);
        lv_obj_set_style_radius(dd, 6, 0);

        return {lbl, dd};
    };

    // =========================================================================
    // Column 1: AUDIO ENGINE (10 items spaced evenly)
    // =========================================================================
    lv_obj_t* audioCard = lv_obj_create(tab);
    applyCardStyle(audioCard);

    // Item 1: Card Title
    lv_obj_t* audioTitle = lv_label_create(audioCard);
    lv_label_set_text(audioTitle, "AUDIO ENGINE");
    lv_obj_set_style_text_font(audioTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(audioTitle, trackColor, 0);

    // Item 2: Section 1 Divider
    makeSectionDivider(audioCard, "DEVICE & FORMAT");

    // Item 3: SDL Audio Device (Labeled Dropdown)
    auto [deviceLabel, deviceDd] = makeLabeledDropdown(audioCard, "Active Audio Device:");
    (void)deviceLabel;

    std::string deviceOptions = "Default\n";
    int numDevs = SDL_GetNumAudioDevices(0);
    std::vector<std::string> devNames;
    devNames.push_back("Default");
    int activeDevIdx = 0;
    for (int i = 0; i < numDevs; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name) {
            deviceOptions += std::string(name) + "\n";
            devNames.push_back(name);
            if (gCurrentAudioDevice == name) activeDevIdx = devNames.size() - 1;
        }
    }
    if (!deviceOptions.empty() && deviceOptions.back() == '\n') deviceOptions.pop_back();

    lv_dropdown_set_options(deviceDd, deviceOptions.c_str());
    lv_dropdown_set_selected(deviceDd, activeDevIdx);

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
            if (switchAudioDevice(selectedName)) {
                d->ui->mSettingsAudioDevice = selectedName;
                d->ui->saveSettings(d->ui->mSettingsFilePath);
            }
        }
    };
    lv_obj_add_event_cb(deviceDd, devCb, LV_EVENT_VALUE_CHANGED, devData);
    auto devFreeCb = [](lv_event_t* e) { delete (DeviceChangeData*)lv_event_get_user_data(e); };
    lv_obj_add_event_cb(deviceDd, devFreeCb, LV_EVENT_DELETE, devData);

    // Item 4: SDL Audio Input Device (Labeled Dropdown)
    auto [inDevLabel, inDevDd] = makeLabeledDropdown(audioCard, "Active Input Device:");
    (void)inDevLabel;

    std::string inDevOptions = "Default\n";
    int numInDevs = SDL_GetNumAudioDevices(1);
    std::vector<std::string> inDevNames;
    inDevNames.push_back("Default");
    int activeInDevIdx = 0;
    for (int i = 0; i < numInDevs; ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (name) {
            inDevOptions += std::string(name) + "\n";
            inDevNames.push_back(name);
            if (gCurrentCaptureDevice == name || mSettingsAudioMicDevice == name) {
                activeInDevIdx = inDevNames.size() - 1;
            }
        }
    }
    if (!inDevOptions.empty() && inDevOptions.back() == '\n') inDevOptions.pop_back();

    lv_dropdown_set_options(inDevDd, inDevOptions.c_str());
    lv_dropdown_set_selected(inDevDd, activeInDevIdx);

    DeviceChangeData* inDevData = new DeviceChangeData{this, inDevNames};
    auto inDevCb = [](lv_event_t* e) {
        DeviceChangeData* d = (DeviceChangeData*)lv_event_get_user_data(e);
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        int selected = lv_dropdown_get_selected(dd);
        if (selected >= 0 && selected < (int)d->names.size()) {
            std::string selectedName = d->names[selected];
            if (switchCaptureDevice(selectedName)) {
                d->ui->mSettingsAudioMicDevice = selectedName;
                d->ui->mSettingsAudioLineInDevice = selectedName;
                d->ui->saveSettings(d->ui->mSettingsFilePath);
            }
        }
    };
    lv_obj_add_event_cb(inDevDd, inDevCb, LV_EVENT_VALUE_CHANGED, inDevData);
    auto inDevFreeCb = [](lv_event_t* e) { delete (DeviceChangeData*)lv_event_get_user_data(e); };
    lv_obj_add_event_cb(inDevDd, inDevFreeCb, LV_EVENT_DELETE, inDevData);

    // Item 5: Sample Rate (Labeled Dropdown)
    auto [srLbl, srDd] = makeLabeledDropdown(audioCard, "Sample Rate:");
    (void)srLbl;
    lv_dropdown_set_options(srDd, "44100 Hz\n48000 Hz");
    float currentSr = mEngine.getSampleRate();
    lv_dropdown_set_selected(srDd, (std::abs(currentSr - 48000.0f) < 100.0f) ? 1 : 0);
    lv_obj_add_event_cb(srDd, settingsSampleRateDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 5: Audio Output Mode (Labeled Dropdown)
    auto [outModeLbl, outModeDd] = makeLabeledDropdown(audioCard, "Output Channel Mode:");
    (void)outModeLbl;
    lv_dropdown_set_options(outModeDd, "Stereo\nMono (L-Only)\nPseudo-Stereo\nPhase-Invert");
    lv_dropdown_set_selected(outModeDd, mEngine.getAudioOutputMode());
    auto outModeDdCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        int selected = lv_dropdown_get_selected(dd);
        ui->mEngine.setAudioOutputMode(selected);
    };
    lv_obj_add_event_cb(outModeDd, outModeDdCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 6: Buffer & Latency info badge
    lv_obj_t* bufferLabel = lv_label_create(audioCard);
    lv_label_set_text(bufferLabel, "Buffer: 256 samples | Latency: ~5.3 ms");
    lv_obj_set_style_text_font(bufferLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bufferLabel, lv_color_hex(0x888888), 0);

    // Item 7: Section 2 Divider
    makeSectionDivider(audioCard, "DSP & UTILITY");

    // Item 8: Fast Granular switch
    lv_obj_t* fastGranRow = lv_obj_create(audioCard);
    lv_obj_set_size(fastGranRow, 232, 38);
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
    auto fastGranCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        bool isChecked = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        ui->mEngine.setFastGranularEnabled(isChecked);
        ui->saveSettings(ui->mSettingsFilePath);
    };
    lv_obj_add_event_cb(fastGranSw, fastGranCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 9: PANIC ALL OFF button
    lv_obj_t* panicBtn = lv_button_create(audioCard);
    lv_obj_set_size(panicBtn, 232, 38);
    lv_obj_set_style_bg_color(panicBtn, lv_color_hex(0x661111), 0);
    lv_obj_set_style_border_color(panicBtn, lv_color_hex(0xCC3333), 0);
    lv_obj_set_style_border_width(panicBtn, 1, 0);
    lv_obj_set_style_radius(panicBtn, 8, 0);
    lv_obj_t* panicLbl = lv_label_create(panicBtn);
    lv_label_set_text(panicLbl, LV_SYMBOL_WARNING " PANIC ALL OFF");
    lv_obj_set_style_text_font(panicLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(panicLbl);
    lv_obj_add_event_cb(panicBtn, settingsPanicBtnEventCb, LV_EVENT_CLICKED, this);

    // Item 10: RESET MIDI / PATCHING button
    lv_obj_t* resetMidiBtn = lv_button_create(audioCard);
    lv_obj_set_size(resetMidiBtn, 232, 38);
    lv_obj_set_style_bg_color(resetMidiBtn, lv_color_hex(0x996633), 0);
    lv_obj_set_style_border_color(resetMidiBtn, lv_color_hex(0xCC9944), 0);
    lv_obj_set_style_border_width(resetMidiBtn, 1, 0);
    lv_obj_set_style_radius(resetMidiBtn, 8, 0);
    lv_obj_t* resetMidiLbl = lv_label_create(resetMidiBtn);
    lv_label_set_text(resetMidiLbl, "RESET MIDI / PATCHING");
    lv_obj_set_style_text_font(resetMidiLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(resetMidiLbl);
    lv_obj_add_event_cb(resetMidiBtn, settingsResetMidiBtnEventCb, LV_EVENT_CLICKED, this);

    // =========================================================================
    // Column 2: MIDI ROUTING (10 items spaced evenly)
    // =========================================================================
    lv_obj_t* midiCard = lv_obj_create(tab);
    applyCardStyle(midiCard);

    // Item 1: Card Title
    lv_obj_t* midiTitle = lv_label_create(midiCard);
    lv_label_set_text(midiTitle, "MIDI ROUTING");
    lv_obj_set_style_text_font(midiTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(midiTitle, trackColor, 0);

    // Item 2: Section 1 Divider
    makeSectionDivider(midiCard, "TRACK CHANNEL ROUTING");

    // Item 3: Configure Track (Labeled Dropdown)
    auto [trkLbl, trkDd] = makeLabeledDropdown(midiCard, "Configure Track:");
    (void)trkLbl;
    mSettingsMidiTrackDd = trkDd;
    lv_dropdown_set_options(mSettingsMidiTrackDd, "Track 1\nTrack 2\nTrack 3\nTrack 4\nTrack 5\nTrack 6\nTrack 7\nTrack 8");
    mSettingsMidiTrackSelect = mActiveTrack;
    lv_dropdown_set_selected(mSettingsMidiTrackDd, mSettingsMidiTrackSelect);
    lv_obj_add_event_cb(mSettingsMidiTrackDd, settingsMidiTrackSelectDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 4: MIDI Input Channel (Labeled Dropdown)
    auto [midiInLbl, midiInDd] = makeLabeledDropdown(midiCard, "MIDI Input Channel:");
    (void)midiInLbl;
    mSettingsMidiInDd = midiInDd;
    lv_dropdown_set_options(mSettingsMidiInDd, "NONE\nChannel 1\nChannel 2\nChannel 3\nChannel 4\nChannel 5\nChannel 6\nChannel 7\nChannel 8\nChannel 9\nChannel 10\nChannel 11\nChannel 12\nChannel 13\nChannel 14\nChannel 15\nChannel 16\nALL");
    int currentInChan = mEngine.getTracks()[mSettingsMidiTrackSelect].midiInChannel;
    if (currentInChan >= 0 && currentInChan <= 17) {
        lv_dropdown_set_selected(mSettingsMidiInDd, currentInChan);
    }
    lv_obj_add_event_cb(mSettingsMidiInDd, settingsMidiInChannelDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 5: Routing description note
    lv_obj_t* routeDesc = lv_label_create(midiCard);
    lv_label_set_text(routeDesc, "ALL = respond when selected.\nSpecific ch = always respond.");
    lv_obj_set_style_text_font(routeDesc, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(routeDesc, lv_color_hex(0x888888), 0);

    // Item 6: MIDI Output Channel (Labeled Dropdown)
    auto [midiOutLbl, midiOutDd] = makeLabeledDropdown(midiCard, "MIDI Output Channel:");
    (void)midiOutLbl;
    mSettingsMidiOutDd = midiOutDd;
    lv_dropdown_set_options(mSettingsMidiOutDd, "NONE\nChannel 1\nChannel 2\nChannel 3\nChannel 4\nChannel 5\nChannel 6\nChannel 7\nChannel 8\nChannel 9\nChannel 10\nChannel 11\nChannel 12\nChannel 13\nChannel 14\nChannel 15\nChannel 16");
    int currentOutChan = mEngine.getTracks()[mSettingsMidiTrackSelect].midiOutChannel;
    if (currentOutChan >= 0 && currentOutChan <= 16) {
        lv_dropdown_set_selected(mSettingsMidiOutDd, currentOutChan);
    }
    lv_obj_add_event_cb(mSettingsMidiOutDd, settingsMidiOutChannelDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 7: Section 2 Divider
    makeSectionDivider(midiCard, "CONTROLLER MODES");

    // Item 8: Velocity Sensitivity switch row
    lv_obj_t* velSensRow = lv_obj_create(midiCard);
    lv_obj_set_size(velSensRow, 232, 38);
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
    if (mEngine.getVelocitySensitivityEnabled()) lv_obj_add_state(velSensSw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(velSensSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    auto velSensCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        bool isChecked = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        ui->mEngine.setVelocitySensitivityEnabled(isChecked);
    };
    lv_obj_add_event_cb(velSensSw, velSensCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 9: QWERTY keyboard mode switch row
    lv_obj_t* kbRow = lv_obj_create(midiCard);
    lv_obj_set_size(kbRow, 232, 38);
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

    // Item 10: QWERTY layout hint label
    lv_obj_t* kbHint = lv_label_create(midiCard);
    lv_label_set_text(kbHint, "QWERTY keys play active track.");
    lv_obj_set_style_text_font(kbHint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(kbHint, lv_color_hex(0x666666), 0);

    // =========================================================================
    // Column 3: PROJECT & DISPLAY (10 items spaced evenly)
    // =========================================================================
    lv_obj_t* systemCard = lv_obj_create(tab);
    applyCardStyle(systemCard);

    // Item 1: Card Title
    lv_obj_t* systemTitle = lv_label_create(systemCard);
    lv_label_set_text(systemTitle, "PROJECT & DISPLAY");
    lv_obj_set_style_text_font(systemTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(systemTitle, trackColor, 0);

    // Item 2: Section 1 Divider
    makeSectionDivider(systemCard, "PROJECT ACTIONS");

    auto makeFileBtn = [trackColor](lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* userData) {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, 232, 38);
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

    // Item 3: New Project Button
    makeFileBtn(systemCard, LV_SYMBOL_FILE " NEW PROJECT", settingsNewBtnEventCb, this);
    // Item 4: Save Project Button
    makeFileBtn(systemCard, LV_SYMBOL_SAVE " SAVE PROJECT", settingsSaveBtnEventCb, this);
    // Item 5: Load Project Button
    makeFileBtn(systemCard, LV_SYMBOL_DIRECTORY " LOAD PROJECT", settingsLoadBtnEventCb, this);

    // Item 6: Section 2 Divider
    makeSectionDivider(systemCard, "DISPLAY & POWER");

    // Item 7: Screen Brightness Slider
    lv_obj_t* brightRow = lv_obj_create(systemCard);
    lv_obj_set_size(brightRow, 232, 48);
    lv_obj_set_style_bg_opa(brightRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brightRow, 0, 0);
    lv_obj_set_style_pad_all(brightRow, 0, 0);
    lv_obj_remove_flag(brightRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* brightTitle = lv_label_create(brightRow);
    int currentB = mSettingsBacklightBrightness > 0 ? mSettingsBacklightBrightness : HardwareDisplay::getBrightness();
    mSettingsBacklightBrightness = currentB;
    lv_label_set_text_fmt(brightTitle, "BRIGHTNESS: %d%%", currentB);
    lv_obj_set_style_text_font(brightTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(brightTitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(brightTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* brightSlider = lv_slider_create(brightRow);
    lv_obj_set_size(brightSlider, 232, 20);
    lv_obj_align(brightSlider, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_slider_set_range(brightSlider, 10, 100);
    lv_slider_set_value(brightSlider, currentB, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightSlider, trackColor, LV_PART_KNOB);

    struct BrightData {
        UIManager* ui;
        lv_obj_t* label;
    };
    BrightData* bData = new BrightData{this, brightTitle};
    lv_obj_add_event_cb(brightSlider, [](lv_event_t* e) {
        BrightData* d = (BrightData*)lv_event_get_user_data(e);
        lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        d->ui->mSettingsBacklightBrightness = val;
        HardwareDisplay::setBrightness(val);
        lv_label_set_text_fmt(d->label, "BRIGHTNESS: %d%%", val);
        d->ui->saveSettings(d->ui->mSettingsFilePath);
    }, LV_EVENT_VALUE_CHANGED, bData);
    lv_obj_add_event_cb(brightSlider, [](lv_event_t* e) {
        delete (BrightData*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, bData);

    // Item 8: Screen Timeout (Labeled Dropdown)
    auto [timeoutLbl, timeoutDd] = makeLabeledDropdown(systemCard, "Screen Timeout:");
    (void)timeoutLbl;
    lv_dropdown_set_options(timeoutDd, "Never\n1 min\n2 min\n5 min\n10 min\n15 min\n30 min");

    static const int kTimeoutSecs[] = {0, 60, 120, 300, 600, 900, 1800};
    int selectedTimeoutIdx = 0;
    for (int idx = 0; idx < 7; ++idx) {
        if (mSettingsScreenTimeoutSec == kTimeoutSecs[idx]) {
            selectedTimeoutIdx = idx;
            break;
        }
    }
    lv_dropdown_set_selected(timeoutDd, selectedTimeoutIdx);

    auto timeoutDdCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
        int sel = lv_dropdown_get_selected(dd);
        static const int kTimeoutSecsMap[] = {0, 60, 120, 300, 600, 900, 1800};
        if (sel >= 0 && sel < 7) {
            ui->mSettingsScreenTimeoutSec = kTimeoutSecsMap[sel];
            ui->mLastActivityTicks = SDL_GetTicks();
            ui->saveSettings(ui->mSettingsFilePath);
        }
    };
    lv_obj_add_event_cb(timeoutDd, timeoutDdCb, LV_EVENT_VALUE_CHANGED, this);

    // Item 9: Display Flip Button
    lv_obj_t* flipBtn = lv_button_create(systemCard);
    lv_obj_set_size(flipBtn, 232, 38);
    lv_obj_set_style_bg_color(flipBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(flipBtn, trackColor, 0);
    lv_obj_set_style_border_width(flipBtn, 1, 0);
    lv_obj_set_style_radius(flipBtn, 8, 0);
    lv_obj_t* flipLbl = lv_label_create(flipBtn);
    double curAngle = HardwareDisplay::getRotationAngle();
    lv_label_set_text_fmt(flipLbl, "FLIP ORIENTATION (%.0f" "\xC2\xB0" ")", curAngle);
    lv_obj_set_style_text_font(flipLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(flipLbl);
    lv_obj_add_event_cb(flipBtn, [](lv_event_t* e) {
        lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        double angle = HardwareDisplay::getRotationAngle();
        double newAngle = (angle == 270.0) ? 90.0 : (angle == 90.0 ? 270.0 : (angle == 0.0 ? 180.0 : 0.0));
        HardwareDisplay::setRotationAngle(newAngle);
        lv_label_set_text_fmt(lbl, "FLIP ORIENTATION (%.0f" "\xC2\xB0" ")", newAngle);
    }, LV_EVENT_CLICKED, this);

    // Item 10: Credits/Privacy button
    lv_obj_t* credBtn = lv_button_create(systemCard);
    lv_obj_set_size(credBtn, 232, 38);
    lv_obj_set_style_bg_color(credBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(credBtn, trackColor, 0);
    lv_obj_set_style_border_width(credBtn, 1, 0);
    lv_obj_set_style_radius(credBtn, 8, 0);
    lv_obj_t* credBtnLbl = lv_label_create(credBtn);
    lv_label_set_text(credBtnLbl, LV_SYMBOL_LIST " CREDITS / PRIVACY");
    lv_obj_set_style_text_font(credBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(credBtnLbl);
    lv_obj_add_event_cb(credBtn, settingsCreditsBtnEventCb, LV_EVENT_CLICKED, this);

    // =========================================================================
    // Column 4: PERFORMANCE & UPDATES (10 items spaced evenly)
    // =========================================================================
    lv_obj_t* perfCard = lv_obj_create(tab);
    applyCardStyle(perfCard);

    // Item 1: Card Title
    lv_obj_t* perfTitle = lv_label_create(perfCard);
    lv_label_set_text(perfTitle, "PERFORMANCE & UPDATES");
    lv_obj_set_style_text_font(perfTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(perfTitle, trackColor, 0);

    // Item 2: Section 1 Divider
    makeSectionDivider(perfCard, "SYSTEM STATUS");

    // Item 3: System Status Info Box (container with status labels)
    lv_obj_t* statusCont = lv_obj_create(perfCard);
    lv_obj_set_size(statusCont, 232, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statusCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(statusCont, 0, 0);
    lv_obj_set_style_pad_all(statusCont, 0, 0);
    lv_obj_remove_flag(statusCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(statusCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(statusCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(statusCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(statusCont, 5, 0);

    mCpuLoadLabel = lv_label_create(statusCont);
    float initCpuTemp = HardwareDisplay::getCpuTemperature();
    if (initCpuTemp > 0.0f) {
        lv_label_set_text_fmt(mCpuLoadLabel, "CPU: %.1f%% | %.1f\xC2\xB0" "C", mEngine.getCpuLoad() * 100.0f, initCpuTemp);
    } else {
        lv_label_set_text_fmt(mCpuLoadLabel, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
    }
    lv_obj_set_style_text_font(mCpuLoadLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mCpuLoadLabel, lv_color_hex(0x00FFCC), 0);

    mIpAddressLbl = lv_label_create(statusCont);
    std::string ip = getLocalIPAddress();
    lv_label_set_text_fmt(mIpAddressLbl, "IP: %s", ip.c_str());
    lv_obj_set_style_text_font(mIpAddressLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mIpAddressLbl, lv_color_hex(0x00FFCC), 0);

    lv_obj_t* versionLbl = lv_label_create(statusCont);
    lv_label_set_text(versionLbl, "Version: v3.1.25 (10\" Edition)");
    lv_obj_set_style_text_font(versionLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(versionLbl, lv_color_hex(0xAAAAAA), 0);

    mSettingsUpdateStatus = lv_label_create(statusCont);
    lv_label_set_text(mSettingsUpdateStatus, "Status: Idle");
    lv_obj_set_style_text_font(mSettingsUpdateStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mSettingsUpdateStatus, lv_color_hex(0xAAAAAA), 0);

    // Item 4: Section 2 Divider
    makeSectionDivider(perfCard, "ACTIONS & POWER");

    // Item 5: Check & Update Button
    lv_obj_t* updateBtn = lv_button_create(perfCard);
    lv_obj_set_size(updateBtn, 232, 38);
    lv_obj_set_style_bg_color(updateBtn, trackColor, 0);
    lv_obj_set_style_radius(updateBtn, 8, 0);
    lv_obj_t* updateBtnLbl = lv_label_create(updateBtn);
    lv_label_set_text(updateBtnLbl, "CHECK & UPDATE");
    lv_obj_set_style_text_font(updateBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(updateBtnLbl);
    lv_obj_add_event_cb(updateBtn, settingsUpdateBtnEventCb, LV_EVENT_CLICKED, this);

    // Item 6: Restart Loom Button
    lv_obj_t* restartBtn = lv_button_create(perfCard);
    lv_obj_set_size(restartBtn, 232, 38);
    lv_obj_set_style_bg_color(restartBtn, lv_color_hex(0xE06C75), 0);
    lv_obj_set_style_radius(restartBtn, 8, 0);
    lv_obj_t* restartBtnLbl = lv_label_create(restartBtn);
    lv_label_set_text(restartBtnLbl, "RESTART LOOM");
    lv_obj_set_style_text_font(restartBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(restartBtnLbl);
    lv_obj_add_event_cb(restartBtn, settingsRestartBtnEventCb, LV_EVENT_CLICKED, this);

    // Item 7: Renew IP Address Button
    lv_obj_t* renewNetBtn = lv_button_create(perfCard);
    lv_obj_set_size(renewNetBtn, 232, 38);
    lv_obj_set_style_bg_color(renewNetBtn, lv_color_hex(0x28A745), 0);
    lv_obj_set_style_radius(renewNetBtn, 8, 0);
    lv_obj_t* renewNetLbl = lv_label_create(renewNetBtn);
    lv_label_set_text(renewNetLbl, "RENEW IP ADDRESS");
    lv_obj_set_style_text_font(renewNetLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(renewNetLbl);
    lv_obj_add_event_cb(renewNetBtn, [](lv_event_t* e) {
        int r = system("sudo dhcpcd -n wlan0 2>/dev/null || sudo dhcpcd -n eth0 2>/dev/null || "
                       "sudo systemctl restart dhcpcd 2>/dev/null || "
                       "sudo systemctl restart NetworkManager 2>/dev/null");
        (void)r;
    }, LV_EVENT_CLICKED, this);

    // Item 8: Exit to Console Button
    lv_obj_t* exitConsoleBtn = lv_button_create(perfCard);
    lv_obj_set_size(exitConsoleBtn, 232, 38);
    lv_obj_set_style_bg_color(exitConsoleBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(exitConsoleBtn, 8, 0);
    lv_obj_t* exitConsoleLbl = lv_label_create(exitConsoleBtn);
    lv_label_set_text(exitConsoleLbl, "EXIT TO CONSOLE");
    lv_obj_set_style_text_font(exitConsoleLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(exitConsoleLbl);
    lv_obj_add_event_cb(exitConsoleBtn, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->openConsoleModal();
    }, LV_EVENT_CLICKED, this);

    // Item 9: Reboot Pi Button
    lv_obj_t* rebootBtn = lv_button_create(perfCard);
    lv_obj_set_size(rebootBtn, 232, 38);
    lv_obj_set_style_bg_color(rebootBtn, lv_color_hex(0xCC6600), 0);
    lv_obj_set_style_radius(rebootBtn, 8, 0);
    lv_obj_t* rebootLbl = lv_label_create(rebootBtn);
    lv_label_set_text(rebootLbl, LV_SYMBOL_REFRESH " REBOOT PI");
    lv_obj_set_style_text_font(rebootLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(rebootLbl);
    lv_obj_add_event_cb(rebootBtn, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->showConfirmationModal(
            "REBOOT SYSTEM",
            "Are you sure you want to reboot the Raspberry Pi?\nAny unsaved changes will be lost.",
            "REBOOT",
            lv_color_hex(0xCC6600),
            []() {
                HardwareDisplay::rebootSystem();
            }
        );
    }, LV_EVENT_CLICKED, this);

    // Item 10: Shutdown Pi Button
    lv_obj_t* shutdownBtn = lv_button_create(perfCard);
    lv_obj_set_size(shutdownBtn, 232, 38);
    lv_obj_set_style_bg_color(shutdownBtn, lv_color_hex(0x8B0000), 0);
    lv_obj_set_style_radius(shutdownBtn, 8, 0);
    lv_obj_t* shutdownLbl = lv_label_create(shutdownBtn);
    lv_label_set_text(shutdownLbl, LV_SYMBOL_POWER " SHUT DOWN PI");
    lv_obj_set_style_text_font(shutdownLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(shutdownLbl);
    lv_obj_add_event_cb(shutdownBtn, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->showConfirmationModal(
            "SHUT DOWN SYSTEM",
            "Are you sure you want to safely power off the Raspberry Pi?\nAny unsaved changes will be lost.",
            "SHUT DOWN",
            lv_color_hex(0x8B0000),
            []() {
                HardwareDisplay::shutdownSystem();
            }
        );
    }, LV_EVENT_CLICKED, this);
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

std::string UIManager::getDrumRowNoteLabel(int targetTrack, int note) {
    if (targetTrack < 0 || targetTrack >= 8) return "";
    int engType = mEngine.getTracks()[targetTrack].engineType;

    if (engType == 5) { // FM Drum (notes 60-67)
        if (note >= 60 && note <= 67) {
            int idx = note - 60;
            if (idx >= 0 && idx < 8) return kFmDrumNames[idx];
        }
    } else if (engType == 6) { // Analogue Drum
        if (note == 35 || note == 36) return "KICK";
        if (note == 38 || note == 40) return "SNARE";
        if (note == 39) return "CLAP";
        if (note == 42) return "HIHAT CL";
        if (note == 46) return "HIHAT OP";
        if (note == 49 || note == 51) return "CYMBAL";
        if (note >= 60 && note <= 67) {
            int idx = note - 60;
            if (idx >= 0 && idx < 8) return kAnalogDrumNames[idx];
        }
    } else if (engType == 2 && mEngine.getTracks()[targetTrack].samplerEngine.isChopMode()) {
        // Sampler Chops
        int numSlices = (int)mEngine.getSamplerSlicePoints(targetTrack).size();
        if (note >= 60 && (note - 60) < numSlices) {
            return "SmpSlc " + std::to_string((note - 60) + 1);
        }
    } else if (engType == 9) { // SoundFont
        int presetCount = mEngine.getSoundFontPresetCount(targetTrack);
        if (presetCount > 0 && note >= 60 && (note - 60) < presetCount) {
            std::string pName = mEngine.getSoundFontPresetName(targetTrack, note - 60);
            if (!pName.empty()) return pName;
        }
        // General MIDI Drum mapping for standard drum notes
        if (note == 35 || note == 36) return "Bass Drum";
        if (note == 37) return "Side Stick";
        if (note == 38 || note == 40) return "Snare";
        if (note == 39) return "Hand Clap";
        if (note == 41) return "Low Floor Tom";
        if (note == 42) return "Closed Hi-Hat";
        if (note == 43) return "High Floor Tom";
        if (note == 44) return "Pedal Hi-Hat";
        if (note == 45) return "Low Tom";
        if (note == 46) return "Open Hi-Hat";
        if (note == 47) return "Low-Mid Tom";
        if (note == 48) return "Hi-Mid Tom";
        if (note == 49) return "Crash Cymbal 1";
        if (note == 50) return "High Tom";
        if (note == 51) return "Ride Cymbal 1";
        if (note == 52) return "Chinese Cymbal";
        if (note == 53) return "Ride Bell";
        if (note == 54) return "Tambourine";
        if (note == 55) return "Splash Cymbal";
        if (note == 56) return "Cowbell";
        if (note == 57) return "Crash Cymbal 2";
    }

    return "";
}

std::string UIManager::buildDrumRowNoteOptions(int targetTrack) {
    std::string opt;
    for (int n = 20; n <= 120; ++n) {
        int oct = (n / 12) - 1;
        int idx = n % 12;
        std::string notePitch = std::string(kNoteNames[idx]) + std::to_string(oct);
        std::string inst = getDrumRowNoteLabel(targetTrack, n);
        if (!inst.empty()) {
            opt += std::to_string(n) + ": " + inst + " (" + notePitch + ")";
        } else {
            opt += std::to_string(n) + " (" + notePitch + ")";
        }
        if (n < 120) opt += "\n";
    }
    return opt;
}

void UIManager::updateDrumRowScrubLabel(int keyIdx) {
    if (keyIdx < 0 || keyIdx >= 8 || !mDrumRowScrubLbl[keyIdx]) return;
    int note = mDrumRowNotes[keyIdx];
    std::string inst = getDrumRowNoteLabel(mDrumRowTargetTrack, note);
    if (!inst.empty()) {
        std::string shortLabel = inst;
        if (shortLabel.rfind("SmpSlc ", 0) == 0) {
            shortLabel = "Slc" + shortLabel.substr(7);
        } else if (shortLabel == "HIHAT CL") {
            shortLabel = "HH CL";
        } else if (shortLabel == "HIHAT OP") {
            shortLabel = "HH OP";
        } else if (shortLabel == "Crash Cymbal 1" || shortLabel == "Crash Cymbal 2") {
            shortLabel = "Crash";
        } else if (shortLabel == "Ride Cymbal 1") {
            shortLabel = "Ride";
        } else if (shortLabel == "Closed Hi-Hat") {
            shortLabel = "HH Cl";
        } else if (shortLabel == "Open Hi-Hat") {
            shortLabel = "HH Op";
        } else if (shortLabel == "Bass Drum") {
            shortLabel = "Kick";
        } else if (shortLabel.length() > 5) {
            shortLabel = shortLabel.substr(0, 5);
        }
        lv_label_set_text(mDrumRowScrubLbl[keyIdx], shortLabel.c_str());
    } else {
        int oct = (note / 12) - 1;
        std::string notePitch = std::string(kNoteNames[note % 12]) + std::to_string(oct);
        lv_label_set_text(mDrumRowScrubLbl[keyIdx], notePitch.c_str());
    }
}

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
    lv_dropdown_set_options(padCountDd, "4\n8\n12\n16\n20\n24\n28\n32\n36\n40");
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

    // --- Content Row: Left (Square Pads Grid) + Right (Drum Number Row 1-8) ---
    lv_obj_t* contentRow = lv_obj_create(tab);
    lv_obj_set_size(contentRow, lv_pct(100), 665);
    lv_obj_set_style_bg_opa(contentRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentRow, 0, 0);
    lv_obj_set_style_pad_all(contentRow, 0, 0);
    lv_obj_set_layout(contentRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(contentRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(contentRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(contentRow, 14, 0);
    lv_obj_remove_flag(contentRow, LV_OBJ_FLAG_SCROLLABLE);

    // Left container for Pad Grid and bottom buttons
    lv_obj_t* padGridWrapper = lv_obj_create(contentRow);
    lv_obj_set_size(padGridWrapper, 580, 660);
    lv_obj_set_style_bg_opa(padGridWrapper, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(padGridWrapper, 0, 0);
    lv_obj_set_style_pad_all(padGridWrapper, 0, 0);
    lv_obj_remove_flag(padGridWrapper, LV_OBJ_FLAG_SCROLLABLE);

    mSettingsPadGrid = lv_obj_create(padGridWrapper);
    lv_obj_set_size(mSettingsPadGrid, 580, 565);
    lv_obj_align(mSettingsPadGrid, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(mSettingsPadGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mSettingsPadGrid, 0, 0);
    lv_obj_set_style_pad_all(mSettingsPadGrid, 0, 0);
    lv_obj_remove_flag(mSettingsPadGrid, LV_OBJ_FLAG_SCROLLABLE);

    // Floating Pads Wizard Button (anchored at bottom left of pad area)
    lv_obj_t* padsWizardBtn = lv_button_create(padGridWrapper);
    lv_obj_set_size(padsWizardBtn, 140, 36);
    lv_obj_align(padsWizardBtn, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    lv_obj_set_style_bg_color(padsWizardBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(padsWizardBtn, 6, 0);
    lv_obj_t* padsWizardLbl = lv_label_create(padsWizardBtn);
    lv_label_set_text(padsWizardLbl, "PADS WIZARD");
    lv_obj_set_style_text_font(padsWizardLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(padsWizardLbl);
    
    auto padsWizardClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->openWizard(1); // 1 = MIDI Pads
    };
    lv_obj_add_event_cb(padsWizardBtn, padsWizardClickCb, LV_EVENT_CLICKED, this);

    // Pad Learn Button (next to wizard button)
    lv_obj_t* padLearnBtn = lv_button_create(padGridWrapper);
    lv_obj_set_size(padLearnBtn, 150, 36);
    lv_obj_align(padLearnBtn, LV_ALIGN_BOTTOM_LEFT, 160, -8);
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

    // -------------------------------------------------------------------------
    // RIGHT PANEL: DRUM NUMBER ROW 1-8 (Hardware Custom Keyboard & Drum Trigger)
    // -------------------------------------------------------------------------
    lv_obj_t* drumRowPanel = lv_obj_create(contentRow);
    lv_obj_set_flex_grow(drumRowPanel, 1);
    lv_obj_set_height(drumRowPanel, 660);
    lv_obj_set_style_bg_color(drumRowPanel, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(drumRowPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(drumRowPanel, lv_color_hex(0x2E2E2E), 0);
    lv_obj_set_style_border_width(drumRowPanel, 1, 0);
    lv_obj_set_style_radius(drumRowPanel, 8, 0);
    lv_obj_set_style_pad_all(drumRowPanel, 8, 0);
    lv_obj_set_layout(drumRowPanel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(drumRowPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(drumRowPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(drumRowPanel, 4, 0);
    lv_obj_remove_flag(drumRowPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* drumRowTitle = lv_label_create(drumRowPanel);
    lv_label_set_text(drumRowTitle, "DRUM NUMBER ROW 1-8");
    lv_obj_set_style_text_font(drumRowTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(drumRowTitle, trackColor, 0);

    // Target Track Selector Row with MIDI Learn Button
    lv_obj_t* trkRow = lv_obj_create(drumRowPanel);
    lv_obj_set_size(trkRow, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(trkRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trkRow, 0, 0);
    lv_obj_set_style_pad_all(trkRow, 0, 0);
    lv_obj_set_layout(trkRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(trkRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trkRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(trkRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* trkLeft = lv_obj_create(trkRow);
    lv_obj_set_size(trkLeft, 250, 36);
    lv_obj_set_style_bg_opa(trkLeft, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trkLeft, 0, 0);
    lv_obj_set_style_pad_all(trkLeft, 0, 0);
    lv_obj_set_layout(trkLeft, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(trkLeft, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trkLeft, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(trkLeft, 8, 0);
    lv_obj_remove_flag(trkLeft, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* trkLbl = lv_label_create(trkLeft);
    lv_label_set_text(trkLbl, "Target Track:");
    lv_obj_set_style_text_font(trkLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(trkLbl, lv_color_hex(0xCCCCCC), 0);

    mDrumRowTrackDd = lv_dropdown_create(trkLeft);
    lv_obj_set_size(mDrumRowTrackDd, 130, 32);
    lv_dropdown_set_options(mDrumRowTrackDd, "Track 1\nTrack 2\nTrack 3\nTrack 4\nTrack 5\nTrack 6\nTrack 7\nTrack 8");
    lv_dropdown_set_selected(mDrumRowTrackDd, mDrumRowTargetTrack);
    lv_obj_set_style_text_font(mDrumRowTrackDd, &lv_font_montserrat_10, 0);
    lv_obj_add_event_cb(mDrumRowTrackDd, drumRowTrackDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // MIDI Learn button for Drum Row knobs
    mDrumRowLearnBtn = lv_button_create(trkRow);
    lv_obj_set_size(mDrumRowLearnBtn, 130, 32);
    if (mDrumRowLearnActive) {
        lv_obj_set_style_bg_color(mDrumRowLearnBtn, lv_color_hex(0xD32F2F), 0);
        lv_obj_set_style_border_color(mDrumRowLearnBtn, lv_color_hex(0xFF5252), 0);
    } else {
        lv_obj_set_style_bg_color(mDrumRowLearnBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(mDrumRowLearnBtn, lv_color_hex(0x555555), 0);
    }
    lv_obj_set_style_border_width(mDrumRowLearnBtn, 1, 0);
    lv_obj_set_style_radius(mDrumRowLearnBtn, 6, 0);
    mDrumRowLearnBtnLbl = lv_label_create(mDrumRowLearnBtn);
    lv_label_set_text(mDrumRowLearnBtnLbl, mDrumRowLearnActive ? (mDrumRowLearnTargetKey >= 0 ? "TAP & WIGGLE" : "TAP KNOB") : "MIDI LEARN");
    lv_obj_set_style_text_font(mDrumRowLearnBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mDrumRowLearnBtnLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(mDrumRowLearnBtnLbl);
    lv_obj_add_event_cb(mDrumRowLearnBtn, drumRowLearnBtnEventCb, LV_EVENT_CLICKED, this);

    // Sub-header explaining columns: Key | Note Value (20-120) | Scrub / Learn | Ratchet
    lv_obj_t* subHdr = lv_obj_create(drumRowPanel);
    lv_obj_set_size(subHdr, lv_pct(100), 20);
    lv_obj_set_style_bg_opa(subHdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(subHdr, 0, 0);
    lv_obj_set_style_pad_all(subHdr, 0, 0);
    lv_obj_set_layout(subHdr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(subHdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(subHdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(subHdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdrKey = lv_label_create(subHdr);
    lv_label_set_text(hdrKey, "Key");
    lv_obj_set_style_text_font(hdrKey, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hdrKey, lv_color_hex(0x888888), 0);

    lv_obj_t* hdrNote = lv_label_create(subHdr);
    lv_label_set_text(hdrNote, "Assigned Note");
    lv_obj_set_style_text_font(hdrNote, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hdrNote, lv_color_hex(0x888888), 0);

    lv_obj_t* hdrScrub = lv_label_create(subHdr);
    lv_label_set_text(hdrScrub, "Scrub / Learn");
    lv_obj_set_style_text_font(hdrScrub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hdrScrub, lv_color_hex(0x888888), 0);

    lv_obj_t* hdrRatch = lv_label_create(subHdr);
    lv_label_set_text(hdrRatch, "Ratchet");
    lv_obj_set_style_text_font(hdrRatch, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hdrRatch, lv_color_hex(0x888888), 0);

    std::string noteOptions = buildDrumRowNoteOptions(mDrumRowTargetTrack);

    for (int i = 0; i < 8; ++i) {
        lv_obj_t* row = lv_obj_create(drumRowPanel);
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1F1F1F), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2E2E2E), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_hor(row, 6, 0);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // Key badge
        lv_obj_t* badge = lv_obj_create(row);
        lv_obj_set_size(badge, 28, 28);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x141414), 0);
        lv_obj_set_style_border_color(badge, trackColor, 0);
        lv_obj_set_style_border_width(badge, 1, 0);
        lv_obj_set_style_radius(badge, 4, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* badgeLbl = lv_label_create(badge);
        lv_label_set_text_fmt(badgeLbl, "%d", i + 1);
        lv_obj_set_style_text_font(badgeLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(badgeLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(badgeLbl);

        // Note Dropdown (20 to 120)
        mDrumRowNoteDd[i] = lv_dropdown_create(row);
        lv_obj_set_size(mDrumRowNoteDd[i], 135, 34);
        lv_dropdown_set_options(mDrumRowNoteDd[i], noteOptions.c_str());
        int selIdx = std::max(0, std::min(100, mDrumRowNotes[i] - 20));
        lv_dropdown_set_selected(mDrumRowNoteDd[i], selIdx);
        lv_obj_set_style_text_font(mDrumRowNoteDd[i], &lv_font_montserrat_10, 0);
        lv_obj_set_user_data(mDrumRowNoteDd[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(mDrumRowNoteDd[i], drumRowNoteDdEventCb, LV_EVENT_VALUE_CHANGED, this);

        // Scrub Arc Knob
        lv_obj_t* scrubArc = lv_arc_create(row);
        mDrumRowScrubArc[i] = scrubArc;
        lv_obj_set_size(scrubArc, 40, 40);
        lv_arc_set_range(scrubArc, 20, 120);
        lv_arc_set_rotation(scrubArc, 135);
        lv_arc_set_bg_angles(scrubArc, 0, 270);
        lv_arc_set_value(scrubArc, mDrumRowNotes[i]);
        lv_obj_set_style_bg_opa(scrubArc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(scrubArc, 0, LV_PART_KNOB);
        lv_obj_set_style_pad_all(scrubArc, 0, LV_PART_KNOB);
        lv_obj_set_style_arc_width(scrubArc, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(scrubArc, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(scrubArc, lv_color_hex(0x2D2D2D), LV_PART_MAIN);
        bool isThisLearning = (mDrumRowLearnActive && mDrumRowLearnTargetKey == i);
        lv_obj_set_style_arc_color(scrubArc, isThisLearning ? lv_color_hex(0xFF3333) : trackColor, LV_PART_INDICATOR);

        mDrumRowScrubLbl[i] = lv_label_create(scrubArc);
        lv_obj_set_style_text_font(mDrumRowScrubLbl[i], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(mDrumRowScrubLbl[i], lv_color_hex(0xCCCCCC), 0);
        lv_obj_center(mDrumRowScrubLbl[i]);
        updateDrumRowScrubLabel(i);

        lv_obj_set_user_data(scrubArc, (void*)(intptr_t)i);
        lv_obj_add_event_cb(scrubArc, drumRowScrubArcEventCb, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(scrubArc, drumRowScrubArcEventCb, LV_EVENT_CLICKED, this);

        // Ratchet Button
        mDrumRowRatchetBtn[i] = lv_button_create(row);
        lv_obj_set_size(mDrumRowRatchetBtn[i], 52, 34);
        int r = mDrumRowRatchets[i];
        if (r > 1) {
            lv_obj_set_style_bg_color(mDrumRowRatchetBtn[i], trackColor, 0);
        } else {
            lv_obj_set_style_bg_color(mDrumRowRatchetBtn[i], lv_color_hex(0x2D2D2D), 0);
        }
        lv_obj_set_style_radius(mDrumRowRatchetBtn[i], 6, 0);
        lv_obj_t* ratchLbl = lv_label_create(mDrumRowRatchetBtn[i]);
        lv_label_set_text_fmt(ratchLbl, "%dx", r);
        lv_obj_set_style_text_font(ratchLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(ratchLbl);
        lv_obj_set_user_data(mDrumRowRatchetBtn[i], (void*)(intptr_t)i);
        lv_obj_add_event_cb(mDrumRowRatchetBtn[i], drumRowRatchetBtnEventCb, LV_EVENT_CLICKED, this);
    }

    rebuildPadGrid();
}

void UIManager::rebuildPadGrid() {
    if (!mSettingsPadGrid) return;
    lv_obj_clean(mSettingsPadGrid);

    lv_color_t trackColor = getTrackColor(mActiveTrack);
    int cols = 4;
    int rows = (mSettingsPadCount + cols - 1) / cols;
    int gapX = 10;
    int gapY = 10;
    int padW = 132;
    int padH = (rows <= 4) ? 132 : std::max(40, (565 - (rows - 1) * gapY) / rows);
    if (rows > 4) {
        padW = padH; // keep square if more rows
    }
    int totalGridW = cols * padW + (cols - 1) * gapX;
    int startX = std::max(0, (580 - totalGridW) / 2);

    for (int i = 0; i < mSettingsPadCount; ++i) {
        int r = (mSettingsPadMode == 5) ? (i / cols) : (rows - 1 - (i / cols));
        int c = i % cols;
        int x = startX + c * (padW + gapX);
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
    lv_dropdown_set_options(knobCountDd, "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40");
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
    lv_dropdown_set_options(sliderCountDd, "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40");
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
    lv_obj_set_size(tableContainer, lv_pct(100), 610);
    lv_obj_set_style_bg_color(tableContainer, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(tableContainer, LV_OPA_80, 0);
    lv_obj_set_style_border_color(tableContainer, trackColor, 0);
    lv_obj_set_style_border_width(tableContainer, 1, 0);
    lv_obj_set_style_radius(tableContainer, 10, 0);
    lv_obj_set_style_pad_all(tableContainer, 12, 0);
    lv_obj_set_layout(tableContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tableContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tableContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(tableContainer, 16, 0);
    lv_obj_set_style_pad_row(tableContainer, 8, 0);

    // Title label for mappings (Make it spans full width by setting width pct 100)
    lv_obj_t* mappingTitle = lv_label_create(tableContainer);
    lv_obj_set_width(mappingTitle, lv_pct(100));
    lv_label_set_text(mappingTitle, "CUSTOM HARDWARE MIDI CC ASSIGNMENTS");
    lv_obj_set_style_text_font(mappingTitle, &lv_font_montserrat_12, 0);
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
        lv_obj_set_size(row, 490, 46);
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
        lv_obj_set_size(ccDd, 130, 36);
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
                for (int t = 0; t < 8; ++t) {
                    d->ui->mSeqMidiKnobCC[t][d->idx] = selected;
                }
            } else {
                for (int t = 0; t < 8; ++t) {
                    d->ui->mSeqMidiFaderCC[t][d->idx] = selected;
                }
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
        lv_obj_set_size(row, 490, 46);
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
        lv_obj_set_size(ccDd, 130, 36);
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
                for (int t = 0; t < 8; ++t) {
                    d->ui->mSeqMidiKnobCC[t][d->idx] = selected;
                }
            } else {
                for (int t = 0; t < 8; ++t) {
                    d->ui->mSeqMidiFaderCC[t][d->idx] = selected;
                }
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
    lv_obj_set_style_text_font(transTitle, &lv_font_montserrat_12, 0);
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
        lv_obj_set_size(row, 490, 46);
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
        lv_obj_set_size(ccDd, 130, 36);
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

    // Floating Hardware Wizard Button in the bottom right
    lv_obj_t* wizardBtn = lv_button_create(tab);
    lv_obj_set_size(wizardBtn, 150, 40);
    lv_obj_add_flag(wizardBtn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(wizardBtn, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(wizardBtn, trackColor, 0);
    lv_obj_set_style_radius(wizardBtn, 6, 0);
    lv_obj_t* wizardLbl = lv_label_create(wizardBtn);
    lv_label_set_text(wizardLbl, "HARDWARE WIZARD");
    lv_obj_set_style_text_font(wizardLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(wizardLbl);
    
    auto wizardClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->openWizard(0); // 0 = Knobs/Sliders
    };
    lv_obj_add_event_cb(wizardBtn, wizardClickCb, LV_EVENT_CLICKED, this);
}

void UIManager::populateSettingsUsbMidiTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_t* headerRow = lv_obj_create(tab);
    lv_obj_set_size(headerRow, 750, 50);
    lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(headerRow, 0, 0);
    lv_obj_set_style_pad_all(headerRow, 0, 0);
    lv_obj_set_layout(headerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* title = lv_label_create(headerRow);
    lv_label_set_text(title, "USB MIDI HARDWARE ROUTING");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

    lv_obj_t* scanBtn = lv_button_create(headerRow);
    lv_obj_set_size(scanBtn, 120, 34);
    lv_obj_set_style_bg_color(scanBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* scanBtnLbl = lv_label_create(scanBtn);
    lv_label_set_text(scanBtnLbl, "SCAN DEVICES");
    lv_obj_set_style_text_font(scanBtnLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(scanBtnLbl);

    lv_obj_add_event_cb(scanBtn, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->mEngine.scanMidiDevices();
        ui->mSettingsActiveTabIdx = 3;
        ui->createCenterContentArea();
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* listContainer = lv_obj_create(tab);
    lv_obj_set_size(listContainer, 750, 390);
    lv_obj_align(listContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listContainer, 0, 0);
    lv_obj_set_style_pad_all(listContainer, 5, 0);
    lv_obj_set_layout(listContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(listContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(listContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(listContainer, 10, 0);

    mEngine.scanMidiDevices();

    if (mEngine.mMidiDevices.empty()) {
        lv_obj_t* emptyLbl = lv_label_create(listContainer);
        lv_label_set_text(emptyLbl, "No external USB MIDI devices detected.\nPlug in a keyboard, pad controller, or synthesizer and click SCAN.");
        lv_obj_set_style_text_font(emptyLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(emptyLbl, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_align(emptyLbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(emptyLbl, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    std::string chOptions = "ALL\nOFF\n";
    for (int c = 1; c <= 16; ++c) {
        chOptions += "Ch " + std::to_string(c) + "\n";
    }
    if (!chOptions.empty()) chOptions.pop_back();

    for (size_t i = 0; i < mEngine.mMidiDevices.size(); ++i) {
        auto& dev = mEngine.mMidiDevices[i];

        lv_obj_t* row = lv_obj_create(listContainer);
        lv_obj_set_size(row, 720, 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* nameLbl = lv_label_create(row);
        lv_label_set_text(nameLbl, dev.name.c_str());
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(nameLbl, 220);

        lv_obj_t* sendCol = lv_obj_create(row);
        lv_obj_set_size(sendCol, 100, 36);
        lv_obj_set_style_bg_opa(sendCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sendCol, 0, 0);
        lv_obj_set_layout(sendCol, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(sendCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sendCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* sendLbl = lv_label_create(sendCol);
        lv_label_set_text(sendLbl, "SEND CH");
        lv_obj_set_style_text_font(sendLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sendLbl, lv_color_hex(0x888888), 0);
        
        lv_obj_t* sendDd = lv_dropdown_create(sendCol);
        lv_obj_set_size(sendDd, 90, 24);
        lv_obj_set_style_text_font(sendDd, &lv_font_montserrat_10, 0);
        lv_dropdown_set_options(sendDd, chOptions.c_str());
        lv_dropdown_set_selected(sendDd, dev.sendChannel + 1);

        struct ChannelUserData {
            MidiDeviceSettings* devPtr;
            bool isSend;
        };
        ChannelUserData* cud = new ChannelUserData{&dev, true};
        lv_obj_add_event_cb(sendDd, [](lv_event_t* e) {
            ChannelUserData* data = (ChannelUserData*)lv_event_get_user_data(e);
            lv_obj_t* ddObj = lv_event_get_target_obj(e);
            int selected = lv_dropdown_get_selected(ddObj);
            data->devPtr->sendChannel = selected - 1;
            std::cout << "Midi Device '" << data->devPtr->name << "' send channel set to " << data->devPtr->sendChannel << std::endl;
        }, LV_EVENT_VALUE_CHANGED, cud);
        lv_obj_add_event_cb(sendDd, [](lv_event_t* e) {
            ChannelUserData* data = (ChannelUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, cud);

        lv_obj_t* recvCol = lv_obj_create(row);
        lv_obj_set_size(recvCol, 100, 36);
        lv_obj_set_style_bg_opa(recvCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(recvCol, 0, 0);
        lv_obj_set_layout(recvCol, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(recvCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(recvCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        lv_obj_t* recvLbl = lv_label_create(recvCol);
        lv_label_set_text(recvLbl, "RECV CH");
        lv_obj_set_style_text_font(recvLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(recvLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* recvDd = lv_dropdown_create(recvCol);
        lv_obj_set_size(recvDd, 90, 24);
        lv_obj_set_style_text_font(recvDd, &lv_font_montserrat_10, 0);
        lv_dropdown_set_options(recvDd, chOptions.c_str());
        lv_dropdown_set_selected(recvDd, dev.receiveChannel + 1);

        ChannelUserData* cud2 = new ChannelUserData{&dev, false};
        lv_obj_add_event_cb(recvDd, [](lv_event_t* e) {
            ChannelUserData* data = (ChannelUserData*)lv_event_get_user_data(e);
            lv_obj_t* ddObj = lv_event_get_target_obj(e);
            int selected = lv_dropdown_get_selected(ddObj);
            data->devPtr->receiveChannel = selected - 1;
            std::cout << "Midi Device '" << data->devPtr->name << "' receive channel set to " << data->devPtr->receiveChannel << std::endl;
        }, LV_EVENT_VALUE_CHANGED, cud2);
        lv_obj_add_event_cb(recvDd, [](lv_event_t* e) {
            ChannelUserData* data = (ChannelUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, cud2);

        lv_obj_t* muteCol = lv_obj_create(row);
        lv_obj_set_size(muteCol, 100, 36);
        lv_obj_set_style_bg_opa(muteCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(muteCol, 0, 0);
        lv_obj_set_layout(muteCol, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(muteCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(muteCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* muteLbl = lv_label_create(muteCol);
        lv_label_set_text(muteLbl, "MUTE OUT");
        lv_obj_set_style_text_font(muteLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(muteLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* muteSw = lv_switch_create(muteCol);
        lv_obj_set_size(muteSw, 40, 20);
        if (dev.muteOutgoing) {
            lv_obj_add_state(muteSw, LV_STATE_CHECKED);
        }
        
        struct ToggleUserData {
            MidiDeviceSettings* devPtr;
            bool isMute;
        };
        ToggleUserData* tud = new ToggleUserData{&dev, true};
        lv_obj_add_event_cb(muteSw, [](lv_event_t* e) {
            ToggleUserData* data = (ToggleUserData*)lv_event_get_user_data(e);
            lv_obj_t* swObj = lv_event_get_target_obj(e);
            data->devPtr->muteOutgoing = lv_obj_has_state(swObj, LV_STATE_CHECKED);
            std::cout << "Midi Device '" << data->devPtr->name << "' mute outgoing set to " << data->devPtr->muteOutgoing << std::endl;
        }, LV_EVENT_VALUE_CHANGED, tud);
        lv_obj_add_event_cb(muteSw, [](lv_event_t* e) {
            ToggleUserData* data = (ToggleUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, tud);

        lv_obj_t* ignoreCol = lv_obj_create(row);
        lv_obj_set_size(ignoreCol, 100, 36);
        lv_obj_set_style_bg_opa(ignoreCol, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ignoreCol, 0, 0);
        lv_obj_set_layout(ignoreCol, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(ignoreCol, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(ignoreCol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* ignoreLbl = lv_label_create(ignoreCol);
        lv_label_set_text(ignoreLbl, "IGNORE IN");
        lv_obj_set_style_text_font(ignoreLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ignoreLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* ignoreSw = lv_switch_create(ignoreCol);
        lv_obj_set_size(ignoreSw, 40, 20);
        if (dev.ignoreIncoming) {
            lv_obj_add_state(ignoreSw, LV_STATE_CHECKED);
        }

        ToggleUserData* tud2 = new ToggleUserData{&dev, false};
        lv_obj_add_event_cb(ignoreSw, [](lv_event_t* e) {
            ToggleUserData* data = (ToggleUserData*)lv_event_get_user_data(e);
            lv_obj_t* swObj = lv_event_get_target_obj(e);
            data->devPtr->ignoreIncoming = lv_obj_has_state(swObj, LV_STATE_CHECKED);
            std::cout << "Midi Device '" << data->devPtr->name << "' ignore incoming set to " << data->devPtr->ignoreIncoming << std::endl;
        }, LV_EVENT_VALUE_CHANGED, tud2);
        lv_obj_add_event_cb(ignoreSw, [](lv_event_t* e) {
            ToggleUserData* data = (ToggleUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, tud2);
    }
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
    int counts[] = {4, 8, 12, 16, 20, 24, 28, 32, 36, 40};
    ui->mSettingsPadCount = counts[sel];
    ui->rebuildPadGrid();
}

void UIManager::drumRowTrackDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    ui->mDrumRowTargetTrack = lv_dropdown_get_selected(dd);
    // Update button colors for ratchet > 1
    for (int i = 0; i < 8; ++i) {
        if (ui->mDrumRowRatchetBtn[i]) {
            if (ui->mDrumRowRatchets[i] > 1) {
                lv_obj_set_style_bg_color(ui->mDrumRowRatchetBtn[i], ui->getTrackColor(ui->mDrumRowTargetTrack), 0);
            }
        }
    }
    // Update note options for all 8 note dropdowns with new target track instruments
    std::string noteOptions = ui->buildDrumRowNoteOptions(ui->mDrumRowTargetTrack);
    for (int i = 0; i < 8; ++i) {
        if (ui->mDrumRowNoteDd[i]) {
            lv_dropdown_set_options(ui->mDrumRowNoteDd[i], noteOptions.c_str());
            lv_dropdown_set_selected(ui->mDrumRowNoteDd[i], ui->mDrumRowNotes[i] - 20);
        }
        ui->updateDrumRowScrubLabel(i);
        if (ui->mDrumRowScrubArc[i] && !ui->mDrumRowLearnActive) {
            lv_obj_set_style_arc_color(ui->mDrumRowScrubArc[i], ui->getTrackColor(ui->mDrumRowTargetTrack), LV_PART_INDICATOR);
        }
    }
}

void UIManager::drumRowNoteDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    int keyIdx = (int)(intptr_t)lv_obj_get_user_data(dd);
    if (keyIdx >= 0 && keyIdx < 8) {
        int sel = lv_dropdown_get_selected(dd);
        int note = 20 + sel;
        if (note != ui->mDrumRowNotes[keyIdx]) {
            int oldNote = ui->mDrumRowNotes[keyIdx];
            ui->mDrumRowNotes[keyIdx] = note;
            if (ui->mDrumRowScrubArc[keyIdx]) {
                lv_arc_set_value(ui->mDrumRowScrubArc[keyIdx], note);
            }
            ui->updateDrumRowScrubLabel(keyIdx);
            int trk = ui->mDrumRowTargetTrack;
            ui->mEngine.releaseNote(trk, oldNote);
            ui->mEngine.triggerNote(trk, note, 100);
        }
    }
}

void UIManager::drumRowScrubArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    int keyIdx = (int)(intptr_t)lv_obj_get_user_data(arc);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED || code == LV_EVENT_PRESSED) {
        if (ui->mDrumRowLearnActive) {
            ui->mDrumRowLearnTargetKey = keyIdx;
            ui->mMidiLearnActive = true;
            ui->mMidiLearnTargetParamId = 2410 + keyIdx;
            ui->mMidiLearnTargetTrack = ui->mDrumRowTargetTrack;
            if (ui->mDrumRowLearnBtnLbl) {
                lv_label_set_text_fmt(ui->mDrumRowLearnBtnLbl, "WIGGLE CC (KEY %d)", keyIdx + 1);
            }
            for (int k = 0; k < 8; ++k) {
                if (ui->mDrumRowScrubArc[k]) {
                    lv_obj_set_style_arc_color(ui->mDrumRowScrubArc[k],
                        (k == keyIdx) ? lv_color_hex(0xFF3333) : ui->getTrackColor(ui->mDrumRowTargetTrack),
                        LV_PART_INDICATOR);
                }
            }
            return;
        }
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        int note = (int)lv_arc_get_value(arc);
        note = std::max(20, std::min(120, note));
        if (keyIdx >= 0 && keyIdx < 8 && note != ui->mDrumRowNotes[keyIdx]) {
            int oldNote = ui->mDrumRowNotes[keyIdx];
            ui->mDrumRowNotes[keyIdx] = note;
            ui->updateDrumRowScrubLabel(keyIdx);
            if (ui->mDrumRowNoteDd[keyIdx]) {
                lv_dropdown_set_selected(ui->mDrumRowNoteDd[keyIdx], note - 20);
            }
            int trk = ui->mDrumRowTargetTrack;
            ui->mEngine.releaseNote(trk, oldNote);
            ui->mEngine.triggerNote(trk, note, 100);
        }
    }
}

void UIManager::drumRowLearnBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mDrumRowLearnActive = !ui->mDrumRowLearnActive;
    ui->mDrumRowLearnTargetKey = -1;
    if (!ui->mDrumRowLearnActive) {
        if (ui->mMidiLearnTargetParamId >= 2410 && ui->mMidiLearnTargetParamId <= 2417) {
            ui->mMidiLearnTargetParamId = -1;
            ui->mMidiLearnActive = false;
        }
        if (ui->mDrumRowLearnBtnLbl) {
            lv_label_set_text(ui->mDrumRowLearnBtnLbl, "MIDI LEARN");
        }
        if (ui->mDrumRowLearnBtn) {
            lv_obj_set_style_bg_color(ui->mDrumRowLearnBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_color(ui->mDrumRowLearnBtn, lv_color_hex(0x555555), 0);
        }
        for (int k = 0; k < 8; ++k) {
            if (ui->mDrumRowScrubArc[k]) {
                lv_obj_set_style_arc_color(ui->mDrumRowScrubArc[k], ui->getTrackColor(ui->mDrumRowTargetTrack), LV_PART_INDICATOR);
            }
        }
    } else {
        if (ui->mDrumRowLearnBtnLbl) {
            lv_label_set_text(ui->mDrumRowLearnBtnLbl, "TAP KNOB");
        }
        if (ui->mDrumRowLearnBtn) {
            lv_obj_set_style_bg_color(ui->mDrumRowLearnBtn, lv_color_hex(0xD32F2F), 0);
            lv_obj_set_style_border_color(ui->mDrumRowLearnBtn, lv_color_hex(0xFF5252), 0);
        }
    }
}

void UIManager::drumRowRatchetBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int keyIdx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (keyIdx >= 0 && keyIdx < 8) {
        int r = ui->mDrumRowRatchets[keyIdx];
        r = (r % 5) + 1; // 1 -> 2 -> 3 -> 4 -> 5 -> 1
        ui->mDrumRowRatchets[keyIdx] = r;
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_label_set_text_fmt(lbl, "%dx", r);
        }
        if (r > 1) {
            lv_obj_set_style_bg_color(btn, ui->getTrackColor(ui->mDrumRowTargetTrack), 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
        }
    }
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
    for (int i = 0; i < 40; ++i) ui->mSettingsPadFxToggleState[i] = false;
}

void UIManager::settingsPadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* pad = (lv_obj_t*)lv_event_get_target(e);
    int padIdx = (int)(intptr_t)lv_obj_get_user_data(pad);
    if (padIdx < 0 || padIdx >= 40) return;

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
        case 3: { // FM Drum - trigger voice
            int drumIdx = ui->mSettingsPadDrumAssign[padIdx] % 8;
            ui->mEngine.triggerNote(ui->mActiveTrack, 60 + drumIdx, 100);
            break;
        }
        case 4: { // Analogue Drum - trigger voice
            int drumIdx = ui->mSettingsPadDrumAssign[padIdx] % 8;
            ui->mEngine.triggerNote(ui->mActiveTrack, 60 + drumIdx, 100);
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
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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

    if (padIdx >= 0 && padIdx < 40 && fxIdx >= 0 && fxIdx < kNumFxSlots) {
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
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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
    lv_obj_set_flex_align(gridContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
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
    if (!ui) return;
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsProject = true;
    ui->mFileBrowserIsSave = true;
    ui->openFileBrowser(true);
    std::cout << "Settings: Save button pressed." << std::endl;
}

void UIManager::settingsNewBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    ui->mEngine.newProject();
    ui->createCenterContentArea();
    std::cout << "Settings: New project created." << std::endl;
}

void UIManager::settingsLoadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsProject = true;
    ui->mFileBrowserIsSave = false;
    ui->openFileBrowser(false);
    std::cout << "Settings: Load button pressed." << std::endl;
}

void UIManager::settingsCreditsBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);

    // Create full-screen overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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
#ifdef __linux__
    DIR* dir = opendir("/sys/class/backlight");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            std::string basePath = std::string("/sys/class/backlight/") + entry->d_name;
            std::ofstream pwrFile(basePath + "/bl_power");
            if (pwrFile.is_open()) {
                pwrFile << (on ? 0 : 1) << "\n";
            }
            break;
        }
        closedir(dir);
    }
#endif
    if (on) {
        std::system("vcgencmd display_power 1 2>/dev/null");
    } else {
        std::system("vcgencmd display_power 0 2>/dev/null");
    }
}

void UIManager::registerActivity() {
    mLastActivityTicks = SDL_GetTicks();
    if (mScreenIsSleeping) {
        setScreenSleep(false);
    }
}

void UIManager::setScreenSleep(bool sleep) {
    if (mScreenIsSleeping == sleep) return;
    mScreenIsSleeping = sleep;
    if (sleep) {
        if (!mSleepOverlay) {
            mSleepOverlay = lv_obj_create(lv_layer_top());
            lv_obj_set_size(mSleepOverlay, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(mSleepOverlay, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(mSleepOverlay, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(mSleepOverlay, 0, 0);
            lv_obj_clear_flag(mSleepOverlay, LV_OBJ_FLAG_SCROLLABLE);
        }
        HardwareDisplay::setBrightness(0);
        setBacklightPower(false);
    } else {
        if (mSleepOverlay) {
            lv_obj_delete(mSleepOverlay);
            mSleepOverlay = nullptr;
        }
        HardwareDisplay::setBrightness(mSettingsBacklightBrightness);
        setBacklightPower(true);
        mLastActivityTicks = SDL_GetTicks();
        lv_display_trigger_activity(nullptr);
    }
}

void UIManager::update() {
    if (mMidiWakeRequested) {
        mMidiWakeRequested = false;
        registerActivity();
    }

    if (mSettingsScreenTimeoutSec > 0 && !mScreenIsSleeping) {
        uint32_t now = SDL_GetTicks();
        if (now - mLastActivityTicks >= (uint32_t)(mSettingsScreenTimeoutSec * 1000)) {
            setScreenSleep(true);
        }
    }

    if (mStepModal && mEditingStepIdx >= 0) {
        int engineType = mEngine.getTracks()[mActiveTrack].engineType;
        bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
        bool isDrum = (engineType == 5 || engineType == 6 || isSamplerChops);
        
        std::vector<Step> currentSteps = isDrum ? mEngine.getDrumSequencerSteps(mActiveTrack, mActiveDrumIdx)
                                                : mEngine.getSequencerSteps(mActiveTrack);
        
        if (mEditingStepIdx < (int)currentSteps.size()) {
            const Step& stepObj = currentSteps[mEditingStepIdx];
            if (!stepObj.notes.empty()) {
                int noteVal = stepObj.notes[0].note;
                if (mStepModalNoteSlider && lv_slider_get_value(mStepModalNoteSlider) != noteVal) {
                    lv_slider_set_value(mStepModalNoteSlider, noteVal, LV_ANIM_OFF);
                    
                    lv_obj_t* parent = lv_obj_get_parent(mStepModalNoteSlider);
                    if (parent) {
                        lv_obj_t* label = lv_obj_get_child(parent, 0);
                        if (label) {
                            static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                            int octave = (noteVal / 12) - 1;
                            lv_label_set_text_fmt(label, "Note: %s%d (%d)", noteNames[noteVal % 12], octave, noteVal);
                        }
                    }
                }
            }
        }
    }

    // Sync Drum Number Row 1-8 scrub knobs in Settings screen
    if (mActiveNav == 5) {
        for (int i = 0; i < 8; ++i) {
            if (mDrumRowScrubArc[i]) {
                int note = mDrumRowNotes[i];
                if ((int)lv_arc_get_value(mDrumRowScrubArc[i]) != note) {
                    lv_arc_set_value(mDrumRowScrubArc[i], note);
                    if (mDrumRowScrubLbl[i]) {
                        lv_label_set_text_fmt(mDrumRowScrubLbl[i], "%d", note);
                    }
                    if (mDrumRowNoteDd[i]) {
                        lv_dropdown_set_selected(mDrumRowNoteDd[i], note - 20);
                    }
                }
            }
        }
    }

    // Poll parameter 477 (Wavetable selection) for all 8 tracks
    for (int t = 0; t < 8; ++t) {
        float currentVal = mEngine.getTracks()[t].parameters[477];
        if (currentVal != mLastWtSelectVal[t]) {
            mLastWtSelectVal[t] = currentVal;
            
            const char* home = getenv("HOME");
            std::string dirPath = home ? std::string(home) + "/Loom/wavetables" : "./Loom/wavetables";
            
            std::vector<std::string> wtFiles;
            DIR* dir = opendir(dirPath.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string name(entry->d_name);
                    if (name.rfind(".", 0) == 0) continue; // skip hidden files
                    
                    std::string lowerName = name;
                    for (char &c : lowerName) c = std::tolower((unsigned char)c);
                    bool isWavetable = false;
                    if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".wav") isWavetable = true;
                    else if (lowerName.length() >= 3 && lowerName.substr(lowerName.length() - 3) == ".wt") isWavetable = true;
                    
                    if (isWavetable) {
                        wtFiles.push_back(name);
                    }
                }
                closedir(dir);
            }
            
            if (!wtFiles.empty()) {
                std::sort(wtFiles.begin(), wtFiles.end());
                int idx = (int)(currentVal * (wtFiles.size() - 0.0001f));
                if (idx < 0) idx = 0;
                if (idx >= (int)wtFiles.size()) idx = (int)wtFiles.size() - 1;
                
                std::string chosenFile = wtFiles[idx];
                std::string fullPath = dirPath + "/" + chosenFile;
                
                if (mEngine.getTracks()[t].lastSamplePath != chosenFile) {
                    mEngine.loadWavetable(t, fullPath);
                    mEngine.getTracks()[t].lastSamplePath = chosenFile;
                    if (t == mActiveTrack && mWtActiveNameLbl) {
                        lv_label_set_text_fmt(mWtActiveNameLbl, "ACTIVE: %s", chosenFile.c_str());
                    }
                    std::cout << "WT Select loaded file: " << chosenFile << " for track " << t << std::endl;
                }
            }
        }
    }

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

                bool valChanged = false;
                if (lv_obj_check_type(w.widget, &lv_arc_class)) {
                    if (lv_arc_get_value(w.widget) != lvVal) {
                        lv_arc_set_value(w.widget, lvVal);
                        valChanged = true;
                    }
                } else {
                    if (lv_slider_get_value(w.widget) != lvVal) {
                        lv_slider_set_value(w.widget, lvVal, LV_ANIM_OFF);
                        valChanged = true;
                    }
                }

                if (valChanged && w.valLbl) {
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
                        if (rawVal < 0.125f) modeStr = "1-HIT";
                        else if (rawVal < 0.25f) modeStr = "SUSTN";
                        else if (rawVal < 0.375f) modeStr = "LOOP";
                        else if (rawVal < 0.50f) modeStr = "CHOP";
                        else if (rawVal < 0.625f) modeStr = "1-CHP";
                        else if (rawVal < 0.75f) modeStr = "L-CHP";
                        else if (rawVal < 0.875f) modeStr = "SCRUB";
                        else modeStr = "SL-SCR";
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

                bool valChanged = false;
                if (lv_obj_check_type(w.widget, &lv_arc_class)) {
                    if (lv_arc_get_value(w.widget) != lvVal) {
                        lv_arc_set_value(w.widget, lvVal);
                        valChanged = true;
                    }
                } else {
                    if (lv_slider_get_value(w.widget) != lvVal) {
                        lv_slider_set_value(w.widget, lvVal, LV_ANIM_OFF);
                        valChanged = true;
                    }
                }

                if (valChanged && w.valLbl) {
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
            float cpuTemp = HardwareDisplay::getCpuTemperature();
            if (mCpuLoadLabel != nullptr) {
                if (cpuTemp > 0.0f) {
                    lv_label_set_text_fmt(mCpuLoadLabel, "CPU: %.1f%% | %.1f\xC2\xB0" "C", mEngine.getCpuLoad() * 100.0f, cpuTemp);
                } else {
                    lv_label_set_text_fmt(mCpuLoadLabel, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
                }
            }
            if (mCpuLoadLabelSystem != nullptr) {
                if (cpuTemp > 0.0f) {
                    lv_label_set_text_fmt(mCpuLoadLabelSystem, "CPU: %.1f%% | %.1f\xC2\xB0" "C", mEngine.getCpuLoad() * 100.0f, cpuTemp);
                } else {
                    lv_label_set_text_fmt(mCpuLoadLabelSystem, "CPU Load: %.1f%%", mEngine.getCpuLoad() * 100.0f);
                }
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

        // Periodically update IP address label if visible (every ~2 seconds)
        if (mIpAddressLbl != nullptr) {
            static uint32_t ipCheckCounter = 0;
            if (ipCheckCounter++ % 120 == 0) {
                std::string ip = getLocalIPAddress();
                lv_label_set_text_fmt(mIpAddressLbl, "IP Address: %s", ip.c_str());
            }
        }

        // Update MIDI console monitor
        if (mMidiMonitorConsoleLabel != nullptr) {
            std::lock_guard<std::mutex> lock(mMidiLogMutex);
            if (mMidiLogDirty) {
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
                mMidiLogDirty = false;
            }
        }
    } else if (mActiveNav == 2) {
        // Sync sequencer UI step buttons state with engine state (crucial for live recording!)
        bool isDrum = false;
        int activeDrumIdx = -1;
        int numDrumLanes = 0;
        int engineType = mEngine.getTracks()[mActiveTrack].engineType;
        bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
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
        
        if (currentStep != mLastLaunchkeyStep) {
            mLastLaunchkeyStep = currentStep;
            pushLaunchkeyLedUpdate(&mEngine, this);
        }
        
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
    } else if (mActiveNav == 3) {
        // Highlight active playing step (Arpeggiator Playhead Tracking)
        const auto& arp = mEngine.getTracks()[mActiveTrack].arpeggiator;
        int activeStep = (mEngine.getIsPlaying() && arp.getMode() != ArpMode::OFF) ? (arp.getStep() % 16) : -1;
        bool isRecording = mEngine.getIsRecording();
        lv_color_t playheadColor = isRecording ? lv_color_hex(0xEF4444) : lv_color_hex(0xFFFFFF);
        
        for (int c = 0; c < 16; ++c) {
            if (mArpColumns[c]) {
                lv_obj_set_style_bg_opa(mArpColumns[c], LV_OPA_TRANSP, 0);
                lv_obj_t* colNum = lv_obj_get_child(mArpColumns[c], 0);
                if (colNum) {
                    if (c == activeStep) {
                        lv_obj_set_style_text_color(colNum, playheadColor, 0);
                    } else {
                        bool isBeatStart = (c == 0 || c == 4 || c == 8 || c == 12);
                        lv_obj_set_style_text_color(colNum, isBeatStart ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x777777), 0);
                    }
                }
            }
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
                        std::string displayName = dev.name.empty() ? "Unknown Device" : dev.name;
                        if (dev.rssi != -100) {
                            lv_label_set_text_fmt(btnLbl, "%s   [%s]   RSSI: %d dBm", displayName.c_str(), dev.mac.c_str(), dev.rssi);
                        } else {
                            lv_label_set_text_fmt(btnLbl, "%s   [%s]", displayName.c_str(), dev.mac.c_str());
                        }
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
    lv_obj_set_height(tabview, SCREEN_HEIGHT);
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
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
    
    if (engineType == 5 || engineType == 6 || isSamplerChops) {
        lv_obj_t* drumTabRow = lv_obj_create(seqTab);
        lv_obj_set_width(drumTabRow, lv_pct(100));
        lv_obj_set_height(drumTabRow, isSamplerChops ? 70 : 40);
        lv_obj_set_style_bg_opa(drumTabRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(drumTabRow, 0, 0);
        lv_obj_set_style_pad_all(drumTabRow, 0, 0);
        lv_obj_set_layout(drumTabRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(drumTabRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(drumTabRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(drumTabRow, 4, 0);
        lv_obj_set_style_pad_row(drumTabRow, 4, 0);
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
    // RIGHT SIDE PANEL: dynamic container (Track Params or Step Editor)
    // =========================================================================
    lv_obj_t* sidePanel = lv_obj_create(outerRow);
    lv_obj_set_size(sidePanel, 250, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(sidePanel, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(sidePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sidePanel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(sidePanel, 1, 0);
    lv_obj_set_style_pad_all(sidePanel, 0, 0);
    lv_obj_remove_flag(sidePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sidePanel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sidePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    mSeqSidePanel = sidePanel;
    rebuildSeqSidePanel();
}

void UIManager::closeSeqStepEditor() {
    mEditingStepIdx = -1;
    mStepModalActiveLocksList = nullptr;
    mStepModalRatchetDd = nullptr;
    mStepModalNoteSlider = nullptr;
    mStepModalPunchSw = nullptr;
    mStepModalProbSlider = nullptr;
    mStepModalGateSlider = nullptr;
    mStepModalSkipSw = nullptr;
    mStepModalPLockDd = nullptr;
    mStepModalPLockSlider = nullptr;
    rebuildSeqSidePanel();
    rebuildSeqGrid();
}

void UIManager::rebuildSeqSidePanel() {
    if (!mSeqSidePanel) return;
    lv_obj_clean(mSeqSidePanel);
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Create tabview inside mSeqSidePanel with tab bar at the bottom
    mSeqSideTabview = lv_tabview_create(mSeqSidePanel);
    lv_tabview_set_tab_bar_position(mSeqSideTabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(mSeqSideTabview, 42);
    lv_obj_set_size(mSeqSideTabview, 250, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(mSeqSideTabview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mSeqSideTabview, 0, 0);

    lv_obj_t* sideTabBar = lv_tabview_get_tab_bar(mSeqSideTabview);
    lv_obj_set_style_bg_color(sideTabBar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(sideTabBar, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(sideTabBar, 1, LV_PART_MAIN);

    mSeqTrackTab = lv_tabview_add_tab(mSeqSideTabview, "Sequence");
    mSeqStepTab = lv_tabview_add_tab(mSeqSideTabview, "Step");

    for (uint32_t i = 0; i < lv_obj_get_child_count(sideTabBar); i++) {
        lv_obj_t* btn = lv_obj_get_child(sideTabBar, i);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_color(btn, trackColor, LV_STATE_CHECKED);
    }

    // Default to Step tab if mEditingStepIdx >= 0, otherwise Sequence tab
    if (mEditingStepIdx >= 0) {
        lv_tabview_set_active(mSeqSideTabview, 1, LV_ANIM_OFF);
    } else {
        lv_tabview_set_active(mSeqSideTabview, 0, LV_ANIM_OFF);
    }

    // -------------------------------------------------------------------------
    // TAB 1: SEQUENCE / TRACK PARAMETERS
    // -------------------------------------------------------------------------
    lv_obj_set_style_pad_all(mSeqTrackTab, 4, 0);
    lv_obj_set_layout(mSeqTrackTab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mSeqTrackTab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mSeqTrackTab, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(mSeqTrackTab, LV_OBJ_FLAG_SCROLLABLE);

    auto makeSideGroup = [&](lv_obj_t* parent, int h) -> lv_obj_t* {
        lv_obj_t* grp = lv_obj_create(parent);
        lv_obj_set_size(grp, 226, h);
        lv_obj_set_style_bg_opa(grp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grp, 0, 0);
        lv_obj_set_style_pad_all(grp, 0, 0);
        lv_obj_remove_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(grp, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return grp;
    };

    auto makeSideLabel = [&](lv_obj_t* parent, const char* text) -> lv_obj_t* {
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        return lbl;
    };

    // 1. 4×4 / 8×8 toggle & Play Order side-by-side
    lv_obj_t* toggleGrp = makeSideGroup(mSeqTrackTab, 48);
    lv_obj_set_flex_flow(toggleGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggleGrp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* toggleBtn = lv_button_create(toggleGrp);
    lv_obj_set_size(toggleBtn, 108, 42);
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
    lv_obj_set_style_text_font(toggleLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(toggleLbl);
    lv_obj_add_event_cb(toggleBtn, seqGridToggleBtnEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* playOrderBtn = lv_button_create(toggleGrp);
    lv_obj_set_size(playOrderBtn, 108, 42);
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
    lv_obj_set_style_text_font(playOrderLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(playOrderLbl);
    lv_obj_add_event_cb(playOrderBtn, seqPlayOrderBtnEventCb, LV_EVENT_CLICKED, this);

    // 2. Knob grid: Length, Humanize, Probability, Clock Div in a 2x2 layout
    lv_obj_t* arcGrid = lv_obj_create(mSeqTrackTab);
    lv_obj_set_size(arcGrid, 226, 205);
    lv_obj_set_style_bg_opa(arcGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arcGrid, 0, 0);
    lv_obj_set_style_pad_all(arcGrid, 0, 0);
    lv_obj_remove_flag(arcGrid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(arcGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(arcGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(arcGrid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_row(arcGrid, 10, 0);
    lv_obj_set_style_pad_column(arcGrid, 8, 0);

    auto makeArcCell = [&](lv_obj_t* parent) -> lv_obj_t* {
        lv_obj_t* cell = lv_obj_create(parent);
        lv_obj_set_size(cell, 106, 95);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 4, 0);
        return cell;
    };

    // Cell 1: Length
    lv_obj_t* lenCell = makeArcCell(arcGrid);
    lv_obj_t* lenArc = lv_arc_create(lenCell);
    lv_obj_set_size(lenArc, 60, 60);
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
    lv_obj_set_size(humArc, 60, 60);
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
    lv_obj_set_size(probArc, 60, 60);
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
    lv_obj_set_size(clkArc, 60, 60);
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

    // 3. Transpose row
    lv_obj_t* transpGrp = makeSideGroup(mSeqTrackTab, 64);
    makeSideLabel(transpGrp, "Transpose");
    lv_obj_t* transpRow = lv_obj_create(transpGrp);
    lv_obj_set_size(transpRow, 220, 40);
    lv_obj_set_style_bg_opa(transpRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(transpRow, 0, 0);
    lv_obj_set_style_pad_all(transpRow, 0, 0);
    lv_obj_set_layout(transpRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(transpRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transpRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* tDnBtn = lv_button_create(transpRow);
    lv_obj_set_size(tDnBtn, 64, 38);
    lv_obj_set_style_radius(tDnBtn, 6, 0);
    lv_obj_set_style_bg_color(tDnBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* tDnLbl = lv_label_create(tDnBtn); lv_label_set_text(tDnLbl, "-"); lv_obj_center(tDnLbl);
    lv_obj_add_event_cb(tDnBtn, seqTransposeBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(tDnBtn, (void*)(uintptr_t)0);

    mSeqTransposeLbl = lv_label_create(transpRow);
    char tBuf[8];
    snprintf(tBuf, sizeof(tBuf), "%+d", mSeqTrackTranspose[mActiveTrack]);
    lv_label_set_text(mSeqTransposeLbl, tBuf);
    lv_obj_set_style_text_font(mSeqTransposeLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSeqTransposeLbl, trackColor, 0);

    lv_obj_t* tUpBtn = lv_button_create(transpRow);
    lv_obj_set_size(tUpBtn, 64, 38);
    lv_obj_set_style_radius(tUpBtn, 6, 0);
    lv_obj_set_style_bg_color(tUpBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* tUpLbl = lv_label_create(tUpBtn); lv_label_set_text(tUpLbl, "+"); lv_obj_center(tUpLbl);
    lv_obj_add_event_cb(tUpBtn, seqTransposeBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(tUpBtn, (void*)(uintptr_t)1);

    // 4. Octave row
    lv_obj_t* octGrp = makeSideGroup(mSeqTrackTab, 64);
    makeSideLabel(octGrp, "Octave");
    lv_obj_t* octRow = lv_obj_create(octGrp);
    lv_obj_set_size(octRow, 220, 40);
    lv_obj_set_style_bg_opa(octRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(octRow, 0, 0);
    lv_obj_set_style_pad_all(octRow, 0, 0);
    lv_obj_set_layout(octRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(octRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(octRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* oDnBtn = lv_button_create(octRow);
    lv_obj_set_size(oDnBtn, 64, 38);
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
    lv_obj_set_size(oUpBtn, 64, 38);
    lv_obj_set_style_radius(oUpBtn, 6, 0);
    lv_obj_set_style_bg_color(oUpBtn, lv_color_hex(0x333333), 0);
    lv_obj_t* oUpLbl2 = lv_label_create(oUpBtn); lv_label_set_text(oUpLbl2, "+"); lv_obj_center(oUpLbl2);
    lv_obj_add_event_cb(oUpBtn, seqOctaveBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(oUpBtn, (void*)(uintptr_t)1);

    // 5. Copy / Paste / Clear buttons
    lv_obj_t* cpGrp = makeSideGroup(mSeqTrackTab, 48);
    lv_obj_set_flex_flow(cpGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cpGrp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* copyBtn = lv_button_create(cpGrp);
    lv_obj_set_size(copyBtn, 68, 38);
    lv_obj_set_style_bg_color(copyBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(copyBtn, trackColor, 0);
    lv_obj_set_style_border_width(copyBtn, 1, 0);
    lv_obj_set_style_radius(copyBtn, 6, 0);
    lv_obj_t* copyLbl = lv_label_create(copyBtn); lv_label_set_text(copyLbl, "Copy");
    lv_obj_set_style_text_font(copyLbl, &lv_font_montserrat_12, 0); lv_obj_center(copyLbl);
    lv_obj_add_event_cb(copyBtn, seqCopyBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* pasteBtn = lv_button_create(cpGrp);
    lv_obj_set_size(pasteBtn, 68, 38);
    lv_obj_set_style_bg_color(pasteBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(pasteBtn, trackColor, 0);
    lv_obj_set_style_border_width(pasteBtn, 1, 0);
    lv_obj_set_style_radius(pasteBtn, 6, 0);
    lv_obj_t* pasteLbl = lv_label_create(pasteBtn); lv_label_set_text(pasteLbl, "Paste");
    lv_obj_set_style_text_font(pasteLbl, &lv_font_montserrat_12, 0); lv_obj_center(pasteLbl);
    lv_obj_add_event_cb(pasteBtn, seqPasteBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* clearBtn = lv_button_create(cpGrp);
    lv_obj_set_size(clearBtn, 68, 38);
    lv_obj_set_style_bg_color(clearBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(clearBtn, trackColor, 0);
    lv_obj_set_style_border_width(clearBtn, 1, 0);
    lv_obj_set_style_radius(clearBtn, 6, 0);
    lv_obj_t* clearLbl = lv_label_create(clearBtn); lv_label_set_text(clearLbl, "Clear");
    lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_12, 0); lv_obj_center(clearLbl);
    lv_obj_add_event_cb(clearBtn, seqClearBtnEventCb, LV_EVENT_CLICKED, this);

    // 6. Save / Load buttons
    lv_obj_t* slGrp = makeSideGroup(mSeqTrackTab, 48);
    lv_obj_set_flex_flow(slGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slGrp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* saveBtn = lv_button_create(slGrp);
    lv_obj_set_size(saveBtn, 108, 38);
    lv_obj_set_style_bg_color(saveBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(saveBtn, trackColor, 0);
    lv_obj_set_style_border_width(saveBtn, 1, 0);
    lv_obj_set_style_radius(saveBtn, 6, 0);
    lv_obj_t* saveLbl = lv_label_create(saveBtn); lv_label_set_text(saveLbl, "Save");
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_12, 0); lv_obj_center(saveLbl);
    lv_obj_add_event_cb(saveBtn, seqSaveBtnEventCb, LV_EVENT_CLICKED, this);

    lv_obj_t* loadBtn = lv_button_create(slGrp);
    lv_obj_set_size(loadBtn, 108, 38);
    lv_obj_set_style_bg_color(loadBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(loadBtn, trackColor, 0);
    lv_obj_set_style_border_width(loadBtn, 1, 0);
    lv_obj_set_style_radius(loadBtn, 6, 0);
    lv_obj_t* loadLbl = lv_label_create(loadBtn); lv_label_set_text(loadLbl, "Load");
    lv_obj_set_style_text_font(loadLbl, &lv_font_montserrat_12, 0); lv_obj_center(loadLbl);
    lv_obj_add_event_cb(loadBtn, seqLoadBtnEventCb, LV_EVENT_CLICKED, this);

    // -------------------------------------------------------------------------
    // TAB 2: STEP PARAMETERS & P-LOCKS EDITOR
    // -------------------------------------------------------------------------
    lv_obj_set_style_pad_all(mSeqStepTab, 4, 0);
    lv_obj_set_layout(mSeqStepTab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mSeqStepTab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mSeqStepTab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(mSeqStepTab, 10, 0);
    lv_obj_add_flag(mSeqStepTab, LV_OBJ_FLAG_SCROLLABLE);

    int activeStep = mEditingStepIdx >= 0 ? mEditingStepIdx : 0;

    // Header: Step title
    lv_obj_t* headerRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(headerRow, 226, 38);
    lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(headerRow, 0, 0);
    lv_obj_set_style_pad_all(headerRow, 0, 0);
    lv_obj_set_layout(headerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* titleLbl = lv_label_create(headerRow);
    lv_label_set_text_fmt(titleLbl, "STEP %d OPTIONS", activeStep + 1);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, trackColor, 0);

    lv_obj_t* doneBtn = lv_button_create(headerRow);
    lv_obj_set_size(doneBtn, 70, 32);
    lv_obj_set_style_bg_color(doneBtn, trackColor, 0);
    lv_obj_set_style_radius(doneBtn, 6, 0);
    lv_obj_t* doneLbl = lv_label_create(doneBtn);
    lv_label_set_text(doneLbl, "CLOSE");
    lv_obj_set_style_text_font(doneLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(doneLbl);
    auto doneCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->closeSeqStepEditor();
    };
    lv_obj_add_event_cb(doneBtn, doneCb, LV_EVENT_CLICKED, this);

    // Fetch step data
    int engineType = mEngine.getTracks()[mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
    bool isDrum = (engineType == 5 || engineType == 6 || isSamplerChops);

    std::vector<Step> currentSteps;
    if (isDrum) {
        currentSteps = mEngine.getDrumSequencerSteps(mActiveTrack, mActiveDrumIdx);
    } else {
        currentSteps = mEngine.getSequencerSteps(mActiveTrack);
    }

    Step stepObj;
    if (activeStep < (int)currentSteps.size()) {
        stepObj = currentSteps[activeStep];
    } else {
        stepObj.active = mSeqTrackSteps[mActiveTrack][activeStep];
    }

    // 1. Ratchet
    lv_obj_t* ratchetRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(ratchetRow, 226, 36);
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
    lv_obj_set_size(mStepModalRatchetDd, 110, 32);
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

    // 2. Note Slider
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

    lv_obj_t* noteRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(noteRow, 226, 52);
    lv_obj_set_style_bg_opa(noteRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(noteRow, 0, 0);
    lv_obj_set_style_pad_all(noteRow, 0, 0);
    lv_obj_set_layout(noteRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(noteRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(noteRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(noteRow, 6, 0);

    lv_obj_t* noteLbl = lv_label_create(noteRow);
    lv_label_set_text_fmt(noteLbl, "Note: %s", getNoteName(currentNoteVal).c_str());
    lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(noteLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalNoteSlider = lv_slider_create(noteRow);
    lv_obj_set_size(mStepModalNoteSlider, 226, 16);
    lv_slider_set_range(mStepModalNoteSlider, 24, 108);
    lv_slider_set_value(mStepModalNoteSlider, currentNoteVal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalNoteSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalNoteSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalNoteSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalNoteSlider, (void*)(uintptr_t)11);

    // 3. Punch & Skip Switches side-by-side
    lv_obj_t* swRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(swRow, 226, 36);
    lv_obj_set_style_bg_opa(swRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swRow, 0, 0);
    lv_obj_set_style_pad_all(swRow, 0, 0);
    lv_obj_set_layout(swRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(swRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(swRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* punchLbl = lv_label_create(swRow);
    lv_label_set_text(punchLbl, "Punch:");
    lv_obj_set_style_text_font(punchLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(punchLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalPunchSw = lv_switch_create(swRow);
    lv_obj_set_size(mStepModalPunchSw, 42, 24);
    if (stepObj.punch) lv_obj_add_state(mStepModalPunchSw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(mStepModalPunchSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(mStepModalPunchSw, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalPunchSw, (void*)(uintptr_t)12);

    lv_obj_t* skipLbl = lv_label_create(swRow);
    lv_label_set_text(skipLbl, "Skip:");
    lv_obj_set_style_text_font(skipLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(skipLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalSkipSw = lv_switch_create(swRow);
    lv_obj_set_size(mStepModalSkipSw, 42, 24);
    if (stepObj.isSkipped) lv_obj_add_state(mStepModalSkipSw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(mStepModalSkipSw, trackColor, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(mStepModalSkipSw, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalSkipSw, (void*)(uintptr_t)15);

    // 4. Probability Slider
    lv_obj_t* probRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(probRow, 226, 52);
    lv_obj_set_style_bg_opa(probRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(probRow, 0, 0);
    lv_obj_set_style_pad_all(probRow, 0, 0);
    lv_obj_set_layout(probRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(probRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(probRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(probRow, 6, 0);

    lv_obj_t* probLbl = lv_label_create(probRow);
    int probVal = (int)(stepObj.probability * 100.0f);
    lv_label_set_text_fmt(probLbl, "Probability: %d%%", probVal);
    lv_obj_set_style_text_font(probLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(probLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalProbSlider = lv_slider_create(probRow);
    lv_obj_set_size(mStepModalProbSlider, 226, 16);
    lv_slider_set_range(mStepModalProbSlider, 0, 100);
    lv_slider_set_value(mStepModalProbSlider, probVal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalProbSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalProbSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalProbSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalProbSlider, (void*)(uintptr_t)13);

    // 5. Gate Slider
    lv_obj_t* gateRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(gateRow, 226, 52);
    lv_obj_set_style_bg_opa(gateRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gateRow, 0, 0);
    lv_obj_set_style_pad_all(gateRow, 0, 0);
    lv_obj_set_layout(gateRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gateRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gateRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(gateRow, 6, 0);

    lv_obj_t* gateLbl = lv_label_create(gateRow);
    int gateVal = (int)(stepObj.gate * 100.0f);
    lv_label_set_text_fmt(gateLbl, "Gate Length: %d%%", gateVal);
    lv_obj_set_style_text_font(gateLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(gateLbl, lv_color_hex(0xCCCCCC), 0);

    mStepModalGateSlider = lv_slider_create(gateRow);
    lv_obj_set_size(mStepModalGateSlider, 226, 16);
    lv_slider_set_range(mStepModalGateSlider, 0, 100);
    lv_slider_set_value(mStepModalGateSlider, gateVal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalGateSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalGateSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalGateSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalGateSlider, (void*)(uintptr_t)14);

    // 6. Parameter Lock Section
    lv_obj_t* pLockHeader = lv_label_create(mSeqStepTab);
    lv_label_set_text(pLockHeader, "PARAMETER LOCKS");
    lv_obj_set_style_text_font(pLockHeader, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pLockHeader, trackColor, 0);
    lv_obj_set_style_margin_top(pLockHeader, 8, 0);

    auto params = getTrackParamOptions(mActiveTrack);
    std::string paramOptionsStr = "";
    for (size_t i = 0; i < params.size(); ++i) {
        paramOptionsStr += params[i].second;
        if (i < params.size() - 1) paramOptionsStr += "\n";
    }

    mStepModalPLockDd = lv_dropdown_create(mSeqStepTab);
    lv_obj_set_size(mStepModalPLockDd, 226, 34);
    lv_dropdown_set_options(mStepModalPLockDd, paramOptionsStr.c_str());
    lv_obj_set_style_text_font(mStepModalPLockDd, &lv_font_montserrat_12, 0);

    // Lock Value Slider
    lv_obj_t* pLockValRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(pLockValRow, 226, 50);
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
    lv_obj_set_size(mStepModalPLockSlider, 226, 16);
    lv_slider_set_range(mStepModalPLockSlider, 0, 100);
    lv_slider_set_value(mStepModalPLockSlider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mStepModalPLockSlider, trackColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(mStepModalPLockSlider, trackColor, LV_PART_KNOB);
    lv_obj_add_event_cb(mStepModalPLockSlider, stepModalControlEventCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_set_user_data(mStepModalPLockSlider, (void*)(uintptr_t)20);

    // Buttons: Add Lock & Clear All
    lv_obj_t* pLockBtnRow = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(pLockBtnRow, 226, 36);
    lv_obj_set_style_bg_opa(pLockBtnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pLockBtnRow, 0, 0);
    lv_obj_set_style_pad_all(pLockBtnRow, 0, 0);
    lv_obj_set_layout(pLockBtnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pLockBtnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pLockBtnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* addLockBtn = lv_button_create(pLockBtnRow);
    lv_obj_set_size(addLockBtn, 108, 34);
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
    lv_obj_set_size(clearLocksBtn, 108, 34);
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
    mStepModalActiveLocksList = lv_obj_create(mSeqStepTab);
    lv_obj_set_size(mStepModalActiveLocksList, 226, 160);
    lv_obj_set_style_bg_color(mStepModalActiveLocksList, lv_color_hex(0x111111), 0);
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

// ---- rebuildSeqGrid: renders 8×8 or 4×4 step grid ----
void UIManager::rebuildSeqGrid() {
    if (!mSeqGridContainer) return;
    lv_obj_clean(mSeqGridContainer);

    bool is4x4 = mSeqTrackIs4x4[mActiveTrack];
    int cols = is4x4 ? 4 : 8;
    int rows = is4x4 ? 4 : 8;
    int btnW = is4x4 ? 160 : 82;
    int btnH = is4x4 ? 130 : 72;
    int gap  = is4x4 ? 8   : 5;

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
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
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
        lv_obj_add_event_cb(btn, seqStepPressEventCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(btn, seqStepReleaseEventCb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(btn, seqStepReleaseEventCb, LV_EVENT_PRESS_LOST, this);
        mSeqStepButtons[i] = btn;
    }
    // Clear remaining slots beyond totalSteps
    for (int i = totalSteps; i < 64; ++i) {
        mSeqStepButtons[i] = nullptr;
    }
}

void UIManager::resetFileBrowserFlags() {
    mFileBrowserIsSave = false;
    mFileBrowserIsFmImport = false;
    mFileBrowserIsWtSelect = false;
    mFileBrowserIsWtImport = false;
    mFileBrowserIsSampleLoad = false;
    mFileBrowserIsSampleSave = false;
    mFileBrowserIsSfSelect = false;
    mFileBrowserIsSfImport = false;
    mFileBrowserIsPresetLoad = false;
    mFileBrowserIsPresetSave = false;
    mFileBrowserIsProject = false;
}

// ---- File Browser Modal ----
void UIManager::openFileBrowser(bool isSave) {
    if (mSeqModal) {
        lv_obj_delete(mSeqModal);
        mSeqModal = nullptr;
    }
    mFileBrowserIsSave = isSave;

    // Full-screen dimmed overlay
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_FLOATING);
    mSeqModal = overlay;

    // Modal card
    lv_obj_t* card = lv_obj_create(overlay);
    if (isSave) {
        lv_obj_set_size(card, 560, (SCREEN_HEIGHT >= 800) ? 480 : 260);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
    } else {
        lv_obj_set_size(card, 560, (SCREEN_HEIGHT >= 800) ? 520 : 420);
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
    lv_obj_set_size(fileList, 528, isSave ? ((SCREEN_HEIGHT >= 800) ? 220 : 75) : (showShortcuts ? 220 : ((SCREEN_HEIGHT >= 800) ? 360 : 280)));
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

            if (mFileBrowserIsProject) {
                bool matched = false;
                std::string lowerName = name;
                for (char &c : lowerName) c = std::tolower((unsigned char)c);
                if (lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".loom") matched = true;
                if (matched) matchingFiles.push_back(name);
            } else if (mFileBrowserIsFmImport) {
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
        if (mFileBrowserIsProject) emptyMsg = "(no projects found)";
        else if (mFileBrowserIsFmImport) emptyMsg = "(no presets found)";
        else if (mFileBrowserIsPresetLoad || mFileBrowserIsPresetSave) emptyMsg = "(no track presets found)";
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
        mFileBrowserTa = ta;
        lv_obj_set_size(ta, 528, 36);
        if (mFileBrowserIsProject) {
            lv_textarea_set_placeholder_text(ta, "project.loom");
        } else if (mFileBrowserIsSampleSave) {
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
        lv_obj_add_event_cb(ta, fileBrowserSaveBtnEventCb, LV_EVENT_READY, this);

        // Add a virtual keyboard to the overlay for touchscreen/mouse entry accessibility!
        lv_obj_t* kb = lv_keyboard_create(overlay);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_set_size(kb, (SCREEN_WIDTH >= 1280) ? 1050 : 780, (SCREEN_HEIGHT >= 800) ? 220 : 190);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_add_event_cb(kb, fileBrowserSaveBtnEventCb, LV_EVENT_READY, this);
        lv_obj_add_event_cb(kb, fileBrowserCancelEventCb, LV_EVENT_CANCEL, this);
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
    mFileBrowserTa = nullptr;
    resetFileBrowserFlags();
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
        bool isSamplerChops = (engineType == 2 && ui->mEngine.getTracks()[ui->mActiveTrack].samplerEngine.isChopMode());
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

void UIManager::seqStepPressEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int stepIdx = (int)(uintptr_t)lv_obj_get_user_data(btn);
    ui->mHeldStepIdx = stepIdx;
    ui->mStepMidiEntryCount = 0;
}

void UIManager::seqStepReleaseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int stepIdx = (int)(uintptr_t)lv_obj_get_user_data(btn);
    if (ui->mHeldStepIdx == stepIdx) {
        ui->mHeldStepIdx = -1;
    }
}

void UIManager::openSeqStepModal(int stepIdx) {
    mEditingStepIdx = stepIdx;
    rebuildSeqSidePanel();
    rebuildSeqGrid();
}

void UIManager::stepModalCloseEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->closeSeqStepEditor();
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

            std::vector<Step> currentSteps = ui->mEngine.getSequencerSteps(ui->mActiveTrack);
            if (ui->mEditingStepIdx >= 0 && ui->mEditingStepIdx < (int)currentSteps.size()) {
                const Step& s = currentSteps[ui->mEditingStepIdx];
                std::vector<std::pair<int, float>> remainingLocks;
                for (const auto& lp : s.parameterLocks) {
                    if (lp.first != pidToDelete) {
                        remainingLocks.push_back(lp);
                    }
                }
                
                ui->mEngine.clearParameterLocks(ui->mActiveTrack, ui->mEditingStepIdx);
                
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
    if (ui->mStepModalRatchetDd) {
        int selectedRatchetIdx = lv_dropdown_get_selected(ui->mStepModalRatchetDd);
        if (selectedRatchetIdx == 1) ratchetVal = 2;
        else if (selectedRatchetIdx == 2) ratchetVal = 3;
        else if (selectedRatchetIdx == 3) ratchetVal = 4;
        else if (selectedRatchetIdx == 4) ratchetVal = 8;
    }

    int noteVal = ui->mStepModalNoteSlider ? lv_slider_get_value(ui->mStepModalNoteSlider) : 60;
    bool punchVal = ui->mStepModalPunchSw ? lv_obj_has_state(ui->mStepModalPunchSw, LV_STATE_CHECKED) : false;
    float probVal = ui->mStepModalProbSlider ? (lv_slider_get_value(ui->mStepModalProbSlider) / 100.0f) : 1.0f;
    float gateVal = ui->mStepModalGateSlider ? (lv_slider_get_value(ui->mStepModalGateSlider) / 100.0f) : 0.8f;
    bool skipVal = ui->mStepModalSkipSw ? lv_obj_has_state(ui->mStepModalSkipSw, LV_STATE_CHECKED) : false;

    ui->mSeqTrackSteps[ui->mActiveTrack][ui->mEditingStepIdx] = !skipVal;

    ui->mEngine.setStep(ui->mActiveTrack, ui->mEditingStepIdx, true, {noteVal}, 0.8f,
                        ratchetVal, punchVal, probVal, gateVal, skipVal);
}

void UIManager::stepModalAddLockEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mEditingStepIdx < 0) return;

    auto params = ui->getTrackParamOptions(ui->mActiveTrack);
    if (!ui->mStepModalPLockDd) return;
    int selectedParamIdx = lv_dropdown_get_selected(ui->mStepModalPLockDd);
    if (selectedParamIdx < 0 || selectedParamIdx >= (int)params.size()) return;

    int paramId = params[selectedParamIdx].first;
    float lockVal = ui->mStepModalPLockSlider ? (lv_slider_get_value(ui->mStepModalPLockSlider) / 100.0f) : 0.5f;

    ui->mEngine.setParameterLock(ui->mActiveTrack, ui->mEditingStepIdx, paramId, lockVal);
    ui->refreshStepModalLocksList();
}

void UIManager::stepModalClearLocksEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mEditingStepIdx < 0) return;

    ui->mEngine.clearParameterLocks(ui->mActiveTrack, ui->mEditingStepIdx);
    ui->refreshStepModalLocksList();
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
    int engineType = ui->mEngine.getTracks()[ui->mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && ui->mEngine.getTracks()[ui->mActiveTrack].samplerEngine.isChopMode());
    bool isDrum = (engineType == 5 || engineType == 6 || isSamplerChops);

    if (isDrum) {
        ui->mSeqClipboard = ui->mEngine.getDrumSequencerSteps(ui->mActiveTrack, ui->mActiveDrumIdx);
    } else {
        ui->mSeqClipboard = ui->mEngine.getSequencerSteps(ui->mActiveTrack);
    }
}

void UIManager::seqPasteBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mSeqClipboard.empty()) return;

    int engineType = ui->mEngine.getTracks()[ui->mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && ui->mEngine.getTracks()[ui->mActiveTrack].samplerEngine.isChopMode());
    bool isDrum = (engineType == 5 || engineType == 6 || isSamplerChops);

    int total = ui->mSeqTrackIs4x4[ui->mActiveTrack] ? 16 : 64;
    for (int i = 0; i < (int)ui->mSeqClipboard.size() && i < total; ++i) {
        const Step& s = ui->mSeqClipboard[i];
        ui->mSeqTrackSteps[ui->mActiveTrack][i] = s.active;

        std::vector<int> rawNotes;
        if (s.notes.empty()) {
            rawNotes = {isDrum ? (60 + ui->mActiveDrumIdx) : 60};
        } else {
            for (const auto& n : s.notes) {
                rawNotes.push_back(n.note);
            }
        }
        float velocity = s.notes.empty() ? 0.8f : s.notes[0].velocity;

        ui->mEngine.setStep(ui->mActiveTrack, i, s.active, rawNotes, velocity,
                            s.ratchet, s.punch, s.probability, s.gate, s.isSkipped);
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
    if (!ui) return;
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSave = true;
    ui->openFileBrowser(true);
}

void UIManager::seqLoadBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    ui->resetFileBrowserFlags();
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
    if (!ui) return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, -1);
    if (lbl) {
        const char* filename = lv_label_get_text(lbl);
        if (ui->mFileBrowserIsSave) {
            // In save mode, clicking an existing file populates the text area to allow overwriting or editing
            if (ui->mFileBrowserTa) {
                lv_textarea_set_text(ui->mFileBrowserTa, filename);
            }
            return;
        }

        std::cout << (ui->mFileBrowserIsFmImport ? "Import FM Preset: " : "Load: ") << filename << std::endl;
        
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
    if (!ui) return;

    std::string nameStr = "";
    if (ui->mFileBrowserTa) {
        const char* txt = lv_textarea_get_text(ui->mFileBrowserTa);
        if (txt) nameStr = txt;
    } else {
        // Fallback: try to find textarea in children
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        if (target) {
            lv_obj_t* card = ui->mSeqModal ? lv_obj_get_child(ui->mSeqModal, 0) : nullptr;
            if (card) {
                uint32_t cnt = lv_obj_get_child_cnt(card);
                for (uint32_t i = 0; i < cnt; i++) {
                    lv_obj_t* child = lv_obj_get_child(card, i);
                    if (lv_obj_check_type(child, &lv_textarea_class)) {
                        const char* txt = lv_textarea_get_text(child);
                        if (txt) nameStr = txt;
                        break;
                    }
                }
            }
        }
    }

    // Trim whitespace and newlines
    while (!nameStr.empty() && (nameStr.back() == '\n' || nameStr.back() == '\r' || nameStr.back() == ' ' || nameStr.back() == '\t')) {
        nameStr.pop_back();
    }
    while (!nameStr.empty() && (nameStr.front() == ' ' || nameStr.front() == '\t')) {
        nameStr.erase(0, 1);
    }

    // If empty, use sensible default
    if (nameStr.empty()) {
        if (ui->mFileBrowserIsProject) nameStr = "Project.loom";
        else if (ui->mFileBrowserIsSampleSave) nameStr = "sample.wav";
        else if (ui->mFileBrowserIsPresetSave) nameStr = "patch.gbs";
        else nameStr = "sequence.seq";
    }

    const char* browseDir = getenv("HOME");
    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";

    if (ui->mFileBrowserIsProject) {
        std::string dirPath = !ui->mFileBrowserCurrentPath.empty() ? ui->mFileBrowserCurrentPath : (homeStr + "/projects");
        try {
            std::filesystem::create_directories(dirPath);
        } catch (...) {}

        std::string finalName = nameStr;
        std::string lowerName = finalName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName == "init" || lowerName == "init.loom") {
            finalName = "Init.loom";
        }
        if (finalName.length() < 5 || finalName.substr(finalName.length() - 5) != ".loom") {
            finalName += ".loom";
        }
        std::string fullPath = dirPath + "/" + finalName;
        ui->mEngine.saveProject(fullPath);
        ui->mSettingsFilePath = fullPath + ".settings";
        ui->saveSettings(ui->mSettingsFilePath);
        std::cout << "Project saved to: " << fullPath << std::endl;
    } else if (ui->mFileBrowserIsSampleSave) {
        std::string dirPath = !ui->mFileBrowserCurrentPath.empty() ? ui->mFileBrowserCurrentPath : (homeStr + "/samples");
        try {
            std::filesystem::create_directories(dirPath);
        } catch (...) {}
        if (nameStr.length() < 4 || (nameStr.substr(nameStr.length() - 4) != ".wav" && nameStr.substr(nameStr.length() - 4) != ".WAV")) {
            nameStr += ".wav";
        }
        std::string fullPath = dirPath + "/" + nameStr;
        ui->mEngine.saveSample(ui->mActiveTrack, fullPath);
        std::cout << "Saved sample: " << fullPath << std::endl;
    } else if (ui->mFileBrowserIsPresetSave) {
        std::string dirPath = !ui->mFileBrowserCurrentPath.empty() ? ui->mFileBrowserCurrentPath : (homeStr + "/presets");
        try {
            std::filesystem::create_directories(dirPath);
        } catch (...) {}
        if (nameStr.length() < 4 || nameStr.substr(nameStr.length() - 4) != ".gbs") {
            nameStr += ".gbs";
        }
        std::string fullPath = dirPath + "/" + nameStr;
        ui->mEngine.saveTrackPresetToPath(ui->mActiveTrack, fullPath);
        std::cout << "Saved track preset: " << fullPath << std::endl;
    } else {
        std::string dirPath = !ui->mFileBrowserCurrentPath.empty() ? ui->mFileBrowserCurrentPath : (homeStr + "/sequences");
        try {
            std::filesystem::create_directories(dirPath);
        } catch (...) {}
        if (nameStr.length() < 4 || (nameStr.substr(nameStr.length() - 4) != ".seq" && nameStr.substr(nameStr.length() - 5) != ".json")) {
            nameStr += ".seq";
        }
        std::string fullPath = dirPath + "/" + nameStr;
        ui->mEngine.saveProject(fullPath);
        std::cout << "Sequence saved: " << fullPath << std::endl;
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
        case 0: return "32/1";
        case 1: return "24/1";
        case 2: return "16/1";
        case 3: return "12/1";
        case 4: return "8/1";
        case 5: return "6/1";
        case 6: return "4/1";
        case 7: return "3/1";
        case 8: return "2/1";
        case 9: return "1/1";
        case 10: return "1/2";
        case 11: return "1/3";
        case 12: return "1/4";
        case 13: return "1/6";
        case 14: return "1/8";
        case 15: return "1/12";
        case 16: return "1/16";
        case 17: return "1/24";
        case 18: return "1/32";
        case 19: return "1/48";
        case 20: return "1/64";
        case 21: return "1/72";
        case 22: return "1/96";
        default: return "1/4";
    }
}

std::string UIManager::getParameterNameString(int trackIdx, int paramId, AudioEngine* engine) {
    if (paramId >= 490 && paramId < 600) {
        if (paramId == 500) return "Reverb Size";
        if (paramId == 501) return "Reverb Damp";
        if (paramId == 502) return "Reverb Mod";
        if (paramId == 503) return "Reverb Mix";
        if (paramId == 504) return "Reverb PreDelay";
        if (paramId == 506) return "Reverb Tone";
        if (paramId == 510) return "Chorus Rate";
        if (paramId == 511) return "Chorus Depth";
        if (paramId == 512) return "Chorus Mix";
        if (paramId == 513) return "Chorus Voices";
        if (paramId == 520) return "Delay Time";
        if (paramId == 521) return "Delay Feedbk";
        if (paramId == 522) return "Delay Mix";
        if (paramId == 523) return "Delay Cutoff";
        if (paramId == 524) return "Delay Reson";
        if (paramId == 530) return "Crush Bits";
        if (paramId == 531) return "Crush Rate";
        if (paramId == 532) return "Crush Mix";
        if (paramId == 540) return "Drive Drive";
        if (paramId == 541) return "Drive Dist";
        if (paramId == 542) return "Drive Level";
        if (paramId == 543) return "Drive Tone";
        if (paramId == 550) return "Phaser Rate";
        if (paramId == 551) return "Phaser Depth";
        if (paramId == 552) return "Phaser Mix";
        if (paramId == 553) return "Phaser Feedbk";
        if (paramId == 570) return "Slicer Rate1";
        if (paramId == 571) return "Slicer Rate2";
        if (paramId == 572) return "Slicer Rate3";
        if (paramId == 573) return "Slicer Pat";
        if (paramId == 574) return "Slicer Mix";
        if (paramId == 580) return "Comp Thresh";
        if (paramId == 581) return "Comp Ratio";
        if (paramId == 582) return "Comp Attack";
        if (paramId == 583) return "Comp Release";
        if (paramId == 584) return "Comp Makeup";
        if (paramId == 586) return "Comp SC Drum";
        if (paramId == 490) return "LP LFO Rate";
        if (paramId == 491) return "LP LFO Depth";
        if (paramId == 492) return "LP LFO Shape";
        if (paramId == 493) return "LP LFO Cutoff";
        if (paramId == 494) return "LP LFO Reson";
        return "Global FX " + std::to_string(paramId);
    }
    if (paramId >= 1500 && paramId < 1600) {
        if (paramId == 1500) return "Flanger Rate";
        if (paramId == 1501) return "Flanger Depth";
        if (paramId == 1502) return "Flanger Mix";
        if (paramId == 1503) return "Flanger Feedbk";
        if (paramId == 1504) return "Flanger Delay";
        if (paramId == 1510) return "Echo Time";
        if (paramId == 1511) return "Echo Feedbk";
        if (paramId == 1512) return "Echo Mix";
        if (paramId == 1513) return "Echo Drive";
        if (paramId == 1514) return "Echo Wow";
        if (paramId == 1515) return "Echo Flutter";
        if (paramId == 1520) return "Wobble Rate";
        if (paramId == 1521) return "Wobble Depth";
        if (paramId == 1522) return "Wobble Mix";
        if (paramId == 1530) return "Octaver Mix";
        if (paramId == 1531) return "Octaver Oct1";
        if (paramId == 1532) return "Octaver Oct2";
        if (paramId == 1536) return "EQ Bass";
        if (paramId == 1537) return "EQ Mid";
        if (paramId == 1538) return "EQ Treble";
        if (paramId == 1539) return "EQ MidFreq";
        if (paramId == 1590) return "HP LFO Rate";
        if (paramId == 1591) return "HP LFO Depth";
        if (paramId == 1592) return "HP LFO Shape";
        if (paramId == 1593) return "HP LFO Cutoff";
        if (paramId == 1594) return "HP LFO Reson";
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
    if (paramId >= 2300 && paramId <= 2309) {
        std::string prefix = "Track " + std::to_string(trackIdx + 1) + " Arp ";
        if (paramId == 2300) return prefix + "Mode";
        if (paramId == 2301) return prefix + "Rate";
        if (paramId == 2302) return prefix + "Octaves";
        if (paramId == 2303) return prefix + "Latch";
        if (paramId == 2304) return prefix + "Strum";
        if (paramId == 2305) return prefix + "Probability";
        if (paramId == 2306) return prefix + "Chord Gen";
        if (paramId == 2307) return prefix + "Chord Mood";
        if (paramId == 2308) return prefix + "Chord Complexity";
        if (paramId == 2309) return prefix + "Inversions";
        return prefix + std::to_string(paramId);
    }
    if (paramId >= 2000 && paramId < 2180) {
        int fxIdx = (paramId - 2000) / 10;
        const char* FX_NAMES[17] = {
            "Overdrive", "Bitcrusher", "Chorus", "Phaser", "Tape Wobble",
            "Delay", "Reverb", "Slicer", "Compressor", "HP LFO", "LP LFO",
            "Flanger", "Filter 1", "Tape Echo", "Octaver", "Filter 2", "Filter 3"
        };
        if (fxIdx >= 0 && fxIdx < 17) {
            return "Track " + std::to_string(trackIdx + 1) + " " + FX_NAMES[fxIdx] + " Send";
        }
    }

    std::string prefix = "Track " + std::to_string(trackIdx + 1) + " ";
    if (paramId == 0) return prefix + "Volume";
    if (paramId == 9) return prefix + "Pan";
    if (paramId == 2400) return prefix + "Ratchet";
    if (paramId >= 2410 && paramId <= 2417) {
        return "Drum Key " + std::to_string(paramId - 2409) + " Note";
    }

    int engineType = 0; // Default: Subtractive
    if (engine && trackIdx >= 0 && trackIdx < 8) {
        engineType = engine->getTracks()[trackIdx].engineType;
    }

    // Engine-specific naming:
    if (engineType == 0) { // Subtractive
        if (paramId == 1 || paramId == 112) return prefix + "Cutoff";
        if (paramId == 2 || paramId == 113) return prefix + "Reson";
        if (paramId == 118) return prefix + "Env Amount";
        if (paramId == 290) return prefix + "Morphx3";
        if (paramId == 291) return prefix + "Foldx3";
        if (paramId == 292) return prefix + "Drivex3";
        if (paramId == 104 || paramId == 4) return prefix + "Osc1 Morph";
        if (paramId == 170) return prefix + "Osc1 Drive";
        if (paramId == 180) return prefix + "Osc1 Fold";
        if (paramId == 160) return prefix + "Osc1 Pitch";
        if (paramId == 107) return prefix + "Osc1 Vol";
        if (paramId == 105) return prefix + "Osc2 Morph";
        if (paramId == 171) return prefix + "Osc2 Drive";
        if (paramId == 181) return prefix + "Osc2 Fold";
        if (paramId == 161) return prefix + "Osc2 Pitch";
        if (paramId == 108) return prefix + "Osc2 Vol";
        if (paramId == 155) return prefix + "Sub Morph";
        if (paramId == 172) return prefix + "Sub Drive";
        if (paramId == 182) return prefix + "Sub Fold";
        if (paramId == 162) return prefix + "Sub Pitch";
        if (paramId == 109) return prefix + "Sub Vol";
        if (paramId == 106 || paramId == 6) return prefix + "Detune";
        if (paramId == 110) return prefix + "Noise Vol";
        if (paramId == 355) return prefix + "Glide";
        if (paramId == 7) return prefix + "LFO Rate";
        if (paramId == 8) return prefix + "LFO Depth";
        if (paramId == 100) return prefix + "Amp A";
        if (paramId == 101) return prefix + "Amp D";
        if (paramId == 102) return prefix + "Amp S";
        if (paramId == 103) return prefix + "Amp R";
        if (paramId == 114) return prefix + "Filt A";
        if (paramId == 115) return prefix + "Filt D";
        if (paramId == 116) return prefix + "Filt S";
        if (paramId == 117) return prefix + "Filt R";
    } else if (engineType == 1) { // FM
        if (paramId == 1 || paramId == 151) return prefix + "Cutoff";
        if (paramId == 2 || paramId == 152) return prefix + "Reson";
        if (paramId == 118) return prefix + "Env Amt";
        if (paramId == 150) return prefix + "Algorithm";
        if (paramId == 154) return prefix + "Feedback";
        if (paramId == 157) return prefix + "Brightness";
        if (paramId == 159) return prefix + "Drive";
        if (paramId == 355) return prefix + "Glide";
        if (paramId == 196) return prefix + "FM Preset";
        if (paramId >= 160 && paramId < 196) {
            int op = (paramId - 160) / 6;
            int sub = (paramId - 160) % 6;
            const char* subNames[6] = {"Level", "Attack", "Decay", "Sustain", "Release", "Ratio"};
            return prefix + "Op" + std::to_string(op + 1) + " " + subNames[sub];
        }
    } else if (engineType == 2) { // Sampler
        if (paramId == 1 || paramId == 303) return prefix + "Cutoff";
        if (paramId == 2 || paramId == 304) return prefix + "Reson";
        if (paramId == 314) return prefix + "Env Amt";
        if (paramId == 320) return prefix + "Play Mode";
        if (paramId == 330) return prefix + "Start Pnt";
        if (paramId == 331) return prefix + "End Point";
        if (paramId == 302) return prefix + "Speed";
        if (paramId == 300) return prefix + "Pitch";
        if (paramId == 301) return prefix + "Stretch";
        if (paramId == 360) return prefix + "Scrub Pos";
        if (paramId == 355) return prefix + "Glide";
        if (paramId == 340) return prefix + "Slices";
        if (paramId == 341) return prefix + "Slice Select";
        if (paramId == 342) return prefix + "Slice Lock";
        if (paramId == 310) return prefix + "Amp A";
        if (paramId == 311) return prefix + "Amp D";
        if (paramId == 312) return prefix + "Amp S";
        if (paramId == 313) return prefix + "Amp R";
    } else if (engineType == 3) { // Granular
        if (paramId == 1) return prefix + "Cutoff";
        if (paramId == 2) return prefix + "Reson";
        if (paramId == 400) return prefix + "Grain Size";
        if (paramId == 401) return prefix + "Density";
        if (paramId == 402) return prefix + "Jitter";
        if (paramId == 403) return prefix + "Spread";
        if (paramId == 330) return prefix + "Position";
        if (paramId == 355) return prefix + "Glide";
        if (paramId == 425) return prefix + "Amp A";
        if (paramId == 426) return prefix + "Amp D";
        if (paramId == 427) return prefix + "Amp S";
        if (paramId == 428) return prefix + "Amp R";
    } else if (engineType == 4) { // Wavetable
        if (paramId == 450) return prefix + "WT Morph";
        if (paramId == 465) return prefix + "WT Warp";
        if (paramId == 466) return prefix + "WT Crush";
        if (paramId == 467) return prefix + "WT Drive";
        if (paramId == 451) return prefix + "WT Detune";
        if (paramId == 355) return prefix + "Glide";
        if (paramId == 475) return prefix + "Bitrate";
        if (paramId == 476) return prefix + "Samplerate";
        if (paramId == 458 || paramId == 1) return prefix + "WT Cutoff";
        if (paramId == 459 || paramId == 2) return prefix + "WT Reson";
        if (paramId == 464) return prefix + "Env Amt";
        if (paramId == 477) return prefix + "WT Select";
        if (paramId == 454) return prefix + "Amp A";
        if (paramId == 455) return prefix + "Amp D";
        if (paramId == 456) return prefix + "Amp S";
        if (paramId == 457) return prefix + "Amp R";
        if (paramId == 471) return prefix + "Filt A";
        if (paramId == 472) return prefix + "Filt D";
        if (paramId == 473) return prefix + "Filt S";
        if (paramId == 474) return prefix + "Filt R";
    } else if (engineType == 5) { // FM Drum
        if (paramId >= 200 && paramId < 280) {
            int drumIdx = (paramId - 200) / 10;
            int offset = (paramId - 200) % 10;
            const char* drumNames[8] = {"BD", "SD", "TOM", "CH", "OH", "CYMB", "PERC", "NOISE"};
            const char* paramNames[10] = {"Pitch", "Snap", "Decay", "Tone", "ParamA", "Level", "ParamB", "H", "I", "J"};
            if (drumIdx >= 0 && drumIdx < 8 && offset >= 0 && offset < 10) {
                return prefix + drumNames[drumIdx] + " " + paramNames[offset];
            }
        }
    } else if (engineType == 6) { // Analog Drum
        if (paramId == 600) return prefix + "BD Decay";
        if (paramId == 601) return prefix + "BD Tone";
        if (paramId == 602) return prefix + "BD Tune";
        if (paramId == 605) return prefix + "BD Gain";
        if (paramId == 610) return prefix + "SD Decay";
        if (paramId == 613) return prefix + "SD Snap";
        if (paramId == 612) return prefix + "SD Tune";
        if (paramId == 615) return prefix + "SD Gain";
        if (paramId == 620) return prefix + "RIM Decay";
        if (paramId == 621) return prefix + "RIM Col";
        if (paramId == 622) return prefix + "RIM Tune";
        if (paramId == 625) return prefix + "RIM Gain";
        if (paramId == 630) return prefix + "HAT C Decay";
        if (paramId == 631) return prefix + "HAT C Col";
        if (paramId == 635) return prefix + "HAT C Gain";
        if (paramId == 640) return prefix + "HAT O Decay";
        if (paramId == 641) return prefix + "HAT O Col";
        if (paramId == 645) return prefix + "HAT O Gain";
        if (paramId == 653) return prefix + "CYM Atk";
        if (paramId == 650) return prefix + "CYM Decay";
        if (paramId == 651) return prefix + "CYM Col";
        if (paramId == 655) return prefix + "CYM Gain";
        if (paramId == 660) return prefix + "PERC Decay";
        if (paramId == 661) return prefix + "PERC Tone";
        if (paramId == 662) return prefix + "PERC Tune";
        if (paramId == 665) return prefix + "PERC Gain";
        if (paramId == 670) return prefix + "NOISE Decay";
        if (paramId == 671) return prefix + "NOISE Tone";
        if (paramId == 675) return prefix + "NOISE Gain";
    } else if (engineType == 9) { // SoundFont
        if (paramId == 180) return prefix + "SF Preset";
        if (paramId == 181) return prefix + "SF Bank";
        if (paramId == 1) return prefix + "Cutoff";
        if (paramId == 2) return prefix + "Reson";
        if (paramId == 7) return prefix + "LFO Rate";
        if (paramId == 8) return prefix + "LFO Depth";
        if (paramId == 100) return prefix + "Amp A";
        if (paramId == 101) return prefix + "Amp D";
        if (paramId == 102) return prefix + "Amp S";
        if (paramId == 103) return prefix + "Amp R";
    }

    // Generic fallbacks if not matched:
    if (paramId == 1) return prefix + "Cutoff";
    if (paramId == 2) return prefix + "Reson";
    if (paramId == 100) return prefix + "Attack";
    if (paramId == 101) return prefix + "Decay";
    if (paramId == 102) return prefix + "Sustain";
    if (paramId == 103) return prefix + "Release";

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
    lv_obj_set_size(knobsRow, lv_pct(100), LV_SIZE_CONTENT);
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
        lv_obj_set_size(kCard, 118, 195);
        applyCardStyle(kCard);
        lv_obj_set_style_pad_all(kCard, 6, 0);
        lv_obj_set_layout(kCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(kCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(kCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* identLbl = lv_label_create(kCard);
        lv_label_set_text_fmt(identLbl, "K%d", k + 1);
        lv_obj_set_style_text_font(identLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(identLbl, lv_color_hex(0xFFB300), 0);

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
        lv_obj_set_size(arc, 74, 74);
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
        int ch = mSeqMidiKnobChannel[mActiveTrack][k];
        if (ch == 0) {
            lv_label_set_text_fmt(ccLbl, "CC %d", mSeqMidiKnobCC[mActiveTrack][k]);
        } else {
            lv_label_set_text_fmt(ccLbl, "CC %d [CH%d]", mSeqMidiKnobCC[mActiveTrack][k], ch);
        }
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
    lv_obj_set_size(fadersRow, lv_pct(100), LV_SIZE_CONTENT);
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
        lv_obj_set_size(fCard, 118, 250);
        applyCardStyle(fCard);
        lv_obj_set_style_pad_all(fCard, 6, 0);
        lv_obj_set_layout(fCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(fCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(fCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* identLbl = lv_label_create(fCard);
        lv_label_set_text_fmt(identLbl, "F%d", f + 1);
        lv_obj_set_style_text_font(identLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(identLbl, lv_color_hex(0xFFB300), 0);

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
        lv_obj_set_size(fader, 16, 160);
        lv_slider_set_range(fader, 0, 100);
        lv_slider_set_value(fader, (int)(mSeqMidiFaderValue[mActiveTrack][f] * 100), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(fader, trackColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(fader, trackColor, LV_PART_KNOB);

        lv_obj_t* valLbl = lv_label_create(fCard);
        mAssignFaderValLabels[f] = valLbl;
        lv_label_set_text_fmt(valLbl, "%d%%", (int)(mSeqMidiFaderValue[mActiveTrack][f] * 100));
        lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* ccLbl = lv_label_create(fCard);
        int ch = mSeqMidiFaderChannel[mActiveTrack][f];
        if (ch == 0) {
            lv_label_set_text_fmt(ccLbl, "CC %d", mSeqMidiFaderCC[mActiveTrack][f]);
        } else {
            lv_label_set_text_fmt(ccLbl, "CC %d [CH%d]", mSeqMidiFaderCC[mActiveTrack][f], ch);
        }
        lv_obj_set_style_text_font(ccLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ccLbl, trackColor, 0);

        // Click fader card to remap
        lv_obj_add_flag(fCard, LV_OBJ_FLAG_CLICKABLE);
        struct RemapEventData {
            UIManager* ui;
            int targetIdx; // 40-79 for faders
        };
        RemapEventData* remapData = new RemapEventData{this, f + 40};
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
    lv_obj_set_size(tab2Container, lv_pct(100), 680);
    lv_obj_set_style_bg_opa(tab2Container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab2Container, 0, 0);
    lv_obj_set_style_pad_all(tab2Container, 0, 0);
    lv_obj_set_layout(tab2Container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab2Container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab2Container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(tab2Container, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: Macros Grid (2 rows of 4 Macros)
    lv_obj_t* macrosGrid = lv_obj_create(tab2Container);
    lv_obj_set_size(macrosGrid, 760, 680);
    lv_obj_set_style_bg_opa(macrosGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(macrosGrid, 0, 0);
    lv_obj_set_style_pad_all(macrosGrid, 0, 0);
    lv_obj_set_layout(macrosGrid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(macrosGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(macrosGrid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_row(macrosGrid, 12, 0);
    lv_obj_set_style_pad_column(macrosGrid, 10, 0);
    lv_obj_remove_flag(macrosGrid, LV_OBJ_FLAG_SCROLLABLE);

    // Right side: Active Connections List
    lv_obj_t* listCard = lv_obj_create(tab2Container);
    lv_obj_set_size(listCard, 280, 680);
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
    lv_obj_set_size(atCard, 256, 75);
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
    lv_obj_set_size(atDestBtn, 240, 28);
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
    lv_obj_set_size(mActiveRoutingsContainer, 256, 530);
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
        lv_obj_set_size(macroCard, 175, 325);
        applyCardStyle(macroCard);
        lv_obj_set_style_pad_all(macroCard, 8, 0);
        lv_obj_set_layout(macroCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(macroCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(macroCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Header: Macro Name & Learn Button Row
        lv_obj_t* headerRow = lv_obj_create(macroCard);
        lv_obj_set_size(headerRow, 158, 26);
        lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(headerRow, 0, 0);
        lv_obj_set_style_pad_all(headerRow, 0, 0);
        lv_obj_set_layout(headerRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* mLbl = lv_label_create(headerRow);
        lv_label_set_text_fmt(mLbl, "MACRO %d", m + 1);
        lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(mLbl, trackColor, 0);

        lv_obj_t* learnBtn = lv_button_create(headerRow);
        lv_obj_set_size(learnBtn, 56, 22);
        lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(learnBtn, 4, 0);
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
        lv_obj_set_size(srcDd, 158, 32);
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
        lv_obj_set_size(controlsRow, 158, 180);
        lv_obj_set_style_bg_opa(controlsRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(controlsRow, 0, 0);
        lv_obj_set_style_pad_all(controlsRow, 0, 0);
        lv_obj_set_layout(controlsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(controlsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(controlsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (int d = 0; d < 2; ++d) {
            // Column for Arc + Dest Button (more compact, fits card perfectly)
            lv_obj_t* col = lv_obj_create(controlsRow);
            lv_obj_set_size(col, 74, 170);
            lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(col, 0, 0);
            lv_obj_set_style_pad_all(col, 0, 0);
            lv_obj_set_layout(col, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // Bipolar Arc
            int arcVal = (int)((d == 0 ? amt1 : amt2) * 100.0f);
            lv_obj_t* mArc = lv_arc_create(col);
            lv_obj_set_size(mArc, 66, 66);
            lv_arc_set_range(mArc, -100, 100);
            lv_arc_set_value(mArc, arcVal);
            lv_obj_set_style_arc_color(mArc, trackColor, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(mArc, trackColor, LV_PART_KNOB);
            
            // Thin arc styling & small handle scaling to prevent text/label clipping
            lv_obj_set_style_arc_width(mArc, 4, LV_PART_MAIN);
            lv_obj_set_style_arc_width(mArc, 4, LV_PART_INDICATOR);
            lv_obj_set_style_width(mArc, 8, LV_PART_KNOB);
            lv_obj_set_style_height(mArc, 8, LV_PART_KNOB);
            lv_obj_set_style_pad_all(mArc, 0, LV_PART_KNOB);

            mMacroArc[m][d] = mArc;

            lv_obj_t* mVal = lv_label_create(mArc);
            lv_label_set_text_fmt(mVal, "%s%d%%", arcVal > 0 ? "+" : "", arcVal);
            lv_obj_set_style_text_font(mVal, &lv_font_montserrat_10, 0);
            lv_obj_center(mVal);

            MacroArcCallbackData* arcData = new MacroArcCallbackData{this, m, d, mVal};
            lv_obj_add_event_cb(mArc, macroValueArcEventCb, LV_EVENT_VALUE_CHANGED, arcData);
            auto aDataFreeCb = [](lv_event_t* e) { delete (MacroArcCallbackData*)lv_event_get_user_data(e); };
            lv_obj_add_event_cb(mArc, aDataFreeCb, LV_EVENT_DELETE, arcData);

            // Destination Button
            lv_obj_t* destBtn = lv_button_create(col);
            lv_obj_set_size(destBtn, 74, 34);
            lv_obj_set_style_bg_color(destBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_radius(destBtn, 6, 0);
            lv_obj_set_style_pad_all(destBtn, 2, 0);

            lv_obj_t* destLbl = lv_label_create(destBtn);
            mMacroDestBtnLabel[m][d] = destLbl;

            int targetPid = mMacroDestParamId[m][d];
            int targetTrk = mMacroDestTrack[m][d];
            if (targetPid != -1) {
                std::string destName = getParameterNameString(targetTrk, targetPid, &mEngine);
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
    lv_obj_set_flex_align(tab3, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_row(tab3, 10, 0);
    lv_obj_set_style_pad_column(tab3, 10, 0);

    for (int l = 0; l < 6; ++l) {
        LfoEngine& lfo = mEngine.mLfos[l];

        lv_obj_t* lfoCard = lv_obj_create(tab3);
        lv_obj_set_size(lfoCard, 510, 215); // Wide and compact 2-column layout (2 cols x 3 rows)!
        applyCardStyle(lfoCard);
        lv_obj_set_style_pad_all(lfoCard, 8, 0);
        lv_obj_set_layout(lfoCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(lfoCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(lfoCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Header with active track colored label
        lv_obj_t* lfoHeader = lv_obj_create(lfoCard);
        lv_obj_set_size(lfoHeader, 486, 30);
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
        lv_obj_set_size(syncBtn, 75, 26);
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

        // Middle Row: Shape Dropdown on left (230px), Destination Button on right (230px)
        lv_obj_t* midRow = lv_obj_create(lfoCard);
        lv_obj_set_size(midRow, 486, 36);
        lv_obj_set_style_bg_opa(midRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(midRow, 0, 0);
        lv_obj_set_style_pad_all(midRow, 0, 0);
        lv_obj_set_layout(midRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(midRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(midRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Shape selector dropdown
        lv_obj_t* shapeDd = lv_dropdown_create(midRow);
        lv_obj_set_size(shapeDd, 235, 34);
        lv_dropdown_set_options(shapeDd, "Sine Wave\nTriangle\nSquare\nSawtooth\nRandom / S&H");
        lv_dropdown_set_selected(shapeDd, lfo.getShape());
        lv_obj_set_style_text_font(shapeDd, &lv_font_montserrat_12, 0);

        // Destination Button
        lv_obj_t* destBtn = lv_button_create(midRow);
        lv_obj_set_size(destBtn, 235, 34);
        lv_obj_set_style_bg_color(destBtn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_radius(destBtn, 6, 0);

        lv_obj_t* destLbl = lv_label_create(destBtn);
        mLfoDestBtnLabel[l] = destLbl;

        if (mLfoDestParamId[l] != -1) {
            std::string destName = getParameterNameString(mLfoDestTrack[l], mLfoDestParamId[l], &mEngine);
            lv_label_set_text(destLbl, destName.c_str());
        } else {
            lv_label_set_text(destLbl, "Select Destination");
        }
        lv_obj_set_style_text_font(destLbl, &lv_font_montserrat_12, 0);
        lv_obj_center(destLbl);

        ModDestModalData* clickData = new ModDestModalData{this, 2, l};
        lv_obj_add_event_cb(destBtn, openModDestModalEventCb, LV_EVENT_CLICKED, clickData);

        auto clickDataFreeCb = [](lv_event_t* e) {
            ModDestModalData* data = (ModDestModalData*)lv_event_get_user_data(e);
            delete data;
        };
        lv_obj_add_event_cb(destBtn, clickDataFreeCb, LV_EVENT_DELETE, clickData);

        // Arcs row (Depth and Rate)
        lv_obj_t* lfoControlsRow = lv_obj_create(lfoCard);
        lv_obj_set_size(lfoControlsRow, 486, 115);
        lv_obj_set_style_bg_opa(lfoControlsRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lfoControlsRow, 0, 0);
        lv_obj_set_style_pad_all(lfoControlsRow, 0, 0);
        lv_obj_set_layout(lfoControlsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(lfoControlsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(lfoControlsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // 1. Depth Arc
        lv_obj_t* depthGrp = lv_obj_create(lfoControlsRow);
        lv_obj_set_size(depthGrp, 200, 110);
        lv_obj_set_style_bg_opa(depthGrp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(depthGrp, 0, 0);
        lv_obj_set_style_pad_all(depthGrp, 0, 0);
        lv_obj_set_layout(depthGrp, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(depthGrp, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(depthGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* dLbl = lv_label_create(depthGrp);
        lv_label_set_text(dLbl, "Depth");
        lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(dLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* dArc = lv_arc_create(depthGrp);
        lv_obj_set_size(dArc, 72, 72);
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
        lv_obj_set_size(rateGrp, 200, 110);
        lv_obj_set_style_bg_opa(rateGrp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(rateGrp, 0, 0);
        lv_obj_set_style_pad_all(rateGrp, 0, 0);
        lv_obj_set_layout(rateGrp, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(rateGrp, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(rateGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* rLbl = lv_label_create(rateGrp);
        lv_label_set_text(rLbl, "Rate");
        lv_obj_set_style_text_font(rLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(rLbl, lv_color_hex(0x888888), 0);

        lv_obj_t* rArc = lv_arc_create(rateGrp);
        lv_obj_set_size(rArc, 72, 72);
        lv_arc_set_range(rArc, 0, 100);
        lv_arc_set_value(rArc, (int)(lfo.getUiRate() * 100));
        lv_obj_set_style_arc_color(rArc, trackColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(rArc, trackColor, LV_PART_KNOB);

        lv_obj_t* rVal = lv_label_create(rArc);
        if (lfo.getSync()) {
            int syncIdx = (int)(lfo.getUiRate() * 22.99f);
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
    }

    // =========================================================================
    // --- Tab 4: FX Pedal Serial Chaining ---
    // =========================================================================
    lv_obj_set_flex_flow(tab4, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab4, 20, 0);

    const char* FX_NAMES[17] = {
        "Overdrive", "Bitcrusher", "Chorus", "Phaser", "Tape Wobble",
        "Delay", "Reverb", "Slicer", "Compressor", "HP LFO Filter",
        "LP LFO Filter", "Flanger", "Filter Pedal 1", "Tape Echo", "Octaver",
        "Filter Pedal 2", "Filter Pedal 3"
    };

    for (int chainIdx = 0; chainIdx < 2; ++chainIdx) {
        lv_obj_t* chainCard = lv_obj_create(tab4);
        lv_obj_set_size(chainCard, lv_pct(100), 280);
        applyCardStyle(chainCard);
        lv_obj_set_layout(chainCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(chainCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chainCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* chainTitle = lv_label_create(chainCard);
        lv_label_set_text_fmt(chainTitle, "SERIAL FX CHAIN %d", chainIdx + 1);
        lv_obj_set_style_text_font(chainTitle, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(chainTitle, trackColor, 0);

        lv_obj_t* slotsRow = lv_obj_create(chainCard);
        lv_obj_set_size(slotsRow, lv_pct(100), 200);
        lv_obj_set_style_bg_opa(slotsRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(slotsRow, 0, 0);
        lv_obj_set_style_pad_all(slotsRow, 0, 0);
        lv_obj_set_layout(slotsRow, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(slotsRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(slotsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        for (int slotIdx = 0; slotIdx < 5; ++slotIdx) {
            int pedalId = mFxChainPedals[chainIdx][slotIdx];

            lv_obj_t* slotBtn = lv_button_create(slotsRow);
            lv_obj_set_size(slotBtn, 180, 160);
            lv_obj_set_style_radius(slotBtn, 12, 0);

            if (pedalId >= 0 && pedalId < 17) {
                lv_obj_set_style_bg_color(slotBtn, lv_color_hex(0x222222), 0);
                lv_obj_set_style_border_color(slotBtn, trackColor, 0);
                lv_obj_set_style_border_width(slotBtn, 2, 0);
            } else {
                lv_obj_set_style_bg_color(slotBtn, lv_color_hex(0x151515), 0);
                lv_obj_set_style_border_color(slotBtn, lv_color_hex(0x444444), 0);
                lv_obj_set_style_border_width(slotBtn, 1, 0);
            }

            lv_obj_t* slotLbl = lv_label_create(slotBtn);
            lv_label_set_text_fmt(slotLbl, "SLOT %d", slotIdx + 1);
            lv_obj_set_style_text_font(slotLbl, &lv_font_montserrat_12, 0);
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
            lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_14, 0);
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
                lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(arrow, lv_color_hex(0x666666), 0);
            }
        }
    }
}

void UIManager::rebuildActiveRoutings(lv_obj_t* parent) {
    if (!parent) return;
    if (mActiveNav != 4) return; // Only rebuild when active on the Assign/Modulation screen!
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
        int syncIdx = (int)(raw * 22.99f);
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
        int syncIdx = (int)(raw * 22.99f);
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
        int targetIdx;
    };
    RemapEventData* data = (RemapEventData*)lv_event_get_user_data(e);
    UIManager* ui = data->ui;

    ui->mRemapTargetIndex = data->targetIdx;

    if (ui->mControllerSetupActive) {
        ui->mControllerSetupTargetIndex = data->targetIdx;
        if (ui->mControllerSetupBtnLabel) {
            bool isKnob = (data->targetIdx < 40);
            int idx = isKnob ? data->targetIdx : (data->targetIdx - 40);
            lv_label_set_text_fmt(ui->mControllerSetupBtnLabel, "WIGGLE HARDWARE CONTROLLER TO ASSIGN TO %s %d", 
                                  isKnob ? "KNOB" : "SLIDER", idx + 1);
        }
        return;
    }

    // Renders custom remapping modal
    ui->mRemapModal = lv_obj_create(ui->mMainScreen);
    lv_obj_set_size(ui->mRemapModal, 420, 500);
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
    if (ui->mRemapTargetIndex < 40) {
        lv_label_set_text_fmt(title, "REMAP KNOB %d", ui->mRemapTargetIndex + 1);
    } else {
        lv_label_set_text_fmt(title, "REMAP FADER %d", ui->mRemapTargetIndex - 39);
    }
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

    // LEARN Button
    lv_obj_t* learnBtn = lv_button_create(ui->mRemapModal);
    lv_obj_set_size(learnBtn, 120, 32);
    lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_radius(learnBtn, 6, 0);
    lv_obj_t* learnLbl = lv_label_create(learnBtn);
    lv_label_set_text(learnLbl, "LEARN");
    lv_obj_set_style_text_font(learnLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(learnLbl);
    ui->mRemapLearnBtnLbl = learnLbl;
    ui->mRemapLearnActive = false;

    auto learnCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->mRemapLearnActive = !ui->mRemapLearnActive;
        if (ui->mRemapLearnActive) {
            lv_label_set_text(ui->mRemapLearnBtnLbl, "LISTENING...");
            lv_obj_set_style_bg_color(lv_obj_get_parent(ui->mRemapLearnBtnLbl), lv_color_hex(0xFF0000), 0);
        } else {
            lv_label_set_text(ui->mRemapLearnBtnLbl, "LEARN");
            lv_obj_set_style_bg_color(lv_obj_get_parent(ui->mRemapLearnBtnLbl), lv_color_hex(0x2D2D2D), 0);
        }
    };
    lv_obj_add_event_cb(learnBtn, learnCb, LV_EVENT_CLICKED, ui);

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
    
    int currentCc = (ui->mRemapTargetIndex < 40) ? ui->mSeqMidiKnobCC[ui->mActiveTrack][ui->mRemapTargetIndex]
                                               : ui->mSeqMidiFaderCC[ui->mActiveTrack][ui->mRemapTargetIndex - 40];
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
    ui->mRemapCcValLbl = ccValLbl;

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

    // Channel selection row
    lv_obj_t* chRow = lv_obj_create(ui->mRemapModal);
    lv_obj_set_size(chRow, 380, 45);
    lv_obj_set_style_bg_opa(chRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chRow, 0, 0);
    lv_obj_set_style_pad_all(chRow, 0, 0);
    lv_obj_set_layout(chRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* chLbl = lv_label_create(chRow);
    lv_label_set_text(chLbl, "MIDI Channel");
    lv_obj_set_style_text_font(chLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chLbl, lv_color_hex(0x888888), 0);

    ui->mRemapChannelDd = lv_dropdown_create(chRow);
    lv_obj_set_size(ui->mRemapChannelDd, 120, 32);
    lv_dropdown_set_options(ui->mRemapChannelDd, "All\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16");
    int currentChannel = (ui->mRemapTargetIndex < 40) ? ui->mSeqMidiKnobChannel[ui->mActiveTrack][ui->mRemapTargetIndex]
                                                     : ui->mSeqMidiFaderChannel[ui->mActiveTrack][ui->mRemapTargetIndex - 40];
    lv_dropdown_set_selected(ui->mRemapChannelDd, currentChannel);
    lv_obj_set_style_bg_color(ui->mRemapChannelDd, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_font(ui->mRemapChannelDd, &lv_font_montserrat_12, 0);

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
    lv_obj_set_size(listContainer, 380, 150);
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

    int currentParam = (ui->mRemapTargetIndex < 40) ? ui->mSeqMidiKnobParam[ui->mActiveTrack][ui->mRemapTargetIndex]
                                                  : ui->mSeqMidiFaderParam[ui->mActiveTrack][ui->mRemapTargetIndex - 40];
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
    int newCh = lv_dropdown_get_selected(ui->mRemapChannelDd);
    int paramId = ui->mRemapSelectedParamId;

    if (ui->mRemapTargetIndex < 40) {
        for (int t = 0; t < 8; ++t) {
            ui->mSeqMidiKnobCC[t][ui->mRemapTargetIndex] = newCc;
            ui->mSeqMidiKnobChannel[t][ui->mRemapTargetIndex] = newCh;
        }
        ui->mSeqMidiKnobParam[ui->mActiveTrack][ui->mRemapTargetIndex] = paramId;
    } else {
        for (int t = 0; t < 8; ++t) {
            ui->mSeqMidiFaderCC[t][ui->mRemapTargetIndex - 40] = newCc;
            ui->mSeqMidiFaderChannel[t][ui->mRemapTargetIndex - 40] = newCh;
        }
        ui->mSeqMidiFaderParam[ui->mActiveTrack][ui->mRemapTargetIndex - 40] = paramId;
    }

    // Refresh center screen
    ui->createCenterContentArea();

    // Close Modal
    ui->closeRemapModalEventCb(e);
}

void UIManager::closeRemapModalEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui && ui->mRemapModal) {
        ui->mRemapLearnActive = false;
        lv_obj_delete(ui->mRemapModal);
        ui->mRemapModal = nullptr;
        ui->mRemapChannelDd = nullptr;
        ui->mRemapCcValLbl = nullptr;
        ui->mRemapLearnBtnLbl = nullptr;
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
        lv_obj_set_size(catBtn, 195, 38);
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
        lv_obj_set_style_text_font(catLbl, &lv_font_montserrat_12, 0);
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
            lv_obj_set_size(pBtn, 132, 44);
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
            lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_12, 0);
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
            lv_obj_set_size(pBtn, 132, 44);
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
            lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_12, 0);
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
    } else if (data->callerType == 4) { // Play Mod X (standard or Split Left)
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            ui->mModDestBtnLabel = ui->mPlaySplitLeftModXLbl;
            ui->mModDestTrack = ui->mPlaySplitLeftModXTrack;
            ui->mModDestParamId = ui->mPlaySplitLeftModXDest;
        } else {
            ui->mModDestBtnLabel = ui->mPlayModXDestLbl;
            ui->mModDestTrack = ui->mPlayModXTrack;
            ui->mModDestParamId = ui->mPlayModXDest;
        }
    } else if (data->callerType == 5) { // Play Mod Y (standard or Split Left)
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            ui->mModDestBtnLabel = ui->mPlaySplitLeftModYLbl;
            ui->mModDestTrack = ui->mPlaySplitLeftModYTrack;
            ui->mModDestParamId = ui->mPlaySplitLeftModYDest;
        } else {
            ui->mModDestBtnLabel = ui->mPlayModYDestLbl;
            ui->mModDestTrack = ui->mPlayModYTrack;
            ui->mModDestParamId = ui->mPlayModYDest;
        }
    } else if (data->callerType == 6) { // Play Mod X (Split Right)
        ui->mModDestBtnLabel = ui->mPlaySplitRightModXLbl;
        ui->mModDestTrack = ui->mPlaySplitRightModXTrack;
        ui->mModDestParamId = ui->mPlaySplitRightModXDest;
    } else if (data->callerType == 7) { // Play Mod Y (Split Right)
        ui->mModDestBtnLabel = ui->mPlaySplitRightModYLbl;
        ui->mModDestTrack = ui->mPlaySplitRightModYTrack;
        ui->mModDestParamId = ui->mPlaySplitRightModYDest;
    } else {
        ui->mModDestBtnLabel = nullptr;
    }

    bool isFx = ((ui->mModDestParamId >= 490 && ui->mModDestParamId < 600) || 
                 (ui->mModDestParamId >= 1500 && ui->mModDestParamId < 1600) || 
                 (ui->mModDestParamId >= 2200 && ui->mModDestParamId < 2215));
    ui->mModDestType = isFx ? 6 : 5;

    ui->mModDestModal = lv_obj_create(ui->mMainScreen);
    lv_obj_set_size(ui->mModDestModal, 860, 620);
    lv_obj_center(ui->mModDestModal);
    lv_obj_set_style_bg_color(ui->mModDestModal, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_border_color(ui->mModDestModal, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_border_width(ui->mModDestModal, 2, 0);
    lv_obj_set_style_radius(ui->mModDestModal, 16, 0);
    lv_obj_set_style_pad_all(ui->mModDestModal, 18, 0);
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
    lv_obj_set_size(splitRow, 820, 480);
    lv_obj_set_style_bg_opa(splitRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(splitRow, 0, 0);
    lv_obj_set_style_pad_all(splitRow, 0, 0);
    lv_obj_set_layout(splitRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(splitRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(splitRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left Container (Holds Toggle Row + Left Col Category List)
    lv_obj_t* leftContainer = lv_obj_create(splitRow);
    lv_obj_set_size(leftContainer, 220, 475);
    lv_obj_set_style_bg_opa(leftContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftContainer, 0, 0);
    lv_obj_set_style_pad_all(leftContainer, 0, 0);
    lv_obj_set_layout(leftContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Toggle Row (Tracks / FX)
    lv_obj_t* toggleRow = lv_obj_create(leftContainer);
    lv_obj_set_size(toggleRow, 220, 36);
    lv_obj_set_style_bg_opa(toggleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(toggleRow, 0, 0);
    lv_obj_set_style_pad_all(toggleRow, 0, 0);
    lv_obj_set_layout(toggleRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(toggleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toggleRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* tracksToggleBtn = lv_button_create(toggleRow);
    lv_obj_set_size(tracksToggleBtn, 106, 32);
    lv_obj_set_style_radius(tracksToggleBtn, 6, 0);
    lv_obj_t* tracksToggleLbl = lv_label_create(tracksToggleBtn);
    lv_label_set_text(tracksToggleLbl, "TRACKS");
    lv_obj_set_style_text_font(tracksToggleLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(tracksToggleLbl);

    lv_obj_t* fxToggleBtn = lv_button_create(toggleRow);
    lv_obj_set_size(fxToggleBtn, 106, 32);
    lv_obj_set_style_radius(fxToggleBtn, 6, 0);
    lv_obj_t* fxToggleLbl = lv_label_create(fxToggleBtn);
    lv_label_set_text(fxToggleLbl, "FX");
    lv_obj_set_style_text_font(fxToggleLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(fxToggleLbl);

    // Left Column: Category List (scrolling vertical column)
    lv_obj_t* leftCol = lv_obj_create(leftContainer);
    lv_obj_set_size(leftCol, 220, 430);
    lv_obj_set_style_bg_color(leftCol, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(leftCol, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(leftCol, 1, 0);
    lv_obj_set_style_radius(leftCol, 8, 0);
    lv_obj_set_style_pad_all(leftCol, 6, 0);
    lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(leftCol, 6, 0);
    lv_obj_add_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);

    // Right Column: Parameters scrolling grid
    lv_obj_t* rightCol = lv_obj_create(splitRow);
    lv_obj_set_size(rightCol, 590, 475);
    lv_obj_set_style_bg_color(rightCol, lv_color_hex(0x151515), 0);
    lv_obj_set_style_border_color(rightCol, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(rightCol, 1, 0);
    lv_obj_set_style_radius(rightCol, 8, 0);
    lv_obj_set_style_pad_all(rightCol, 10, 0);
    lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(rightCol, 10, 0);
    lv_obj_set_style_pad_row(rightCol, 10, 0);
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

    // Button Row (Cancel + Clear)
    lv_obj_t* btnRow = lv_obj_create(ui->mModDestModal);
    lv_obj_set_size(btnRow, 820, 48);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_remove_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Cancel Button
    lv_obj_t* cancelBtn = lv_button_create(btnRow);
    lv_obj_set_size(cancelBtn, 180, 40);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "CANCEL");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);
    lv_obj_add_event_cb(cancelBtn, closeModDestModalEventCb, LV_EVENT_CLICKED, ui);

    // Clear Button
    lv_obj_t* clearBtn = lv_button_create(btnRow);
    lv_obj_set_size(clearBtn, 180, 40);
    lv_obj_set_style_bg_color(clearBtn, lv_color_hex(0xD9534F), 0); // Red
    lv_obj_set_style_radius(clearBtn, 8, 0);
    lv_obj_t* clearLbl = lv_label_create(clearBtn);
    lv_label_set_text(clearLbl, "CLEAR");
    lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(clearLbl);
    lv_obj_add_event_cb(clearBtn, clearModDestModalEventCb, LV_EVENT_CLICKED, ui);
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
        } else if (ui->mModDestModalCallerType == 4 || ui->mModDestModalCallerType == 6) {
            std::string compactName = getCompactDestName(ui->mModDestTrack, ui->mModDestParamId, &(ui->mEngine));
            lv_label_set_text_fmt(ui->mModDestBtnLabel, "X: %s", compactName.c_str());
        } else if (ui->mModDestModalCallerType == 5 || ui->mModDestModalCallerType == 7) {
            std::string compactName = getCompactDestName(ui->mModDestTrack, ui->mModDestParamId, &(ui->mEngine));
            lv_label_set_text_fmt(ui->mModDestBtnLabel, "Y: %s", compactName.c_str());
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
    } else if (ui->mModDestModalCallerType == 4) {
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            ui->mPlaySplitLeftModXTrack = data->trackIdx;
            ui->mPlaySplitLeftModXDest = data->paramId;
            ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                         ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
        } else {
            ui->mPlayModXTrack = data->trackIdx;
            ui->mPlayModXDest = data->paramId;
            ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                         ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
        }
    } else if (ui->mModDestModalCallerType == 5) {
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            ui->mPlaySplitLeftModYTrack = data->trackIdx;
            ui->mPlaySplitLeftModYDest = data->paramId;
            ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                         ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
        } else {
            ui->mPlayModYTrack = data->trackIdx;
            ui->mPlayModYDest = data->paramId;
            ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                         ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
        }
    } else if (ui->mModDestModalCallerType == 6) {
        ui->mPlaySplitRightModXTrack = data->trackIdx;
        ui->mPlaySplitRightModXDest = data->paramId;
        ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                     ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
    } else if (ui->mModDestModalCallerType == 7) {
        ui->mPlaySplitRightModYTrack = data->trackIdx;
        ui->mPlaySplitRightModYDest = data->paramId;
        ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                     ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
    }

    if (ui->mActiveNav == 4 && ui->mActiveRoutingsContainer) {
        ui->rebuildActiveRoutings(ui->mActiveRoutingsContainer);
    }

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

void UIManager::clearModDestModalEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;

    if (ui->mModDestModalCallerType == 1) { // Macro
        int m = ui->mModDestModalCallerIdx;
        int d = ui->mModDestModalCallerSlot;
        int prevParam = ui->mMacroDestParamId[m][d];
        int prevTrack = ui->mMacroDestTrack[m][d];
        if (prevParam != -1) {
            ui->mEngine.setRouting(prevTrack, ui->mActiveTrack, 10 + m, 5, 0.0f, prevParam);
        }
        ui->mMacroDestParamId[m][d] = -1;
        ui->mMacroDestTrack[m][d] = 0;
        ui->mMacroDestType[m][d] = 5;
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "---");
        }
    } else if (ui->mModDestModalCallerType == 2) { // LFO
        int l = ui->mModDestModalCallerIdx;
        int prevParam = ui->mLfoDestParamId[l];
        int prevTrack = ui->mLfoDestTrack[l];
        if (prevParam != -1) {
            ui->mEngine.setRouting(prevTrack, ui->mActiveTrack, 2 + l, 5, 0.0f, prevParam);
        }
        ui->mLfoDestParamId[l] = -1;
        ui->mLfoDestTrack[l] = 0;
        ui->mLfoDestType[l] = 5;
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "---");
        }
    } else if (ui->mModDestModalCallerType == 3) { // Aftertouch
        int prevParam = ui->mAftertouchDestParamId[ui->mActiveTrack];
        if (prevParam != -1) {
            ui->mEngine.setRouting(ui->mActiveTrack, ui->mActiveTrack, 27, 5, 0.0f, prevParam);
        }
        ui->mAftertouchDestParamId[ui->mActiveTrack] = -1;
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "AFTERTOUCH DEST: NONE (TAP TO ASSIGN)");
        }
    } else if (ui->mModDestModalCallerType == 4) { // Play Mod X
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            ui->mPlaySplitLeftModXDest = -1;
            ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                         ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
        } else {
            ui->mPlayModXDest = -1;
            ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                         ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
        }
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "X: None");
        }
    } else if (ui->mModDestModalCallerType == 5) { // Play Mod Y
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            ui->mPlaySplitLeftModYDest = -1;
            ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                         ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
        } else {
            ui->mPlayModYDest = -1;
            ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                         ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
        }
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "Y: None");
        }
    } else if (ui->mModDestModalCallerType == 6) { // Play Mod X (Split Right)
        ui->mPlaySplitRightModXDest = -1;
        ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                     ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "X: None");
        }
    } else if (ui->mModDestModalCallerType == 7) { // Play Mod Y (Split Right)
        ui->mPlaySplitRightModYDest = -1;
        ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                     ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
        if (ui->mModDestBtnLabel) {
            lv_label_set_text(ui->mModDestBtnLabel, "Y: None");
        }
    }

    if (ui->mActiveNav == 4 && ui->mActiveRoutingsContainer) {
        ui->rebuildActiveRoutings(ui->mActiveRoutingsContainer);
    }

    if (ui->mModDestModal) {
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

    if (mModDestModalCallerType == 3) {
        params.push_back({2400, "Ratchet"});
    }

    if (engineType == 0) { // Subtractive
        // Filter & Macros right at the top
        params.push_back({1, "Cutoff"});
        params.push_back({2, "Reson"});
        params.push_back({118, "Env Amount"});
        params.push_back({290, "Morphx3"});
        params.push_back({291, "Foldx3"});
        params.push_back({292, "Drivex3"});

        // Oscillator 1
        params.push_back({104, "Osc1 Morph"});
        params.push_back({170, "Osc1 Drive"});
        params.push_back({180, "Osc1 Fold"});
        params.push_back({160, "Osc1 Pitch"});
        params.push_back({107, "Osc1 Vol"});

        // Oscillator 2
        params.push_back({105, "Osc2 Morph"});
        params.push_back({171, "Osc2 Drive"});
        params.push_back({181, "Osc2 Fold"});
        params.push_back({161, "Osc2 Pitch"});
        params.push_back({108, "Osc2 Vol"});

        // Sub Oscillator
        params.push_back({155, "Sub Morph"});
        params.push_back({172, "Sub Drive"});
        params.push_back({182, "Sub Fold"});
        params.push_back({162, "Sub Pitch"});
        params.push_back({109, "Sub Vol"});

        // Utilities & LFO
        params.push_back({106, "Detune"});
        params.push_back({110, "Noise Vol"});
        params.push_back({355, "Glide"});
        params.push_back({7, "LFO Rate"});
        params.push_back({8, "LFO Depth"});

        // Envelopes
        params.push_back({100, "Amp A"});
        params.push_back({101, "Amp D"});
        params.push_back({102, "Amp S"});
        params.push_back({103, "Amp R"});
        params.push_back({114, "Filt A"});
        params.push_back({115, "Filt D"});
        params.push_back({116, "Filt S"});
        params.push_back({117, "Filt R"});
    } else if (engineType == 1) { // FM
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
    } else if (engineType == 2) { // Sampler
        params.push_back({1, "Cutoff"});
        params.push_back({2, "Resonance"});
        params.push_back({314, "Env Amt"});
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
    } else if (engineType == 3) { // Granular
        params.push_back({1, "Cutoff"});
        params.push_back({2, "Resonance"});
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
    } else if (engineType == 4) { // Wavetable
        params.push_back({458, "Cutoff"});
        params.push_back({459, "Resonance"});
        params.push_back({464, "Env Amt"});
        params.push_back({450, "Morph"});
        params.push_back({465, "Warp"});
        params.push_back({466, "Crush"});
        params.push_back({467, "Drive"});
        params.push_back({451, "Detune"});
        params.push_back({355, "Glide"});
        params.push_back({475, "Bitrate"});
        params.push_back({476, "Samplerate"});
        params.push_back({477, "WT Select"});
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
        // PERC
        params.push_back({660, "PERC Decay"});
        params.push_back({661, "PERC Tone"});
        params.push_back({662, "PERC Tune"});
        params.push_back({665, "PERC Gain"});
        // NOISE
        params.push_back({670, "NOISE Decay"});
        params.push_back({671, "NOISE Tone"});
        params.push_back({675, "NOISE Gain"});
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
    mParamTabview = tabview;
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
        addPedalKnob(pha, "Rate", 550, 0.0f, 1.0f);
        addPedalKnob(pha, "Depth", 551, 0.0f, 1.0f);
        addPedalKnob(pha, "Feedback", 553, 0.0f, 0.95f);
        addPedalToggle(pha, "Sync", 554);
        addPedalKnob(pha, "Mix", 552, 0.0f, 1.0f);
        addPedalKnob(pha, "Send", 2030, 0.0f, 1.0f);

        lv_obj_t* fla = createPedalCard(tab2, "FLANGER", lv_color_hex(0x06B6D4)); // Cyan
        addPedalKnob(fla, "Rate", 1500, 0.0f, 1.0f);
        addPedalKnob(fla, "Depth", 1501, 0.0f, 1.0f);
        addPedalKnob(fla, "Feedback", 1503, 0.0f, 0.95f);
        addPedalToggle(fla, "Sync", 1505);
        addPedalKnob(fla, "Delay", 1504, 0.0f, 1.0f);
        addPedalKnob(fla, "Mix", 1502, 0.0f, 1.0f);
        addPedalKnob(fla, "Send", 2110, 0.0f, 1.0f);
    }

    // Tab 3: Time & Space
    {
        lv_obj_t* echo = createPedalCard(tab3, "TAPE ECHO", lv_color_hex(0xF97316)); // Warm Orange
        addPedalKnob(echo, "Time", 1510, 0.0f, 1.0f);
        addPedalKnob(echo, "Feedback", 1511, 0.0f, 0.99f);
        addPedalKnob(echo, "Drive", 1513, 0.0f, 2.0f);
        addPedalKnob(echo, "Wow", 1514, 0.0f, 1.0f);
        addPedalKnob(echo, "Flutter", 1515, 0.0f, 1.0f);
        addPedalToggle(echo, "Sync", 1516);
        addPedalKnob(echo, "Mix", 1512, 0.0f, 1.0f);
        addPedalKnob(echo, "Send", 2130, 0.0f, 1.0f);

        lv_obj_t* del = createPedalCard(tab3, "DELAY", lv_color_hex(0xE0F2FE)); // Light Blue
        addPedalKnob(del, "Time", 520, 0.0f, 1.0f);
        addPedalKnob(del, "Feedback", 521, 0.0f, 0.99f);
        addPedalDropdown(del, "Type", 525, "Digital\nAnalog\nTape\nPingPong");
        addPedalKnob(del, "Cutoff", 523, 0.0f, 1.0f);
        addPedalKnob(del, "Filt Res", 524, 0.0f, 0.95f);
        addPedalDropdown(del, "Filt Mode", 526, "LP\nHP\nBP");
        addPedalToggle(del, "Sync", 527);
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
        addPedalKnob(lplfo, "Rate", 490, 0.0f, 1.0f);
        addPedalKnob(lplfo, "Depth", 491, 0.0f, 1.0f);
        addPedalDropdown(lplfo, "Shape", 492, "Sine\nTri\nSaw\nSquare\nS&H");
        addPedalKnob(lplfo, "Cutoff", 493, 20.0f, 20000.0f, 0);
        addPedalKnob(lplfo, "Reson", 494, 0.0f, 0.95f);
        addPedalToggle(lplfo, "Sync", 495);
        addPedalKnob(lplfo, "Send", 2100, 0.0f, 1.0f);

        lv_obj_t* hplfo = createPedalCard(tab5, "HP LFO FILTER", lv_color_hex(0xF43F5E)); // Vivid Rose
        addPedalKnob(hplfo, "Rate", 1590, 0.0f, 1.0f);
        addPedalKnob(hplfo, "Depth", 1591, 0.0f, 1.0f);
        addPedalDropdown(hplfo, "Shape", 1592, "Sine\nTri\nSaw\nSquare\nS&H");
        addPedalKnob(hplfo, "Cutoff", 1593, 20.0f, 20000.0f, 0);
        addPedalKnob(hplfo, "Reson", 1594, 0.0f, 0.95f);
        addPedalToggle(hplfo, "Sync", 1595);
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
        lv_obj_set_size(slidersCont, 305, 240);
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
        lv_obj_set_size(eqLine, 305, 2);
        lv_obj_set_style_bg_color(eqLine, lv_color_hex(0xF59E0B), 0);
        lv_obj_set_style_bg_opa(eqLine, LV_OPA_20, 0);
        lv_obj_set_style_border_width(eqLine, 0, 0);
        lv_obj_align(eqLine, LV_ALIGN_TOP_MID, 0, 270);

        // Bottom knobs container: Mix & Send Knobs side-by-side
        lv_obj_t* mixCont = lv_obj_create(eq);
        lv_obj_set_size(mixCont, 305, 220);
        lv_obj_set_style_bg_opa(mixCont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(mixCont, 0, 0);
        lv_obj_set_style_pad_all(mixCont, 0, 0);
        lv_obj_set_layout(mixCont, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(mixCont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(mixCont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(mixCont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(mixCont, LV_ALIGN_TOP_MID, 0, 290);

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

    if (mParamTabview && mParamActiveTabIdx >= 0 && mParamActiveTabIdx < 6) {
        lv_tabview_set_active(mParamTabview, mParamActiveTabIdx, LV_ANIM_OFF);
    }
}

lv_obj_t* UIManager::createPedalCard(lv_obj_t* parent, const char* name, lv_color_t accentColor) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 335, 700);
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
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, accentColor, 0);
    
    // Separator line
    lv_obj_t* line = lv_obj_create(card);
    lv_obj_set_size(line, 305, 2);
    lv_obj_set_style_bg_color(line, accentColor, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    lv_obj_set_style_border_width(line, 0, 0);

    // Controls Body container
    lv_obj_t* body = lv_obj_create(card);
    lv_obj_set_size(body, 305, 610);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(body, 10, 0);
    lv_obj_set_style_pad_row(body, 22, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    return body;
}

void UIManager::addPedalKnob(lv_obj_t* pedal, const char* labelText, int paramId, float minVal, float maxVal, int decimals) {
    lv_obj_t* container = lv_obj_create(pedal);
    lv_obj_set_size(container, 82, 115);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Mini Arc
    lv_obj_t* arc = lv_arc_create(container);
    lv_obj_set_size(arc, 68, 68);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
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
    lv_obj_set_size(container, 64, 55);
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
    lv_obj_set_size(container, 88, 58);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Dropdown
    lv_obj_t* dd = lv_dropdown_create(container);
    lv_obj_set_size(dd, 84, 30);
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
    lv_obj_set_size(spacer, 350, height);
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

    if (d->paramId == 527 || d->paramId == 1516 || d->paramId == 1505 || d->paramId == 554 || d->paramId == 495 || d->paramId == 1595) {
        d->ui->mNeedsScreenRebuild = true;
    }

    // Update UI labels if needed
    if (d->valLbl) {
        bool isSynced = false;
        int syncIdx = -1;
        if (d->paramId == 520) {
            isSynced = d->ui->mEngine.getTracks()[0].parameters[527] >= 0.5f;
            syncIdx = (int)(rawVal * 19.99f);
        } else if (d->paramId == 1510) {
            isSynced = d->ui->mEngine.getTracks()[0].parameters[1516] >= 0.5f;
            syncIdx = (int)(rawVal * 19.99f);
        } else if (d->paramId == 1500) {
            isSynced = d->ui->mEngine.getTracks()[0].parameters[1505] >= 0.5f;
            syncIdx = (int)(rawVal * 19.99f);
        } else if (d->paramId == 550) {
            isSynced = d->ui->mEngine.getTracks()[0].parameters[554] >= 0.5f;
            syncIdx = (int)(rawVal * 19.99f);
        } else if (d->paramId == 490) {
            isSynced = d->ui->mEngine.getTracks()[0].parameters[495] >= 0.5f;
            syncIdx = (int)(rawVal * 22.99f);
        } else if (d->paramId == 1590) {
            isSynced = d->ui->mEngine.getTracks()[0].parameters[1595] >= 0.5f;
            syncIdx = (int)(rawVal * 22.99f);
        }

        if (isSynced) {
            if (d->paramId == 490 || d->paramId == 1590) {
                lv_label_set_text(d->valLbl, getLfoSyncLabel(syncIdx));
            } else {
                const char* delaySyncLabels[] = {"1/32", "1/24", "1/5", "1/16", "1/12", "1/16D", "1/8", "1/4T", "1/8D", "4/5", "1/4", "1/2T", "4/3", "3/8", "1/2", "5/8", "7/8", "1/1", "5/4", "3/2"};
                lv_label_set_text(d->valLbl, delaySyncLabels[syncIdx % 20]);
            }
        } else {
            float dispVal = rawVal;
            if (d->paramId == 520 || d->paramId == 1510) {
                dispVal = 0.01f + rawVal * 1.99f;
            } else if (d->paramId == 1500) {
                dispVal = 0.05f + rawVal * 4.95f;
            } else if (d->paramId == 550) {
                dispVal = 0.1f + rawVal * 9.9f;
            } else if (d->paramId == 490 || d->paramId == 1590) {
                dispVal = 0.05f + rawVal * 19.95f;
            }

            if (d->decimals == -1) {
                float db = (rawVal - 0.5f) * 48.0f;
                lv_label_set_text_fmt(d->valLbl, "%d", (int)db);
            } else if (d->decimals == 0) {
                lv_label_set_text_fmt(d->valLbl, "%d", (int)dispVal);
            } else if (d->decimals == 1) {
                lv_label_set_text_fmt(d->valLbl, "%.1f", dispVal);
            } else {
                lv_label_set_text_fmt(d->valLbl, "%.2f", dispVal);
            }
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
        LV_SYMBOL_AUDIO " SoundFont\n"
        LV_SYMBOL_USB " MIDI"
    );
    lv_obj_set_style_bg_color(engDd, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(engDd, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(engDd, 1, 0);
    lv_obj_set_style_text_color(engDd, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(engDd, &lv_font_montserrat_12, 0);

    // Style the dropdown list dynamically so it fits all 10 items on screen perfectly without scrolling
    lv_obj_t* engList = lv_dropdown_get_list(engDd);
    lv_obj_set_style_max_height(engList, 350, 0);
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
    } else if (currentEngineType == 10) {
        ddIndex = 9;
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
    } else if (selected == 9) {
        engineType = 10;
    }
    d->ui->mEngine.setEngineType(d->trackIdx, engineType);
    d->ui->applyDefaultMidiMappings(d->trackIdx, engineType);
    d->ui->updateHighlighting();
    d->ui->createCenterContentArea();
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

    // Single unified container (replacing tabview)
    lv_obj_t* container = lv_obj_create(mCenterArea);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Register delete callback to safely nullify widget pointers
    lv_obj_add_event_cb(container, mixRecScreenDeleteEventCb, LV_EVENT_DELETE, this);

    // Outer vertical flex: Top Card (Tempo & Master Transport) + Bottom Card (8-Track Mixer)
    lv_obj_t* outerCol = lv_obj_create(container);
    lv_obj_set_size(outerCol, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(outerCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outerCol, 0, 0);
    lv_obj_set_style_pad_all(outerCol, 0, 0);
    lv_obj_set_layout(outerCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(outerCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(outerCol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(outerCol, LV_OBJ_FLAG_SCROLLABLE);

    // Glassmorphic Card Styling helper
    auto applyCardStyle = [](lv_obj_t* card) {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    };

    // =========================================================================
    // TOP CARD: TEMPO, MASTER VOLUME & TRANSPORT (Width: 1050px, Height: 260px)
    // =========================================================================
    lv_obj_t* topCard = lv_obj_create(outerCol);
    lv_obj_set_size(topCard, 1050, 260);
    applyCardStyle(topCard);
    lv_obj_set_style_pad_all(topCard, 10, 0);
    lv_obj_set_layout(topCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Section 1 (Left of Top Card): Tempo & Swing Section (Width: 500px)
    lv_obj_t* tempoSection = lv_obj_create(topCard);
    lv_obj_set_size(tempoSection, 500, 238);
    lv_obj_set_style_bg_opa(tempoSection, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tempoSection, 0, 0);
    lv_obj_set_style_pad_all(tempoSection, 0, 0);
    lv_obj_set_layout(tempoSection, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tempoSection, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tempoSection, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(tempoSection, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title1 = lv_label_create(tempoSection);
    lv_label_set_text(title1, "TEMPO & SWING");
    lv_obj_set_style_text_font(title1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title1, lv_color_hex(0x888888), 0);

    // Row containing Knobs on left, Tap Tempo button on right
    lv_obj_t* tempoKnobsRow = lv_obj_create(tempoSection);
    lv_obj_set_size(tempoKnobsRow, 500, 200);
    lv_obj_set_style_bg_opa(tempoKnobsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tempoKnobsRow, 0, 0);
    lv_obj_set_style_pad_all(tempoKnobsRow, 0, 0);
    lv_obj_set_layout(tempoKnobsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tempoKnobsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tempoKnobsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(tempoKnobsRow, LV_OBJ_FLAG_SCROLLABLE);

    // 1. BPM Arc/Knob
    lv_obj_t* bpmCont = lv_obj_create(tempoKnobsRow);
    lv_obj_set_size(bpmCont, 150, 190);
    lv_obj_set_style_bg_opa(bpmCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bpmCont, 0, 0);
    lv_obj_set_style_pad_all(bpmCont, 0, 0);
    lv_obj_set_layout(bpmCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bpmCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bpmCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bpmCont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bpmArc = lv_arc_create(bpmCont);
    lv_obj_set_size(bpmArc, 130, 130);
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
    lv_obj_t* swingCont = lv_obj_create(tempoKnobsRow);
    lv_obj_set_size(swingCont, 150, 190);
    lv_obj_set_style_bg_opa(swingCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swingCont, 0, 0);
    lv_obj_set_style_pad_all(swingCont, 0, 0);
    lv_obj_set_layout(swingCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(swingCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(swingCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(swingCont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* swingArc = lv_arc_create(swingCont);
    lv_obj_set_size(swingArc, 130, 130);
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

    // TAP TEMPO Button placed to the right of the knobs
    lv_obj_t* tapBtn = lv_button_create(tempoKnobsRow);
    lv_obj_set_size(tapBtn, 150, 68);
    lv_obj_set_style_bg_color(tapBtn, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(tapBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tapBtn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(tapBtn, 1, 0);
    lv_obj_set_style_radius(tapBtn, 10, 0);

    lv_obj_t* tapBtnLbl = lv_label_create(tapBtn);
    lv_label_set_text(tapBtnLbl, "TAP TEMPO");
    lv_obj_set_style_text_font(tapBtnLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tapBtnLbl, trackColor, 0);
    lv_obj_center(tapBtnLbl);
    lv_obj_add_event_cb(tapBtn, mixRecBpmTapEventCb, LV_EVENT_CLICKED, this);

    // Vertical divider between sections
    lv_obj_t* vDivider = lv_obj_create(topCard);
    lv_obj_set_size(vDivider, 1, 230);
    lv_obj_set_style_bg_color(vDivider, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_opa(vDivider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vDivider, 0, 0);
    lv_obj_set_style_pad_all(vDivider, 0, 0);

    // Section 2 (Right of Top Card): Master Volume & Transport Buttons (Width: 500px)
    lv_obj_t* masterSection = lv_obj_create(topCard);
    lv_obj_set_size(masterSection, 500, 238);
    lv_obj_set_style_bg_opa(masterSection, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(masterSection, 0, 0);
    lv_obj_set_style_pad_all(masterSection, 0, 0);
    lv_obj_set_layout(masterSection, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(masterSection, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(masterSection, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(masterSection, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title2 = lv_label_create(masterSection);
    lv_label_set_text(title2, "MASTER VOLUME & TRANSPORT");
    lv_obj_set_style_text_font(title2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title2, lv_color_hex(0x888888), 0);

    // Row containing Master Volume Knob on left, Transport Buttons on right
    lv_obj_t* masterRow = lv_obj_create(masterSection);
    lv_obj_set_size(masterRow, 500, 200);
    lv_obj_set_style_bg_opa(masterRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(masterRow, 0, 0);
    lv_obj_set_style_pad_all(masterRow, 0, 0);
    lv_obj_set_layout(masterRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(masterRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(masterRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(masterRow, LV_OBJ_FLAG_SCROLLABLE);

    // Master Volume Arc Container
    lv_obj_t* volCont = lv_obj_create(masterRow);
    lv_obj_set_size(volCont, 170, 190);
    lv_obj_set_style_bg_opa(volCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(volCont, 0, 0);
    lv_obj_set_style_pad_all(volCont, 0, 0);
    lv_obj_set_layout(volCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(volCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(volCont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(volCont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* volArc = lv_arc_create(volCont);
    lv_obj_set_size(volArc, 130, 130);
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

    // Transport buttons row placed to the right of Master Volume knob
    lv_obj_t* transRow = lv_obj_create(masterRow);
    lv_obj_set_size(transRow, 290, 68);
    lv_obj_set_style_bg_opa(transRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(transRow, 0, 0);
    lv_obj_set_style_pad_all(transRow, 0, 0);
    lv_obj_set_layout(transRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(transRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(transRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(transRow, LV_OBJ_FLAG_SCROLLABLE);

    auto applyTransBtnStyle = [](lv_obj_t* btn) {
        lv_obj_set_size(btn, 88, 56);
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
    lv_obj_t* recCircle = lv_obj_create(recBtn);
    lv_obj_set_size(recCircle, 18, 18);
    lv_obj_set_style_bg_color(recCircle, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_bg_opa(recCircle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(recCircle, 0, 0);
    lv_obj_set_style_radius(recCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(recCircle);
    lv_obj_remove_flag(recCircle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(recBtn, mixRecRecordBtnEventCb, LV_EVENT_CLICKED, this);
    mMixRecRecordBtn = recBtn;

    // =========================================================================
    // BOTTOM CARD: 8-TRACK MIXER FADERS (Width: 1050px, Height: 505px)
    // =========================================================================
    lv_obj_t* mixerCard = lv_obj_create(outerCol);
    lv_obj_set_size(mixerCard, 1050, 505);
    applyCardStyle(mixerCard);
    lv_obj_set_style_pad_all(mixerCard, 10, 0);
    lv_obj_set_layout(mixerCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mixerCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mixerCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* mixerTitle = lv_label_create(mixerCard);
    lv_label_set_text(mixerTitle, "8-TRACK MIXER CONSOLE");
    lv_obj_set_style_text_font(mixerTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mixerTitle, lv_color_hex(0x888888), 0);

    lv_obj_t* mixerRow = lv_obj_create(mixerCard);
    lv_obj_set_size(mixerRow, 1030, 455);
    lv_obj_set_style_bg_opa(mixerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mixerRow, 0, 0);
    lv_obj_set_style_pad_all(mixerRow, 0, 0);
    lv_obj_set_layout(mixerRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mixerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mixerRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(mixerRow, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 8; ++i) {
        lv_color_t tColor = getTrackColor(i);

        lv_obj_t* sliderCard = lv_obj_create(mixerRow);
        mMixerCards[i] = sliderCard;
        lv_obj_set_size(sliderCard, 122, 450);
        applyCardStyle(sliderCard);
        lv_obj_set_style_pad_all(sliderCard, 6, 0);
        lv_obj_set_layout(sliderCard, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(sliderCard, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sliderCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Track title
        lv_obj_t* trackTitle = lv_label_create(sliderCard);
        lv_label_set_text_fmt(trackTitle, "Track %d", i + 1);
        lv_obj_set_style_text_font(trackTitle, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trackTitle, tColor, 0);

        // Parameter Value Label (0% to 150%)
        lv_obj_t* valLbl = lv_label_create(sliderCard);
        mMixerVolLabels[i] = valLbl;
        float currentVol = mEngine.getTracks()[i].volume;
        int pctVal = (int)(currentVol * 100.0f);
        lv_label_set_text_fmt(valLbl, "%d%%", pctVal);
        lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(valLbl, lv_color_hex(0xFFFFFF), 0);

        // Vertical Slider (fader) - 340px tall fader!
        lv_obj_t* slider = lv_slider_create(sliderCard);
        mMixerVolSliders[i] = slider;
        lv_obj_set_size(slider, 22, 340);
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
    
    // Placeholder guard if active track is not using Subtractive (0), FM (1), Sampler (2), Granular (3), Wavetable (4), SoundFont (9), Audio In (8), FM Drum (5), Analogue Drum (6), or MIDI Engine (10)
    if (engineType != 0 && engineType != 1 && engineType != 2 && engineType != 3 && engineType != 4 && engineType != 9 && engineType != 8 && engineType != 5 && engineType != 6 && engineType != 10) {
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
        lv_obj_set_size(tabview, lv_pct(100), 730);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "OSCILLATORS");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "FILTER, LFO & ENVELOPES");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamSubtractiveOscTab(tab1);
        populateParamSubtractiveFilterTab(tab2);
    } else if (engineType == 1) {
        // FM Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, lv_pct(100), 730);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "OPERATORS & ROUTING");
        lv_obj_t* tab2 = lv_tabview_add_tab(tabview, "FILTER & ENVELOPES");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        lv_obj_set_style_pad_all(tab2, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(tab2, LV_OBJ_FLAG_SCROLLABLE);

        populateParamFmTab(tab1, tab2);
    } else if (engineType == 2) {
        // Sampler Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, lv_pct(100), 730);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "SAMPLER & SYNTHESIS");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);

        populateParamSamplerTab(tab1);
    } else if (engineType == 3) {
        // Granular Active: Create tab view dashboard
        lv_obj_t* tabview = lv_tabview_create(mCenterArea);
        mParamTabview = tabview;
        lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
        lv_tabview_set_tab_bar_size(tabview, 40);
        lv_obj_set_size(tabview, lv_pct(100), 730);
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
        lv_obj_set_size(tabview, lv_pct(100), 730);
        lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(tabview, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(tabview, 0, 0);
        
        // Style the tabs bar
        lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
        lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(tab_bar, 1, LV_BORDER_SIDE_BOTTOM);

        lv_obj_t* tab1 = lv_tabview_add_tab(tabview, "WAVETABLE SYNTHESIS");

        lv_obj_set_style_pad_all(tab1, 10, 0);
        
        lv_obj_remove_flag(tab1, LV_OBJ_FLAG_SCROLLABLE);

        populateParamWavetableTab(tab1);
    } else if (engineType == 9) {
        // SoundFont Active: Single-page dashboard
        mParamTabview = nullptr;
        lv_obj_t* page = lv_obj_create(mCenterArea);
        lv_obj_set_size(page, lv_pct(100), 730);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 10, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

        populateParamSoundFontLibraryTab(page);
    } else if (engineType == 8) {
        // Audio In Active: Single-page dashboard
        mParamTabview = nullptr;
        lv_obj_t* page = lv_obj_create(mCenterArea);
        lv_obj_set_size(page, lv_pct(100), 730);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 10, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

        populateParamAudioInTab(page);
    } else if (engineType == 5) {
        // FM Drum Active: Single-page dashboard (all 8 voices side-by-side)
        mParamTabview = nullptr;
        lv_obj_t* page = lv_obj_create(mCenterArea);
        lv_obj_set_size(page, lv_pct(100), 730);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 6, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

        populateParamFmDrumTab1(page);
    } else if (engineType == 6) {
        // Analogue Drum Active: Single-page dashboard (all 8 voices side-by-side)
        mParamTabview = nullptr;
        lv_obj_t* page = lv_obj_create(mCenterArea);
        lv_obj_set_size(page, lv_pct(100), 730);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 6, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

        populateParamAnalogDrumTab1(page);
    } else if (engineType == 10) {
        // MIDI Engine Active: Single-page dashboard (Routing on top, Controller Mapping list below)
        mParamTabview = nullptr;
        lv_obj_t* page = lv_obj_create(mCenterArea);
        lv_obj_set_size(page, lv_pct(100), 730);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(page, lv_color_hex(0x121212), 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 8, 0);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

        populateParamMidiRoutingTab(page);
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
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_column(tab, 8, 0);

    auto createSynthCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 410);
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

    auto createKnobGrid = [](lv_obj_t* parent) -> lv_obj_t* {
        lv_obj_t* grid = lv_obj_create(parent);
        lv_obj_set_size(grid, 156, 240);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
        lv_obj_set_style_pad_row(grid, 10, 0);
        lv_obj_set_style_pad_column(grid, 4, 0);
        return grid;
    };

    // --- MACROS ---
    lv_obj_t* macroCard = createSynthCard(tab, "MACROS", 95);
    lv_obj_set_style_pad_all(macroCard, 4, 0);
    addSynthKnob(macroCard, "MORPHX3", 290, 0.0f, 1.0f, 2, true);
    addSynthKnob(macroCard, "FOLDX3", 291, 0.0f, 1.0f, 2, true);
    addSynthKnob(macroCard, "DRIVEX3", 292, 0.0f, 1.0f, 2, true);

    // --- OSC 1 ---
    lv_obj_t* card1 = createSynthCard(tab, "OSCILLATOR 1", 175);
    lv_obj_t* grid1 = createKnobGrid(card1);
    addSynthKnob(grid1, "PITCH", 160, 0.0f, 1.0f, 2, false);
    addSynthKnob(grid1, "MORPH", 104, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid1, "DRIVE", 170, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid1, "FOLD", 180, 0.0f, 1.0f, 2, true);
    addSynthKnob(card1, "VOLUME", 107, 0.0f, 1.0f, 2, true);

    // --- OSC 2 ---
    lv_obj_t* card2 = createSynthCard(tab, "OSCILLATOR 2", 175);
    lv_obj_t* grid2 = createKnobGrid(card2);
    addSynthKnob(grid2, "PITCH", 161, 0.0f, 1.0f, 2, false);
    addSynthKnob(grid2, "MORPH", 105, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid2, "DRIVE", 171, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid2, "FOLD", 181, 0.0f, 1.0f, 2, true);
    addSynthKnob(card2, "VOLUME", 108, 0.0f, 1.0f, 2, true);

    // --- SUB OSC ---
    lv_obj_t* card3 = createSynthCard(tab, "SUB OSCILLATOR", 175);
    lv_obj_t* grid3 = createKnobGrid(card3);
    addSynthKnob(grid3, "PITCH", 162, 0.0f, 1.0f, 2, false);
    addSynthKnob(grid3, "MORPH", 155, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid3, "DRIVE", 172, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid3, "FOLD", 182, 0.0f, 1.0f, 2, true);
    addSynthKnob(card3, "VOLUME", 109, 0.0f, 1.0f, 2, true);

    // --- TUNE & UTILITIES ---
    lv_obj_t* card4 = createSynthCard(tab, "TUNE & UTILITIES", 175);
    lv_obj_t* grid4 = createKnobGrid(card4);
    addSynthKnob(grid4, "DETUNE", 106, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid4, "NOISE VOL", 110, 0.0f, 1.0f, 2, true);
    addSynthKnob(grid4, "GLIDE", 355, 0.0f, 1.0f, 2, true);

    // Flex switches row inside card 4
    lv_obj_t* swRow = lv_obj_create(card4);
    lv_obj_set_size(swRow, 156, 60);
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
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 8, 0);
    lv_obj_set_style_pad_column(tab, 8, 0);

    auto createFilterCard = [trackColor](lv_obj_t* parent, const char* name, int width, int height) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, height);
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
        
        return card;
    };

    // --- 1. FILTER CARD (Top-Left: 490px x 300px) ---
    lv_obj_t* filterCard = createFilterCard(tab, "FILTER", 490, 300);
    
    // Header label
    lv_obj_t* filterTitle = lv_label_create(filterCard);
    lv_label_set_text(filterTitle, "FILTER");
    lv_obj_set_style_text_font(filterTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(filterTitle, trackColor, 0);

    // Dropdown row at top
    lv_obj_t* fDdCont = lv_obj_create(filterCard);
    lv_obj_set_size(fDdCont, 470, 52);
    lv_obj_set_style_bg_opa(fDdCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fDdCont, 0, 0);
    lv_obj_set_style_pad_all(fDdCont, 0, 0);
    lv_obj_remove_flag(fDdCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(fDdCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(fDdCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(fDdCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    addSynthDropdown(fDdCont, "FILTER MODE", 157, "LowPass\nHighPass\nBandPass\nNotch\nPeak", 0, true, 260);

    // Row for the 3 filter knobs below
    lv_obj_t* knobRow1 = lv_obj_create(filterCard);
    lv_obj_set_size(knobRow1, 470, 140);
    lv_obj_set_style_bg_opa(knobRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobRow1, 0, 0);
    lv_obj_set_style_pad_all(knobRow1, 0, 0);
    lv_obj_remove_flag(knobRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobRow1, "CUTOFF", 1, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobRow1, "RESONANCE", 2, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobRow1, "ENV AMT", 118, 0.0f, 1.0f, 2, true);

    // --- 2. LFO CARD (Top-Right: 490px x 300px) ---
    lv_obj_t* lfoCard = createFilterCard(tab, "LFO", 490, 300);

    // Header label
    lv_obj_t* lfoTitle = lv_label_create(lfoCard);
    lv_label_set_text(lfoTitle, "LFO");
    lv_obj_set_style_text_font(lfoTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lfoTitle, trackColor, 0);

    // Shape and destination dropdowns row
    lv_obj_t* ddRow = lv_obj_create(lfoCard);
    lv_obj_set_size(ddRow, 470, 52);
    lv_obj_set_style_bg_opa(ddRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ddRow, 0, 0);
    lv_obj_set_style_pad_all(ddRow, 0, 0);
    lv_obj_remove_flag(ddRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ddRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ddRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ddRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* shapeCont = lv_obj_create(ddRow);
    lv_obj_set_size(shapeCont, 210, 50);
    lv_obj_set_style_bg_opa(shapeCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shapeCont, 0, 0);
    lv_obj_set_style_pad_all(shapeCont, 0, 0);
    addSynthDropdown(shapeCont, "SHAPE", 154, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false, 190);

    lv_obj_t* destCont = lv_obj_create(ddRow);
    lv_obj_set_size(destCont, 210, 50);
    lv_obj_set_style_bg_opa(destCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(destCont, 0, 0);
    lv_obj_set_style_pad_all(destCont, 0, 0);
    addSynthDropdown(destCont, "DEST", 153, "Cutoff\nPitch\nMorph 1\nMorph 2\nFold 1\nFold 2\nVol 1\nVol 2", 0, false, 190);

    // Row for the 2 LFO knobs below
    lv_obj_t* knobRow2 = lv_obj_create(lfoCard);
    lv_obj_set_size(knobRow2, 470, 140);
    lv_obj_set_style_bg_opa(knobRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobRow2, 0, 0);
    lv_obj_set_style_pad_all(knobRow2, 0, 0);
    lv_obj_remove_flag(knobRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobRow2, "RATE", 7, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobRow2, "DEPTH", 8, 0.0f, 1.0f, 2, true);

    // --- 3. AMPLITUDE ENVELOPE CARD (Bottom-Left: 490px x 300px) ---
    lv_obj_t* ampCard = createFilterCard(tab, "AMP ENVELOPE", 490, 300);

    // Bypass Env row at the top with Title
    lv_obj_t* bypassRow = lv_obj_create(ampCard);
    lv_obj_set_size(bypassRow, 470, 30);
    lv_obj_set_style_bg_opa(bypassRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bypassRow, 0, 0);
    lv_obj_set_style_pad_all(bypassRow, 0, 0);
    lv_obj_remove_flag(bypassRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bypassRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bypassRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bypassRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* ampTitle = lv_label_create(bypassRow);
    lv_label_set_text(ampTitle, "AMP ENVELOPE");
    lv_obj_set_style_text_font(ampTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ampTitle, trackColor, 0);

    lv_obj_t* swWrap = lv_obj_create(bypassRow);
    lv_obj_set_size(swWrap, 160, 28);
    lv_obj_set_style_bg_opa(swWrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(swWrap, 0, 0);
    lv_obj_set_style_pad_all(swWrap, 0, 0);
    lv_obj_remove_flag(swWrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(swWrap, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(swWrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(swWrap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(swWrap, 8, 0);

    lv_obj_t* bpLabel = lv_label_create(swWrap);
    lv_label_set_text(bpLabel, "USE AMP ENV");
    lv_obj_set_style_text_font(bpLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(bpLabel, lv_color_hex(0x888888), 0);

    lv_obj_t* bpSw = lv_switch_create(swWrap);
    lv_obj_set_size(bpSw, 40, 20);
    
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
    lv_obj_set_size(faderRow1, 470, 220);
    lv_obj_set_style_bg_opa(faderRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow1, 0, 0);
    lv_obj_set_style_pad_all(faderRow1, 0, 0);
    lv_obj_remove_flag(faderRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow1, "A", 100, 0.001f, 15.0f, 2, false, 220);
    addSynthSlider(faderRow1, "D", 101, 0.0f, 15.0f, 2, false, 220);
    addSynthSlider(faderRow1, "S", 102, 0.0f, 1.0f, 2, true, 220);
    addSynthSlider(faderRow1, "R", 103, 0.001f, 15.0f, 2, false, 220);

    // --- 4. FILTER ENVELOPE CARD (Bottom-Right: 490px x 300px) ---
    lv_obj_t* filterEnvCard = createFilterCard(tab, "FILTER ENVELOPE", 490, 300);

    // Title label
    lv_obj_t* filterEnvTitle = lv_label_create(filterEnvCard);
    lv_label_set_text(filterEnvTitle, "FILTER ENVELOPE");
    lv_obj_set_style_text_font(filterEnvTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(filterEnvTitle, trackColor, 0);

    // Row for the 4 filter ADSR sliders
    lv_obj_t* faderRow2 = lv_obj_create(filterEnvCard);
    lv_obj_set_size(faderRow2, 470, 220);
    lv_obj_set_style_bg_opa(faderRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow2, 0, 0);
    lv_obj_set_style_pad_all(faderRow2, 0, 0);
    lv_obj_remove_flag(faderRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow2, "A", 114, 0.001f, 15.0f, 2, false, 220);
    addSynthSlider(faderRow2, "D", 115, 0.0f, 15.0f, 2, false, 220);
    addSynthSlider(faderRow2, "S", 116, 0.0f, 1.0f, 2, true, 220);
    addSynthSlider(faderRow2, "R", 117, 0.001f, 15.0f, 2, false, 220);
}

void UIManager::populateParamSubtractiveEnvTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamSubtractiveFilterTab
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
        if (currentVal < 0.125f) modeStr = "1-HIT";
        else if (currentVal < 0.25f) modeStr = "SUSTN";
        else if (currentVal < 0.375f) modeStr = "LOOP";
        else if (currentVal < 0.50f) modeStr = "CHOP";
        else if (currentVal < 0.625f) modeStr = "1-CHP";
        else if (currentVal < 0.75f) modeStr = "L-CHP";
        else if (currentVal < 0.875f) modeStr = "SCRUB";
        else modeStr = "SL-SCR";
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
            if (rawVal < 0.125f) modeStr = "1-HIT";
            else if (rawVal < 0.25f) modeStr = "SUSTN";
            else if (rawVal < 0.375f) modeStr = "LOOP";
            else if (rawVal < 0.50f) modeStr = "CHOP";
            else if (rawVal < 0.625f) modeStr = "1-CHP";
            else if (rawVal < 0.75f) modeStr = "L-CHP";
            else if (rawVal < 0.875f) modeStr = "SCRUB";
            else modeStr = "SL-SCR";
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

void UIManager::populateParamFmTab(lv_obj_t* tab1, lv_obj_t* tab2) {
    populateParamFmOperatorsTab(tab1);
    populateParamFmFilterTab(tab2);
}

void UIManager::populateParamFmOperatorsTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tab, 8, 0);

    // =========================================================================
    // LEFT SIDE (560px): OPERATOR SELECTION GRID & OP DETAIL CARD
    // =========================================================================
    lv_obj_t* opSide = lv_obj_create(tab);
    lv_obj_set_size(opSide, 560, 640);
    lv_obj_set_style_bg_opa(opSide, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(opSide, 0, 0);
    lv_obj_set_style_pad_all(opSide, 0, 0);
    lv_obj_remove_flag(opSide, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(opSide, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(opSide, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(opSide, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 6-operator block buttons container
    lv_obj_t* gridRow = lv_obj_create(opSide);
    lv_obj_set_size(gridRow, 560, 75);
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
        lv_obj_set_size(opBtn, 88, 68);
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
        const char* stateStr = !isActive ? "OFF" : (!isCarrier ? "MOD" : "CARR");
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
    lv_obj_t* detailCard = lv_obj_create(opSide);
    lv_obj_set_size(detailCard, 560, 545);
    lv_obj_set_style_bg_color(detailCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(detailCard, LV_OPA_90, 0);
    lv_obj_set_style_border_color(detailCard, trackColor, 0);
    lv_obj_set_style_border_width(detailCard, 2, 0);
    lv_obj_set_style_radius(detailCard, 12, 0);
    lv_obj_set_style_pad_all(detailCard, 8, 0);
    lv_obj_remove_flag(detailCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(detailCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(detailCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detailCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left Column: Selector & Mode Dropdown
    lv_obj_t* leftCol = lv_obj_create(detailCard);
    lv_obj_set_size(leftCol, 130, 510);
    lv_obj_set_style_bg_opa(leftCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftCol, 0, 0);
    lv_obj_set_style_pad_all(leftCol, 4, 0);
    lv_obj_remove_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* detTitle = lv_label_create(leftCol);
    lv_label_set_text_fmt(detTitle, "OPERATOR %d", mSelectedOpIdx + 1);
    lv_obj_set_style_text_font(detTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detTitle, trackColor, 0);

    bool selActive = (activeMask & (1 << mSelectedOpIdx)) != 0;
    bool selCarrier = (carrierMask & (1 << mSelectedOpIdx)) != 0;
    int currentSel = !selActive ? 0 : (!selCarrier ? 1 : 2);

    lv_obj_t* stLbl = lv_label_create(leftCol);
    const char* stStr = (currentSel == 0) ? "OFF" : ((currentSel == 1) ? "MODULATOR" : "CARRIER");
    lv_label_set_text(stLbl, stStr);
    lv_obj_set_style_text_font(stLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(stLbl, lv_color_hex(0x888888), 0);

    lv_obj_t* ddLbl = lv_label_create(leftCol);
    lv_label_set_text(ddLbl, "MODE");
    lv_obj_set_style_text_font(ddLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ddLbl, lv_color_hex(0x888888), 0);

    lv_obj_t* modeDd = lv_dropdown_create(leftCol);
    lv_obj_set_size(modeDd, 120, 34);
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
    lv_obj_set_size(midCol, 290, 510);
    lv_obj_set_style_bg_opa(midCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(midCol, 0, 0);
    lv_obj_set_style_pad_all(midCol, 0, 0);
    lv_obj_remove_flag(midCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(midCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(midCol, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(midCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    int base = 160 + mSelectedOpIdx * 6;
    addSynthSlider(midCol, "A", base + 1, 0.001f, 15.0f, 2, false, 380);
    addSynthSlider(midCol, "D", base + 2, 0.0f, 15.0f, 2, false, 380);
    addSynthSlider(midCol, "S", base + 3, 0.0f, 1.0f, 2, true, 380);
    addSynthSlider(midCol, "R", base + 4, 0.001f, 15.0f, 2, false, 380);

    // Right Column: Level & Ratio Knobs
    lv_obj_t* rightCol = lv_obj_create(detailCard);
    lv_obj_set_size(rightCol, 120, 510);
    lv_obj_set_style_bg_opa(rightCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightCol, 0, 0);
    lv_obj_set_style_pad_all(rightCol, 0, 0);
    lv_obj_remove_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(rightCol, "LEVEL", base + 0, 0.0f, 1.0f, 2, true);
    addSynthKnob(rightCol, "RATIO", base + 5, 0.0f, 1.0f, 1, false);

    // =========================================================================
    // RIGHT SIDE (470px): PRESET MANAGEMENT & ROUTING / ALGORITHM CARDS
    // =========================================================================
    lv_obj_t* routingSide = lv_obj_create(tab);
    lv_obj_set_size(routingSide, 470, 640);
    lv_obj_set_style_bg_opa(routingSide, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(routingSide, 0, 0);
    lv_obj_set_style_pad_all(routingSide, 0, 0);
    lv_obj_remove_flag(routingSide, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(routingSide, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(routingSide, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(routingSide, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createRoutingCard = [trackColor](lv_obj_t* parent, const char* name, int height) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, 470, height);
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

    // 1. PRESET MANAGEMENT CARD (Top: 270px)
    lv_obj_t* presetCard = createRoutingCard(routingSide, "PRESET MANAGEMENT", 270);

    int currentSelPreset = s_activeFmPreset[mActiveTrack];
    const auto& custom = mEngine.getTracks()[mActiveTrack].fmEngine.mCustomPresets;
    int totalCount = 32 + (int)custom.size();
    if (currentSelPreset >= totalCount) {
        currentSelPreset = 0;
        s_activeFmPreset[mActiveTrack] = 0;
    }
    std::string currentPresetName = (currentSelPreset < 32) ? FM_PRESET_NAMES[currentSelPreset] : (currentSelPreset - 32 < (int)custom.size() ? custom[currentSelPreset - 32].name : "Unknown");

    mFmActivePresetLbl = lv_label_create(presetCard);
    lv_label_set_text_fmt(mFmActivePresetLbl, "PRESET: %d - %s", currentSelPreset, currentPresetName.c_str());
    lv_obj_set_style_text_font(mFmActivePresetLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mFmActivePresetLbl, trackColor, 0);
    lv_label_set_long_mode(mFmActivePresetLbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(mFmActivePresetLbl, 440);

    lv_obj_t* presetActionRow = lv_obj_create(presetCard);
    lv_obj_set_size(presetActionRow, 450, 150);
    lv_obj_set_style_bg_opa(presetActionRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetActionRow, 0, 0);
    lv_obj_set_style_pad_all(presetActionRow, 0, 0);
    lv_obj_remove_flag(presetActionRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(presetActionRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(presetActionRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(presetActionRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Buttons sub-column
    lv_obj_t* presetBtnsCol = lv_obj_create(presetActionRow);
    lv_obj_set_size(presetBtnsCol, 330, 140);
    lv_obj_set_style_bg_opa(presetBtnsCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetBtnsCol, 0, 0);
    lv_obj_set_style_pad_all(presetBtnsCol, 0, 0);
    lv_obj_remove_flag(presetBtnsCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(presetBtnsCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(presetBtnsCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(presetBtnsCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* selectPresetBtn = lv_button_create(presetBtnsCol);
    lv_obj_set_size(selectPresetBtn, 320, 42);
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

    lv_obj_t* importBtn = lv_button_create(presetBtnsCol);
    lv_obj_set_size(importBtn, 320, 45);
    lv_obj_set_style_bg_color(importBtn, trackColor, 0);
    lv_obj_set_style_radius(importBtn, 8, 0);

    lv_obj_t* importLbl = lv_label_create(importBtn);
    lv_label_set_text(importLbl, "IMPORT PRESET (.fmp, .syx)");
    lv_obj_set_style_text_font(importLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(importLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(importLbl);
    lv_obj_add_event_cb(importBtn, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        if (!ui) return;
        ui->resetFileBrowserFlags();
        ui->mFileBrowserIsFmImport = true;
        ui->openFileBrowser(false);
    }, LV_EVENT_CLICKED, this);

    // Preset knob on the right
    lv_obj_t* presetKnobCont = lv_obj_create(presetActionRow);
    lv_obj_set_size(presetKnobCont, 100, 140);
    lv_obj_set_style_bg_opa(presetKnobCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetKnobCont, 0, 0);
    lv_obj_set_style_pad_all(presetKnobCont, 0, 0);
    lv_obj_remove_flag(presetKnobCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(presetKnobCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(presetKnobCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(presetKnobCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(presetKnobCont, "PRESET", 196, 0.0f, 1.0f, 0, false);

    // 2. ROUTING & ALGORITHM CARD (Bottom: 350px)
    lv_obj_t* rightCard = createRoutingCard(routingSide, "ROUTING & ALGORITHM", 350);

    lv_obj_t* row1 = lv_obj_create(rightCard);
    lv_obj_set_size(row1, 450, 120);
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
    lv_obj_set_size(row2, 450, 120);
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

void UIManager::populateParamFmRoutingTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamFmOperatorsTab
}

void UIManager::populateParamFmFilterTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 10, 0);

    auto createFmCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 640);
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

    // --- FILTER CONFIGURATION CARD (260px) ---
    lv_obj_t* filterCard = createFmCard(tab, "FILTER", 260);

    lv_obj_t* knobCol = lv_obj_create(filterCard);
    lv_obj_set_size(knobCol, 236, 560);
    lv_obj_set_style_bg_opa(knobCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobCol, 0, 0);
    lv_obj_set_style_pad_all(knobCol, 0, 0);
    lv_obj_remove_flag(knobCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(knobCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(knobCol, "FILTER TYPE", 156, "Lowpass\nBandpass\nHighpass\nBypass", (int)mEngine.getTracks()[mActiveTrack].parameters[156], false, 180);
    addSynthKnob(knobCol, "CUTOFF", 151, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobCol, "RES", 152, 0.0f, 1.0f, 2, true);

    // --- FILTER ENVELOPE CARD (380px) ---
    lv_obj_t* filterEnvCard = createFmCard(tab, "FILTER ENVELOPE", 380);

    lv_obj_t* faderRow1 = lv_obj_create(filterEnvCard);
    lv_obj_set_size(faderRow1, 356, 420);
    lv_obj_set_style_bg_opa(faderRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow1, 0, 0);
    lv_obj_set_style_pad_all(faderRow1, 0, 0);
    lv_obj_remove_flag(faderRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow1, "A", 114, 0.001f, 15.0f, 2, false, 380);
    addSynthSlider(faderRow1, "D", 115, 0.0f, 15.0f, 2, false, 380);
    addSynthSlider(faderRow1, "S", 116, 0.0f, 1.0f, 2, true, 380);
    addSynthSlider(faderRow1, "R", 117, 0.001f, 15.0f, 2, false, 380);

    lv_obj_t* bottomAmt = lv_obj_create(filterEnvCard);
    lv_obj_set_size(bottomAmt, 356, 120);
    lv_obj_set_style_bg_opa(bottomAmt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomAmt, 0, 0);
    lv_obj_set_style_pad_all(bottomAmt, 0, 0);
    lv_obj_remove_flag(bottomAmt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomAmt, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomAmt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottomAmt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(bottomAmt, "ENV AMT", 118, 0.0f, 1.0f, 2, true);

    // --- AMPLITUDE ENVELOPE CARD (380px) ---
    lv_obj_t* ampEnvCard = createFmCard(tab, "AMPLITUDE ENVELOPE", 380);

    lv_obj_t* faderRow2 = lv_obj_create(ampEnvCard);
    lv_obj_set_size(faderRow2, 356, 420);
    lv_obj_set_style_bg_opa(faderRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow2, 0, 0);
    lv_obj_set_style_pad_all(faderRow2, 0, 0);
    lv_obj_remove_flag(faderRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(faderRow2, "A", 100, 0.001f, 15.0f, 2, false, 380);
    addSynthSlider(faderRow2, "D", 101, 0.0f, 15.0f, 2, false, 380);
    addSynthSlider(faderRow2, "S", 102, 0.0f, 1.0f, 2, true, 380);
    addSynthSlider(faderRow2, "R", 103, 0.001f, 15.0f, 2, false, 380);

    // Spacer block to keep symmetry
    lv_obj_t* spacer = lv_obj_create(ampEnvCard);
    lv_obj_set_size(spacer, 356, 120);
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
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsWtSelect = true;
    ui->openFileBrowser(false);
}

void UIManager::wtImportBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsWtImport = true;
    ui->openFileBrowser(false);
}

void UIManager::populateParamWavetableTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    auto createCard = [trackColor](lv_obj_t* parent, const char* name, int width, int height) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, height);
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

    auto createKnobRow = [](lv_obj_t* parent, int width, int height) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, width, height);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return row;
    };

    // =========================================================================
    // ROW 1 (180px): CHARACTER (320px), UNISON & LOFI (320px), FILTER (390px)
    // =========================================================================
    lv_obj_t* row1 = lv_obj_create(tab);
    lv_obj_set_size(row1, 1050, 180);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // --- CHARACTER CARD (320px) ---
    lv_obj_t* charCard = createCard(row1, "CHARACTER", 320, 180);
    lv_obj_t* charKnobs = createKnobRow(charCard, 300, 130);
    addSynthKnob(charKnobs, "MORPH", 450, 0.0f, 1.0f, 2, true);
    addSynthKnob(charKnobs, "WARP", 465, -1.0f, 1.0f, 2, false);
    addSynthKnob(charKnobs, "CRUSH", 466, 0.0f, 1.0f, 2, true);
    addSynthKnob(charKnobs, "DRIVE", 467, 0.0f, 1.0f, 2, true);

    // --- UNISON & LOFI CARD (320px) ---
    lv_obj_t* unisonCard = createCard(row1, "UNISON & LOFI", 320, 180);
    lv_obj_t* unisonKnobs = createKnobRow(unisonCard, 300, 130);
    addSynthKnob(unisonKnobs, "DETUNE", 451, 0.0f, 1.0f, 2, true);
    addSynthKnob(unisonKnobs, "GLIDE", 355, 0.0f, 1.0f, 2, true);
    addSynthKnob(unisonKnobs, "BITRATE", 475, 0.0f, 1.0f, 2, true);
    addSynthKnob(unisonKnobs, "SAMPLERATE", 476, 0.0f, 1.0f, 2, true);

    // --- FILTER CARD (390px) ---
    lv_obj_t* filterCard = createCard(row1, "FILTER", 390, 180);
    lv_obj_t* filterContentRow = lv_obj_create(filterCard);
    lv_obj_set_size(filterContentRow, 370, 140);
    lv_obj_set_style_bg_opa(filterContentRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterContentRow, 0, 0);
    lv_obj_set_style_pad_all(filterContentRow, 0, 0);
    lv_obj_remove_flag(filterContentRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterContentRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterContentRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterContentRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Dropdown on left side of filter card
    lv_obj_t* filterDdCont = lv_obj_create(filterContentRow);
    lv_obj_set_size(filterDdCont, 125, 130);
    lv_obj_set_style_bg_opa(filterDdCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterDdCont, 0, 0);
    lv_obj_set_style_pad_all(filterDdCont, 0, 0);
    lv_obj_remove_flag(filterDdCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterDdCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterDdCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(filterDdCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    addSynthDropdown(filterDdCont, "FILTER TYPE", 470, "LowPass\nHighPass\nBandPass\nNotch\nPeak", 0, false, 115);

    // Knobs on right side of filter card
    lv_obj_t* filterKnobs = createKnobRow(filterContentRow, 240, 130);
    addSynthKnob(filterKnobs, "CUTOFF", 458, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobs, "RESONANCE", 459, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobs, "ENV AMT", 464, 0.0f, 1.0f, 2, true);

    // =========================================================================
    // ROW 2 (160px): WAVETABLE SELECTION CARD (1050px FULL WIDTH)
    // =========================================================================
    lv_obj_t* selectCard = lv_obj_create(tab);
    lv_obj_set_size(selectCard, 1050, 160);
    lv_obj_set_style_bg_color(selectCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(selectCard, LV_OPA_90, 0);
    lv_obj_set_style_border_color(selectCard, trackColor, 0);
    lv_obj_set_style_border_width(selectCard, 2, 0);
    lv_obj_set_style_radius(selectCard, 12, 0);
    lv_obj_set_style_pad_all(selectCard, 10, 0);
    lv_obj_remove_flag(selectCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(selectCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selectCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(selectCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Left container: Title, Active label box, Button row
    lv_obj_t* selectLeft = lv_obj_create(selectCard);
    lv_obj_set_size(selectLeft, 910, 138);
    lv_obj_set_style_bg_opa(selectLeft, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(selectLeft, 0, 0);
    lv_obj_set_style_pad_all(selectLeft, 0, 0);
    lv_obj_remove_flag(selectLeft, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(selectLeft, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selectLeft, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(selectLeft, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* selectTitle = lv_label_create(selectLeft);
    lv_label_set_text(selectTitle, "WAVETABLE SELECTION");
    lv_obj_set_style_text_font(selectTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(selectTitle, trackColor, 0);

    // Active wavetable text box
    lv_obj_t* activeBox = lv_obj_create(selectLeft);
    lv_obj_set_size(activeBox, 890, 42);
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
    lv_obj_t* btnRow = lv_obj_create(selectLeft);
    lv_obj_set_size(btnRow, 890, 50);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_remove_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto addActionButton = [this, trackColor](lv_obj_t* parent, const char* labelText, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, 285, 42);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_color(btn, trackColor, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labelText);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
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

    // Right container: SELECT WT Knob
    lv_obj_t* selectRight = lv_obj_create(selectCard);
    lv_obj_set_size(selectRight, 110, 138);
    lv_obj_set_style_bg_opa(selectRight, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(selectRight, 0, 0);
    lv_obj_set_style_pad_all(selectRight, 0, 0);
    lv_obj_remove_flag(selectRight, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(selectRight, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selectRight, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(selectRight, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(selectRight, "SELECT WT", 477, 0.0f, 1.0f, 2, true);

    // =========================================================================
    // ROW 3 (280px): AMP ENVELOPE (515px) & FILTER ENVELOPE (515px)
    // =========================================================================
    lv_obj_t* row3 = lv_obj_create(tab);
    lv_obj_set_size(row3, 1050, 280);
    lv_obj_set_style_bg_opa(row3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row3, 0, 0);
    lv_obj_set_style_pad_all(row3, 0, 0);
    lv_obj_remove_flag(row3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row3, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // --- AMP ENVELOPE CARD (515px) ---
    lv_obj_t* ampCard = createCard(row3, "AMP ENVELOPE", 515, 280);
    lv_obj_t* ampRow = lv_obj_create(ampCard);
    lv_obj_set_size(ampRow, 490, 230);
    lv_obj_set_style_bg_opa(ampRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ampRow, 0, 0);
    lv_obj_set_style_pad_all(ampRow, 0, 0);
    lv_obj_remove_flag(ampRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ampRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ampRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ampRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(ampRow, "A", 454, 0.001f, 15.0f, 2, false, 230);
    addSynthSlider(ampRow, "D", 455, 0.0f, 15.0f, 2, false, 230);
    addSynthSlider(ampRow, "S", 456, 0.0f, 1.0f, 2, true, 230);
    addSynthSlider(ampRow, "R", 457, 0.001f, 15.0f, 2, false, 230);

    // --- FILTER ENVELOPE CARD (515px) ---
    lv_obj_t* filterEnvCard = createCard(row3, "FILTER ENVELOPE", 515, 280);
    lv_obj_t* filterEnvRow = lv_obj_create(filterEnvCard);
    lv_obj_set_size(filterEnvRow, 490, 230);
    lv_obj_set_style_bg_opa(filterEnvRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterEnvRow, 0, 0);
    lv_obj_set_style_pad_all(filterEnvRow, 0, 0);
    lv_obj_remove_flag(filterEnvRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterEnvRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterEnvRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterEnvRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(filterEnvRow, "A", 471, 0.001f, 15.0f, 2, false, 230);
    addSynthSlider(filterEnvRow, "D", 472, 0.0f, 15.0f, 2, false, 230);
    addSynthSlider(filterEnvRow, "S", 473, 0.0f, 1.0f, 2, true, 230);
    addSynthSlider(filterEnvRow, "R", 474, 0.001f, 15.0f, 2, false, 230);
}

void UIManager::populateParamWavetableFilterTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamWavetableTab
}

void UIManager::populateParamSamplerTab(lv_obj_t* tab) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    // =========================================================================
    // ROW 1 (45px): ACTION BUTTONS ROW (1050px FULL WIDTH)
    // =========================================================================
    lv_obj_t* topRow = lv_obj_create(tab);
    lv_obj_set_size(topRow, 1050, 45);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_remove_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(topRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1. LATCH button
    mSamplerLatchBtn = lv_btn_create(topRow);
    lv_obj_set_size(mSamplerLatchBtn, 130, 40);
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
    lv_obj_set_size(mSamplerRecordBtn, 170, 40);
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
    lv_obj_align(recDot, LV_ALIGN_LEFT_MID, 20, 0);
    
    lv_obj_t* recLbl = lv_label_create(mSamplerRecordBtn);
    lv_label_set_text(recLbl, "RECORD");
    lv_obj_set_style_text_font(recLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(recLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(recLbl, LV_ALIGN_LEFT_MID, 40, 0);

    lv_obj_add_event_cb(mSamplerRecordBtn, UIManager::samplerRecordBtnEventCb, LV_EVENT_ALL, this);

    // 3. TRIM button
    lv_obj_t* trimBtn = lv_btn_create(topRow);
    lv_obj_set_size(trimBtn, 130, 40);
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
    lv_obj_set_size(loadBtn, 130, 40);
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
    lv_obj_set_size(saveBtn, 130, 40);
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
    lv_obj_set_size(srcDd, 150, 40);
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

    // =========================================================================
    // ROW 2 (200px): WAVEFORM CONTAINER (1050px FULL WIDTH)
    // =========================================================================
    mSamplerWaveformContainer = lv_obj_create(tab);
    lv_obj_set_size(mSamplerWaveformContainer, 1050, 200);
    lv_obj_set_style_bg_color(mSamplerWaveformContainer, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(mSamplerWaveformContainer, trackColor, 0);
    lv_obj_set_style_border_width(mSamplerWaveformContainer, 2, 0);
    lv_obj_set_style_radius(mSamplerWaveformContainer, 12, 0);
    lv_obj_set_layout(mSamplerWaveformContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mSamplerWaveformContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mSamplerWaveformContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(mSamplerWaveformContainer, 8, 0);
    lv_obj_set_style_pad_ver(mSamplerWaveformContainer, 10, 0);
    lv_obj_remove_flag(mSamplerWaveformContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mSamplerWaveformContainer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mSamplerWaveformContainer, samplerWaveformContainerEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(mSamplerWaveformContainer, samplerWaveformContainerEventCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(mSamplerWaveformContainer, samplerWaveformContainerEventCb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(mSamplerWaveformContainer, samplerWaveformContainerEventCb, LV_EVENT_PRESS_LOST, this);

    // Play / Audition Button in upper left corner of waveform preview
    mSamplerPlayBtn = lv_btn_create(mSamplerWaveformContainer);
    lv_obj_add_flag(mSamplerPlayBtn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mSamplerPlayBtn, 36, 36);
    lv_obj_align(mSamplerPlayBtn, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_set_style_radius(mSamplerPlayBtn, 8, 0);
    lv_obj_set_style_bg_color(mSamplerPlayBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(mSamplerPlayBtn, LV_OPA_90, 0);
    lv_obj_set_style_border_width(mSamplerPlayBtn, 1, 0);
    lv_obj_set_style_border_color(mSamplerPlayBtn, trackColor, 0);
    lv_obj_set_style_pad_all(mSamplerPlayBtn, 0, 0);
    lv_obj_remove_flag(mSamplerPlayBtn, LV_OBJ_FLAG_SCROLLABLE);

    mSamplerPlayBtnLabel = lv_label_create(mSamplerPlayBtn);
    lv_label_set_text(mSamplerPlayBtnLabel, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(mSamplerPlayBtnLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSamplerPlayBtnLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(mSamplerPlayBtnLabel);

    lv_obj_add_event_cb(mSamplerPlayBtn, UIManager::samplerPlayBtnEventCb, LV_EVENT_CLICKED, this);

    // Create 150 vertical bars representing amplitude
    for (int i = 0; i < 150; ++i) {
        mSamplerWaveformBars[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_set_size(mSamplerWaveformBars[i], 5, 2);
        lv_obj_set_style_bg_color(mSamplerWaveformBars[i], lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(mSamplerWaveformBars[i], LV_OPA_40, 0);
        lv_obj_set_style_border_width(mSamplerWaveformBars[i], 0, 0);
        lv_obj_set_style_pad_all(mSamplerWaveformBars[i], 0, 0);
        lv_obj_set_style_radius(mSamplerWaveformBars[i], 1, 0);
        lv_obj_remove_flag(mSamplerWaveformBars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(mSamplerWaveformBars[i], LV_OBJ_FLAG_CLICKABLE);
    }

    // Green Start Marker
    mSamplerStartLine = lv_obj_create(mSamplerWaveformContainer);
    lv_obj_add_flag(mSamplerStartLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mSamplerStartLine, 2, 200);
    lv_obj_set_style_bg_color(mSamplerStartLine, lv_color_hex(0x00FF66), 0);
    lv_obj_set_style_border_width(mSamplerStartLine, 0, 0);
    lv_obj_set_style_radius(mSamplerStartLine, 0, 0);
    lv_obj_remove_flag(mSamplerStartLine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mSamplerStartLine, LV_OBJ_FLAG_CLICKABLE);

    // Red End Marker
    mSamplerEndLine = lv_obj_create(mSamplerWaveformContainer);
    lv_obj_add_flag(mSamplerEndLine, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(mSamplerEndLine, 2, 200);
    lv_obj_set_style_bg_color(mSamplerEndLine, lv_color_hex(0xFF3366), 0);
    lv_obj_set_style_border_width(mSamplerEndLine, 0, 0);
    lv_obj_set_style_radius(mSamplerEndLine, 0, 0);
    lv_obj_remove_flag(mSamplerEndLine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mSamplerEndLine, LV_OBJ_FLAG_CLICKABLE);

    // Playhead Shades (25% opacity)
    for (int i = 0; i < 16; ++i) {
        mSamplerPlayheadShades[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerPlayheadShades[i], 0, 200);
        lv_obj_set_style_bg_color(mSamplerPlayheadShades[i], trackColor, 0);
        lv_obj_set_style_bg_opa(mSamplerPlayheadShades[i], 64, 0);
        lv_obj_set_style_border_width(mSamplerPlayheadShades[i], 0, 0);
        lv_obj_set_style_radius(mSamplerPlayheadShades[i], 0, 0);
        lv_obj_remove_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(mSamplerPlayheadShades[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Playhead Lines (track color)
    for (int i = 0; i < 16; ++i) {
        mSamplerPlayheadLines[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerPlayheadLines[i], 2, 200);
        lv_obj_set_style_bg_color(mSamplerPlayheadLines[i], trackColor, 0);
        lv_obj_set_style_border_width(mSamplerPlayheadLines[i], 0, 0);
        lv_obj_set_style_radius(mSamplerPlayheadLines[i], 0, 0);
        lv_obj_remove_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_CLICKABLE);
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
    lv_obj_add_event_cb(mSamplerScrubHandle, samplerScrubHandleEventCb, LV_EVENT_PRESS_LOST, this);

    // 16 Slices Lines & Rounded Handles
    for (int i = 0; i < 16; ++i) {
        mSamplerSliceLines[i] = lv_obj_create(mSamplerWaveformContainer);
        lv_obj_add_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(mSamplerSliceLines[i], 2, 200);
        lv_obj_set_style_bg_color(mSamplerSliceLines[i], lv_color_hex(0x00D2FF), 0); // Cyan
        lv_obj_set_style_border_width(mSamplerSliceLines[i], 0, 0);
        lv_obj_set_style_radius(mSamplerSliceLines[i], 0, 0);
        lv_obj_remove_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(mSamplerSliceLines[i], LV_OBJ_FLAG_CLICKABLE);
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

    // =========================================================================
    // ROW 3 (380px): SAMPLE EDITS (315px), SYNTHESIS (355px), ENVELOPE (360px)
    // =========================================================================
    lv_obj_t* bottomRow = lv_obj_create(tab);
    lv_obj_set_size(bottomRow, 1050, 380);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_remove_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createBottomCard = [trackColor](lv_obj_t* parent, const char* name, int width) -> lv_obj_t* {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, width, 380);
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

    // --- CARD 1: SAMPLE EDITS & SLICES (315px) ---
    lv_obj_t* editsCard = createBottomCard(bottomRow, "SAMPLE & SLICES", 315);

    lv_obj_t* editsRow1 = lv_obj_create(editsCard);
    lv_obj_set_size(editsRow1, 295, 100);
    lv_obj_set_style_bg_opa(editsRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(editsRow1, 0, 0);
    lv_obj_set_style_pad_all(editsRow1, 0, 0);
    lv_obj_remove_flag(editsRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(editsRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(editsRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(editsRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(editsRow1, "START", 330, 0.0f, 1.0f, 2, true);
    addSynthKnob(editsRow1, "END", 331, 0.0f, 1.0f, 2, true);
    addSynthKnob(editsRow1, "MODE", 320, 0.0f, 1.0f, 0, false);

    lv_obj_t* editsRow2 = lv_obj_create(editsCard);
    lv_obj_set_size(editsRow2, 295, 100);
    lv_obj_set_style_bg_opa(editsRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(editsRow2, 0, 0);
    lv_obj_set_style_pad_all(editsRow2, 0, 0);
    lv_obj_remove_flag(editsRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(editsRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(editsRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(editsRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(editsRow2, "SLICES", 340, 0.0f, 1.0f, 0, false);
    addSynthKnob(editsRow2, "SLICE SEL", 341, 0.0f, 1.0f, 0, false);
    addSynthKnob(editsRow2, "SCRUB", 360, 0.0f, 1.0f, 2, true);

    // Button Row (Reverse & Slice Lock)
    lv_obj_t* editsBtnRow = lv_obj_create(editsCard);
    lv_obj_set_size(editsBtnRow, 295, 110);
    lv_obj_set_style_bg_opa(editsBtnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(editsBtnRow, 0, 0);
    lv_obj_set_style_pad_all(editsBtnRow, 0, 0);
    lv_obj_remove_flag(editsBtnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(editsBtnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(editsBtnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(editsBtnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // REVERSE Button
    lv_obj_t* revBtn = lv_btn_create(editsBtnRow);
    lv_obj_set_size(revBtn, 135, 42);
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
    lv_obj_t* lockBtn = lv_btn_create(editsBtnRow);
    lv_obj_set_size(lockBtn, 135, 42);
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

    // --- CARD 2: SYNTHESIS (355px) ---
    lv_obj_t* synthCard = createBottomCard(bottomRow, "SYNTHESIS", 355);

    lv_obj_t* sRow1 = lv_obj_create(synthCard);
    lv_obj_set_size(sRow1, 335, 100);
    lv_obj_set_style_bg_opa(sRow1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sRow1, 0, 0);
    lv_obj_set_style_pad_all(sRow1, 0, 0);
    lv_obj_remove_flag(sRow1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sRow1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sRow1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sRow1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(sRow1, "PITCH", 300, 0.0f, 1.0f, 0, false);
    addSynthKnob(sRow1, "SPEED", 302, 0.0f, 1.0f, 2, false);
    addSynthKnob(sRow1, "STRETCH", 301, 0.0f, 1.0f, 2, false);

    lv_obj_t* sRow2 = lv_obj_create(synthCard);
    lv_obj_set_size(sRow2, 335, 100);
    lv_obj_set_style_bg_opa(sRow2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sRow2, 0, 0);
    lv_obj_set_style_pad_all(sRow2, 0, 0);
    lv_obj_remove_flag(sRow2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sRow2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sRow2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sRow2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(sRow2, "CUTOFF", 303, 0.0f, 1.0f, 2, true);
    addSynthKnob(sRow2, "RES", 304, 0.0f, 1.0f, 2, true);
    addSynthDropdown(sRow2, "FILTER TYPE", 305, "LowPass\nHighPass\nBandPass\nNotch\nPeak", 0, false, 110);

    lv_obj_t* sRow3 = lv_obj_create(synthCard);
    lv_obj_set_size(sRow3, 335, 110);
    lv_obj_set_style_bg_opa(sRow3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sRow3, 0, 0);
    lv_obj_set_style_pad_all(sRow3, 0, 0);
    lv_obj_remove_flag(sRow3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sRow3, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sRow3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sRow3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(sRow3, "GLIDE", 355, 0.0f, 1.0f, 2, true);

    // --- CARD 3: ENVELOPE (360px) ---
    lv_obj_t* envCard = createBottomCard(bottomRow, "ENVELOPE", 360);

    lv_obj_t* adsrRow = lv_obj_create(envCard);
    lv_obj_set_size(adsrRow, 340, 230);
    lv_obj_set_style_bg_opa(adsrRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(adsrRow, 0, 0);
    lv_obj_set_style_pad_all(adsrRow, 0, 0);
    lv_obj_remove_flag(adsrRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(adsrRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(adsrRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(adsrRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthSlider(adsrRow, "A", 310, 0.001f, 15.0f, 2, false, 230);
    addSynthSlider(adsrRow, "D", 311, 0.0f, 15.0f, 2, false, 230);
    addSynthSlider(adsrRow, "S", 312, 0.0f, 1.0f, 2, true, 230);
    addSynthSlider(adsrRow, "R", 313, 0.001f, 15.0f, 2, false, 230);

    lv_obj_t* envAmtCont = lv_obj_create(envCard);
    lv_obj_set_size(envAmtCont, 340, 90);
    lv_obj_set_style_bg_opa(envAmtCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(envAmtCont, 0, 0);
    lv_obj_set_style_pad_all(envAmtCont, 0, 0);
    lv_obj_remove_flag(envAmtCont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(envAmtCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(envAmtCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(envAmtCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(envAmtCont, "ENV AMT", 314, 0.0f, 1.0f, 2, true);

    // Trigger initial preview draw
    updateSamplerWaveformPreview();
}

void UIManager::populateParamSamplerSynthesisTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamSamplerTab
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
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSampleLoad = true;
    ui->openFileBrowser(false);
}

void UIManager::samplerSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSampleSave = true;
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

void UIManager::samplerPlayBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->mActiveTrack < 0 || ui->mActiveTrack >= (int)ui->mEngine.getTracks().size()) return;

    GranularEngine::PlayheadInfo playheads[16];
    ui->mEngine.getGranularPlayheads(ui->mActiveTrack, playheads, 16);
    bool isPlaying = false;
    for (int i = 0; i < 16; ++i) {
        if (playheads[i].pos >= 0.0f) {
            isPlaying = true;
            break;
        }
    }

    if (isPlaying) {
        ui->mEngine.allNotesOff(ui->mActiveTrack);
    } else {
        ui->mEngine.triggerNote(ui->mActiveTrack, 60, 100, 60);
    }
    ui->updateSamplerWaveformPreview();
}

void UIManager::samplerScrubHandleEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->mActiveTrack < 0 || ui->mActiveTrack >= (int)ui->mEngine.getTracks().size()) return;

    lv_event_code_t code = lv_event_get_code(e);

    if (ui->mMidiLearnActive) {
        if (code == LV_EVENT_PRESSED) {
            ui->mMidiLearnTargetParamId = 360; // Scrub Position
            ui->mMidiLearnTargetTrack = ui->mActiveTrack;
            if (ui->mMidiLearnBtnLabel) {
                std::string pName = getParameterNameString(ui->mActiveTrack, 360, &(ui->mEngine));
                lv_label_set_text_fmt(ui->mMidiLearnBtnLabel, "LEARN: MOVE CC CONTROL TO MAP '%s'", pName.c_str());
            }
        }
        return;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);

            lv_area_t container_coords;
            lv_obj_get_coords(ui->mSamplerWaveformContainer, &container_coords);
            int pad_left = lv_obj_get_style_pad_left(ui->mSamplerWaveformContainer, 0) + lv_obj_get_style_border_width(ui->mSamplerWaveformContainer, 0);
            int content_w = lv_obj_get_content_width(ui->mSamplerWaveformContainer);
            if (content_w <= 0) content_w = 1030;

            int local_x = p.x - (container_coords.x1 + pad_left);
            float scrubPos = (float)local_x / (float)content_w;
            if (scrubPos < 0.0f) scrubPos = 0.0f;
            if (scrubPos > 1.0f) scrubPos = 1.0f;

            ui->mEngine.setParameter(ui->mActiveTrack, 361, 1.0f); // mScrubGate
            ui->mEngine.setParameter(ui->mActiveTrack, 360, scrubPos); // mScrubPosition
            ui->updateSamplerWaveformPreview();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui->mEngine.setParameter(ui->mActiveTrack, 361, 0.0f);
    }
}

void UIManager::samplerWaveformContainerEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->mActiveTrack < 0 || ui->mActiveTrack >= (int)ui->mEngine.getTracks().size()) return;
    if (ui->mEngine.getTracks()[ui->mActiveTrack].engineType != 2) return;

    lv_event_code_t code = lv_event_get_code(e);

    if (ui->mMidiLearnActive) {
        if (code == LV_EVENT_PRESSED) {
            ui->mMidiLearnTargetParamId = 360; // Scrub Position
            ui->mMidiLearnTargetTrack = ui->mActiveTrack;
            if (ui->mMidiLearnBtnLabel) {
                std::string pName = getParameterNameString(ui->mActiveTrack, 360, &(ui->mEngine));
                lv_label_set_text_fmt(ui->mMidiLearnBtnLabel, "LEARN: MOVE CC CONTROL TO MAP '%s'", pName.c_str());
            }
        }
        return;
    }

    bool isScrubMode = (ui->mEngine.getTracks()[ui->mActiveTrack].samplerEngine.getPlayMode() == SamplerEngine::Scrub);
    if (!isScrubMode) return;

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);

            lv_area_t container_coords;
            lv_obj_get_coords(ui->mSamplerWaveformContainer, &container_coords);
            int pad_left = lv_obj_get_style_pad_left(ui->mSamplerWaveformContainer, 0) + lv_obj_get_style_border_width(ui->mSamplerWaveformContainer, 0);
            int content_w = lv_obj_get_content_width(ui->mSamplerWaveformContainer);
            if (content_w <= 0) content_w = 1030;

            int local_x = p.x - (container_coords.x1 + pad_left);
            float scrubPos = (float)local_x / (float)content_w;
            if (scrubPos < 0.0f) scrubPos = 0.0f;
            if (scrubPos > 1.0f) scrubPos = 1.0f;

            ui->mEngine.setParameter(ui->mActiveTrack, 361, 1.0f); // mScrubGate
            ui->mEngine.setParameter(ui->mActiveTrack, 360, scrubPos); // mScrubPosition
            ui->updateSamplerWaveformPreview();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui->mEngine.setParameter(ui->mActiveTrack, 361, 0.0f);
    }
}

void UIManager::samplerSliceHandleEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    lv_obj_t* handle = (lv_obj_t*)lv_event_get_target(e);
    int sliceIdx = (int)(uintptr_t)lv_obj_get_user_data(handle);

    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        lv_area_t container_coords;
        lv_obj_get_coords(ui->mSamplerWaveformContainer, &container_coords);
        int pad_left = lv_obj_get_style_pad_left(ui->mSamplerWaveformContainer, 0) + lv_obj_get_style_border_width(ui->mSamplerWaveformContainer, 0);
        int content_w = lv_obj_get_content_width(ui->mSamplerWaveformContainer);
        if (content_w <= 0) content_w = 1030;

        int local_x = p.x - (container_coords.x1 + pad_left);
        float pos = (float)local_x / (float)content_w;
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

    float containerWidth = (float)lv_obj_get_content_width(mSamplerWaveformContainer);
    if (containerWidth <= 10.0f) {
        containerWidth = 1030.0f;
    }

    if (mSamplerStartLine) {
        int x = (int)(startPnt * containerWidth);
        lv_obj_align(mSamplerStartLine, LV_ALIGN_LEFT_MID, x, 0);
    }
    if (mSamplerEndLine) {
        int x = (int)(endPnt * containerWidth);
        lv_obj_align(mSamplerEndLine, LV_ALIGN_LEFT_MID, x, 0);
    }

    // 1. Scrub Mode vs Normal Playhead
    bool isScrubMode = (mEngine.getTracks()[mActiveTrack].samplerEngine.getPlayMode() == SamplerEngine::Scrub);

    // Query active playheads and update playhead line & shade
    GranularEngine::PlayheadInfo playheads[16];
    mEngine.getGranularPlayheads(mActiveTrack, playheads, 16);

    // Update Waveform Play / Audition Button State
    if (mSamplerPlayBtn && mSamplerPlayBtnLabel) {
        bool isAnyPlaying = false;
        for (int i = 0; i < 16; ++i) {
            if (playheads[i].pos >= 0.0f) {
                isAnyPlaying = true;
                break;
            }
        }
        if (isAnyPlaying) {
            lv_label_set_text(mSamplerPlayBtnLabel, LV_SYMBOL_STOP);
            lv_obj_set_style_bg_color(mSamplerPlayBtn, trackColor, 0);
            lv_obj_set_style_text_color(mSamplerPlayBtnLabel, lv_color_hex(0x000000), 0);
        } else {
            lv_label_set_text(mSamplerPlayBtnLabel, LV_SYMBOL_PLAY);
            lv_obj_set_style_bg_color(mSamplerPlayBtn, lv_color_hex(0x222222), 0);
            lv_obj_set_style_text_color(mSamplerPlayBtnLabel, lv_color_hex(0xFFFFFF), 0);
        }
    }

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
            int x = (int)(pos * containerWidth);
            lv_obj_align(mSamplerPlayheadLines[0], LV_ALIGN_LEFT_MID, x, 0);
            lv_obj_clear_flag(mSamplerPlayheadLines[0], LV_OBJ_FLAG_HIDDEN);

            if (mSamplerScrubHandle) {
                lv_obj_set_style_bg_color(mSamplerScrubHandle, oppositeColor, 0);
                lv_obj_align(mSamplerScrubHandle, LV_ALIGN_BOTTOM_LEFT, x - 12, -6);
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
                    int x = (int)(playheads[i].pos * containerWidth);
                    lv_obj_align(mSamplerPlayheadLines[i], LV_ALIGN_LEFT_MID, x, 0);
                    lv_obj_clear_flag(mSamplerPlayheadLines[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (mSamplerPlayheadShades[i]) {
                    lv_obj_set_style_bg_color(mSamplerPlayheadShades[i], trackColor, 0);
                    lv_obj_set_style_bg_opa(mSamplerPlayheadShades[i], 64, 0); // 25% opacity
                    float s = std::min(playheads[i].start, playheads[i].pos);
                    float e = std::max(playheads[i].start, playheads[i].pos);
                    int xs = (int)(s * containerWidth);
                    int ws = (int)((e - s) * containerWidth);
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
            int x = (int)(p * containerWidth);
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

    addSynthSlider(ampRow, "A", 425, 0.001f, 15.0f, 2, false, 140);
    addSynthSlider(ampRow, "D", 426, 0.0f, 15.0f, 2, false, 140);
    addSynthSlider(ampRow, "S", 427, 0.0f, 1.0f, 2, true, 140);
    addSynthSlider(ampRow, "R", 428, 0.001f, 15.0f, 2, false, 140);

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
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSampleLoad = true;
    ui->openFileBrowser(false);
}

void UIManager::granularSaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSampleSave = true;
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

    float containerWidth = (float)lv_obj_get_content_width(mGranularWaveformContainer);
    if (containerWidth <= 10.0f) {
        containerWidth = 738.0f;
    }

    if (mGranularStartLine) {
        int x = (int)(startPnt * containerWidth);
        lv_obj_align(mGranularStartLine, LV_ALIGN_LEFT_MID, x, 0);
    }
    if (mGranularEndLine) {
        int x = (int)(endPnt * containerWidth);
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
            int x = (int)(activePos * containerWidth);
            lv_obj_align(mGranularPlayheadLine, LV_ALIGN_LEFT_MID, x, 0);
            lv_obj_clear_flag(mGranularPlayheadLine, LV_OBJ_FLAG_HIDDEN);
        }
        if (mGranularPlayheadShade) {
            lv_obj_set_style_bg_color(mGranularPlayheadShade, trackColor, 0);
            lv_obj_set_style_bg_opa(mGranularPlayheadShade, 64, 0);
            float s = std::min(startPos, activePos);
            float e = std::max(startPos, activePos);
            int xs = (int)(s * containerWidth);
            int ws = (int)((e - s) * containerWidth);
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
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tab, 8, 0);

    // =========================================================================
    // LEFT COLUMN (490px): ACTIONS, BANK/PRESET INFO, MASTER KNOBS
    // =========================================================================
    lv_obj_t* leftCol = lv_obj_create(tab);
    lv_obj_set_size(leftCol, 490, 640);
    lv_obj_set_style_bg_opa(leftCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftCol, 0, 0);
    lv_obj_set_style_pad_all(leftCol, 0, 0);
    lv_obj_remove_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftCol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1. Actions Row (Top Bar)
    lv_obj_t* actionsRow = lv_obj_create(leftCol);
    lv_obj_set_size(actionsRow, 490, 52);
    lv_obj_set_style_bg_opa(actionsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actionsRow, 0, 0);
    lv_obj_set_style_pad_all(actionsRow, 0, 0);
    lv_obj_remove_flag(actionsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(actionsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actionsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actionsRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto createActionBtn = [this, trackColor](lv_obj_t* parent, const char* labelText, lv_event_cb_t cb) {
        lv_obj_t* btn = lv_button_create(parent);
        lv_obj_set_size(btn, 155, 44);
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

    // 2. Status / Info Card
    lv_obj_t* infoCard = lv_obj_create(leftCol);
    lv_obj_set_size(infoCard, 490, 565);
    lv_obj_set_style_bg_color(infoCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(infoCard, trackColor, 0);
    lv_obj_set_style_border_width(infoCard, 1, 0);
    lv_obj_set_style_radius(infoCard, 14, 0);
    lv_obj_set_style_pad_all(infoCard, 16, 0);
    lv_obj_remove_flag(infoCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(infoCard, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(infoCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(infoCard, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    std::string bankPath = mEngine.getTracks()[mActiveTrack].lastSamplePath;
    std::string bankName = bankPath.empty() ? "None (Default GS)" : bankPath;
    size_t lastSlash = bankName.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        bankName = bankName.substr(lastSlash + 1);
    }

    mSoundFontActiveBankLbl = lv_label_create(infoCard);
    lv_label_set_text_fmt(mSoundFontActiveBankLbl, "BANK: %s", bankName.c_str());
    lv_obj_set_style_text_font(mSoundFontActiveBankLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSoundFontActiveBankLbl, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(mSoundFontActiveBankLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(mSoundFontActiveBankLbl, 450);
    lv_obj_set_style_text_align(mSoundFontActiveBankLbl, LV_TEXT_ALIGN_CENTER, 0);

    int activeP = mEngine.getTracks()[mActiveTrack].soundFontEngine.getPresetIndex();
    std::string pName = mEngine.getSoundFontPresetName(mActiveTrack, activeP);
    if (pName.empty()) pName = "General User GS Default";

    mSoundFontActivePresetLbl = lv_label_create(infoCard);
    lv_label_set_text_fmt(mSoundFontActivePresetLbl, "PRESET %d: %s", activeP, pName.c_str());
    lv_obj_set_style_text_font(mSoundFontActivePresetLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mSoundFontActivePresetLbl, trackColor, 0);
    lv_label_set_long_mode(mSoundFontActivePresetLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(mSoundFontActivePresetLbl, 450);
    lv_obj_set_style_text_align(mSoundFontActivePresetLbl, LV_TEXT_ALIGN_CENTER, 0);

    // Knobs for Preset and Bank
    lv_obj_t* knobsRow = lv_obj_create(infoCard);
    lv_obj_set_size(knobsRow, 340, 110);
    lv_obj_set_style_bg_opa(knobsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobsRow, 0, 0);
    lv_obj_set_style_pad_all(knobsRow, 0, 0);
    lv_obj_remove_flag(knobsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobsRow, "PRESET", 180, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobsRow, "BANK", 181, 0.0f, 1.0f, 2, true);

    // =========================================================================
    // RIGHT COLUMN (550px): FILTER LFO, FILTER CONTROLS, ADSR ENVELOPE
    // =========================================================================
    lv_obj_t* rightCol = lv_obj_create(tab);
    lv_obj_set_size(rightCol, 550, 640);
    lv_obj_set_style_bg_opa(rightCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightCol, 0, 0);
    lv_obj_set_style_pad_all(rightCol, 0, 0);
    lv_obj_remove_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Row 1: side-by-side LFO and Filter Cards
    lv_obj_t* row1 = lv_obj_create(rightCol);
    lv_obj_set_size(row1, 550, 260);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1.1 LFO Card
    lv_obj_t* lfoCard = lv_obj_create(row1);
    lv_obj_set_size(lfoCard, 268, 260);
    lv_obj_set_style_bg_color(lfoCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(lfoCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(lfoCard, 1, 0);
    lv_obj_set_style_radius(lfoCard, 10, 0);
    lv_obj_set_style_pad_all(lfoCard, 8, 0);
    lv_obj_remove_flag(lfoCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lfoTitle = lv_label_create(lfoCard);
    lv_label_set_text(lfoTitle, "FILTER LFO MOD");
    lv_obj_set_style_text_font(lfoTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lfoTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(lfoTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* lfoContent = lv_obj_create(lfoCard);
    lv_obj_set_size(lfoContent, 252, 220);
    lv_obj_set_style_bg_opa(lfoContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoContent, 0, 0);
    lv_obj_set_style_pad_all(lfoContent, 0, 0);
    lv_obj_align(lfoContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(lfoContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lfoContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lfoDropdownRow = lv_obj_create(lfoContent);
    lv_obj_set_size(lfoDropdownRow, 252, 45);
    lv_obj_set_style_bg_opa(lfoDropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoDropdownRow, 0, 0);
    lv_obj_set_style_pad_all(lfoDropdownRow, 0, 0);
    lv_obj_remove_flag(lfoDropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoDropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoDropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfoDropdownRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(lfoDropdownRow, "LFO SHAPE", 114, "Sine\nTriangle\nSaw\nSquare\nRandom", 0, false, 110);

    lv_obj_t* lfoKnobRow = lv_obj_create(lfoContent);
    lv_obj_set_size(lfoKnobRow, 252, 100);
    lv_obj_set_style_bg_opa(lfoKnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lfoKnobRow, 0, 0);
    lv_obj_set_style_pad_all(lfoKnobRow, 0, 0);
    lv_obj_remove_flag(lfoKnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(lfoKnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(lfoKnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lfoKnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(lfoKnobRow, "RATE", 7, 0.0f, 1.0f, 2, true);
    addSynthKnob(lfoKnobRow, "DEPTH", 8, 0.0f, 1.0f, 2, true);

    // 1.2 Filter Card
    lv_obj_t* filterCard = lv_obj_create(row1);
    lv_obj_set_size(filterCard, 268, 260);
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
    lv_obj_set_size(filterContent, 252, 220);
    lv_obj_set_style_bg_opa(filterContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterContent, 0, 0);
    lv_obj_set_style_pad_all(filterContent, 0, 0);
    lv_obj_align(filterContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(filterContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(filterContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* filterDropdownRow = lv_obj_create(filterContent);
    lv_obj_set_size(filterDropdownRow, 252, 45);
    lv_obj_set_style_bg_opa(filterDropdownRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterDropdownRow, 0, 0);
    lv_obj_set_style_pad_all(filterDropdownRow, 0, 0);
    lv_obj_remove_flag(filterDropdownRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterDropdownRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterDropdownRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterDropdownRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(filterDropdownRow, "TYPE", 20, "LowPass\nHighPass\nBandPass\nBypass", 0, false, 110);

    lv_obj_t* filterKnobRow = lv_obj_create(filterContent);
    lv_obj_set_size(filterKnobRow, 252, 100);
    lv_obj_set_style_bg_opa(filterKnobRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterKnobRow, 0, 0);
    lv_obj_set_style_pad_all(filterKnobRow, 0, 0);
    lv_obj_remove_flag(filterKnobRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterKnobRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterKnobRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterKnobRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(filterKnobRow, "CUTOFF", 112, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobRow, "RES", 113, 0.0f, 1.0f, 2, true);

    // Row 2: ADSR Card
    lv_obj_t* adsrCard = lv_obj_create(rightCol);
    lv_obj_set_size(adsrCard, 550, 360);
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
    lv_obj_set_size(faderRow, 530, 320);
    lv_obj_set_style_bg_opa(faderRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(faderRow, 0, 0);
    lv_obj_set_style_pad_all(faderRow, 0, 0);
    lv_obj_remove_flag(faderRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(faderRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(faderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(faderRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(faderRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(faderRow, "ATTACK", 100, 0.001f, 15.0f, 2, false, 240);
    addSynthSlider(faderRow, "DECAY", 101, 0.0f, 15.0f, 2, false, 240);
    addSynthSlider(faderRow, "SUSTAIN", 102, 0.0f, 1.0f, 2, true, 240);
    addSynthSlider(faderRow, "RELEASE", 103, 0.001f, 15.0f, 2, false, 240);
}

void UIManager::populateParamSoundFontSynthTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamSoundFontLibraryTab
}

void UIManager::soundfontLoadBtnCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSfSelect = true;

    const char* browseDir = getenv("HOME");
    std::string homeStr = browseDir ? std::string(browseDir) + "/Loom" : "./Loom";
    ui->mFileBrowserCurrentPath = homeStr + "/soundfonts";
    ui->openFileBrowser(false);
}

void UIManager::soundfontImportBtnCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsSfImport = true;

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
        lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);

    // =========================================================================
    // ROW 1: INPUT CONTROL (490px) & CHARACTER EQ (550px) - Height 290px
    // =========================================================================
    lv_obj_t* row1 = lv_obj_create(tab);
    lv_obj_set_size(row1, 1050, 290);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 1.1 Audio Input Control Card
    lv_obj_t* ioCard = lv_obj_create(row1);
    lv_obj_set_size(ioCard, 490, 290);
    lv_obj_set_style_bg_color(ioCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(ioCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(ioCard, 1, 0);
    lv_obj_set_style_radius(ioCard, 12, 0);
    lv_obj_set_style_pad_all(ioCard, 10, 0);
    lv_obj_remove_flag(ioCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ioTitle = lv_label_create(ioCard);
    lv_label_set_text(ioTitle, "AUDIO INPUT CONTROL");
    lv_obj_set_style_text_font(ioTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ioTitle, trackColor, 0);
    lv_obj_align(ioTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* ioContent = lv_obj_create(ioCard);
    lv_obj_set_size(ioContent, 470, 245);
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
    lv_obj_set_size(selectorsCol, 260, 235);
    lv_obj_set_style_bg_opa(selectorsCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(selectorsCol, 0, 0);
    lv_obj_set_style_pad_all(selectorsCol, 0, 0);
    lv_obj_remove_flag(selectorsCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(selectorsCol, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selectorsCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(selectorsCol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Source Selector Dropdown
    lv_obj_t* sourceRow = lv_obj_create(selectorsCol);
    lv_obj_set_size(sourceRow, 260, 70);
    lv_obj_set_style_bg_opa(sourceRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sourceRow, 0, 0);
    lv_obj_set_style_pad_all(sourceRow, 0, 0);
    lv_obj_remove_flag(sourceRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(sourceRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(sourceRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sourceRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* srcLbl = lv_label_create(sourceRow);
    lv_label_set_text(srcLbl, "INPUT SELECTOR");
    lv_obj_set_style_text_font(srcLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(srcLbl, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t* srcDd = lv_dropdown_create(sourceRow);
    lv_obj_set_size(srcDd, 250, 36);
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
    lv_obj_set_size(gateRow, 260, 70);
    lv_obj_set_style_bg_opa(gateRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gateRow, 0, 0);
    lv_obj_set_style_pad_all(gateRow, 0, 0);
    lv_obj_remove_flag(gateRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(gateRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(gateRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gateRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* gateLbl = lv_label_create(gateRow);
    lv_label_set_text(gateLbl, "MODE / GATE TYPE");
    lv_obj_set_style_text_font(gateLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(gateLbl, lv_color_hex(0xCCCCCC), 0);

    addSynthDropdown(gateRow, "GATE MODE", 120, "GATED\nOPEN", 0, false, 250);

    // Right half: Gain & Fold Knobs
    lv_obj_t* knobsRow = lv_obj_create(ioContent);
    lv_obj_set_size(knobsRow, 190, 235);
    lv_obj_set_style_bg_opa(knobsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knobsRow, 0, 0);
    lv_obj_set_style_pad_all(knobsRow, 0, 0);
    lv_obj_remove_flag(knobsRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(knobsRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(knobsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(knobsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(knobsRow, "GAIN", 121, 0.0f, 1.0f, 2, true);
    addSynthKnob(knobsRow, "FOLD", 122, 0.0f, 1.0f, 2, true);

    // 1.2 Character EQ card
    lv_obj_t* eqCard = lv_obj_create(row1);
    lv_obj_set_size(eqCard, 550, 290);
    lv_obj_set_style_bg_color(eqCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(eqCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(eqCard, 1, 0);
    lv_obj_set_style_radius(eqCard, 12, 0);
    lv_obj_set_style_pad_all(eqCard, 8, 0);
    lv_obj_remove_flag(eqCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* eqTitle = lv_label_create(eqCard);
    lv_label_set_text(eqTitle, "CHARACTER EQ (+/- 12dB)");
    lv_obj_set_style_text_font(eqTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(eqTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(eqTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* eqFadersRow = lv_obj_create(eqCard);
    lv_obj_set_size(eqFadersRow, 530, 250);
    lv_obj_set_style_bg_opa(eqFadersRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eqFadersRow, 0, 0);
    lv_obj_set_style_pad_all(eqFadersRow, 0, 0);
    lv_obj_remove_flag(eqFadersRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(eqFadersRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(eqFadersRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eqFadersRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(eqFadersRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(eqFadersRow, "LOW", 1530, 0.0f, 1.0f, 2, true, 180);
    addSynthSlider(eqFadersRow, "L-MID", 1531, 0.0f, 1.0f, 2, true, 180);
    addSynthSlider(eqFadersRow, "MID", 1532, 0.0f, 1.0f, 2, true, 180);
    addSynthSlider(eqFadersRow, "H-MID", 1533, 0.0f, 1.0f, 2, true, 180);
    addSynthSlider(eqFadersRow, "HIGH", 1534, 0.0f, 1.0f, 2, true, 180);

    // =========================================================================
    // ROW 2: FILTER (370px), AMP ENVELOPE (330px), FILTER ENVELOPE (330px) - Height 350px
    // =========================================================================
    lv_obj_t* row2 = lv_obj_create(tab);
    lv_obj_set_size(row2, 1050, 350);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_remove_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(row2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 2.1 Filter Card (370px)
    lv_obj_t* filterCard = lv_obj_create(row2);
    lv_obj_set_size(filterCard, 370, 350);
    lv_obj_set_style_bg_color(filterCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(filterCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(filterCard, 1, 0);
    lv_obj_set_style_radius(filterCard, 12, 0);
    lv_obj_set_style_pad_all(filterCard, 8, 0);
    lv_obj_remove_flag(filterCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* filterTitle = lv_label_create(filterCard);
    lv_label_set_text(filterTitle, "FILTER CONTROLS");
    lv_obj_set_style_text_font(filterTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(filterTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(filterTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* filterContent = lv_obj_create(filterCard);
    lv_obj_set_size(filterContent, 350, 310);
    lv_obj_set_style_bg_opa(filterContent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterContent, 0, 0);
    lv_obj_set_style_pad_all(filterContent, 0, 0);
    lv_obj_align(filterContent, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(filterContent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterContent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(filterContent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* typeBox = lv_obj_create(filterContent);
    lv_obj_set_size(typeBox, 350, 48);
    lv_obj_set_style_bg_opa(typeBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(typeBox, 0, 0);
    lv_obj_set_style_pad_all(typeBox, 0, 0);
    lv_obj_remove_flag(typeBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(typeBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(typeBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(typeBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthDropdown(typeBox, "FILTER MODE", 123, "LP (Lowpass)\nHP (Highpass)\nBP (Bandpass)", 0, false, 200);

    lv_obj_t* filterKnobs = lv_obj_create(filterContent);
    lv_obj_set_size(filterKnobs, 350, 240);
    lv_obj_set_style_bg_opa(filterKnobs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterKnobs, 0, 0);
    lv_obj_set_style_pad_all(filterKnobs, 0, 0);
    lv_obj_remove_flag(filterKnobs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterKnobs, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterKnobs, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(filterKnobs, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    addSynthKnob(filterKnobs, "CUTOFF", 112, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobs, "RESONANCE", 113, 0.0f, 1.0f, 2, true);
    addSynthKnob(filterKnobs, "ENV AMOUNT", 118, -1.0f, 1.0f, 2, false);

    // 2.2 Amp Envelope Card (330px)
    lv_obj_t* ampCard = lv_obj_create(row2);
    lv_obj_set_size(ampCard, 330, 350);
    lv_obj_set_style_bg_color(ampCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(ampCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(ampCard, 1, 0);
    lv_obj_set_style_radius(ampCard, 12, 0);
    lv_obj_set_style_pad_all(ampCard, 8, 0);
    lv_obj_remove_flag(ampCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ampTitle = lv_label_create(ampCard);
    lv_label_set_text(ampTitle, "AMP ENVELOPE (ADSR)");
    lv_obj_set_style_text_font(ampTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ampTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(ampTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* ampRow = lv_obj_create(ampCard);
    lv_obj_set_size(ampRow, 310, 310);
    lv_obj_set_style_bg_opa(ampRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ampRow, 0, 0);
    lv_obj_set_style_pad_all(ampRow, 0, 0);
    lv_obj_remove_flag(ampRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(ampRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ampRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ampRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(ampRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(ampRow, "A", 100, 0.001f, 15.0f, 2, false, 230);
    addSynthSlider(ampRow, "D", 101, 0.0f, 15.0f, 2, false, 230);
    addSynthSlider(ampRow, "S", 102, 0.0f, 1.0f, 2, true, 230);
    addSynthSlider(ampRow, "R", 103, 0.001f, 15.0f, 2, false, 230);

    // 2.3 Filter Envelope Card (330px)
    lv_obj_t* filterEnvCard = lv_obj_create(row2);
    lv_obj_set_size(filterEnvCard, 330, 350);
    lv_obj_set_style_bg_color(filterEnvCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(filterEnvCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(filterEnvCard, 1, 0);
    lv_obj_set_style_radius(filterEnvCard, 12, 0);
    lv_obj_set_style_pad_all(filterEnvCard, 8, 0);
    lv_obj_remove_flag(filterEnvCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* filterEnvTitle = lv_label_create(filterEnvCard);
    lv_label_set_text(filterEnvTitle, "FILTER ENVELOPE (ADSR)");
    lv_obj_set_style_text_font(filterEnvTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(filterEnvTitle, lv_color_hex(0x888888), 0);
    lv_obj_align(filterEnvTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* filterEnvRow = lv_obj_create(filterEnvCard);
    lv_obj_set_size(filterEnvRow, 310, 310);
    lv_obj_set_style_bg_opa(filterEnvRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(filterEnvRow, 0, 0);
    lv_obj_set_style_pad_all(filterEnvRow, 0, 0);
    lv_obj_remove_flag(filterEnvRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(filterEnvRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(filterEnvRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(filterEnvRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(filterEnvRow, LV_ALIGN_BOTTOM_MID, 0, 0);

    addSynthSlider(filterEnvRow, "A", 114, 0.001f, 15.0f, 2, false, 230);
    addSynthSlider(filterEnvRow, "D", 115, 0.0f, 15.0f, 2, false, 230);
    addSynthSlider(filterEnvRow, "S", 116, 0.0f, 1.0f, 2, true, 230);
    addSynthSlider(filterEnvRow, "R", 117, 0.001f, 15.0f, 2, false, 230);
}

void UIManager::populateParamAudioInFilterEnvTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamAudioInTab
}

void UIManager::audioInSourceDropdownEventCb(lv_event_t* e) {
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    int sel = lv_dropdown_get_selected(dd);
    ui->mEngine.setRecordingSource(sel); // 0 = MIC, 1 = LINE_IN, 2 = RESAMPLE
    if (sel == 0) {
        switchCaptureDevice(ui->mSettingsAudioMicDevice);
    } else if (sel == 1) {
        switchCaptureDevice(ui->mSettingsAudioLineInDevice);
    }
    const char* srcNames[] = {"MIC", "LINE-IN", "RESAMPLE"};
    std::cout << "Audio Input Source changed to: " << srcNames[sel % 3] << std::endl;
}

void UIManager::populateParamFmDrumTab1(lv_obj_t* tab) {
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 4, 0);

    addDrumVoiceStrip(tab, "KICK", 0);
    addDrumVoiceStrip(tab, "SNARE", 1);
    addDrumVoiceStrip(tab, "TOM", 2);
    addDrumVoiceStrip(tab, "HIHAT", 3);
    addDrumVoiceStrip(tab, "OHH", 4);
    addDrumVoiceStrip(tab, "CYMB", 5);
    addDrumVoiceStrip(tab, "PERC", 6);
    addDrumVoiceStrip(tab, "NOISE", 7);
}

void UIManager::populateParamFmDrumTab2(lv_obj_t* tab) {
    // Deprecated: merged into populateParamFmDrumTab1
}

void UIManager::addDrumVoiceStrip(lv_obj_t* parent, const char* name, int drumIdx) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 126, 640);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

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
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 4, 0);

    addAnalogDrumVoiceStrip(tab, "KICK", 0);
    addAnalogDrumVoiceStrip(tab, "SNARE", 1);
    addAnalogDrumVoiceStrip(tab, "CLAP", 2);
    addAnalogDrumVoiceStrip(tab, "HAT C", 3);
    addAnalogDrumVoiceStrip(tab, "HAT O", 4);
    addAnalogDrumVoiceStrip(tab, "CYMBAL", 5);
    addAnalogDrumVoiceStrip(tab, "PERC", 6);
    addAnalogDrumVoiceStrip(tab, "NOISE", 7);
}

void UIManager::populateParamAnalogDrumTab2(lv_obj_t* tab) {
    // Deprecated: merged into populateParamAnalogDrumTab1
}

void UIManager::addAnalogDrumVoiceStrip(lv_obj_t* parent, const char* name, int drumIdx) {
    lv_color_t trackColor = getTrackColor(mActiveTrack);
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 126, 640);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, trackColor, 0);

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
    } else if (drumIdx == 2) { // RIM / CLAP: DCY (0), COL (1), TUNE (2), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("COL", baseParam + 1);
        addMiniDrumKnob("TUNE", baseParam + 2);
        addMiniDrumKnob("GAIN", baseParam + 5);
    } else if (drumIdx == 3 || drumIdx == 4) { // HAT C & HAT O: DCY (0), COL (1), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("COL", baseParam + 1);
        addMiniDrumKnob("GAIN", baseParam + 5);
        
        // Spacer to keep vertical balance
        lv_obj_t* spacer = lv_obj_create(card);
        lv_obj_set_size(spacer, 74, 95);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
    } else if (drumIdx == 5) { // CYMBAL: ATK (3), DCY (0), COL (1), GAIN (5)
        addMiniDrumKnob("ATK", baseParam + 3);
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("COL", baseParam + 1);
        addMiniDrumKnob("GAIN", baseParam + 5);
    } else if (drumIdx == 6) { // PERC: DCY (0), TONE (1), TUNE (2), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("TONE", baseParam + 1);
        addMiniDrumKnob("TUNE", baseParam + 2);
        addMiniDrumKnob("GAIN", baseParam + 5);
    } else if (drumIdx == 7) { // NOISE: DCY (0), TONE (1), GAIN (5)
        addMiniDrumKnob("DCY", baseParam + 0);
        addMiniDrumKnob("TONE", baseParam + 1);
        addMiniDrumKnob("GAIN", baseParam + 5);

        // Spacer to keep vertical balance
        lv_obj_t* spacer = lv_obj_create(card);
        lv_obj_set_size(spacer, 74, 95);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
    }
}

void UIManager::populateParamMidiRoutingTab(lv_obj_t* tab) {
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_row(tab, 12, 0);

    lv_color_t trackColor = getTrackColor(mActiveTrack);
    auto& track = mEngine.getTracks()[mActiveTrack];

    mEngine.scanMidiDevices();

    // =========================================================================
    // TOP ROUTING CONTAINER (1050x95px)
    // =========================================================================
    lv_obj_t* routingCard = lv_obj_create(tab);
    lv_obj_set_size(routingCard, 1050, 95);
    lv_obj_set_style_bg_color(routingCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(routingCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(routingCard, 1, 0);
    lv_obj_set_style_radius(routingCard, 12, 0);
    lv_obj_set_style_pad_all(routingCard, 8, 0);
    lv_obj_remove_flag(routingCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* routingTitle = lv_label_create(routingCard);
    lv_label_set_text(routingTitle, "MIDI ROUTING");
    lv_obj_set_style_text_font(routingTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(routingTitle, trackColor, 0);
    lv_obj_align(routingTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* routingRow = lv_obj_create(routingCard);
    lv_obj_set_size(routingRow, 1030, 60);
    lv_obj_set_style_bg_opa(routingRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(routingRow, 0, 0);
    lv_obj_set_style_pad_all(routingRow, 0, 0);
    lv_obj_align(routingRow, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(routingRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(routingRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(routingRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(routingRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // TARGET DEVICE ITEM
    lv_obj_t* targetBox = lv_obj_create(routingRow);
    lv_obj_set_size(targetBox, 330, 52);
    lv_obj_set_style_bg_color(targetBox, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(targetBox, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(targetBox, 1, 0);
    lv_obj_set_style_radius(targetBox, 8, 0);
    lv_obj_set_layout(targetBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(targetBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(targetBox, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(targetBox, 8, 0);

    lv_obj_t* targetLbl = lv_label_create(targetBox);
    lv_label_set_text(targetLbl, "Target Device");
    lv_obj_set_style_text_font(targetLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(targetLbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* targetDd = lv_dropdown_create(targetBox);
    lv_obj_set_size(targetDd, 200, 32);
    lv_obj_set_style_text_font(targetDd, &lv_font_montserrat_12, 0);
    
    std::string deviceOptions = "ALL\n";
    for (const auto& dev : mEngine.mMidiDevices) {
        if (dev.isOutput) {
            deviceOptions += dev.name + "\n";
        }
    }
    if (!deviceOptions.empty()) deviceOptions.pop_back();
    lv_dropdown_set_options(targetDd, deviceOptions.c_str());
    
    int selectedIdx = 0;
    if (track.targetMidiDevice != "ALL") {
        int idx = 1;
        for (size_t i = 0; i < mEngine.mMidiDevices.size(); ++i) {
            if (mEngine.mMidiDevices[i].isOutput) {
                if (mEngine.mMidiDevices[i].name == track.targetMidiDevice) {
                    selectedIdx = idx;
                    break;
                }
                idx++;
            }
        }
    }
    lv_dropdown_set_selected(targetDd, selectedIdx);

    lv_obj_add_event_cb(targetDd, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = lv_event_get_target_obj(e);
        char buf[128];
        lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
        std::string selectedName = buf;
        
        auto& trk = ui->mEngine.getTracks()[ui->mActiveTrack];
        if (selectedName == "ALL") {
            trk.targetMidiDevice = "ALL";
        } else {
            trk.targetMidiDevice = selectedName;
        }
        std::cout << "MIDI Engine set target device: " << trk.targetMidiDevice << std::endl;
    }, LV_EVENT_VALUE_CHANGED, this);

    // OUT CHANNEL ITEM
    lv_obj_t* outBox = lv_obj_create(routingRow);
    lv_obj_set_size(outBox, 330, 52);
    lv_obj_set_style_bg_color(outBox, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(outBox, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(outBox, 1, 0);
    lv_obj_set_style_radius(outBox, 8, 0);
    lv_obj_set_layout(outBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(outBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(outBox, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(outBox, 8, 0);

    lv_obj_t* outLbl = lv_label_create(outBox);
    lv_label_set_text(outLbl, "Out Channel");
    lv_obj_set_style_text_font(outLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(outLbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* outDd = lv_dropdown_create(outBox);
    lv_obj_set_size(outDd, 200, 32);
    lv_obj_set_style_text_font(outDd, &lv_font_montserrat_12, 0);
    
    std::string chanOptions = "OFF\n";
    for (int c = 1; c <= 16; ++c) {
        chanOptions += "Channel " + std::to_string(c) + "\n";
    }
    if (!chanOptions.empty()) chanOptions.pop_back();
    lv_dropdown_set_options(outDd, chanOptions.c_str());
    lv_dropdown_set_selected(outDd, track.midiOutChannel);

    lv_obj_add_event_cb(outDd, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = lv_event_get_target_obj(e);
        int selected = lv_dropdown_get_selected(dd);
        ui->mEngine.setParameter(ui->mActiveTrack, 901, (float)selected);
    }, LV_EVENT_VALUE_CHANGED, this);

    // IN CHANNEL ITEM
    lv_obj_t* inBox = lv_obj_create(routingRow);
    lv_obj_set_size(inBox, 330, 52);
    lv_obj_set_style_bg_color(inBox, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_color(inBox, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(inBox, 1, 0);
    lv_obj_set_style_radius(inBox, 8, 0);
    lv_obj_set_layout(inBox, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(inBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(inBox, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(inBox, 8, 0);

    lv_obj_t* inLbl = lv_label_create(inBox);
    lv_label_set_text(inLbl, "In Channel");
    lv_obj_set_style_text_font(inLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(inLbl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* inDd = lv_dropdown_create(inBox);
    lv_obj_set_size(inDd, 200, 32);
    lv_obj_set_style_text_font(inDd, &lv_font_montserrat_12, 0);
    
    std::string inChanOptions = "NONE\n";
    for (int c = 1; c <= 16; ++c) {
        inChanOptions += "Channel " + std::to_string(c) + "\n";
    }
    inChanOptions += "ALL";
    lv_dropdown_set_options(inDd, inChanOptions.c_str());
    lv_dropdown_set_selected(inDd, track.midiInChannel);

    lv_obj_add_event_cb(inDd, [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        lv_obj_t* dd = lv_event_get_target_obj(e);
        int selected = lv_dropdown_get_selected(dd);
        ui->mEngine.setParameter(ui->mActiveTrack, 900, (float)selected);
    }, LV_EVENT_VALUE_CHANGED, this);

    // =========================================================================
    // BOTTOM CONTROLLER MAPPING CARD & SCROLLABLE LIST (1050x535px)
    // =========================================================================
    lv_obj_t* mappingCard = lv_obj_create(tab);
    lv_obj_set_size(mappingCard, 1050, 535);
    lv_obj_set_style_bg_color(mappingCard, lv_color_hex(0x161616), 0);
    lv_obj_set_style_border_color(mappingCard, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(mappingCard, 1, 0);
    lv_obj_set_style_radius(mappingCard, 12, 0);
    lv_obj_set_style_pad_all(mappingCard, 8, 0);
    lv_obj_remove_flag(mappingCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* mapTitle = lv_label_create(mappingCard);
    lv_label_set_text(mapTitle, "CONTROLLER MAPPING (VIRTUAL CONTROLS)");
    lv_obj_set_style_text_font(mapTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mapTitle, trackColor, 0);
    lv_obj_align(mapTitle, LV_ALIGN_TOP_LEFT, 2, 0);

    lv_obj_t* listContainer = lv_obj_create(mappingCard);
    lv_obj_set_size(listContainer, 1030, 490);
    lv_obj_align(listContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listContainer, 0, 0);
    lv_obj_set_style_pad_all(listContainer, 6, 0);
    lv_obj_set_layout(listContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(listContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(listContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(listContainer, 8, 0);
    lv_obj_add_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);

    std::string ccOptions = "";
    for (int i = 0; i <= 127; ++i) {
        ccOptions += "CC " + std::to_string(i) + "\n";
    }
    if (!ccOptions.empty()) ccOptions.pop_back();

    auto createMappingRow = [&](const std::string& labelText, int parameterId, bool isKnob, int index) {
        lv_obj_t* row = lv_obj_create(listContainer);
        lv_obj_set_size(row, 1000, 48);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1E1E1E), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, labelText.c_str());
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(lbl, 120);

        lv_obj_t* slider = lv_slider_create(row);
        lv_obj_set_size(slider, 320, 10);
        lv_slider_set_range(slider, 0, 127);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, trackColor, LV_PART_INDICATOR);
        lv_slider_set_value(slider, (int)(track.parameters[parameterId] * 127.f), LV_ANIM_OFF);
        
        struct SliderUserData {
            UIManager* ui;
            int paramId;
        };
        SliderUserData* sud = new SliderUserData{this, parameterId};
        lv_obj_add_event_cb(slider, [](lv_event_t* e) {
            SliderUserData* data = (SliderUserData*)lv_event_get_user_data(e);
            lv_obj_t* sliderObj = lv_event_get_target_obj(e);
            float normVal = lv_slider_get_value(sliderObj) / 127.f;
            data->ui->mEngine.setParameter(data->ui->mActiveTrack, data->paramId, normVal);
        }, LV_EVENT_VALUE_CHANGED, sud);
        lv_obj_add_event_cb(slider, [](lv_event_t* e) {
            SliderUserData* data = (SliderUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, sud);

        lv_obj_t* ccDd = lv_dropdown_create(row);
        lv_obj_set_size(ccDd, 140, 32);
        lv_obj_set_style_text_font(ccDd, &lv_font_montserrat_10, 0);
        lv_dropdown_set_options(ccDd, ccOptions.c_str());
        
        int currentCc = isKnob ? track.midiOutCcKnob[index] : track.midiOutCcFader[index];
        lv_dropdown_set_selected(ccDd, currentCc);
        
        struct DdUserData {
            UIManager* ui;
            bool isKnobType;
            int idx;
        };
        DdUserData* dud = new DdUserData{this, isKnob, index};
        lv_obj_add_event_cb(ccDd, [](lv_event_t* e) {
            DdUserData* data = (DdUserData*)lv_event_get_user_data(e);
            lv_obj_t* ddObj = lv_event_get_target_obj(e);
            int cc = lv_dropdown_get_selected(ddObj);
            auto& trk = data->ui->mEngine.getTracks()[data->ui->mActiveTrack];
            if (data->isKnobType) {
                trk.midiOutCcKnob[data->idx] = cc;
            } else {
                trk.midiOutCcFader[data->idx] = cc;
            }
            std::cout << "Updated MIDI Out CC for " << (data->isKnobType ? "Knob" : "Fader") << " " << (data->idx + 1) << " to CC " << cc << std::endl;
        }, LV_EVENT_VALUE_CHANGED, dud);
        lv_obj_add_event_cb(ccDd, [](lv_event_t* e) {
            DdUserData* data = (DdUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, dud);

        lv_obj_t* ccMappingLbl = lv_label_create(row);
        lv_obj_set_width(ccMappingLbl, 180);
        
        int hwCc = -1;
        int hwChan = -1;
        for (int k = 0; k < mSettingsKnobCount; ++k) {
            if (mSeqMidiKnobParam[mActiveTrack][k] == parameterId) {
                hwCc = mSeqMidiKnobCC[mActiveTrack][k];
                hwChan = mSeqMidiKnobChannel[mActiveTrack][k];
                break;
            }
        }
        if (hwCc == -1) {
            for (int f = 0; f < mSettingsSliderCount; ++f) {
                if (mSeqMidiFaderParam[mActiveTrack][f] == parameterId) {
                    hwCc = mSeqMidiFaderCC[mActiveTrack][f];
                    hwChan = mSeqMidiFaderChannel[mActiveTrack][f];
                    break;
                }
            }
        }

        if (hwCc != -1) {
            lv_label_set_text_fmt(ccMappingLbl, "IN: CC %d (Ch %d)", hwCc, hwChan);
            lv_obj_set_style_text_color(ccMappingLbl, trackColor, 0);
        } else {
            lv_label_set_text(ccMappingLbl, "IN: Unmapped");
            lv_obj_set_style_text_color(ccMappingLbl, lv_color_hex(0x777777), 0);
        }
        lv_obj_set_style_text_font(ccMappingLbl, &lv_font_montserrat_10, 0);

        lv_obj_t* learnBtn = lv_button_create(row);
        lv_obj_set_size(learnBtn, 100, 32);
        
        bool isThisLearning = (mMidiLearnActive && mMidiLearnTargetParamId == parameterId);
        if (isThisLearning) {
            lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0xD32F2F), 0);
        } else {
            lv_obj_set_style_bg_color(learnBtn, lv_color_hex(0x333333), 0);
        }
        lv_obj_set_style_radius(learnBtn, 4, 0);
        
        lv_obj_t* learnBtnLbl = lv_label_create(learnBtn);
        lv_label_set_text(learnBtnLbl, isThisLearning ? "LISTENING" : "LEARN");
        lv_obj_set_style_text_font(learnBtnLbl, &lv_font_montserrat_10, 0);
        lv_obj_center(learnBtnLbl);

        struct LearnUserData {
            UIManager* ui;
            int paramId;
            lv_obj_t* btnLbl;
        };
        LearnUserData* lud = new LearnUserData{this, parameterId, learnBtnLbl};
        
        lv_obj_add_event_cb(learnBtn, [](lv_event_t* e) {
            LearnUserData* data = (LearnUserData*)lv_event_get_user_data(e);
            lv_obj_t* btnObj = lv_event_get_target_obj(e);
            
            data->ui->mMidiLearnActive = !data->ui->mMidiLearnActive;
            if (data->ui->mMidiLearnActive) {
                data->ui->mMidiLearnTargetParamId = data->paramId;
                lv_label_set_text(data->btnLbl, "LISTENING");
                lv_obj_set_style_bg_color(btnObj, lv_color_hex(0xD32F2F), 0);
            } else {
                data->ui->mMidiLearnTargetParamId = -1;
                lv_label_set_text(data->btnLbl, "LEARN");
                lv_obj_set_style_bg_color(btnObj, lv_color_hex(0x333333), 0);
            }
        }, LV_EVENT_CLICKED, lud);
        lv_obj_add_event_cb(learnBtn, [](lv_event_t* e) {
            LearnUserData* data = (LearnUserData*)lv_event_get_user_data(e);
            delete data;
        }, LV_EVENT_DELETE, lud);
    };

    for (int i = 0; i < mSettingsKnobCount; ++i) {
        createMappingRow("V-KNOB " + std::to_string(i + 1), 2400 + i, true, i);
    }
    
    for (int i = 0; i < mSettingsSliderCount; ++i) {
        createMappingRow("V-FADER " + std::to_string(i + 1), 2416 + i, false, i);
    }
}

void UIManager::populateParamMidiMappingTab(lv_obj_t* tab) {
    // Deprecated: merged into populateParamMidiRoutingTab
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
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 2.0f));  // R

        // Filter ADSR
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 2.0f));  // R

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
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 2.0f));  // R

        // Filter EG
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 2.0f));  // R
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
            ui->mEngine.setParameter(activeTrk, base + 2, rRange(0.01f, 2.0f));  // Decay
            ui->mEngine.setParameter(activeTrk, base + 3, rRange(0.0f, 1.0f));   // Sustain
            ui->mEngine.setParameter(activeTrk, base + 4, rRange(0.01f, 2.0f));  // Release
            
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
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 2.0f));  // R

        // Filter ADSR
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 2.0f));  // R
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
        for (int d = 0; d < 8; ++d) {
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
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 2.0f));  // R

        // Filter ADSR
        ui->mEngine.setParameter(activeTrk, 114, rRange(0.001f, 1.5f)); // A
        ui->mEngine.setParameter(activeTrk, 115, rRange(0.01f, 2.0f));  // D
        ui->mEngine.setParameter(activeTrk, 116, rRange(0.0f, 1.0f));   // S
        ui->mEngine.setParameter(activeTrk, 117, rRange(0.01f, 2.0f));  // R

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
        ui->mEngine.setParameter(activeTrk, 103, rRange(0.01f, 2.0f));  // R

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
    ui->resetFileBrowserFlags();
    ui->mFileBrowserIsPresetLoad = true;
    ui->openFileBrowser(false);
}

void UIManager::savePatchBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->resetFileBrowserFlags();
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
        mSeqMidiKnobChannel[trackIdx][k] = 0;
        mSeqMidiKnobParam[trackIdx][k] = -1;
        mSeqMidiKnobValue[trackIdx][k] = 0.5f;
        mSeqMidiKnobInverted[trackIdx][k] = false;
    }
    for (int f = 0; f < 24; ++f) {
        mSeqMidiFaderCC[trackIdx][f] = 12 + f;
        mSeqMidiFaderChannel[trackIdx][f] = 0;
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
    } else if (engineType == 5) { // FM Drum
        // Knobs 1-4: BD Decay, SD Decay, CH Decay, OH Decay
        mSeqMidiKnobParam[trackIdx][0] = 202; // BD Decay
        mSeqMidiKnobParam[trackIdx][1] = 212; // SD Decay
        mSeqMidiKnobParam[trackIdx][2] = 232; // CH Decay
        mSeqMidiKnobParam[trackIdx][3] = 242; // OH Decay

        // Faders 1-4: BD Tune, SD Tune, CH Tune, CYM Decay
        mSeqMidiFaderParam[trackIdx][0] = 200; // BD Tune
        mSeqMidiFaderParam[trackIdx][1] = 210; // SD Tune
        mSeqMidiFaderParam[trackIdx][2] = 230; // CH Tune
        mSeqMidiFaderParam[trackIdx][3] = 252; // CYM Decay
    } else if (engineType == 6) { // Analogue Drum
        // Knobs 1-4: BD Decay, SD Decay, CH Decay, OH Decay
        mSeqMidiKnobParam[trackIdx][0] = 600; // BD Decay
        mSeqMidiKnobParam[trackIdx][1] = 610; // SD Decay
        mSeqMidiKnobParam[trackIdx][2] = 630; // CH Decay
        mSeqMidiKnobParam[trackIdx][3] = 640; // OH Decay

        // Faders 1-4: BD Tune, SD Tune, RIM Decay, CYM Decay
        mSeqMidiFaderParam[trackIdx][0] = 602; // BD Tune
        mSeqMidiFaderParam[trackIdx][1] = 612; // SD Tune
        mSeqMidiFaderParam[trackIdx][2] = 620; // RIM Decay
        mSeqMidiFaderParam[trackIdx][3] = 650; // CYM Decay
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
    file << "KNOB_COUNT:" << mSettingsKnobCount << "\n";
    file << "SLIDER_COUNT:" << mSettingsSliderCount << "\n";
    file << "VELOCITY_SENSITIVITY:" << (mEngine.getVelocitySensitivityEnabled() ? 1 : 0) << "\n";
    file << "FAST_GRANULAR:" << (mEngine.getFastGranularEnabled() ? 1 : 0) << "\n";
    file << "AUDIO_OUTPUT_MODE:" << mEngine.getAudioOutputMode() << "\n";
    file << "AUDIO_DEVICE:" << mSettingsAudioDevice << "\n";
    file << "AUDIO_MIC_DEVICE:" << mSettingsAudioMicDevice << "\n";
    file << "AUDIO_LINE_IN_DEVICE:" << mSettingsAudioLineInDevice << "\n";
    file << "BRIGHTNESS:" << mSettingsBacklightBrightness << "\n";
    file << "SCREEN_TIMEOUT:" << mSettingsScreenTimeoutSec << "\n";
    file << "PLAY_VOICE_LINK:" << (mPlayVoiceLinkPoly ? 1 : 0) << "\n";

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

    file << "DRUM_ROW_TRACK:" << mDrumRowTargetTrack << "\n";
    file << "DRUM_ROW_NOTES:";
    for (int i = 0; i < 8; ++i) file << mDrumRowNotes[i] << (i < 7 ? " " : "");
    file << "\n";
    file << "DRUM_ROW_RATCHETS:";
    for (int i = 0; i < 8; ++i) file << mDrumRowRatchets[i] << (i < 7 ? " " : "");
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
            file << "KNOB_MAP:" << t << ":" << k << ":" << mSeqMidiKnobCC[t][k] << ":" << mSeqMidiKnobParam[t][k] << ":" << mSeqMidiKnobValue[t][k] << ":" << (mSeqMidiKnobInverted[t][k] ? 1 : 0) << ":" << mSeqMidiKnobChannel[t][k] << "\n";
        }
        for (int f = 0; f < 24; ++f) {
            file << "FADER_MAP:" << t << ":" << f << ":" << mSeqMidiFaderCC[t][f] << ":" << mSeqMidiFaderParam[t][f] << ":" << mSeqMidiFaderValue[t][f] << ":" << (mSeqMidiFaderInverted[t][f] ? 1 : 0) << ":" << mSeqMidiFaderChannel[t][f] << "\n";
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
            else if (key == "KNOB_COUNT") mSettingsKnobCount = std::stoi(val);
            else if (key == "SLIDER_COUNT") mSettingsSliderCount = std::stoi(val);
            else if (key == "VELOCITY_SENSITIVITY") mEngine.setVelocitySensitivityEnabled(std::stoi(val) != 0);
            else if (key == "FAST_GRANULAR") mEngine.setFastGranularEnabled(std::stoi(val) != 0);
            else if (key == "AUDIO_OUTPUT_MODE") mEngine.setAudioOutputMode(std::stoi(val));
            else if (key == "AUDIO_DEVICE") {
                mSettingsAudioDevice = val;
                switchAudioDevice(val);
            }
            else if (key == "AUDIO_MIC_DEVICE") {
                mSettingsAudioMicDevice = val;
                if (mEngine.mRecordingSource.load() == 0) {
                    gCurrentCaptureDevice = val;
                    switchCaptureDevice(val);
                }
            }
            else if (key == "AUDIO_LINE_IN_DEVICE") {
                mSettingsAudioLineInDevice = val;
                if (mEngine.mRecordingSource.load() == 1) {
                    gCurrentCaptureDevice = val;
                    switchCaptureDevice(val);
                }
            }
            else if (key == "BRIGHTNESS") {
                mSettingsBacklightBrightness = std::clamp(std::stoi(val), 10, 100);
                HardwareDisplay::setBrightness(mSettingsBacklightBrightness);
            }
            else if (key == "SCREEN_TIMEOUT") {
                mSettingsScreenTimeoutSec = std::stoi(val);
            }
            else if (key == "PLAY_VOICE_LINK") {
                mPlayVoiceLinkPoly = std::stoi(val) != 0;
            }
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
            } else if (key == "DRUM_ROW_TRACK") {
                mDrumRowTargetTrack = std::stoi(val);
            } else if (key == "DRUM_ROW_NOTES") {
                std::stringstream ss(val);
                for (int i = 0; i < 8; ++i) {
                    if (ss >> mDrumRowNotes[i]) {}
                }
            } else if (key == "DRUM_ROW_RATCHETS") {
                std::stringstream ss(val);
                for (int i = 0; i < 8; ++i) {
                    if (ss >> mDrumRowRatchets[i]) {}
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
                int ch = 0;
                char colon;
                ss >> t >> colon >> k >> colon >> cc >> colon >> p >> colon >> v;
                if (ss.peek() == ':') {
                    ss >> colon >> inv;
                    if (ss.peek() == ':') {
                        ss >> colon >> ch;
                    }
                }
                if (t >= 0 && t < 8 && k >= 0 && k < 24) {
                    mSeqMidiKnobCC[t][k] = cc;
                    mSeqMidiKnobChannel[t][k] = ch;
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
                int ch = 0;
                char colon;
                ss >> t >> colon >> f >> colon >> cc >> colon >> p >> colon >> v;
                if (ss.peek() == ':') {
                    ss >> colon >> inv;
                    if (ss.peek() == ':') {
                        ss >> colon >> ch;
                    }
                }
                if (t >= 0 && t < 8 && f >= 0 && f < 24) {
                    mSeqMidiFaderCC[t][f] = cc;
                    mSeqMidiFaderChannel[t][f] = ch;
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
    mMidiWakeRequested = true;

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
    mMidiLogDirty = true;
}

static std::vector<std::string> runCommandAndGetLines(const std::string& cmd);

void UIManager::settingsUpdateBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui) return;
    if (ui->mUpdateInstallActive) return;

    ui->mUpdateInstallActive = true;
    ui->mUpdateInstallFinished = false;
    ui->mUpdateInstallStatusStr = "Checking for updates...";
    ui->mUpdateInstallProgressPercent = 10;

    std::thread updateThread([ui]() {
        ui->mUpdateInstallStatusStr = "Checking for updates...";

        // 1. Detect current branch
        std::string branch = "main";
        auto branchLines = runCommandAndGetLines("git rev-parse --abbrev-ref HEAD");
        if (!branchLines.empty() && !branchLines[0].empty() && branchLines[0] != "HEAD") {
            branch = branchLines[0];
        }

        // 2. Fetch updates for this branch
        ui->mUpdateInstallStatusStr = "Fetching " + branch + "...";
        std::string fetchCmd = "git fetch origin " + branch;
        int ret = std::system(fetchCmd.c_str());
        if (ret != 0) {
            ret = std::system("git fetch origin");
            if (ret != 0) {
                ui->mUpdateInstallStatusStr = "Network error: Could not check for updates.";
                ui->mUpdateInstallFinished = true;
                ui->mUpdateInstallActive = false;
                return;
            }
        }
        ui->mUpdateInstallProgressPercent = 25;
        
        // 3. Compare local HEAD against origin/<branch>
        std::string localHash = "";
        std::string remoteHash = "";
        auto localLines = runCommandAndGetLines("git rev-parse HEAD");
        if (!localLines.empty()) localHash = localLines[0];
        auto remoteLines = runCommandAndGetLines(("git rev-parse origin/" + branch).c_str());
        if (!remoteLines.empty()) remoteHash = remoteLines[0];

        if (!localHash.empty() && !remoteHash.empty() && localHash == remoteHash) {
            ui->mUpdateInstallStatusStr = "Loom is already up to date (" + branch + ").";
            ui->mUpdateInstallProgressPercent = 100;
            ui->mUpdateInstallFinished = true;
            ui->mUpdateInstallActive = false;
            return;
        }

        ui->mUpdateInstallStatusStr = "Pulling updates (" + branch + ")...";
        std::string pullCmd = "git pull --ff-only origin " + branch;
        ret = std::system(pullCmd.c_str());
        if (ret != 0) {
            pullCmd = "git pull origin " + branch;
            ret = std::system(pullCmd.c_str());
            if (ret != 0) {
                ui->mUpdateInstallStatusStr = "Git pull failed for " + branch + ".";
                ui->mUpdateInstallFinished = true;
                ui->mUpdateInstallActive = false;
                return;
            }
        }

        // 4. Locate repo root and build folder
        std::string repoRoot = ".";
        auto rootLines = runCommandAndGetLines("git rev-parse --show-toplevel");
        if (!rootLines.empty() && !rootLines[0].empty()) {
            repoRoot = rootLines[0];
        }
        std::string buildDir = repoRoot + "/build";

        ui->mUpdateInstallProgressPercent = 50;
        ui->mUpdateInstallStatusStr = "Generating build configuration...";
        std::string cmakeConfigCmd = "cmake -S \"" + repoRoot + "\" -B \"" + buildDir + "\" -DCMAKE_BUILD_TYPE=Release";
        ret = std::system(cmakeConfigCmd.c_str());
        if (ret != 0) {
            ui->mUpdateInstallStatusStr = "CMake build generation failed.";
            ui->mUpdateInstallFinished = true;
            ui->mUpdateInstallActive = false;
            return;
        }

        ui->mUpdateInstallProgressPercent = 70;
        ui->mUpdateInstallStatusStr = "Compiling system (takes ~2 mins)...";
        std::string cmakeBuildCmd = "cmake --build \"" + buildDir + "\" --config Release -j4";
        ret = std::system(cmakeBuildCmd.c_str());
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
    std::cout << "Settings: Restart requested. Exiting process gracefully..." << std::endl;
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);
}

// =========================================================================
// --- Bluetooth Pairing Manager ---
// =========================================================================

static bool parseBtLine(const std::string& rawLine, std::string& mac, std::string& name, bool& isNameUpdate, int& rssi) {
    std::string line;
    bool inEscape = false;
    for (char c : rawLine) {
        if (c == '\x1B') inEscape = true;
        else if (inEscape) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) inEscape = false;
        } else {
            line += c;
        }
    }

    isNameUpdate = false;
    rssi = -100;
    
    std::string::size_type rssiPos = line.find("RSSI: ");
    if (rssiPos != std::string::npos) {
        std::string::size_type parenStart = line.find("(", rssiPos);
        std::string::size_type parenEnd = line.find(")", parenStart);
        if (parenStart != std::string::npos && parenEnd != std::string::npos) {
            try {
                rssi = std::stoi(line.substr(parenStart + 1, parenEnd - parenStart - 1));
            } catch (...) {}
        }
    }

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
                    isNameUpdate = true;
                } else if (line.find("[NEW]") != std::string::npos) {
                    isNameUpdate = true;
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
            
            // Clean up MAC-as-name
            std::string hyphenMac = mac;
            std::replace(hyphenMac.begin(), hyphenMac.end(), ':', '-');
            if (name == hyphenMac) {
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
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        std::string s(buffer);
        if (!s.empty() && s.back() == '\n') s.pop_back();
        lines.push_back(s);
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
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
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

        // Run scan inside the interactive shell by holding stdin open for 10 seconds
        std::system("(echo \"scan on\"; sleep 10) | stdbuf -oL bluetoothctl > /tmp/bt_scan.log 2>&1");

        std::vector<BtDevice> foundDevices;
        
        auto pairedLines = runCommandAndGetLines("bluetoothctl devices");
        for (const auto& line : pairedLines) {
            std::string mac, name;
            bool isNameUpdate = false;
            int rssi = -100;
            if (parseBtLine(line, mac, name, isNameUpdate, rssi)) {
                foundDevices.push_back({mac, name, rssi});
            }
        }

        // Also parse scan log for any newly resolved names
        auto scanLines = runCommandAndGetLines("cat /tmp/bt_scan.log");
        for (const auto& line : scanLines) {
            std::string mac, name;
            bool isNameUpdate = false;
            int rssi = -100;
            if (parseBtLine(line, mac, name, isNameUpdate, rssi)) {
                // Check if MAC is already in list
                auto it = std::find_if(foundDevices.begin(), foundDevices.end(), [&](const BtDevice& d) {
                    return d.mac == mac;
                });
                if (it == foundDevices.end()) {
                    foundDevices.push_back({mac, name, rssi});
                } else {
                    if (isNameUpdate && !name.empty() && name.find("-") == std::string::npos && name != mac) {
                        it->name = name;
                    }
                    if (rssi != -100) {
                        it->rssi = rssi;
                    }
                }
            }
        }

        // Sort found devices by RSSI
        std::sort(foundDevices.begin(), foundDevices.end(), [](const BtDevice& a, const BtDevice& b) {
            return a.rssi > b.rssi;
        });

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
        std::cout << "BT: Attempting to pair, trust, and connect: " << mac << std::endl;
        
        std::string script = 
            "timeout 15 stdbuf -oL bluetoothctl <<EOF\n"
            "agent NoInputNoOutput\n"
            "default-agent\n"
            "pair " + mac + "\n"
            "trust " + mac + "\n"
            "connect " + mac + "\n"
            "quit\n"
            "EOF\n";
            
        int retConnect = std::system(script.c_str());

        if (retConnect == 0) {
            mBtStatusStr = "Connected successfully!";
        } else {
            mBtStatusStr = "Connection failed.";
        }
        mBtStatusChanged = true;
    });
    connThread.detach();
}

void UIManager::openWizard(int type) {
    closeWizard(); // safety
    
    mWizardActive = true;
    mWizardType = type;
    mWizardStep = 0;
    mLastWizardActionTimeMs = 0;

    // Full screen dimmed background
    mWizardModal = lv_obj_create(lv_screen_active());
    lv_obj_set_size(mWizardModal, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(mWizardModal, 0, 0);
    lv_obj_set_style_bg_color(mWizardModal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mWizardModal, LV_OPA_80, 0);
    lv_obj_set_style_border_width(mWizardModal, 0, 0);
    lv_obj_add_flag(mWizardModal, LV_OBJ_FLAG_FLOATING);

    // Card container
    lv_obj_t* card = lv_obj_create(mWizardModal);
    lv_obj_set_size(card, 500, 320);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(card, getTrackColor(mActiveTrack), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, type == 0 ? "HARDWARE MAPPING WIZARD" : "MIDI PADS MAPPING WIZARD");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, getTrackColor(mActiveTrack), 0);

    // Step indicator
    mWizardStepLbl = lv_label_create(card);
    lv_obj_set_style_text_font(mWizardStepLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mWizardStepLbl, lv_color_hex(0xFFFFFF), 0);

    // Description/instructions
    mWizardDescLbl = lv_label_create(card);
    lv_obj_set_style_text_font(mWizardDescLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mWizardDescLbl, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_long_mode(mWizardDescLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mWizardDescLbl, 400);
    lv_obj_set_style_text_align(mWizardDescLbl, LV_TEXT_ALIGN_CENTER, 0);

    // Skip and Cancel button row
    lv_obj_t* btnRow = lv_obj_create(card);
    lv_obj_set_size(btnRow, 440, 50);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_remove_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* skipBtn = lv_button_create(btnRow);
    lv_obj_set_size(skipBtn, 120, 36);
    lv_obj_set_style_bg_color(skipBtn, lv_color_hex(0x3A3A3A), 0);
    lv_obj_t* skipLbl = lv_label_create(skipBtn);
    lv_label_set_text(skipLbl, "SKIP STEP");
    lv_obj_set_style_text_font(skipLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(skipLbl);
    
    auto skipClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->advanceWizard(-1); // -1 triggers a skip
    };
    lv_obj_add_event_cb(skipBtn, skipClickCb, LV_EVENT_CLICKED, this);

    lv_obj_t* cancelBtn = lv_button_create(btnRow);
    lv_obj_set_size(cancelBtn, 120, 36);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0xE06C75), 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "CANCEL");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(cancelLbl);
    
    auto cancelClickCb = [](lv_event_t* e) {
        UIManager* ui = (UIManager*)lv_event_get_user_data(e);
        ui->closeWizard();
    };
    lv_obj_add_event_cb(cancelBtn, cancelClickCb, LV_EVENT_CLICKED, this);

    advanceWizard(-2); // Initialize UI labels
}

void UIManager::closeWizard() {
    mWizardActive = false;
    if (mWizardModal) {
        lv_obj_delete(mWizardModal);
        mWizardModal = nullptr;
        mWizardStepLbl = nullptr;
        mWizardDescLbl = nullptr;
    }
    createCenterContentArea(); // Refresh UI
}

void UIManager::advanceWizard(int incomingVal, int incomingChannel) {
    if (!mWizardActive) return;

    int totalSteps = 0;
    if (mWizardType == 0) {
        totalSteps = mSettingsKnobCount + mSettingsSliderCount + 6;
    } else {
        totalSteps = mSettingsPadCount;
    }

    // Process assignment if not initialization or skip
    if (incomingVal >= 0 && mWizardStep >= 0 && mWizardStep < totalSteps) {
        uint32_t now = SDL_GetTicks();
        if (mLastWizardActionTimeMs > 0 && (now - mLastWizardActionTimeMs < 400)) {
            return;
        }
        mLastWizardActionTimeMs = now;

        if (mWizardType == 0) {
            if (mWizardStep < mSettingsKnobCount) {
                // Map knob CC and Channel globally across all tracks
                for (int t = 0; t < 8; ++t) {
                    mSeqMidiKnobCC[t][mWizardStep] = incomingVal;
                    mSeqMidiKnobChannel[t][mWizardStep] = incomingChannel;
                }
                std::cout << "Wizard: Mapped KNOB " << (mWizardStep + 1) << " to CC " << incomingVal << " [CH " << incomingChannel << "]" << std::endl;
            } else if (mWizardStep < mSettingsKnobCount + mSettingsSliderCount) {
                int sIdx = mWizardStep - mSettingsKnobCount;
                for (int t = 0; t < 8; ++t) {
                    mSeqMidiFaderCC[t][sIdx] = incomingVal;
                    mSeqMidiFaderChannel[t][sIdx] = incomingChannel;
                }
                std::cout << "Wizard: Mapped SLIDER " << (sIdx + 1) << " to CC " << incomingVal << " [CH " << incomingChannel << "]" << std::endl;
            } else {
                int transIdx = mWizardStep - (mSettingsKnobCount + mSettingsSliderCount);
                if (transIdx == 0) mCcPlay = incomingVal;
                else if (transIdx == 1) mCcStop = incomingVal;
                else if (transIdx == 2) mCcRecord = incomingVal;
                else if (transIdx == 3) mCcClear = incomingVal;
                else if (transIdx == 4) mCcPrevTrack = incomingVal;
                else if (transIdx == 5) mCcNextTrack = incomingVal;
                std::cout << "Wizard: Mapped Transport/Track button " << transIdx << " to CC " << incomingVal << std::endl;
            }
        } else {
            // Map Pad Note
            mSettingsPadNoteMap[mWizardStep] = incomingVal;
            std::cout << "Wizard: Mapped PAD " << (mWizardStep + 1) << " to Note " << incomingVal << std::endl;
        }
        
        mWizardStep++;
        saveSettings(mSettingsFilePath);
    } else if (incomingVal == -1) {
        // Skip step
        mWizardStep++;
    }

    if (mWizardStep >= totalSteps) {
        closeWizard();
        return;
    }

    // Update UI step labels
    if (mWizardStepLbl && mWizardDescLbl) {
        if (mWizardType == 0) {
            lv_label_set_text_fmt(mWizardStepLbl, "STEP %d / %d", mWizardStep + 1, totalSteps);
            if (mWizardStep < mSettingsKnobCount) {
                lv_label_set_text_fmt(mWizardDescLbl, "Please turn KNOB %d\non your hardware controller.", mWizardStep + 1);
            } else if (mWizardStep < mSettingsKnobCount + mSettingsSliderCount) {
                lv_label_set_text_fmt(mWizardDescLbl, "Please move SLIDER %d\non your hardware controller.", mWizardStep - mSettingsKnobCount + 1);
            } else {
                int transIdx = mWizardStep - (mSettingsKnobCount + mSettingsSliderCount);
                const char* transNames[6] = {"PLAY", "STOP", "RECORD", "CLEAR", "PREVIOUS TRACK", "NEXT TRACK"};
                lv_label_set_text_fmt(mWizardDescLbl, "Please press the %s button\non your hardware controller.", transNames[transIdx]);
            }
        } else {
            lv_label_set_text_fmt(mWizardStepLbl, "PAD %d / %d", mWizardStep + 1, totalSteps);
            lv_label_set_text_fmt(mWizardDescLbl, "Please tap PAD %d\non your pad controller.", mWizardStep + 1);
        }
    }
}

void UIManager::openConsoleModal() {
    if (mConsoleModal) return;
    
    mConsoleModal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(mConsoleModal, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(mConsoleModal);
    lv_obj_set_style_bg_color(mConsoleModal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(mConsoleModal, 0, 0);
    lv_obj_remove_flag(mConsoleModal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* closeBtn = lv_button_create(mConsoleModal);
    lv_obj_set_size(closeBtn, 40, 40);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0xD32F2F), 0);
    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_label_set_text(closeLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(closeBtn, consoleCloseCb, LV_EVENT_CLICKED, this);

    mConsoleOutputTa = lv_textarea_create(mConsoleModal);
    lv_obj_set_size(mConsoleOutputTa, 1000, 250);
    lv_obj_align(mConsoleOutputTa, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(mConsoleOutputTa, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(mConsoleOutputTa, lv_color_hex(0x00FF00), 0); // Green terminal text
    lv_obj_set_style_text_font(mConsoleOutputTa, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(mConsoleOutputTa, lv_color_hex(0x444444), 0);
    lv_textarea_set_text(mConsoleOutputTa, "Loom Pi Integrated Console\nType a command below and press Enter on the keyboard.\n");
    lv_textarea_set_cursor_click_pos(mConsoleOutputTa, false);
    
    mConsoleInputTa = lv_textarea_create(mConsoleModal);
    lv_obj_set_size(mConsoleInputTa, 1000, 50);
    lv_obj_align(mConsoleInputTa, LV_ALIGN_TOP_MID, 0, 270);
    
    // Explicitly style ALL states so LV_STATE_EDITED doesn't make it invisible
    lv_obj_set_style_bg_color(mConsoleInputTa, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(mConsoleInputTa, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(mConsoleInputTa, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(mConsoleInputTa, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(mConsoleInputTa, 2, 0);
    lv_obj_set_style_text_font(mConsoleInputTa, &lv_font_montserrat_12, 0);
    lv_textarea_set_one_line(mConsoleInputTa, true);
    
    // Add default text and visible cursor for debugging
    lv_textarea_set_text(mConsoleInputTa, "> ");
    lv_textarea_set_cursor_click_pos(mConsoleInputTa, true);
    lv_obj_set_style_bg_color(mConsoleInputTa, lv_color_hex(0x00FF00), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(mConsoleInputTa, LV_OPA_COVER, LV_PART_CURSOR);
    
    // Debug print on keystroke
    lv_obj_add_event_cb(mConsoleInputTa, [](lv_event_t* e) {
        lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
        std::cout << "[DEBUG] TextArea updated: " << lv_textarea_get_text(ta) << std::endl;
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    
    // Add the execution callback to the text area directly for hardware keyboard "Enter" support!
    lv_obj_add_event_cb(mConsoleInputTa, consoleExecuteCb, LV_EVENT_READY, this);
    
    mConsoleKb = lv_keyboard_create(mConsoleModal);
    lv_obj_set_size(mConsoleKb, 1024, 220);
    lv_obj_align(mConsoleKb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(mConsoleKb, mConsoleInputTa);
    
    lv_obj_add_event_cb(mConsoleKb, consoleExecuteCb, LV_EVENT_READY, this);
    
    // Route hardware keyboard to the input textarea
    lv_group_t* g = lv_group_create();
    lv_group_add_obj(g, mConsoleInputTa);
    lv_group_focus_obj(mConsoleInputTa); // explicitly focus it for the hardware keyboard
    lv_obj_add_state(mConsoleInputTa, LV_STATE_FOCUSED | LV_STATE_EDITED); // explicitly focus it for the onscreen keyboard
    
    lv_indev_t* kb_indev = lv_indev_get_next(NULL);
    while(kb_indev) {
        if(lv_indev_get_type(kb_indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(kb_indev, g);
        }
        kb_indev = lv_indev_get_next(kb_indev);
    }
    
    // Bring close button to front so it isn't blocked by the textarea
    lv_obj_move_foreground(closeBtn);
}

void UIManager::consoleExecuteCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (!ui->mConsoleInputTa || !ui->mConsoleOutputTa) return;
    
    const char* rawCmd = lv_textarea_get_text(ui->mConsoleInputTa);
    if (!rawCmd || strlen(rawCmd) == 0) return;
    
    std::string commandStr = rawCmd;
    // Strip any trailing newlines (LVGL sometimes inserts them on Enter)
    while (!commandStr.empty() && (commandStr.back() == '\n' || commandStr.back() == '\r')) {
        commandStr.pop_back();
    }
    if (commandStr.empty()) return;
    
    lv_textarea_add_text(ui->mConsoleOutputTa, "\n$ ");
    lv_textarea_add_text(ui->mConsoleOutputTa, commandStr.c_str());
    lv_textarea_add_text(ui->mConsoleOutputTa, "\n");
    
    commandStr += " 2>&1"; // capture stderr
    
    FILE* fp = popen(commandStr.c_str(), "r");
    if (fp) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            lv_textarea_add_text(ui->mConsoleOutputTa, buffer);
        }
        pclose(fp);
    } else {
        lv_textarea_add_text(ui->mConsoleOutputTa, "Error executing command.\n");
    }
    
    lv_textarea_set_text(ui->mConsoleInputTa, "");
}

void UIManager::consoleCloseCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    if (ui->mConsoleModal) {
        lv_group_t* g = lv_obj_get_group(ui->mConsoleInputTa);
        if(g) {
            lv_indev_t* kb_indev = lv_indev_get_next(NULL);
            while(kb_indev) {
                if(lv_indev_get_type(kb_indev) == LV_INDEV_TYPE_KEYPAD && lv_indev_get_group(kb_indev) == g) {
                    lv_indev_set_group(kb_indev, NULL);
                }
                kb_indev = lv_indev_get_next(kb_indev);
            }
            lv_group_del(g);
        }
        
        lv_obj_del(ui->mConsoleModal);
        ui->mConsoleModal = nullptr;
        ui->mConsoleOutputTa = nullptr;
        ui->mConsoleInputTa = nullptr;
        ui->mConsoleKb = nullptr;
    }
}

void UIManager::showConfirmationModal(const char* title, const char* message, const char* confirmBtnText, lv_color_t confirmBtnColor, std::function<void()> onConfirm) {
    lv_obj_t* modalBackdrop = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modalBackdrop, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(modalBackdrop);
    lv_obj_set_style_bg_color(modalBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modalBackdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modalBackdrop, 0, 0);
    lv_obj_remove_flag(modalBackdrop, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dialog = lv_obj_create(modalBackdrop);
    lv_obj_set_size(dialog, 460, 220);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_border_color(dialog, confirmBtnColor, 0);
    lv_obj_set_style_border_width(dialog, 2, 0);
    lv_obj_set_style_radius(dialog, 12, 0);
    lv_obj_set_style_pad_all(dialog, 20, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLbl = lv_label_create(dialog);
    lv_label_set_text(titleLbl, title);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, confirmBtnColor, 0);
    lv_obj_align(titleLbl, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* msgLbl = lv_label_create(dialog);
    lv_label_set_text(msgLbl, message);
    lv_obj_set_style_text_font(msgLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(msgLbl, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_text_align(msgLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msgLbl, 400);
    lv_label_set_long_mode(msgLbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(msgLbl, LV_ALIGN_CENTER, 0, -10);

    // Cancel Button
    lv_obj_t* cancelBtn = lv_button_create(dialog);
    lv_obj_set_size(cancelBtn, 160, 42);
    lv_obj_align(cancelBtn, LV_ALIGN_BOTTOM_LEFT, 20, 0);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_t* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "CANCEL");
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cancelLbl);

    lv_obj_add_event_cb(cancelBtn, [](lv_event_t* e) {
        lv_obj_t* backdrop = (lv_obj_t*)lv_event_get_user_data(e);
        lv_obj_delete(backdrop);
    }, LV_EVENT_CLICKED, modalBackdrop);

    // Confirm Button
    struct ConfirmContext {
        lv_obj_t* backdrop;
        std::function<void()> cb;
    };
    ConfirmContext* ctx = new ConfirmContext{modalBackdrop, onConfirm};

    lv_obj_t* okBtn = lv_button_create(dialog);
    lv_obj_set_size(okBtn, 160, 42);
    lv_obj_align(okBtn, LV_ALIGN_BOTTOM_RIGHT, -20, 0);
    lv_obj_set_style_bg_color(okBtn, confirmBtnColor, 0);
    lv_obj_set_style_radius(okBtn, 8, 0);
    lv_obj_t* okLbl = lv_label_create(okBtn);
    lv_label_set_text(okLbl, confirmBtnText);
    lv_obj_set_style_text_font(okLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(okLbl);

    lv_obj_add_event_cb(okBtn, [](lv_event_t* e) {
        ConfirmContext* c = (ConfirmContext*)lv_event_get_user_data(e);
        if (c->cb) c->cb();
        lv_obj_delete(c->backdrop);
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_add_event_cb(okBtn, [](lv_event_t* e) {
        delete (ConfirmContext*)lv_event_get_user_data(e);
    }, LV_EVENT_DELETE, ctx);
}

// =========================================================================
// --- Play Screen (Expansive Multitouch Pads + X/Y Modulation + Chords) ---
// =========================================================================

void UIManager::populatePlayScreen() {
    lv_color_t trackColor = getTrackColor(mActiveTrack);

    // Root container filling the full 1280x800 center area
    lv_obj_t* playRoot = lv_obj_create(mCenterArea);
    lv_obj_set_size(playRoot, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(playRoot, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(playRoot, 0, 0);
    lv_obj_set_style_pad_all(playRoot, 10, 0);
    lv_obj_remove_flag(playRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(playRoot, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(playRoot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(playRoot, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(playRoot, 8, 0);

    // -------------------------------------------------------------------------
    // 1. Top Performance Control Bar (Root Key, Scale, Chords, Octave, X/Y Mod)
    // -------------------------------------------------------------------------
    lv_obj_t* topBar = lv_obj_create(playRoot);
    lv_obj_set_size(topBar, lv_pct(100), 50);
    lv_obj_set_style_bg_color(topBar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(topBar, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(topBar, 1, 0);
    lv_obj_set_style_radius(topBar, 10, 0);
    lv_obj_set_style_pad_hor(topBar, 8, 0);
    lv_obj_set_style_pad_ver(topBar, 4, 0);
    lv_obj_remove_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(topBar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(topBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topBar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(topBar, 14, 0);

    // Left Group: Root & Scale
    lv_obj_t* scaleGrp = lv_obj_create(topBar);
    lv_obj_set_size(scaleGrp, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_opa(scaleGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scaleGrp, 0, 0);
    lv_obj_set_style_pad_all(scaleGrp, 0, 0);
    lv_obj_set_layout(scaleGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scaleGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scaleGrp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(scaleGrp, 6, 0);

    lv_obj_t* rootLbl = lv_label_create(scaleGrp);
    lv_label_set_text(rootLbl, "ROOT:");
    lv_obj_set_style_text_font(rootLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rootLbl, lv_color_hex(0x888888), 0);

    mPlayRootDd = lv_dropdown_create(scaleGrp);
    lv_dropdown_set_options(mPlayRootDd, "C\nC#\nD\nD#\nE\nF\nF#\nG\nG#\nA\nA#\nB");
    lv_dropdown_set_selected(mPlayRootDd, mPlaySelectedRoot);
    lv_obj_set_size(mPlayRootDd, 65, 34);
    lv_obj_set_style_text_font(mPlayRootDd, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(mPlayRootDd, playRootDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* scaleLbl = lv_label_create(scaleGrp);
    lv_label_set_text(scaleLbl, "SCALE:");
    lv_obj_set_style_text_font(scaleLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(scaleLbl, lv_color_hex(0x888888), 0);

    mPlayScaleDd = lv_dropdown_create(scaleGrp);
    lv_dropdown_set_options(mPlayScaleDd, 
        "Chromatic\nMajor\nNatural Minor\nHarmonic Minor\nMelodic Minor\nDorian\nPhrygian\nLydian\nMixolydian\nLocrian\n"
        "Phrygian Dom\nLydian Dom\nPentatonic Maj\nPentatonic Min\nBlues\nBlues Maj\nWhole Tone\nHirajoshi\nIn-Sen\nYo\nIwato");
    lv_dropdown_set_selected(mPlayScaleDd, mPlaySelectedScaleIdx);
    lv_obj_set_size(mPlayScaleDd, 65, 34);
    lv_obj_set_style_text_font(mPlayScaleDd, &lv_font_montserrat_10, 0);
    lv_obj_add_event_cb(mPlayScaleDd, playScaleDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Center Group: Chord Mode
    lv_obj_t* chordGrp = lv_obj_create(topBar);
    lv_obj_set_size(chordGrp, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_opa(chordGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chordGrp, 0, 0);
    lv_obj_set_style_pad_all(chordGrp, 0, 0);
    lv_obj_set_layout(chordGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chordGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chordGrp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chordGrp, 6, 0);

    lv_obj_t* chordLbl = lv_label_create(chordGrp);
    lv_label_set_text(chordLbl, "VOICE:");
    lv_obj_set_style_text_font(chordLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chordLbl, lv_color_hex(0x888888), 0);

    mPlayChordDd = lv_dropdown_create(chordGrp);
    lv_dropdown_set_options(mPlayChordDd, "Off (Single)\nTriad\n7th\n9th\nSus4");
    lv_dropdown_set_selected(mPlayChordDd, mPlayChordType);
    lv_obj_set_size(mPlayChordDd, 65, 34);
    lv_obj_set_style_text_font(mPlayChordDd, &lv_font_montserrat_10, 0);
    lv_obj_add_event_cb(mPlayChordDd, playChordDdEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Octave - / + Group (for single track modes)
    mPlayTopOctaveGrp = lv_obj_create(topBar);
    lv_obj_set_size(mPlayTopOctaveGrp, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_bg_opa(mPlayTopOctaveGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mPlayTopOctaveGrp, 0, 0);
    lv_obj_set_style_pad_all(mPlayTopOctaveGrp, 0, 0);
    lv_obj_set_layout(mPlayTopOctaveGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mPlayTopOctaveGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mPlayTopOctaveGrp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mPlayTopOctaveGrp, 6, 0);

    lv_obj_t* octDownBtn = lv_button_create(mPlayTopOctaveGrp);
    lv_obj_set_size(octDownBtn, 32, 34);
    lv_obj_set_style_bg_color(octDownBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_user_data(octDownBtn, (void*)(intptr_t)-1);
    lv_obj_add_event_cb(octDownBtn, playOctaveBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* octDownLbl = lv_label_create(octDownBtn);
    lv_label_set_text(octDownLbl, "-");
    lv_obj_center(octDownLbl);

    mPlayOctaveLbl = lv_label_create(mPlayTopOctaveGrp);
    lv_label_set_text_fmt(mPlayOctaveLbl, "OCT %s%d", (mPlayOctaveOffset >= 0 ? "+" : ""), mPlayOctaveOffset);
    lv_obj_set_style_text_font(mPlayOctaveLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mPlayOctaveLbl, trackColor, 0);

    lv_obj_t* octUpBtn = lv_button_create(mPlayTopOctaveGrp);
    lv_obj_set_size(octUpBtn, 32, 34);
    lv_obj_set_style_bg_color(octUpBtn, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_user_data(octUpBtn, (void*)(intptr_t)1);
    lv_obj_add_event_cb(octUpBtn, playOctaveBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* octUpLbl = lv_label_create(octUpBtn);
    lv_label_set_text(octUpLbl, "+");
    lv_obj_center(octUpLbl);

    // Single-track X/Y Touch Modulation Assignment (for single track modes)
    mPlayTopModGrp = lv_obj_create(topBar);
    lv_obj_set_size(mPlayTopModGrp, LV_SIZE_CONTENT, 42);
    lv_obj_set_style_bg_opa(mPlayTopModGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mPlayTopModGrp, 0, 0);
    lv_obj_set_style_pad_all(mPlayTopModGrp, 0, 0);
    lv_obj_set_layout(mPlayTopModGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mPlayTopModGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mPlayTopModGrp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mPlayTopModGrp, 8, 0);

    // X Intensity Knob (Attenuates X modulation depth)
    lv_obj_t* xIntGrp = lv_obj_create(mPlayTopModGrp);
    lv_obj_set_size(xIntGrp, LV_SIZE_CONTENT, 42);
    lv_obj_set_style_bg_opa(xIntGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(xIntGrp, 0, 0);
    lv_obj_set_style_pad_all(xIntGrp, 0, 0);
    lv_obj_set_layout(xIntGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(xIntGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(xIntGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(xIntGrp, 2, 0);

    // Vertical label container for "X" and "INT" stacked
    lv_obj_t* xIntLblGrp = lv_obj_create(xIntGrp);
    lv_obj_set_size(xIntLblGrp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(xIntLblGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(xIntLblGrp, 0, 0);
    lv_obj_set_style_pad_all(xIntLblGrp, 0, 0);
    lv_obj_set_layout(xIntLblGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(xIntLblGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(xIntLblGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(xIntLblGrp, 0, 0);

    lv_obj_t* xIntLbl1 = lv_label_create(xIntLblGrp);
    lv_label_set_text(xIntLbl1, "X");
    lv_obj_set_style_text_font(xIntLbl1, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(xIntLbl1, lv_color_hex(0x00FFFF), 0);

    lv_obj_t* xIntLbl2 = lv_label_create(xIntLblGrp);
    lv_label_set_text(xIntLbl2, "INT");
    lv_obj_set_style_text_font(xIntLbl2, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(xIntLbl2, lv_color_hex(0x00FFFF), 0);

    mPlayModXIntensityArc = lv_arc_create(xIntGrp);
    lv_obj_set_size(mPlayModXIntensityArc, 32, 32);
    lv_arc_set_range(mPlayModXIntensityArc, 0, 100);
    lv_arc_set_value(mPlayModXIntensityArc, (int)(mPlayModXIntensity * 100.0f));
    lv_obj_set_style_arc_color(mPlayModXIntensityArc, lv_color_hex(0x00FFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(mPlayModXIntensityArc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(mPlayModXIntensityArc, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mPlayModXIntensityArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(mPlayModXIntensityArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mPlayModXIntensityArc, 0, LV_PART_KNOB);
    lv_obj_remove_flag(mPlayModXIntensityArc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* xIntValLbl = lv_label_create(mPlayModXIntensityArc);
    lv_label_set_text_fmt(xIntValLbl, "%d", (int)(mPlayModXIntensity * 100.0f));
    lv_obj_set_style_text_font(xIntValLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(xIntValLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(xIntValLbl);
    lv_obj_set_user_data(mPlayModXIntensityArc, xIntValLbl);
    lv_obj_add_event_cb(mPlayModXIntensityArc, playModXIntensityArcEventCb, LV_EVENT_VALUE_CHANGED, this);

    // Y Intensity Knob (Attenuates Y modulation depth)
    lv_obj_t* yIntGrp = lv_obj_create(mPlayTopModGrp);
    lv_obj_set_size(yIntGrp, LV_SIZE_CONTENT, 42);
    lv_obj_set_style_bg_opa(yIntGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(yIntGrp, 0, 0);
    lv_obj_set_style_pad_all(yIntGrp, 0, 0);
    lv_obj_set_layout(yIntGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(yIntGrp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(yIntGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(yIntGrp, 2, 0);

    // Vertical label container for "Y" and "INT" stacked
    lv_obj_t* yIntLblGrp = lv_obj_create(yIntGrp);
    lv_obj_set_size(yIntLblGrp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(yIntLblGrp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(yIntLblGrp, 0, 0);
    lv_obj_set_style_pad_all(yIntLblGrp, 0, 0);
    lv_obj_set_layout(yIntLblGrp, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(yIntLblGrp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(yIntLblGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(yIntLblGrp, 0, 0);

    lv_obj_t* yIntLbl1 = lv_label_create(yIntLblGrp);
    lv_label_set_text(yIntLbl1, "Y");
    lv_obj_set_style_text_font(yIntLbl1, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(yIntLbl1, lv_color_hex(0xFF4081), 0);

    lv_obj_t* yIntLbl2 = lv_label_create(yIntLblGrp);
    lv_label_set_text(yIntLbl2, "INT");
    lv_obj_set_style_text_font(yIntLbl2, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(yIntLbl2, lv_color_hex(0xFF4081), 0);

    mPlayModYIntensityArc = lv_arc_create(yIntGrp);
    lv_obj_set_size(mPlayModYIntensityArc, 32, 32);
    lv_arc_set_range(mPlayModYIntensityArc, 0, 100);
    lv_arc_set_value(mPlayModYIntensityArc, (int)(mPlayModYIntensity * 100.0f));
    lv_obj_set_style_arc_color(mPlayModYIntensityArc, lv_color_hex(0xFF4081), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(mPlayModYIntensityArc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(mPlayModYIntensityArc, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mPlayModYIntensityArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(mPlayModYIntensityArc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(mPlayModYIntensityArc, 0, LV_PART_KNOB);
    lv_obj_remove_flag(mPlayModYIntensityArc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* yIntValLbl = lv_label_create(mPlayModYIntensityArc);
    lv_label_set_text_fmt(yIntValLbl, "%d", (int)(mPlayModYIntensity * 100.0f));
    lv_obj_set_style_text_font(yIntValLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(yIntValLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(yIntValLbl);
    lv_obj_set_user_data(mPlayModYIntensityArc, yIntValLbl);
    lv_obj_add_event_cb(mPlayModYIntensityArc, playModYIntensityArcEventCb, LV_EVENT_VALUE_CHANGED, this);

    if (mPlayModXTrack < 0 || mPlayModXTrack >= 8) mPlayModXTrack = mActiveTrack;
    if (mPlayModYTrack < 0 || mPlayModYTrack >= 8) mPlayModYTrack = mActiveTrack;
    mEngine.setPadModRouting(mActiveTrack, mPlayModXDest, mPlayModXIntensity,
                             mPlayModYDest, mPlayModYIntensity, mPlayVoiceLinkPoly);

    // X MOD button: opens Loom's full Modulation Destination Picker Modal
    mPlayModXDestBtn = lv_button_create(mPlayTopModGrp);
    lv_obj_set_size(mPlayModXDestBtn, 100, 34);
    lv_obj_set_style_bg_color(mPlayModXDestBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(mPlayModXDestBtn, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_border_width(mPlayModXDestBtn, 1, 0);
    lv_obj_set_style_radius(mPlayModXDestBtn, 6, 0);
    mPlayModXDestLbl = lv_label_create(mPlayModXDestBtn);
    std::string xName = getCompactDestName(mPlayModXTrack, mPlayModXDest, &mEngine);
    lv_label_set_text_fmt(mPlayModXDestLbl, "X: %s", xName.c_str());
    lv_obj_set_style_text_font(mPlayModXDestLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mPlayModXDestLbl, lv_color_hex(0x00FFFF), 0);
    lv_obj_center(mPlayModXDestLbl);
    ModDestModalData* xClickData = new ModDestModalData{this, 4, 0, 0};
    lv_obj_add_event_cb(mPlayModXDestBtn, openModDestModalEventCb, LV_EVENT_CLICKED, xClickData);
    auto xFreeCb = [](lv_event_t* e) {
        ModDestModalData* d = (ModDestModalData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(mPlayModXDestBtn, xFreeCb, LV_EVENT_DELETE, xClickData);

    // Y MOD button: opens Loom's full Modulation Destination Picker Modal
    mPlayModYDestBtn = lv_button_create(mPlayTopModGrp);
    lv_obj_set_size(mPlayModYDestBtn, 100, 34);
    lv_obj_set_style_bg_color(mPlayModYDestBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(mPlayModYDestBtn, lv_color_hex(0xFF4081), 0);
    lv_obj_set_style_border_width(mPlayModYDestBtn, 1, 0);
    lv_obj_set_style_radius(mPlayModYDestBtn, 6, 0);
    mPlayModYDestLbl = lv_label_create(mPlayModYDestBtn);
    std::string yName = getCompactDestName(mPlayModYTrack, mPlayModYDest, &mEngine);
    lv_label_set_text_fmt(mPlayModYDestLbl, "Y: %s", yName.c_str());
    lv_obj_set_style_text_font(mPlayModYDestLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mPlayModYDestLbl, lv_color_hex(0xFF4081), 0);
    lv_obj_center(mPlayModYDestLbl);
    ModDestModalData* yClickData = new ModDestModalData{this, 5, 0, 0};
    lv_obj_add_event_cb(mPlayModYDestBtn, openModDestModalEventCb, LV_EVENT_CLICKED, yClickData);
    auto yFreeCb = [](lv_event_t* e) {
        ModDestModalData* d = (ModDestModalData*)lv_event_get_user_data(e);
        delete d;
    };
    lv_obj_add_event_cb(mPlayModYDestBtn, yFreeCb, LV_EVENT_DELETE, yClickData);

    // Voice Link Mode Toggle (POLY per-voice modulation vs GLITCH parameter-fighting)
    mPlayVoiceLinkBtn = lv_button_create(topBar);
    lv_obj_set_size(mPlayVoiceLinkBtn, 84, 34);
    lv_obj_set_style_bg_color(mPlayVoiceLinkBtn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(mPlayVoiceLinkBtn, mPlayVoiceLinkPoly ? lv_color_hex(0x00FFFF) : lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_border_width(mPlayVoiceLinkBtn, 1, 0);
    lv_obj_set_style_radius(mPlayVoiceLinkBtn, 6, 0);
    mPlayVoiceLinkLbl = lv_label_create(mPlayVoiceLinkBtn);
    lv_label_set_text(mPlayVoiceLinkLbl, mPlayVoiceLinkPoly ? "LINK: POLY" : "LINK: GLITCH");
    lv_obj_set_style_text_font(mPlayVoiceLinkLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(mPlayVoiceLinkLbl, mPlayVoiceLinkPoly ? lv_color_hex(0x00FFFF) : lv_color_hex(0xFF9800), 0);
    lv_obj_center(mPlayVoiceLinkLbl);
    lv_obj_add_event_cb(mPlayVoiceLinkBtn, playVoiceLinkBtnEventCb, LV_EVENT_CLICKED, this);

    // Toggle Pad Density (16 Large vs 24 Squares vs 40 Dense vs 20/20 Split)
    mPlayPadCountBtn = lv_button_create(topBar);
    lv_obj_set_size(mPlayPadCountBtn, 56, 34);
    lv_obj_set_style_bg_color(mPlayPadCountBtn, trackColor, 0);
    lv_obj_set_style_radius(mPlayPadCountBtn, 6, 0);
    lv_obj_t* padCountLbl = lv_label_create(mPlayPadCountBtn);
    int activeEngType = mEngine.getTracks()[mActiveTrack].engineType;
    bool isChopTrack = (activeEngType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
    std::string countText;
    if (activeEngType == 5 || activeEngType == 6) {
        countText = "8";
    } else if (isChopTrack) {
        int numSlices = (int)mEngine.getSamplerSlicePoints(mActiveTrack).size();
        numSlices = std::max(1, std::min(16, numSlices));
        countText = std::to_string(numSlices);
    } else {
        if (mPlayPadCount == PLAY_PADS_16) countText = "16";
        else if (mPlayPadCount == PLAY_PADS_24) countText = "24";
        else if (mPlayPadCount == PLAY_PADS_40) countText = "40";
        else countText = "20/20";
    }
    lv_label_set_text(padCountLbl, countText.c_str());
    lv_obj_set_style_text_font(padCountLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(padCountLbl);
    lv_obj_add_event_cb(mPlayPadCountBtn, playPadCountToggleEventCb, LV_EVENT_CLICKED, this);

    if (mPlayPadCount == PLAY_PADS_20_20) {
        lv_obj_add_flag(mPlayTopOctaveGrp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mPlayTopModGrp, LV_OBJ_FLAG_HIDDEN);
    }

    // -------------------------------------------------------------------------
    // 2. Maximized Pad Performance Grid Area (~720px height remaining)
    // -------------------------------------------------------------------------
    mPlayPadGrid = lv_obj_create(playRoot);
    lv_obj_set_size(mPlayPadGrid, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(mPlayPadGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mPlayPadGrid, 0, 0);
    lv_obj_set_style_pad_all(mPlayPadGrid, 4, 0);
    lv_obj_remove_flag(mPlayPadGrid, LV_OBJ_FLAG_SCROLLABLE);

    rebuildPlayPadGrid();
}

void UIManager::rebuildPlayPadGrid() {
    if (!mPlayPadGrid) return;
    lv_obj_clean(mPlayPadGrid);

    // Available center area inside mPlayPadGrid: ~1070px width x ~710px height
    const int availW = 1060;
    const int availH = 700;

    // Build Scale table intervals
    static const int kPlayScaleIntervals[21][12] = {
        {0,1,2,3,4,5,6,7,8,9,10,11},   // Chromatic
        {0,2,4,5,7,9,11,-1,-1,-1,-1,-1}, // Major
        {0,2,3,5,7,8,10,-1,-1,-1,-1,-1}, // Natural Minor
        {0,2,3,5,7,8,11,-1,-1,-1,-1,-1}, // Harmonic Minor
        {0,2,3,5,7,9,11,-1,-1,-1,-1,-1}, // Melodic Minor
        {0,2,3,5,7,9,10,-1,-1,-1,-1,-1}, // Dorian
        {0,1,3,5,7,8,10,-1,-1,-1,-1,-1}, // Phrygian
        {0,2,4,6,7,9,11,-1,-1,-1,-1,-1}, // Lydian
        {0,2,4,5,7,9,10,-1,-1,-1,-1,-1}, // Mixolydian
        {0,1,3,5,6,8,10,-1,-1,-1,-1,-1}, // Locrian
        {0,1,4,5,7,8,10,-1,-1,-1,-1,-1}, // Phrygian Dom
        {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // Lydian Dom
        {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1},// Pentatonic Maj
        {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1},// Pentatonic Min
        {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1},// Blues
        {0,2,3,4,7,9,-1,-1,-1,-1,-1,-1}, // Blues Maj
        {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1},// Whole Tone
        {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1},// Hirajoshi
        {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1},// In-Sen
        {0,2,5,7,9,-1,-1,-1,-1,-1,-1,-1},// Yo
        {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1} // Iwato
    };

    std::vector<int> intervals;
    int scaleIdx = (mPlaySelectedScaleIdx >= 0 && mPlaySelectedScaleIdx < 21) ? mPlaySelectedScaleIdx : 1;
    const int* scaleRow = kPlayScaleIntervals[scaleIdx];
    for (int i = 0; i < 12 && scaleRow[i] >= 0; ++i) intervals.push_back(scaleRow[i]);
    if (intervals.empty()) intervals = {0,2,4,5,7,9,11};

    // -------------------------------------------------------------
    // 20/20 Dual-Track Split Performance Grid
    // -------------------------------------------------------------
    if (mPlayPadCount == PLAY_PADS_20_20) {
        int leftTrack = (mPlaySplitLeftTrack >= 0 && mPlaySplitLeftTrack < 8) ? mPlaySplitLeftTrack : 0;
        int rightTrack = (mPlaySplitRightTrack >= 0 && mPlaySplitRightTrack < 8) ? mPlaySplitRightTrack : 1;
        lv_color_t trackColorLeft = getTrackColor(leftTrack);
        lv_color_t trackColorRight = getTrackColor(rightTrack);

        // Build dynamic track options list
        std::string trkOptions;
        static const char* kEngNames[] = { "Sub", "FM", "Sampler", "Wave", "DrumFM", "DrumAna" };
        for (int t = 0; t < 8; ++t) {
            if (t > 0) trkOptions += "\n";
            trkOptions += "T" + std::to_string(t + 1) + ": ";
            int eng = mEngine.getTracks()[t].engineType;
            if (eng >= 0 && eng < 6) trkOptions += kEngNames[eng];
            else trkOptions += "Synth";
        }

        const int bankW = 502;
        const int dividerGap = 26;
        const int totalW = bankW * 2 + dividerGap; // 1030
        const int startOffsetX = std::max(0, (availW - totalW) / 2); // 15
        const int headerH = 36;
        const int headerGap = 10;
        const int padW = 118;
        const int padH = 114;
        const int gapX = 10;
        const int gapY = 10;
        const int totalPadH = 5 * padH + 4 * gapY; // 610
        const int totalBankH = headerH + headerGap + totalPadH; // 656
        const int startOffsetY = std::max(0, (availH - totalBankH) / 2); // 22

        // Lambda to build a bank's top header banner
        auto createBankHeader = [this, &trkOptions](int startX, int startY, int bW, int hH,
                                                    int bank, int trackIdx, lv_color_t themeColor,
                                                    int octVal, int modXDest, float modXInt, int modYDest, float modYInt,
                                                    lv_obj_t*& outTrackDd, lv_obj_t*& outOctLbl,
                                                    lv_obj_t*& outModXBtn, lv_obj_t*& outModXLbl, lv_obj_t*& outModXArc,
                                                    lv_obj_t*& outModYBtn, lv_obj_t*& outModYLbl, lv_obj_t*& outModYArc) {
            lv_obj_t* header = lv_obj_create(mPlayPadGrid);
            lv_obj_set_pos(header, startX, startY);
            lv_obj_set_size(header, bW, hH);
            lv_obj_set_style_bg_color(header, lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_border_color(header, themeColor, 0);
            lv_obj_set_style_border_width(header, 1, 0);
            lv_obj_set_style_radius(header, 8, 0);
            lv_obj_set_style_pad_hor(header, 6, 0);
            lv_obj_set_style_pad_ver(header, 2, 0);
            lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_layout(header, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            // 1. Track dropdown
            outTrackDd = lv_dropdown_create(header);
            lv_dropdown_set_options(outTrackDd, trkOptions.c_str());
            lv_dropdown_set_selected(outTrackDd, trackIdx);
            lv_obj_set_size(outTrackDd, 110, 28);
            lv_obj_set_style_text_font(outTrackDd, &lv_font_montserrat_10, 0);
            lv_obj_add_event_cb(outTrackDd, (bank == 1) ? playSplitLeftTrackDdEventCb : playSplitRightTrackDdEventCb,
                                LV_EVENT_VALUE_CHANGED, this);

            // 2. Octave group: [-] [OCT 0] [+]
            lv_obj_t* octGrp = lv_obj_create(header);
            lv_obj_set_size(octGrp, LV_SIZE_CONTENT, 30);
            lv_obj_set_style_bg_opa(octGrp, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(octGrp, 0, 0);
            lv_obj_set_style_pad_all(octGrp, 0, 0);
            lv_obj_set_layout(octGrp, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(octGrp, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(octGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(octGrp, 3, 0);

            lv_obj_t* octDownBtn = lv_button_create(octGrp);
            lv_obj_set_size(octDownBtn, 24, 26);
            lv_obj_set_style_bg_color(octDownBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_user_data(octDownBtn, (void*)(intptr_t)-1);
            lv_obj_add_event_cb(octDownBtn, (bank == 1) ? playSplitLeftOctBtnEventCb : playSplitRightOctBtnEventCb,
                                LV_EVENT_CLICKED, this);
            lv_obj_t* downLbl = lv_label_create(octDownBtn);
            lv_label_set_text(downLbl, "-");
            lv_obj_set_style_text_font(downLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(downLbl);

            outOctLbl = lv_label_create(octGrp);
            lv_label_set_text_fmt(outOctLbl, "OCT %s%d", (octVal >= 0 ? "+" : ""), octVal);
            lv_obj_set_style_text_font(outOctLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(outOctLbl, themeColor, 0);

            lv_obj_t* octUpBtn = lv_button_create(octGrp);
            lv_obj_set_size(octUpBtn, 24, 26);
            lv_obj_set_style_bg_color(octUpBtn, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_user_data(octUpBtn, (void*)(intptr_t)1);
            lv_obj_add_event_cb(octUpBtn, (bank == 1) ? playSplitLeftOctBtnEventCb : playSplitRightOctBtnEventCb,
                                LV_EVENT_CLICKED, this);
            lv_obj_t* upLbl = lv_label_create(octUpBtn);
            lv_label_set_text(upLbl, "+");
            lv_obj_set_style_text_font(upLbl, &lv_font_montserrat_10, 0);
            lv_obj_center(upLbl);

            // 3. X Mod group: [X: Dest] + Mini Arc
            lv_obj_t* xGrp = lv_obj_create(header);
            lv_obj_set_size(xGrp, LV_SIZE_CONTENT, 30);
            lv_obj_set_style_bg_opa(xGrp, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(xGrp, 0, 0);
            lv_obj_set_style_pad_all(xGrp, 0, 0);
            lv_obj_set_layout(xGrp, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(xGrp, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(xGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(xGrp, 4, 0);

            outModXBtn = lv_button_create(xGrp);
            lv_obj_set_size(outModXBtn, 76, 26);
            lv_obj_set_style_bg_color(outModXBtn, lv_color_hex(0x222222), 0);
            lv_obj_set_style_border_color(outModXBtn, lv_color_hex(0x00FFFF), 0);
            lv_obj_set_style_border_width(outModXBtn, 1, 0);
            lv_obj_set_style_radius(outModXBtn, 5, 0);
            outModXLbl = lv_label_create(outModXBtn);
            std::string xName = getCompactDestName(trackIdx, modXDest, &mEngine);
            lv_label_set_text_fmt(outModXLbl, "X: %s", xName.c_str());
            lv_obj_set_style_text_font(outModXLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(outModXLbl, lv_color_hex(0x00FFFF), 0);
            lv_obj_center(outModXLbl);
            int xCallerType = (bank == 1) ? 4 : 6;
            ModDestModalData* xData = new ModDestModalData{this, xCallerType, 0, 0};
            lv_obj_add_event_cb(outModXBtn, openModDestModalEventCb, LV_EVENT_CLICKED, xData);
            lv_obj_add_event_cb(outModXBtn, [](lv_event_t* e){ delete (ModDestModalData*)lv_event_get_user_data(e); },
                                LV_EVENT_DELETE, xData);

            outModXArc = lv_arc_create(xGrp);
            lv_obj_set_size(outModXArc, 26, 26);
            lv_arc_set_range(outModXArc, 0, 100);
            lv_arc_set_value(outModXArc, (int)(modXInt * 100.0f));
            lv_obj_set_style_arc_color(outModXArc, lv_color_hex(0x00FFFF), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(outModXArc, 2, LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(outModXArc, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(outModXArc, LV_OPA_TRANSP, LV_PART_KNOB);
            lv_obj_set_style_border_width(outModXArc, 0, LV_PART_KNOB);
            lv_obj_remove_flag(outModXArc, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t* xValLbl = lv_label_create(outModXArc);
            lv_label_set_text_fmt(xValLbl, "%d", (int)(modXInt * 100.0f));
            lv_obj_set_style_text_font(xValLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(xValLbl, lv_color_hex(0xCCCCCC), 0);
            lv_obj_center(xValLbl);
            lv_obj_set_user_data(outModXArc, xValLbl);
            lv_obj_add_event_cb(outModXArc, (bank == 1) ? playSplitLeftModXArcEventCb : playSplitRightModXArcEventCb,
                                LV_EVENT_VALUE_CHANGED, this);

            // 4. Y Mod group: [Y: Dest] + Mini Arc
            lv_obj_t* yGrp = lv_obj_create(header);
            lv_obj_set_size(yGrp, LV_SIZE_CONTENT, 30);
            lv_obj_set_style_bg_opa(yGrp, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(yGrp, 0, 0);
            lv_obj_set_style_pad_all(yGrp, 0, 0);
            lv_obj_set_layout(yGrp, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(yGrp, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(yGrp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(yGrp, 4, 0);

            outModYBtn = lv_button_create(yGrp);
            lv_obj_set_size(outModYBtn, 76, 26);
            lv_obj_set_style_bg_color(outModYBtn, lv_color_hex(0x222222), 0);
            lv_obj_set_style_border_color(outModYBtn, lv_color_hex(0xFF4081), 0);
            lv_obj_set_style_border_width(outModYBtn, 1, 0);
            lv_obj_set_style_radius(outModYBtn, 5, 0);
            outModYLbl = lv_label_create(outModYBtn);
            std::string yName = getCompactDestName(trackIdx, modYDest, &mEngine);
            lv_label_set_text_fmt(outModYLbl, "Y: %s", yName.c_str());
            lv_obj_set_style_text_font(outModYLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(outModYLbl, lv_color_hex(0xFF4081), 0);
            lv_obj_center(outModYLbl);
            int yCallerType = (bank == 1) ? 5 : 7;
            ModDestModalData* yData = new ModDestModalData{this, yCallerType, 0, 0};
            lv_obj_add_event_cb(outModYBtn, openModDestModalEventCb, LV_EVENT_CLICKED, yData);
            lv_obj_add_event_cb(outModYBtn, [](lv_event_t* e){ delete (ModDestModalData*)lv_event_get_user_data(e); },
                                LV_EVENT_DELETE, yData);

            outModYArc = lv_arc_create(yGrp);
            lv_obj_set_size(outModYArc, 26, 26);
            lv_arc_set_range(outModYArc, 0, 100);
            lv_arc_set_value(outModYArc, (int)(modYInt * 100.0f));
            lv_obj_set_style_arc_color(outModYArc, lv_color_hex(0xFF4081), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(outModYArc, 2, LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(outModYArc, 2, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(outModYArc, LV_OPA_TRANSP, LV_PART_KNOB);
            lv_obj_set_style_border_width(outModYArc, 0, LV_PART_KNOB);
            lv_obj_remove_flag(outModYArc, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t* yValLbl = lv_label_create(outModYArc);
            lv_label_set_text_fmt(yValLbl, "%d", (int)(modYInt * 100.0f));
            lv_obj_set_style_text_font(yValLbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(yValLbl, lv_color_hex(0xCCCCCC), 0);
            lv_obj_center(yValLbl);
            lv_obj_set_user_data(outModYArc, yValLbl);
            lv_obj_add_event_cb(outModYArc, (bank == 1) ? playSplitLeftModYArcEventCb : playSplitRightModYArcEventCb,
                                LV_EVENT_VALUE_CHANGED, this);
        };

        // Create Left Bank Header
        createBankHeader(startOffsetX, startOffsetY, bankW, headerH,
                         1, leftTrack, trackColorLeft,
                         mPlaySplitLeftOctave, mPlaySplitLeftModXDest, mPlaySplitLeftModXInt,
                         mPlaySplitLeftModYDest, mPlaySplitLeftModYInt,
                         mPlaySplitLeftTrackDd, mPlaySplitLeftOctLbl,
                         mPlaySplitLeftModXBtn, mPlaySplitLeftModXLbl, mPlaySplitLeftModXArc,
                         mPlaySplitLeftModYBtn, mPlaySplitLeftModYLbl, mPlaySplitLeftModYArc);

        // Center Divider
        lv_obj_t* divider = lv_obj_create(mPlayPadGrid);
        lv_obj_set_pos(divider, startOffsetX + bankW + (dividerGap - 2) / 2, startOffsetY + 2);
        lv_obj_set_size(divider, 2, totalBankH - 4);
        lv_obj_set_style_bg_color(divider, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(divider, 0, 0);

        // Create Right Bank Header
        createBankHeader(startOffsetX + bankW + dividerGap, startOffsetY, bankW, headerH,
                         2, rightTrack, trackColorRight,
                         mPlaySplitRightOctave, mPlaySplitRightModXDest, mPlaySplitRightModXInt,
                         mPlaySplitRightModYDest, mPlaySplitRightModYInt,
                         mPlaySplitRightTrackDd, mPlaySplitRightOctLbl,
                         mPlaySplitRightModXBtn, mPlaySplitRightModXLbl, mPlaySplitRightModXArc,
                         mPlaySplitRightModYBtn, mPlaySplitRightModYLbl, mPlaySplitRightModYArc);

        // Build 20 Pads for each Bank
        int padStartY = startOffsetY + headerH + headerGap;
        for (int b = 1; b <= 2; ++b) {
            int bTrack = (b == 1) ? leftTrack : rightTrack;
            lv_color_t bColor = (b == 1) ? trackColorLeft : trackColorRight;
            int bOct = (b == 1) ? mPlaySplitLeftOctave : mPlaySplitRightOctave;
            int bBaseNote = 48 + mPlaySelectedRoot + bOct * 12;
            int bStartX = (b == 1) ? startOffsetX : (startOffsetX + bankW + dividerGap);
            int bEng = mEngine.getTracks()[bTrack].engineType;
            bool bIsDrum = (bEng == 5 || bEng == 6);
            bool bIsChops = (bEng == 2 && mEngine.getTracks()[bTrack].samplerEngine.isChopMode());

            for (int i = 0; i < 20; ++i) {
                int r = 4 - (i / 4); // 0 to 4 bottom-to-top
                int c = i % 4;
                int x = bStartX + c * (padW + gapX);
                int y = padStartY + r * (padH + gapY);

                int note = 60;
                if (bIsDrum) {
                    note = 60 + (i % 8);
                } else if (bIsChops) {
                    note = 60 + i;
                } else {
                    int octShift = i / (int)intervals.size();
                    int degIdx = i % (int)intervals.size();
                    note = bBaseNote + octShift * 12 + intervals[degIdx];
                    if (note < 0) note = 0;
                    if (note > 127) note = 127;
                }

                bool isRoot = (!bIsDrum && !bIsChops && ((note % 12) == mPlaySelectedRoot));

                lv_obj_t* pad = lv_obj_create(mPlayPadGrid);
                lv_obj_set_size(pad, padW, padH);
                lv_obj_set_pos(pad, x, y);
                lv_obj_set_style_bg_color(pad, isRoot ? bColor : lv_color_hex(0x1F1F1F), 0);
                lv_obj_set_style_bg_opa(pad, isRoot ? LV_OPA_30 : LV_OPA_COVER, 0);
                lv_obj_set_style_border_color(pad, isRoot ? bColor : lv_color_hex(0x333333), 0);
                lv_obj_set_style_border_width(pad, isRoot ? 2 : 1, 0);
                lv_obj_set_style_radius(pad, 10, 0);
                lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);

                // User data encodes bank (1 or 2), track (0..7), note (0..127):
                int packed = (b << 10) | ((bTrack & 0x07) << 7) | (note & 0x7F);
                lv_obj_set_user_data(pad, (void*)(intptr_t)packed);

                // Crosshair lines
                lv_obj_t* xLine = lv_obj_create(pad);
                lv_obj_set_size(xLine, 1, padH - 20);
                lv_obj_center(xLine);
                lv_obj_set_style_bg_color(xLine, lv_color_hex(0x333333), 0);
                lv_obj_set_style_border_width(xLine, 0, 0);
                lv_obj_remove_flag(xLine, LV_OBJ_FLAG_CLICKABLE);

                lv_obj_t* yLine = lv_obj_create(pad);
                lv_obj_set_size(yLine, padW - 20, 1);
                lv_obj_center(yLine);
                lv_obj_set_style_bg_color(yLine, lv_color_hex(0x333333), 0);
                lv_obj_set_style_border_width(yLine, 0, 0);
                lv_obj_remove_flag(yLine, LV_OBJ_FLAG_CLICKABLE);

                // Pad text
                lv_obj_t* noteLbl = lv_label_create(pad);
                if (bIsDrum) {
                    if (i < 8) {
                        const char* dName = (bEng == 5) ? kFmDrumNames[i] : kAnalogDrumNames[i];
                        lv_label_set_text(noteLbl, dName);
                    } else {
                        lv_label_set_text_fmt(noteLbl, "Drm %d", i + 1);
                    }
                    lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(noteLbl, lv_color_hex(0xFFFFFF), 0);
                } else if (bIsChops) {
                    lv_label_set_text_fmt(noteLbl, "SmpSlc %d", i + 1);
                    lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(noteLbl, lv_color_hex(0xFFFFFF), 0);
                } else {
                    int noteName = note % 12;
                    int octave = (note / 12) - 1;
                    lv_label_set_text_fmt(noteLbl, "%s%d", kNoteNames[noteName], octave);
                    lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(noteLbl, isRoot ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xCCCCCC), 0);
                }
                lv_obj_center(noteLbl);

                // Pad index top-left
                lv_obj_t* numLbl = lv_label_create(pad);
                lv_label_set_text_fmt(numLbl, "%d", i + 1);
                lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_10, 0);
                lv_obj_set_style_text_color(numLbl, lv_color_hex(0x666666), 0);
                lv_obj_align(numLbl, LV_ALIGN_TOP_LEFT, 5, 5);

                // Register touch callbacks
                lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSED, this);
                lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSING, this);
                lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_RELEASED, this);
                lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESS_LOST, this);
            }
        }
        return;
    }

    lv_color_t trackColor = getTrackColor(mActiveTrack);

    int engineType = mEngine.getTracks()[mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && mEngine.getTracks()[mActiveTrack].samplerEngine.isChopMode());
    bool isFmDrum = (engineType == 5);
    bool isAnalogDrum = (engineType == 6);

    if (isFmDrum || isAnalogDrum) {
        // 8 drum pads in the middle two rows (rows 1 & 2 of the 16-pad layout)
        int cols = 4;
        int rows = 4;
        int padW = 160;
        int padH = 160;
        int gapX = 24;
        int gapY = 16;
        int totalGridW = cols * padW + (cols - 1) * gapX;
        int totalGridH = rows * padH + (rows - 1) * gapY;
        int startOffsetX = std::max(0, (availW - totalGridW) / 2);
        int startOffsetY = std::max(0, (availH - totalGridH) / 2);

        // Lower row (Row 2): voices 0..3 (Kick, Snare, Clap/Tom, HiHat)
        // Upper row (Row 1): voices 4..7 (HiHat Open, Cymbal, Perc, Noise)
        for (int i = 0; i < 8; ++i) {
            int r = (i < 4) ? 2 : 1;
            int c = (i < 4) ? i : (i - 4);
            int x = startOffsetX + c * (padW + gapX);
            int y = startOffsetY + r * (padH + gapY);
            int note = 60 + i;

            lv_obj_t* pad = lv_obj_create(mPlayPadGrid);
            lv_obj_set_size(pad, padW, padH);
            lv_obj_set_pos(pad, x, y);
            lv_obj_set_style_bg_color(pad, lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(pad, trackColor, 0);
            lv_obj_set_style_border_width(pad, 2, 0);
            lv_obj_set_style_radius(pad, 12, 0);
            lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_set_user_data(pad, (void*)(intptr_t)note);

            // Subdued crosshair line representing X/Y center
            lv_obj_t* xLine = lv_obj_create(pad);
            lv_obj_set_size(xLine, 1, padH - 24);
            lv_obj_center(xLine);
            lv_obj_set_style_bg_color(xLine, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(xLine, 0, 0);
            lv_obj_remove_flag(xLine, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* yLine = lv_obj_create(pad);
            lv_obj_set_size(yLine, padW - 24, 1);
            lv_obj_center(yLine);
            lv_obj_set_style_bg_color(yLine, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(yLine, 0, 0);
            lv_obj_remove_flag(yLine, LV_OBJ_FLAG_CLICKABLE);

            // Drum instrument name in center
            const char* drumName = isFmDrum ? kFmDrumNames[i] : kAnalogDrumNames[i];
            lv_obj_t* noteLbl = lv_label_create(pad);
            lv_label_set_text(noteLbl, drumName);
            lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(noteLbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(noteLbl);

            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSING, this);
            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_RELEASED, this);
            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESS_LOST, this);
        }
        return;
    }

    if (isSamplerChops) {
        int numSlices = (int)mEngine.getSamplerSlicePoints(mActiveTrack).size();
        numSlices = std::max(1, std::min(16, numSlices));

        int cols = 4;
        int rows = 4;
        int padW = 160;
        int padH = 160;
        int gapX = 24;
        int gapY = 16;
        int totalGridW = cols * padW + (cols - 1) * gapX;
        int totalGridH = rows * padH + (rows - 1) * gapY;
        int startOffsetX = std::max(0, (availW - totalGridW) / 2);
        int startOffsetY = std::max(0, (availH - totalGridH) / 2);

        // Fill middle two rows first (Row 2, then Row 1), then bottom (Row 3), then top (Row 0)
        static const int kSliceRows[16] = { 2, 2, 2, 2, 1, 1, 1, 1, 3, 3, 3, 3, 0, 0, 0, 0 };
        static const int kSliceCols[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };

        for (int i = 0; i < numSlices; ++i) {
            int r = kSliceRows[i];
            int c = kSliceCols[i];
            int x = startOffsetX + c * (padW + gapX);
            int y = startOffsetY + r * (padH + gapY);
            int note = 60 + i;

            lv_obj_t* pad = lv_obj_create(mPlayPadGrid);
            lv_obj_set_size(pad, padW, padH);
            lv_obj_set_pos(pad, x, y);
            lv_obj_set_style_bg_color(pad, lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(pad, trackColor, 0);
            lv_obj_set_style_border_width(pad, 2, 0);
            lv_obj_set_style_radius(pad, 12, 0);
            lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_set_user_data(pad, (void*)(intptr_t)note);

            // Subdued crosshair line representing X/Y center
            lv_obj_t* xLine = lv_obj_create(pad);
            lv_obj_set_size(xLine, 1, padH - 24);
            lv_obj_center(xLine);
            lv_obj_set_style_bg_color(xLine, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(xLine, 0, 0);
            lv_obj_remove_flag(xLine, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* yLine = lv_obj_create(pad);
            lv_obj_set_size(yLine, padW - 24, 1);
            lv_obj_center(yLine);
            lv_obj_set_style_bg_color(yLine, lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(yLine, 0, 0);
            lv_obj_remove_flag(yLine, LV_OBJ_FLAG_CLICKABLE);

            // Slice name in center
            lv_obj_t* noteLbl = lv_label_create(pad);
            lv_label_set_text_fmt(noteLbl, "SmpSlc %d", i + 1);
            lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(noteLbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(noteLbl);

            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSING, this);
            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_RELEASED, this);
            lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESS_LOST, this);
        }
        return;
    }
    
    // Determine grid columns and rows based on density mode
    int cols = 4;
    int rows = 4;
    if (mPlayPadCount == 24) {
        cols = 6;
        rows = 4;
    } else if (mPlayPadCount == 40) {
        cols = 8;
        rows = 5;
    }
    int totalPads = cols * rows;

    int gapX = 10;
    int gapY = 10;
    int padSize = 0;

    if (mPlayPadCount == 16) {
        // 4x4 layout: square pads with generous separation
        padSize = 160;
        gapX = 24;
        gapY = 16;
    } else if (mPlayPadCount == 24) {
        // 6x4 layout: square pads
        padSize = 155;
        gapX = 14;
        gapY = 16;
    } else { // 40 pads (8x5)
        // Square pads with comfortable spacing across 5 rows
        padSize = 120;
        gapX = 10;
        gapY = 12;
    }

    int padW = padSize;
    int padH = padSize;

    // Center grid in available area to prevent edge/right border cutoffs
    int totalGridW = cols * padW + (cols - 1) * gapX;
    int totalGridH = rows * padH + (rows - 1) * gapY;
    int startOffsetX = std::max(0, (availW - totalGridW) / 2);
    int startOffsetY = std::max(0, (availH - totalGridH) / 2);



    int baseNote = 48 + mPlaySelectedRoot + mPlayOctaveOffset * 12;

    for (int i = 0; i < totalPads; ++i) {
        int r = rows - 1 - (i / cols); // Bottom to top like standard MPC/launchpad layout
        int c = i % cols;
        int x = startOffsetX + c * (padW + gapX);
        int y = startOffsetY + r * (padH + gapY);

        int octShift = i / (int)intervals.size();
        int degIdx = i % (int)intervals.size();
        int note = baseNote + octShift * 12 + intervals[degIdx];
        if (note < 0) note = 0;
        if (note > 127) note = 127;

        bool isRoot = ((note % 12) == mPlaySelectedRoot);

        lv_obj_t* pad = lv_obj_create(mPlayPadGrid);
        lv_obj_set_size(pad, padW, padH);
        lv_obj_set_pos(pad, x, y);
        lv_obj_set_style_bg_color(pad, isRoot ? trackColor : lv_color_hex(0x1F1F1F), 0);
        lv_obj_set_style_bg_opa(pad, isRoot ? LV_OPA_30 : LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(pad, isRoot ? trackColor : lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(pad, isRoot ? 2 : 1, 0);
        lv_obj_set_style_radius(pad, 12, 0);
        lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);

        // Store note value in user_data
        lv_obj_set_user_data(pad, (void*)(intptr_t)note);

        // Subdued crosshair line representing X/Y center
        lv_obj_t* xLine = lv_obj_create(pad);
        lv_obj_set_size(xLine, 1, padH - 24);
        lv_obj_center(xLine);
        lv_obj_set_style_bg_color(xLine, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(xLine, 0, 0);
        lv_obj_remove_flag(xLine, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* yLine = lv_obj_create(pad);
        lv_obj_set_size(yLine, padW - 24, 1);
        lv_obj_center(yLine);
        lv_obj_set_style_bg_color(yLine, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(yLine, 0, 0);
        lv_obj_remove_flag(yLine, LV_OBJ_FLAG_CLICKABLE);

        // Note name label in center
        int noteName = note % 12;
        int octave = (note / 12) - 1;
        lv_obj_t* noteLbl = lv_label_create(pad);
        lv_label_set_text_fmt(noteLbl, "%s%d", kNoteNames[noteName], octave);
        lv_obj_set_style_text_font(noteLbl, (mPlayPadCount == 16) ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(noteLbl, isRoot ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xCCCCCC), 0);
        lv_obj_center(noteLbl);

        // Pad index top-left
        lv_obj_t* numLbl = lv_label_create(pad);
        lv_label_set_text_fmt(numLbl, "%d", i + 1);
        lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(numLbl, lv_color_hex(0x666666), 0);
        lv_obj_align(numLbl, LV_ALIGN_TOP_LEFT, 6, 6);

        // Register press, drag/motion, and release callbacks for X/Y modulation
        lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESSING, this);
        lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(pad, playPadTouchEventCb, LV_EVENT_PRESS_LOST, this);
    }
}

void UIManager::playPadTouchEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* pad = (lv_obj_t*)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int packed = (int)(intptr_t)lv_obj_get_user_data(pad);
    int note = packed & 0x7F;
    int padTrack = (packed >> 7) & 0x07;
    int bank = (packed >> 10) & 0x03;
    if (bank == 0) padTrack = ui->mActiveTrack;

    int engType = ui->mEngine.getTracks()[padTrack].engineType;
    bool isSamplerChops = (engType == 2 && ui->mEngine.getTracks()[padTrack].samplerEngine.isChopMode());
    bool isDrum = (engType == 5 || engType == 6 || isSamplerChops);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(pad, ui->getTrackColor(padTrack), 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_80, 0);
        lv_obj_set_style_border_color(pad, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(pad, 3, 0);

        // Trigger note or chord
        if (isDrum || ui->mPlayChordType == 0) {
            ui->mEngine.triggerNote(padTrack, note, 110, note);
        } else if (ui->mPlayChordType == 1) { // Triad
            ui->mEngine.triggerNote(padTrack, note, 105, note);
            ui->mEngine.triggerNote(padTrack, note + 4, 100, note);
            ui->mEngine.triggerNote(padTrack, note + 7, 100, note);
        } else if (ui->mPlayChordType == 2) { // 7th
            ui->mEngine.triggerNote(padTrack, note, 105, note);
            ui->mEngine.triggerNote(padTrack, note + 4, 100, note);
            ui->mEngine.triggerNote(padTrack, note + 7, 100, note);
            ui->mEngine.triggerNote(padTrack, note + 10, 95, note);
        } else if (ui->mPlayChordType == 3) { // 9th
            ui->mEngine.triggerNote(padTrack, note, 105, note);
            ui->mEngine.triggerNote(padTrack, note + 4, 100, note);
            ui->mEngine.triggerNote(padTrack, note + 7, 100, note);
            ui->mEngine.triggerNote(padTrack, note + 10, 95, note);
            ui->mEngine.triggerNote(padTrack, note + 14, 90, note);
        } else if (ui->mPlayChordType == 4) { // Sus4
            ui->mEngine.triggerNote(padTrack, note, 105, note);
            ui->mEngine.triggerNote(padTrack, note + 5, 100, note);
            ui->mEngine.triggerNote(padTrack, note + 7, 100, note);
        }
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        // Calculate finger touch position inside the pad (0.0 to 1.0 for X and Y)
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t pt;
            lv_indev_get_point(indev, &pt);
            lv_area_t coords;
            lv_obj_get_coords(pad, &coords);

            float normX = (float)(pt.x - coords.x1) / (float)(coords.x2 - coords.x1);
            float normY = 1.0f - ((float)(pt.y - coords.y1) / (float)(coords.y2 - coords.y1)); // Up = higher
            normX = std::max(0.0f, std::min(1.0f, normX));
            normY = std::max(0.0f, std::min(1.0f, normY));

            int modXTrack = ui->mPlayModXTrack;
            int modXDest  = ui->mPlayModXDest;
            float modXInt = ui->mPlayModXIntensity;
            int modYTrack = ui->mPlayModYTrack;
            int modYDest  = ui->mPlayModYDest;
            float modYInt = ui->mPlayModYIntensity;

            if (bank == 1) { // Split Left
                modXTrack = ui->mPlaySplitLeftModXTrack;
                modXDest  = ui->mPlaySplitLeftModXDest;
                modXInt   = ui->mPlaySplitLeftModXInt;
                modYTrack = ui->mPlaySplitLeftModYTrack;
                modYDest  = ui->mPlaySplitLeftModYDest;
                modYInt   = ui->mPlaySplitLeftModYInt;
            } else if (bank == 2) { // Split Right
                modXTrack = ui->mPlaySplitRightModXTrack;
                modXDest  = ui->mPlaySplitRightModXDest;
                modXInt   = ui->mPlaySplitRightModXInt;
                modYTrack = ui->mPlaySplitRightModYTrack;
                modYDest  = ui->mPlaySplitRightModYDest;
                modYInt   = ui->mPlaySplitRightModYInt;
            }

            bool isSynthTrack = (engType != 5 && engType != 6);
            bool isChopScrub = (engType == 2 && ui->mEngine.getTracks()[padTrack].samplerEngine.getPlayMode() == SamplerEngine::SliceScrub);
            if ((ui->mPlayVoiceLinkPoly || isChopScrub) && isSynthTrack) {
                // Per-voice polyphonic modulation!
                ui->mEngine.setVoicePadMod(padTrack, note, normX, normY);

                auto isVoiceParam = [](int eng, int pid) -> bool {
                    if (pid == 1 || pid == 2) return true; // Common Cutoff / Resonance
                    if (eng == 0) { // Subtractive
                        if (pid == 112 || pid == 113) return true;
                        if (pid == 290 || pid == 291 || pid == 292) return true; // Morphx3, Foldx3, Drivex3
                        if (pid == 104 || pid == 105 || pid == 155 || pid == 4) return true; // Morphs
                        if (pid >= 170 && pid <= 172) return true; // Drives
                        if (pid >= 180 && pid <= 182) return true; // Folds
                        return false;
                    } else if (eng == 1) { // FM
                        if (pid == 151 || pid == 152) return true;
                        return false;
                    } else if (eng == 2) { // Sampler
                        if (pid == 303 || pid == 304 || pid == 360 || pid == 330) return true;
                        return false;
                    } else if (eng == 4) { // Wavetable
                        if (pid == 458 || pid == 459 || pid == 450 || pid == 300 || pid == 310) return true;
                        return false;
                    }
                    return false;
                };

                // Route to target track / parameter
                if (modXDest >= 0) {
                    int targetXTrack = (modXTrack >= 0 && modXTrack < 8) ? modXTrack : padTrack;
                    int targetXEng = ui->mEngine.getTracks()[targetXTrack].engineType;
                    if (targetXTrack != padTrack || !isVoiceParam(targetXEng, modXDest)) {
                        ui->mEngine.setParameter(targetXTrack, modXDest, normX * modXInt);
                    }
                }
                if (modYDest >= 0) {
                    int targetYTrack = (modYTrack >= 0 && modYTrack < 8) ? modYTrack : padTrack;
                    int targetYEng = ui->mEngine.getTracks()[targetYTrack].engineType;
                    if (targetYTrack != padTrack || !isVoiceParam(targetYEng, modYDest)) {
                        ui->mEngine.setParameter(targetYTrack, modYDest, normY * modYInt);
                    }
                }
            } else {
                // Parameter-fighting Glitch mode: Modulate assigned parameters directly on the track
                int targetXTrack = (modXTrack >= 0 && modXTrack < 8) ? modXTrack : padTrack;
                int targetYTrack = (modYTrack >= 0 && modYTrack < 8) ? modYTrack : padTrack;
                if (modXDest >= 0) {
                    float effectiveX = normX * modXInt;
                    ui->mEngine.setParameter(targetXTrack, modXDest, effectiveX);
                }
                if (modYDest >= 0) {
                    float effectiveY = normY * modYInt;
                    ui->mEngine.setParameter(targetYTrack, modYDest, effectiveY);
                }
            }
        }
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_color_t padColor = ui->getTrackColor(padTrack);
        if (isDrum) {
            lv_obj_set_style_bg_color(pad, lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(pad, padColor, 0);
            lv_obj_set_style_border_width(pad, 2, 0);
            ui->mEngine.releaseNote(padTrack, note);
        } else {
            bool isRoot = ((note % 12) == ui->mPlaySelectedRoot);
            lv_obj_set_style_bg_color(pad, isRoot ? padColor : lv_color_hex(0x1F1F1F), 0);
            lv_obj_set_style_bg_opa(pad, isRoot ? LV_OPA_30 : LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(pad, isRoot ? padColor : lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(pad, isRoot ? 2 : 1, 0);

            // Note off
            ui->mEngine.releaseNote(padTrack, note);
            if (ui->mPlayChordType > 0) {
                ui->mEngine.releaseNote(padTrack, note + 4);
                ui->mEngine.releaseNote(padTrack, note + 5);
                ui->mEngine.releaseNote(padTrack, note + 7);
                ui->mEngine.releaseNote(padTrack, note + 10);
                ui->mEngine.releaseNote(padTrack, note + 14);
            }
        }
    }
}

void UIManager::playRootDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mPlaySelectedRoot = lv_dropdown_get_selected(ui->mPlayRootDd);
    ui->rebuildPlayPadGrid();
}

void UIManager::playScaleDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mPlaySelectedScaleIdx = lv_dropdown_get_selected(ui->mPlayScaleDd);
    ui->rebuildPlayPadGrid();
}

void UIManager::playChordDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mPlayChordType = lv_dropdown_get_selected(ui->mPlayChordDd);
}

void UIManager::playOctaveBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int dir = (int)(intptr_t)lv_obj_get_user_data(btn);
    ui->mPlayOctaveOffset = std::max(-3, std::min(3, ui->mPlayOctaveOffset + dir));
    if (ui->mPlayOctaveLbl) {
        lv_label_set_text_fmt(ui->mPlayOctaveLbl, "OCT %s%d", (ui->mPlayOctaveOffset >= 0 ? "+" : ""), ui->mPlayOctaveOffset);
    }
    ui->rebuildPlayPadGrid();
}

void UIManager::playPadCountToggleEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    int engineType = ui->mEngine.getTracks()[ui->mActiveTrack].engineType;
    bool isSamplerChops = (engineType == 2 && ui->mEngine.getTracks()[ui->mActiveTrack].samplerEngine.isChopMode());
    if (ui->mPlayPadCount != PLAY_PADS_20_20 && (engineType == 5 || engineType == 6 || isSamplerChops)) {
        return; // Fixed pad count for drum / chop tracks
    }
    // Cycle 16 -> 24 -> 40 -> 20/20 -> 16
    if (ui->mPlayPadCount == PLAY_PADS_16) {
        ui->mPlayPadCount = PLAY_PADS_24;
    } else if (ui->mPlayPadCount == PLAY_PADS_24) {
        ui->mPlayPadCount = PLAY_PADS_40;
    } else if (ui->mPlayPadCount == PLAY_PADS_40) {
        ui->mPlayPadCount = PLAY_PADS_20_20;
    } else {
        ui->mPlayPadCount = PLAY_PADS_16;
    }

    if (ui->mPlayTopOctaveGrp && ui->mPlayTopModGrp) {
        if (ui->mPlayPadCount == PLAY_PADS_20_20) {
            lv_obj_add_flag(ui->mPlayTopOctaveGrp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui->mPlayTopModGrp, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(ui->mPlayTopOctaveGrp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ui->mPlayTopModGrp, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (ui->mPlayPadCountBtn) {
        lv_obj_t* lbl = lv_obj_get_child(ui->mPlayPadCountBtn, 0);
        if (lbl) {
            const char* txt = (ui->mPlayPadCount == PLAY_PADS_16) ? "16" : 
                             ((ui->mPlayPadCount == PLAY_PADS_24) ? "24" : 
                             ((ui->mPlayPadCount == PLAY_PADS_40) ? "40" : "20/20"));
            lv_label_set_text(lbl, txt);
        }
    }
    ui->rebuildPlayPadGrid();
}

void UIManager::playSplitLeftTrackDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    ui->mPlaySplitLeftTrack = lv_dropdown_get_selected(dd);
    if (ui->mPlaySplitLeftModXTrack < 0 || ui->mPlaySplitLeftModXTrack >= 8) ui->mPlaySplitLeftModXTrack = ui->mPlaySplitLeftTrack;
    if (ui->mPlaySplitLeftModYTrack < 0 || ui->mPlaySplitLeftModYTrack >= 8) ui->mPlaySplitLeftModYTrack = ui->mPlaySplitLeftTrack;
    ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                 ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
    ui->rebuildPlayPadGrid();
}

void UIManager::playSplitRightTrackDdEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
    ui->mPlaySplitRightTrack = lv_dropdown_get_selected(dd);
    if (ui->mPlaySplitRightModXTrack < 0 || ui->mPlaySplitRightModXTrack >= 8) ui->mPlaySplitRightModXTrack = ui->mPlaySplitRightTrack;
    if (ui->mPlaySplitRightModYTrack < 0 || ui->mPlaySplitRightModYTrack >= 8) ui->mPlaySplitRightModYTrack = ui->mPlaySplitRightTrack;
    ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                 ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
    ui->rebuildPlayPadGrid();
}

void UIManager::playSplitLeftOctBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int delta = (int)(intptr_t)lv_obj_get_user_data(btn);
    ui->mPlaySplitLeftOctave += delta;
    if (ui->mPlaySplitLeftOctave < -3) ui->mPlaySplitLeftOctave = -3;
    if (ui->mPlaySplitLeftOctave > 3) ui->mPlaySplitLeftOctave = 3;
    ui->rebuildPlayPadGrid();
}

void UIManager::playSplitRightOctBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int delta = (int)(intptr_t)lv_obj_get_user_data(btn);
    ui->mPlaySplitRightOctave += delta;
    if (ui->mPlaySplitRightOctave < -3) ui->mPlaySplitRightOctave = -3;
    if (ui->mPlaySplitRightOctave > 3) ui->mPlaySplitRightOctave = 3;
    ui->rebuildPlayPadGrid();
}

void UIManager::playSplitLeftModXArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* valLbl = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    ui->mPlaySplitLeftModXInt = (float)val / 100.0f;
    if (valLbl) lv_label_set_text_fmt(valLbl, "%d", (int)val);
    ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                 ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
}

void UIManager::playSplitLeftModYArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* valLbl = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    ui->mPlaySplitLeftModYInt = (float)val / 100.0f;
    if (valLbl) lv_label_set_text_fmt(valLbl, "%d", (int)val);
    ui->mEngine.setPadModRouting(ui->mPlaySplitLeftTrack, ui->mPlaySplitLeftModXDest, ui->mPlaySplitLeftModXInt,
                                 ui->mPlaySplitLeftModYDest, ui->mPlaySplitLeftModYInt, ui->mPlayVoiceLinkPoly);
}

void UIManager::playSplitRightModXArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* valLbl = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    ui->mPlaySplitRightModXInt = (float)val / 100.0f;
    if (valLbl) lv_label_set_text_fmt(valLbl, "%d", (int)val);
    ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                 ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
}

void UIManager::playSplitRightModYArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* valLbl = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    ui->mPlaySplitRightModYInt = (float)val / 100.0f;
    if (valLbl) lv_label_set_text_fmt(valLbl, "%d", (int)val);
    ui->mEngine.setPadModRouting(ui->mPlaySplitRightTrack, ui->mPlaySplitRightModXDest, ui->mPlaySplitRightModXInt,
                                 ui->mPlaySplitRightModYDest, ui->mPlaySplitRightModYInt, ui->mPlayVoiceLinkPoly);
}


void UIManager::playModXDestDdEventCb(lv_event_t* e) {
    (void)e;
}

void UIManager::playModYDestDdEventCb(lv_event_t* e) {
    (void)e;
}

void UIManager::playModDestBtnEventCb(lv_event_t* e) {
    (void)e;
}

void UIManager::playModXIntensityArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* valLbl = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    ui->mPlayModXIntensity = (float)val / 100.0f;
    if (valLbl) {
        lv_label_set_text_fmt(valLbl, "%" PRId32, val);
    }
    ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                 ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
}

void UIManager::playModYIntensityArcEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* arc = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* valLbl = (lv_obj_t*)lv_obj_get_user_data(arc);
    int32_t val = lv_arc_get_value(arc);
    ui->mPlayModYIntensity = (float)val / 100.0f;
    if (valLbl) {
        lv_label_set_text_fmt(valLbl, "%" PRId32, val);
    }
    ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                 ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
}

void UIManager::playVoiceLinkBtnEventCb(lv_event_t* e) {
    UIManager* ui = (UIManager*)lv_event_get_user_data(e);
    ui->mPlayVoiceLinkPoly = !ui->mPlayVoiceLinkPoly;
    lv_color_t color = ui->mPlayVoiceLinkPoly ? lv_color_hex(0x00FFFF) : lv_color_hex(0xFF9800);
    lv_obj_set_style_border_color(ui->mPlayVoiceLinkBtn, color, 0);
    lv_obj_set_style_text_color(ui->mPlayVoiceLinkLbl, color, 0);
    lv_label_set_text(ui->mPlayVoiceLinkLbl, ui->mPlayVoiceLinkPoly ? "LINK: POLY" : "LINK: GLITCH");
    ui->mEngine.setPadModRouting(ui->mActiveTrack, ui->mPlayModXDest, ui->mPlayModXIntensity,
                                 ui->mPlayModYDest, ui->mPlayModYIntensity, ui->mPlayVoiceLinkPoly);
}


