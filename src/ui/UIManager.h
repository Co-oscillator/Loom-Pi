#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "lvgl.h"
#include "../AudioEngine.h"
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

class UIManager {
public:
    UIManager(AudioEngine& engine);
    ~UIManager();

    // Initialize the main shell layout
    void init();

    // Update/tick method if needed
    void update();

    // Public accessors for main loop keyboard handling
    int getActiveTrack() const { return mActiveTrack; }
    void setActiveTrack(int track) { mActiveTrack = track; }
    bool isKeyboardModeEnabled() const { return mSettingsKeyboardMode; }
    bool isFileBrowserOpen() const { return mSeqModal != nullptr; }

    // Dynamic MIDI and screen controls mappings (publicly accessible by MIDI thread)
    int mSeqMidiKnobCC[8][24];
    int mSeqMidiKnobParam[8][24]; // mapped param IDs
    float mSeqMidiKnobValue[8][24];
    bool mSeqMidiKnobInverted[8][24];
    uint32_t mLastKnobMs[8][24] = {0};
    uint8_t mLastKnobCcVal[8][24] = {0};
    bool mKnobInitialized[8][24] = {false};
    int mSeqMidiFaderCC[8][24];
    int mSeqMidiFaderParam[8][24]; // mapped param IDs
    float mSeqMidiFaderValue[8][24];
    bool mSeqMidiFaderInverted[8][24];
    int mSettingsKnobCount = 12;
    int mSettingsSliderCount = 4;

    // Transport CC mapping configuration
    int mCcPlay = 59;
    int mCcStop = 59;
    int mCcRecord = 60;
    int mCcClear = 61;
    int mCcPrevTrack = 62;
    int mCcNextTrack = 63;

    // Real-time synchronization widget tracking structures
    struct ParamWidgetTracking {
        int paramId;
        lv_obj_t* widget;
        lv_obj_t* valLbl;
        float minVal;
        float maxVal;
        int decimals;
        bool isPercent;
        std::string labelText;
    };
    struct FxWidgetTracking {
        int paramId;
        lv_obj_t* widget;
        lv_obj_t* valLbl;
        float minVal;
        float maxVal;
        int decimals;
        bool isPercent;
    };

    std::vector<ParamWidgetTracking> mActiveParamWidgets;
    std::vector<FxWidgetTracking> mActiveFxWidgets;

    // MIDI Learn state
    bool mMidiLearnActive = false;
    int mMidiLearnTargetParamId = -1;
    int mMidiLearnTargetTrack = 0;
    lv_obj_t* mMidiLearnBtnLabel = nullptr;
    bool mNeedsScreenRebuild = false;

    // Controller Setup state
    bool mControllerSetupActive = false;
    int mControllerSetupTargetIndex = -1;
    lv_obj_t* mControllerSetupBtnLabel = nullptr;

    // Track aftertouch destinations
    int mAftertouchDestParamId[8];
    lv_obj_t* mAftertouchDestBtnLabel[8];

    // Helper functions for MIDI Learn, Aftertouch, and Default Mappings
    void createMidiLearnButton();
    static void paramMidiLearnClickEventCb(lv_event_t* e);
    void applyDefaultMidiMappings(int trackIdx, int engineType);

private:
    AudioEngine& mEngine;

    // Main layout containers
    lv_obj_t* mMainScreen;
    lv_obj_t* mLeftBar;
    lv_obj_t* mCenterArea;
    lv_obj_t* mRightBar;

    // Internal creation methods
    void createLeftMixerBar();
    void createRightNavBar();
    void createCenterContentArea();

    // UI updating
    void updateHighlighting();
    bool isTrackPlaying(int trackIdx);
    void populateArpScreen();

    // Event callbacks
    static void trackBtnEventCb(lv_event_t* e);
    static void navBtnEventCb(lv_event_t* e);

    // Mixer popup menu methods
    void openMixerPopup(int trackIdx);
    static void closeMixerPopupEventCb(lv_event_t* e);
    static void mixerVolumeSliderEventCb(lv_event_t* e);
    static void mixerPanSliderEventCb(lv_event_t* e);
    static void mixerEngineDdEventCb(lv_event_t* e);
    static void mixerMuteBtnEventCb(lv_event_t* e);
    static void mixerSoloBtnEventCb(lv_event_t* e);
    static void mixerActiveBtnEventCb(lv_event_t* e);

    // Color definitions based on standard Loom track colors
    lv_color_t getTrackColor(int trackIndex);
    
    lv_obj_t* mTrackButtons[8];
    bool mTrackEnabled[8];
    bool mLongPressedTrack = false;
    lv_obj_t* mNavButtons[7];
    
    int mActiveTrack = 0;
    int mActiveNav = 4; // Default navigation page
    
    // Arpeggiator pattern grid
    lv_obj_t* mArpButtons[4][16];
    static void arpButtonEventCb(lv_event_t* e);
    void randomizeRhythm();
    void randomizeNotes();

    // Event callbacks for Arp Settings
    static void latchBtnEventCb(lv_event_t* e);
    static void strumArcEventCb(lv_event_t* e);
    static void probArcEventCb(lv_event_t* e);
    static void chEnBtnEventCb(lv_event_t* e);
    static void inversionsSliderEventCb(lv_event_t* e);
    static void octavesSliderEventCb(lv_event_t* e);
    static void randRhythmBtnEventCb(lv_event_t* e);
    static void randNotesBtnEventCb(lv_event_t* e);

    // Arpeggiator UI elements & callbacks
    lv_obj_t* mArpPatternDd = nullptr;
    lv_obj_t* mArpRateDd = nullptr;
    lv_obj_t* mArpOctavesSlider = nullptr;
    lv_obj_t* mArpLatchBtn = nullptr;
    lv_obj_t* mArpStrumArc = nullptr;
    lv_obj_t* mArpProbArc = nullptr;
    lv_obj_t* mArpChordGenBtn = nullptr;
    lv_obj_t* mArpChordMoodDd = nullptr;
    lv_obj_t* mArpChordComplexityDd = nullptr;
    lv_obj_t* mArpInversionsSlider = nullptr;
    lv_obj_t* mArpToggleBtn = nullptr;

    void updateArpConfig();
    void updateChordConfig();

    static void arpPatternDdEventCb(lv_event_t* e);
    static void arpRateDdEventCb(lv_event_t* e);
    static void arpChordMoodDdEventCb(lv_event_t* e);
    static void arpChordComplexityDdEventCb(lv_event_t* e);
    static void arpToggleBtnEventCb(lv_event_t* e);

    // =========================================================================
    // --- Sequencer Screen ---
    // =========================================================================
    void populateSeqScreen();
    void rebuildSeqGrid();
    void openFileBrowser(bool isSave);
    void closeFileBrowser();

    // Sequencer state
    lv_obj_t*  mSeqGridContainer   = nullptr;
    lv_obj_t*  mSeqStepButtons[64] = {};
    lv_obj_t*  mSeqTransposeLbl    = nullptr;
    lv_obj_t*  mSeqOctaveLbl       = nullptr;
    lv_obj_t*  mSeqLengthLbl       = nullptr;
    lv_obj_t*  mSeqClockLbl        = nullptr;
    lv_obj_t*  mSeqProbLbl         = nullptr;
    lv_obj_t*  mSeqModal           = nullptr;
    int        mSeqTrackTranspose[8]      = {};
    int        mSeqTrackOctave[8]         = {};
    int        mSeqTrackLength[8]         = {64, 64, 64, 64, 64, 64, 64, 64};
    int        mSeqTrackClockDivIndex[8]  = {3, 3, 3, 3, 3, 3, 3, 3}; // index into clock div list; default ×1
    bool       mSeqTrackIs4x4[8]          = {};
    int        mSeqTrackHumanize[8]       = {};
    int        mSeqTrackProbability[8]    = {100, 100, 100, 100, 100, 100, 100, 100};
    bool       mSeqTrackSteps[8][64]      = {};
    lv_obj_t*  mSeqHumanValLbl            = nullptr;
    bool       mFileBrowserIsSave         = false;
    bool       mFileBrowserIsFmImport     = false;
    bool       mFileBrowserIsWtSelect     = false;
    bool       mFileBrowserIsWtImport     = false;
    bool       mFileBrowserIsSampleLoad   = false;
    bool       mFileBrowserIsSampleSave   = false;
    bool       mFileBrowserIsSfSelect     = false;
    bool       mFileBrowserIsSfImport     = false;
    bool       mFileBrowserIsPresetLoad   = false;
    bool       mFileBrowserIsPresetSave   = false;
    std::string mFileBrowserCurrentPath;
    int        mSelectedOpIdx             = 0;
    std::vector<bool> mSeqClipboard;
    std::string mSeqChainSlots[25]; // empty = unassigned

    // Chain slot cycle state (stub cycling index per slot)
    int mSeqChainCycleIndex[25] = {};

    // Step option popup state
    int mEditingStepIdx = -1;
    lv_obj_t* mStepModal = nullptr;
    lv_obj_t* mStepModalActiveLocksList = nullptr;
    
    // SoundFont & FM UI state
    lv_obj_t* mSoundFontActiveBankLbl = nullptr;
    lv_obj_t* mSoundFontActivePresetLbl = nullptr;
    lv_obj_t* mSoundFontPresetModal = nullptr;
    lv_obj_t* mFmActivePresetLbl = nullptr;
    lv_obj_t* mFmPresetModal = nullptr;
    lv_obj_t* mStepModalRatchetDd = nullptr;
    lv_obj_t* mStepModalNoteSlider = nullptr;
    lv_obj_t* mStepModalPunchSw = nullptr;
    lv_obj_t* mStepModalProbSlider = nullptr;
    lv_obj_t* mStepModalGateSlider = nullptr;
    lv_obj_t* mStepModalSkipSw = nullptr;
    lv_obj_t* mStepModalMicrotimingSlider = nullptr;
    lv_obj_t* mStepModalPLockDd = nullptr;
    lv_obj_t* mStepModalPLockSlider = nullptr;

    void openSeqStepModal(int stepIdx);
    void refreshStepModalLocksList();

    // Sequencer callbacks
    static void seqGridBtnEventCb(lv_event_t* e);
    static void seqGridToggleBtnEventCb(lv_event_t* e);
    static void seqPlayOrderBtnEventCb(lv_event_t* e);
    static void seqStepLongPressEventCb(lv_event_t* e);
    static void stepModalControlEventCb(lv_event_t* e);
    static void stepModalAddLockEventCb(lv_event_t* e);
    static void stepModalClearLocksEventCb(lv_event_t* e);
    static void stepModalCloseEventCb(lv_event_t* e);
    static void seqLengthArcEventCb(lv_event_t* e);
    static void seqDrumTabClickEventCb(lv_event_t* e);
    static void seqHumanizeArcEventCb(lv_event_t* e);
    static void seqProbArcEventCb(lv_event_t* e);
    static void seqClockDivArcEventCb(lv_event_t* e);
    static void seqTransposeBtnEventCb(lv_event_t* e);
    static void seqOctaveBtnEventCb(lv_event_t* e);
    static void seqCopyBtnEventCb(lv_event_t* e);
    static void seqPasteBtnEventCb(lv_event_t* e);
    static void seqClearBtnEventCb(lv_event_t* e);
    static void seqSaveBtnEventCb(lv_event_t* e);
    static void seqLoadBtnEventCb(lv_event_t* e);
    static void seqChainBoxEventCb(lv_event_t* e);
    static void fileBrowserCancelEventCb(lv_event_t* e);
    static void fileBrowserItemEventCb(lv_event_t* e);
    static void fileBrowserSaveBtnEventCb(lv_event_t* e);

    // =========================================================================
    // --- Assign Screen (Patch Bay, LFOs, FX Chain, Controller Map) ---
    // =========================================================================
    void populateAssignScreen();
    void rebuildActiveRoutings(lv_obj_t* parent);

    // Assign screen callbacks
    static void addRouteBtnEventCb(lv_event_t* e);
    static void deleteRouteBtnEventCb(lv_event_t* e);
    static void macroValueArcEventCb(lv_event_t* e);
    static void macroDropdownEventCb(lv_event_t* e);
    void assignMacroSourceLearned(int macroIdx, int paramId);
    void assignMacroSourceStatic(int macroIdx, int sourceType, int sourceIndex);
    static void macroSourceBtnEventCb(lv_event_t* e);
    static void macroStaticSourceSelectEventCb(lv_event_t* e);
    static void macroDestBtnEventCb(lv_event_t* e);
    static void macroDestArcEventCb(lv_event_t* e);
    static void lfoShapeDdEventCb(lv_event_t* e);
    static void lfoDepthArcEventCb(lv_event_t* e);
    static void lfoRateArcEventCb(lv_event_t* e);
    static void lfoSyncBtnEventCb(lv_event_t* e);
    static void fxChainDdEventCb(lv_event_t* e);
    static void physicalControlEventCb(lv_event_t* e);
    static void openRemapModalEventCb(lv_event_t* e);
    static void saveRemapModalEventCb(lv_event_t* e);
    static void closeRemapModalEventCb(lv_event_t* e);
    static void paramSelectorClickEventCb(lv_event_t* e);

    // FX chain picker and modulation picker callbacks
    static void pedalSlotClickEventCb(lv_event_t* e);
    static void assignPedalBtnEventCb(lv_event_t* e);
    static void removePedalBtnEventCb(lv_event_t* e);
    static void openModDestModalEventCb(lv_event_t* e);
    static void modDestCategoryClickEventCb(lv_event_t* e);
    static void modDestParamClickEventCb(lv_event_t* e);
    static void closeModDestModalEventCb(lv_event_t* e);
    static void clearModDestModalEventCb(lv_event_t* e);
    static void populateModDestCategories(UIManager* ui, bool isFxMode, lv_obj_t* leftCol, lv_obj_t* rightCol, int initialCat = -1);
    static void modDestToggleClickEventCb(lv_event_t* e);

    // Assign screen state
    
    // Assign screen real-time widgets tracking
    lv_obj_t* mAssignKnobArcs[24] = {nullptr};
    lv_obj_t* mAssignKnobValLabels[24] = {nullptr};
    lv_obj_t* mAssignFaderSliders[24] = {nullptr};
    lv_obj_t* mAssignFaderValLabels[24] = {nullptr};
    int mFxChainPedals[2][5]; // [chainIndex][slotIndex], stores pedal ID (0-16) or -1 for empty

    // Modal and routing picking state
    int mRemapSelectedParamId = -1;
    int mModDestTrack = 0;
    int mModDestType = 0;
    int mModDestParamId = -1;
    
    int mSelectedChainIdx = -1;
    int mSelectedSlotIdx = -1;

    lv_obj_t* mActiveRoutingsContainer = nullptr;
    lv_obj_t* mAssignTabview = nullptr;
    int mAssignActiveTabIdx = 0;
    lv_obj_t* mParamTabview = nullptr;
    int mParamActiveTabIdx = 0;
    int mLastActiveTrack = 0;
    int mLastActiveNav = 0;
    int mLastEngineType = -1;
    int mActiveDrumIdx = 0;
    lv_obj_t* mRemapModal = nullptr;
    int mRemapTargetIndex = 0; // 0-7 for knobs, 8-15 for faders
    lv_obj_t* mRemapCcSpinner = nullptr;
    lv_obj_t* mModDestModal = nullptr;
    lv_obj_t* mModDestBtnLabel = nullptr;
    lv_obj_t* mPedalPickerModal = nullptr;
    lv_obj_t* mMixerModal = nullptr;

    // Macro and LFO Custom Modulation Routing Destinations
    int mMacroDestParamId[8][2];    // 8 Macros, 2 Destinations each
    int mMacroDestTrack[8][2];
    int mMacroDestType[8][2];
    float mMacroDestAmount[8][2];
    lv_obj_t* mMacroDestBtnLabel[8][2];
    lv_obj_t* mMacroArc[8][2];

    int mActiveMacroLearnIdx = -1;  // -1 = Not learning, 0-7 = Macro 1-8
    int mActiveMacroLearnDestSlot = -1; // 0 or 1 for Dest 1 or Dest 2
    lv_obj_t* mMacroLearnModal = nullptr;

    int mLfoDestParamId[6];
    int mLfoDestTrack[6];
    int mLfoDestType[6];
    lv_obj_t* mLfoDestBtnLabel[6];

    // Modulation Picker Caller tracking
    int mModDestModalCallerType = 0; // 0 = Add Route, 1 = Macro, 2 = LFO
    int mModDestModalCallerIdx = 0;  // 0-7 for Macro/LFO
    int mModDestModalCallerSlot = 0; // 0 or 1 for Macro Dest 1 or Dest 2

    // =========================================================================
    // --- FX Screen ---
    // =========================================================================
    void populateFxScreen();
    static void fxControlEventCb(lv_event_t* e);

    // =========================================================================
    // --- Mix/Rec Screen ---
    // =========================================================================
    void populateMixRecScreen();
    void updateTransportVisuals();

    // Mix/Rec Screen Handles & State
    lv_obj_t* mMixRecBpmArc = nullptr;
    lv_obj_t* mMixRecSwingArc = nullptr;
    lv_obj_t* mMixRecMasterVolArc = nullptr;
    lv_obj_t* mMixRecPlayBtn = nullptr;
    lv_obj_t* mMixRecStopBtn = nullptr;
    lv_obj_t* mMixRecRecordBtn = nullptr;
    lv_obj_t* mMixerVolSliders[8] = {};
    lv_obj_t* mMixerVolLabels[8] = {};
    lv_obj_t* mMixerCards[8] = {};
    std::vector<uint32_t> mTapTimestamps;

    static void mixRecBpmKnobEventCb(lv_event_t* e);
    static void mixRecBpmTapEventCb(lv_event_t* e);
    static void mixRecSwingKnobEventCb(lv_event_t* e);
    static void mixRecMasterVolKnobEventCb(lv_event_t* e);
    static void mixRecPlayBtnEventCb(lv_event_t* e);
    static void mixRecStopBtnEventCb(lv_event_t* e);
    static void mixRecRecordBtnEventCb(lv_event_t* e);
    static void mixerSliderEventCb(lv_event_t* e);
    static void mixRecScreenDeleteEventCb(lv_event_t* e);
    
    // Pedal building helpers
    lv_obj_t* createPedalCard(lv_obj_t* parent, const char* name, lv_color_t accentColor);
    void addPedalKnob(lv_obj_t* pedal, const char* labelText, int paramId, float minVal, float maxVal, int decimals = 2);
    void addPedalSlider(lv_obj_t* pedal, const char* labelText, int paramId, float minVal, float maxVal);
    void addPedalToggle(lv_obj_t* pedal, const char* labelText, int paramId);
    void addPedalDropdown(lv_obj_t* pedal, const char* labelText, int paramId, const char* options);
    void addPedalSpacer(lv_obj_t* pedal, int height);

    // =========================================================================
    // --- Parameters Screen ---
    // =========================================================================
    void populateParamScreen();
    void populateParamSubtractiveOscTab(lv_obj_t* tab);
    void populateParamSubtractiveFilterTab(lv_obj_t* tab);
    void populateParamSubtractiveEnvTab(lv_obj_t* tab);
    
    void populateParamFmTab(lv_obj_t* tab1, lv_obj_t* tab2, lv_obj_t* tab3);
    void populateParamFmOperatorsTab(lv_obj_t* tab);
    void populateParamFmRoutingTab(lv_obj_t* tab);
    void populateParamFmFilterTab(lv_obj_t* tab);
    
    void populateParamWavetableTab(lv_obj_t* tab);
    void populateParamWavetableFilterTab(lv_obj_t* tab);
    
    void populateParamSamplerTab(lv_obj_t* tab);
    void populateParamSamplerSynthesisTab(lv_obj_t* tab);
    
    struct SynthParamData {
        UIManager* ui;
        int paramId;
        lv_obj_t* valLabel;
        float minVal;
        float maxVal;
        int decimals;
        bool isPercent;
        std::string labelText;
    };

    struct FmOpClickData {
        UIManager* ui;
        int opIdx;
        lv_obj_t* tab;
    };
    
    static void synthParamSliderEventCb(lv_event_t* e);
    static void synthParamDropdownEventCb(lv_event_t* e);
    static void fmOpBlockEventCb(lv_event_t* e);
    static void fmOpModeDropdownEventCb(lv_event_t* e);
    static void wtSelectBtnEventCb(lv_event_t* e);
    static void wtImportBtnEventCb(lv_event_t* e);
    
    static void samplerRecordBtnEventCb(lv_event_t* e);
    static void samplerLatchBtnEventCb(lv_event_t* e);
    static void samplerLoadBtnEventCb(lv_event_t* e);
    static void samplerSaveBtnEventCb(lv_event_t* e);
    static void samplerTrimBtnEventCb(lv_event_t* e);
    
    static void granularRecordBtnEventCb(lv_event_t* e);
    static void granularLatchBtnEventCb(lv_event_t* e);
    static void granularLoadBtnEventCb(lv_event_t* e);
    static void granularSaveBtnEventCb(lv_event_t* e);
    static void granularTrimBtnEventCb(lv_event_t* e);
    
    // SoundFont callbacks & methods
    static void soundfontLoadBtnCb(lv_event_t* e);
    static void soundfontImportBtnCb(lv_event_t* e);
    static void soundfontPresetSelectCb(lv_event_t* e);
    static void soundfontPresetItemSelectCb(lv_event_t* e);
    void populateParamSoundFontLibraryTab(lv_obj_t* tab);
    void populateParamSoundFontSynthTab(lv_obj_t* tab);

    // FM Preset callbacks
    static void fmPresetSelectCb(lv_event_t* e);
    static void fmPresetItemSelectCb(lv_event_t* e);

    // Audio In callbacks & methods
    static void audioInSourceDropdownEventCb(lv_event_t* e);
    void populateParamAudioInTab(lv_obj_t* tab);
    void populateParamAudioInFilterEnvTab(lv_obj_t* tab);

    // FM Drum callbacks & methods
    void populateParamFmDrumTab1(lv_obj_t* tab);
    void populateParamFmDrumTab2(lv_obj_t* tab);
    void addDrumVoiceStrip(lv_obj_t* parent, const char* name, int drumIdx);

    // Analogue Drum callbacks & methods
    void populateParamAnalogDrumTab1(lv_obj_t* tab);
    void populateParamAnalogDrumTab2(lv_obj_t* tab);
    void addAnalogDrumVoiceStrip(lv_obj_t* parent, const char* name, int drumIdx);
    
    static void randomizeParamsBtnEventCb(lv_event_t* e);
    static void defaultPatchBtnEventCb(lv_event_t* e);
    static void loadPatchBtnEventCb(lv_event_t* e);
    static void savePatchBtnEventCb(lv_event_t* e);

    void addSynthKnob(lv_obj_t* parent, const char* labelText, int paramId, float minVal, float maxVal, int decimals = 2, bool isPercent = false);
    void addSynthSlider(lv_obj_t* parent, const char* labelText, int paramId, float minVal, float maxVal, int decimals = 2, bool isPercent = false, int height = 280);
    void addSynthDropdown(lv_obj_t* parent, const char* labelText, int paramId, const char* options, int initialSel, bool isFilterMode = false, int width = 156);

    // =========================================================================
    // --- Settings Screen ---
    // =========================================================================
    void populateSettingsScreen();
    void populateSettingsGeneralTab(lv_obj_t* tab);
    void populateSettingsMidiPadsTab(lv_obj_t* tab);
    void populateSettingsKnobsFadersTab(lv_obj_t* tab);
    void populateSettingsSystemTab(lv_obj_t* tab);
    void rebuildPadGrid();
    std::string detectChordName(const int* notes, int count);
    
    // Settings UI handles
    lv_obj_t* mCpuLoadLabel = nullptr;
    lv_obj_t* mCpuLoadLabelSystem = nullptr;
    lv_obj_t* mWtActiveNameLbl = nullptr;
    lv_obj_t* mSettingsUpdateStatus = nullptr;
    lv_obj_t* mSettingsUpdateProgress = nullptr;
    
    // Sampler UI handles
    lv_obj_t* mSamplerWaveformContainer = nullptr;
    lv_obj_t* mSamplerWaveformBars[150] = {};
    lv_obj_t* mSamplerStartLine = nullptr;
    lv_obj_t* mSamplerEndLine = nullptr;
    lv_obj_t* mSamplerPlayheadLines[16] = {};
    lv_obj_t* mSamplerPlayheadShades[16] = {};
    lv_obj_t* mSamplerRecordBtn = nullptr;
    lv_obj_t* mSamplerLatchBtn = nullptr;
    lv_obj_t* mSamplerScrubHandle = nullptr;
    lv_obj_t* mSamplerSliceLines[16] = {};
    lv_obj_t* mSamplerSliceHandles[16] = {};
    static void samplerScrubHandleEventCb(lv_event_t* e);
    static void samplerSliceHandleEventCb(lv_event_t* e);
    void updateSamplerWaveformPreview();

    // Granular UI handles
    lv_obj_t* mGranularWaveformContainer = nullptr;
    lv_obj_t* mGranularWaveformBars[150] = {};
    lv_obj_t* mGranularStartLine = nullptr;
    lv_obj_t* mGranularEndLine = nullptr;
    lv_obj_t* mGranularPlayheadLine = nullptr;
    lv_obj_t* mGranularPlayheadShade = nullptr;
    lv_obj_t* mGranularRecordBtn = nullptr;
    lv_obj_t* mGranularLatchBtn = nullptr;
    lv_obj_t* mGranularLockBtn = nullptr;
    void populateParamGranularSamplingTab(lv_obj_t* tab);
    void populateParamGranularSynthTab(lv_obj_t* tab);
    void updateGranularWaveformPreview();
public:
    lv_obj_t* mSettingsRootDd = nullptr;
    lv_obj_t* mSettingsScaleDd = nullptr;
    lv_obj_t* mSettingsScaleBtn = nullptr;
    lv_obj_t* mSettingsScaleModal = nullptr;
    int mSelectedScaleIdx = 0;
    lv_obj_t* mSettingsTabview = nullptr;
    lv_obj_t* mSettingsPadGrid = nullptr;
    lv_obj_t* mSettingsMidiTrackDd = nullptr;
    lv_obj_t* mSettingsMidiInDd = nullptr;
    lv_obj_t* mSettingsMidiOutDd = nullptr;
    lv_obj_t* mSettingsCreditsModal = nullptr;
    lv_obj_t* mSettingsFxSelectModal = nullptr;
    
    // Settings – MIDI Pad state
    int mSettingsPadCount = 16;
    int mLastActiveFxPadIdx = -1;
    int mSettingsPadMode = 0;  // 0=FX, 1=Scales, 2=Chords, 3=FM Drum, 4=Analogue Drum
    int mSettingsOctaveOffset = 0; // -5 to +5
    lv_obj_t* mSettingsOctaveDd = nullptr;
    bool mSettingsFxPadMomentary = true; // true=Momentary, false=Toggle
    int mSettingsPadFxAssign[24] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};
    int mSettingsPadDrumAssign[24] = {0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7};
    int mSettingsPadChordNotes[24][8] = {};
    int mSettingsPadChordCount[24] = {};
    bool mSettingsPadFxToggleState[24] = {}; // For toggle mode: is FX currently on?
    int mSettingsChordRecordingPad = -1;
    int mSettingsMidiTrackSelect = 0;
    int mSettingsActiveTabIdx = 0;

    // Pad learn state
    int mSettingsPadNoteMap[24] = {20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43};
    bool mPadLearnActive = false;
    int mPadLearnTarget = -1;
    lv_obj_t* mPadLearnBtnLabel = nullptr;
    
    // Settings – Keyboard mode
    bool mSettingsKeyboardMode = true;
    std::string mSettingsAudioDevice = "Default";
    std::string mSettingsAudioMicDevice = "Default";
    std::string mSettingsAudioLineInDevice = "Default";
    std::string mSettingsFilePath;
    
    // Settings callbacks (General tab)
    static void settingsSampleRateDdEventCb(lv_event_t* e);
    static void settingsPanicBtnEventCb(lv_event_t* e);
    static void settingsResetMidiBtnEventCb(lv_event_t* e);
    static void settingsMidiTrackSelectDdEventCb(lv_event_t* e);
    static void settingsMidiInChannelDdEventCb(lv_event_t* e);
    static void settingsMidiOutChannelDdEventCb(lv_event_t* e);
    static void settingsScaleDropdownEventCb(lv_event_t* e);
    static void settingsScreenDeleteEventCb(lv_event_t* e);
    static void settingsAudioDeviceDdEventCb(lv_event_t* e);
    static void settingsUpdateBtnEventCb(lv_event_t* e);
    static void settingsRestartBtnEventCb(lv_event_t* e);
    static void settingsRebootBtnEventCb(lv_event_t* e);
    static void settingsShutdownBtnEventCb(lv_event_t* e);
    
    // Settings callbacks (MIDI Pads tab)
    static void settingsPadCountDdEventCb(lv_event_t* e);
    static void settingsKnobCountDdEventCb(lv_event_t* e);
    static void settingsSliderCountDdEventCb(lv_event_t* e);
    static void settingsOctaveDdEventCb(lv_event_t* e);
    static void settingsPadModeDdEventCb(lv_event_t* e);
    static void settingsFxPadBehaviorSwitchEventCb(lv_event_t* e);
    static void settingsPadBtnEventCb(lv_event_t* e);
    static void settingsPadReassignDdEventCb(lv_event_t* e);
    void openSettingsFxSelectPopup(int padIdx);
    static void settingsFxSelectBtnEventCb(lv_event_t* e);
    static void settingsFxSelectCloseEventCb(lv_event_t* e);
    void openScalePickerModal();
    static void settingsScaleSelectBtnEventCb(lv_event_t* e);
    static void settingsScaleSelectCloseEventCb(lv_event_t* e);
    static void settingsScaleBtnEventCb(lv_event_t* e);
    
    // Settings callbacks (System tab)
    static void settingsSaveBtnEventCb(lv_event_t* e);
    static void settingsNewBtnEventCb(lv_event_t* e);
    static void settingsLoadBtnEventCb(lv_event_t* e);
    static void settingsCreditsBtnEventCb(lv_event_t* e);
    static void settingsCreditsCloseEventCb(lv_event_t* e);
    static void settingsKeyboardModeSwitchEventCb(lv_event_t* e);

    // Helper functions
    void updateAudioEngineFxChains();
    std::vector<std::pair<int, std::string>> getTrackParamOptions(int trackIdx);
    static void populateModDestParams(UIManager* ui, int categoryIdx, lv_obj_t* rightCol);
    void saveSettings(const std::string& path);
    void loadSettings(const std::string& path);
    bool mFileBrowserIsProject = false;
    
    static float mapLinearToNonLinear(float norm, float minVal, float maxVal, const std::string& labelText);
    static float mapNonLinearToLinear(float rawVal, float minVal, float maxVal, const std::string& labelText);
    static float scaleParamFromNormalized(int paramId, float normValue);
    static float normalizeParamValue(int paramId, float scaledValue);
    
    static std::string getParameterNameString(int trackIdx, int paramId, AudioEngine* engine = nullptr);
    static std::string getCompactDestName(int trackIdx, int paramId, AudioEngine* engine = nullptr);

    std::atomic<bool> mUpdateCheckActive{false};
    std::atomic<bool> mUpdateCheckFinished{false};
    std::string mLatestVersionStr = "";
    std::atomic<bool> mUpdateInstallActive{false};
    std::atomic<bool> mUpdateInstallFinished{false};
    std::string mUpdateInstallStatusStr = "";
    int mUpdateInstallProgressPercent = 0;

    // MIDI Monitor log definitions
    struct MidiLogMessage {
        std::string typeStr;
        int channel;
        int data1;
        int data2;
    };
    std::vector<MidiLogMessage> mMidiLog;
    std::mutex mMidiLogMutex;
    void addMidiLog(const std::string& type, int channel, int d1, int d2);

    // Diagnostic console UI handles
    lv_obj_t* mMidiDeviceListLabel = nullptr;
    lv_obj_t* mMidiMonitorConsoleLabel = nullptr;

    // Bluetooth pairing manager declarations
    struct BtDevice {
        std::string mac;
        std::string name;
    };
    std::vector<BtDevice> mBtDevices;
    std::mutex mBtMutex;
    std::atomic<bool> mBtScanning{false};
    std::string mBtStatusStr = "Idle";
    std::atomic<bool> mBtStatusChanged{false};
    std::atomic<bool> mBtDeviceListChanged{false};

    lv_obj_t* mBtModal = nullptr;
    lv_obj_t* mBtListContainer = nullptr;
    lv_obj_t* mBtStatusLabel = nullptr;
    lv_obj_t* mSleepOverlay = nullptr;

    void openBtPairModal();
    void startBluetoothScan();
    void connectBluetoothDevice(const std::string& mac);

    static void settingsBtPairBtnEventCb(lv_event_t* e);
    static void btDeviceSelectEventCb(lv_event_t* e);
    static void btCloseEventCb(lv_event_t* e);
};

std::vector<std::string> getSystemConnectedMidiInputs();
std::vector<std::string> getSystemConnectedJoysticks();

#endif // UI_MANAGER_H
