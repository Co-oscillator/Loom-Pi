#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <bitset>
#include <memory>
#include <mutex>
#include <vector>

#include "Arpeggiator.h"
#include "EnvelopeFollower.h"
#include "RoutingMatrix.h"
#include "Sequencer.h"
#include "engines/AnalogDrumEngine.h"
#include "engines/AudioInEngine.h"
#include "engines/AutoPannerFx.h"
#include "engines/BitcrusherFx.h"
#include "engines/ChorusFx.h"
#include "engines/CompressorFx.h"
#include "engines/DelayFx.h"
#include "engines/Eq5BandFx.h"
#include "engines/FilterLfoFx.h"
#include "engines/FlangerFx.h"
#include "engines/FmDrumEngine.h"
#include "engines/FmEngine.h"
#include "engines/GalacticReverb.h"
#include "engines/GranularEngine.h"
#include "engines/LfoEngine.h"
#include "engines/OctaverFx.h"
#include "engines/OverdriveFx.h"
#include "engines/PhaserFx.h"
#include "engines/SamplerEngine.h"
#include "engines/SimpleFilterFx.h"
#include "engines/SlicerFx.h"
#include "engines/SoundFontEngine.h"
#include "engines/SubtractiveEngine.h"
#include "engines/TapeEchoFx.h"
#include "engines/TapeWobbleFx.h"
#include "engines/WavetableEngine.h"

class AudioEngine {
public:
  enum RecordingSource { MIC = 0, LINE_IN = 1, RESAMPLE = 2 };

  AudioEngine();
  virtual ~AudioEngine();

  void init(float sampleRate);
  void stop();

  // Audio Rendering methods
  void renderOutput(float *outputData, int32_t numFrames, int32_t numChannels);
  void renderInput(const float *inputData, int32_t numFrames, int32_t numChannels);

  void setAppDataDir(const std::string &dir);
  void saveAppState();
  void loadAppState();
  std::string getLastSamplePath(int trackIndex);

  // Control methods
  void setTrackVolume(int trackIndex, float volume);
  void setEngineType(int trackIndex, int type);
  void triggerNote(int trackIndex, int note, int velocity);
  void releaseNote(int trackIndex, int note);
  void setTempo(float bpm);
  float getBpm() const { return mBpm; }
  void setPlaying(bool playing);
  void setStep(int trackIndex, int stepIndex, bool active,
               const std::vector<int> &notes, float velocity = 0.8f,
               int ratchet = 1, bool punch = false, float probability = 1.0f,
               float gate = 1.0f, bool isSkipped = false,
               float subStepOffset = 0.0f,
               const std::vector<float> &noteOffsets = {},
               const std::vector<float> &noteVelocities = {});
  void setSequencerConfig(int trackIndex, int numPages, int stepsPerPage);
  // New helper for modulation without affecting UI state
  void updateEngineParameter(int trackIndex, int parameterId, float value,
                             bool immediate = false);

  void setFilterType(int trackIndex, int filterType);
  void setFilterCutoff(int trackIndex, float cutoff);
  void setFilterResonance(int trackIndex, float resonance);

  // setRouting takes specific destParamId now
  void setRouting(int destTrack, int sourceTrack, int source, int dest,
                  float amount, int destParamId = -1);
  void getFxChain(int *destination);
  void setParameter(int trackIndex, int parameterId, float value,
                    bool immediate = false);
  void setParameterPreview(int trackIndex, int parameterId, float value);
  void setSwing(float swing);
  void setTrackHumanize(int trackIndex, float amount);
  void setPatternLength(int trackIndex, int length);
  void setPlaybackDirection(int trackIndex, int direction);
  void setIsRandomOrder(int trackIndex, bool isRandom);
  int getPlaybackDirection(int trackIndex) const;
  bool getIsRandomOrder(int trackIndex) const;
  void setIsJumpMode(int trackIndex, bool isJump);
  void setSelectedFmDrumInstrument(int trackIndex, int drumIndex);
  void jumpToStep(int stepIndex);
  void setParameterLock(int trackIndex, int stepIndex, int parameterId,
                        float value);
  void setOpLevel(int trackIndex, int op, float l);
  float getOpLevel(int trackIndex, int op) const;
  void clearParameterLocks(int trackIndex, int stepIndex);
  void setIsRecording(bool isRecording);
  void setResampling(bool isResampling); // Deprecated: Use setRecordingSource
  void setRecordingSource(int source);   // New: MIC, RESAMPLE, or SYSTEM
  int getCurrentStep(int trackIndex, int drumIndex = -1);
  void setArpConfig(int trackIndex, int mode, int octaves, int inversion,
                    bool isLatched, bool isMutated,
                    const std::vector<std::vector<bool>> &rhythms,
                    const std::vector<int> &sequence,
                    const std::vector<float> &gateLengths,
                    float probability, float weird);
  void setChordProgConfig(int trackIndex, bool enabled, int mood,
                          int complexity);
  void setScaleConfig(int rootNote, const std::vector<int> &intervals);
  void getGranularPlayheads(int trackIndex, GranularEngine::PlayheadInfo *out,
                            int maxCount);
  void startRecordingSample(int trackIndex);
  void stopRecordingSample(int trackIndex);
  void setRecordingLocked(bool locked);
  std::vector<float> getSamplerWaveform(int trackIndex, int numPoints);
  void normalizeSample(int trackIndex);
  void resetSampler(int trackIndex);
  void loadSample(int trackIndex, const std::string &path);
  void loadWavetable(int trackIndex, const std::string &path);
  void loadDefaultWavetable(int trackIndex);
  void saveSample(int trackIndex, const std::string &path); // New
  void trimSample(int trackIndex);
  void loadSoundFont(int trackIndex, const std::string &path);
  void setSoundFontPreset(int trackIndex, int presetIndex);
  void setSoundFontMapping(int trackIndex, int knobIndex, int paramId);
  int getSoundFontPresetCount(int trackIndex);
  std::string getSoundFontPresetName(int trackIndex, int presetIndex);
  std::vector<std::string> getSoundFontFilesList();
  void clearSequencer(int trackIndex);
  void setMasterVolume(float volume);
  bool getVelocitySensitivityEnabled() const { return mVelocitySensitivityEnabled; }
  void setVelocitySensitivityEnabled(bool enabled) { mVelocitySensitivityEnabled = enabled; }
  bool getFastGranularEnabled() const { return mFastGranularEnabled; }
  void setFastGranularEnabled(bool enabled) { mFastGranularEnabled = enabled; }
  bool getStepActive(int trackIndex, int stepIndex, int drumIndex = -1);
  bool getIsPlaying() const { return mIsPlaying; }
  bool getIsRecording() const { return mIsRecording; }
  bool getIsRecordingSample() const { return mIsRecordingSample; }
  void getStepActiveStates(int trackIndex, bool *out, int maxSize);
  std::vector<Step> getSequencerSteps(int trackIndex);
  std::vector<Step> getDrumSequencerSteps(int trackIndex, int drumIndex);
  std::vector<float> getAllTrackParameters(int trackIndex); // New sync method
  void getFxSends(int trackIndex, float *dest);
  void getFxMix(int trackIndex, float *dest);
  std::vector<float> getSamplerSlicePoints(int trackIndex);
  std::vector<float> getRecordedSampleData(int trackIndex,
                                           float targetSampleRate);
  size_t getSampleLength(int trackIndex);
  uint64_t getActiveNoteMask(int trackIndex);
  void setPitchBend(int trackIndex, float semitones);
  void setPadMod(int trackIndex, float value);
  void panic();
  void loadFmPreset(int trackIndex, int presetId);
  void setClockMultiplier(int trackIndex, float multiplier);
  void setFilterMode(int trackIndex, int mode);
  void setArpTriplet(int trackIndex, bool isTriplet);
  void setArpRate(int trackIndex, float rate, int divisionMode);
  void setArpStrum(int trackIndex, float strum);
  float getCpuLoad();
  void setInputDevice(int deviceId);
  void setSidechainConfig(int trackIndex, int drumIndex);
  void setSlicePosition(int trackIndex, int sliceIndex, float position);
  void setTrackActive(int trackIndex, bool active);
  void setTrackPan(int trackIndex, float pan);
  void setTrackMute(int trackIndex, bool muted);
  void setTrackSolo(int trackIndex, bool soloed);
  void setSlices(int trackIndex, const std::vector<int> &starts,
                 const std::vector<int> &ends);

  void setChainEnabled(int trackIndex, bool enabled);
  void setChainLength(int trackIndex, int length);
  void setChainSlot(int trackIndex, int slotIndex, int laneIndex,
                    const std::vector<Step> &steps);

  // Audio Export
  void renderToWav(int numCycles, const std::string &path);
  void renderStereo(float *outBuffer, int numFrames);
  void updateSampleRate(float sampleRate);
  double getSampleRate() const { return mSampleRate; }
  int getScaleRoot() const;
  void pushSystemAudioSamples(const float *data,
                              int numSamples); // New: for System Audio

  // Track Management
  void initTrack(int i);
  void restoreTrackPreset(int trackIndex);
  void saveTrackPreset(int trackIndex);
  void saveTrackPresetToPath(int trackIndex, std::string path);
  void loadTrackPresetFromPath(int trackIndex, std::string path);

  // Routing / Macro Controls
  void setGenericLfoParam(int lfoIndex, int paramId, float value);
  void setMacroValue(int macroIndex, float value);
  void setFxChain(int sourceFx, int destFx);
  void rebuildFxActivityMask(); // Recalculates which FX slots have active sends

  // Global Transpose
  void setTrackTranspose(int trackIndex, int semitones);

  // MIDI Mode
  struct MidiMessage {
    int type;    // 0x90 (NoteOn), 0x80 (NoteOff)
    int channel; // Track Index (0-7)
    int data1;   // Note
    int data2;   // Velocity
  };

  void restorePresets();
  void resetMidiPatching();
  void saveProject(const std::string& path);
  void loadProject(const std::string& path);
  void newProject();

  // Returns flat array: [type, ch, d1, d2, type, ch, d1, d2...]
  int fetchMidiEvents(int *outBuffer, int maxEvents);

private:
  void enqueueMidiEvent(int type, int channel, int data1, int data2);
  std::vector<MidiMessage> mMidiQueue;
  std::mutex mMidiLock;

  // Command Queue for Race-Free UI->Audio Communication
  struct AudioCommand {
    AudioCommand()
        : type(NOTE_ON), trackIndex(0), data1(0), data2(0), value(0.0f),
          bValue(false), velocity(0.8f), ratchet(1), punch(false),
          probability(1.0f), gate(1.0f), isSkipped(false), extraData(0),
          immediate(false), subStepOffset(0.0f), laneIndex(-1) {}
    enum Type {
      NOTE_ON,
      NOTE_OFF,
      PARAM_SET,
      GLOBAL_PARAM_SET,
      SET_ENGINE_TYPE,
      SET_TRACK_VOLUME,
      SET_TRACK_PAN,
      SET_TRACK_ACTIVE,
      SET_TEMPO,
      SET_PATTERN_LENGTH,
      SET_STEP,
      SET_TRACK_MUTE,
      SET_TRACK_SOLO,
      SET_ARP_RATE,
      SET_ARP_STRUM,
      SET_SWING,
      SET_SLICES,
      SET_TRACK_HUMANIZE,
      SET_TRACK_TRANSPOSE,
      SET_PITCH_BEND,
      SET_CHAIN_ENABLED,
      SET_CHAIN_LENGTH,
      SET_CHAIN_SLOT,
      SET_PAD_MOD
    };
    Type type;
    int trackIndex;
    int data1;                         // note, or paramId
    int data2;                         // Second data field (e.g. stepIndex)
    float value;                       // velocity, or paramValue
    bool bValue;                       // Boolean value
    std::vector<int> notes;            // For SET_STEP
    float velocity;                    // For SET_STEP
    int ratchet;                       // For SET_STEP
    bool punch;                        // For SET_STEP
    float probability;                 // For SET_STEP
    float gate;                        // For SET_STEP
    bool isSkipped;                    // For SET_STEP
    std::vector<int> sliceStarts;      // For SET_SLICES
    std::vector<int> sliceEnds;        // For SET_SLICES
    int extraData;                     // extra
    bool immediate;                    // For P-Locks (Snap)
    float subStepOffset;               // For SET_STEP (Microtiming)
    std::vector<float> noteOffsets;    // For SET_STEP (Per-Note Microtiming)
    std::vector<float> noteVelocities; // For SET_STEP (Per-Note Velocity)
    std::vector<Step> steps;           // For SET_CHAIN_SLOT
    int laneIndex;                     // For SET_CHAIN_SLOT (-1 for melodic)
  };
  std::vector<AudioCommand> mCommandQueue;
  std::mutex mCommandLock;

  void processCommands();

  struct EngineEvent {
    enum Type { PATTERN_SAVE = 1 };
    Type type;
    int trackIndex;
    int data;
  };
  std::vector<EngineEvent> mEventQueue;
  std::mutex mEventLock;
  void enqueuePatternSaveEvent(int trackIndex, int slotIndex);

public:
  int fetchEngineEvents(int *outBuffer, int maxEvents);
  void updateGlobalParameter(int parameterId, float value);

  std::atomic<bool> mFirstRun{true};
  std::atomic<float> mCpuLoad{0.0f};

public:
  struct Track {
    float volume = 0.8f;
    float smoothedVolume = 0.8f;
    float pan = 0.5f;
    int transpose = 0;
    float sequencerProbability = 1.0f;
    float mPitchBend = 0.0f;  // -1.0 to 1.0 (semitones)
    float padModValue = 0.0f; // 0.0 to 1.0 (Y-axis)

    float smoothedPan = 0.5f;
    float humanize = 0.0f;
    bool isMuted = false;
    bool isSoloed = false;
    int engineType = 0; // 0=Subtractive, 1=FM, 2=Sampler, etc.
    int selectedFmDrumInstrument = 0;
    SubtractiveEngine subtractiveEngine;
    FmEngine fmEngine;
    FmDrumEngine fmDrumEngine;
    SamplerEngine samplerEngine;
    GranularEngine granularEngine;
    WavetableEngine wavetableEngine;
    AnalogDrumEngine analogDrumEngine;
    AudioInEngine audioInEngine;
    SoundFontEngine soundFontEngine;

    float parameters[2500] = {0.0f};
    float appliedParameters[2500] = {0.0f}; // Values after P-locks and Mods
    std::bitset<2500> mModulatedParams;     // Tracking for reset logic

    struct RecordingNote {
      int note;
      int stepIndex;
      int drumIdx; // -1 if synth
      uint64_t startGlobalStep;
      double startOffset; // within step
    };
    std::vector<RecordingNote> mRecordingNotes;

    Sequencer sequencer;
    Sequencer drumSequencers[16];
    Arpeggiator arpeggiator;
    EnvelopeFollower follower;
    float fxSends[18] = {0.0f};
    float smoothedFxSends[18] = {0.0f};
    float fxMix[18] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                       1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    bool isActive = false;
    bool isTrackEnabled = true;
    int mAftertouchDestParamId = -1;
    float mAftertouchRatchetCountdown = 0.0f;
    std::vector<int> mLiveHeldNotes;
    float currentFrequency = 440.0f;
    float lastOutput = 0.0f;
    float gainReduction = 1.0f;
    int patternLength = 16;
    int mPhysicallyHeldNoteCount = 0;
    int midiInChannel = 17; // 1-16, 17=ALL, 0=NONE
    int midiOutChannel = 0; // 0=NONE, 1-16
    struct PendingNote {
      int note;
      float velocity;
      double samplesRemaining;
      float gate = 0.5f;
      int ratchetCount = 1;
      bool punch = false;
    };
    std::vector<PendingNote> mPendingNotes;

    struct PendingParam {
      int id;
      float value;
      double samplesRemaining;
    };
    std::vector<PendingParam> mPendingParams;
    float mClockMultiplier = 1.0f;
    float mArpRate = 1.0f;    // 1.0 = 1/16th, 0.5 = 1/8th, etc.
    int mArpDivisionMode = 0; // 0=Reg, 1=Dotted, 2=Triplet
    int mActivePLocks[32];    // Track up to 32 P-locks per step for fast reset
    int mActivePLockCount = 0;
    bool mArpTriplet = false;
    bool mParametersDirty = true; // Flag for optimization

    double mStepCountdown = 0.0;
    double mArpCountdown = 0.0;
    int mInternalStepIndex = 0;

    struct ActiveNote {
      int note = -1;
      double durationRemaining = 0.0;
      bool active = false;
    };
    static const int MAX_POLYPHONY = 16;
    ActiveNote mActiveNotes[MAX_POLYPHONY];
    float panL = 0.707f;
    float panR = 0.707f;

    int mPunchCounter = 0; // Frames remaining for punch compression

    int mLastTriggeredNote = -1;
    double mNoteDurationRemaining = 0.0;
    double mDrumNoteDurationRemaining[16] = {0.0};
    int mDrumLastTriggeredNote[16] = {-1, -1, -1, -1, -1, -1, -1, -1,
                                      -1, -1, -1, -1, -1, -1, -1, -1};
    std::string lastSamplePath = "";
    struct ChainSlot {
      std::vector<Step> steps; // For melodic
      std::vector<std::vector<Step>> drumLanes =
          std::vector<std::vector<Step>>(16);
      bool hasSequence = false;
    };

    std::vector<ChainSlot> chainSlots = std::vector<ChainSlot>(100);
    int currentChainSlot = 0;
    int chainLength = 1;
    bool isChainEnabled = false;

    int mSilenceFrames = 0;
    int activeFmPreset = 0;
  };

  std::vector<Track> mTracks;
  RoutingMatrix mRoutingMatrix;
  std::atomic<bool> mIsPlaying{false};
  std::atomic<bool> mIsRecording{false};       // Transport record (sequencer)
  std::atomic<bool> mIsRecordingSample{false}; // Sample capture
  std::atomic<int> mRecordingSource{0};        // 0=Mic, 1=Resample, 2=System
  std::atomic<bool> mIsRecordingLocked{false};
  std::atomic<bool> mSampleRateChanged{false};
  std::atomic<float> mPendingSampleRate{48000.0f};
  int mRecordingTrackIndex = -1;
  float mBpm = 80.0f;
  double mSampleCount = 0;
  double mSamplesPerStep = 0;
  int mGlobalStepIndex = 0;
  int mPatternLength = 16;
  float mTempo = 80.0f;
  float mSwing = 0.0f;
  long mStartupFrames = 24000; // Wait ~500ms at 48k
  double mSampleRate = 44100.0;
  std::recursive_mutex mLock;
  // Bitmask: bit i set = FX slot i has at least one track sending to it (or is in a chain).
  // Rebuilt on every send/chain change. Avoids even the branch overhead for idle FX slots.
  uint32_t mActiveFxSlotMask = 0;
  void triggerNoteLocked(int trackIndex, int note, int velocity,
                         bool isSequencerTrigger = false, float gate = 0.95f,
                         bool punch = false, bool isArpTrigger = false);
  void releaseNoteLocked(int trackIndex, int note,
                         bool isSequencerTrigger = false);
  void setupTracks();

  // Global Effects
  GalacticReverb mReverbFx;
  DelayFx mDelayFx;
  SlicerFx mSlicerFxL, mSlicerFxR;
  CompressorFx mCompressorFx;
  FilterLfoFx mFilterLfoFx{FilterLfoMode::LowPass};

  // New Effects (Mono per channel)
  ChorusFx mChorusFxL, mChorusFxR;
  PhaserFx mPhaserFxL, mPhaserFxR;
  OverdriveFx mOverdriveFxL, mOverdriveFxR;
  BitcrusherFx mBitcrusherFxL, mBitcrusherFxR;
  TapeWobbleFx mTapeWobbleFx; // Stereo Linked!
  FlangerFx mFlangerFxL, mFlangerFxR;
  SimpleFilterFx mFilterPedalL[3]; // 3 Stereo Pairs of Filters
  SimpleFilterFx mFilterPedalR[3];
  TapeEchoFx mTapeEchoFxL, mTapeEchoFxR;
  OctaverFx mOctaverFxL, mOctaverFxR;
  Eq5BandFx mEq5BandFxL, mEq5BandFxR;
  bool mDelaySync = false;
  bool mTapeEchoSync = false;
  bool mFlangerSync = false;
  bool mPhaserSync = false;

  // Generic LFOs for Routing
  LfoEngine mLfos[6];

  // Macros (Patch Points)
  struct MacroModule {
    float value = 0.0f;
    int sourceType = 0; // 0=None, 1=Strip, 2=Knob, 3=LFO, 4=Envelope
    int sourceIndex = -1;
    int sourceTrackIndex = -1; // For Env/Track sources
  };
  MacroModule mMacros[8]; // Increased to 8 to match UI

  // Public methods for modulation
public:
  void setMacroSource(int macroIndex, int sourceType, int sourceIndex,
                      int sourceTrackIndex = -1);
  void applyModulations();

  // FX Chaining (Soft Routing)
  // Maps SourceFX Index -> DestinationFX Index. -1 means Master Mix.
  int mFxChainDest[18];
  // Feedback buffers for backward-chaining effects (1-sample latency)
  float mFxFeedbacksL[18] = {0.0f};
  float mFxFeedbacksR[18] = {0.0f};

  // FX Split Filter LFO Effects (Slots 9/10)
  FilterLfoFx mHpLfoL{FilterLfoMode::HighPass};
  FilterLfoFx mHpLfoR{FilterLfoMode::HighPass};
  FilterLfoFx mLpLfoL{FilterLfoMode::LowPass};
  FilterLfoFx mLpLfoR{FilterLfoMode::LowPass};

  int mSidechainSourceTrack = -1;
  int mSidechainSourceDrumIdx = -1;
  float mMasterVolume = 0.8f;
  bool mVelocitySensitivityEnabled = false;
  bool mFastGranularEnabled = false;
  bool mIsFloatFormat = true; // Assume Float, but verify at stream open
  int mAudioOutputMode = 2; // 0=Stereo, 1=Mono (L-Only), 2=Pseudo-Stereo (Delay), 3=Phase-Invert
  float mDelaySpreadBuffer[512] = {0.0f};
  int mDelaySpreadWritePtr = 0;

  int getAudioOutputMode() const { return mAudioOutputMode; }
  void setAudioOutputMode(int mode) { mAudioOutputMode = mode; }
  float mFxMixLevels[18] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f, 1.0f, 1.0f, 1.0f}; // Default to 1.0
  float mInputRingBuffer[8192] = {0.0f};
  std::atomic<uint32_t> mInputWritePtr{0};
  uint32_t mInputReadPtr = 0;
  std::atomic<int> mGlobalVoiceCount{0};
  std::string mAppDataDir = "";
  int mGlobalTranspose =
      0; // Keeping it for compatibility but will use per-track
  std::recursive_mutex &getLock() { return mLock; }
  static constexpr int kAudioBlockSize = 256;
  const std::vector<Track> &getTracks() const { return mTracks; }
  std::vector<Track> &getTracks() { return mTracks; }
};

#endif // AUDIO_ENGINE_H
