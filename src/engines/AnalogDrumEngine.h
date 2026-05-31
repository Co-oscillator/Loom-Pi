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
        currentFreq +=
            (baseFreq - currentFreq) * (0.002f + (1.0f - tone) * 0.005f);
        phase += (uint32_t)(currentFreq * invSr * 4294967296.0);

        float sine = FastSine::getInt(phase);
        if (tone > 0.5f) {
          float x = sine * 1.4f;
          if (x > 1.0f)
            x = 1.0f - expf(1.0f - x);
          else if (x < -1.0f)
            x = -1.0f + expf(1.0f + x);
          float x2 = x * x;
          sine = x * (27.0f + x2) / (27.0f + 9.0f * x2);
        }
        out = sine * env;
        break;
      }

      case DrumType::Snare: {
        float invSr = 1.0f / sampleRate;
        float envTone = std::max(0.0f, env - invSr / (decay * 0.4f));
        env -= invSr / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        phase += (uint32_t)(baseFreq * invSr * 4294967296.0);
        float shell = FastSine::getInt(phase) * envTone;
        float noise = rng.next();
        float hpCoeff = 0.1f + (tone * 0.6f);
        filterState += (noise - filterState) * hpCoeff;
        float wires = (noise - filterState) * env;
        out = (shell * (1.0f - paramA * 0.5f)) + (wires * (0.2f + paramA));
        break;
      }

      case DrumType::Clap: {
        clapTimer -= dt;
        if (clapStage < 4) {
          if (clapTimer <= 0) {
            clapEnv = 1.0f;
            float spreadTime = 0.005f + (paramA * 0.025f);
            clapTimer = spreadTime + rng.next() * 0.005f;
            clapStage++;
          }
        }
        clapEnv -= dt / (0.01f + decay * 0.1f);
        if (clapEnv < 0.0f)
          clapEnv = 0.0f;
        env -= dt / decay;
        if (env <= 0.0f && clapStage >= 4) {
          active = false;
          return 0.0f;
        }
        float noise = rng.next();
        filterState += (noise - filterState) * (0.4f + tone * 0.4f);
        out = (noise - filterState) * clapEnv * 0.8f;
        break;
      }

      case DrumType::HiHatClosed:
      case DrumType::HiHatOpen: {
        env -= dt / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        float spread = 1.0f + (paramB * 0.3f);
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
        float hpFreq = 0.25f + (tone * 0.7f);
        filterState += (cluster - filterState) * hpFreq;
        out = (cluster - filterState) * env * 0.3f;
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
        float hpFreq = 0.05f + (tone * 0.4f);
        filterState += (cluster - filterState) * hpFreq;
        out = (cluster - filterState) * env * 0.4f;
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
        out = sine * env * 0.8f;
        break;
      }

      case DrumType::Noise: {
        env -= dt / decay;
        if (env <= 0.0f) {
          active = false;
          return 0.0f;
        }
        float noise = rng.next();
        float lpFreq = 0.1f + (tone * 0.8f);
        filterState += (noise - filterState) * lpFreq;
        out = filterState * env * 0.7f;
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
        v.baseFreq = 30.0f + (value * 60.0f);
      else if (v.type == DrumType::Perc)
        v.baseFreq = 200.0f + (value * 600.0f);
      else if (v.type == DrumType::HiHatClosed || v.type == DrumType::HiHatOpen)
        v.baseFreq = 200.0f + (value * 800.0f);
      else if (v.type == DrumType::Cymbal)
        v.baseFreq = 100.0f + (value * 400.0f);
      else
        v.baseFreq = value;
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
