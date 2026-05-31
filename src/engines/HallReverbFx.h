#ifndef HALL_REVERB_FX_H
#define HALL_REVERB_FX_H

#include "../Utils.h"
#include <cmath>
#include <vector>

// Helper Classes
class CombFilter {
public:
  void setBufferSize(int size) {
    mBuffer.assign(size, 0.0f);
    mWritePos = 0;
  }

  float process(float input, float feedback, float damp) {
    float readVal = mBuffer[mWritePos];
    mFilterStore = fixDenormal((readVal * (1.0f - damp)) + (mFilterStore * damp));

    float output = readVal;
    float newVal = input + (mFilterStore * feedback);

    // Safety Saturation (prevent explosion)
    newVal = std::tanh(newVal);

    mBuffer[mWritePos] = newVal;
    mWritePos++;
    if (mWritePos >= mBuffer.size())
      mWritePos = 0;

    return output;
  }

  // Helper Helper
  void clear() {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
    mFilterStore = 0.0f;
  }

private:
  std::vector<float> mBuffer;
  int mWritePos = 0;
  float mFilterStore = 0.0f;
};

class AllPassFilter {
public:
  void setBufferSize(int size) {
    mBuffer.assign(size, 0.0f);
    mWritePos = 0;
  }

  void clear() {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
  }

  float process(float input) {
    float bufOut = fixDenormal(mBuffer[mWritePos]);

    float output = -input + bufOut;
    float newVal = input + (bufOut * 0.5f);

    // Safety Saturation
    newVal = std::tanh(newVal);

    mBuffer[mWritePos] = newVal;
    mWritePos++;
    if (mWritePos >= mBuffer.size())
      mWritePos = 0;

    return output;
  }

private:
  std::vector<float> mBuffer;
  int mWritePos = 0;
};

class HallReverbFx {
public:
  HallReverbFx() {
    // Standard Schroeder tunings (approx for 44.1k)
    // Scaled by sample rate in setParameters usually, but fixed for now
    mCombs[0].setBufferSize(1116);
    mCombs[1].setBufferSize(1188);
    mCombs[2].setBufferSize(1277);
    mCombs[3].setBufferSize(1356);

    mAllPass[0].setBufferSize(225);
    mAllPass[1].setBufferSize(556);
  }

  void clear() {
    for (int i = 0; i < 4; ++i)
      mCombs[i].clear();
    for (int i = 0; i < 2; ++i)
      mAllPass[i].clear();
  }

  // Initialize with SR support if needed, but fixed delays are often fine for
  // reverb character
  void setSampleRate(float sr) {
    float scale = sr / 44100.0f;
    mCombs[0].setBufferSize((int)(1116 * scale));
    mCombs[1].setBufferSize((int)(1188 * scale));
    mCombs[2].setBufferSize((int)(1277 * scale));
    mCombs[3].setBufferSize((int)(1356 * scale));
    mAllPass[0].setBufferSize((int)(225 * scale));
    mAllPass[1].setBufferSize((int)(556 * scale));
  }

  void setSize(float size) { mTargetSize = 0.7f + (size * 0.25f); }
  void setDamping(float damp) { mTargetDamp = damp * 0.4f; }
  void setMix(float mix) { mTargetMix = mix; }
  void setPreDelay(float val) { /* TODO: Implement PreDelay line */ }

  // Safe Parameter Setter
  void setParameters(float size, float damp, float mix) {
    setSize(size);
    setDamping(damp);
    setMix(mix);
    // Sync smoothed states on load to prevent slow ramp from 0
    mSmoothedSize = mTargetSize;
    mSmoothedDamp = mTargetDamp;
    mSmoothedMix = mTargetMix;
  }

  float process(float input) {
    // Smooth Params
    mSmoothedSize += 0.001f * (mTargetSize - mSmoothedSize);
    mSmoothedDamp += 0.001f * (mTargetDamp - mSmoothedDamp);
    mSmoothedMix += 0.001f * (mTargetMix - mSmoothedMix);

    if (mSmoothedMix <= 0.001f)
      return 0.0f;
    float out = 0.0f;
    for (int i = 0; i < 4; ++i) {
      out += mCombs[i].process(input, mSmoothedSize, mSmoothedDamp);
    }
    out = mAllPass[0].process(out);
    out = mAllPass[1].process(out);
    return out * mSmoothedMix * 0.3f;
  }

  void processStereoWet(float inL, float inR, float &outL, float &outR) {
    // Simple mono-summed reverb for now, can be improved to true stereo
    float input = (inL + inR) * 0.5f;
    float wet = process(input);
    outL = wet;
    outR = wet;
  }

private:
  float mTargetSize = 0.5f, mSmoothedSize = 0.5f;
  float mTargetDamp = 0.2f, mSmoothedDamp = 0.2f;
  float mTargetMix = 0.3f, mSmoothedMix = 0.3f;

  CombFilter mCombs[4];
  AllPassFilter mAllPass[2];
};

#endif // HALL_REVERB_FX_H
