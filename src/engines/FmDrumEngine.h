#ifndef FM_DRUM_ENGINE_H
#define FM_DRUM_ENGINE_H

#include "../Utils.h"
#include "FmEngine.h"

class FmDrumEngine {
public:
  enum class DrumType {
    Kick = 0,
    Snare = 1,
    Tom = 2,
    HiHat = 3,
    HiHatOpen = 4,
    Cymbal = 5,
    Perc = 6,
    Noise = 7
  };

  FmDrumEngine() {
    for (int i = 0; i < 8; ++i) {
      initEngine(i, static_cast<DrumType>(i));
    }
  }

  void initEngine(int index, DrumType type) {
    FmEngine &e = mEngines[index];
    switch (type) {
    case DrumType::Kick:
      // GOAL: Thud, not "Boing"
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 1.0f);
      e.setOpADSR(0, 0.001f, 0.3f, 0.0f, 0.1f);

      e.setOpRatio(1,
                   0.5f); // 0.5 = Sub-octave. Adds "weight" rather than "tone"
      e.setOpLevel(1, 0.8f);
      e.setOpADSR(1, 0.001f, 0.05f, 0.0f, 0.05f); // Super short "thwack"
      e.setFeedback(0.0f);
      e.setFrequency(45.0f, 48000.0f); // Deep base
      e.setPitchSweep(2.5f);           // Initial snap
      break;

    case DrumType::Snare:
      // GOAL: Paper/Wire, not "Ping"
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.9f);
      e.setOpADSR(0, 0.001f, 0.2f, 0.0f, 0.1f);

      e.setOpRatio(
          1, 1.76f); // Non-Integer! Creates a "square drum" or "membrane" sound
      e.setOpLevel(1, 0.65f);
      e.setOpADSR(1, 0.001f, 0.15f, 0.0f, 0.1f);
      e.setFeedback(0.6f); // Moderate feedback for snare buzz
      e.setFrequency(160.0f, 48000.0f);
      break;
    case DrumType::Tom:
      // GOAL: Tubby resonance
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.9f);
      e.setOpADSR(0, 0.001f, 0.4f, 0.0f, 0.2f);
      e.setOpRatio(1, 0.75f); // Lower ratio = thicker, deeper sound
      e.setOpLevel(1, 0.4f);
      e.setOpADSR(1, 0.001f, 0.15f, 0.0f, 0.1f);
      e.setFeedback(0.0f);
      e.setFrequency(90.0f, 48000.0f);
      break;

    case DrumType::HiHat:
      // GOAL: "Chiff" without piercing ears
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.5f);
      e.setOpADSR(0, 0.001f, 0.05f, 0.0f, 0.02f);

      e.setOpRatio(1, 3.4f); // High, but non-integer ratio avoids "bell" tone
      e.setOpLevel(1, 0.8f);
      e.setOpADSR(1, 0.001f, 0.04f, 0.0f, 0.02f);
      e.setFeedback(0.8f);              // Dirty noise
      e.setFrequency(400.0f, 48000.0f); // Start lower than you think!
      break;

    case DrumType::HiHatOpen:
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.5f);
      e.setOpADSR(0, 0.01f, 0.4f, 0.0f, 0.2f);

      e.setOpRatio(1, 3.4f);
      e.setOpLevel(1, 0.8f);
      e.setOpADSR(1, 0.01f, 0.4f, 0.0f, 0.2f);
      e.setFeedback(0.8f);
      e.setFrequency(400.0f, 48000.0f);
      break;
    case DrumType::Cymbal:
      // GOAL: Complex washout
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.5f);
      e.setOpADSR(0, 0.01f, 1.2f, 0.0f, 0.5f);

      e.setOpRatio(1, 1.45f); // "Detuned" ratio is great for cymbals
      e.setOpLevel(1, 0.9f);  // High modulation level is key for crash
      e.setOpADSR(1, 0.01f, 1.2f, 0.0f, 0.5f);
      e.setFeedback(0.7f);
      e.setFrequency(300.0f, 48000.0f); // Low base freq + FM = Thick Cymbal
      break;

    case DrumType::Perc:
      // GOAL: Woodblock / Cowbell
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.8f);
      e.setOpADSR(0, 0.001f, 0.12f, 0.0f, 0.1f);

      e.setOpRatio(1, 1.5f); // 1.5 ratio creates a "hollow" square-ish sound
      e.setOpLevel(1, 0.6f);
      e.setOpADSR(1, 0.001f, 0.1f, 0.0f, 0.1f);
      e.setFeedback(0.0f);
      e.setFrequency(550.0f, 48000.0f);
      break;

    case DrumType::Noise:
      e.setOpRatio(0, 1.0f);
      e.setOpLevel(0, 0.7f);
      e.setOpADSR(0, 0.001f, 0.25f, 0.0f, 0.1f);
      e.setOpRatio(1, 19.3f); // Odd decimal ratio breaks phase
      e.setOpLevel(1, 1.0f);
      e.setFeedback(1.0f);
      e.setFrequency(100.0f, 44100.0f);
      break;
    }
    e.setCarrierMask(1);
    e.setIgnoreNoteFrequency(true);
  }

  void triggerNote(int note, int velocity) {
    int drumIdx = note - 60;
    if (drumIdx >= 0 && drumIdx < 8) {
      mEngines[drumIdx].triggerNote(note, velocity);
    }
  }

  void releaseNote(int note) {
    int drumIdx = note - 60;
    if (drumIdx >= 0 && drumIdx < 8) {
      mEngines[drumIdx].releaseNote(note);
    }
  }

  void setPlaybackSpeed(float speed) {}

  void setFrequency(float freq, float sampleRate) { setSampleRate(sampleRate); }

  void setSampleRate(float sampleRate) {
    for (int i = 0; i < 8; ++i) {
      mEngines[i].setSampleRate(sampleRate);
    }
  }

  void resetToDefaults() {
    for (int i = 0; i < 8; ++i) {
      initEngine(i, static_cast<DrumType>(i));
      mGains[i] = 0.65f;
    }
  }

  void setParameter(int drumIdx, int id, float value) {
    if (drumIdx < 0 || drumIdx >= 8)
      return;

    FmEngine &e = mEngines[drumIdx];
    DrumType type = static_cast<DrumType>(drumIdx);

    // LIMITS & SCALING
    float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

    if (id == 0) { // PITCH (Tune)
      float baseFreq;
      float freqRange;

      // Customize tuning ranges per instrument
      switch (type) {
      case DrumType::Kick:
        baseFreq = 30.0f;
        freqRange = 100.0f;
        break; // 30-130Hz
      case DrumType::Snare:
        baseFreq = 100.0f;
        freqRange = 200.0f;
        break; // 100-300Hz
      case DrumType::Tom:
        baseFreq = 60.0f;
        freqRange = 140.0f;
        break; // 60-200Hz
      case DrumType::Perc:
        baseFreq = 300.0f;
        freqRange = 600.0f;
        break; // 300-900Hz
      case DrumType::Cymbal:
      case DrumType::HiHat:
      case DrumType::HiHatOpen:
        baseFreq = 200.0f;
        freqRange = 800.0f;
        break; // 200-1000Hz (FM multiplies this!)
      default:
        baseFreq = 20.0f;
        freqRange = 400.0f;
        break;
      }

      float pitchFreq = baseFreq + (v * v * freqRange);
      e.setFrequency(pitchFreq, 48000.0f);

    } else if (id == 1) { // REPLACES "ATK" -> "CLICK"
      // For Kicks/Toms: Control Pitch Sweep Amount
      if (type == DrumType::Kick || type == DrumType::Tom) {
        e.setOpADSR(1, 0.001f, 0.02f + (v * 0.1f), 0.0f, 0.05f);
        e.setPitchSweep(v * 4.0f); // Fast Pitch Sweep
      }
      // For Snares/Hats: Control Feedback (Noise amount)
      else {
        e.setFeedback(v * 0.95f);
      }

    } else if (id == 2) {                      // DECAY (Smart Duration)
      float carrierDecay = 0.05f + (v * 2.0f); // Range 50ms -> 2s
      float modDecay = carrierDecay * 0.6f;    // Mod decays faster than carrier

      if (type == DrumType::Cymbal || type == DrumType::HiHatOpen) {
        modDecay = carrierDecay;
      }

      e.setOpADSR(0, 0.001f, carrierDecay, 0.0f, carrierDecay * 0.5f);
      e.setOpADSR(1, 0.001f, modDecay, 0.0f, modDecay * 0.5f);

    } else if (id ==
               4) { // PUNCH / OVERDRIVE (Adds harmonics for small speakers)
      e.setFeedback(v * 0.4f);
      e.setOpLevel(1, std::max(e.getOpLevel(1), v * 0.7f));
    } else if (id == 5) { // GAIN
      setVoiceGain(drumIdx, v);
    } else {
      e.setParameter(id, v);
    }
  }

  void allNotesOff() {
    for (int i = 0; i < 8; ++i) {
      mEngines[i].allNotesOff();
    }
  }

  void renderBlock(float* outL, float* outR, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
      float s = render();
      outL[i] = s;
      outR[i] = s;
    }
  }

  float render() {
    float mixed = 0.0f;
    for (int i = 0; i < 8; ++i) {
      mLastRenders[i] = mEngines[i].render() * mGains[i];
      mixed += mLastRenders[i];
    }
    // v3.1.40: Remove engine-level saturation and rely on master limiter. 
    // Extra headroom factor of 0.5 to prevent inter-engine clipping.
    return mixed * 0.5f;
  }

  void setVoiceGain(int index, float gain) {
    if (index >= 0 && index < 8)
      mGains[index] = gain;
  }

  float getVoiceOutput(int index) {
    if (index >= 0 && index < 8) {
      return mLastRenders[index];
    }
    return 0.0f;
  }

  bool isActive() const {
    for (int i = 0; i < 8; ++i) {
      if (mEngines[i].isActive())
        return true;
    }
    return false;
  }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (int i = 0; i < 8; ++i) {
      maxEnv = std::max(maxEnv, mEngines[i].getEnvelopeValue());
    }
    return maxEnv;
  }

private:
  FmEngine mEngines[8];
  float mLastRenders[8] = {0.0f};
  float mGains[8] = {0.65f, 0.65f, 0.65f, 0.65f, 0.65f, 0.65f, 0.65f, 0.65f};
};

#endif // FM_DRUM_ENGINE_H
