#ifndef CHORUS_FX_H
#define CHORUS_FX_H

#include <cmath>
#include <vector>

class ChorusFx {
public:
  ChorusFx(int maxDelay = 4096) { mBuffer.resize(maxDelay, 0.0f); }

  void setRate(float v) { mRate = v; }
  void setDepth(float v) { mDepth = v; }
  void setMix(float v) { mMix = v; }
  void setVoices(float v) {
    mVoices = 1 + (int)(v * 6.0f); // 1 to 7 voices
  }

  void setParameters(float rate, float depth, float mix, int voices) {
    mRate = rate;
    mDepth = depth;
    mMix = mix;
    mVoices = voices;
    if (mVoices < 1)
      mVoices = 1;
    if (mVoices > 7)
      mVoices = 7;
  }

  void clear() {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
    mPhase = 0.0f;
    mHpState = 0.0f;
  }

  float process(float input, float sampleRate) {
    mPhase += (2.0f * M_PI * mRate) / sampleRate;
    if (mPhase > 2.0f * M_PI)
      mPhase -= 2.0f * M_PI;

    float wetSignal = 0.0f;
    // Spread voices across phase
    float phaseOffsetStep = (2.0f * M_PI) / (float)mVoices;

    for (int v = 0; v < mVoices; ++v) {
      float vPhase = mPhase + (v * phaseOffsetStep);
      // Modulate delay time between 10ms and 30ms with offset per voice
      // Enhanced Depth: modulate slightly wider
      float delaySamples =
          (25.0f + sinf(vPhase) * mDepth * 15.0f) * (sampleRate / 1000.0f);

      float tap = getInterpolatedTap(delaySamples);
      wetSignal += tap;
    }

    // Normalize wet signal
    wetSignal /= (float)mVoices;

    // Character: High Pass Wet Signal to remove muddiness
    mHpState += 0.05f * (wetSignal - mHpState);
    wetSignal = wetSignal - mHpState;

    // Character: Warmth (Saturation on Wet)
    wetSignal = std::tanh(wetSignal * 1.5f);

    mBuffer[mWritePos] = input;
    mWritePos = (mWritePos + 1) % mBuffer.size();

    return std::tanh(wetSignal) * mMix; // Wet Only for Parallel Mix
  }

private:
  float getInterpolatedTap(float delaySamples) {
    float readPos = (float)mWritePos - delaySamples;
    while (readPos < 0)
      readPos += mBuffer.size();
    while (readPos >= mBuffer.size())
      readPos -= mBuffer.size();

    int i1 = (int)readPos;
    int i2 = (i1 + 1) % mBuffer.size();
    float frac = readPos - i1;

    return mBuffer[i1] * (1.0f - frac) + mBuffer[i2] * frac;
  }

  std::vector<float> mBuffer;
  int mWritePos = 0;
  float mPhase = 0.0f;
  float mRate = 1.0f;
  float mDepth = 0.5f;
  float mMix = 0.5f;
  int mVoices = 3;
  float mHpState = 0.0f;
};

#endif // CHORUS_FX_H
