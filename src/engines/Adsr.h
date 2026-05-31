#ifndef ADSR_H
#define ADSR_H

#include <cmath>
#include "../Utils.h"

enum class AdsrStage { Idle, Attack, Decay, Sustain, Release };

class Adsr {
public:
  void setSampleRate(float sr) { mSampleRate = sr; }
  void setParameters(float a, float d, float s, float r) {
    mAttack = a;
    mDecay = d;
    mSustain = s;
    mRelease = r;
    // Pre-calculate coefficients could be done here for optimization,
    // but calculating per-sample is okay for now or valid in nextValue with
    // pow/exp approximations. For simple exp decay: val *= coeff.
    // Cubic curve for fine control at low values
    float aCurve = mAttack * mAttack * mAttack;
    float dCurve = mDecay * mDecay * mDecay;
    float rCurve = mRelease * mRelease * mRelease;

    // Max times: Attack 2s, Decay 3s, Release 3s
    mDecayCoeff = exp(-1.0f / (dCurve * mSampleRate * 3.0f + 1.0f));
    mReleaseCoeff = exp(-1.0f / (rCurve * mSampleRate * 3.0f + 1.0f));
    mAttackRate = 1.0f / (aCurve * mSampleRate * 2.0f + 1.0f);
  }

  void trigger() { mStage = AdsrStage::Attack; }

  void release() {
    if (mStage != AdsrStage::Idle) {
      mStage = AdsrStage::Release;
    }
  }

  void reset() {
    mStage = AdsrStage::Idle;
    mValue = 0.0f;
  }

  void forceSustain() {
    mStage = AdsrStage::Sustain;
    mValue = 1.0f;
  }

  float nextValue() {
    switch (mStage) {
    case AdsrStage::Idle:
      return 0.0f;
    case AdsrStage::Attack:
      mValue += mAttackRate;
      if (mValue >= 1.0f) {
        mValue = 1.0f;
        mStage = AdsrStage::Decay;
      }
      break;
    case AdsrStage::Decay:
      // Exponential Decay towards Sustain level: current = target + (current -
      // target) * coeff
      mValue = mSustain + (mValue - mSustain) * mDecayCoeff;

      if (std::abs(mValue - mSustain) < 0.0001f || mValue <= mSustain) {
        mValue = mSustain;
        mStage = AdsrStage::Sustain;
      }
      break;
    case AdsrStage::Sustain:
      mValue = mSustain;
      break;
    case AdsrStage::Release:
      mValue *= mReleaseCoeff;
      if (mValue < 0.0001f) {
        mValue = 0.0f;
        mStage = AdsrStage::Idle;
      }
      break;
    }
    mValue = fixDenormal(mValue);
    return mValue;
  }

  void processBlock(int blockSize, float& outStartValue, float& outDelta) {
    outStartValue = mValue;
    for(int i = 0; i < blockSize; ++i) {
      nextValue();
    }
    outDelta = (mValue - outStartValue) / (float)blockSize;
  }

  bool isActive() const { return mStage != AdsrStage::Idle; }
  float getValue() const { return mValue; }
  AdsrStage getStage() const { return mStage; }

private:
  float mSampleRate = 48000.0f;
  float mAttack = 0.01f, mDecay = 0.1f, mSustain = 0.8f, mRelease = 0.5f;

  float mDecayCoeff = 0.999f;
  float mReleaseCoeff = 0.999f;
  float mAttackRate = 0.01f;

  float mValue = 0.0f;
  AdsrStage mStage = AdsrStage::Idle;
};

#endif
