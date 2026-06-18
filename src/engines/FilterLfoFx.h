#ifndef FILTER_LFO_FX_H
#define FILTER_LFO_FX_H

#include "../Utils.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

enum class FilterLfoMode { LowPass, HighPass };

class FilterLfoFx {
public:
  FilterLfoFx(FilterLfoMode mode) : mMode(mode) {}

  void setRate(float v) { mRate = v; }
  void setDepth(float v) { mDepth = v; }
  void setShape(float v) { mShape = v; }
  void setCutoff(float v) { mCutoff = v; }
  void setMode(int mode) { mMode = static_cast<FilterLfoMode>(mode); }
  void setResonance(float v) { mResonance = v; }
  void setSync(bool s) { mSync = s; }
  void setBpm(float b) { mBpm = b; }

  void syncFrom(const FilterLfoFx &other) {
    mPhase = other.mPhase;
    mNoiseSeed = other.mNoiseSeed;
    mNoiseSample = other.mNoiseSample;
    mControlCounter = other.mControlCounter;
  }

  float getPhase() const { return mPhase; }

  void setParameters(float rate, float depth, float shape, float cutoff,
                     float resonance) {
    mRate = rate;
    mDepth = depth;
    mShape = shape;
    mCutoff = cutoff;
    mResonance = resonance;
  }

  float process(float input, float sampleRate) {
    // Control rate update (every 16 samples)
    if (mControlCounter++ % 16 == 0) {
      // 1. LFO Calculation (Control Rate)
      float rateHz = 0.02f + (mRate * mRate) * 29.98f; // 0.02Hz to 30Hz range
      if (mSync) {
        float beatFreq = mBpm / 60.0f;
        int syncIdx = (int)(mRate * 22.99f);
        switch (syncIdx) {
        case 0:  rateHz = beatFreq / 128.0f; break; // 32/1
        case 1:  rateHz = beatFreq / 96.0f; break; // 24/1
        case 2:  rateHz = beatFreq / 64.0f; break; // 16/1
        case 3:  rateHz = beatFreq / 48.0f; break; // 12/1
        case 4:  rateHz = beatFreq / 32.0f; break; // 8/1
        case 5:  rateHz = beatFreq / 24.0f; break; // 6/1
        case 6:  rateHz = beatFreq / 16.0f; break; // 4/1
        case 7:  rateHz = beatFreq / 12.0f; break; // 3/1
        case 8:  rateHz = beatFreq / 8.0f; break; // 2/1
        case 9:  rateHz = beatFreq / 4.0f; break; // 1/1
        case 10: rateHz = beatFreq / 2.0f; break; // 1/2
        case 11: rateHz = beatFreq * 0.75f; break; // 1/3
        case 12: rateHz = beatFreq; break; // 1/4
        case 13: rateHz = beatFreq * 1.5f; break; // 1/6
        case 14: rateHz = beatFreq * 2.0f; break; // 1/8
        case 15: rateHz = beatFreq * 3.0f; break; // 1/12
        case 16: rateHz = beatFreq * 4.0f; break; // 1/16
        case 17: rateHz = beatFreq * 6.0f; break; // 1/24
        case 18: rateHz = beatFreq * 8.0f; break; // 1/32
        case 19: rateHz = beatFreq * 12.0f; break; // 1/48
        case 20: rateHz = beatFreq * 16.0f; break; // 1/64
        case 21: rateHz = beatFreq * 18.0f; break; // 1/72
        case 22: rateHz = beatFreq * 24.0f; break; // 1/96
        default: rateHz = beatFreq;
        }
      }
      mPhase += (rateHz * 16.0f) / sampleRate;
      if (mPhase >= 1.0f) {
        mPhase -= floorf(mPhase);
        mNoiseSeed = (mNoiseSeed * 1103515245 + 12345);
        mNoiseSample =
            (static_cast<float>(mNoiseSeed & 0x7FFFFFFF) / 2147483648.0f) *
                2.0f -
            1.0f;
      }

      float lfoValue = 0.0f;
      int shapeIdx = static_cast<int>(mShape * 4.99f);
      switch (shapeIdx) {
      case 0: { // Sine
        lfoValue = sinf(mPhase * 2.0f * (float)M_PI);
        break;
      }
      case 1: // Triangle
        lfoValue =
            (mPhase < 0.5f) ? (4.0f * mPhase - 1.0f) : (3.0f - 4.0f * mPhase);
        break;
      case 2: // Square
        lfoValue = (mPhase < 0.5f) ? 1.0f : -1.0f;
        break;
      case 3: // Saw
        lfoValue = 2.0f * mPhase - 1.0f;
        break;
      case 4: // Random
        lfoValue = mNoiseSample;
        break;
      }

      mTargetMod = lfoValue * mDepth;
    }

    // Per-sample smoothing and filter update
    float currentCutoff = mCutoff + mTargetMod;
    currentCutoff = std::max(0.001f, std::min(0.999f, currentCutoff));

    mSmoothedCutoff += 0.01f * (currentCutoff - mSmoothedCutoff);
    mSmoothedRes += 0.01f * (mResonance - mSmoothedRes);

    float targetFreq = 10.0f * powf(2000.0f, mSmoothedCutoff);
    targetFreq = std::min(targetFreq, sampleRate * 0.40f);

    mSvf.setParams(targetFreq, std::max(0.1f, mSmoothedRes * 3.5f), sampleRate);

    TSvf::Type type =
        (mMode == FilterLfoMode::LowPass) ? TSvf::LowPass : TSvf::HighPass;
    return mSvf.process(input, type);
  }

  void reset(float sampleRate) {
    mPhase = 0.0f;
    mSmoothedCutoff = mCutoff;
    mSmoothedRes = mResonance;
    mControlCounter = 0;
    mSvf.setParams(1000.0f, 0.7f, sampleRate);
  }

private:
  FilterLfoMode mMode;
  float mRate = 0.5f;
  float mDepth = 0.0f;
  float mShape = 0.0f;
  float mCutoff = 0.5f;
  float mResonance = 0.0f;
  bool mSync = false;
  float mBpm = 120.0f;

  float mPhase = 0.0f;
  unsigned int mNoiseSeed = 12345;
  float mNoiseSample = 0.0f;

  TSvf mSvf;
  float mSmoothedCutoff = 0.5f;
  float mSmoothedRes = 0.0f;
  float mTargetMod = 0.0f;
  uint32_t mControlCounter = 0;
};

#endif // FILTER_LFO_FX_H
