#ifndef ANALOG_DRUM_ENGINE_H
#define ANALOG_DRUM_ENGINE_H

#include "../Utils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Simple, fast, thread-safe pseudo-random generator
struct FastNoise {
  uint32_t seed = 22222;
  float next() {
    seed = (seed * 196314165 + 907633515);
    return ((int32_t)seed) * 4.6566128737e-10f;
  }
};

class AnalogDrumEngine {
public:
  enum class DrumType {
    Kick = 0,
    Snare = 1,
    Clap = 2,
    HiHatClosed = 3,
    HiHatOpen = 4,
    Cymbal = 5,
    Perc = 6,
    Noise = 7
  };

  AnalogDrumEngine() {
    setSampleRate(48000.0f);
    resetToDefaults();
  }

  void resetToDefaults() {
    setParams(0, 0.5f, 0.3f, 0.2f, 0.8f, 0.0f); // Kick
    setParams(1, 0.2f, 0.5f, 0.5f, 0.7f, 0.0f); // Snare
    setParams(2, 0.3f, 0.5f, 0.5f, 0.5f, 0.2f); // Clap
    setParams(3, 0.1f, 0.8f, 0.5f, 0.0f, 0.1f); // CH
    setParams(4, 0.4f, 0.8f, 0.5f, 0.0f, 0.1f); // OH
    setParams(5, 0.8f, 0.7f, 0.5f, 0.0f, 0.6f); // Cymbal
    setParams(6, 0.1f, 0.5f, 0.8f, 0.5f, 0.0f); // Perc
    setParams(7, 0.3f, 0.9f, 0.5f, 0.2f, 0.8f); // Noise
  }

private:
  struct AnalogVoice {
    DrumType type = DrumType::Kick;
    bool active = false;
    float sampleRate = 48000.0f;
    FastNoise rng;

    // Current State
    uint32_t phase = 0;
    float currentFreq = 0.0f;
    float env = 0.0f;

    // Hat Oscillators
    uint32_t hatPhases[6] = {0};

    // Filter State
    float filterState = 0.0f;
    float filterState2 = 0.0f;
    float attackPhase = 0.0f;

    // Clap State
    float clapTimer = 0.0f;
    int clapStage = 0;
    float clapEnv = 0.0f;

    // Settings
    float baseFreq = 50.0f;
    float decay = 0.5f;
    float tone = 0.5f;   // Brightness/Filter
    float paramA = 0.5f; // "Punch" or "Snappy"
    float paramB = 0.0f; // "Metal"
    float gain = 0.65f;

    float velocity = 0.0f;

    void trigger(float vel) {
      active = true;
      velocity = vel;
      env = 1.0f;
      phase = 0;
      clapTimer = 0.0f;
      clapStage = 0;
      clapEnv = 0.0f;
      filterState = 0.0f;
      filterState2 = 0.0f;
      attackPhase = 0.0f;
      currentFreq = baseFreq;

      if (type == DrumType::Cymbal && paramA > 0.01f) {
        env = 0.0f;
        attackPhase = 1.0f;
      }

      if (type == DrumType::Kick) {
        float punchAmt = 2.0f + (paramA * 6.0f);
        currentFreq = baseFreq * punchAmt;
      }
    }

    float render() {
      if (!active)
        return 0.0f;

      float out = 0.0f;
      float dt = 1.0f / sampleRate;

      switch (type) {
      case DrumType::Kick: {
        env -= dt / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        float invSr = 1.0f / sampleRate;
        // Fast pitch sweep characteristic of 808
        float sweepRate = 0.007f + paramA * 0.013f;
        currentFreq += (baseFreq - currentFreq) * sweepRate;
        phase += (uint32_t)(currentFreq * invSr * 4294967296.0);

        float sine = FastSine::getInt(phase);
        // Soft analog saturation waveshaper
        if (tone > 0.1f) {
          float drive = 1.0f + tone * 1.5f;
          float x = sine * drive;
          if (x > 1.0f) x = 1.0f - expf(1.0f - x);
          else if (x < -1.0f) x = -1.0f + expf(1.0f + x);
          sine = x * 0.7f;
        }
        out = sine * env;
        break;
      }

      case DrumType::Snare: {
        float invSr = 1.0f / sampleRate;
        float envTone = std::max(0.0f, env - invSr / (decay * 0.35f));
        env -= invSr / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        // Two oscillators for snare drum shell (fundamental + harmonic)
        phase += (uint32_t)(baseFreq * invSr * 4294967296.0);
        float shell1 = FastSine::getInt(phase) * envTone;
        float shell2 = FastSine::getInt(phase * 1.5f) * envTone * 0.4f;
        float shell = shell1 + shell2;

        float noise = rng.next();
        // Snappy high-pass filtered white noise for snares
        float hpCoeff = 0.15f + (tone * 0.55f);
        filterState += (noise - filterState) * hpCoeff;
        float wires = (noise - filterState) * env;
        out = (shell * (1.0f - paramA * 0.4f)) + (wires * (0.15f + paramA * 0.8f));
        break;
      }

      case DrumType::Clap: {
        clapTimer -= dt;
        if (clapStage < 4) {
          if (clapTimer <= 0) {
            clapEnv = 1.0f;
            float spreadTime = 0.006f + (paramA * 0.02f);
            clapTimer = spreadTime + rng.next() * 0.004f;
            clapStage++;
          }
        }
        clapEnv -= dt / (0.008f + decay * 0.08f);
        if (clapEnv < 0.0f)
          clapEnv = 0.0f;
        env -= dt / decay;
        if (env <= 0.0f && clapStage >= 4) {
          active = false;
          return 0.0f;
        }
        float noise = rng.next();
        // Bandpass filter centered around 1kHz for authentic 808 clap
        filterState += (noise - filterState) * (0.35f + tone * 0.35f);
        out = (noise - filterState) * (clapEnv * 0.7f + env * 0.3f) * 0.85f;
        break;
      }

      case DrumType::HiHatClosed:
      case DrumType::HiHatOpen: {
        env -= dt / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        float spread = 1.0f + (paramB * 0.35f);
        // Six square wave frequencies of TR-808 hi-hat
        float freqs[6] = {baseFreq,
                          baseFreq * 1.48f * spread,
                          baseFreq * 1.58f,
                          baseFreq * 1.83f * spread,
                          baseFreq * 2.14f * spread,
                          baseFreq * 2.63f};
        float cluster = 0.0f;
        for (int i = 0; i < 6; ++i) {
          hatPhases[i] += (uint32_t)(freqs[i] * dt * 4294967296.0);
          cluster += (hatPhases[i] > 2147483648) ? 1.0f : -1.0f;
        }
        // Double stage 12dB/oct highpass filter for crisp 808 metal sheen
        float hpFreq = 0.45f + (tone * 0.45f);
        filterState += (cluster - filterState) * hpFreq;
        float hp1 = cluster - filterState;
        filterState2 += (hp1 - filterState2) * hpFreq;
        float hp2 = hp1 - filterState2;
        out = hp2 * env * 0.28f;
        break;
      }

      case DrumType::Cymbal: {
        if (attackPhase > 0.0f) {
          env += dt / (0.01f + paramA * 0.5f);
          if (env >= 1.0f) {
            env = 1.0f;
            attackPhase = 0.0f;
          }
        } else {
          env -= dt / decay;
        }

        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        float spread = 1.0f + (paramB * 0.4f);
        float freqs[6] = {baseFreq,
                          baseFreq * 1.5f * spread,
                          baseFreq * 1.63f,
                          baseFreq * 1.86f * spread,
                          baseFreq * 2.16f * spread,
                          baseFreq * 2.66f};
        float cluster = 0.0f;
        for (int i = 0; i < 6; ++i) {
          hatPhases[i] += (uint32_t)(freqs[i] * dt * 4294967296.0);
          cluster += (hatPhases[i] > 2147483648) ? 1.0f : -1.0f;
        }
        // 12dB/oct Highpass filter
        float hpFreq = 0.15f + (tone * 0.45f);
        filterState += (cluster - filterState) * hpFreq;
        float hp1 = cluster - filterState;
        filterState2 += (hp1 - filterState2) * hpFreq;
        float hp2 = hp1 - filterState2;
        out = hp2 * env * 0.35f;
        break;
      }

      case DrumType::Perc: {
        env -= dt / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        phase += (uint32_t)(baseFreq * dt * 4294967296.0);
        float sine = FastSine::getInt(phase);
        // Subtle cowbell-like harmonic ring if tone is high
        if (tone > 0.4f) {
          sine = (sine + FastSine::getInt(phase * 1.48f) * 0.3f) * 0.8f;
        }
        out = sine * env * 0.75f;
        break;
      }

      case DrumType::Noise: {
        env -= dt / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        float noise = rng.next();
        float lpFreq = 0.08f + (tone * 0.82f);
        filterState += (noise - filterState) * lpFreq;
        out = filterState * env * 0.65f;
        break;
      }
      } // Switch
      return out * velocity * gain;
    }
  };

  AnalogVoice mVoices[8];
  float mLastRenders[8] = {0.0f};

public:
  void setSampleRate(float sr) {
    for (auto &v : mVoices)
      v.sampleRate = sr;
  }

  void allNotesOff() {
    for (auto &v : mVoices) {
      v.active = false;
      v.env = 0.0f;
    }
  }

  void setParameter(int drumIdx, int paramId, float value) {
    if (drumIdx < 0 || drumIdx >= 8)
      return;
    AnalogVoice &v = mVoices[drumIdx];

    switch (paramId) {
    case 0:
      v.decay = 0.05f + (value * 1.5f);
      break;
    case 1:
      v.tone = value;
      break;
    case 2: // Tune
      if (v.type == DrumType::Kick)
        v.baseFreq = 38.0f + (value * 45.0f); // 808 Kick sweet spot
      else if (v.type == DrumType::Snare)
        v.baseFreq = 120.0f + (value * 160.0f); // Snare shell tuning (120-280Hz)
      else if (v.type == DrumType::Clap)
        v.baseFreq = 800.0f + (value * 1000.0f); // Clap bandpass center
      else if (v.type == DrumType::Perc)
        v.baseFreq = 150.0f + (value * 500.0f);
      else if (v.type == DrumType::HiHatClosed || v.type == DrumType::HiHatOpen)
        v.baseFreq = 250.0f + (value * 450.0f);
      else if (v.type == DrumType::Cymbal)
        v.baseFreq = 200.0f + (value * 300.0f);
      else
        v.baseFreq = 100.0f + (value * 400.0f);
      break;
    case 3:
      v.paramA = value;
      break;
    case 4:
      v.paramB = value;
      break;
    case 5:
      v.gain = value;
      break;
    }
  }

  void triggerNote(int note, int velocity) {
    int idx = -1;
    switch (note) {
    case 36:
    case 35:
      idx = 0;
      break; // Kick
    case 38:
    case 40:
      idx = 1;
      break; // Snare
    case 39:
      idx = 2;
      break; // Clap
    case 42:
      idx = 3;
      break; // CH
    case 46:
      idx = 4;
      break; // OH
    case 49:
      idx = 5;
      break; // Cymbal (Crash)
    case 51:
      idx = 5;
      break; // Cymbal (Ride)
    default:
      if (note >= 0 && note < 8)
        idx = note;
      else if (note >= 60 && note < 68)
        idx = note - 60;
      break;
    }
    if (idx != -1)
      mVoices[idx].trigger(velocity / 127.0f);
  }

  void releaseNote(int note) {}

  void renderBlock(float* outL, float* outR, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
      float s = render();
      outL[i] = s;
      outR[i] = s;
    }
  }

  float render() {
    float out = 0.0f;
    for (int i = 0; i < 8; ++i) {
      if (mVoices[i].active) {
        mLastRenders[i] = mVoices[i].render();
        out += mLastRenders[i];
      } else {
        mLastRenders[i] = 0.0f;
      }
    }
    return std::tanh(out * 0.9f);
  }

  bool isActive() const {
    for (const auto &v : mVoices)
      if (v.active)
        return true;
    return false;
  }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (int i = 0; i < 8; ++i) {
      if (mVoices[i].active) {
        maxEnv = std::max(maxEnv, mVoices[i].env);
      }
    }
    return maxEnv;
  }

  float getVoiceOutput(int index) {
    if (index >= 0 && index < 8)
      return mLastRenders[index];
    return 0.0f;
  }

  void setParams(int idx, float dec, float tone, float tune, float pA,
                 float pB) {
    if (idx < 0 || idx >= 8)
      return;
    // Map extended indices to existing DSP types
    if (idx == 5)
      mVoices[idx].type = DrumType::Cymbal;
    else if (idx == 6)
      mVoices[idx].type = DrumType::Perc;
    else if (idx == 7)
      mVoices[idx].type = DrumType::Noise;
    else
      mVoices[idx].type = (DrumType)idx;

    setParameter(idx, 0, dec);
    setParameter(idx, 1, tone);
    setParameter(idx, 2, tune);
    setParameter(idx, 3, pA);
    setParameter(idx, 4, pB);
  }
};

#endif // ANALOG_DRUM_ENGINE_H
