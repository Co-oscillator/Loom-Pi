#ifndef FLANGER_FX_H
#define FLANGER_FX_H

#include <algorithm>
#include <cmath>
#include <vector>

class FlangerFx {
public:
  FlangerFx() {
    mBuffer.resize(44100, 0.0f); // 1 sec buffer
  }

  float process(float input, float sampleRate) {
    if (mMix <= 0.001f)
      return 0.0f; // Return 0 if not active (send effect logic)
    // Actually, if it's an insert, we might want to return input, but this is a
    // send bus usually. Wait, current FX are parallel sends. So returning 0
    // means "no effect signal".

    // LFO
    mPhase += mRate / sampleRate;
    if (mPhase >= 1.0f)
      mPhase -= 1.0f;

    float lfoVal =
        0.5f * (1.0f + std::sin(2.0f * 3.14159265f * mPhase)); // 0..1

    // Delay Time
    float minDelay = mBaseDelay;
    float maxDelay = mBaseDelay + 0.006f;
    float currentDelay = minDelay + (maxDelay - minDelay) * mDepth * lfoVal;

    float delaySamples = currentDelay * sampleRate;

    // Read Pointer
    float readPos = (float)mWritePos - delaySamples;
    while (readPos < 0.0f)
      readPos += mBuffer.size();
    while (readPos >= mBuffer.size())
      readPos -= mBuffer.size();

    int i0 = (int)readPos;
    int i1 = i0 + 1;
    if (i1 >= mBuffer.size())
      i1 = 0;
    float frac = readPos - i0;

    float delayed = mBuffer[i0] * (1.0f - frac) + mBuffer[i1] * frac;

    // Feedback
    float feedback = delayed * mFeedback;

    // Write w/ Saturation to prevent explosion
    float toWrite = input + std::tanh(feedback);
    if (std::abs(toWrite) < 1.0e-15f)
      toWrite = 0.0f;
    mBuffer[mWritePos] = toWrite;
    mWritePos++;
    if (mWritePos >= mBuffer.size())
      mWritePos = 0;

    return delayed * mMix;
  }

  void setParameters(float rate, float depth, float feedback, float mix) {
    setRate(rate);
    setDepth(depth);
    setFeedback(feedback);
    setMix(mix);
  }

  void clear() {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
    mPhase = 0.0f;
  }

  void setRate(float v) { mRate = 0.05f + (v * v * v) * 5.0f; }
  void setDepth(float v) { mDepth = v; }
  void setFeedback(float v) { mFeedback = v * 0.95f; }
  void setDelay(float v) {
    mBaseDelay = 0.001f + v * 0.010f;
  } // 1ms - 11ms window
  void setMix(float v) { mMix = v; }

private:
  std::vector<float> mBuffer;
  int mWritePos = 0;
  float mPhase = 0.0f;

  float mRate = 0.5f;
  float mDepth = 0.8f;
  float mFeedback = 0.5f;
  float mMix = 0.0f;
  float mBaseDelay = 0.001f;
};

#endif
