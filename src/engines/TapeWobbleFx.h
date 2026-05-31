#ifndef TAPE_WOBBLE_FX_H
#define TAPE_WOBBLE_FX_H

#include <cmath>
#include <random>
#include <vector>

class TapeWobbleFx {
public:
  TapeWobbleFx(int maxDelay = 8192) {
    mBufferL.resize(maxDelay, 0.0f);
    mBufferR.resize(maxDelay, 0.0f);
    mRandEngine.seed(std::random_device{}());
    mDist = std::uniform_real_distribution<float>(-0.2f, 0.2f);
  }

  void setRate(float v) { mRate = v; }
  void setDepth(float v) { mDepth = v; }
  void setSaturation(float v) { mSaturation = v; }
  void setMix(float v) { mMix = v; }

  void setParameters(float rate, float depth, float saturation, float mix) {
    mRate = rate;
    mDepth = depth;
    mSaturation = saturation;
    mMix = mix;
  }

  void clear() {
    std::fill(mBufferL.begin(), mBufferL.end(), 0.0f);
    std::fill(mBufferR.begin(), mBufferR.end(), 0.0f);
    mWritePos = 0;
    mPhase = 0.0f;
  }

  // Process stereo block (linked wobble)
  void processStereo(float inL, float inR, float &outL, float &outR,
                     float sampleRate) {
    // Shared modulation update (once per stereo pair)
    mPhase += (2.0f * M_PI * mRate) / sampleRate;
    if (mPhase > 2.0f * M_PI) {
      mPhase -= 2.0f * M_PI;
      mRandomOffset = mDist(mRandEngine);
    }

    float mod = sinf(mPhase + mRandomOffset);
    float targetDelay = (10.0f + mod * mDepth * 8.0f);
    // Smoother transition: 0.005 -> 0.0005 (reduce crackle)
    mSmoothedDelay += 0.0005f * (targetDelay - mSmoothedDelay);
    float delaySamples = mSmoothedDelay * (sampleRate / 1000.0f);

    float tapL = getInterpolatedTap(mBufferL, mWritePos, delaySamples);
    float tapR = getInterpolatedTap(mBufferR, mWritePos,
                                    delaySamples); // Same delay samples

    if (mSaturation > 0.0f) {
      float drive = 1.0f + mSaturation * 3.0f;
      tapL = std::tanh(tapL * drive) / std::tanh(drive);
      tapR = std::tanh(tapR * drive) / std::tanh(drive);
    }

    mBufferL[mWritePos] = inL;
    mBufferR[mWritePos] = inR;
    mWritePos = (mWritePos + 1) % mBufferL.size();

    float wetL = inL * (1.0f - mMix) + tapL * mMix;
    float wetR = inR * (1.0f - mMix) + tapR * mMix;

    // Return delta (Wet - Dry) for additive mixer
    outL = wetL - inL;
    outR = wetR - inR;
  }

  // Mono fallback (unused but kept for API compat if needed)
  float process(float input, float sampleRate) {
    // Not safe to use if interleaved with processStereo due to phase updates
    return 0.0f;
  }

private:
  float getInterpolatedTap(const std::vector<float> &buffer, int writePos,
                           float delaySamples) {
    float readPos = (float)writePos - delaySamples;
    while (readPos < 0)
      readPos += buffer.size();
    while (readPos >= buffer.size())
      readPos -= buffer.size();

    int i1 = (int)readPos;
    int i2 = (i1 + 1) % buffer.size();
    float frac = readPos - i1;
    return buffer[i1] * (1.0f - frac) + buffer[i2] * frac;
  }

  std::vector<float> mBufferL;
  std::vector<float> mBufferR;
  int mWritePos = 0;
  float mPhase = 0.0f;
  float mRate = 0.5f;
  float mDepth = 0.5f;
  float mSaturation = 0.0f;
  float mMix = 0.5f;
  float mSmoothedDelay = 10.0f;
  float mRandomOffset = 0.0f;
  std::mt19937 mRandEngine;
  std::uniform_real_distribution<float> mDist;
};

#endif // TAPE_WOBBLE_FX_H
