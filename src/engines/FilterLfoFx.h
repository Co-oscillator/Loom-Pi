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
