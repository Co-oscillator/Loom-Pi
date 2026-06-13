#define TSF_IMPLEMENTATION
#include "AudioEngine.h"
#include "WavFileUtils.h" // New
#include "engines/BitcrusherFx.h"
#include <fstream>
#include <sstream>
#include <dirent.h>

#include <algorithm>

#include <chrono>
#include <cmath>

#include <sys/resource.h>
#include <sys/stat.h>

#if defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

#undef LOG_TAG
#define LOG_TAG "AudioEngine"

// Fast, lock-free random generator (Linear Congruential Generator)
struct FastRandom {
  unsigned int seed = 123456789;
  inline float next() {
    seed = (seed * 1103515245 + 12345);
    return static_cast<float>(seed) / 4294967296.0f;
  }
} gRng;

// (Using fast_tanh from Utils.h)

static inline float softLimit(float x) {
  if (std::isnan(x))
    return 0.0f;
  
  // Smoother Knee Limiter
  // Starts saturating at 0.5f, with a smoother curve for high peaks
  float absX = std::abs(x);
  if (absX < 0.5f) return x;
  
  // Curve: f(x) = 0.5 + 0.5 * tanh((x - 0.5) / 0.5)
  float limited = 0.5f + 0.5f * fast_tanh((absX - 0.5f) * 2.0f);
  return (x > 0) ? limited : -limited;
}

AudioEngine::AudioEngine() {
  mSampleRate = 48000.0; // Default to common Android rate
  mBpm = 80.0f;
  setupTracks();
  for (int i = 0; i < 18; ++i)
    mFxChainDest[i] = -1;

  // FX Slot Filters (Slots 9/10)
  mHpLfoL.setMode(1); // HP
  mHpLfoR.setMode(1); // HP
  mHpLfoL.setCutoff(0.0f);
  mHpLfoR.setCutoff(0.0f);

  mLpLfoL.setMode(0); // LP
  mLpLfoR.setMode(0); // LP
  mLpLfoL.setCutoff(1.0f);
  mLpLfoR.setCutoff(1.0f);
  mHpLfoL.reset((float)mSampleRate);
  mHpLfoR.reset((float)mSampleRate);
  mLpLfoL.reset((float)mSampleRate);
  mLpLfoR.reset((float)mSampleRate);
  mSidechainSourceTrack = -1;
  mSidechainSourceDrumIdx = -1;
  mVelocitySensitivityEnabled = false;

  for (int i = 0; i < 18; ++i) {
    mFxMixLevels[i] = 1.0f;
    mFxChainDest[i] = -1;

    mFxFeedbacksL[i] = 0.0f;
    mFxFeedbacksR[i] = 0.0f;
  }

  // Initialize Filter Pedals
  mEq5BandFxL.reset();
  mEq5BandFxR.reset();

  for (int i = 0; i < 3; ++i) {
    mFilterPedalL[i].clear();
    mFilterPedalR[i].clear();
    // Default to mid-range cutoff (~632Hz) so filtering is audible
    mFilterPedalL[i].setCutoff(0.5f);
    mFilterPedalR[i].setCutoff(0.5f);
    mFilterPedalL[i].setMix(1.0f);
    mFilterPedalR[i].setMix(1.0f);
  }
  mFxMixLevels[12] = 1.0f; // Filter 1 Bus
  mFxMixLevels[15] = 1.0f; // Filter 2 Bus
  mFxMixLevels[16] = 1.0f; // Filter 3 Bus

  // Initialize other FX Mixes to 1.0 (Since we use per-track sends/mixes now)
  mChorusFxL.setMix(1.0f);
  mChorusFxR.setMix(1.0f);
  mPhaserFxL.setMix(1.0f);
  mPhaserFxR.setMix(1.0f);
  mFlangerFxL.setMix(1.0f);
  mFlangerFxR.setMix(1.0f);
  mOctaverFxL.setMix(0.0f);
  mOctaverFxR.setMix(0.0f);
  mTapeEchoFxL.setMix(1.0f);
  mTapeEchoFxR.setMix(1.0f);

  // Reverb and Delay can stay wet-only by default too
  mDelayFx.setMix(1.0f);
  mGlobalTranspose = 0;
  mInputWritePtr = 0;
  mInputReadPtr = 0;
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setTrackTranspose(int trackIndex, int semitones) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_TRANSPOSE;
  cmd.trackIndex = trackIndex;
  cmd.data1 = std::max(-24, std::min(24, semitones));
  {
    std::lock_guard<std::mutex> lock(mCommandLock);
    mCommandQueue.push_back(cmd);
  }
}

void AudioEngine::setPitchBend(int trackIndex, float semitones) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_PITCH_BEND;
  cmd.trackIndex = trackIndex;
  cmd.value = std::max(-12.0f, std::min(12.0f, semitones));
  {
    std::lock_guard<std::mutex> lock(mCommandLock);
    mCommandQueue.push_back(cmd);
  }
}

// Helper to reset a single track's parameters and engine state
void AudioEngine::initTrack(int i) {
  mTracks[i].volume = 0.25f;
  mTracks[i].smoothedVolume = 0.25f;
  mTracks[i].pan = 0.5f;
  mTracks[i].smoothedPan = 0.5f;
  mTracks[i].panL = 0.7071f;
  mTracks[i].panR = 0.7071f;
  mTracks[i].mSilenceFrames = 48002;
  mTracks[i].isActive = false; // Start inactive to save CPU

  // Attempt to load from default file if it exists
  if (!mAppDataDir.empty()) {
    std::string path = mAppDataDir + "/defaults/default_" +
                       std::to_string(mTracks[i].engineType) + ".gbs";
    std::ifstream file(path);
    if (file.is_open()) {
      std::string line;
      if (std::getline(file, line) && line == "LOOM_PRESET_V1") {
        int idx = 0;
        int pCount = sizeof(mTracks[i].parameters) / sizeof(float);
        while (std::getline(file, line) && idx < pCount) {
          try {
            mTracks[i].parameters[idx] = std::stof(line);
            mTracks[i].appliedParameters[idx] = mTracks[i].parameters[idx];
          } catch (...) {
          }
          idx++;
        }
        file.close();
        mTracks[i].mParametersDirty = true;
        // If we loaded a preset, we still want to initialize non-parameter
        // state but we skip the hardcoded param defaults
        // Reset Engines to match params
        mTracks[i].subtractiveEngine.resetToDefaults();
        mTracks[i].fmEngine.resetToDefaults();
        mTracks[i].fmDrumEngine.resetToDefaults();
        mTracks[i].analogDrumEngine.resetToDefaults();
        mTracks[i].wavetableEngine.resetToDefaults();
        mTracks[i].audioInEngine.resetToDefaults();
        mTracks[i].soundFontEngine.allNotesOff();

        // Apply loaded parameters to engines
        for (int p = 0; p < idx; ++p) {
          setParameter(i, p, mTracks[i].parameters[p], true);
        }
        return;
      }
      file.close();
    }
  }

  // Clear all parameters and FX sends to prevent ghost states (Phantom Panning)
  std::fill(std::begin(mTracks[i].parameters), std::end(mTracks[i].parameters),
            0.0f);
  std::fill(std::begin(mTracks[i].appliedParameters),
            std::end(mTracks[i].appliedParameters), 0.0f);
  std::fill(std::begin(mTracks[i].fxSends), std::end(mTracks[i].fxSends), 0.0f);
  std::fill(std::begin(mTracks[i].smoothedFxSends),
            std::end(mTracks[i].smoothedFxSends), 0.0f);
  std::fill(std::begin(mTracks[i].fxMix), std::end(mTracks[i].fxMix),
            1.0f); // Default to Unity!

  // Initialize defaults
  mTracks[i].subtractiveEngine.setSustain(1.0f);
  mTracks[i].subtractiveEngine.setDecay(0.5f);

  mTracks[i].fmEngine.resetToDefaults();
  mTracks[i].fmEngine.setParameter(101, 0.5f); // Decay
  mTracks[i].fmEngine.setParameter(102, 1.0f); // Sustain

  // Analog Drum Defaults
  mTracks[i].analogDrumEngine.resetToDefaults();
  for (int k = 0; k < 8; k++) {
    int base = 600 + k * 10;
    mTracks[i].parameters[base + 0] = 0.5f;  // Decay
    mTracks[i].parameters[base + 1] = 0.5f;  // Tone / Snap
    mTracks[i].parameters[base + 2] = 0.5f;  // Tune
    mTracks[i].parameters[base + 3] = 0.5f;  // Param A
    mTracks[i].parameters[base + 4] = 0.5f;  // Param B
    mTracks[i].parameters[base + 5] = 0.65f; // Gain
    
    mTracks[i].analogDrumEngine.setParameter(k, 0, 0.5f);
    mTracks[i].analogDrumEngine.setParameter(k, 1, 0.5f);
    mTracks[i].analogDrumEngine.setParameter(k, 2, 0.5f);
    mTracks[i].analogDrumEngine.setParameter(k, 3, 0.5f);
    mTracks[i].analogDrumEngine.setParameter(k, 4, 0.5f);
    mTracks[i].analogDrumEngine.setParameter(k, 5, 0.65f);
  }

  // Audio In Defaults
  mTracks[i].audioInEngine.resetToDefaults();

  // FM Drum Defaults
  for (int k = 0; k < 8; k++) {
    int base = 200 + k * 10;
    mTracks[i].parameters[base + 0] = 0.5f;  // Pitch
    mTracks[i].parameters[base + 1] = 0.5f;  // Tone
    mTracks[i].parameters[base + 2] = 0.20f; // Decay
    mTracks[i].parameters[base + 5] = 0.65f; // Gain / LVL

    mTracks[i].fmDrumEngine.setParameter(k, 0, 0.5f);  // Pitch
    mTracks[i].fmDrumEngine.setParameter(k, 1, 0.5f);  // Tone
    mTracks[i].fmDrumEngine.setParameter(k, 2, 0.20f); // Decay
    mTracks[i].fmDrumEngine.setParameter(k, 5, 0.65f); // Gain / LVL
  }

  // Sync parameters array with audible defaults so UI doesn't zero them out
  mTracks[i].parameters[0] = 0.7f;     // Volume
  mTracks[i].parameters[1] = 1.0f;     // Cutoff (Open)
  mTracks[i].parameters[2] = 0.0f;     // Resonance
  mTracks[i].parameters[112] = 1.0f;   // Cutoff ID 112 (Open)
  mTracks[i].parameters[113] = 0.0f;   // Resonance ID 113
  mTracks[i].parameters[151] = 1.0f;   // FM Cutoff (Open)
  mTracks[i].parameters[303] = 1.0f;   // Sampler/Granular Cutoff (Open)
  mTracks[i].parameters[458] = 1.0f;   // Wavetable Cutoff (Open)
  mTracks[i].parameters[9] = 0.5f;     // Pan (Center) - Fix "Left at Launch"
  mTracks[i].parameters[100] = 0.01f;  // Attack
  mTracks[i].parameters[101] = 0.5f;   // Decay
  mTracks[i].parameters[102] = 1.0f;   // Sustain
  mTracks[i].parameters[103] = 0.2f;   // Release
  mTracks[i].parameters[107] = 0.6f;   // Osc 1 Volume
  mTracks[i].parameters[108] = 0.4f;   // Osc 2 Volume
  mTracks[i].parameters[109] = 0.4f;   // Osc 3 (Sub) Volume
  mTracks[i].parameters[160] = 0.25f;  // Osc 1 Pitch (1.0)
  mTracks[i].parameters[161] = 0.25f;  // Osc 2 Pitch (1.0)
  mTracks[i].parameters[162] = 0.125f; // Osc 3 (Sub) Pitch (0.5)
  mTracks[i].parameters[163] = 0.25f;  // Osc 4 Pitch (1.0)

  // FM Defaults (Op Levels)
  mTracks[i].parameters[160] = 0.8f; // Op 1 Level
  mTracks[i].parameters[166] = 0.0f; // Op 2 Level (Modulator usually)

  // Wavetable Defaults
  mTracks[i].parameters[310] = 0.0f; // Position
  mTracks[i].parameters[311] = 0.0f; // Morph

  // Sampler Defaults
  mTracks[i].parameters[320] = 0.0f; // Mode: 1-Shot
  mTracks[i].parameters[302] = 0.4627f; // Speed (1.0x)
  mTracks[i].parameters[341] = 0.0f; // Slice Select (0)

  // CRITICAL: Force DSP to update immediately using these defaults.
  // The UI relies on the 'parameters' array, but the Engine needs
  // 'setParameter' calls.
  // Common Defaults
  setParameter(i, 0, 0.7f);     // Volume
  setParameter(i, 1, 1.0f);     // Cutoff (Compat)
  setParameter(i, 2, 0.0f);     // Resonance (Compat)
  setParameter(i, 112, 1.0f);   // Cutoff ID 112 (Open)
  setParameter(i, 113, 0.0f);   // Resonance ID 113
  setParameter(i, 151, 1.0f);   // FM Cutoff (Open)
  setParameter(i, 303, 1.0f);   // Sampler/Granular Cutoff (Open)
  setParameter(i, 458, 1.0f);   // Wavetable Cutoff (Open)
  setParameter(i, 100, 0.01f);  // Attack
  setParameter(i, 101, 0.5f);   // Decay
  setParameter(i, 102, 1.0f);   // Sustain
  setParameter(i, 103, 0.2f);   // Release
  setParameter(i, 107, 0.6f);   // Osc 1
  setParameter(i, 108, 0.4f);   // Osc 2
  setParameter(i, 109, 0.4f);   // Osc 3 (Sub)
  setParameter(i, 162, 0.125f); // Osc 3 Pitch (Sub)
  setParameter(i, 9, 0.5f);     // Pan (Center)
  setParameter(i, 302, 0.4627f); // Sampler Speed (1.0x)
  setParameter(i, 341, 0.0f);   // Slice Select (0)

  // Engine-Specific Defaults
  int type = mTracks[i].engineType;
  if (type == 0) {               // Subtractive
    setParameter(i, 104, 0.2f);  // Osc 1 Wave: Sawtooth
    setParameter(i, 105, 0.4f);  // Osc 2 Wave: Square
    setParameter(i, 160, 0.25f); // Osc 1 Pitch: 1.0 (Value * 4.0)
    setParameter(i, 161, 0.25f); // Osc 2 Pitch: 1.0 (Value * 4.0)
    setParameter(
        i, 162,
        0.5f); // Osc 3 (Sub) Pitch: 1.0 (Value * 2.0) -> One octave lower
    setParameter(i, 350, 1.0f); // Use Envelope: True
    setParameter(i, 107, 0.6f); // Ensure Osc 1 Vol is set
    setParameter(i, 9, 0.5f);   // PAN CENTER
  } else if (type == 1) {       // FM
    // Use the "Vibe" preset (ID 11) directly to guarantee good sound
    mTracks[i].fmEngine.loadPreset(11);

    // Ensure envelope is enabled
    setParameter(i, 350, 1.0f);

    // Sync critical params back to parameter array for UI consistency
    mTracks[i].parameters[156] = 0.2f;  // Algo 2
    mTracks[i].parameters[160] = 0.5f;  // Op 1 Level
    mTracks[i].parameters[161] = 0.05f; // Op 1 Atk
    mTracks[i].parameters[166] = 0.0f;  // Op 2 Level
  } else if (type == 5) {               // FM Drum
    // Initialize 8 drums (Kick, Snare, Tom, HH, OpenHH, Cymbal, Perc, Noise)
    for (int drum = 0; drum < 8; ++drum) {
      int baseId = 200 + (drum * 10);
      setParameter(i, baseId + 5, 0.7f);  // Gain
      setParameter(i, baseId + 2, 0.20f); // Decay
      setParameter(i, baseId + 1, 0.5f);  // Tone/Feedback
    }
  } else if (type == 4) { // Wavetable
    mTracks[i].wavetableEngine.resetToDefaults();
    setParameter(i, 458, 1.0f); // Cutoff
    // ADSR A=2, D=10, S=30, R=50 (approx scaled 0.0-1.0 or raw?)
    // Assuming 0-1 scale: 0.02, 0.1, 0.3, 0.5
    setParameter(i, 454, 0.02f);
    setParameter(i, 455, 0.1f);
    setParameter(i, 456, 0.3f);
    setParameter(i, 457, 0.5f);
    // Explicitly set bits/srate to full quality
    setParameter(i, 475, 0.0f); // Bits (0=Full)
    setParameter(i, 476, 0.0f); // Srate (0=Full)
  } else if (type == 3) {       // Granular
    mTracks[i].granularEngine.resetToDefaults();

    // Explicitly reset ALL parameters to match Engine defaults
    // This ensures that when "Restore Patch" is clicked, everything resets
    // and the UI (via getAllTrackParameters) reflects it.

    // Core (Window)
    setParameter(i, 400, 0.5f); // Position
    setParameter(i, 401, 1.0f); // Speed
    setParameter(i, 406, 0.2f); // Grain Size
    setParameter(i, 407, 0.5f); // Density
    setParameter(i, 415, 0.0f); // Spray
    setParameter(i, 429, 0.4f); // Gain

    // ADSR (Main) - 425-428
    setParameter(i, 425, 0.01f); // Attack (Main)
    setParameter(i, 426, 0.1f);  // Decay
    setParameter(i, 427, 1.0f);  // Sustain
    setParameter(i, 428, 0.2f);  // Release

    // Grain Envelope - 408-409 (Not typically exposed but good to reset)
    setParameter(i, 408, 0.5f);
    setParameter(i, 409, 0.5f);

    // Pitch & Mod
    setParameter(i, 410, 1.0f); // Pitch
    setParameter(i, 416, 0.0f); // Detune
    setParameter(i, 355, 0.0f); // Glide
    setParameter(i, 417, 0.0f); // Random Timing

    // Behavior
    setParameter(i, 420, 0.0f); // Reverse Prob
    setParameter(i, 419, 0.5f); // Width
    setParameter(i, 418, 0.2f); // Grain Count (approx 20)
    setParameter(i, 430, 1.0f); // Probability (100% default)

    // LFOs (Reset all to 0/Basic)
    // LFO 1 (402-405)
    setParameter(i, 402, 0.0f);
    setParameter(i, 403, 0.1f);
    setParameter(i, 404, 0.0f);
    setParameter(i, 405, 0.0f);
    // LFO 2 (411-414)
    setParameter(i, 411, 0.0f);
    setParameter(i, 412, 0.1f);
    setParameter(i, 413, 0.0f);
    setParameter(i, 414, 0.0f);
    // LFO 3 (421-424)
    setParameter(i, 421, 0.0f);
    setParameter(i, 422, 0.1f);
    setParameter(i, 423, 0.0f);
    setParameter(i, 424, 0.0f);

    setParameter(i, 350, 1.0f);  // Use Env
  } else if (type == 2) {        // Sampler
    setParameter(i, 330, 0.0f);  // Start
    setParameter(i, 331, 1.0f);  // End
    setParameter(i, 300, 0.5f);  // Pitch (Normal / 50%)
    setParameter(i, 301, 0.25f); // Stretch (1.0x / 25%)
    setParameter(i, 302, 0.4627f); // Speed (1.0x, non-linear)
    setParameter(i, 305, 0.0f);  // Filter Type: LowPass
    setParameter(i, 350, 1.0f);  // Use Envelope: True
    setParameter(i, 341, 0.0f);  // Default Slice Select to 0
    setParameter(i, 310, 0.01f); // Attack
    setParameter(i, 311, 0.5f);  // Decay
    setParameter(i, 312, 1.0f);  // Sustain
    setParameter(i, 313, 0.2f);  // Release
  } else if (type == 9) {        // SoundFont
    setParameter(i, 100, 0.01f); // Attack
    setParameter(i, 101, 0.1f);  // Decay
    setParameter(i, 102, 1.0f);  // Sustain
    setParameter(i, 103, 0.2f);  // Release
    setParameter(i, 112, 1.0f);  // Cutoff
    setParameter(i, 113, 0.0f);  // Resonance
    setParameter(i, 20, 1.0f);   // Filter Mode: Bypass (maps to index 3)
    setParameter(i, 7, 0.1f);    // LFO Rate
    setParameter(i, 8, 0.0f);    // LFO Depth
    setParameter(i, 114, 0.0f);  // LFO Shape: Sine

    // Auto-load default SoundFont if lastSamplePath is empty
    if (mTracks[i].lastSamplePath.empty()) {
      const char* home = getenv("HOME");
      std::string homeStr = home ? std::string(home) + "/Loom" : "./Loom";
      std::string defaultPath = homeStr + "/soundfonts/GeneralUser-GS.sf2";
      FILE* f = fopen(defaultPath.c_str(), "rb");
      if (f) {
        fclose(f);
        mTracks[i].soundFontEngine.load(defaultPath);
        mTracks[i].lastSamplePath = "GeneralUser-GS.sf2";
        mTracks[i].soundFontEngine.setPreset(0);
      }
    }
  } else if (type == 8) {        // Audio In
    setParameter(i, 100, 0.01f); // Attack
    setParameter(i, 101, 0.1f);  // Decay
    setParameter(i, 102, 1.0f);  // Sustain
    setParameter(i, 103, 0.1f);  // Release
    setParameter(i, 112, 1.0f);  // Cutoff
    setParameter(i, 113, 0.0f);  // Resonance
    setParameter(i, 114, 0.01f); // Filter Attack
    setParameter(i, 115, 0.1f);  // Filter Decay
    setParameter(i, 116, 1.0f);  // Filter Sustain
    setParameter(i, 117, 0.1f);  // Filter Release
    setParameter(i, 118, 0.0f);  // Filter Env Amount
    setParameter(i, 120, 0.0f);  // Gate Mode: Gated
    setParameter(i, 121, 0.5f);  // Input Gain
    setParameter(i, 122, 0.0f);  // Wavefold
    setParameter(i, 123, 0.0f);  // Filter Mode: LP
    setParameter(i, 1530, 0.5f); // EQ Band 1
    setParameter(i, 1531, 0.5f); // EQ Band 2
    setParameter(i, 1532, 0.5f); // EQ Band 3
    setParameter(i, 1533, 0.5f); // EQ Band 4
    setParameter(i, 1534, 0.5f); // EQ Band 5
  } else {
    setParameter(i, 350, 1.0f); // Default Env usage True
  }

  clearSequencer(i);
  setSequencerConfig(i, 4, 16); // 4 pages of 16 steps = 64 steps
}

void AudioEngine::setupTracks() {
  mTracks.reserve(8);
  for (int i = 0; i < 8; ++i) {
    mTracks.emplace_back();
    // Set default engine type per track
    switch (i) {
      case 0: mTracks[i].engineType = 0; break; // Track 1: Subtractive
      case 1: mTracks[i].engineType = 1; break; // Track 2: FM Synth
      case 2: mTracks[i].engineType = 4; break; // Track 3: Wavetable
      case 3: mTracks[i].engineType = 2; break; // Track 4: Sampler
      case 4: mTracks[i].engineType = 3; break; // Track 5: Granular
      case 5: mTracks[i].engineType = 5; break; // Track 6: FM Drum
      case 6: mTracks[i].engineType = 6; break; // Track 7: Analog Drum
      case 7: mTracks[i].engineType = 9; break; // Track 8: Soundfont
      default: mTracks[i].engineType = 0; break;
    }
    initTrack(i); // FORCE RESTORE PATCH STATE ON STARTUP
  }
}

void AudioEngine::restoreTrackPreset(int trackIndex) {
  if (trackIndex >= 0 && trackIndex < 8) {
    initTrack(trackIndex);
  }
}

void AudioEngine::saveTrackPreset(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex < 0 || trackIndex >= (int)mTracks.size() ||
      mAppDataDir.empty())
    return;

  auto &track = mTracks[trackIndex];
  // Ensure "defaults" directory exists
  std::string dir = mAppDataDir + "/defaults";
  mkdir(dir.c_str(), 0777);

  std::string path =
      dir + "/default_" + std::to_string(track.engineType) + ".gbs";
  std::ofstream file(path);
  if (file.is_open()) {
    file << "LOOM_PRESET_V1\n";
    for (int i = 0; i < 2500; i++) {
      file << track.parameters[i] << "\n";
    }
    file.close();
    LOGD("Saved default preset for engine %d to %s", track.engineType,
         path.c_str());
  }
}

void AudioEngine::saveTrackPresetToPath(int trackIndex, std::string path) {
  std::string content;
  {
    std::lock_guard<std::recursive_mutex> lock(mLock);
    if (trackIndex < 0 || trackIndex >= (int)mTracks.size())
      return;

    auto &track = mTracks[trackIndex];
    std::stringstream ss;
    ss << "LOOM_PRESET_V1\n";
    // Save parameters
    for (int i = 0; i < 2500; i++) {
      float val = track.parameters[i];
      if (!std::isfinite(val))
        val = 0.0f;
      ss << val << "\n";
    }
    // Save sequencer steps
    auto steps = track.sequencer.getSteps();
    ss << "STEPS_V1\n";
    ss << steps.size() << "\n";
    for (const auto &step : steps) {
      float firstVel = step.notes.empty() ? 0.8f : step.notes[0].velocity;
      if (!std::isfinite(firstVel))
        firstVel = 0.8f;
      float gate = step.gate;
      if (!std::isfinite(gate))
        gate = 1.0f;

      ss << (step.active ? 1 : 0) << " " << firstVel << " " << gate << " "
         << step.notes.size();
      for (const auto &noteInfo : step.notes)
        ss << " " << noteInfo.note;
      ss << "\n";
    }
    content = ss.str();
  }

  // Disk I/O outside of the lock
  std::string tempPath = path + ".tmp";
  std::ofstream file(tempPath);
  if (file.is_open()) {
    file << content;
    file.close();
    // Atomic rename
    if (std::rename(tempPath.c_str(), path.c_str()) == 0) {
      LOGD("Auto-saved track %d sequence to %s", trackIndex, path.c_str());
    }
  }
}

void AudioEngine::loadTrackPresetFromPath(int trackIndex, std::string path) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex < 0 || trackIndex >= (int)mTracks.size())
    return;
  std::ifstream file(path);
  if (file.is_open()) {
    std::string line;
    if (std::getline(file, line) && line == "LOOM_PRESET_V1") {
      int idx = 0;
      int pCount = sizeof(mTracks[trackIndex].parameters) / sizeof(float);
      while (std::getline(file, line) && idx < pCount) {
        try {
          mTracks[trackIndex].parameters[idx] = std::stof(line);
          mTracks[trackIndex].appliedParameters[idx] = mTracks[trackIndex].parameters[idx];
        } catch (...) {
        }
        idx++;
      }
      file.close();
      mTracks[trackIndex].mParametersDirty = true;
      
      // Reset engines to match parameters
      mTracks[trackIndex].subtractiveEngine.resetToDefaults();
      mTracks[trackIndex].fmEngine.resetToDefaults();
      mTracks[trackIndex].fmDrumEngine.resetToDefaults();
      mTracks[trackIndex].analogDrumEngine.resetToDefaults();
      mTracks[trackIndex].wavetableEngine.resetToDefaults();
      mTracks[trackIndex].audioInEngine.resetToDefaults();
      mTracks[trackIndex].soundFontEngine.allNotesOff();
      
      for (int p = 0; p < idx; ++p) {
        setParameter(trackIndex, p, mTracks[trackIndex].parameters[p], true);
      }
      LOGD("Loaded preset from: %s", path.c_str());
    }
    file.close();
  }
}

void AudioEngine::init(float sampleRate) {
  mSampleRate = sampleRate;
  mReverbFx.setSampleRate(mSampleRate);

  for (auto &t : mTracks) {
    t.subtractiveEngine.setSampleRate(mSampleRate);
    t.fmEngine.setSampleRate(mSampleRate);
    t.samplerEngine.setSampleRate(mSampleRate);
    t.granularEngine.setSampleRate(mSampleRate);
    t.wavetableEngine.setSampleRate(mSampleRate);
    t.fmDrumEngine.setSampleRate(mSampleRate);
    t.analogDrumEngine.setSampleRate(mSampleRate);
    t.soundFontEngine.setSampleRate(mSampleRate);
    t.audioInEngine.setSampleRate(mSampleRate);
  }

  // Initialize Filter Pedals
  for (int i = 0; i < 3; ++i) {
    mFilterPedalL[i].clear();
    mFilterPedalR[i].clear();
    // Default to mid-range cutoff (~632Hz) so filtering is audible
    mFilterPedalL[i].setCutoff(0.5f);
    mFilterPedalR[i].setCutoff(0.5f);
    mFilterPedalL[i].setMix(1.0f);
    mFilterPedalR[i].setMix(1.0f);
  }
}

void AudioEngine::updateSampleRate(float sampleRate) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mSampleRate = sampleRate;
  mReverbFx.setSampleRate(mSampleRate);
  for (auto &t : mTracks) {
    t.subtractiveEngine.setSampleRate(mSampleRate);
    t.fmEngine.setSampleRate(mSampleRate);
    t.samplerEngine.setSampleRate(mSampleRate);
    t.granularEngine.setSampleRate(mSampleRate);
    t.wavetableEngine.setSampleRate(mSampleRate);
    t.fmDrumEngine.setSampleRate(mSampleRate);
    t.analogDrumEngine.setSampleRate(mSampleRate);
    t.soundFontEngine.setSampleRate(mSampleRate);
    t.audioInEngine.setSampleRate(mSampleRate);
  }
}

void AudioEngine::stop() {
  // Generic stop, no hardware stream to close here
}

// Internal Note Logic
void AudioEngine::triggerNoteLocked(int trackIndex, int note, int velocity,
                                    bool isSequencerTrigger, float gate,
                                    bool punch, bool isArpTrigger) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    Track &track = mTracks[trackIndex];
    if (!track.isTrackEnabled) {
      return;
    }
    if (!isSequencerTrigger && !mVelocitySensitivityEnabled) {
      velocity = 100;
    }

    if (track.engineType == 7) {
      enqueueMidiEvent(0x90, track.midiOutChannel - 1, note, velocity);
      track.isActive = true;
      return;
    }

    // Apply Per-Track Transpose (Skip Drums and Slicers)
    if (track.engineType != 5 && track.engineType != 6) {
      bool isSliceMode =
          (track.engineType == 2 && track.parameters[320] > 1.5f);
      if (!isSliceMode) {
        note += track.transpose;
      }
    }

    if (!isSequencerTrigger) {
      track.mPhysicallyHeldNoteCount++;
      if (track.arpeggiator.getMode() != ArpMode::OFF) {
        if (track.mPhysicallyHeldNoteCount == 1) {
          track.mArpCountdown = 0; // Immediate trigger for new gesture
        }
        track.arpeggiator.addNote(note);
        return;
      }
    }

    // Check for "Tie" (Sustain across steps)
    if (isSequencerTrigger) {
      static int trigLog = 0;
      if (trigLog++ % 50 == 0 ||
          trackIndex == 1) { // ALWAYS log FM (T1) for now
        float samplesPerStep = (15.0f * mSampleRate) / std::max(1.0f, mBpm);
        float effectiveMultiplier = std::max(0.01f, track.mClockMultiplier);
        float trackSamplesPerStep = samplesPerStep / effectiveMultiplier;
        LOGD("SeqTrigger T%d Note=%d SPS=%.2f Multi=%.2f "
             "Gate=%.2f Bank=%d Punch=%d",
                            trackIndex, note, trackSamplesPerStep,
                            effectiveMultiplier, gate,
                            track.selectedFmDrumInstrument, punch);
      }
    } else {
      // Recording Logic Fix: Avoid duplicate notes if one is already recording
      // for this pitch
      if (mIsRecording) {
        for (const auto &rn : track.mRecordingNotes) {
          if (rn.note == note)
            return; // Already recording this note (Legato/Slide)
        }
      }
    }

    if (punch) {
      track.mPunchCounter = static_cast<int>(mSampleRate * 0.05f); // 50ms spike
    }

    // 1. Legato / Retrigger Check
    for (int i = 0; i < AudioEngine::Track::MAX_POLYPHONY; ++i) {
      if (track.mActiveNotes[i].active && track.mActiveNotes[i].note == note &&
          track.mActiveNotes[i].durationRemaining > 512) {
        float samplesPerStep = (15.0f * mSampleRate) / std::max(1.0f, mBpm);
        float trackSamplesPerStep =
            samplesPerStep / std::max(0.01f, track.mClockMultiplier);

        // Only tie if gate is explicitly long (legato)
        if (gate > 0.9f) {
          track.mActiveNotes[i].durationRemaining =
              trackSamplesPerStep * (gate + 0.05f); // Overlap for tie
          return; // Skip re-triggering (Legato)
        } else {
          // Retriggering: Cut it off and restart.
          track.mActiveNotes[i].active = false;
          mGlobalVoiceCount--;

          // RATCHET/RETRIGGER FIX: Explicitly release on engines for crisp
          // attack
          track.subtractiveEngine.releaseNote(note);
          track.fmEngine.releaseNote(note);
          track.samplerEngine.releaseNote(note);
          track.wavetableEngine.releaseNote(note);
          track.soundFontEngine.noteOff(note);
        }
      }
    }

    // 2. Setup Frequency & Track State
    float freq =
        (track.engineType == 5)
            ? 440.0f
            : 440.0f * powf(2.0f, (note - 69 + track.mPitchBend) / 12.0f);
    track.currentFrequency = freq;
    track.subtractiveEngine.setFrequency(freq, mSampleRate);
    track.fmEngine.setFrequency(freq, mSampleRate);
    track.wavetableEngine.setFrequency(freq, mSampleRate);
    track.analogDrumEngine.setSampleRate(mSampleRate);

    track.isActive = true;
    track.mSilenceFrames = 0;

    // RATCHET FIX (v2.2.1): If same note is already playing on this track,
    // ensure the engine stops it before we re-trigger for crisp attack.
    for (int i = 0; i < AudioEngine::Track::MAX_POLYPHONY; ++i) {
      if (track.mActiveNotes[i].active && track.mActiveNotes[i].note == note) {
        track.mActiveNotes[i].active = false;
        mGlobalVoiceCount--;
        // Inform engines to stop existing note for crisp re-trigger
        track.subtractiveEngine.releaseNote(note);
        track.fmEngine.releaseNote(note);
        track.samplerEngine.releaseNote(note);
        track.wavetableEngine.releaseNote(note);
      }
    }

    if (punch) {
      track.mPunchCounter = static_cast<int>(mSampleRate * 0.05f); // 50ms spike
    }

    // 3. Allocate Voice (under Global Cap)
    if (mGlobalVoiceCount < 64) {
      for (int i = 0; i < AudioEngine::Track::MAX_POLYPHONY; ++i) {
        if (!track.mActiveNotes[i].active) {
          track.mActiveNotes[i].active = true;
          track.mActiveNotes[i].note = note;
          mGlobalVoiceCount++;
          if (isSequencerTrigger) {
            float samplesPerStep = (15.0f * mSampleRate) / std::max(1.0f, mBpm);
            float trackSamplesPerStep =
                samplesPerStep / std::max(0.01f, track.mClockMultiplier);
            track.mActiveNotes[i].durationRemaining =
                trackSamplesPerStep * gate;
          } else {
            track.mActiveNotes[i].durationRemaining = 9999998.0f;
          }
          break;
        }
      }
    }

    // 4. Trigger Actual Synthesis Engines
    switch (track.engineType) {
    case 0:
      track.subtractiveEngine.triggerNote(note, velocity);
      break;
    case 1:
      track.fmEngine.triggerNote(note, velocity);
      break;
    case 2:
      track.samplerEngine.triggerNote(note, velocity);
      break;
    case 3:
      track.granularEngine.triggerNote(note, velocity);
      break;
    case 4:
      track.wavetableEngine.triggerNote(note, velocity);
      break;
    case 5:
      track.fmDrumEngine.triggerNote(note, velocity);
      break;
    case 6:
      track.analogDrumEngine.triggerNote(note, velocity);
      break;
    case 8: // AUDIO IN
      track.audioInEngine.triggerNote(note, velocity);
      break;
    case 9: // SOUNDFONT
      track.soundFontEngine.noteOn(note, velocity);
      break;
    }

    // 5. Recording Logic
    // Record if it's a manual tap OR an Arp trigger (but NOT a sequencer
    // playback trigger)
    bool isPhysicalNote = !isSequencerTrigger && !isArpTrigger;
    bool arpActive = track.arpeggiator.getMode() != ArpMode::OFF;

    if (mIsRecording && mIsPlaying && (!isSequencerTrigger || isArpTrigger)) {
      if (isPhysicalNote && arpActive) {
        // Do not record physical presses if Arp is handling them
      } else {
        double phase = (double)mSampleCount / (mSamplesPerStep + 0.001);
        // MICROTIMING FIX: Always record to current step with precise offset
        int stepOffset = 0;
        float subStep = static_cast<float>(phase);

        int currentStepIdx = track.sequencer.getCurrentStepIndex();

        if (track.engineType == 5 || track.engineType == 6) {
          int drumIdx = -1;
          if (note >= 60)
            drumIdx = note - 60;
          else if (note >= 0 && note < 16)
            drumIdx = note;

          if (drumIdx >= 0 && drumIdx < 16) {
            Step &s =
                track.drumSequencers[drumIdx].getStepsMutable()[currentStepIdx];
            // Use precise subStep
            s.addNote(note, static_cast<float>(velocity) / 127.0f, subStep);

            // Mirror to main sequencer so it is visible on the sequencer screen
            Step &mainS = track.sequencer.getStepsMutable()[currentStepIdx];
            mainS.addNote(note, static_cast<float>(velocity) / 127.0f, subStep);

            track.mRecordingNotes.push_back({note, currentStepIdx, drumIdx,
                                             (uint64_t)mGlobalStepIndex,
                                             (double)subStep});
          }
        } else if (track.engineType == 2 &&
                   track.samplerEngine.getPlayMode() >= 3) {
          int drumIdx = -1;
          if (note >= 60)
            drumIdx = note - 60;

          if (drumIdx >= 0 && drumIdx < 16) {
            Step &s =
                track.drumSequencers[drumIdx].getStepsMutable()[currentStepIdx];
            s.addNote(note, static_cast<float>(velocity) / 127.0f, subStep);

            // Mirror to main sequencer so it is visible on the sequencer screen
            Step &mainS = track.sequencer.getStepsMutable()[currentStepIdx];
            mainS.addNote(note, static_cast<float>(velocity) / 127.0f, subStep);

            track.mRecordingNotes.push_back({note, currentStepIdx, drumIdx,
                                             (uint64_t)mGlobalStepIndex,
                                             (double)subStep});
          }
        } else {
          Step &s = track.sequencer.getStepsMutable()[currentStepIdx];
          s.addNote(note, static_cast<float>(velocity) / 127.0f, subStep);

          track.mRecordingNotes.push_back({note, currentStepIdx, -1,
                                           (uint64_t)mGlobalStepIndex,
                                           (double)subStep});
        }
      }
    }
  }
}

// Internal Param Logic
void AudioEngine::setParameter(int trackIndex, int parameterId, float value,
                               bool immediate) {
  AudioCommand cmd;
  cmd.type = (trackIndex == -1) ? AudioCommand::GLOBAL_PARAM_SET
                                : AudioCommand::PARAM_SET;
  cmd.trackIndex = trackIndex;
  cmd.data1 = parameterId;
  cmd.value = value;
  cmd.immediate = immediate;

  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setParameterPreview(int trackIndex, int parameterId,
                                      float value) {
  // Preview only: Update engine directly without modifying base parameters
  // This is used during P-lock editing so knob changes don't affect the track
  updateEngineParameter(trackIndex, parameterId, value);
}

void AudioEngine::updateEngineParameter(int trackIndex, int parameterId,
                                        float value, bool immediate) {

  // Sanitization (v2.2.2 Stability)
  if (!std::isfinite(value)) {
    return;
  }

  // Global Parameters (trackIndex = -1)
  if (trackIndex == -1) {
    // Global FX Mix Levels (3000+)
    if (parameterId >= 3000 && parameterId < 3015) {
      int fxIdx = parameterId - 3000;
      mFxMixLevels[fxIdx] = value;
      // CRITICAL: Persist Mix Levels to Track 0
      if (mTracks.size() > 0)
        mTracks[0].parameters[parameterId] = value;
      // NO RETURN: Let it fall through if needed (though global might not need
      // fallthrough if track 0 is updated directly)
    }
  }

  // Filter Pedals (2200+) - Global but callable from Track 0
  if (parameterId >= 2200 && parameterId < 2300) {
    int filterIdx = -1;
    int subParam = -1;

    if (parameterId >= 2200 && parameterId < 2205) { // 2200-2204: Filter 1
      filterIdx = 0;
      subParam = parameterId - 2200;
    } else if (parameterId >= 2205 &&
               parameterId < 2210) { // 2205-2209: Filter 2
      filterIdx = 1;
      subParam = parameterId - 2205;
    } else if (parameterId >= 2210 &&
               parameterId < 2215) { // 2210-2214: Filter 3
      filterIdx = 2;
      subParam = parameterId - 2210;
    }

    if (filterIdx != -1) {
      if (subParam == 0) { // Cutoff
        mFilterPedalL[filterIdx].setCutoff(value);
        mFilterPedalR[filterIdx].setCutoff(value);
      } else if (subParam == 1) { // Resonance
        mFilterPedalL[filterIdx].setResonance(value);
        mFilterPedalR[filterIdx].setResonance(value);
      } else if (subParam == 2) { // Mode
        // Button sends 0.0, 1.0, 2.0 directly
        mFilterPedalL[filterIdx].setMode(value);
        mFilterPedalR[filterIdx].setMode(value);
      }
      // NOTE: subParam==3 (Mix) removed - filters use Pedal SEND control now
      // mFxMixLevels for filters are initialized to 1.0 and should stay there
      // NO RETURN: Let it fall through to update mParameters array for UI
      // Persistence
      if (immediate) {
        mFilterPedalL[filterIdx].snap();
        mFilterPedalR[filterIdx].snap();
      }
    }
  }

  // Global Parameters (trackIndex = -1 or parameterId in global FX range 490-599)
  if (trackIndex == -1 || (parameterId >= 490 && parameterId < 600)) {
    updateGlobalParameter(parameterId, value);
    if (trackIndex == -1) return;
  }

  if (trackIndex < 0 || trackIndex >= mTracks.size())

    return;
  if (parameterId < 0 || parameterId >= 2500)
    return;
  Track &track = mTracks[trackIndex];

  // Specific Logic for Global / Sends
  if (parameterId >= 2000 &&
      parameterId <
          2180) { // Covers all 18 FX slots (0-17, including 5-Band EQ)
    int fxIndex = (parameterId - 2000) / 10;
    int subId = (parameterId - 2000) % 10;
    if (fxIndex >= 0 && fxIndex < 18) {
      if (subId == 0) {
        track.fxSends[fxIndex] = value;
        rebuildFxActivityMask(); // Keep mask in sync whenever a send changes
      } else if (subId == 1) {
        track.fxMix[fxIndex] = 1.0f; // FORCE UNITY (Ignore 0.0 from sync)
      }
    }
    return;
  }

  // Common Track Params (< 100)
  if (parameterId < 100) {
    switch (parameterId) {
    case 0:
      track.volume = std::max(0.001f, value);
      break;
    case 9:
      track.pan = std::max(0.0f, std::min(value, 1.0f));
      {
        float angle = track.pan * (float)M_PI * 0.5f;
        track.panL = cosf(angle);
        track.panR = sinf(angle);
      }
      break;
    case 1: // Common Filter Cutoff
      track.subtractiveEngine.setCutoff(value);
      track.fmEngine.setFilter(value);
      track.samplerEngine.setFilterCutoff(value);
      track.wavetableEngine.setFilterCutoff(value);
      track.granularEngine.setParameter(1, value);
      track.soundFontEngine.setParameter(1, value);
      break;
    case 2: // Common Resonance
      track.subtractiveEngine.setResonance(value);
      track.fmEngine.setResonance(value);
      track.samplerEngine.setFilterResonance(value);
      track.wavetableEngine.setResonance(value);
      track.granularEngine.setParameter(2, value);
      track.soundFontEngine.setParameter(2, value);
      break;
    case 3: // Env Amount
      track.subtractiveEngine.setFilterEnvAmount(value);
      track.fmEngine.setParameter(3, value);
      track.soundFontEngine.setParameter(3, value);
      break;
    case 4:
      track.subtractiveEngine.setOscWaveform(1, value);
      break;
    case 5:
      track.subtractiveEngine.setOscVolume(0, std::max(0.001f, value));
      break;
    case 6:
      track.subtractiveEngine.setDetune(value);
      track.soundFontEngine.setParameter(6, value);
      break;
    case 7:
      // Cubic Scaling for Synth LFO too
      track.subtractiveEngine.setLfoRate(0.01f * powf(10.0f, value * 3.69897f));
      track.soundFontEngine.setParameter(7, value);
      break;
    case 8:
      track.subtractiveEngine.setLfoDepth(value);
      track.soundFontEngine.setParameter(8, value);
      break;
    }
  }
  // ADSR / Internal Params (100-149)
  else if (parameterId >= 100 && parameterId < 150) {
    switch (parameterId) {
    case 120:
      mTracks[trackIndex].audioInEngine.setParameter(120, value);
      if (value >= 0.5f) { // Open Mode
        // Force track active immediately (Wake Up)
        mTracks[trackIndex].isActive = true;
        mTracks[trackIndex].mSilenceFrames = 0;
      }
      break;
    case 123: // Audio In Filter Mode
      mTracks[trackIndex].audioInEngine.setParameter(123, value);
      break;
    case 100:
      track.subtractiveEngine.setAttack(value);
      track.samplerEngine.setAttack(value);
      track.granularEngine.setAttack(value);
      track.wavetableEngine.setAttack(value);
      track.fmEngine.setParameter(100, value);
      track.audioInEngine.setParameter(100, value);
      track.soundFontEngine.setParameter(100, value);
      break;
    case 101:
      track.subtractiveEngine.setDecay(value);
      track.samplerEngine.setDecay(value);
      track.granularEngine.setDecay(value);
      track.wavetableEngine.setDecay(value);
      track.fmEngine.setParameter(101, value);
      track.audioInEngine.setParameter(101, value);
      track.soundFontEngine.setParameter(101, value);
      break;
    case 102:
      track.subtractiveEngine.setSustain(value);
      track.samplerEngine.setParameter(parameterId, value);
      track.granularEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      track.wavetableEngine.setSustain(value);
      track.audioInEngine.setParameter(parameterId, value);
      track.soundFontEngine.setParameter(102, value);
      break;
    case 103:
      track.subtractiveEngine.setRelease(value);
      track.samplerEngine.setParameter(parameterId, value);
      track.granularEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      track.wavetableEngine.setRelease(value);
      track.audioInEngine.setParameter(parameterId, value);
      track.soundFontEngine.setParameter(103, value);
      break;
    case 104:
      track.subtractiveEngine.setOscWaveform(0, value);
      break;
    case 105:
      track.subtractiveEngine.setOscWaveform(1, value);
      break;
    case 106:
      track.subtractiveEngine.setDetune(value);
      break;
    case 107:
      track.subtractiveEngine.setOscVolume(0, value);
      break;
    case 108:
      track.subtractiveEngine.setOscVolume(1, value);
      break;
    case 109:
      track.subtractiveEngine.setOscVolume(2, value);
      break;
    case 110:
      track.subtractiveEngine.setNoiseLevel(value);
      break;
    case 112:
    case 113:
    case 122: // Wavefold
      track.subtractiveEngine.setParameter(parameterId, value);
      track.samplerEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      track.wavetableEngine.setParameter(parameterId, value);
      track.audioInEngine.setParameter(parameterId, value);
      track.soundFontEngine.setParameter(parameterId, value);
      break;
    case 118:
      track.subtractiveEngine.setFilterEnvAmount(value);
      track.samplerEngine.setFilterEnvAmount(value);
      track.fmEngine.setParameter(118, value);
      track.audioInEngine.setParameter(118, value);
      break;
    case 114:
      track.subtractiveEngine.setFilterAttack(value);
      track.samplerEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      break;
    case 115:
      track.subtractiveEngine.setFilterDecay(value);
      track.samplerEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      break;
    case 116:
      track.subtractiveEngine.setFilterSustain(value);
      track.samplerEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      break;
    case 117:
      track.subtractiveEngine.setFilterRelease(value);
      track.samplerEngine.setParameter(parameterId, value);
      track.fmEngine.setParameter(parameterId, value);
      break;
    }
  }
  // Filter & Env (120-149)
  else if (parameterId >= 120 && parameterId < 150) {
    track.subtractiveEngine.setParameter(parameterId, value);
    track.samplerEngine.setParameter(parameterId, value);
    track.granularEngine.setParameter(parameterId, value);
    track.wavetableEngine.setParameter(parameterId, value);
    track.fmDrumEngine.setParameter(track.selectedFmDrumInstrument, parameterId,
                                    value);
    track.audioInEngine.setParameter(parameterId, value);
  }
  // FM / Sound Design (150-199)
  else if (parameterId >= 150 && parameterId < 200) {
    if (track.engineType == 0) { // Subtractive
      track.subtractiveEngine.setParameter(parameterId, value);
    } else if (track.engineType == 1) { // FM
      if (parameterId == 196) { // Preset selection knob
        int totalPresets = 32 + (int)track.fmEngine.mCustomPresets.size();
        if (totalPresets > 0) {
          int presetIdx = (int)(value * (totalPresets - 1) + 0.5f);
          if (presetIdx >= 0 && presetIdx < totalPresets) {
            if (track.activeFmPreset != presetIdx) {
              // Re-use thread-safe loadFmPreset to reload all parameters in sync
              loadFmPreset(trackIndex, presetIdx);
            }
          }
        }
      } else if (parameterId == 156) {
        track.fmEngine.setParameter(156, value); // Mode
      } else {
        track.fmEngine.setParameter(parameterId, value);
      }
    } else if (track.engineType == 9) { // SoundFont
      if (parameterId == 180) { // Preset selection knob
        int presetCount = track.soundFontEngine.getPresetCount();
        if (presetCount > 0) {
          int presetIdx = (int)(value * (presetCount - 1) + 0.5f);
          if (presetIdx >= 0 && presetIdx < presetCount) {
            track.soundFontEngine.setPreset(presetIdx);
          }
        }
      } else if (parameterId == 181) { // Bank selection knob
        std::vector<std::string> sfFiles = getSoundFontFilesList();
        if (!sfFiles.empty()) {
          int bankIdx = (int)(value * (sfFiles.size() - 1) + 0.5f);
          if (bankIdx >= 0 && bankIdx < (int)sfFiles.size()) {
            const char* home = getenv("HOME");
            std::string homeStr = home ? std::string(home) + "/Loom" : "./Loom";
            std::string fullPath = homeStr + "/soundfonts/" + sfFiles[bankIdx];
            // Only load if it's different from the currently loaded bank
            if (track.lastSamplePath != sfFiles[bankIdx]) {
              track.soundFontEngine.load(fullPath);
              track.lastSamplePath = sfFiles[bankIdx];
              // reset preset parameter
              track.parameters[180] = 0.0f;
              track.appliedParameters[180] = 0.0f;
              track.soundFontEngine.setPreset(0);
            }
          }
        }
      }
    }
  }
  // FM Drum (200-299)
  else if (parameterId >= 200 && parameterId < 300) {
    track.fmDrumEngine.setParameter((parameterId - 200) / 10,
                                    (parameterId - 200) % 10, value);
  }
  // Sampler & Engine Sub-params (300-399)
  else if (parameterId >= 300 && parameterId < 400) {
    if (parameterId == 350) {
      track.subtractiveEngine.setUseEnvelope(value > 0.5f);
      track.fmEngine.setUseEnvelope(value > 0.5f);
      track.samplerEngine.setParameter(350, value);
      track.granularEngine.setParameter(350, value);
    } else if (parameterId == 355) {
      // User requested Curve: val * val * 0.3 (Max 0.3s)
      float glideVal = value * value * 0.3f;
      track.subtractiveEngine.setParameter(355, glideVal);
      track.fmEngine.setParameter(355, glideVal);
      track.samplerEngine.setParameter(355, glideVal);
      track.granularEngine.setParameter(355, glideVal);
      track.wavetableEngine.setParameter(355, glideVal);
      track.soundFontEngine.setParameter(355, glideVal);
    } else if (track.engineType == 5) {
      track.fmDrumEngine.setParameter(track.selectedFmDrumInstrument,
                                      parameterId - 300, value);
    } else {
      track.samplerEngine.setParameter(parameterId, value);
    }
  }
  // Granular (400-449)
  else if (parameterId >= 400 && parameterId < 450) {
    track.granularEngine.setParameter(parameterId, value);
  }
  // Wavetable (450-489)
  else if (parameterId >= 450 && parameterId < 490) {
    if (parameterId == 450)
      track.wavetableEngine.setParameter(0, value);
    else if (parameterId == 451)
      track.wavetableEngine.setParameter(1, value);
    else if (parameterId == 454)
      track.wavetableEngine.setAttack(value);
    else if (parameterId == 455)
      track.wavetableEngine.setDecay(value);
    else if (parameterId == 456)
      track.wavetableEngine.setSustain(value);
    else if (parameterId == 457)
      track.wavetableEngine.setRelease(value);
    else if (parameterId == 458)
      track.wavetableEngine.setFilterCutoff(value);
    else if (parameterId == 459)
      track.wavetableEngine.setResonance(value);
    else if (parameterId == 461)
      track.wavetableEngine.setParameter(11, value);
    else if (parameterId == 464)
      track.wavetableEngine.setParameter(14, value);
    else if (parameterId == 465)
      track.wavetableEngine.setParameter(15, value);
    else if (parameterId == 466)
      track.wavetableEngine.setParameter(16, value);
    else if (parameterId == 467)
      track.wavetableEngine.setParameter(17, value);
    else if (parameterId == 470) // Filter Mode (Added)
      track.wavetableEngine.setParameter(20, value);
    else if (parameterId == 471) // Filter Atk (Added)
      track.wavetableEngine.setParameter(21, value);
    else if (parameterId == 472) // Filter Dcy (Shared handle)
      track.wavetableEngine.setParameter(11, value);
    else if (parameterId == 473) // Filter Sus (Added)
      track.wavetableEngine.setParameter(23, value);
    else if (parameterId == 474) // Filter Rel (Added)
      track.wavetableEngine.setParameter(24, value);
    else if (parameterId == 475) // Bits (Moved from 530)
      track.wavetableEngine.setParameter(30, value);
    else if (parameterId == 476) // Srate (Moved from 531)
      track.wavetableEngine.setParameter(31, value);
  }
  // LP LFO (Pedal 10)
  if (parameterId >= 490 && parameterId < 500) {
    updateGlobalParameter(parameterId, value);
  }
  // Global Effects & Arp (500-599)
  else if (parameterId >= 500 && parameterId < 600) {
    updateGlobalParameter(parameterId, value);
    // CRITICAL: Persist All Global FX (500-599) to Track 0
    if (mTracks.size() > 0)
      mTracks[0].parameters[parameterId] = value;
  }
  // Analog Drum (600-699)
  else if (parameterId >= 600 && parameterId < 700) {
    int drumIdx = (parameterId - 600) / 10;
    int subId = (parameterId - 600) % 10;
    track.analogDrumEngine.setParameter(drumIdx, subId, value);
  }
  // Sampler Per-Slice Parameters (700-859)
  else if (parameterId >= 700 && parameterId < 860) {
    if (track.engineType == 2) {
      int sliceIdx = (parameterId - 700) / 10;
      int subParam = (parameterId - 700) % 10;
      track.samplerEngine.setSliceParameter(sliceIdx, subParam, value);
    }
  }
  // Sampler Per-Slice FX Sends (1000-1319)
  else if (parameterId >= 1000 && parameterId < 1320) {
    if (track.engineType == 2) {
      int sliceIdx = (parameterId - 1000) / 20;
      int slotIdx = (parameterId - 1000) % 20;
      track.samplerEngine.setSliceParameter(sliceIdx, 8 + slotIdx, value);
    }
  }
  // Midi Channels (800-809)
  else if (parameterId >= 800 && parameterId < 810) {
    if (parameterId == 800)
      track.midiInChannel = static_cast<int>(value);
    else if (parameterId == 801)
      track.midiOutChannel = static_cast<int>(value);
  }
  // Extra Global FX (1500-1599)
  else if (parameterId >= 1500 && parameterId < 1600) {
    updateGlobalParameter(parameterId, value);
    if (mTracks.size() > 0)
      mTracks[0].parameters[parameterId] = value;
  }
}
// End of updateEngineParameter

// Processing Commands
void AudioEngine::processCommands() {
  std::vector<AudioCommand> todo;
  {
    std::lock_guard<std::mutex> lock(mCommandLock);
    if (mCommandQueue.empty())
      return;
    todo.swap(mCommandQueue);
  }
  for (const auto &cmd : todo) {
    switch (cmd.type) {
    case AudioCommand::NOTE_ON:
      triggerNoteLocked(cmd.trackIndex, cmd.data1, (int)cmd.value, false);
      break;
    case AudioCommand::NOTE_OFF:
      releaseNoteLocked(cmd.trackIndex, cmd.data1, false);
      break;
    case AudioCommand::PARAM_SET:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].parameters[cmd.data1] = cmd.value;
        mTracks[cmd.trackIndex].appliedParameters[cmd.data1] = cmd.value;
        mTracks[cmd.trackIndex].mParametersDirty = true;
        updateEngineParameter(cmd.trackIndex, cmd.data1, cmd.value,
                              cmd.immediate);
      }
      break;
    case AudioCommand::GLOBAL_PARAM_SET:
      updateGlobalParameter(cmd.data1, cmd.value);
      break;
    case AudioCommand::SET_ENGINE_TYPE:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        if (mTracks[cmd.trackIndex].engineType != cmd.data1) {
          mTracks[cmd.trackIndex].engineType = cmd.data1;
          initTrack(cmd.trackIndex);
        }
      }
      break;
    case AudioCommand::SET_TRACK_VOLUME:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].volume = cmd.value;
        mTracks[cmd.trackIndex].mSilenceFrames = 0;
        if (cmd.value > 0.001f) {
          mTracks[cmd.trackIndex].isActive = true;
        }
      }
      break;
    case AudioCommand::SET_TRACK_PAN:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size())
        mTracks[cmd.trackIndex].pan = cmd.value;
      break;
    case AudioCommand::SET_TRACK_ACTIVE:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        auto &track = mTracks[cmd.trackIndex];
        track.isTrackEnabled = cmd.bValue;
        if (!cmd.bValue) {
          track.isActive = false;
          track.mSilenceFrames = 2405; // Force DSP bypass
          track.subtractiveEngine.allNotesOff();
          track.fmEngine.allNotesOff();
          track.fmDrumEngine.allNotesOff();
          track.analogDrumEngine.allNotesOff();
          track.wavetableEngine.allNotesOff();
          track.samplerEngine.allNotesOff();
          track.granularEngine.allNotesOff();
          track.soundFontEngine.allNotesOff();
          for (int v = 0; v < Track::MAX_POLYPHONY; ++v) {
            track.mActiveNotes[v].active = false;
          }
        } else {
          track.isActive = true;
          track.mSilenceFrames = 0;
        }
      }
      break;
    case AudioCommand::SET_TEMPO:
      mBpm = cmd.value;
      for (auto &track : mTracks) {
        track.samplerEngine.setProjectBpm(mBpm);
      }
      break;
    case AudioCommand::SET_TRACK_HUMANIZE:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < 8) {
        mTracks[cmd.trackIndex].humanize = cmd.value;
      }
      break;
    case AudioCommand::SET_TRACK_TRANSPOSE:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].transpose = cmd.data1;
      }
      break;
    case AudioCommand::SET_PITCH_BEND:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        Track &track = mTracks[cmd.trackIndex];
        track.mPitchBend = cmd.value;
        track.subtractiveEngine.setPitchBend(cmd.value);
        track.fmEngine.setPitchBend(cmd.value);
        track.wavetableEngine.setPitchBend(cmd.value);
        track.samplerEngine.setPitchBend(cmd.value);
        track.granularEngine.setPitchBend(cmd.value);
      }
      break;
    case AudioCommand::SET_PAD_MOD:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].padModValue = cmd.value;
      }
      break;
    case AudioCommand::SET_PATTERN_LENGTH:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        int length = (cmd.data1 <= 0) ? 1 : (cmd.data1 > 64 ? 64 : cmd.data1);
        mTracks[cmd.trackIndex].patternLength = length;
        int pages = (length + 15) / 16;
        mTracks[cmd.trackIndex].sequencer.setConfiguration(pages, 16);
        mTracks[cmd.trackIndex].sequencer.setLoopLength(length);
        for (int d = 0; d < 16; ++d) {
          mTracks[cmd.trackIndex].drumSequencers[d].setConfiguration(pages, 16);
          mTracks[cmd.trackIndex].drumSequencers[d].setLoopLength(length);
        }
      } else {
        mPatternLength =
            (cmd.data1 <= 0) ? 1 : (cmd.data1 > 64 ? 64 : cmd.data1);
        // Propagate to all track sequencers
        {
          int pages = (mPatternLength + 15) / 16;
          for (int i = 0; i < (int)mTracks.size(); ++i) {
            mTracks[i].patternLength = mPatternLength;
            mTracks[i].sequencer.setConfiguration(pages, 16);
            mTracks[i].sequencer.setLoopLength(mPatternLength);
            for (int d = 0; d < 16; ++d) {
              mTracks[i].drumSequencers[d].setConfiguration(pages, 16);
              mTracks[i].drumSequencers[d].setLoopLength(mPatternLength);
            }
          }
        }
      }
      break;
    case AudioCommand::SET_STEP:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        Step step;
        step.isSkipped = cmd.isSkipped;
        for (size_t i = 0; i < cmd.notes.size(); ++i) {
          int n = cmd.notes[i];
          float vel = (i < cmd.noteVelocities.size()) ? cmd.noteVelocities[i]
                                                      : cmd.velocity;
          float off = (i < cmd.noteOffsets.size()) ? cmd.noteOffsets[i]
                                                   : cmd.subStepOffset;
          step.addNote(n, vel, off);
        }
        step.active = cmd.bValue;
        step.ratchet = cmd.ratchet;
        step.punch = cmd.punch;
        step.probability = cmd.probability;
        step.gate = cmd.gate;

        int firstNote = cmd.notes.empty() ? 60 : cmd.notes[0];
        int drumIdx = -1;
        bool isSamplerChops =
            (mTracks[cmd.trackIndex].engineType == 2 &&
             mTracks[cmd.trackIndex].samplerEngine.getPlayMode() >= 3);

        if (mTracks[cmd.trackIndex].engineType == 5 ||
            mTracks[cmd.trackIndex].engineType == 6 || isSamplerChops) {
          if (firstNote >= 60)
            drumIdx = firstNote - 60;
          else if (firstNote >= 0 && firstNote < 16)
            drumIdx = firstNote;
          else if (firstNote >= 35) {
            if (firstNote == 35 || firstNote == 36)
              drumIdx = 0;
            else if (firstNote == 38 || firstNote == 40)
              drumIdx = 1;
            else if (firstNote == 39)
              drumIdx = 2; // Clap
            else if (firstNote == 41 || firstNote == 43 || firstNote == 45)
              drumIdx = 2; // Toms
            else if (firstNote == 42 || firstNote == 44 || firstNote == 46)
              drumIdx = 3; // Hats
            else if (firstNote == 49)
              drumIdx = 5; // Crash
            else
              drumIdx = (firstNote % 8);
          }
        }

        if (drumIdx >= 0 && drumIdx < 16) {
          mTracks[cmd.trackIndex].drumSequencers[drumIdx].setStep(cmd.data1,
                                                                  step);
        } else {
          mTracks[cmd.trackIndex].sequencer.setStep(cmd.data1, step);
        }
      }
      break;
    case AudioCommand::SET_TRACK_MUTE:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size())
        mTracks[cmd.trackIndex].isMuted = cmd.bValue;
      break;
    case AudioCommand::SET_TRACK_SOLO:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size())
        mTracks[cmd.trackIndex].isSoloed = cmd.bValue;
      break;
    case AudioCommand::SET_ARP_RATE:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].mArpRate = cmd.value;
        mTracks[cmd.trackIndex].mArpDivisionMode = cmd.data1;
      }
      break;
    case AudioCommand::SET_ARP_STRUM:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].arpeggiator.setStrum(cmd.value);
      }
      break;
    case AudioCommand::SET_SWING:
      mSwing = cmd.value;
      for (auto &track : mTracks) {
        track.sequencer.setSwing(mSwing);
      }
      break;
    case AudioCommand::SET_CHAIN_ENABLED:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].isChainEnabled = cmd.bValue;
      }
      break;
    case AudioCommand::SET_CHAIN_LENGTH:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].chainLength =
            std::max(1, std::min(16, cmd.data1));
      }
      break;
    case AudioCommand::SET_CHAIN_SLOT:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        auto &track = mTracks[cmd.trackIndex];
        if (cmd.data1 >= 0 && cmd.data1 < 16) {
          if (cmd.laneIndex == -1) {
            track.chainSlots[cmd.data1].steps = cmd.steps;
          } else if (cmd.laneIndex >= 0 && cmd.laneIndex < 16) {
            track.chainSlots[cmd.data1].drumLanes[cmd.laneIndex] = cmd.steps;
          }
          track.chainSlots[cmd.data1].hasSequence = true;
        }
      }
      break;
    case AudioCommand::SET_SLICES:
      if (cmd.trackIndex >= 0 && cmd.trackIndex < (int)mTracks.size()) {
        mTracks[cmd.trackIndex].samplerEngine.setSlicePoints(cmd.sliceStarts,
                                                             cmd.sliceEnds);
      }
      break;
      break;
    }
  }
}

// Consolidated Global Parameter Logic
void AudioEngine::updateGlobalParameter(int parameterId, float value) {
  // LP LFO (490-499)
  if (parameterId >= 490 && parameterId < 500) {
    int subId = parameterId % 10;
    if (subId == 0) {
      mLpLfoL.setRate(value);
      mLpLfoR.setRate(value);
    } else if (subId == 1) {
      mLpLfoL.setDepth(value);
      mLpLfoR.setDepth(value);
    } else if (subId == 2) {
      mLpLfoL.setShape(value);
      mLpLfoR.setShape(value);
    } else if (subId == 3) {
      mLpLfoL.setCutoff(value);
      mLpLfoR.setCutoff(value);
    } else if (subId == 4) {
      mLpLfoL.setResonance(value);
      mLpLfoR.setResonance(value);
    }
  }
  // Handle Global Effects & Arp (500-599)
  else if (parameterId >= 500 && parameterId < 600) {
    int fxId = (parameterId - 500) / 10;
    int subId = parameterId % 10;
    switch (fxId) {
    case 0: // Reverb (Slot 6)
      if (subId == 0)
        mReverbFx.setSize(value);
      else if (subId == 1)
        mReverbFx.setDamping(value);
      else if (subId == 2)
        mReverbFx.setModDepth(value);
      else if (subId == 3) {     // MIX
        mReverbFx.setMix(value); // Internal Mix
        mFxMixLevels[6] = 1.0f;  // Pass through routeFx at unity
      } else if (subId == 4)
        mReverbFx.setPreDelay(value);
      else if (subId == 5)
        mReverbFx.setType(static_cast<int>(value * 3.9f));
      else if (subId == 6)
        mReverbFx.setTone(value);
      break;
    case 1: // Chorus (Slot 2)
      if (subId == 0) {
        mChorusFxL.setRate(value);
        mChorusFxR.setRate(value);
      } else if (subId == 1) {
        mChorusFxL.setDepth(value);
        mChorusFxR.setDepth(value);
      } else if (subId == 2) { // MIX
        mChorusFxL.setMix(value);
        mChorusFxR.setMix(value);
        mFxMixLevels[2] = 1.0f;
      } else if (subId == 3) {
        mChorusFxL.setVoices(value);
        mChorusFxR.setVoices(value);
      }
      break;
    case 2: // Delay (Slot 5)
      if (subId == 0)
        mDelayFx.setDelayTime(value);
      else if (subId == 1)
        mDelayFx.setFeedback(value);
      else if (subId == 2) { // MIX
        mDelayFx.setMix(value);
        mFxMixLevels[5] = 1.0f;
      } else if (subId == 3)
        mDelayFx.setFilterMix(value);
      else if (subId == 4)
        mDelayFx.setFilterResonance(value);
      else if (subId == 5)
        mDelayFx.setType(static_cast<int>(value * 3.9f));
      else if (subId == 6)
        mDelayFx.setFilterMode(static_cast<int>(value * 2.9f));
      break;
    case 3: // Bitcrusher (Slot 1)
      if (subId == 0) {
        mBitcrusherFxL.setBits(value);
        mBitcrusherFxR.setBits(value);
      } else if (subId == 1) {
        mBitcrusherFxL.setRate(value);
        mBitcrusherFxR.setRate(value);
      } else if (subId == 2) { // MIX
        mBitcrusherFxL.setMix(value);
        mBitcrusherFxR.setMix(value);
        mFxMixLevels[1] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      }
      break;
    case 4: // Overdrive (Slot 0)
      if (subId == 0) {
        mOverdriveFxL.setDrive(value);
        mOverdriveFxR.setDrive(value);
      } else if (subId == 1) { // DISTORTION (Acts as Mix/Enable)
        mOverdriveFxL.setDistortion(value);
        mOverdriveFxR.setDistortion(value);
        // Ensure enabled
        mOverdriveFxL.setMix(1.0f);
        mOverdriveFxR.setMix(1.0f);
        mFxMixLevels[0] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      } else if (subId == 2) {
        mOverdriveFxL.setLevel(value);
        mOverdriveFxR.setLevel(value);
      } else if (subId == 3) {
        mOverdriveFxL.setTone(value);
        mOverdriveFxR.setTone(value);
      }
      break;
    case 5: // Phaser (Slot 3)
      if (subId == 0) {
        mPhaserFxL.setRate(value);
        mPhaserFxR.setRate(value);
      } else if (subId == 1) {
        mPhaserFxL.setDepth(value);
        mPhaserFxR.setDepth(value);
      } else if (subId == 2) { // MIX
        mPhaserFxL.setMix(value);
        mPhaserFxR.setMix(value);
        mFxMixLevels[3] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      } else if (subId == 3) {
        mPhaserFxL.setIntensity(value);
        mPhaserFxR.setIntensity(value);
      }
      break;
    case 6: // Tape Wobble (Slot 4)
      if (subId == 0)
        mTapeWobbleFx.setRate(value);
      else if (subId == 1)
        mTapeWobbleFx.setDepth(value);
      else if (subId == 2)
        mTapeWobbleFx.setSaturation(value);
      else if (subId == 3) { // MIX
        mTapeWobbleFx.setMix(value);
        mFxMixLevels[4] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      }
      break;
    case 7: // Slicer (Slot 7)
      if (subId < 3) {
        float rates1[] = {0.015625f, 0.03125f, 0.0625f, 0.125f,
                          0.25f,     0.5f,     1.0f,    2.0f};
        float rates2[] = {0.046875f, 0.09375f, 0.1875f, 0.375f,
                          0.75f,     1.5f,     3.0f,    6.0f};
        float rates3[] = {0.1f, 0.2f, 0.4f, 0.8f, 1.6f, 3.2f, 6.4f, 12.8f};
        if (subId == 0) {
          int idx = static_cast<int>(value * 7.99f);
          mSlicerFxL.setRate1(rates1[idx]);
          mSlicerFxR.setRate1(rates1[idx]);
          mSlicerFxL.setActive1(true);
          mSlicerFxR.setActive1(true);
        } else if (subId == 1) {
          int idx = static_cast<int>(value * 7.99f);
          mSlicerFxL.setRate2(rates2[idx]);
          mSlicerFxR.setRate2(rates2[idx]);
          mSlicerFxL.setActive2(true);
          mSlicerFxR.setActive2(true);
        } else if (subId == 2) {
          int idx = static_cast<int>(value * 7.99f);
          mSlicerFxL.setRate3(rates3[idx]);
          mSlicerFxR.setRate3(rates3[idx]);
          mSlicerFxL.setActive3(true);
          mSlicerFxR.setActive3(true);
        }
      } else if (subId == 3) {
        bool v = (value > 0.5f);
        mSlicerFxL.setActive1(v);
        mSlicerFxR.setActive1(v);
      } else if (subId == 4) {
        bool v = (value > 0.5f);
        mSlicerFxL.setActive2(v);
        mSlicerFxR.setActive2(v);
      } else if (subId == 5) {
        bool v = (value > 0.5f);
        mSlicerFxL.setActive3(v);
        mSlicerFxR.setActive3(v);
      } else if (subId == 6) { // DEPTH -> Acts as Mix
        mSlicerFxL.setDepth(value);
        mSlicerFxR.setDepth(value);
        mFxMixLevels[7] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      }
      break;
    case 8: // Compressor
      if (subId == 0)
        mCompressorFx.setThreshold(value);
      else if (subId == 1)
        mCompressorFx.setRatio(value);
      else if (subId == 2)
        mCompressorFx.setAttack(value);
      else if (subId == 3)
        mCompressorFx.setRelease(value);
      else if (subId == 4)
        mCompressorFx.setMakeup(value);
      else if (subId == 5)
        mSidechainSourceTrack = static_cast<int>(value);
      else if (subId == 6)
        mSidechainSourceDrumIdx = static_cast<int>(value);
      break;
    case 9: // HP LFO
      if (subId == 0) {
        mHpLfoL.setRate(value);
        mHpLfoR.setRate(value);
      } else if (subId == 1) {
        mHpLfoL.setDepth(value);
        mHpLfoR.setDepth(value);
      } else if (subId == 2) {
        mHpLfoL.setShape(value);
        mHpLfoR.setShape(value);
      } else if (subId == 3) {
        mHpLfoL.setCutoff(value);
        mHpLfoR.setCutoff(value);
      } else if (subId == 4) {
        mHpLfoL.setResonance(value);
        mHpLfoR.setResonance(value);
      } else if (subId == 5) { // ADDED MIX for HP LFO
        // Removed coupling
      }
      break;
    }
  }
  // Handle Extra Global FX (1500-1599)
  else if (parameterId >= 1500 && parameterId < 1600) {
    int fxId = (parameterId - 1500) / 10;
    int subId = parameterId % 10;
    switch (fxId) {
    case 0: // Flanger (Slot 11) - Insert Style
      if (subId == 0) {
        mFlangerFxL.setRate(value);
        mFlangerFxR.setRate(value);
      } else if (subId == 1) {
        mFlangerFxL.setDepth(value);
        mFlangerFxR.setDepth(value);
      } else if (subId == 2) { // MIX
        mFlangerFxL.setMix(value);
        mFlangerFxR.setMix(value);
        mFxMixLevels[11] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      } else if (subId == 3) {
        mFlangerFxL.setFeedback(value);
        mFlangerFxR.setFeedback(value);
      } else if (subId == 4) {
        float d = value * 0.02f;
        mFlangerFxL.setDelay(d);
        mFlangerFxR.setDelay(d);
      }
      break;
    case 1: // Echo (Slot 13)
      if (subId == 0) {
        mTapeEchoFxL.setDelayTime(value);
        mTapeEchoFxR.setDelayTime(value);
      } else if (subId == 1) {
        mTapeEchoFxL.setFeedback(value);
        mTapeEchoFxR.setFeedback(value);
      } else if (subId == 2) { // MIX
        mTapeEchoFxL.setMix(value);
        mTapeEchoFxR.setMix(value);
        mFxMixLevels[13] = 1.0f;
      } else if (subId == 3) { // DRIVE
        mTapeEchoFxL.setDrive(value);
        mTapeEchoFxR.setDrive(value);
      } else if (subId == 4) { // WOW
        mTapeEchoFxL.setWow(value);
        mTapeEchoFxR.setWow(value);
      } else if (subId == 5) { // FLUTTER
        mTapeEchoFxL.setFlutter(value);
        mTapeEchoFxR.setFlutter(value);
      }
      break;
    case 2: // Octaver (Slot 14)
      // Based on previous code: 0=Mix, 1=Mode, 2=Unison, 3=Detune?
      // Let's check the header I just viewed (hypothetically)
      // If header says: setMix,    case 2: // Octaver (Slot 14)
      if (subId == 0) { // MIX
        mOctaverFxL.setMix(value);
        mOctaverFxR.setMix(value);
        mFxMixLevels[14] = 1.0f;
        // REMOVED FORCE SEND: Let users control per-track sends
      } else if (subId == 1) {
        mOctaverFxL.setMode(value);
        mOctaverFxR.setMode(value);
      } else if (subId == 2) {
        mOctaverFxL.setUnison(value);
        mOctaverFxR.setUnison(value);
      } else if (subId == 3) {
        mOctaverFxL.setDetune(value);
        mOctaverFxR.setDetune(value);
      }
      break;
    case 3: // 5-Band EQ (Slot 17)
      if (subId >= 0 && subId < 5) {
        mEq5BandFxL.setBandGain(subId, (value - 0.5f) * 48.0f); // +/- 24dB
        mEq5BandFxR.setBandGain(subId, (value - 0.5f) * 48.0f);
      } else if (subId == 9) { // MIX
        mEq5BandFxL.setMix(value);
        mEq5BandFxR.setMix(value);
        mFxMixLevels[17] = 1.0f;
      }
      break;
    }
  }

  // Filter Pedals (2200-2214)
  if (parameterId >= 2200 && parameterId < 2215) {
    int filterIdx = -1;
    int subParam = -1;

    if (parameterId >= 2200 && parameterId < 2205) { // 2200-2204: Filter 1
      filterIdx = 0;
      subParam = parameterId - 2200;
    } else if (parameterId >= 2205 &&
               parameterId < 2210) { // 2205-2209: Filter 2
      filterIdx = 1;
      subParam = parameterId - 2205;
    } else if (parameterId >= 2210 &&
               parameterId < 2215) { // 2210-2214: Filter 3
      filterIdx = 2;
      subParam = parameterId - 2210;
    }

    if (filterIdx != -1) {
      if (subParam == 0) { // Cutoff
        mFilterPedalL[filterIdx].setCutoff(value);
        mFilterPedalR[filterIdx].setCutoff(value);
      } else if (subParam == 1) { // Resonance
        mFilterPedalL[filterIdx].setResonance(value);
        mFilterPedalR[filterIdx].setResonance(value);
      } else if (subParam == 2) { // Mode
        mFilterPedalL[filterIdx].setMode(value);
        mFilterPedalR[filterIdx].setMode(value);
      }
    }
  }

  // CRITICAL: Persist All Global Parameters to Track 0
  // This ensures the UI can read them back via getTrackParameters(0)
  if (!mTracks.empty()) {
    if ((parameterId >= 490 && parameterId < 1600) ||
        (parameterId >= 2200 && parameterId < 2215)) {
      mTracks[0].parameters[parameterId] = value;
    }
  }
}

void AudioEngine::releaseNoteLocked(int trackIndex, int note,
                                    bool isSequencerTrigger) {
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    Track &track = mTracks[trackIndex];
    if (!track.isTrackEnabled) {
      return;
    }
    if (track.engineType == 7) {
      enqueueMidiEvent(0x80, track.midiOutChannel - 1, note, 0);
      track.isActive = false;
    } else {
      if (!isSequencerTrigger) {
        track.mPhysicallyHeldNoteCount--;
        if (track.mPhysicallyHeldNoteCount <= 0) {
          track.mPhysicallyHeldNoteCount = 0;
          track.arpeggiator.onAllPhysicallyReleased();
          track.mArpCountdown = 0; // Reset for next gesture
        }
        if (track.arpeggiator.getMode() != ArpMode::OFF) {
          track.arpeggiator.removeNote(note);
        }
      }

      // Apply Per-Track Transpose (Match triggerNoteLocked logic)
      int transposedNote = note;
      if (track.engineType != 5 && track.engineType != 6) {
        bool isSliceMode =
            (track.engineType == 2 && track.parameters[320] > 1.5f);
        if (!isSliceMode) {
          transposedNote += track.transpose;
        }
      }

      // ALWAYS release engine notes for manual interaction,
      // even if Arp is ON, otherwise they get stuck when unlatching or
      // switching.
      for (int i = 0; i < Track::MAX_POLYPHONY; ++i) {
        if (track.mActiveNotes[i].active &&
            track.mActiveNotes[i].note == transposedNote) {
          track.mActiveNotes[i].active = false;
          mGlobalVoiceCount--;
          // Do NOT break, release all voices for this note to be safe
        }
      }
      if (mIsRecording && mIsPlaying && !isSequencerTrigger) {
        for (auto it = track.mRecordingNotes.begin();
             it != track.mRecordingNotes.end();) {
          if (it->note == note) {
            // Calculate gate length
            // Calculate gate length with wrapping support
            double currentPos =
                (double)mGlobalStepIndex +
                ((double)mSampleCount / (mSamplesPerStep + 0.001));
            double startPos = (double)it->startGlobalStep + it->startOffset;

            // Handle wrap-around (if currentPos < startPos)
            if (currentPos < startPos) {
              currentPos +=
                  static_cast<double>(mPatternLength > 0 ? mPatternLength : 16);
            }

            float gate = static_cast<float>(currentPos - startPos);

            if (gate < 0.1f)
              gate = 0.1f;
            if (gate > 64.0f)
              gate = 64.0f; // Max 64 steps in v2.2.1

            if (it->drumIdx >= 0 && it->drumIdx < 8) {
              track.drumSequencers[it->drumIdx]
                  .getStepsMutable()[it->stepIndex]
                  .gate = gate;
            } else {
              track.sequencer.getStepsMutable()[it->stepIndex].gate = gate;
            }
            it = track.mRecordingNotes.erase(it);
          } else {
            ++it;
          }
        }
      }

      track.subtractiveEngine.releaseNote(transposedNote);
      track.fmEngine.releaseNote(transposedNote);
      track.samplerEngine.releaseNote(transposedNote);
      track.fmDrumEngine.releaseNote(transposedNote);
      track.granularEngine.releaseNote(transposedNote);
      track.wavetableEngine.releaseNote(transposedNote);
      track.analogDrumEngine.releaseNote(transposedNote);
      track.audioInEngine.releaseNote(transposedNote);
      track.soundFontEngine.noteOff(transposedNote);
    }
  }
}

void AudioEngine::renderInput(const float *inputData, int32_t numFrames, int32_t channels) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  for (int i = 0; i < numFrames; ++i) {
    float combined = 0.0f;
    if (channels == 2) {
      if (mRecordingSource == MIC) {
        combined = inputData[i * 2];
      } else if (mRecordingSource == LINE_IN) {
        combined = inputData[i * 2 + 1];
      } else {
        combined = (inputData[i * 2] + inputData[i * 2 + 1]) * 0.5f;
      }
    } else {
      combined = inputData[i];
    }
    mInputRingBuffer[mInputWritePtr % 8192] = combined;
    mInputWritePtr++;
  }

  if (mRecordingSource != MIC && mRecordingSource != LINE_IN) {
    return;
  }

  if (mIsRecordingSample && mRecordingTrackIndex != -1) {
    auto &track = mTracks[mRecordingTrackIndex];
    constexpr int MAX_BATCH = 1024;
    float batchBuffer[MAX_BATCH];
    int batchCount = 0;

    for (int i = 0; i < numFrames; ++i) {
      float sampleToPush = 0.0f;
      if (channels == 2) {
        if (mRecordingSource == MIC) {
          sampleToPush = inputData[i * 2];
        } else {
          sampleToPush = inputData[i * 2 + 1];
        }
      } else {
        sampleToPush = inputData[i];
      }
      if (batchCount < MAX_BATCH) {
        batchBuffer[batchCount++] = sampleToPush;
      }
    }

    if (batchCount > 0) {
      if (track.engineType == 2)
        track.samplerEngine.pushSamples(batchBuffer, batchCount);
      else if (track.engineType == 3)
        track.granularEngine.pushSamples(batchBuffer, batchCount);
    }
  }
}

void AudioEngine::renderOutput(float *outputData, int32_t numFrames, int32_t numChannels) {
  if (mFirstRun.exchange(false)) {
#if !defined(__APPLE__) && !defined(__linux__)
    setpriority(PRIO_PROCESS, 0, -19);
#endif
  }

#if defined(__aarch64__)
  uint64_t fpcr;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= (1 << 24); // FZ
  fpcr |= (1 << 25); // DN
  asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__i386__) || defined(__x86_64__)
  uint32_t mxcsr = _mm_getcsr();
  _mm_setcsr(mxcsr | 0x8040); // FTZ | DAZ
#endif

  auto start = std::chrono::steady_clock::now();
  float *output = outputData;

  memset(output, 0, numFrames * numChannels * sizeof(float));

  if (mStartupFrames > 0) {
    mStartupFrames -= numFrames;
    return;
  }

  const int kBlockSize = 256;

  float samplesPerStep =
      (static_cast<float>(mSampleRate) * 60.0f) / (std::max(1.0f, mBpm) * 4.0f);
  // Safety: Prevent near-infinite loops if BPM is crazy high
  if (samplesPerStep < 10.0f)
    samplesPerStep = 10.0f;
  mSamplesPerStep = samplesPerStep;

  // --- Consolidated Thread Safety (v2.2.2 Stability Fix) ---
  // Acquire mLock BEFORE processCommands and render loop to ensure
  // state consistency during JNI calls and rendering.
  std::lock_guard<std::recursive_mutex> lock(mLock);

  processCommands();

  for (int frameIdx = 0; frameIdx < numFrames; frameIdx += kBlockSize) {
    int framesToDo = std::min(kBlockSize, numFrames - frameIdx);

    {
      // Unified Processing Block (Control + Audio + NoteOff)
      // mLock is already held by the outer onAudioReady scope.

      mSampleCount += framesToDo;
      while (mSampleCount >= samplesPerStep && samplesPerStep > 0.0f) {
        mSampleCount -= samplesPerStep;
        if (mIsPlaying)
          mGlobalStepIndex = (mGlobalStepIndex + 1) % mPatternLength;
      }

      for (int t = 0; t < (int)mTracks.size(); ++t) {
        Track &track = mTracks[t];
        float effectiveMultiplier = track.mClockMultiplier;

        // Only run Arp logic if NOT a drum track to prevent
        // dual-triggering/glitches
        bool isDrumTrack = (track.engineType == 5 || track.engineType == 6);
        if (!isDrumTrack && track.mArpTriplet &&
            track.arpeggiator.getMode() != ArpMode::OFF) {
          effectiveMultiplier *= 1.5f;
        }

        float trackSamplesPerStep =
            samplesPerStep / std::max(0.01f, effectiveMultiplier);

        if (trackSamplesPerStep < 2400.0f)
          trackSamplesPerStep = 2400.0f;

        if (mIsPlaying && trackSamplesPerStep > 0) {
          track.mStepCountdown -= framesToDo;
          int safetyCounter = 0;
          while (track.mStepCountdown <= 0 && safetyCounter < 4) {
            safetyCounter++;

            bool looped = track.sequencer.advance();

            // Apply SWING: even steps are longer, odd steps are shorter
            // mSwing range is -0.23 to +0.23
            int currentStep = track.sequencer.getCurrentStepIndex();
            float swingFactor =
                (currentStep % 2 == 0) ? (1.0f + mSwing) : (1.0f - mSwing);
            track.mStepCountdown += trackSamplesPerStep * swingFactor;
            if (looped && track.isChainEnabled) {
              // 1. COMMIT EXECUTED STEPS BACK TO SLOT (Required for
              // recording!)
              if (track.currentChainSlot >= 0 && track.currentChainSlot < 100) {
                track.chainSlots[track.currentChainSlot].steps =
                    track.sequencer.getSteps();
                for (int d = 0; d < 16; ++d) {
                  track.chainSlots[track.currentChainSlot].drumLanes[d] =
                      track.drumSequencers[d].getSteps();
                }
                track.chainSlots[track.currentChainSlot].hasSequence = true;

                // 2. SIGNAL FOR BACKGROUND SAVE TO FILE (if recording)
                if (mIsRecording) {
                  enqueuePatternSaveEvent(t, track.currentChainSlot);
                }
              }

              // 3. ADVANCE TO NEXT SLOT
              int nextSlot = track.currentChainSlot;
              int searchCount = 0;
              do {
                nextSlot = (nextSlot + 1) % 16;
                searchCount++;
              } while (
                  searchCount < 16 && !track.chainSlots[nextSlot].hasSequence &&
                  !mIsRecording); // If recording, even empty slots are fine!

              if (nextSlot != track.currentChainSlot) {
                track.currentChainSlot = nextSlot;
                const auto &slot = track.chainSlots[track.currentChainSlot];
                track.sequencer.setSteps(slot.steps);
                for (int i = 0; i < 16; ++i) {
                  track.drumSequencers[i].setSteps(slot.drumLanes[i]);
                }
              }
            }
            int seqStep = track.sequencer.getCurrentStepIndex();
            track.mInternalStepIndex = seqStep;

            // Restoration: Revert P-locks from the PREVIOUS step to base
            // values Optimized: Only restore parameters that were actually
            // modified
            for (int i = 0; i < track.mActivePLockCount; ++i) {
              int p = track.mActivePLocks[i];
              track.appliedParameters[p] = track.parameters[p];
              updateEngineParameter(t, p, track.parameters[p], true);
            }
            track.mActivePLockCount = 0;

            const std::vector<Step> &steps = track.sequencer.getSteps();
            if (seqStep < steps.size()) {
              const Step &s = steps[seqStep];
              if (s.active) {
                float combinedProb = s.probability * track.sequencerProbability;
                if (combinedProb >= 1.0f || gRng.next() <= combinedProb) {
                  for (const auto &ni : s.notes) {
                    double randomOffset = 0.0;
                    float velocityScale = 1.0f;
                    double effectiveSubStep = ni.subStepOffset;

                    if (track.humanize > 0.001f) {
                      // Increased Humanize intensity (v2.2.1 Feedback)
                      // Timing: Up to +/- 15% of step duration
                      randomOffset = (gRng.next() - 0.5f) * 0.3f *
                                     track.humanize * trackSamplesPerStep;
                      // Velocity: Up to +/- 30%
                      velocityScale =
                          1.0f + ((gRng.next() - 0.5f) * 0.6f * track.humanize);
                    }

                    // Sanity Clamp
                    if (effectiveSubStep < 0.0 || effectiveSubStep > 1.0)
                      effectiveSubStep = 0.0;

                    // FIX: Calculate offset within the current block
                    // accurately mStepCountdown has just been incremented by
                    // trackSamplesPerStep So the step event happened at:
                    // framesToDo + (mStepCountdown - trackSamplesPerStep)
                    double offsetInBlock =
                        (double)framesToDo +
                        (track.mStepCountdown - (double)trackSamplesPerStep);

                    double delayedSamples =
                        offsetInBlock + effectiveSubStep * trackSamplesPerStep +
                        randomOffset;

                    float ratchetedGate =
                        s.gate / static_cast<float>(s.ratchet);

                    // Apply velocity scaling
                    int vel =
                        static_cast<int>(ni.velocity * 127.0f * velocityScale);
                    if (vel < 1)
                      vel = 1;
                    if (vel > 127)
                      vel = 127;

                    if (delayedSamples <= 1.0) {
                      triggerNoteLocked(t, ni.note, vel, true, ratchetedGate);
                    } else {
                      track.mPendingNotes.push_back({ni.note, (float)vel,
                                                     delayedSamples,
                                                     ratchetedGate, 1});
                    }

                    if (s.ratchet > 1) {
                      float ratchetInterval =
                          trackSamplesPerStep / static_cast<float>(s.ratchet);
                      for (int r = 1; r < s.ratchet; ++r) {
                        double rDelay = delayedSamples + (r * ratchetInterval);
                        track.mPendingNotes.push_back(
                            {ni.note, ni.velocity * 127.0f, rDelay,
                             ratchetedGate, 1});
                      }
                    }
                  }
                  // v2.2.1: Apply Parameter Locks with microtiming alignment
                  // (Rushing fix)
                  if (!s.parameterLocks.empty()) {
                    double firstNoteOffset =
                        s.notes.empty() ? 0.0 : s.notes[0].subStepOffset;
                    for (auto const &[pid, val] : s.parameterLocks) {
                      track.appliedParameters[pid] = val;
                      if (firstNoteOffset > 0.001) {
                        track.mPendingParams.push_back(
                            {pid, val, firstNoteOffset * trackSamplesPerStep});
                      } else {
                        updateEngineParameter(t, pid, val, true);
                      }
                      // Track modified parameters for efficient restoration
                      if (track.mActivePLockCount < 32) {
                        bool alreadyTracked = false;
                        for (int k = 0; k < track.mActivePLockCount; ++k) {
                          if (track.mActivePLocks[k] == pid) {
                            alreadyTracked = true;
                            break;
                          }
                        }
                        if (!alreadyTracked) {
                          track.mActivePLocks[track.mActivePLockCount++] = pid;
                        }
                      }
                    }
                  }
                }
              }
            }

            // Drum Sequencer
            bool isSamplerChops = (track.engineType == 2 &&
                                   track.samplerEngine.getPlayMode() >= 3);
            if (track.engineType == 5 || track.engineType == 6 ||
                isSamplerChops) {
              for (int d = 0; d < 16; ++d) {
                track.drumSequencers[d].advance();
                int drumStep = track.drumSequencers[d].getCurrentStepIndex();
                const std::vector<Step> &dSteps =
                    track.drumSequencers[d].getSteps();
                if (drumStep < dSteps.size()) {
                  const Step &ds = dSteps[drumStep];
                  if (ds.active) {
                    float combinedProb = ds.probability * track.sequencerProbability;
                    if (combinedProb >= 1.0f ||
                        gRng.next() <= combinedProb) {
                      for (const auto &ni : ds.notes) {

                        double randomOffset = 0.0;
                        float velocityScale = 1.0f;
                        double effectiveSubStep = ni.subStepOffset;

                        // FIX: ADD HUMANIZE TO DRUM SEQUENCER
                        if (track.humanize > 0.001f) {
                          // Increased Drum Humanize (v2.2.1 Feedback)
                          randomOffset = (gRng.next() - 0.5f) * 0.3f *
                                         track.humanize * trackSamplesPerStep;
                          velocityScale = 1.0f + ((gRng.next() - 0.5f) * 0.6f *
                                                  track.humanize);
                        }

                        // Sanity Clamp
                        if (effectiveSubStep < 0.0 || effectiveSubStep > 1.0)
                          effectiveSubStep = 0.0;

                        // FIX: Calculate offset within the current block
                        // accurately
                        double offsetInBlock =
                            (double)framesToDo + (track.mStepCountdown -
                                                  (double)trackSamplesPerStep);

                        double delayedSamples =
                            offsetInBlock +
                            effectiveSubStep * trackSamplesPerStep +
                            randomOffset;

                        float ratchetedGate =
                            ds.gate / static_cast<float>(ds.ratchet);

                        int vel = static_cast<int>(ni.velocity * 127.0f *
                                                   velocityScale);
                        if (vel < 1)
                          vel = 1;
                        if (vel > 127)
                          vel = 127;

                        if (delayedSamples <= 1.0) {
                          triggerNoteLocked(t, ni.note, vel, true,
                                            ratchetedGate, ds.punch);
                        } else {
                          track.mPendingNotes.push_back(
                              {ni.note, (float)vel, delayedSamples,
                               ratchetedGate, 1, ds.punch});
                        }
                        if (ds.ratchet > 1) {
                          float ratchetInterval =
                              trackSamplesPerStep /
                              static_cast<float>(ds.ratchet);
                          for (int r = 1; r < ds.ratchet; ++r) {
                            double rDelay =
                                delayedSamples + (r * ratchetInterval);
                            track.mPendingNotes.push_back(
                                {ni.note, (float)vel, rDelay, ratchetedGate, 1,
                                 ds.punch});
                          }
                        }
                      }
                      // v2.2.1: Apply Parameter Locks with microtiming
                      // alignment
                      if (!ds.parameterLocks.empty()) {
                        double firstNoteOffset =
                            ds.notes.empty() ? 0.0 : ds.notes[0].subStepOffset;
                        for (auto const &[pid, val] : ds.parameterLocks) {
                          track.appliedParameters[pid] = val;
                          if (firstNoteOffset > 0.001) {
                            track.mPendingParams.push_back(
                                {pid, val,
                                 firstNoteOffset * trackSamplesPerStep});
                          } else {
                            updateEngineParameter(t, pid, val, true);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          if (track.mStepCountdown <= 0)
            track.mStepCountdown = trackSamplesPerStep;
        }

        // Arp Clock
        if (track.arpeggiator.getMode() != ArpMode::OFF) {
          float arpSamplesPerStep =
              samplesPerStep * std::max(0.125f, track.mArpRate);
          
          arpSamplesPerStep /= track.arpeggiator.getRateMultiplier();
          arpSamplesPerStep /= track.arpeggiator.getSpeedMultiplier();

          if (track.mArpDivisionMode == 1)
            arpSamplesPerStep *= 1.5f;
          else if (track.mArpDivisionMode == 2)
            arpSamplesPerStep *= 0.66667f;

          track.mArpCountdown -= framesToDo;
          int asafety = 0;
          while (track.mArpCountdown <= 0 && asafety < 8) {
            asafety++;
            track.mArpCountdown += arpSamplesPerStep;

            float currentGate = track.arpeggiator.getCurrentGate();
            std::vector<int> arpNotes = track.arpeggiator.nextNotes();

            float strum = track.arpeggiator.getStrum();
            int noteCount = (int)arpNotes.size();

            for (int i = 0; i < noteCount; ++i) {
              int arpNote = arpNotes[i];
              if (arpNote >= 0) {
                float delay = 0.0f;
                if (strum > 0.001f && noteCount > 1) {
                  // Spread notes over strum*stepDuration range
                  // Linear spread: 0 to strum*samplesPerStep ?
                  // Use 'arpSamplesPerStep' as reference duration
                  delay = (i / (float)noteCount) * strum * arpSamplesPerStep;
                }

                if (delay <= 1.0f) {
                  triggerNoteLocked(t, arpNote, 100, true, currentGate, false,
                                    true);
                } else {
                  track.mPendingNotes.push_back(
                      {arpNote, 100.0f, delay, currentGate, 1, false});
                }
              }
            }
          }
          if (track.mArpCountdown <= 0)
            track.mArpCountdown = arpSamplesPerStep;
        }

        // Process Pending Parameters (v2.2.1 Rushing Fix)
        for (auto it = track.mPendingParams.begin();
             it != track.mPendingParams.end();) {
          it->samplesRemaining -= framesToDo;
          if (it->samplesRemaining <= 0) {
            updateEngineParameter(t, it->id, it->value, true);
            it = track.mPendingParams.erase(it);
          } else {
            ++it;
          }
        }

        // Process Pending
        for (auto it = track.mPendingNotes.begin();
             it != track.mPendingNotes.end();) {
          it->samplesRemaining -= framesToDo;
          if (it->samplesRemaining <= 0) {
            triggerNoteLocked(t, it->note, static_cast<int>(it->velocity), true,
                              it->gate, it->punch);
            it = track.mPendingNotes.erase(it);
          } else {
            ++it;
          }
        }

        // Note Offs
        for (int i = 0; i < AudioEngine::Track::MAX_POLYPHONY; ++i) {
          if (track.mActiveNotes[i].active) {
            track.mActiveNotes[i].durationRemaining -= framesToDo;
            if (track.mActiveNotes[i].durationRemaining <= 0) {
              releaseNoteLocked(t, track.mActiveNotes[i].note, true);
              track.mActiveNotes[i].active = false;
            }
          }
        }
      }

      // Audio Block Rendering
      renderStereo(&output[frameIdx * numChannels], framesToDo);
    }

    // Push to resampling recorder (Sampler/Granular)
    bool isStillRecording = mIsRecordingSample;
    if (mRecordingSource == RESAMPLE && isStillRecording &&
        mRecordingTrackIndex != -1) {
      auto &recTrack = mTracks[mRecordingTrackIndex];
      std::vector<float> resampleBuffer;
      resampleBuffer.reserve(framesToDo);

      for (int k = 0; k < framesToDo; ++k) {
        float mixed = (output[(frameIdx + k) * numChannels] +
                       output[(frameIdx + k) * numChannels + 1]) *
                      0.5f;
        resampleBuffer.push_back(mixed);
      }

      if (!resampleBuffer.empty()) {
        if (recTrack.engineType == 2)
          recTrack.samplerEngine.pushSamples(resampleBuffer.data(),
                                             resampleBuffer.size());
        else if (recTrack.engineType == 3)
          recTrack.granularEngine.pushSamples(resampleBuffer.data(),
                                              resampleBuffer.size());
      }
    }
  }

  auto end = std::chrono::steady_clock::now();
  float elapsed = std::chrono::duration<float>(end - start).count();
  mCpuLoad =
      mCpuLoad * 0.95f + (elapsed / (numFrames / (float)mSampleRate)) * 0.05f;

  static int logCounter = 0;
  static float maxPeak = 0.0f;

  float currentPeak = 0.0f;
  for (int i = 0; i < numFrames * numChannels; ++i) {
    float a = std::abs(output[i]);
    if (a > currentPeak)
      currentPeak = a;
  }
  if (currentPeak > maxPeak)
    maxPeak = currentPeak;

  if (++logCounter > 187) { // ~Once per second at 48k/256
    logCounter = 0;
    int activeTracks = 0;
    for (const auto &tr : mTracks)
      if (tr.isActive)
        activeTracks++;

    LOGD("AudioEngine Stats: ActiveTracks=%d, MasterVol=%.2f, "
         "SampleRate=%.1f, "
         "BlockPeak=%.4f, MaxPeak=%.4f",
         activeTracks, mMasterVolume, (float)mSampleRate, currentPeak, maxPeak);

    // Extra debug: track states
    for (int t = 0; t < 8; ++t) {
      if (mTracks[t].isActive || mTracks[t].smoothedVolume > 0.01f) {
        LOGD("  T%d: Active=%s, SmVol=%.2f, Engine=%d, GainRed=%.2f", t,
             mTracks[t].isActive ? "YES" : "NO", mTracks[t].smoothedVolume,
             mTracks[t].engineType, mTracks[t].gainReduction);
      }
    }
    maxPeak = 0.0f; // Reset max peak every second
  }

}

void AudioEngine::triggerNote(int trackIndex, int note, int velocity) {
  int finalVelocity = velocity;
  if (!mVelocitySensitivityEnabled) {
    finalVelocity = 100;
  }
  AudioCommand cmd;
  cmd.type = AudioCommand::NOTE_ON;
  cmd.trackIndex = trackIndex;
  cmd.data1 = note;
  cmd.value = static_cast<float>(finalVelocity);
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::releaseNote(int trackIndex, int note) {
  AudioCommand cmd;
  cmd.type = AudioCommand::NOTE_OFF;
  cmd.trackIndex = trackIndex;
  cmd.data1 = note;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

// ... COPY OF OTHER METHODS ...
void AudioEngine::setArpRate(int trackIndex, float rate, int divisionMode) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_ARP_RATE;
  cmd.trackIndex = trackIndex;
  cmd.value = rate;
  cmd.data1 = divisionMode;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setArpStrum(int trackIndex, float strum) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_ARP_STRUM;
  cmd.trackIndex = trackIndex;
  cmd.value = strum;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setStep(int trackIndex, int stepIndex, bool active,
                          const std::vector<int> &notes, float velocity,
                          int ratchet, bool punch, float probability,
                          float gate, bool isSkipped, float subStepOffset,
                          const std::vector<float> &noteOffsets,
                          const std::vector<float> &noteVelocities) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_STEP;
  cmd.trackIndex = trackIndex;
  cmd.data1 = stepIndex;
  cmd.bValue = active;
  cmd.notes = notes;
  cmd.velocity = velocity;
  cmd.ratchet = ratchet;
  cmd.punch = punch;
  cmd.probability = probability;
  cmd.gate = gate;
  cmd.isSkipped = isSkipped;
  cmd.subStepOffset = subStepOffset;
  cmd.noteOffsets = noteOffsets;
  cmd.noteVelocities = noteVelocities;

  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::getFxSends(int trackIndex, float *dest) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    for (int i = 0; i < 17; ++i) {
      dest[i] = mTracks[trackIndex].fxSends[i];
    }
  }
}

void AudioEngine::getFxMix(int trackIndex, float *dest) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    for (int i = 0; i < 17; ++i) {
      dest[i] = mTracks[trackIndex].fxMix[i];
    }
  }
}

std::vector<float> AudioEngine::getAllTrackParameters(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  std::vector<float> params;
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    for (int i = 0; i < 1024; ++i) {
      params.push_back(mTracks[trackIndex].parameters[i]);
    }
  }
  return params;
}

void AudioEngine::setSequencerConfig(int trackIndex, int numPages,
                                     int stepsPerPage) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].sequencer.setConfiguration(numPages, stepsPerPage);
    if (mTracks[trackIndex].engineType == 5 ||
        mTracks[trackIndex].engineType == 6) {
      for (int i = 0; i < 16; ++i) {
        mTracks[trackIndex].drumSequencers[i].setConfiguration(numPages,
                                                               stepsPerPage);
      }
    }
  }
}

void AudioEngine::setTempo(float bpm) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TEMPO;
  cmd.value = bpm;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
  for (int i = 0; i < 6; ++i) {
    mLfos[i].setBpm(bpm);
  }
}

void AudioEngine::setPlaying(bool playing) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  
  // SANITY CHECK: Never allow playing if BPM is invalid
  if (playing && (!std::isfinite(mBpm) || mBpm < 1.0f)) {
      mBpm = 80.0f;
  }

  mIsPlaying = playing;
  if (!playing) {
    mSampleCount = 0;
    mGlobalStepIndex = 0;
    // Panic Logic (Unlocked copy) to stop CPU usage immediately
    for (auto &track : mTracks) {
      track.mInternalStepIndex = 0;
      track.mStepCountdown = 0.0;
      track.mPendingNotes.clear();

      track.sequencer.jumpToStep(0);
      if (track.engineType == 5 || track.engineType == 6 ||
          (track.engineType == 2 && track.samplerEngine.getPlayMode() >= 3)) {
        for (int d = 0; d < 16; ++d)
          track.drumSequencers[d].jumpToStep(0);
      }
      track.isActive = false;
      track.mPendingNotes.clear();

      // Panic: Force silence
      track.subtractiveEngine.allNotesOff();
      track.fmEngine.allNotesOff();
      track.samplerEngine.allNotesOff();
      track.fmDrumEngine.allNotesOff();
      track.granularEngine.allNotesOff();
      track.wavetableEngine.allNotesOff();
      track.analogDrumEngine.allNotesOff();
      track.soundFontEngine.allNotesOff();
    }
  } else {
    // FIX: reset startup frames so we don't skip the first step on play
    mStartupFrames = 0;

    for (auto &track : mTracks) {
      if (!std::isfinite(mBpm) || mBpm < 1.0f) mBpm = 80.0f;
      track.mInternalStepIndex = 0;
      track.mStepCountdown = 0.0;
      track.mPendingNotes.clear();

      // FIX: Reset Sequencers to start from the beginning
      track.sequencer.reset();
      // Reset Drum Sequencers too
      if (track.engineType == 5 || track.engineType == 6 ||
          (track.engineType == 2 && track.samplerEngine.getPlayMode() >= 3)) {
        for (int d = 0; d < 16; ++d) {
          track.drumSequencers[d].reset();
        }
      }
    }
    // Clear Global FX Buffers on Start to prevent noise burst
    mDelayFx.clear();
    mLpLfoL.setDepth(0.0f);
    mLpLfoR.setDepth(0.0f);
    mHpLfoL.setDepth(0.0f);
    mHpLfoR.setDepth(0.0f);
    mLpLfoL.setCutoff(1.0f);
    mLpLfoR.setCutoff(1.0f);
    mHpLfoL.setCutoff(0.0f);
    mHpLfoR.setCutoff(0.0f);
    mReverbFx.clear();
    mTapeWobbleFx.clear();
    mPhaserFxL.clear();
    mPhaserFxR.clear();
    mChorusFxL.clear();
    mChorusFxR.clear();
    mFlangerFxL.clear();
    mFlangerFxR.clear();
    for (int i = 0; i < 3; ++i) {
      mFilterPedalL[i].clear();
      mFilterPedalR[i].clear();
    }
    // Prevent Silence for Filter Pedals (Default Mix to 1.0 if not touched)
    mFxMixLevels[12] = 1.0f; // Filter 1
    mFxMixLevels[15] = 1.0f; // Filter 2
    mFxMixLevels[16] = 1.0f; // Filter 3

    mHpLfoL.reset(mSampleRate);
    mHpLfoR.reset(mSampleRate);
    mLpLfoL.reset(mSampleRate);
    mLpLfoR.reset(mSampleRate);
  }
}

void AudioEngine::setClockMultiplier(int trackIndex, float multiplier) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    mTracks[trackIndex].mClockMultiplier = multiplier;
  }
}

void AudioEngine::setSwing(float swing) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_SWING;
  cmd.value = swing;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setTrackHumanize(int trackIndex, float amount) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_HUMANIZE;
  cmd.trackIndex = trackIndex;
  cmd.value = amount;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setPatternLength(int trackIndex, int length) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_PATTERN_LENGTH;
  cmd.trackIndex = trackIndex;
  cmd.data1 = length;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setPlaybackDirection(int trackIndex, int direction) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].sequencer.setPlaybackDirection(direction);
    for (int i = 0; i < 16; ++i) {
      mTracks[trackIndex].drumSequencers[i].setPlaybackDirection(direction);
    }
  }
}

void AudioEngine::setIsRandomOrder(int trackIndex, bool isRandom) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].sequencer.setIsRandomOrder(isRandom);
    for (int i = 0; i < 16; ++i) {
      mTracks[trackIndex].drumSequencers[i].setIsRandomOrder(isRandom);
    }
  }
}

int AudioEngine::getPlaybackDirection(int trackIndex) const {
  std::lock_guard<std::recursive_mutex> lock(const_cast<std::recursive_mutex&>(mLock));
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    return mTracks[trackIndex].sequencer.getPlaybackDirection();
  }
  return 0;
}

bool AudioEngine::getIsRandomOrder(int trackIndex) const {
  std::lock_guard<std::recursive_mutex> lock(const_cast<std::recursive_mutex&>(mLock));
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    return mTracks[trackIndex].sequencer.getIsRandomOrder();
  }
  return false;
}

void AudioEngine::setIsJumpMode(int trackIndex, bool isJump) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    mTracks[trackIndex].sequencer.setIsJumpMode(isJump);
    for (int i = 0; i < 16; ++i) {
      mTracks[trackIndex].drumSequencers[i].setIsJumpMode(isJump);
    }
  }
}

void AudioEngine::setSelectedFmDrumInstrument(int trackIndex, int drumIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    mTracks[trackIndex].selectedFmDrumInstrument = drumIndex % 16;
  }
}

void AudioEngine::setParameterLock(int trackIndex, int stepIndex,
                                   int parameterId, float value) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].sequencer.setParameterLock(stepIndex, parameterId,
                                                   value);
  }
}

void AudioEngine::clearParameterLocks(int trackIndex, int stepIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].sequencer.clearParameterLocks(stepIndex);
  }
}

void AudioEngine::setRouting(int destTrack, int sourceTrack, int source,
                             int dest, float amount, int destParamId) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (source == 27) {
    source = 18; // Map UI Aftertouch (27) to ModSource::Aftertouch (18)
  }
  RoutingEntry entry = {sourceTrack, static_cast<ModSource>(source),
                        static_cast<ModDestination>(dest), destParamId, amount};
  mRoutingMatrix.addConnection(destTrack, entry);
}

void AudioEngine::applyModulations() {
  // Quick Win 2: Skip entirely if no modulation routes exist for any track
  bool anyRoutes = false;
  for (int t = 0; t < (int)mTracks.size(); ++t) {
    const RoutingEntry *mods;
    int count;
    mRoutingMatrix.getFastConnections(t, &mods, &count);
    if (count > 0) { anyRoutes = true; break; }
  }
  if (!anyRoutes) return;

  for (int t = 0; t < (int)mTracks.size(); ++t) {
    auto &track = mTracks[t];
    std::bitset<2500> currentModulated;

    const RoutingEntry *mods;
    int count;
    mRoutingMatrix.getFastConnections(t, &mods, &count);

    for (int i = 0; i < count; ++i) {
      const auto &mod = mods[i];
      float srcValue = 0.0f;

      // Get Source Value
      switch (mod.source) {
      case ModSource::LFO1:
        srcValue = mLfos[0].getCurrentValue();
        break;
      case ModSource::LFO2:
        srcValue = mLfos[1].getCurrentValue();
        break;
      case ModSource::LFO3:
        srcValue = mLfos[2].getCurrentValue();
        break;
      case ModSource::LFO4:
        srcValue = mLfos[3].getCurrentValue();
        break;
      case ModSource::LFO5:
        srcValue = mLfos[4].getCurrentValue();
        break;
      case ModSource::LFO6:
        srcValue = mLfos[5].getCurrentValue();
        break;
      case ModSource::Macro1:
        srcValue = mMacros[0].value;
        break;
      case ModSource::Macro2:
        srcValue = mMacros[1].value;
        break;
      case ModSource::Macro3:
        srcValue = mMacros[2].value;
        break;
      case ModSource::Macro4:
        srcValue = mMacros[3].value;
        break;
      case ModSource::Macro5:
        srcValue = mMacros[4].value;
        break;
      case ModSource::Macro6:
        srcValue = mMacros[5].value;
        break;
      case ModSource::Macro7:
        srcValue = mMacros[6].value;
        break;
      case ModSource::Macro8:
        srcValue = mMacros[7].value;
        break;
      case ModSource::Aftertouch:
        srcValue = track.padModValue;
        break;
      default:
        break;
      }

      if (!std::isfinite(srcValue))
        srcValue = 0.0f;

      // Apply to Destination
      if (mod.destination == ModDestination::Parameter &&
          mod.destParamId >= 0 && mod.destParamId < 2500) {
        float baseVal = track.parameters[mod.destParamId];
        float effectiveVal = baseVal + (srcValue * mod.amount);
        track.appliedParameters[mod.destParamId] = effectiveVal;
        currentModulated.set(mod.destParamId);

        if (std::isfinite(effectiveVal)) {
          updateEngineParameter(t, mod.destParamId, effectiveVal);
        }
      } else if (mod.destination == ModDestination::FilterCutoff) {
        float baseVal = track.parameters[112];
        float effectiveVal = baseVal + (srcValue * mod.amount);
        track.appliedParameters[112] = effectiveVal;
        currentModulated.set(112);
        if (std::isfinite(effectiveVal)) {
          updateEngineParameter(t, 112, effectiveVal);
        }
      } else if (mod.destination == ModDestination::Pitch) {
        float baseVal = track.mPitchBend;
        float effectiveVal = baseVal + (srcValue * mod.amount * 12.0f);
        if (std::isfinite(effectiveVal)) {
          track.mPitchBend = effectiveVal;
          track.subtractiveEngine.setPitchBend(effectiveVal);
          track.fmEngine.setPitchBend(effectiveVal);
          track.wavetableEngine.setPitchBend(effectiveVal);
          track.samplerEngine.setPitchBend(effectiveVal);
          track.granularEngine.setPitchBend(effectiveVal);
        }
      } else if (mod.destination == ModDestination::Volume) {
        float baseVal = track.volume;
        float effectiveVal = baseVal + (srcValue * mod.amount);
        if (effectiveVal < 0.0f) effectiveVal = 0.0f;
        if (effectiveVal > 1.5f) effectiveVal = 1.5f;
        if (std::isfinite(effectiveVal)) {
          track.volume = effectiveVal;
        }
      } else if (mod.destination == ModDestination::WavetablePos) {
        float baseVal = track.parameters[310];
        float effectiveVal = baseVal + (srcValue * mod.amount);
        track.appliedParameters[310] = effectiveVal;
        currentModulated.set(310);
        if (std::isfinite(effectiveVal)) {
          updateEngineParameter(t, 310, effectiveVal);
          track.wavetableEngine.setParameter(0, effectiveVal);
        }
      }
    }

    // Reset parameters that are no longer modulated
    if (track.mModulatedParams.any()) {
      for (int id = 0; id < 2500; ++id) {
        if (track.mModulatedParams.test(id) && !currentModulated.test(id)) {
          float baseVal = track.parameters[id];
          track.appliedParameters[id] = baseVal;
          updateEngineParameter(t, id, baseVal);
        }
      }
    }
    track.mModulatedParams = currentModulated;
  }
}

void AudioEngine::setIsRecording(bool isRecording) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mIsRecording = isRecording;
}

void AudioEngine::jumpToStep(int stepIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mGlobalStepIndex = stepIndex % mPatternLength;

  for (auto &track : mTracks) {
    track.mStepCountdown = 0.0; // Force immediate trigger on next block
    track.sequencer.jumpToStep(mGlobalStepIndex);
    for (int i = 0; i < 16; ++i) {
      track.drumSequencers[i].jumpToStep(mGlobalStepIndex);
    }
  }
  mSampleCount = mSamplesPerStep;
}

int AudioEngine::getCurrentStep(int trackIndex, int drumIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    if (drumIndex >= 0 && drumIndex < 16) {
      return mTracks[trackIndex]
          .drumSequencers[drumIndex]
          .getCurrentStepIndex();
    }
    return mTracks[trackIndex].sequencer.getCurrentStepIndex();
  }
  return 0;
}

void AudioEngine::setArpConfig(int trackIndex, int mode, int octaves,
                               int inversion, bool isLatched, bool isMutated,
                               const std::vector<std::vector<bool>> &rhythms,
                               const std::vector<int> &sequence,
                               const std::vector<float> &gateLengths,
                               float probability, float weird) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    ArpMode newMode = static_cast<ArpMode>(mode);
    Track &track = mTracks[trackIndex];

    track.arpeggiator.setProbability(probability);
    track.arpeggiator.setWeird(weird);

    bool wasLatched = track.arpeggiator.isLatched();
    if (wasLatched && !isLatched && track.mPhysicallyHeldNoteCount == 0) {
      track.arpeggiator.clear();
      for (int i = 0; i < AudioEngine::Track::MAX_POLYPHONY; ++i) {
        if (track.mActiveNotes[i].active) {
          track.subtractiveEngine.releaseNote(track.mActiveNotes[i].note);
          track.fmEngine.releaseNote(track.mActiveNotes[i].note);
          track.samplerEngine.releaseNote(track.mActiveNotes[i].note);
          track.fmDrumEngine.releaseNote(track.mActiveNotes[i].note);
          track.granularEngine.releaseNote(track.mActiveNotes[i].note);
          track.wavetableEngine.releaseNote(track.mActiveNotes[i].note);
          track.mActiveNotes[i].active = false;
        }
      }
    }

    if (newMode == ArpMode::OFF) {
      track.arpeggiator.clear();
    }
    track.arpeggiator.setMode(newMode);
    track.arpeggiator.setOctaves(octaves);
    track.arpeggiator.setInversion(inversion);
    track.arpeggiator.setLatched(isLatched);
    track.arpeggiator.setIsMutated(isMutated);
    track.arpeggiator.setRhythm(rhythms);
    track.arpeggiator.setRandomSequence(sequence);
    track.arpeggiator.setGateLengths(gateLengths);
  }
}

void AudioEngine::setChordProgConfig(int trackIndex, bool enabled, int mood,
                                     int complexity) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].arpeggiator.setChordProgConfig(enabled, mood,
                                                       complexity);
  }
}

void AudioEngine::setScaleConfig(int rootNote,
                                 const std::vector<int> &intervals) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  for (auto &track : mTracks) {
    track.arpeggiator.setScaleConfig(rootNote, intervals);
  }
}

int AudioEngine::getScaleRoot() const {
  std::lock_guard<std::recursive_mutex> lock(const_cast<std::recursive_mutex&>(mLock));
  if (!mTracks.empty()) {
    return mTracks[0].arpeggiator.getScaleRoot();
  }
  return 0;
}

void AudioEngine::setSidechainConfig(int trackIndex, int drumIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mSidechainSourceTrack = trackIndex;
  mSidechainSourceDrumIdx = drumIndex;
}

void AudioEngine::getGranularPlayheads(int trackIndex,
                                       GranularEngine::PlayheadInfo *out,
                                       int maxCount) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    // ENUM ID: SUB=0, FM=1, SAMP=2, GRAN=3, WAVE=4
    if (mTracks[trackIndex].engineType == 3) { // GRANULAR
      mTracks[trackIndex].granularEngine.getPlayheads(out, maxCount);
    } else if (mTracks[trackIndex].engineType == 2) { // SAMPLER
      // Reuse PlayheadInfo struct but cast or map if they aren't
      // bit-identical In our case they are both {float, float}
      mTracks[trackIndex].samplerEngine.getPlayheads(
          (SamplerEngine::PlayheadInfo *)out, maxCount);
    }
  }
}

void AudioEngine::normalizeSample(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].samplerEngine.normalize();
  }
}

void AudioEngine::saveSample(int trackIndex, const std::string &path) {
  if (trackIndex < 0 || trackIndex >= mTracks.size())
    return;
  auto &track = mTracks[trackIndex];
  // Currently only support SamplerEngine saving
  if (track.engineType == 2) { // Sampler
    std::vector<float> data = track.samplerEngine.getSampleData();
    std::vector<float> slices = track.samplerEngine.getSlicePoints();
    WavFileUtils::writeWav(path, data, 48000, 1, slices);
  } else if (track.engineType == 3) { // Granular (3)
    std::vector<float> data = track.granularEngine.getSampleData();
    std::vector<float> slices;
    WavFileUtils::writeWav(path, data, 48000, 1, slices);
  }
}

void AudioEngine::loadSample(int trackIndex, const std::string &path) {
  if (trackIndex < 0 || trackIndex >= mTracks.size())
    return;
  auto &track = mTracks[trackIndex];
  // Load using WavFileUtils
  std::vector<float> data;
  int sampleRate;
  int channels;
  std::vector<float> slicePoints;
  if (!WavFileUtils::loadWav(path, data, sampleRate, channels, slicePoints)) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (track.engineType == 2) { // Sampler
    track.samplerEngine.loadSample(data);
    if (!slicePoints.empty()) {
      track.samplerEngine.setSlicePoints(slicePoints);
    }
  } else if (track.engineType == 3) { // Granular
    track.granularEngine.setSource(data);
  }
  track.lastSamplePath = path;
}

size_t AudioEngine::getSampleLength(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    if (mTracks[trackIndex].engineType == 2) // Sampler
      return mTracks[trackIndex].samplerEngine.getSampleLength();
    else if (mTracks[trackIndex].engineType == 3) // Granular
      return mTracks[trackIndex].granularEngine.getSampleLength();
  }
  return 0;
}

void AudioEngine::setAppDataDir(const std::string &dir) { mAppDataDir = dir; }

void AudioEngine::saveAppState() {
  if (mAppDataDir.empty())
    return;
  std::string path = mAppDataDir + "/app_state.txt";
  // Write to temp file first to prevent partial writes
  std::string tempPath = path + ".tmp";
  std::ofstream file(tempPath);
  if (file.is_open()) {
    file << "LOOM_STATE_V1\n"; // Magic Header
    for (int i = 0; i < (int)mTracks.size(); ++i) {
      if (!mTracks[i].lastSamplePath.empty()) {
        file << i << ":" << mTracks[i].lastSamplePath << "\n";
      }
    }
    file.close();
    // Atomic rename (mostly atomic on POSIX)
    rename(tempPath.c_str(), path.c_str());
  }
}

void AudioEngine::loadAppState() {
  if (mAppDataDir.empty())
    return;
  std::string path = mAppDataDir + "/app_state.txt";
  std::ifstream file(path);
  if (file.is_open()) {
    std::string line;
    // Check Header
    if (std::getline(file, line)) {
      if (line != "LOOM_STATE_V1") {
        LOGD("Invalid State Header. resetting state.");
        file.close();
        return; // Discard invalid state
      }
    }

    while (std::getline(file, line)) {
      try {
        size_t pos = line.find(':');
        if (pos != std::string::npos) {
          int trackIndex = std::stoi(line.substr(0, pos));
          std::string samplePath = line.substr(pos + 1);
          if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
            // Verify file exists before loading to prevent crash loops
            FILE *f = fopen(samplePath.c_str(), "rb");
            if (f) {
              fclose(f);
              loadSample(trackIndex, samplePath);
            } else {
              LOGD("Skipping missing file in state: %s", samplePath.c_str());
            }
          }
        }
      } catch (...) {
        LOGD("Error parsing state line: %s", line.c_str());
      }
    }
    file.close();
  }
}

std::string AudioEngine::getLastSamplePath(int trackIndex) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    return mTracks[trackIndex].lastSamplePath;
  }
  return "";
}

void AudioEngine::trimSample(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    if (mTracks[trackIndex].engineType == 2) {
      mTracks[trackIndex].samplerEngine.trim();
      // Reset markers to full range after destructive trim
      mTracks[trackIndex].parameters[330] = 0.0f;
      mTracks[trackIndex].parameters[331] = 1.0f;
    } else if (mTracks[trackIndex].engineType == 3) { // Granular
      float start = mTracks[trackIndex].parameters[330];
      float end = mTracks[trackIndex].parameters[331];
      mTracks[trackIndex].granularEngine.trim(start, end);
      mTracks[trackIndex].parameters[330] = 0.0f;
      mTracks[trackIndex].parameters[331] = 1.0f;
    }
  }
}

std::vector<float> AudioEngine::getRecordedSampleData(int trackIndex,
                                                      float targetSampleRate) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex < 0 || trackIndex >= mTracks.size())
    return {};

  auto &track = mTracks[trackIndex];
  const std::vector<float> *source = nullptr;

  if (track.engineType == 2) {
    source = &track.samplerEngine.getSampleData();
  } else if (track.engineType == 3) {
    source = &track.granularEngine.getSampleData();
  }

  if (!source || source->empty())
    return {};

  float sourceRate = static_cast<float>(mSampleRate);
  if (sourceRate <= 0)
    sourceRate = 48000.0f;

  if (std::abs(sourceRate - targetSampleRate) < 1.0f) {
    return *source; // No resampling needed
  }

  double ratio = static_cast<double>(sourceRate) / targetSampleRate;
  size_t targetSize = static_cast<size_t>(source->size() / ratio);
  std::vector<float> result(targetSize);

  for (size_t i = 0; i < targetSize; ++i) {
    double pos = i * ratio;
    int idx = static_cast<int>(pos);
    float frac = static_cast<float>(pos - idx);

    // Get 4 points for cubic interpolation
    float y0 = source->at(std::max(0, idx - 1));
    float y1 = source->at(idx);
    float y2 = source->at(std::min((int)source->size() - 1, idx + 1));
    float y3 = source->at(std::min((int)source->size() - 1, idx + 2));

    result[i] = cubicInterpolation(y0, y1, y2, y3, frac);
  }

  return result;
}

void AudioEngine::startRecordingSample(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mIsRecordingSample = true;
    mIsRecordingLocked = false; // Reset lock on new recording start
    mRecordingTrackIndex = trackIndex;
    if (mTracks[trackIndex].engineType == 2)
      mTracks[trackIndex].samplerEngine.clearBuffer();
    else if (mTracks[trackIndex].engineType == 3) // Granular
      mTracks[trackIndex].granularEngine.clearSource();
  }
}

void AudioEngine::stopRecordingSample(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (!mIsRecordingLocked) {
    // Commit recorded samples to active buffer (lock-free swap)
    if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
      if (mTracks[trackIndex].engineType == 2) { // Sampler
        mTracks[trackIndex].samplerEngine.commitRecording();
        mTracks[trackIndex].samplerEngine.normalize();
      } else if (mTracks[trackIndex].engineType == 3) { // Granular
        mTracks[trackIndex].granularEngine.commitRecording();
        mTracks[trackIndex].granularEngine.normalize();
      }
    }
    mIsRecordingSample = false;
    mRecordingTrackIndex = -1;
  }
}

void AudioEngine::setRecordingLocked(bool locked) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mIsRecordingLocked = locked;
}

std::vector<float> AudioEngine::getSamplerWaveform(int trackIndex,
                                                   int numPoints) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    if (mTracks[trackIndex].engineType == 2)
      return mTracks[trackIndex].samplerEngine.getAmplitudeWaveform(numPoints);
    else if (mTracks[trackIndex].engineType == 3)
      return mTracks[trackIndex].granularEngine.getAmplitudeWaveform(numPoints);
  }
  return {};
}

bool AudioEngine::getStepActive(int trackIndex, int stepIndex, int drumIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    if (drumIndex >= 0 && drumIndex < 16) {
      const auto &steps =
          mTracks[trackIndex].drumSequencers[drumIndex].getSteps();
      if (stepIndex >= 0 && stepIndex < (int)steps.size()) {
        return steps[stepIndex].active;
      }
    } else {
      const auto &steps = mTracks[trackIndex].sequencer.getSteps();
      if (stepIndex >= 0 && stepIndex < (int)steps.size()) {
        return steps[stepIndex].active;
      }
    }
  }
  return false;
}

void AudioEngine::resetSampler(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].samplerEngine.clearBuffer();
  }
}

std::vector<float> AudioEngine::getSamplerSlicePoints(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    if (mTracks[trackIndex].engineType == 2)
      return mTracks[trackIndex].samplerEngine.getSlicePoints();
  }
  return {};
}

void AudioEngine::setSoundFontMapping(int trackIndex, int knobIndex,
                                      int paramId) {
  if (trackIndex >= 0 && trackIndex < 8) {
    mTracks[trackIndex].soundFontEngine.setMapping(knobIndex, paramId);
  }
}

void AudioEngine::clearSequencer(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    mTracks[trackIndex].sequencer.clear();
    for (int i = 0; i < 16; ++i) {
      mTracks[trackIndex].drumSequencers[i].clear();
    }
  }
}

void AudioEngine::setMasterVolume(float volume) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mMasterVolume = volume * 1.5f; // 50% boost at max
  for (auto &track : mTracks) {
    track.mSilenceFrames = 0;
  }
}

void AudioEngine::panic() {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  for (auto &track : mTracks) {
    // Release all notes in the engine
    track.subtractiveEngine.allNotesOff();
    track.fmEngine.allNotesOff();
    track.fmDrumEngine.allNotesOff();
    track.analogDrumEngine.allNotesOff();
    track.wavetableEngine.allNotesOff();
    track.samplerEngine.allNotesOff();
    track.granularEngine.allNotesOff();
    track.soundFontEngine.allNotesOff();

    for (int v = 0; v < Track::MAX_POLYPHONY; ++v) {
      track.mActiveNotes[v].active = false;
    }
  }
}

uint64_t AudioEngine::getActiveNoteMask(int trackIndex) {
  if (trackIndex < 0 || trackIndex >= (int)mTracks.size())
    return 0;
  
  std::lock_guard<std::recursive_mutex> lock(mLock);
  uint64_t mask = 0;
  auto &track = mTracks[trackIndex];
  
  // High bits (62-63) reserved for "Any Voice Active" flag
  bool anyVoiceActive = false;
  
  // Check engine voices for ANY activity (including release)
  if (track.subtractiveEngine.isActive()) anyVoiceActive = true;
  else if (track.fmEngine.isActive()) anyVoiceActive = true;
  else if (track.wavetableEngine.isActive()) anyVoiceActive = true;
  else if (track.samplerEngine.isActive()) anyVoiceActive = true;
  else if (track.granularEngine.isActive()) anyVoiceActive = true;
  else if (track.analogDrumEngine.isActive()) anyVoiceActive = true;
  else if (track.fmDrumEngine.isActive()) anyVoiceActive = true;
  else if (track.soundFontEngine.hasActiveVoices()) anyVoiceActive = true;

  if (anyVoiceActive) {
      mask |= (1ULL << 63); // Bit 63 for "Stuck/Sustaining Voice" identification
  }

  // Engine types that ignore transpose (Drums, Slicers)
  bool ignoresTranspose = (track.engineType == 5 || track.engineType == 6);
  if (track.engineType == 2 && track.parameters[320] > 1.5f) {
    ignoresTranspose = true;
  }

  for (int v = 0; v < Track::MAX_POLYPHONY; ++v) {
    if (track.mActiveNotes[v].active) {
      int note = track.mActiveNotes[v].note;
      
      // Reverse the transpose to get the "physical" note for UI highlighting
      int displayNote = ignoresTranspose ? note : (note - track.transpose);

      if (displayNote >= 48 && displayNote < 111) { // Up to bit 62
        mask |= (1ULL << (displayNote - 48));
      }
    }
  }
  
  return mask;
}

// ... COPY OF OTHER METHODS ...
// (Omitting small getters for brevity, but they follow the pattern)
// setGenericLfoParam, setMacroValue, setFxChain etc.

void AudioEngine::setGenericLfoParam(int lfoIndex, int paramId, float value) {
  if (lfoIndex < 0 || lfoIndex >= 6)
    return;
  std::lock_guard<std::recursive_mutex> lock(mLock);
  switch (paramId) {
  case 0:
    // Cubic scaling: 0.01Hz to 30Hz
    // Range = 30 - 0.01 = 29.99
    // Val = 0.01 + (v^3 * 29.99)
    mLfos[lfoIndex].setFrequency(0.01f * powf(10.0f, value * 3.47712f));
    mLfos[lfoIndex].setUiRate(value);
    break;
  case 1:
    mLfos[lfoIndex].setDepth(value);
    break;
  case 2:
    mLfos[lfoIndex].setShape((int)value);
    break;
  case 3:
    mLfos[lfoIndex].setSync(value > 0.5f);
    break;
  }
}

void AudioEngine::setMacroValue(int macroIndex, float value) {
  if (macroIndex < 0 || macroIndex >= 8)
    return;
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mMacros[macroIndex].value = value;
}

void AudioEngine::setMacroSource(int macroIndex, int sourceType,
                                 int sourceIndex, int sourceTrackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (macroIndex >= 0 && macroIndex < 8) {
    mMacros[macroIndex].sourceType = sourceType;
    mMacros[macroIndex].sourceIndex = sourceIndex;
    mMacros[macroIndex].sourceTrackIndex = sourceTrackIndex;
  }
}

void AudioEngine::rebuildFxActivityMask() {
  // Called whenever fxSends or chain routing changes. No lock needed — caller holds mLock or
  // is in a UI-thread path before audio starts. Mask is read lock-free in renderStereo since
  // worst case of a stale bit is processing a silent FX bus for one buffer (harmless).
  uint32_t mask = 0;
  for (const auto &track : mTracks) {
    for (int f = 0; f < 18; ++f) {
      if (track.fxSends[f] > 0.001f)
        mask |= (1u << f);
    }
  }
  // Also activate any FX that is the destination of a chain (it must process
  // to pass the signal through even if no track sends directly to it)
  for (int f = 0; f < 18; ++f) {
    if (mFxChainDest[f] >= 0 && mFxChainDest[f] < 18) {
      // If the source FX is active, the dest must be too
      if (mask & (1u << f))
        mask |= (1u << mFxChainDest[f]);
    }
  }
  // Delay (5) and Reverb (6) have tails — keep active while !isSilent()
  if (!mDelayFx.isSilent())  mask |= (1u << 5);
  if (!mReverbFx.isSilent()) mask |= (1u << 6);
  if (!mTapeEchoFxL.isSilent() || !mTapeEchoFxR.isSilent()) mask |= (1u << 13);
  mActiveFxSlotMask = mask;
}

void AudioEngine::setFxChain(int sourceFx, int destFx) {
  if (sourceFx < 0 || sourceFx >= 17)
    return;
  if (destFx < -1 || destFx >= 17)
    return;
  std::lock_guard<std::recursive_mutex> lock(mLock);
  mFxChainDest[sourceFx] = destFx;
  rebuildFxActivityMask(); // Chain change may activate a previously-idle dest slot
}



void AudioEngine::setFilterMode(int trackIndex, int mode) {
  if (trackIndex >= 0 && trackIndex < 8) {
    if (mTracks[trackIndex].engineType == 0) { // Subtractive
      mTracks[trackIndex].subtractiveEngine.setFilterMode(mode);
    }
  }
}

void AudioEngine::setArpTriplet(int trackIndex, bool isTriplet) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    mTracks[trackIndex].mArpTriplet = isTriplet;
  }
}

float AudioEngine::getCpuLoad() { return mCpuLoad.load(); }

void AudioEngine::enqueuePatternSaveEvent(int trackIndex, int slotIndex) {
  std::lock_guard<std::mutex> lock(mEventLock);
  EngineEvent ev;
  ev.type = EngineEvent::PATTERN_SAVE;
  ev.trackIndex = trackIndex;
  ev.data = slotIndex;
  mEventQueue.push_back(ev);
}

int AudioEngine::fetchEngineEvents(int *outBuffer, int maxEvents) {
  std::lock_guard<std::mutex> lock(mEventLock);
  int count = 0;
  while (!mEventQueue.empty() && count < maxEvents) {
    auto ev = mEventQueue.front();
    mEventQueue.erase(mEventQueue.begin());
    outBuffer[count * 3 + 0] = (int)ev.type;
    outBuffer[count * 3 + 1] = ev.trackIndex;
    outBuffer[count * 3 + 2] = ev.data;
    count++;
  }
  return count;
}

void AudioEngine::enqueueMidiEvent(int type, int channel, int data1,
                                   int data2) {
  std::lock_guard<std::mutex> lock(mMidiLock);
  MidiMessage msg;
  msg.type = type;
  msg.channel = channel;
  msg.data1 = data1;
  msg.data2 = data2;
  mMidiQueue.push_back(msg);
}

void AudioEngine::restorePresets() {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  for (auto &track : mTracks) {
    track.volume = 0.25f;
    track.smoothedVolume = 0.25f;
    track.subtractiveEngine.resetToDefaults();
    track.fmEngine.resetToDefaults();
    track.fmDrumEngine.resetToDefaults();
    track.analogDrumEngine.resetToDefaults();
    track.samplerEngine.resetToDefaults();
    track.granularEngine.resetToDefaults();
    track.wavetableEngine.resetToDefaults();

    // CRITICAL: Also clear the parameter buffers so UI and Engine stay in
    // sync We set meaningful defaults so the UI knobs show the correct
    // initial values
    std::fill(std::begin(track.parameters), std::end(track.parameters), 0.0f);
    std::fill(std::begin(track.appliedParameters),
              std::end(track.appliedParameters), 0.0f);

    // Common EG Defaults
    track.parameters[100] = 0.01f; // Attack
    track.parameters[101] = 0.1f;  // Decay
    track.parameters[102] = 0.8f;  // Sustain
    track.parameters[103] = 0.5f;  // Release

    // Common Filter Defaults
    track.parameters[112] = 0.85f; // Cutoff (Reasonably Open)
    track.parameters[113] = 0.0f;  // Resonance
    track.parameters[151] = 0.85f; // FM Cutoff (Open)
    track.parameters[303] = 0.85f; // Sampler/Granular Cutoff (Open)
    track.parameters[458] = 0.85f; // Wavetable Cutoff (Open)

    // FM Specific Defaults
    track.parameters[150] = 0.0f;  // Algorithm 0
    track.parameters[153] = 1.0f;  // Carrier Mask (Op 1)
    track.parameters[155] = 63.0f; // Active Mask (All 6 Ops)
    track.parameters[157] = 0.5f;  // Brightness (1.0 in engine)

    // Sampler Defaults
    track.parameters[302] = 0.4627f; // Speed 1.0x (non-linear)
    track.parameters[305] = 0.0f;    // Filter Type: LowPass
    track.parameters[320] = 0.0f; // OneShot mode
    track.parameters[340] = 0.0f; // 2 slices

    // Granular Defaults
    track.parameters[400] = 0.5f;  // Position
    track.parameters[401] = 1.0f;  // Speed
    track.parameters[406] = 0.2f;  // Grain Size
    track.parameters[407] = 0.5f;  // Density
    track.parameters[415] = 0.0f;  // Spray
    track.parameters[416] = 0.0f;  // Detune
    track.parameters[417] = 0.0f;  // Random Timing / Jitter
    track.parameters[418] = 0.2f;  // Max Grains
    track.parameters[419] = 0.5f;  // Width
    track.parameters[420] = 0.0f;  // Reverse Prob
    track.parameters[425] = 0.01f; // Attack
    track.parameters[426] = 0.1f;  // Decay
    track.parameters[427] = 1.0f;  // Sustain
    track.parameters[428] = 0.2f;  // Release
    track.parameters[429] = 0.4f;  // Master Gain
    track.parameters[430] = 1.0f;  // Spawn Probability
    track.parameters[355] = 0.0f;  // Glide
    track.parameters[408] = 0.5f;  // Grain Env A
    track.parameters[409] = 0.5f;  // Grain Env D
    track.parameters[410] = 1.0f;  // Pitch

    // Wavetable Defaults
    track.parameters[450] = 0.0f; // Position
    track.parameters[451] = 0.0f; // Morph
  }
}

void AudioEngine::resetMidiPatching() {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  for (auto& track : mTracks) {
    track.midiInChannel = 17; // ALL
    track.midiOutChannel = 0; // NONE
  }
  // Clear routing matrix for all tracks
  for (int i = 0; i < (int)mTracks.size(); ++i) {
    mRoutingMatrix.clearConnections(i);
  }
  // Reset FX chain destinations to default (-1 = Master)
  for (int i = 0; i < 18; ++i) {
    mFxChainDest[i] = -1;
  }
  LOGD("AudioEngine: MIDI and Patching reset to defaults.");
}

void AudioEngine::newProject() {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  restorePresets();
  resetMidiPatching();
  // Clear all sequencer patterns
  for (int i = 0; i < (int)mTracks.size(); ++i) {
    mTracks[i].sequencer.clear();
    for (int d = 0; d < 16; ++d) {
      mTracks[i].drumSequencers[d].clear();
    }
    // Reset chain
    mTracks[i].isChainEnabled = false;
    mTracks[i].currentChainSlot = 0;
    mTracks[i].chainLength = 1;
    mTracks[i].isTrackEnabled = true;
    mTracks[i].isActive = false;
    mTracks[i].mSilenceFrames = 0;
  }
  mBpm = 80.0f;
  mTempo = 80.0f;
  mSwing = 0.0f;
  mMasterVolume = 0.8f;
  mIsPlaying = false;
  mIsRecording = false;
  LOGD("AudioEngine: New project initialized.");
}

void AudioEngine::saveProject(const std::string& path) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  std::ofstream file(path);
  if (!file.is_open()) {
    LOGD("Failed to save project to: %s", path.c_str());
    return;
  }
  file << "LOOM_PROJECT_V1\n";
  file << "BPM:" << mBpm << "\n";
  file << "SWING:" << mSwing << "\n";
  file << "MASTER_VOL:" << mMasterVolume << "\n";
  for (int i = 0; i < (int)mTracks.size(); ++i) {
    auto& t = mTracks[i];
    file << "TRACK:" << i << "\n";
    file << "ENGINE:" << t.engineType << "\n";
    file << "VOL:" << t.volume << "\n";
    file << "PAN:" << t.pan << "\n";
    file << "MIDI_IN:" << t.midiInChannel << "\n";
    file << "MIDI_OUT:" << t.midiOutChannel << "\n";
    file << "TRANSPOSE:" << t.transpose << "\n";
    file << "MUTE:" << (t.isMuted ? 1 : 0) << "\n";
    file << "SOLO:" << (t.isSoloed ? 1 : 0) << "\n";
    file << "ACTIVE:" << (t.isTrackEnabled ? 1 : 0) << "\n";
    if (!t.lastSamplePath.empty()) {
      file << "SAMPLE:" << t.lastSamplePath << "\n";
    }
    // Save key parameters (first 500)
    for (int p = 0; p < 500; ++p) {
      if (std::abs(t.parameters[p]) > 0.0001f) {
        file << "P:" << p << ":" << t.parameters[p] << "\n";
      }
    }
    // Save FX sends
    for (int f = 0; f < 18; ++f) {
      if (std::abs(t.fxSends[f]) > 0.0001f) {
        file << "FX_SEND:" << f << ":" << t.fxSends[f] << "\n";
      }
    }
    // Save sequencer settings and active steps
    file << "SEQ_LEN:" << t.sequencer.getLoopLength() << "\n";
    file << "SEQ_DIR:" << t.sequencer.getPlaybackDirection() << "\n";
    file << "SEQ_RAND:" << (t.sequencer.getIsRandomOrder() ? 1 : 0) << "\n";
    const auto& steps = t.sequencer.getSteps();
    for (int stepIdx = 0; stepIdx < 64; ++stepIdx) {
      const auto& s = steps[stepIdx];
      if (s.active || s.isSkipped || s.ratchet > 1 || s.punch || !s.parameterLocks.empty()) {
        file << "SEQ_STEP:" << stepIdx << ":" << (s.active ? 1 : 0) << ":" << (s.isSkipped ? 1 : 0) << ":" << s.ratchet << ":" << (s.punch ? 1 : 0) << ":" << s.probability << ":" << s.gate << "\n";
        for (const auto& n : s.notes) {
          file << "SEQ_NOTE:" << stepIdx << ":" << n.note << ":" << n.velocity << ":" << n.subStepOffset << "\n";
        }
        for (const auto& l : s.parameterLocks) {
          file << "SEQ_LOCK:" << stepIdx << ":" << l.first << ":" << l.second << "\n";
        }
      }
    }
    file << "END_TRACK\n";
  }
  file.close();
  LOGD("Project saved to: %s", path.c_str());
}

void AudioEngine::loadProject(const std::string& path) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  std::ifstream file(path);
  if (!file.is_open()) {
    LOGD("Failed to load project from: %s", path.c_str());
    return;
  }
  std::string line;
  if (!std::getline(file, line) || line != "LOOM_PROJECT_V1") {
    LOGD("Invalid project file header.");
    file.close();
    return;
  }
  // Reset to clean state first
  newProject();
  int currentTrack = -1;
  while (std::getline(file, line)) {
    try {
      size_t pos = line.find(':');
      if (pos == std::string::npos) continue;
      std::string key = line.substr(0, pos);
      std::string val = line.substr(pos + 1);
      if (key == "BPM") { mBpm = std::stof(val); mTempo = mBpm; }
      else if (key == "SWING") { mSwing = std::stof(val); }
      else if (key == "MASTER_VOL") { mMasterVolume = std::stof(val); }
      else if (key == "TRACK") { currentTrack = std::stoi(val); }
      else if (currentTrack >= 0 && currentTrack < (int)mTracks.size()) {
        auto& t = mTracks[currentTrack];
        if (key == "ENGINE") { t.engineType = std::stoi(val); initTrack(currentTrack); }
        else if (key == "VOL") { t.volume = std::stof(val); }
        else if (key == "PAN") { t.pan = std::stof(val); }
        else if (key == "MIDI_IN") { t.midiInChannel = std::stoi(val); }
        else if (key == "MIDI_OUT") { t.midiOutChannel = std::stoi(val); }
        else if (key == "TRANSPOSE") { t.transpose = std::stoi(val); }
        else if (key == "MUTE") { t.isMuted = std::stoi(val) != 0; }
        else if (key == "SOLO") { t.isSoloed = std::stoi(val) != 0; }
        else if (key == "ACTIVE") {
          t.isTrackEnabled = std::stoi(val) != 0;
          t.isActive = t.isTrackEnabled;
          if (!t.isTrackEnabled) {
            t.mSilenceFrames = 2405;
          }
        }
        else if (key == "SAMPLE") {
          FILE* f = fopen(val.c_str(), "rb");
          if (f) {
            fclose(f);
            if (t.engineType == 9) {
              loadSoundFont(currentTrack, val);
            } else {
              loadSample(currentTrack, val);
            }
          }
        }
        else if (key == "P") {
          size_t pos2 = val.find(':');
          if (pos2 != std::string::npos) {
            int pid = std::stoi(val.substr(0, pos2));
            float pval = std::stof(val.substr(pos2 + 1));
            if (pid >= 0 && pid < 2500) t.parameters[pid] = pval;
          }
        }
        else if (key == "FX_SEND") {
          size_t pos2 = val.find(':');
          if (pos2 != std::string::npos) {
            int fid = std::stoi(val.substr(0, pos2));
            float fval = std::stof(val.substr(pos2 + 1));
            if (fid >= 0 && fid < 18) t.fxSends[fid] = fval;
          }
        }
        else if (key == "SEQ_LEN") { t.sequencer.setLoopLength(std::stoi(val)); }
        else if (key == "SEQ_DIR") { t.sequencer.setPlaybackDirection(std::stoi(val)); }
        else if (key == "SEQ_RAND") { t.sequencer.setIsRandomOrder(std::stoi(val) != 0); }
        else if (key == "SEQ_STEP") {
          std::stringstream ss(val);
          int stepIdx, active, skipped, ratchet, punch;
          float prob, gate;
          char colon;
          ss >> stepIdx >> colon >> active >> colon >> skipped >> colon >> ratchet >> colon >> punch >> colon >> prob >> colon >> gate;
          if (stepIdx >= 0 && stepIdx < 64) {
            auto& s = t.sequencer.getStepsMutable()[stepIdx];
            s.active = active != 0;
            s.isSkipped = skipped != 0;
            s.ratchet = ratchet;
            s.punch = punch != 0;
            s.probability = prob;
            s.gate = gate;
          }
        }
        else if (key == "SEQ_NOTE") {
          std::stringstream ss(val);
          int stepIdx, note;
          float vel, offset;
          char colon;
          ss >> stepIdx >> colon >> note >> colon >> vel >> colon >> offset;
          if (stepIdx >= 0 && stepIdx < 64) {
            auto& s = t.sequencer.getStepsMutable()[stepIdx];
            s.addNote(note, vel, offset);
          }
        }
        else if (key == "SEQ_LOCK") {
          std::stringstream ss(val);
          int stepIdx, paramId;
          float lockVal;
          char colon;
          ss >> stepIdx >> colon >> paramId >> colon >> lockVal;
          if (stepIdx >= 0 && stepIdx < 64) {
            t.sequencer.setParameterLock(stepIdx, paramId, lockVal);
          }
        }
        else if (key == "END_TRACK") { currentTrack = -1; }
      }
    } catch (...) {
      LOGD("Error parsing project line: %s", line.c_str());
    }
  }
  file.close();
  LOGD("Project loaded from: %s", path.c_str());
}

int AudioEngine::fetchMidiEvents(int *outBuffer, int maxEvents) {
  std::lock_guard<std::mutex> lock(mMidiLock);
  int count = 0;
  while (!mMidiQueue.empty() && count < maxEvents) {
    MidiMessage msg = mMidiQueue.front();
    mMidiQueue.erase(mMidiQueue.begin());
    int offset = count * 4;
    outBuffer[offset] = msg.type;
    outBuffer[offset + 1] = msg.channel;
    outBuffer[offset + 2] = msg.data1;
    outBuffer[offset + 3] = msg.data2;
    count++;
  }
  return count;
}

std::vector<Step> AudioEngine::getSequencerSteps(int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    return mTracks[trackIndex].sequencer.getSteps();
  }
  return {};
}

std::vector<Step> AudioEngine::getDrumSequencerSteps(int trackIndex, int drumIndex) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    if (drumIndex >= 0 && drumIndex < 16) {
      return mTracks[trackIndex].drumSequencers[drumIndex].getSteps();
    }
  }
  return {};
}

void AudioEngine::loadFmPreset(int trackIndex, int presetId) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    Track &track = mTracks[trackIndex];
    track.fmEngine.loadPreset(presetId);
    track.activeFmPreset = presetId;

    // Normalize and sync parameter 180
    int totalPresets = 32 + (int)track.fmEngine.mCustomPresets.size();
    if (totalPresets > 1) {
      track.parameters[180] = (float)presetId / (float)(totalPresets - 1);
      track.appliedParameters[180] = track.parameters[180];
    } else {
      track.parameters[180] = 0.0f;
      track.appliedParameters[180] = 0.0f;
    }

    // SYNC: Read back parameters from Engine to Track State for UI
    track.parameters[150] = (float)track.fmEngine.getAlgorithm();
    track.parameters[151] = track.fmEngine.getCutoff();
    track.parameters[152] = track.fmEngine.getResonance();
    track.parameters[153] = (float)track.fmEngine.getCarrierMask();
    track.parameters[154] = track.fmEngine.getFeedback();
    track.parameters[155] = (float)track.fmEngine.getActiveMask();
    track.parameters[156] = (float)track.fmEngine.getFilterMode();
    track.parameters[157] =
        track.fmEngine.getBrightness(); // Getter already normalizes (0.0-1.0)
    track.parameters[158] = track.fmEngine.getDetune();
    track.parameters[159] = track.fmEngine.getFeedbackDrive();

    // Amp Envelope
    track.parameters[100] = track.fmEngine.getAttack();
    track.parameters[101] = track.fmEngine.getDecay();
    track.parameters[102] = track.fmEngine.getSustain();
    track.parameters[103] = track.fmEngine.getRelease();

    // Operators
    for (int op = 0; op < 6; ++op) {
      int base = 160 + (op * 6);
      track.parameters[base + 0] = track.fmEngine.getOpLevel(op);
      track.parameters[base + 1] = track.fmEngine.getOpAttack(op);
      track.parameters[base + 2] = track.fmEngine.getOpDecay(op);
      track.parameters[base + 3] = track.fmEngine.getOpSustain(op);
      track.parameters[base + 4] = track.fmEngine.getOpRelease(op);
      track.parameters[base + 5] = track.fmEngine.getOpRatio(op) / 16.0f;
    }

    // Filter Envelope
    track.parameters[114] = track.fmEngine.getFilterAttack();
    track.parameters[115] = track.fmEngine.getFilterDecay();
    track.parameters[116] = track.fmEngine.getFilterSustain();
    track.parameters[117] = track.fmEngine.getFilterRelease();
    track.parameters[118] = track.fmEngine.getFilterEnvAmount();
  }
}

void AudioEngine::setResampling(bool isResampling) {
  mRecordingSource = isResampling ? RESAMPLE : MIC;
}

void AudioEngine::setRecordingSource(int source) { mRecordingSource = source; }

void AudioEngine::pushSystemAudioSamples(const float *data, int numSamples) {
  if (mRecordingTrackIndex < 0 || mRecordingTrackIndex >= (int)mTracks.size()) {
    return;
  }

  auto &track = mTracks[mRecordingTrackIndex];
  if (track.engineType == 2)
    track.samplerEngine.pushSamples(data, numSamples);
  else if (track.engineType == 3)
    track.granularEngine.pushSamples(data, numSamples);
}

void AudioEngine::renderStereo(float *outBuffer, int numFrames) {
  // Lock handled by onAudioReady caller
  // Master volume and safety
  if (!std::isfinite(mMasterVolume))
    mMasterVolume = 0.5f;

  // Force Filter FX Mix Levels to 1.0 (Passthrough Fix)
  mFxMixLevels[12] = 1.0f;
  mFxMixLevels[15] = 1.0f;
  mFxMixLevels[16] = 1.0f;

  float sampleRate = static_cast<float>(mSampleRate);

  // --- Block Rate Control Updates ---
  for (int l = 0; l < 6; ++l) {
    mLfos[l].process(sampleRate, numFrames);
  }
  for (int m = 0; m < 8; ++m) {
    if (mMacros[m].sourceType == 3) { // LFO
      int lfoIdx = mMacros[m].sourceIndex;
      if (lfoIdx >= 0 && lfoIdx < 6) {
        float val = (mLfos[lfoIdx].getCurrentValue() + 1.0f) * 0.5f;
        mMacros[m].value = std::max(0.0f, std::min(1.0f, val));
      }
    } else if (mMacros[m].sourceType == 4) { // Envelope
      int trackIdx = mMacros[m].sourceTrackIndex;
      if (trackIdx >= 0 && trackIdx < mTracks.size()) {
        float envVal = 0.0f;
        auto &t = mTracks[trackIdx];
        switch (t.engineType) {
        case 0:
          envVal = t.subtractiveEngine.getEnvelopeValue();
          break;
        case 1:
          envVal = t.fmEngine.getEnvelopeValue();
          break;
        case 2: // SAMPLER
          envVal = t.samplerEngine.getEnvelopeValue();
          break;
        case 3: // GRANULAR
          envVal = t.granularEngine.getEnvelopeValue();
          break;
        case 4: // WAVETABLE
          envVal = t.wavetableEngine.getEnvelopeValue();
          break;
        case 5:
          envVal = t.fmDrumEngine.getEnvelopeValue();
          break;
        case 6:
          envVal = t.analogDrumEngine.getEnvelopeValue();
          break;
        case 8: // AUDIO IN
          envVal = t.audioInEngine.getEnvelopeValue();
          break;
        case 9: // SOUNDFONT
          envVal = t.soundFontEngine.getEnvelopeValue();
          break;
        }
        mMacros[m].value = std::max(0.0f, std::min(1.0f, envVal));
      }
    } else if (mMacros[m].sourceType == 5) { // MIDI PADS
      int trackIdx = mMacros[m].sourceTrackIndex;
      if (trackIdx >= 0 && trackIdx < (int)mTracks.size()) {
        mMacros[m].value = mTracks[trackIdx].padModValue;
      }
    } else if (mMacros[m].sourceType == 2) { // UI Parameter / Control
      int paramId = mMacros[m].sourceIndex;
      int trackIdx = mMacros[m].sourceTrackIndex;
      if (trackIdx >= 0 && trackIdx < (int)mTracks.size() && paramId >= 0 && paramId < 2500) {
        mMacros[m].value = mTracks[trackIdx].parameters[paramId];
      }
    }
  }
  applyModulations();
  // ----------------------------------

  bool anySolo = false;
  for (const auto &t : mTracks) {
    if (t.isSoloed) {
      anySolo = true;
      break;
    }
  }

  for (int blockStart = 0; blockStart < numFrames; blockStart += kAudioBlockSize) {
    int framesToDo = std::min(kAudioBlockSize, numFrames - blockStart);

    float inputSampleBlock[kAudioBlockSize];
    uint32_t savedReadPtr = mInputReadPtr;
    for (int i = 0; i < framesToDo; ++i) {
      uint32_t writePos = mInputWritePtr.load();
      int32_t distance = static_cast<int32_t>(writePos - mInputReadPtr);
      if (distance < 128 || distance > 8000) {
        mInputReadPtr = writePos - 2048;
      }
      inputSampleBlock[i] = mInputRingBuffer[mInputReadPtr % 8192];
      mInputReadPtr++;
    }
    mInputReadPtr = savedReadPtr;

    float trackBuffersL[8][kAudioBlockSize] = {{0.0f}};
    float trackBuffersR[8][kAudioBlockSize] = {{0.0f}};

    float trackVolBlock[8][kAudioBlockSize] = {{0.0f}};
    float trackPanLBlock[8][kAudioBlockSize] = {{0.0f}};
    float trackPanRBlock[8][kAudioBlockSize] = {{0.0f}};
    float trackFxSendsBlock[8][18][kAudioBlockSize] = {{{0.0f}}};

    for (int t = 0; t < (int)mTracks.size(); ++t) {
      Track &track = mTracks[t];

      float currentVol = track.smoothedVolume;
      float targetVol = track.volume;
      if (std::abs(targetVol - currentVol) > 0.0001f) {
        for (int i = 0; i < framesToDo; ++i) {
          currentVol += 0.01f * (targetVol - currentVol);
          trackVolBlock[t][i] = currentVol;
        }
        track.smoothedVolume = currentVol;
      } else {
        for (int i = 0; i < framesToDo; ++i) {
          trackVolBlock[t][i] = targetVol;
        }
        track.smoothedVolume = targetVol;
      }

      float currentPan = track.smoothedPan;
      float targetPan = track.pan;
      if (std::abs(targetPan - currentPan) > 0.0001f) {
        for (int i = 0; i < framesToDo; ++i) {
          currentPan += 0.005f * (targetPan - currentPan);
          float angle = currentPan * (float)M_PI * 0.5f;
          trackPanLBlock[t][i] = cosf(angle);
          trackPanRBlock[t][i] = sinf(angle);
        }
        track.smoothedPan = currentPan;
        track.panL = trackPanLBlock[t][framesToDo - 1];
        track.panR = trackPanRBlock[t][framesToDo - 1];
      } else {
        float pL = track.panL;
        float pR = track.panR;
        for (int i = 0; i < framesToDo; ++i) {
          trackPanLBlock[t][i] = pL;
          trackPanRBlock[t][i] = pR;
        }
        track.smoothedPan = targetPan;
      }

      for (int f = 0; f < 18; ++f) {
        float currentSend = track.smoothedFxSends[f];
        float targetSend = track.fxSends[f];
        if (targetSend > 0.0001f || currentSend > 0.0001f) {
          if (std::abs(targetSend - currentSend) > 0.0001f) {
            for (int i = 0; i < framesToDo; ++i) {
              currentSend += 0.01f * (targetSend - currentSend);
              trackFxSendsBlock[t][f][i] = currentSend;
            }
            track.smoothedFxSends[f] = currentSend;
          } else {
            for (int i = 0; i < framesToDo; ++i) {
              trackFxSendsBlock[t][f][i] = targetSend;
            }
            track.smoothedFxSends[f] = targetSend;
          }
        } else {
          for (int i = 0; i < framesToDo; ++i) {
            trackFxSendsBlock[t][f][i] = 0.0f;
          }
          track.smoothedFxSends[f] = 0.0f;
        }
      }

      if (track.engineType == 0) {
        track.subtractiveEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      } else if (track.engineType == 1) {
        track.fmEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      } else if (track.engineType == 3) {
        track.granularEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      } else if (track.engineType == 4) {
        track.wavetableEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      } else if (track.engineType == 5) {
        track.fmDrumEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      } else if (track.engineType == 6) {
        track.analogDrumEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      } else if (track.engineType == 8) {
        track.audioInEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo, inputSampleBlock);
      } else if (track.engineType == 9) {
        track.soundFontEngine.renderBlock(trackBuffersL[t], trackBuffersR[t], framesToDo);
      }
    }

    for (int i = 0; i < framesToDo; ++i) {
      int outIdx = blockStart + i;

    // Safe ring-buffer read with 2048 samples of latency for stability
    uint32_t writePos = mInputWritePtr.load();
    int32_t distance = static_cast<int32_t>(writePos - mInputReadPtr);
    if (distance < 128 || distance > 8000) {
      mInputReadPtr = writePos - 2048; // Resync if definitely out of bounds
    }
    float inputSample = mInputRingBuffer[mInputReadPtr % 8192];
    mInputReadPtr++;

    float mixedSampleL = 0.0f;
    float mixedSampleR = 0.0f;
    float sidechainSignal = 0.0f;
    float fxBusesL[18];
    float fxBusesR[18];
    // Denormal Bias (1e-15f) helps prevent CPU spikes on nearly-silent audio
    const float denormalBias = 1e-15f;

    for (int b = 0; b < 18; ++b) {
      // Load feedback from previous sample (Backward Chaining)
      fxBusesL[b] = mFxFeedbacksL[b] + denormalBias;
      fxBusesR[b] = mFxFeedbacksR[b] + denormalBias;
      // Clear for next accumulation
      mFxFeedbacksL[b] = 0.0f;
      mFxFeedbacksR[b] = 0.0f;
    }

    for (int t = 0; t < (int)mTracks.size(); ++t) {
      Track &track = mTracks[t];
      track.gainReduction = 1.0f; // Reset per frame

      // Option B: Gate check BEFORE any smoothing work — inactive tracks skip
      // 144+ float ops per sample (volume, 18 fxSends, pan smoothing).
      if ((!track.isTrackEnabled || !track.isActive) && track.mSilenceFrames > 2400) {
        track.follower.process(0.0f);
        continue;
      }

      bool shouldMute = track.isMuted;
      if (anySolo && !track.isSoloed) {
        shouldMute = true;
      }

      float rawSampleL = 0.0f, rawSampleR = 0.0f;
      switch (track.engineType) {
      case 0:
      case 1:
      case 3:
      case 4:
      case 5:
      case 6:
      case 8:
      case 9:
        rawSampleL = trackBuffersL[t][i];
        rawSampleR = trackBuffersR[t][i];
        break;
      case 2:
        rawSampleL = rawSampleR =
            track.samplerEngine.render(fxBusesL, fxBusesR);
        break;
      }

      float monoSum = (rawSampleL + rawSampleR) * 0.5f;
      if (!std::isfinite(monoSum)) {
        rawSampleL = rawSampleR = monoSum = 0.0f;
      }

      // Silence Detection (Tightened)
      if (std::abs(monoSum) < 0.0001f) {
        track.mSilenceFrames++;
        if (track.mSilenceFrames > 2400) {
          bool activeVoices = false;
          for (int v = 0; v < Track::MAX_POLYPHONY; ++v) {
            if (track.mActiveNotes[v].active) {
              activeVoices = true;
              break;
            }
          }
          if (track.mPhysicallyHeldNoteCount == 0 && !activeVoices) {
            track.isActive = false;
            track.mSilenceFrames = 0;
          }
        }
      } else {
        track.mSilenceFrames = 0;
      }

      float finalVol = trackVolBlock[t][i] * track.gainReduction;

      // Pre-Fader Signal calculation (applies Gain Reduction and Punch, but
      // NOT Track Volume)
      float preFaderL = rawSampleL;
      float preFaderR = rawSampleR;

      if (track.mPunchCounter > 0) {
        float punchScale = 1.3f; // Reduced from 2.5f for smoother transients
        preFaderL *= punchScale;
        preFaderR *= punchScale;
        preFaderL = fast_tanh(preFaderL);
        preFaderR = fast_tanh(preFaderR);

        // Apply 1.1x makeup gain (Reduced from 1.25x)
        float makeupGain = 1.1f;
        preFaderL *= makeupGain;
        preFaderR *= makeupGain;

        track.mPunchCounter--;
        rawSampleL = preFaderL;
        rawSampleR = preFaderR;
      }

      preFaderL *= track.gainReduction;
      preFaderR *= track.gainReduction;

      float trackDryKill = 0.0f;
      bool skipTrackSends =
          (track.engineType == 2 && track.samplerEngine.isSliceLockEnabled());

      for (int f = 0; f < 18; ++f) {
        if (trackFxSendsBlock[t][f][i] > 0.001f) {
          // Per-track mix balance
          float wetAmount = trackFxSendsBlock[t][f][i] * track.fxMix[f];

          if (!skipTrackSends && !shouldMute) {
            fxBusesL[f] += preFaderL * wetAmount;
            fxBusesR[f] += preFaderR * wetAmount;
          }

          // Accumulate dry kill for insert-style behavior
          if (wetAmount > trackDryKill)
            trackDryKill = wetAmount;
        }
      }

      float dryScale = 1.0f - trackDryKill;
      if (dryScale < 0.0f)
        dryScale = 0.0f;

      // (Punch already applied to rawSampleL/R above)
      float trackOutputL = rawSampleL * finalVol * dryScale;
      float trackOutputR = rawSampleR * finalVol * dryScale;

      if (shouldMute) {
        trackOutputL = 0.0f;
        trackOutputR = 0.0f;
      }

      mixedSampleL += trackOutputL;
      mixedSampleR += trackOutputR;

      track.follower.process(monoSum);
    }

    float currentSampleL = mixedSampleL;
    float currentSampleR = mixedSampleR;
    float wetSampleL = 0.0f;
    float wetSampleR = 0.0f;
    float spreadL = 0.0f, spreadR = 0.0f;

    auto routeFx = [&](int index, float valL, float valR,
                       bool isDelta = false) {
      // CRITICAL SAFETY CHECK: Block NaNs/Infs from FX
      if (!std::isfinite(valL) || !std::isfinite(valR)) {
        // potential TODO: Reset the FX that caused this?
        return;
      }

      int dest = mFxChainDest[index];
      float outL, outR;

      // Calculate Full Wet Output (Recover from Delta if needed)
      if (isDelta) {
        outL = (fxBusesL[index] + valL) * mFxMixLevels[index];
        outR = (fxBusesR[index] + valR) * mFxMixLevels[index];
      } else {
        outL = valL * mFxMixLevels[index];
        outR = valR * mFxMixLevels[index];
      }

      if (dest >= 0 && dest < 18) {
        // Serial Chaining
        // Determine Direction based on Hardcoded Execution Order
        // IDs: 0, 1, 9, 10, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 17
        const int order[18] = {0, 1, 4,  5,  6,  7,  8,  9,  10,
                               2, 3, 11, 12, 15, 16, 13, 14, 17};
        bool isForward = order[dest] > order[index];

        if (isForward) {
          fxBusesL[dest] += outL; // Pass Full Signal (Unity Gain)
          fxBusesR[dest] += outR;
        } else {
          // Backward Chain -> Use Feedback Buffer for Next Sample
          mFxFeedbacksL[dest] += outL;
          mFxFeedbacksR[dest] += outR;
        }
      } else {
        // Main Mix: Add to accumulator
        wetSampleL += outL;
        wetSampleR += outR;
      }
    };

    // Option A: FX Activity Bitmask — only evaluate slots that have active sends.
    // The mask is rebuilt whenever any fxSend or chain routing changes (cheap, not per-sample).
    // Delay/Reverb/TapeEcho tail bits are refreshed once per render block (see rebuildFxActivityMask).
    uint32_t fxToProcess = mActiveFxSlotMask;
    while (fxToProcess) {
      int slot = __builtin_ctz(fxToProcess); // index of lowest active bit
      fxToProcess &= fxToProcess - 1;        // clear that bit

      // Per-slot threshold gate (still needed for tail silence detection)
      bool busActive = std::abs(fxBusesL[slot]) > 0.00001f ||
                       std::abs(fxBusesR[slot]) > 0.00001f;

      switch (slot) {
      case 0: // Overdrive
        if (busActive)
          routeFx(0, mOverdriveFxL.process(fxBusesL[0]),
                  mOverdriveFxR.process(fxBusesR[0]), false);
        break;
      case 1: // Bitcrusher
        if (busActive)
          routeFx(1, mBitcrusherFxL.process(fxBusesL[1]),
                  mBitcrusherFxR.process(fxBusesR[1]), false);
        break;
      case 2: // Chorus
        if (busActive)
          routeFx(2, mChorusFxL.process(fxBusesL[2], sampleRate),
                  mChorusFxR.process(fxBusesR[2], sampleRate));
        break;
      case 3: // Phaser
        if (busActive)
          routeFx(3, mPhaserFxL.process(fxBusesL[3], sampleRate),
                  mPhaserFxR.process(fxBusesR[3], sampleRate));
        break;
      case 4: // Tape Wobble
        if (busActive) {
          float wL = 0, wR = 0;
          mTapeWobbleFx.processStereo(fxBusesL[4], fxBusesR[4], wL, wR, sampleRate);
          routeFx(4, wL, wR, true);
        }
        break;
      case 5: // Delay (has tail — check isSilent too)
        if (busActive || !mDelayFx.isSilent()) {
          float dL = 0, dR = 0;
          mDelayFx.processStereo(fxBusesL[5], fxBusesR[5], dL, dR, sampleRate);
          int dest = mFxChainDest[5];
          if (dest >= 0 && dest < 17) {
            fxBusesL[dest] += dL * mFxMixLevels[5];
            fxBusesR[dest] += dR * mFxMixLevels[5];
          } else {
            spreadL += dL; spreadR += dR;
          }
        }
        break;
      case 6: // Reverb (has tail — check isSilent too)
        if (busActive || !mReverbFx.isSilent()) {
          float rL = 0, rR = 0;
          mReverbFx.processStereoWet(fxBusesL[6], fxBusesR[6], rL, rR);
          int dest = mFxChainDest[6];
          if (dest >= 0 && dest < 17) {
            fxBusesL[dest] += rL * mFxMixLevels[6];
            fxBusesR[dest] += rR * mFxMixLevels[6];
          } else {
            spreadL += rL; spreadR += rR;
          }
        }
        break;
      case 7: // Slicer
        if (busActive)
          routeFx(7,
                  mSlicerFxL.process(fxBusesL[7], mSampleCount + i, mSamplesPerStep),
                  mSlicerFxR.process(fxBusesR[7], mSampleCount + i, mSamplesPerStep), false);
        break;
      case 8: // Compressor
        if (busActive)
          routeFx(8, mCompressorFx.process(fxBusesL[8], sidechainSignal),
                  mCompressorFx.process(fxBusesR[8], sidechainSignal));
        break;
      case 9: // HP Filter LFO
        if (busActive) {
          float hpL = mHpLfoL.process(fxBusesL[9], sampleRate);
          mHpLfoR.syncFrom(mHpLfoL);
          float hpR = mHpLfoR.process(fxBusesR[9], sampleRate);
          routeFx(9, hpL, hpR);
        }
        break;
      case 10: // LP Filter LFO
        if (busActive) {
          float lpL = mLpLfoL.process(fxBusesL[10], sampleRate);
          mLpLfoR.syncFrom(mLpLfoL);
          float lpR = mLpLfoR.process(fxBusesR[10], sampleRate);
          routeFx(10, lpL, lpR);
        }
        break;
      case 11: // Flanger
        if (busActive) {
          float fL = mFlangerFxL.process(fxBusesL[11], sampleRate);
          float fR = mFlangerFxR.process(fxBusesR[11], sampleRate);
          routeFx(11, fL, fR, true);
        }
        break;
      case 12: // Filter Pedal 1
        if (busActive) {
          routeFx(12, mFilterPedalL[0].process(fxBusesL[12], sampleRate),
                      mFilterPedalR[0].process(fxBusesR[12], sampleRate), false);
        }
        break;
      case 13: // Tape Echo (has tail)
        if (busActive || !mTapeEchoFxL.isSilent() || !mTapeEchoFxR.isSilent()) {
          float dc = 1.0e-18f;
          routeFx(13, mTapeEchoFxL.process(fxBusesL[13] + dc, sampleRate),
                      mTapeEchoFxR.process(fxBusesR[13] + dc, sampleRate));
        }
        break;
      case 14: // Octaver
        if (busActive)
          routeFx(14, mOctaverFxL.process(fxBusesL[14], sampleRate),
                      mOctaverFxR.process(fxBusesR[14], sampleRate));
        break;
      case 15: // Filter Pedal 2
        if (busActive) {
          routeFx(15, mFilterPedalL[1].process(fxBusesL[15], sampleRate),
                      mFilterPedalR[1].process(fxBusesR[15], sampleRate), false);
        }
        break;
      case 16: // Filter Pedal 3
        if (busActive) {
          routeFx(16, mFilterPedalL[2].process(fxBusesL[16], sampleRate),
                      mFilterPedalR[2].process(fxBusesR[16], sampleRate), false);
        }
        break;
      case 17: // 5-Band EQ
        if (busActive)
          routeFx(17, mEq5BandFxL.process(fxBusesL[17], sampleRate),
                      mEq5BandFxR.process(fxBusesR[17], sampleRate));
        break;
      }
    }

    float finalL = (mixedSampleL + wetSampleL + spreadL);
    float finalR = (mixedSampleR + wetSampleR + spreadR);

    if (!std::isfinite(finalL))
      finalL = 0.0f;
    if (!std::isfinite(finalR))
      finalR = 0.0f;

    // NOTE: HP LFO (9) and LP LFO (10) are now ONLY processed via sends
    // (see routeFx calls above). Master insert was removed to prevent
    // double-processing and "track grabbing" issues.

    // Final Master Volume, Audio Output Mode processing & Limiter
    float outL = finalL * mMasterVolume;
    float outR = finalR * mMasterVolume;

    if (mAudioOutputMode == 1) { // Mono (L-Only)
      float monoSum = (outL + outR) * 0.5f;
      outL = monoSum;
      outR = 0.0f;
    } else if (mAudioOutputMode == 2) { // Pseudo-Stereo (Delay Spread)
      int delaySamples = static_cast<int>(mSampleRate * 0.004); // ~4ms (e.g. 192 samples at 48kHz)
      if (delaySamples > 510) delaySamples = 510;
      if (delaySamples < 1) delaySamples = 1;

      // Push Right channel to buffer
      mDelaySpreadBuffer[mDelaySpreadWritePtr] = outR;
      
      // Read delayed sample
      int readPtr = (mDelaySpreadWritePtr - delaySamples + 512) % 512;
      outR = mDelaySpreadBuffer[readPtr];
      
      mDelaySpreadWritePtr = (mDelaySpreadWritePtr + 1) % 512;
    } else if (mAudioOutputMode == 3) { // Phase-Invert Mono
      float monoSum = (outL + outR) * 0.5f;
      outL = monoSum;
      outR = -monoSum;
    }





    outBuffer[outIdx * 2] = softLimit(outL);
    outBuffer[outIdx * 2 + 1] = softLimit(outR);
  }
  }
}

// Reset Punch Active flags for all tracks after processing the block
// Reset of mPunchActive removed here, handled frame-by-frame

void AudioEngine::renderToWav(int numCycles, const std::string &path) {
  std::lock_guard<std::recursive_mutex> lock(mLock);

  // Offline rendering logic
  int framesPerCycle = mSamplesPerStep * 16; // One bar (16 steps)
  int totalFrames = framesPerCycle * numCycles;
  std::vector<float> output(totalFrames * 2);

  // Reset sequence state for export
  mSampleCount = 0;
  // --- High Priority: Audio Rendering ---
  int framesRendered = 0;
  while (framesRendered < totalFrames) {
    int chunk = std::min(64, totalFrames - framesRendered);

    // Lock only for the minimum duration needed to update state or commands
    {
      std::lock_guard<std::recursive_mutex> lock(mLock);
      processCommands();

      // Check for next step trigger
      if (mSampleCount >= mSamplesPerStep) {
        mSampleCount -= mSamplesPerStep;
        mGlobalStepIndex = (mGlobalStepIndex + 1) % mPatternLength;

        // Advance sequencers
        for (int t = 0; t < (int)mTracks.size(); ++t) {
          Track &track = mTracks[t];
          if (track.isActive) {
            bool isDrumTrack = (track.engineType == 5 || track.engineType == 6 ||
                                (track.engineType == 2 &&
                                 track.parameters[320] > 1.5f));

            if (isDrumTrack) {
              for (int d = 0; d < 16; ++d) {
                track.drumSequencers[d].advance();
                const Step &s = track.drumSequencers[d].getCurrentStep();
                if (s.active && !s.notes.empty()) {
                  // Trigger first note (usually 60+d)
                  triggerNoteLocked(t, s.notes[0].note,
                                    static_cast<int>(s.notes[0].velocity * 127.0f), true,
                                    s.gate, s.punch);
                }
              }
            } else {
              track.sequencer.advance();
              const Step &s = track.sequencer.getCurrentStep();
              if (s.active) {
                for (const auto &ni : s.notes) {
                  triggerNoteLocked(t, ni.note,
                                    static_cast<int>(ni.velocity * 127.0f), true,
                                    s.gate, s.punch);
                }
              }
            }
          }
        }
      }
    } // Unlock immediately after state update

    // Render Audio (Lock is re-acquired inside renderStereo only if needed?
    // Actually renderStereo accesses mTracks heavily, so it probably NEEDS
    // safety. However, locking for the WHOLE block causes glitches. The
    // "Shield" strategy: Hold lock during render, but check it's not held
    // too long. For now, simplicity = safety.
    {
      std::lock_guard<std::recursive_mutex> lock(mLock);
      renderStereo(&output[framesRendered * 2], chunk);
    }

    framesRendered += chunk;
    mSampleCount += chunk;
  }

  WavFileUtils::writeWav(path, output, 48000, 2, {});
}

void AudioEngine::loadWavetable(int trackIndex, const std::string &path) {
  if (trackIndex >= 0 && trackIndex < mTracks.size()) {
    if (mTracks[trackIndex].engineType == 4) { // Wavetable Engine
      mTracks[trackIndex].wavetableEngine.loadWavetable(path);
    }
  }
}

void AudioEngine::loadDefaultWavetable(int trackIndex) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    if (mTracks[trackIndex].engineType == 4) {
      mTracks[trackIndex].wavetableEngine.loadDefaultWavetable();
    }
  }
}

void AudioEngine::getStepActiveStates(int trackIndex, bool *out, int maxSize) {
  std::lock_guard<std::recursive_mutex> lock(mLock);
  if (trackIndex < 0 || trackIndex >= (int)mTracks.size()) {
    std::fill(out, out + maxSize, false);
    return;
  }
  const auto &steps = mTracks[trackIndex].sequencer.getSteps();
  int limit = std::min(maxSize, (int)steps.size());
  for (int i = 0; i < limit; ++i) {
    out[i] = steps[i].active;
  }
}
void AudioEngine::setInputDevice(int deviceId) {
  // Input device selection is handled externally
}
void AudioEngine::setTrackVolume(int trackIndex, float volume) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_VOLUME;
  cmd.trackIndex = trackIndex;
  cmd.value = volume;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setTrackActive(int trackIndex, bool active) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_ACTIVE;
  cmd.trackIndex = trackIndex;
  cmd.bValue = active;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setTrackPan(int trackIndex, float pan) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_PAN;
  cmd.trackIndex = trackIndex;
  cmd.value = pan;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setPadMod(int trackIndex, float value) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_PAD_MOD;
  cmd.trackIndex = trackIndex;
  cmd.value = value;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setTrackMute(int trackIndex, bool muted) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_MUTE;
  cmd.trackIndex = trackIndex;
  cmd.bValue = muted;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setTrackSolo(int trackIndex, bool soloed) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_TRACK_SOLO;
  cmd.trackIndex = trackIndex;
  cmd.bValue = soloed;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setEngineType(int trackIndex, int type) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_ENGINE_TYPE;
  cmd.trackIndex = trackIndex;
  cmd.data1 = type;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}
void AudioEngine::loadSoundFont(int trackIndex, const std::string &path) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
      return;

    // Check file size > 1KB (SoundFonts are usually larger)
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f); // <--- Close AFTER using

    if (size < 1024) {
      return;
    }

    std::lock_guard<std::recursive_mutex> lock(mLock);
    mTracks[trackIndex].soundFontEngine.load(path);
    
    std::string filename = path;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
      filename = filename.substr(lastSlash + 1);
    }
    mTracks[trackIndex].lastSamplePath = filename;

    // Sync bank parameter 181
    std::vector<std::string> sfFiles = getSoundFontFilesList();
    int bankIdx = -1;
    for (int i = 0; i < (int)sfFiles.size(); ++i) {
      if (sfFiles[i] == filename) {
        bankIdx = i;
        break;
      }
    }
    if (bankIdx != -1 && sfFiles.size() > 1) {
      float bankVal = (float)bankIdx / (sfFiles.size() - 1);
      mTracks[trackIndex].parameters[181] = bankVal;
      mTracks[trackIndex].appliedParameters[181] = bankVal;
    } else {
      mTracks[trackIndex].parameters[181] = 0.0f;
      mTracks[trackIndex].appliedParameters[181] = 0.0f;
    }
  }
}

std::vector<std::string> AudioEngine::getSoundFontFilesList() {
  std::vector<std::string> sfFiles;
  const char* home = getenv("HOME");
  std::string homeStr = home ? std::string(home) + "/Loom" : "./Loom";
  std::string dirPath = homeStr + "/soundfonts";
  DIR* dir = opendir(dirPath.c_str());
  if (dir) {
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      std::string name = entry->d_name;
      if (name == "." || name == "..") continue;
      std::string lowerName = name;
      for (char &c : lowerName) c = std::tolower((unsigned char)c);
      if ((lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".sf2") ||
          (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".sf3")) {
        sfFiles.push_back(name);
      }
    }
    closedir(dir);
  }
  std::sort(sfFiles.begin(), sfFiles.end());
  return sfFiles;
}

void AudioEngine::setSoundFontPreset(int trackIndex, int presetIndex) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    std::lock_guard<std::recursive_mutex> lock(mLock);
    mTracks[trackIndex].soundFontEngine.setPreset(presetIndex);
  }
}

int AudioEngine::getSoundFontPresetCount(int trackIndex) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    return mTracks[trackIndex].soundFontEngine.getPresetCount();
  }
  return 0;
}

std::string AudioEngine::getSoundFontPresetName(int trackIndex,
                                                int presetIndex) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    return mTracks[trackIndex].soundFontEngine.getPresetName(presetIndex);
  }
  return "";
}

void AudioEngine::setSlices(int trackIndex, const std::vector<int> &starts,
                            const std::vector<int> &ends) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_SLICES;
  cmd.trackIndex = trackIndex;
  cmd.sliceStarts = starts;
  cmd.sliceEnds = ends;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::getFxChain(int *destination) {
  for (int i = 0; i < 17; ++i) {
    destination[i] = mFxChainDest[i];
  }
}

void AudioEngine::setChainEnabled(int trackIndex, bool enabled) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_CHAIN_ENABLED;
  cmd.trackIndex = trackIndex;
  cmd.bValue = enabled;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setChainLength(int trackIndex, int length) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_CHAIN_LENGTH;
  cmd.trackIndex = trackIndex;
  cmd.data1 = length;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setChainSlot(int trackIndex, int slotIndex, int laneIndex,
                               const std::vector<Step> &steps) {
  AudioCommand cmd;
  cmd.type = AudioCommand::SET_CHAIN_SLOT;
  cmd.trackIndex = trackIndex;
  cmd.data1 = slotIndex;
  cmd.laneIndex = laneIndex;
  cmd.steps = steps;
  std::lock_guard<std::mutex> lock(mCommandLock);
  mCommandQueue.push_back(cmd);
}

void AudioEngine::setSlicePosition(int trackIndex, int sliceIndex,
                                   float position) {
  if (trackIndex >= 0 && trackIndex < (int)mTracks.size()) {
    mTracks[trackIndex].samplerEngine.setSlicePosition(sliceIndex, position);
  }
}
