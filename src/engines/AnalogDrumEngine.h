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
    float env2 = 0.0f;
    float pitchEnv = 0.0f;

    float decayCoeff = 0.999f;
    float decayCoeff2 = 0.999f;
    float pitchDecayCoeff = 0.99f;

    // Hat Oscillators
    uint32_t hatPhases[6] = {0};

    // Filter State
    float filterState = 0.0f;
    float filterState2 = 0.0f;
    float attackPhase = 0.0f;
    float attackTime = 0.01f;

    // Clap State
    float clapTimer = 0.0f;
    int clapStage = 0;
    float clapEnv = 0.0f;

    // Settings
    float baseFreq = 50.0f;
    float decay = 0.5f;
    float tone = 0.5f;   // Brightness/Filter
    float paramA = 0.5f; // "Punch" or "Snappy" / "Atk"
    float paramB = 0.0f; // "Metal"
    float gain = 0.65f;

    float velocity = 0.0f;

    void updateCoeffs() {
      float sr = (sampleRate > 1000.0f) ? sampleRate : 48000.0f;
      float d = std::max(0.0f, std::min(1.0f, decay));

      switch (type) {
      case DrumType::Kick: {
        float kickDecayTime = 0.15f + (d * d * 2.35f); // 150ms to 2.5s
        decayCoeff = expf(-1.0f / (sr * kickDecayTime));
        float pTime = 0.020f + (paramA * 0.040f);      // 20ms to 60ms pitch sweep
        pitchDecayCoeff = expf(-1.0f / (sr * pTime));
        break;
      }
      case DrumType::Snare: {
        float shellTime = 0.04f + (d * 0.22f); // 40ms to 260ms shell ring
        float wiresTime = 0.08f + (d * 0.72f); // 80ms to 800ms snare wires
        decayCoeff = expf(-1.0f / (sr * shellTime));
        decayCoeff2 = expf(-1.0f / (sr * wiresTime));
        break;
      }
      case DrumType::Clap: {
        float tailTime = 0.10f + (d * 1.10f); // 100ms to 1.2s tail
        decayCoeff = expf(-1.0f / (sr * tailTime));
        decayCoeff2 = expf(-1.0f / (sr * 0.012f)); // 12ms burst decay
        break;
      }
      case DrumType::HiHatClosed: {
        float hatTime = 0.030f + (d * 0.22f); // 30ms to 250ms
        decayCoeff = expf(-1.0f / (sr * hatTime));
        break;
      }
      case DrumType::HiHatOpen: {
        float hatTime = 0.150f + (d * 1.45f); // 150ms to 1.6s
        decayCoeff = expf(-1.0f / (sr * hatTime));
        break;
      }
      case DrumType::Cymbal: {
        float cymTime = 0.30f + (d * 2.70f); // 300ms to 3.0s
        decayCoeff = expf(-1.0f / (sr * cymTime));
        attackTime = 0.003f + (paramA * 0.080f);
        break;
      }
      case DrumType::Perc: {
        float percTime = 0.060f + (d * 0.94f); // 60ms to 1.0s
        decayCoeff = expf(-1.0f / (sr * percTime));
        break;
      }
      case DrumType::Noise: {
        float noiseTime = 0.050f + (d * 1.45f); // 50ms to 1.5s
        decayCoeff = expf(-1.0f / (sr * noiseTime));
        break;
      }
      }
    }

    void trigger(float vel) {
      active = true;
      velocity = vel;
      env = 1.0f;
      env2 = 1.0f;
      pitchEnv = 1.0f;
      phase = 0;
      clapTimer = 0.0f;
      clapStage = 0;
      clapEnv = 1.0f;
      filterState = 0.0f;
      filterState2 = 0.0f;
      attackPhase = 0.0f;

      updateCoeffs();

      if (type == DrumType::Cymbal && paramA > 0.05f) {
        env = 0.0f;
        attackPhase = 1.0f;
      }

      currentFreq = baseFreq;
    }

    float render() {
      if (!active)
        return 0.0f;

      float out = 0.0f;
      float dt = 1.0f / sampleRate;

      switch (type) {
      case DrumType::Kick: {
        env *= decayCoeff;
        pitchEnv *= pitchDecayCoeff;
        if (env < 0.0002f) {
          active = false;
          env = 0.0f;
          return 0.0f;
        }
        float invSr = 1.0f / sampleRate;
        float punchAmt = 1.5f + (paramA * 5.0f);
        currentFreq = baseFreq * (1.0f + punchAmt * pitchEnv);
        phase += (uint32_t)(currentFreq * invSr * 4294967296.0);

        float sine = FastSine::getInt(phase);
        if (tone > 0.05f) {
          float drive = 1.0f + tone * 2.0f;
          float x = sine * drive;
          if (x > 1.0f) x = 1.0f - expf(1.0f - x);
          else if (x < -1.0f) x = -1.0f + expf(1.0f + x);
          sine = x * 0.75f;
        }
        out = sine * env * 1.25f;
        break;
      }

      case DrumType::Snare: {
        float invSr = 1.0f / sampleRate;
        env *= decayCoeff;
        env2 *= decayCoeff2;
        if (env < 0.0002f && env2 < 0.0002f) {
          active = false;
          env = env2 = 0.0f;
          return 0.0f;
        }
        phase += (uint32_t)(baseFreq * invSr * 4294967296.0);
        float shell1 = FastSine::getInt(phase) * env;
        float shell2 = FastSine::getInt((uint32_t)(phase * 1.53f)) * env * 0.4f;
        float shell = shell1 + shell2;

        float noise = rng.next();
        float hpCoeff = 0.15f + (tone * 0.65f);
        filterState += (noise - filterState) * hpCoeff;
        float wires = (noise - filterState) * env2;

        out = (shell * (1.0f - paramA * 0.45f)) + (wires * (0.25f + paramA * 0.95f));
        out *= 1.2f;
        break;
      }

      case DrumType::Clap: {
        clapTimer -= dt;
        if (clapStage < 3) {
          if (clapTimer <= 0.0f) {
            clapEnv = 1.0f;
            float spreadTime = 0.008f + (paramA * 0.016f);
            clapTimer = spreadTime + rng.next() * 0.002f;
            clapStage++;
          }
        }
        clapEnv *= decayCoeff2;
        env *= decayCoeff;
        if (env < 0.0002f && clapStage >= 3) {
          active = false;
          env = 0.0f;
          return 0.0f;
        }
        float noise = rng.next();
        filterState += (noise - filterState) * (0.32f + tone * 0.38f);
        float bp = noise - filterState;
        out = bp * (clapEnv * 0.8f + env * 0.4f) * 1.3f;
        break;
      }

      case DrumType::HiHatClosed:
      case DrumType::HiHatOpen: {
        env *= decayCoeff;
        if (env < 0.0002f) {
          active = false;
          env = 0.0f;
          return 0.0f;
        }
        float spread = 1.0f + (paramB * 0.35f);
        float freqs[6] = {baseFreq,
                          baseFreq * 1.48f * spread,
                          baseFreq * 1.58f,
                          baseFreq * 1.83f * spread,
                          baseFreq * 2.14f * spread,
                          baseFreq * 2.63f};
        float cluster = 0.0f;
        for (int i = 0; i < 6; ++i) {
          hatPhases[i] += (uint32_t)(freqs[i] * dt * 4294967296.0);
          cluster += (hatPhases[i] > 2147483648) ? 0.35f : -0.35f;
        }
        float hpFreq = 0.45f + (tone * 0.45f);
        filterState += (cluster - filterState) * hpFreq;
        float hp1 = cluster - filterState;
        filterState2 += (hp1 - filterState2) * hpFreq;
        float hp2 = hp1 - filterState2;
        out = hp2 * env * 1.25f;
        break;
      }

      case DrumType::Cymbal: {
        if (attackPhase > 0.0f) {
          env += dt / attackTime;
          if (env >= 1.0f) {
            env = 1.0f;
            attackPhase = 0.0f;
          }
        } else {
          env *= decayCoeff;
        }

        if (env < 0.0002f) {
          active = false;
          env = 0.0f;
          return 0.0f;
        }
        float spread = 1.0f + (paramB * 0.4f);
        float freqs[6] = {baseFreq,
                          baseFreq * 1.50f * spread,
                          baseFreq * 1.63f,
                          baseFreq * 1.86f * spread,
                          baseFreq * 2.16f * spread,
                          baseFreq * 2.66f};
        float cluster = 0.0f;
        for (int i = 0; i < 6; ++i) {
          hatPhases[i] += (uint32_t)(freqs[i] * dt * 4294967296.0);
          cluster += (hatPhases[i] > 2147483648) ? 0.35f : -0.35f;
        }
        float hpFreq = 0.18f + (tone * 0.50f);
        filterState += (cluster - filterState) * hpFreq;
        float hp1 = cluster - filterState;
        filterState2 += (hp1 - filterState2) * hpFreq;
        float hp2 = hp1 - filterState2;
        out = hp2 * env * 1.35f;
        break;
      }

      case DrumType::Perc: {
        env *= decayCoeff;
        if (env < 0.0002f) {
          active = false;
          env = 0.0f;
          return 0.0f;
        }
        phase += (uint32_t)(baseFreq * dt * 4294967296.0);
        float sine = FastSine::getInt(phase);
        if (tone > 0.35f) {
          sine = (sine + FastSine::getInt((uint32_t)(phase * 1.48f)) * 0.35f) * 0.8f;
        }
        out = sine * env * 1.15f;
        break;
      }

      case DrumType::Noise: {
        env *= decayCoeff;
        if (env < 0.0002f) {
          active = false;
          env = 0.0f;
          return 0.0f;
        }
        float noise = rng.next();
        float lpFreq = 0.08f + (tone * 0.82f);
        filterState += (noise - filterState) * lpFreq;
        out = filterState * env * 1.15f;
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
    for (auto &v : mVoices) {
      v.sampleRate = sr;
      v.updateCoeffs();
    }
  }

  void allNotesOff() {
    for (auto &v : mVoices) {
      v.active = false;
      v.env = 0.0f;
      v.env2 = 0.0f;
    }
  }

  void setParameter(int drumIdx, int paramId, float value) {
    if (drumIdx < 0 || drumIdx >= 8)
      return;
    AnalogVoice &v = mVoices[drumIdx];

    switch (paramId) {
    case 0:
      v.decay = value;
      v.updateCoeffs();
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
      v.updateCoeffs();
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
    case 35: // Acoustic Bass Drum
    case 36: // Bass Drum 1
      idx = 0;
      break; // Kick
    case 38: // Acoustic Snare
    case 40: // Electric Snare
      idx = 1;
      break; // Snare
    case 37: // Side Stick
    case 39: // Hand Clap
      idx = 2;
      break; // Clap
    case 42: // Closed Hi-Hat
    case 44: // Pedal Hi-Hat
      idx = 3;
      break; // CH
    case 46: // Open Hi-Hat
      idx = 4;
      break; // OH
    case 49: // Crash Cymbal 1
    case 51: // Ride Cymbal 1
    case 52: // Chinese Cymbal
    case 53: // Ride Bell
    case 55: // Splash Cymbal
    case 57: // Crash Cymbal 2
    case 59: // Ride Cymbal 2
      idx = 5;
      break; // Cymbal
    case 41: // Low Floor Tom
    case 43: // High Floor Tom
    case 45: // Low Tom
    case 47: // Low-Mid Tom
    case 48: // Hi-Mid Tom (Key 6 in drum row)
    case 50: // High Tom
    case 56: // Cowbell
      idx = 6;
      break; // Perc
    case 54: // Tambourine
    case 58: // Vibraslap
    case 69: // Cabasa
    case 70: // Maracas
      idx = 7;
      break; // Noise
    default:
      if (note >= 0 && note < 8)
        idx = note;
      else if (note >= 60 && note < 68)
        idx = note - 60;
      break;
    }
    if (idx != -1) {
      if (idx == 3) {
        // Closed hat chokes open hat
        mVoices[4].active = false;
        mVoices[4].env = 0.0f;
      }
      mVoices[idx].trigger(velocity / 127.0f);
    }
  }

  void releaseNote(int note) {}

  void renderBlock(float* outL, float* outR, int numFrames) {
    if (!isActive()) {
      std::fill(outL, outL + numFrames, 0.0f);
      std::fill(outR, outR + numFrames, 0.0f);
      return;
    }
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
