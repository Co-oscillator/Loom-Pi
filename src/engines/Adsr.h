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

    // Time-based envelope calculation (parameters a, d, r in seconds, up to 15.0s max)
    float safeA = std::min(15.0f, std::max(0.0005f, mAttack));
    float safeD = std::min(15.0f, std::max(0.001f, mDecay));
    float safeR = std::min(15.0f, std::max(0.001f, mRelease));

    // Linear attack: reaches 1.0 in safeA seconds
    mAttackTotalSamples = (uint32_t)(safeA * mSampleRate);
    if (mAttackTotalSamples < 1) mAttackTotalSamples = 1;
    mAttackRate = 1.0f / (float)mAttackTotalSamples;

    // Exponential decay towards sustain: drops 99.9% (-60dB) of the delta in safeD seconds
    mDecayCoeff = expf(-6.908f / (safeD * mSampleRate));

    // Exponential release: drops to 0.0001 (-80dB / silence) in safeR seconds
    mReleaseCoeff = expf(-9.210f / (safeR * mSampleRate));
  }

  void trigger() {
    mStage = AdsrStage::Attack;
    mAttackSample = 0;
    mValue = 0.0f;
  }

  void release() {
    if (mStage != AdsrStage::Idle) {
      mStage = AdsrStage::Release;
    }
  }

  void reset() {
    mStage = AdsrStage::Idle;
    mAttackSample = 0;
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
      mAttackSample++;
      mValue = (float)mAttackSample * mAttackRate;
      if (mAttackSample >= mAttackTotalSamples) {
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
  uint32_t mAttackTotalSamples = 480;
  uint32_t mAttackSample = 0;

  float mValue = 0.0f;
  AdsrStage mStage = AdsrStage::Idle;
};

#endif
