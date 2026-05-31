#ifndef STEREO_SPREAD_FX_H
#define STEREO_SPREAD_FX_H

#include <cmath>
#include <vector>

class StereoSpreadFx {
public:
  StereoSpreadFx() {
    // Short delays for Haas effect / widening
    mDelayL.resize(2048, 0.0f);
    mDelayR.resize(2048, 0.0f);
  }

  // Process Stereo Input (or Mono -> Stereo)
  // The engine's FX buses are Mono currently (float array).
  // But render loop output is going to stereo.
  // If we process a global FX bus, we return a value that gets summed?
  // AudioEngine sums `wetSample` to L and R.
  // If we want Stereo Spread, we need to return L and R separately usually.
  // BUT, the current loop sums `wetSample` to BOTH `outL` and `outR`
  // identically or assumes mono mix. AudioEngine.cpp: float outL = wetSample;
  // float outR = wetSample; To implement TRUE Stereo Spread, AudioEngine's FX
  // processing needs to support Stereo Return. For now, let's implement the
  // class, and we'll hack AudioEngine to use it properly.

  void process(float input, float &outL, float &outR, float sampleRate) {
    if (mMix <= 0.001f) {
      outL = 0.0f;
      outR = 0.0f;
      return;
    }

    // LFO
    mPhase += mRate / sampleRate;
    if (mPhase >= 1.0f)
      mPhase -= 1.0f;
    float lfo = std::sin(2.0f * 3.14159265f * mPhase);

    // Modulate Delay Times inversely for widening
    float baseDelay = 0.010f; // 10ms
    float mod = mDepth * mWidth * 0.005f;

    float dL = baseDelay + mod * lfo;
    float dR = baseDelay - mod * lfo; // Inverse modulation

    // Delay L
    float sampL = readDelay(mDelayL, mPosL, dL * sampleRate);
    mDelayL[mPosL] = input;
    mPosL++;
    if (mPosL >= mDelayL.size())
      mPosL = 0;

    // Delay R
    float sampR = readDelay(mDelayR, mPosR, dR * sampleRate);
    mDelayR[mPosR] = input;
    mPosR++;
    if (mPosR >= mDelayR.size())
      mPosR = 0;

    // Output - purely wet signal?
    // If this is a bus, we want the "spread" effect.
    // Usually spread is added to dry.
    // Output - Blend Dry and Wet
    // Mix 0.0 = All Dry, 1.0 = All Wet (Spread)
    outL = (input * (1.0f - mMix)) + (sampL * mMix);
    outR = (input * (1.0f - mMix)) + (sampR * mMix);
  }

  void setParameters(float width, float rate, float depth, float mix) {
    setWidth(width);
    setRate(rate);
    setDepth(depth);
    setMix(mix);
  }

  void setWidth(float v) { mWidth = v; }
  void setRate(float v) { mRate = 0.1f + v * 2.0f; }
  void setDepth(float v) { mDepth = v; }
  void setMix(float v) { mMix = v; }

private:
  float readDelay(const std::vector<float> &buf, int writePos,
                  float delaySamples) {
    float readPos = (float)writePos - delaySamples;
    while (readPos < 0.0f)
      readPos += buf.size();

    int i0 = (int)readPos;
    int i1 = i0 + 1;
    if (i1 >= buf.size())
      i1 = 0;
    float frac = readPos - i0;
    return buf[i0] * (1.0f - frac) + buf[i1] * frac;
  }

  std::vector<float> mDelayL, mDelayR;
  int mPosL = 0, mPosR = 0;
  float mPhase = 0.0f;

  float mWidth = 1.0f;
  float mRate = 0.5f;
  float mDepth = 0.5f;
  float mMix = 0.0f;
};

#endif
