#include "../Utils.h"
#include "Adsr.h"
#include "Eq5BandFx.h"
#include <algorithm>
#include <vector>

class AudioInEngine {
public:
  struct Voice {
    bool active = false;
    Adsr ampEnv;
    Adsr filterEnv;
    TSvf svf;

    void reset() {
      active = false;
      ampEnv.reset();
      filterEnv.reset();
      svf.setParams(1000.0f, 0.7f, 48000.0f);
    }
  };

  AudioInEngine() {
    mVoices.resize(1); // Mono "oscillator" style for now
    mVoices[0].reset();
  }

  void resetToDefaults() {
    mGated = true;
    mGain = 1.0f;
    mCutoff = 1.0f;
    mResonance = 0.0f;
    mFilterAmt = 0.0f;
    mWavefold = 0.0f;
    mAttack = 0.01f;
    mDecay = 0.1f;
    mSustain = 1.0f;
    mRelease = 0.1f;
    mF_Atk = 0.01f;
    mF_Dcy = 0.1f;
    mF_Sus = 1.0f;
    mF_Rel = 0.1f;
  }

  void setSampleRate(float sr) {
    mSampleRate = sr;
    for (auto &v : mVoices) {
      v.ampEnv.setSampleRate(sr);
      v.filterEnv.setSampleRate(sr);
    }
  }

  void triggerNote(int note, int velocity) {
    mVoices[0].active = true;
    mVoices[0].ampEnv.setParameters(mAttack, mDecay, mSustain, mRelease);
    mVoices[0].filterEnv.setParameters(mF_Atk, mF_Dcy, mF_Sus, mF_Rel);
    mVoices[0].ampEnv.trigger();
    mVoices[0].filterEnv.trigger();
  }

  void releaseNote(int note) {
    mVoices[0].ampEnv.release();
    mVoices[0].filterEnv.release();
  }

  void setParameter(int id, float value) {
    switch (id) {
    case 100:
      mAttack = value;
      break;
    case 101:
      mDecay = value;
      break;
    case 102:
      mSustain = value;
      break;
    case 103:
      mRelease = value;
      break;
    case 112:
      mCutoff = value;
      break;
    case 113:
      mResonance = value;
      break;
    case 114:
      mF_Atk = value;
      break;
    case 115:
      mF_Dcy = value;
      break;
    case 116:
      mF_Sus = value;
      break;
    case 117:
      mF_Rel = value;
      break;
    case 118:
      mFilterAmt = value;
      break;
    case 120:
      mGated = (value < 0.5f); // 0=Gated (Default), 1=Open
      break;
    case 121:
      mGain = value;
      break;
    case 122:
      mWavefold = value;
      break;
    case 123:
      mFilterMode = (int)(value * 2.9f); // 0=LP, 1=HP, 2=BP
      break;
    case 1530:
    case 1531:
    case 1532:
    case 1533:
    case 1534:
      mEq.setBandGain(id - 1530, (value - 0.5f) * 24.0f); // +/- 12dB
      break;
    }
  }

  void renderBlock(float* outL, float* outR, int numFrames, float* inBuffer = nullptr) {
    for (int i = 0; i < numFrames; ++i) {
      float s = render(inBuffer ? inBuffer[i] : 0.0f);
      outL[i] = s;
      outR[i] = s;
    }
  }

  float render(float inputSample) {
    if (mSampleRate <= 0.0f || !std::isfinite(mSampleRate))
      return inputSample;
    Voice &v = mVoices[0];

    // DC Blocker (Simple One-Pole High-Pass at ~10Hz)
    // y[n] = x[n] - x[n-1] + R * y[n-1]
    float x = inputSample;
    float y = x - mLastX + 0.999f * mLastY;
    mLastX = x;
    mLastY = y;
    float dcBlocked = y;

    float env = 1.0f;
    float fEnv = 0.0f;

    if (mGated) {
      env = v.ampEnv.nextValue();
      fEnv = v.filterEnv.nextValue();
      if (!v.ampEnv.isActive()) {
        v.active = false;
        // Clear filter state when inactive to prevent denormal/resonance
        // build-up
        v.svf.setParams(1000.0f, 0.7f, 48000.0f); // Default safe point
        return 0.0f;
      }
    } else {
      // OPEN MODE:
      // 1. Bypass Amp Envelope entirely (Unity Gain env)
      env = 1.0f;
      // 2. Bypass Filter Envelope modulation (Static Filter)
      //    User wants input -> Filter -> FX without trigger.
      //    If we set fEnv = 1.0, the Cutoff is modulated to max (or min if amt
      //    negative). If we set fEnv = 0.0, the Cutoff is set purely by the
      //    Cutoff Knob. This matches standard "Gate Open" behavior on synths.
      fEnv = 0.0f;
    }

    float out = fast_tanh(dcBlocked * mGain * env);

    // Wavefolding (Iterative folding)
    if (mWavefold > 0.001f) {
      float foldAmount = 1.0f + mWavefold * 10.0f;
      out *= foldAmount;
      // Simple folding: if > 1, fold back (reflected)
      // Iterative approach
      for (int i = 0; i < 3; ++i) { // Limit iterations for performance
        if (out > 1.0f)
          out = 2.0f - out;
        else if (out < -1.0f)
          out = -2.0f - out;
        else
          break;
      }
      out = fast_tanh(out);
    }

    // Stable TSvf Filter
    float cutoffNormalized = mCutoff + (fEnv * mFilterAmt);
    cutoffNormalized = std::max(0.001f, std::min(0.999f, cutoffNormalized));

    // Exponential mapping
    float low = 20.0f;
    float high = std::min(mSampleRate * 0.49f, 20000.0f);
    float freq = low * powf(high / low, cutoffNormalized);

    float resonance = std::max(0.1f, mResonance);
    v.svf.setParams(freq, resonance, mSampleRate);

    TSvf::Type type = TSvf::LowPass;
    if (mFilterMode == 1)
      type = TSvf::HighPass;
    else if (mFilterMode == 2)
      type = TSvf::BandPass;

    float filtered = v.svf.process(out, type);

    // Apply 5-Band EQ
    float eqOut = mEq.process(filtered, mSampleRate);

    // Use fast_tanh on output for safety and extra gain
    return fast_tanh(eqOut * 1.2f);
  }

  float getEnvelopeValue() const {
    if (mVoices.empty())
      return 0.0f;
    return mVoices[0].ampEnv.getValue();
  }

private:
  std::vector<Voice> mVoices;
  float mSampleRate = 48000.0f;
  float mLastX = 0.0f;
  float mLastY = 0.0f;
  bool mGated = true;
  float mGain = 1.0f;
  float mWavefold = 0.0f;
  float mCutoff = 1.0f, mResonance = 0.0f, mFilterAmt = 0.0f;
  int mFilterMode = 0; // 0=LP, 1=HP, 2=BP
  float mAttack = 0.01f, mDecay = 0.1f, mSustain = 1.0f, mRelease = 0.1f;
  float mF_Atk = 0.01f, mF_Dcy = 0.1f, mF_Sus = 1.0f, mF_Rel = 0.1f;
  Eq5BandFx mEq;
};
