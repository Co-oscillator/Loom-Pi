#ifndef TAPE_ECHO_FX_H
#define TAPE_ECHO_FX_H

#include "../Utils.h"
#include <algorithm>
#include <cmath>
#include <vector>

class TapeEchoFx {
public:
  TapeEchoFx() {
    mBuffer.resize(192000, 0.0f); // 4 sec at 48k
  }

  void clear() {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
    mFilterState = 0.0f;
    mSmoothedFeedback = mFeedback;
    mSmoothedSaturation = mSaturation;
    mSmoothedMix = mMix;
  }

  float process(float input, float sampleRate) {
    if (!std::isfinite(input))
      input = 0.0f;

    // Wow & Flutter LFOs (Use std::sin for smooth derivative/pitch)
    mWowPhase += 0.5f / sampleRate;
    if (mWowPhase > 6.283185f)
      mWowPhase -= 6.283185f;

    mFlutterPhase += 12.0f / sampleRate;
    if (mFlutterPhase > 6.283185f)
      mFlutterPhase -= 6.283185f;

    float modulation = (std::sin(mWowPhase) * mWowAmount) +
                       (std::sin(mFlutterPhase) * mFlutterAmount);

    float targetDelaySamples = (mTime + modulation * mTime) * sampleRate;

    if (!std::isfinite(targetDelaySamples))
      targetDelaySamples = mTime * sampleRate;

    float diff = targetDelaySamples - mSmoothedDelay;

    // Smooth approach first
    float step = 0.0002f * diff;

    // Slew Rate Limiter: Max 0.95 (0.05x speed). High limit allows manual play
    // but stops aliasing.
    float maxSlew = 0.95f;
    if (step > maxSlew)
      step = maxSlew;
    if (step < -maxSlew)
      step = -maxSlew;

    mSmoothedDelay += step;

    // Safety Clamps
    if (!std::isfinite(mSmoothedDelay))
      mSmoothedDelay = targetDelaySamples;
    if (mSmoothedDelay < 0.0f)
      mSmoothedDelay = 0.0f;

    // Read positions
    // Read position relative to most recent sample (mWritePos - 1)
    float readPos = (float)mWritePos - 1.0f - mSmoothedDelay;

    // Safety check for invalid Delay/LFO state
    if (!std::isfinite(readPos)) {
      readPos = (float)mWritePos - 1.0f;
    }

    // Safe Wrapping (avoid infinite while loops if readPos is extreme)
    float bufSizeVal = (float)mBuffer.size();
    readPos = fmodf(readPos, bufSizeVal);
    if (readPos < 0.0f)
      readPos += bufSizeVal;

    // Hermite Interpolation (4-point)
    int i1 = (int)readPos;
    int i2 = (i1 + 1) % mBuffer.size();
    int i3 = (i2 + 1) % mBuffer.size();
    int i0 = (i1 - 1 + mBuffer.size()) % mBuffer.size();

    float frac = readPos - (float)i1;

    float y0 = mBuffer[i0];
    float y1 = mBuffer[i1];
    float y2 = mBuffer[i2];
    float y3 = mBuffer[i3];

    float a = (3.0f * (y1 - y2) - y0 + y3) * 0.5f;
    float b = 2.0f * y2 + y0 - 5.0f * y1 * 0.5f - y3 * 0.5f;
    float c = (y2 - y0) * 0.5f;
    float d = y1;

    float echo = ((a * frac + b) * frac + c) * frac + d;
    if (!std::isfinite(echo))
      echo = 0.0f;

    // Smooth Parameters
    mSmoothedFeedback += 0.001f * (mFeedback - mSmoothedFeedback);
    mSmoothedSaturation += 0.001f * (mSaturation - mSmoothedSaturation);
    mSmoothedMix +=
        0.01f * (mMix - mSmoothedMix); // Faster smoothing for better response

    // Tape Saturation
    if (mSmoothedSaturation > 0.0f) {
      echo = fast_tanh(echo * (1.0f + mSmoothedSaturation * 4.0f));
    }

    float feedbackSig = echo * mSmoothedFeedback;
    // Low-pass to simulate tape head wear & prevent high-freq "zipper"
    // Denormal protection
    feedbackSig += 1.0e-15f;
    if (!std::isfinite(feedbackSig))
      feedbackSig = 0.0f;

    mFilterState += 0.05f * (feedbackSig - mFilterState);
    if (!std::isfinite(mFilterState))
      mFilterState = 0.0f;
    else if (std::abs(mFilterState) < 1.0e-15f)
      mFilterState = 0.0f;

    feedbackSig = mFilterState;

    float toWrite = input + feedbackSig;
    toWrite = fast_tanh(toWrite);

    // REMOVED: Aggressive zeroing caused "clamped" sound artifacts
    // toWrite += 1.0e-18f; // Removed redundant

    mBuffer[mWritePos] = toWrite + 1.0e-18f;
    mWritePos = (mWritePos + 1) % mBuffer.size();

    float output = (echo * mSmoothedMix);

    // Silence tracking
    if (std::abs(output) < 1e-9f) {
      if (mSilentCounter < 48000)
        mSilentCounter++;
    } else {
      mSilentCounter = 0;
    }

    float wet = output;
    if (!std::isfinite(wet))
      wet = 0.0f;
    float dry = input * (1.0f - mSmoothedMix);
    if (mSmoothedMix > 0.999f)
      dry = 0.0f; // Force dry kill at max mix
    return dry + wet;
  }

  bool isSilent() const { return mSilentCounter >= 48000; }

  void setParameters(float time, float feedback, float saturation, float mix) {
    setDelayTime(time);
    setFeedback(feedback);
    setDrive(saturation);
    setMix(mix);
    // ... rest of params
  }

  void setDelayTime(float v) {
    mTime = 0.05f + (v * v) * 2.95f;
  } // Up to 3.0s (0.05 + 2.95)

  void setFeedback(float v) {
    mFeedback = v * 0.95f;
  } // Increased range allow nearly full feedback
  void setWow(float v) { mWowAmount = v * 0.006f; }
  void setFlutter(float v) { mFlutterAmount = v * 0.003f; }
  // Rescaled Drive: Max (1.0) now equals previous 0.2
  void setDrive(float v) { mSaturation = v * 0.2f; }
  void setMix(float v) { mMix = v; }

private:
  std::vector<float> mBuffer;
  int mWritePos = 0;
  float mSmoothedDelay = 1000.0f;
  float mWowPhase = 0.0f;
  float mFlutterPhase = 0.0f;
  float mFilterState = 0.0f;

  float mTime = 0.3f;
  float mFeedback = 0.4f;
  float mSmoothedFeedback = 0.4f;
  float mSaturation = 0.0f;
  float mSmoothedSaturation = 0.0f;
  float mMix = 0.3f;
  float mSmoothedMix = 0.3f;

  float mWowAmount = 0.002f;
  float mFlutterAmount = 0.0005f;
  uint32_t mSilentCounter = 48000;
};

#endif
