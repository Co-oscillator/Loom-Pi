#ifndef OVERDRIVE_FX_H
#define OVERDRIVE_FX_H

#include <algorithm>
#include <cmath>

class OverdriveFx {
public:
  void setParameters(float drive, float tone, float level) {
    mDrive = drive * 10.0f + 1.0f; // Range 1.0 to 11.0
    mTone = tone;
    mLevel = level;
  }
  void setDrive(float drive) { mDrive = drive * 10.0f + 1.0f; }
  void setTone(float tone) { mTone = tone; }
  void setLevel(float level) { mLevel = level; }
  void setMix(float mix) { mMix = mix; }

  float process(float input) {
    // 1. Tighten Bass (150Hz HP)
    mHpState += 0.15f * (input - mHpState);
    float x = (input - mHpState) * mDrive;

    // 2. Extra Distortion Stage (New)
    if (mDist > 0.0f) {
      // Hard clipping / Folding
      x *= (1.0f + mDist * 5.0f);
      if (std::abs(x) > 1.0f) {
        float overflow = std::abs(x) - 1.0f;
        x = (x > 0 ? 1.0f : -1.0f) - (overflow * 0.5f); // Fold back
      }
    }

    // 3. Multi-stage Clipping & Wavefolding for "Texture"
    float stage1 = (x > 0) ? std::tanh(x) : (x / (1.0f - x)); // Asymmetric

    // Wavefolder component for "grit"
    float grit = 0.0f;
    if (std::abs(stage1) > 0.6f) {
      grit = std::sin(stage1 * 4.0f) * 0.2f * mDrive * 0.1f;
    }
    float mixed = stage1 + grit;

    // Dynamic Low Pass (Tone)
    float lpAlpha = 0.05f + mTone * 0.6f;
    mLastOutput += lpAlpha * (mixed - mLastOutput);

    // Output Level + Strong Boost for volume parity
    float out = mLastOutput * mLevel * 2.1f * mMix;
    // RETURNS (WET - INPUT) for Insert Behavior in Parallel Chain
    return std::tanh(out) - input;
  }

  void setDistortion(float dist) { mDist = dist; }

private:
  float mDrive = 1.0f;
  float mTone = 0.5f;
  float mLevel = 0.8f;
  float mLastOutput = 0.0f;
  float mHpState = 0.0f;
  float mMix = 1.0f;
  float mDist = 0.0f;
};

#endif // OVERDRIVE_FX_H
