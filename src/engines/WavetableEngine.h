#ifndef WAVETABLE_ENGINE_H
#define WAVETABLE_ENGINE_H

#include "../Utils.h"
#include "../WavFileUtils.h"
#include "Adsr.h"
#include <algorithm>

#include <cmath>
#include <memory>
#include <mutex>

#include <string>
#include <vector>

class WavetableEngine {
public:
  struct Voice {
    bool active = false;
    int note = -1;
    uint32_t phase = 0;
    float frequency = 440.0f;
    float targetFrequency = 440.0f;
    float amplitude = 1.0f;
    Adsr envelope;
    Adsr filterEnv;
    TSvf svf;
    float lastSample = 0.0f;
    float srateCounter = 0.0f;
    uint32_t controlCounter = 0;
    float envStart = 0.0f, envDelta = 0.0f;
    float filterEnvStart = 0.0f, filterEnvDelta = 0.0f;

    void reset() {
      active = false;
      note = -1;
      phase = 0;
      envelope.reset();
      filterEnv.reset();
      svf.setParams(1000.0f, 0.7f, 48000.0f);
      frequency = 440.0f;
      targetFrequency = 440.0f;
      lastSample = 0.0f;
      srateCounter = 0.0f;
      controlCounter = 0;
    }
  };

  WavetableEngine() {
    mVoices.resize(16);
    for (auto &v : mVoices)
      v.reset();
    mTable.assign(2048, 0.0f); // Init with sine
    for (int i = 0; i < 2048; ++i)
      mTable[i] = sinf(i * 6.283185f / 2048.0f);
    mMutex = std::make_shared<std::mutex>();
    resetToDefaults();
  }

  void resetToDefaults() {
    mAttack = 0.01f;
    mDecay = 0.1f;
    mSustain = 0.8f;
    mRelease = 0.2f;
    mCutoff = 1.0f;
    mResonance = 0.0f;
    mPosition = 0.0f;
    mDetune = 0.0f;
    mF_Atk = 0.01f;
    mF_Dcy = 0.1f;
    mF_Sus = 0.0f;
    mF_Rel = 0.5f;
    mF_Amt = 0.0f;
    mWarp = 0.0f;
    mCrush = 0.0f;
    mDrive = 0.0f;
    mBits = 1.0f;
    mSrate = 0.0f;
    mFilterMode = 0;
  }

  void setSampleRate(float sr) {
    mSampleRate = sr;
    for (auto &v : mVoices) {
      v.envelope.setSampleRate(sr);
      v.filterEnv.setSampleRate(sr);
    }
  }

  void allNotesOff() {
    for (auto &v : mVoices) {
      v.active = false;
      v.envelope.reset();
      v.filterEnv.reset();
    }
  }

  void setFrequency(float freq, float sampleRate) {
    mSampleRate = sampleRate;
    mFrequency = freq;
  }
  void setGlide(float g) { mGlide = g; }
  void setPitchBend(float v) { mPitchBend = v; }

  void loadWavetable(const std::vector<float> &data) {
    std::lock_guard<std::mutex> lock(*mMutex);
    mTable = data;
    mNumFrames = (mTable.size() > 2048) ? (int)(mTable.size() / 2048) : 1;
  }

  void loadWavetable(const std::string &path) {
    std::vector<float> data;
    int sr, channels;
    std::vector<float> slices;
    if (WavFileUtils::loadWav(path, data, sr, channels, slices)) {
      loadWavetable(data);
    }
  }

  void loadDefaultWavetable() {
    std::lock_guard<std::mutex> lock(*mMutex);
    mTable.assign(2048, 0.0f);
    for (int i = 0; i < 2048; ++i)
      mTable[i] = sinf(i * 6.283185f / 2048.0f);
    mNumFrames = 1;
  }

  void triggerNote(int note, int velocity) {
    int idx = -1;
    for (int i = 0; i < 16; ++i)
      if (!mVoices[i].active) {
        idx = i;
        break;
      }

    if (idx == -1) {
      // Favor stealing released voices
      float minScore = 10.0f;
      for (int i = 0; i < 16; ++i) {
        float vVol = mVoices[i].envelope.getValue();
        float score = vVol;
        if (!mVoices[i].envelope.isActive()) score -= 1.0f;
        
        if (score < minScore) {
          minScore = score;
          idx = i;
        }
      }
      if (idx == -1) idx = 0;
    }

    Voice &v = mVoices[idx];
    v.reset();
    v.active = true;
    v.note = note;
    v.amplitude = velocity / 127.0f;
    float baseFreq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    v.targetFrequency = baseFreq;
    v.frequency = (mGlide > 0.001f) ? mLastFrequency : baseFreq;
    mLastFrequency = baseFreq;

    v.envelope.setSampleRate(mSampleRate);
    v.envelope.setParameters(mAttack, mDecay, mSustain, mRelease);
    v.envelope.trigger();

    v.filterEnv.setSampleRate(mSampleRate);
    v.filterEnv.setParameters(mF_Atk, mF_Dcy, mF_Sus, mF_Rel);
    v.filterEnv.trigger();
  }

  void releaseNote(int note) {
    for (auto &v : mVoices)
      if (v.active && v.note == note) {
        v.envelope.release();
        v.filterEnv.release();
      }
  }

  void setAttack(float v) { mAttack = v; }
  void setDecay(float v) { mDecay = v; }
  void setSustain(float v) { mSustain = v; }
  void setRelease(float v) { mRelease = v; }
  void setFilterCutoff(float v) { mCutoff = v; }
  void setResonance(float v) { mResonance = v; }

  void setParameter(int id, float value) {
    switch (id) {
    case 0:
      mPosition = value;
      break;
    case 1:
      mDetune = value;
      break;
    case 10:
      setFilterCutoff(value);
      break;
    case 11:
      mF_Dcy = value;
      break;
    case 14:
      mF_Amt = value * 2.0f - 1.0f;
      break;
    case 15:
      mWarp = value;
      break;
    case 16:
      mCrush = value;
      break;
    case 17:
      mDrive = value;
      break;
    case 20: // Filter Mode
      mFilterMode = (int)(value * 3.99f);
      break;
    case 21: // Filter Atk
      mF_Atk = value;
      break;
    case 23: // Filter Sus
      mF_Sus = value;
      break;
    case 24: // Filter Rel
      mF_Rel = value;
      break;
    case 30:                          // Bits
      mBits = 1.0f - (value * 0.95f); // 1.0 is full, 0.05 is 1 bit
      break;
    case 31: // Srate
      mSrate = value;
      break;
    case 355:
      setGlide(value);
      break;
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
    float mixedOutput = 0.0f;
    int activeCount = 0;

    std::unique_lock<std::mutex> lock(*mMutex, std::try_to_lock);
    if (!lock.owns_lock() || mTable.empty())
      return 0.0f;

    for (auto &v : mVoices) {
      if (!v.active)
        continue;

      if (v.controlCounter % 16 == 0) {
        if (mGlide > 0.001f) {
          float glideTimeSamples = mGlide * mSampleRate * 0.5f;
          float glideAlpha = 16.0f / (glideTimeSamples + 16.0f);
          v.frequency += (v.targetFrequency - v.frequency) * glideAlpha;
        } else {
          v.frequency = v.targetFrequency;
        }
        v.envelope.processBlock(16, v.envStart, v.envDelta);
        v.filterEnv.processBlock(16, v.filterEnvStart, v.filterEnvDelta);
        
        float fEnv = v.filterEnvStart;
        float cutoff = 20.0f + mCutoff * mCutoff * 18000.0f;
        cutoff += fEnv * mF_Amt * 12000.0f;
        cutoff = std::max(20.0f, std::min(20000.0f, cutoff));

        v.svf.setParams(cutoff, 0.7f + mResonance * 5.0f, mSampleRate);
      }

      int blockPhase = v.controlCounter % 16;
      float env = v.envStart + v.envDelta * blockPhase;
      if (env < 0.0001f && !v.envelope.isActive()) {
        v.active = false;
        continue;
      }
      activeCount++;
      v.controlCounter++;

      float bendFactor = powf(2.0f, mPitchBend / 12.0f);
      float voiceDetune = 1.0f + (mDetune * 0.02f);
      // uint32_t delta mapping
      uint32_t delta = (uint32_t)(((v.frequency * bendFactor * voiceDetune) / mSampleRate) * 4294967296.0);
      v.phase += delta;

      // Sample Rate Reduction (Decimation)
      bool skipSample = false;
      if (mSrate > 0.05f) {
        float period = 1.0f + mSrate * 64.0f;
        v.srateCounter += 1.0f;
        if (v.srateCounter < period) {
          skipSample = true;
        } else {
          v.srateCounter -= period;
        }
      }

      if (!skipSample) {
        double wPhase = v.phase * 2.3283064365386963e-10; // phase / 2^32
        if (mWarp > 0.05f) {
          float p = 1.0f + mWarp * 3.0f;
          wPhase = pow(wPhase, p);
        } else if (mWarp < -0.05f) {
          float p = 1.0f - mWarp * 3.0f;
          wPhase = 1.0 - pow(1.0 - wPhase, p);
        }

        float pos = mPosition * (float)(mNumFrames - 1);
        int frame1 = (int)pos;
        int frame2 = std::min(mNumFrames - 1, frame1 + 1);
        float posFrac = pos - (float)frame1;

        auto getSample = [&](int frame, double phase) -> float {
          int offset = frame * 2048;
          double tablePos = phase * 2048.0;
          int i1 = (int)tablePos % 2048;
          int i2 = (i1 + 1) % 2048;

          if (offset + i1 >= mTable.size() || offset + i2 >= mTable.size())
            return 0.0f;

          float f = (float)(tablePos - (int)tablePos);
          return (1.0f - f) * mTable[offset + i1] + f * mTable[offset + i2];
        };

        float sample = (1.0f - posFrac) * getSample(frame1, wPhase) +
                       posFrac * getSample(frame2, wPhase);

        // Bit Reduction
        if (mBits < 0.99f) {
          float steps = powf(2.0f, mBits * 16.0f);
          sample = roundf(sample * steps) / steps;
        }

        if (mCrush > 0.05f) {
          float st = 2.0f + (1.0f - mCrush) * 32.0f;
          sample = roundf(sample * st) / st;
        }
        if (mDrive > 0.05f) {
          sample = fast_tanh(sample * (1.0f + mDrive * 4.0f));
        }
        v.lastSample = sample;
      }

      mixedOutput += v.svf.process(v.lastSample, (TSvf::Type)mFilterMode) *
                     env * v.amplitude;
    }

    if (activeCount > 1)
      mixedOutput *= (1.0f / sqrtf((float)activeCount));

    // v3.1.40: Remove engine-level saturation and add 6dB headroom.
    mControlCounter++;
    return mixedOutput * 0.5f;
  }

  bool isActive() const {
    for (const auto &v : mVoices)
      if (v.active)
        return true;
    return false;
  }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (const auto &v : mVoices) {
      if (v.active) {
        maxEnv = std::max(maxEnv, v.envelope.getValue());
      }
    }
    return maxEnv;
  }

private:
  std::vector<Voice> mVoices;
  std::vector<float> mTable;
  int mNumFrames = 1;
  float mSampleRate = 48000.0f, mFrequency = 440.0f, mLastFrequency = 440.0f,
        mGlide = 0.0f;
  float mAttack = 0.01f, mDecay = 0.1f, mSustain = 0.8f, mRelease = 0.2f;
  float mF_Atk = 0.01f, mF_Dcy = 0.1f, mF_Sus = 0.0f, mF_Rel = 0.5f,
        mF_Amt = 0.0f;
  float mCutoff = 1.0f, mResonance = 0.0f, mPosition = 0.0f, mDetune = 0.0f;
  float mWarp = 0.0f, mCrush = 0.0f, mDrive = 0.0f;
  float mBits = 1.0f, mSrate = 0.0f;
  float mPitchBend = 0.0f;
  int mFilterMode = 0;
  std::shared_ptr<std::mutex> mMutex;
  uint32_t mControlCounter = 0;
};

#endif
