#ifndef FM_ENGINE_H
#define FM_ENGINE_H

#include "../Utils.h"
#include "Adsr.h"
#include <algorithm>

#include <cmath>
#include <memory>

#include <vector>
#include <string>
#include <fstream>
#include <dirent.h>
#include <cstdio>


class FmOperator {
public:
  FmOperator() : mPhase(0), mPhaseInc(0) { mEnvelope.reset(); }

  void setFrequency(float baseFreq, float ratio, float sampleRate) {
    mPhaseInc = (uint32_t)(((baseFreq * ratio) / sampleRate) * 4294967296.0);
  }

  void setADSR(float a, float d, float s, float r) {
    mEnvelope.setParameters(a, d, s, r);
  }

  void setSampleRate(float sr) { mEnvelope.setSampleRate(sr); }
  void setUseEnvelope(bool use) { mUseEnvelope = use; }

  void processBlock() {
    mEnvelope.processBlock(16, mEnvStart, mEnvDelta);
  }

  float nextSample(float modulation, float pitchMod, int blockPhase) {
    mPhase += (uint32_t)(mPhaseInc * pitchMod);
    uint32_t modInt = (uint32_t)(int32_t)(modulation * 4294967296.0);
    float out = FastSine::getInt(mPhase + modInt);
    return out * (mUseEnvelope ? (mEnvStart + mEnvDelta * blockPhase) : 1.0f);
  }

  void trigger() {
    mPhase = 0;
    mEnvelope.trigger();
  }

  void release() { mEnvelope.release(); }
  bool isActive() const { return mEnvelope.isActive(); }

private:
  uint32_t mPhase;
  uint32_t mPhaseInc;
  Adsr mEnvelope;
  float mEnvStart = 0.0f;
  float mEnvDelta = 0.0f;
  bool mUseEnvelope = true;
};

class FmEngine {
public:
  struct CustomPreset {
    std::string name;
    int algorithm = 1;
    float cutoff = 1.0f;
    float resonance = 0.0f;
    int carrierMask = 0;
    int activeMask = 0;
    float feedback = 0.0f;
    float feedbackDrive = 0.0f;
    float brightness = 0.5f;
    float glide = 0.0f;
    float detune = 0.0f;
    int filterMode = 0;
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.8f;
    float release = 0.5f;
    float filterAttack = 0.01f;
    float filterDecay = 0.1f;
    float filterSustain = 1.0f;
    float filterRelease = 0.2f;
    float filterEnvAmount = 0.0f;
    float opLevels[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float opRatios[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float opAttack[6] = {0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.01f};
    float opDecay[6] = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
    float opSustain[6] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
    float opRelease[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  };

  std::vector<CustomPreset> mCustomPresets;

  struct Voice {
    bool active = false;
    int note = -1;
    float frequency = 440.0f;
    float targetFrequency = 440.0f;
    float amplitude = 1.0f;
    FmOperator operators[6];
    TSvf svf;
    float lastOp5Out = 0.0f;
    float op5FeedbackHistory = 0.0f;
    float pitchEnv = 1.0f;
    float pitchEnvDecay = 0.001f;
    Adsr masterEnv;
    Adsr filterEnv;
    float currentFilterEnvVal = 0.0f;
    float filterEnvStart = 0.0f, filterEnvDelta = 0.0f;
    uint32_t controlCounter = 0;
    float masterEnvStart = 0.0f, masterEnvDelta = 0.0f;

    void reset() {
      active = false;
      note = -1;
      frequency = 440.0f;
      targetFrequency = 440.0f;
      for (auto &op : operators) {
        op.setUseEnvelope(true);
      }
      lastOp5Out = 0.0f;
      op5FeedbackHistory = 0.0f;
      masterEnv.reset();
      filterEnv.reset();
      currentFilterEnvVal = 0.0f;
      controlCounter = 0;
    }
  };

  FmEngine() {
    mVoices.resize(16);
    mOpLevels.assign(6, 0.5f);
    mOpRatios.assign(6, 1.0f);
    mOpAttack.assign(6, 0.01f);
    mOpDecay.assign(6, 0.1f);
    mOpSustain.assign(6, 0.8f);
    mOpRelease.assign(6, 0.5f);
    resetToDefaults();
  }

  void resetToDefaults() {
    mAlgorithm = 0;
    mFeedback = 0.0f;
    mCutoff = 1.0f;
    mResonance = 0.0f;
    mBrightness = 1.0f;
    mDetune = 0.0f;
    mFeedbackDrive = 0.0f;
    mCarrierMask = 1;
    mActiveMask = 63;
    mAttack = 0.01f;
    mDecay = 0.1f;
    mSustain = 1.0f;
    mRelease = 0.2f;
  }

  void setSampleRate(float sr) {
    mSampleRate = sr;
    for (auto &v : mVoices) {
      for (auto &op : v.operators)
        op.setSampleRate(sr);
      v.masterEnv.setSampleRate(sr);
    }
  }

  void updateSampleRate(float sr) { setSampleRate(sr); }

  void allNotesOff() {
    for (auto &v : mVoices) {
      v.active = false;
      v.masterEnv.reset();
    }
  }

  void setAlgorithm(int algo) { mAlgorithm = std::max(0, std::min(31, algo)); }

  void setFilter(float v) { mCutoff = v; }
  void setResonance(float v) { mResonance = v; }
  void setUseEnvelope(bool v) { mUseEnvelope = v; }

  void setCarrierMask(int mask) { mCarrierMask = mask; }
  void setIgnoreNoteFrequency(bool ignore) { mIgnoreNoteFrequency = ignore; }

  void setFrequency(float freq, float sampleRate) {
    mSampleRate = sampleRate;
    mFrequency = freq;
  }
  void setGlide(float g) { mGlide = g; }

  void setOpRatio(int op, float ratio) {
    if (op >= 0 && op < 6)
      mOpRatios[op] = ratio;
  }
  void setOpLevel(int op, float level) {
    if (op >= 0 && op < 6)
      mOpLevels[op] = level;
  }
  float getOpLevel(int op) const {
    if (op >= 0 && op < 6)
      return mOpLevels[op];
    return 0.0f;
  }
  void setOpADSR(int op, float a, float d, float s, float r) {
    if (op >= 0 && op < 6) {
      mOpAttack[op] = a;
      mOpDecay[op] = d;
      mOpSustain[op] = s;
      mOpRelease[op] = r;
    }
  }
  void setFeedback(float fb) { mFeedback = fb; }
  void setPitchSweep(float sweep) { mPitchSweepAmount = sweep; }
  void setPitchBend(float bend) { mPitchBend = bend; }

  // Getters for UI Sync
  int getAlgorithm() const { return mAlgorithm; }
  float getCutoff() const { return mCutoff; }
  float getResonance() const { return mResonance; }
  int getCarrierMask() const { return mCarrierMask; }
  float getFeedback() const { return mFeedback; }
  int getActiveMask() const { return mActiveMask; }
  int getFilterMode() const { return mFilterMode; }
  float getBrightness() const {
    return mBrightness * 0.5f;
  } // Normalized back from *2
  float getDetune() const { return mDetune; }
  float getFeedbackDrive() const { return mFeedbackDrive; }

  // Op Getters
  float getOpRatio(int op) const {
    return (op >= 0 && op < 6) ? mOpRatios[op] : 1.0f;
  }
  float getOpAttack(int op) const {
    return (op >= 0 && op < 6) ? mOpAttack[op] : 0.0f;
  }
  float getOpDecay(int op) const {
    return (op >= 0 && op < 6) ? mOpDecay[op] : 0.0f;
  }
  float getOpSustain(int op) const {
    return (op >= 0 && op < 6) ? mOpSustain[op] : 1.0f;
  }
  float getOpRelease(int op) const {
    return (op >= 0 && op < 6) ? mOpRelease[op] : 0.0f;
  }

  // Amp Env Getters
  float getAttack() const { return mAttack; }
  float getDecay() const { return mDecay; }
  float getSustain() const { return mSustain; }
  float getRelease() const { return mRelease; }

  // Filter Env Getters & Setters
  void setFilterAttack(float v) { mFilterAttack = v; }
  void setFilterDecay(float v) { mFilterDecay = v; }
  void setFilterSustain(float v) { mFilterSustain = v; }
  void setFilterRelease(float v) { mFilterRelease = v; }
  void setFilterEnvAmount(float v) { mFilterEnvAmount = v * 2.0f - 1.0f; }

  float getFilterAttack() const { return mFilterAttack; }
  float getFilterDecay() const { return mFilterDecay; }
  float getFilterSustain() const { return mFilterSustain; }
  float getFilterRelease() const { return mFilterRelease; }
  float getFilterEnvAmount() const { return (mFilterEnvAmount + 1.0f) * 0.5f; }

  void triggerNote(int note, int velocity) {
    int idx = -1;
    for (int i = 0; i < 16; ++i)
      if (!mVoices[i].active) {
        idx = i;
        break;
      }
    if (idx == -1) {
      // Favor stealing released voices
      float minScore = 10.0f;
      for (int i = 0; i < 16; ++i) {
        float vVol = mVoices[i].masterEnv.getValue();
        float score = vVol;
        if (!mVoices[i].masterEnv.isActive()) score -= 1.0f;
        
        if (score < minScore) {
          minScore = score;
          idx = i;
        }
      }
      if (idx == -1) idx = 0;
    }

    Voice &v = mVoices[idx];
    v.reset();
    v.active = true;
    v.note = note;
    v.amplitude = velocity / 127.0f;

    float baseFreq = mIgnoreNoteFrequency
                         ? mFrequency
                         : 440.0f * powf(2.0f, (note - 69) / 12.0f);
    v.targetFrequency = baseFreq;
    v.frequency = (mGlide > 0.001f) ? mLastFrequency : baseFreq;
    mLastFrequency = baseFreq;

    float startFreq = v.frequency;

    for (int i = 0; i < 6; ++i) {
      v.operators[i].setSampleRate(mSampleRate);
      v.operators[i].setFrequency(startFreq, mOpRatios[i], mSampleRate);
      v.operators[i].setADSR(mOpAttack[i], mOpDecay[i], mOpSustain[i],
                             mOpRelease[i]);
      v.operators[i].setUseEnvelope(mUseEnvelope);
      if (mActiveMask & (1 << i))
        v.operators[i].trigger();
    }
    v.masterEnv.setSampleRate(mSampleRate);
    v.masterEnv.setParameters(mAttack, mDecay, mSustain, mRelease);
    v.masterEnv.trigger();

    v.filterEnv.setSampleRate(mSampleRate);
    v.filterEnv.setParameters(mFilterAttack, mFilterDecay, mFilterSustain, mFilterRelease);
    v.filterEnv.trigger();

    v.svf.setParams(1000.0f, 0.7f, mSampleRate);
    v.pitchEnv = 1.0f;
    // Faster decay for drums, slower for others
    v.pitchEnvDecay = mIgnoreNoteFrequency ? 0.005f : 0.001f;
  }

  void releaseNote(int note) {
    for (auto &v : mVoices)
      if (v.active && v.note == note) {
        for (auto &op : v.operators)
          op.release();
        v.masterEnv.release();
        v.filterEnv.release();
      }
  }

  void setParameter(int id, float value) {
    if (id == 114)
      mFilterAttack = value;
    else if (id == 115)
      mFilterDecay = value;
    else if (id == 116)
      mFilterSustain = value;
    else if (id == 117)
      mFilterRelease = value;
    else if (id == 118)
      mFilterEnvAmount = value * 2.0f - 1.0f;
    else if (id == 151)
      mCutoff = value;
    else if (id == 152)
      mResonance = value;
    else if (id == 150)
      setAlgorithm((int)(value * 31.99f));
    else if (id == 153)
      mCarrierMask = (int)value;
    else if (id == 154)
      mFeedback = value;
    else if (id == 155)
      mActiveMask = (int)value;
    else if (id == 156)
      mFilterMode = (int)value;
    else if (id == 157)
      mBrightness = value * 2.0f;
    else if (id == 158)
      mDetune = value;
    else if (id == 159)
      mFeedbackDrive = value;
    else if (id == 100)
      mAttack = value;
    else if (id == 101)
      mDecay = value;
    else if (id == 102)
      mSustain = value;
    else if (id == 103)
      mRelease = value;
    else if (id >= 160 && id <= 195) {
      int opIdx = (id - 160) / 6;
      int subId = (id - 160) % 6;
      if (opIdx < 6) {
        if (subId == 0)
          mOpLevels[opIdx] = value;
        else if (subId == 1)
          mOpAttack[opIdx] = value;
        else if (subId == 2)
          mOpDecay[opIdx] = value;
        else if (subId == 3)
          mOpSustain[opIdx] = value;
        else if (subId == 4)
          mOpRelease[opIdx] = value;
        else if (subId == 5)
          mOpRatios[opIdx] = value * 16.0f;
      }
    } else if (id == 355) {
      setGlide(value);
    }
  }

  void loadPreset(int presetId) {
    if (presetId >= 100) {
      int idx = presetId - 100;
      if (idx >= 0 && idx < (int)mCustomPresets.size()) {
        const auto &p = mCustomPresets[idx];
        resetToDefaults();
        setAlgorithm(p.algorithm);
        mCutoff = p.cutoff;
        mResonance = p.resonance;
        mCarrierMask = p.carrierMask;
        mActiveMask = p.activeMask;
        mFeedback = p.feedback;
        mFeedbackDrive = p.feedbackDrive;
        mBrightness = p.brightness;
        setGlide(p.glide);
        mDetune = p.detune;
        mFilterMode = p.filterMode;
        mAttack = p.attack;
        mDecay = p.decay;
        mSustain = p.sustain;
        mRelease = p.release;
        mFilterAttack = p.filterAttack;
        mFilterDecay = p.filterDecay;
        mFilterSustain = p.filterSustain;
        mFilterRelease = p.filterRelease;
        mFilterEnvAmount = p.filterEnvAmount;
        for (int i = 0; i < 6; ++i) {
          mOpLevels[i] = p.opLevels[i];
          mOpRatios[i] = p.opRatios[i];
          mOpAttack[i] = p.opAttack[i];
          mOpDecay[i] = p.opDecay[i];
          mOpSustain[i] = p.opSustain[i];
          mOpRelease[i] = p.opRelease[i];
        }
      }
      return;
    }

    resetToDefaults();

    // Default Envelope (Safe Start)
    mAttack = 0.01f;
    mDecay = 0.5f;
    mSustain = 0.8f;
    mRelease = 0.4f;
    mBrightness = 0.5f;

    switch (presetId) {
    case 0:            // Brass
    case 1:            // Strings (Soft)
      setAlgorithm(1); // 2 Branches
      mCarrierMask = (1 << 0) | (1 << 3);
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.8f;
      mOpRatios[1] = 1.0f;
      mOpLevels[1] = 0.4f;
      mOpRatios[3] = 1.0f;
      mOpLevels[3] = 0.8f;
      mOpRatios[4] = 1.005f;
      mOpLevels[4] = 0.3f; // Slight detune
      if (presetId == 0) {
        mAttack = 0.05f;
        mBrightness = 0.7f;
      } else {
        mAttack = 0.2f;
        mRelease = 0.8f;
      }
      break;

    case 2:            // Orchestra / Ensemble
      setAlgorithm(2); // Parallel
      mCarrierMask = 63;
      for (int i = 0; i < 6; ++i) {
        mOpLevels[i] = 0.25f;
        mOpRatios[i] = 1.0f + (i * 0.002f); // Detuned ensemble
      }
      mAttack = 0.15f;
      mRelease = 0.6f;
      break;

    case 3:            // Piano
    case 4:            // E. Piano
      setAlgorithm(3); // 3 Pairs
      mCarrierMask = (1 << 0) | (1 << 2) | (1 << 4);
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.8f;
      mOpRatios[1] = 1.0f;
      mOpLevels[1] = 0.5f;
      mOpRatios[2] = 1.0f;
      mOpLevels[2] = 0.6f;
      mOpRatios[3] = 14.0f;
      mOpLevels[3] = 0.2f; // Tine
      mOpRatios[4] = 1.0f;
      mOpLevels[4] = 0.4f;
      mOpRatios[5] = 1.0f;
      mOpLevels[5] = 0.1f;
      mAttack = 0.001f;
      mDecay = 0.6f;
      mSustain = 0.0f;
      if (presetId == 4) {
        mSustain = 0.3f;
        mBrightness = 0.6f;
      }
      break;

    case 6:            // Bass
    case 7:            // Organ
      setAlgorithm(0); // Serial
      mCarrierMask = 1;
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.9f;
      mOpRatios[1] = 0.5f;
      mOpLevels[1] = 0.7f; // Sub
      mOpRatios[2] = 1.0f;
      mOpLevels[2] = 0.4f;
      mOpRatios[3] = 2.0f;
      mOpLevels[3] = 0.2f;
      if (presetId == 6) {
        mDecay = 0.3f;
        mSustain = 0.0f;
        mBrightness = 0.4f;
      } else {
        mSustain = 1.0f;
        mBrightness = 0.6f;
        mOpRatios[1] = 2.0f;
        mOpRatios[2] = 3.0f;
      }
      break;

    case 11:           // Vibe
    case 12:           // Marimba
    case 21:           // Xylophone
      setAlgorithm(2); // Parallel
      mCarrierMask = 63;
      for (int i = 0; i < 5; ++i) {
        mOpLevels[i] = 0.5f / (float)(i + 1);
        mOpRatios[i] = (i == 0) ? 1.0f : (float)(i * 3 + 1.2f);
      }
      mAttack = 0.001f;
      mDecay = 0.7f;
      mSustain = 0.0f;
      if (presetId == 12)
        mDecay = 0.4f;
      if (presetId == 21)
        mDecay = 0.2f;
      break;

    case 14:           // Flute
    case 18:           // Calliope
    case 19:           // Oboe
      setAlgorithm(1); // 2 Branches
      mCarrierMask = (1 << 0) | (1 << 3);
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.8f;
      mOpRatios[1] = 2.0f;
      mOpLevels[1] = 0.3f;
      mOpRatios[3] = 1.0f;
      mOpLevels[3] = 0.7f;
      mOpRatios[4] = (presetId == 14) ? 3.0f : 1.5f;
      mOpLevels[4] = 0.2f;

      // Fix Harshness: Limit Brightness and Feedback for these delicate sounds
      mBrightness = 0.6f;
      mFeedback = 0.0f; // Flutes usually don't need feedback noise

      mAttack = 0.08f;
      mRelease = 0.3f;
      mDecay = 1.0f;
      break;

    case 15:           // Tubular Bells
    case 22:           // Church Bells
      setAlgorithm(2); // Parallel
      mCarrierMask = 63;
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.7f;
      mOpRatios[1] = 2.76f;
      mOpLevels[1] = 0.5f;
      mOpRatios[2] = 5.4f;
      mOpLevels[2] = 0.3f;
      mOpRatios[3] = 8.93f;
      mOpLevels[3] = 0.2f;
      mAttack = 0.001f;
      mDecay = 1.5f;
      mSustain = 0.0f;
      mRelease = 1.5f;
      break;

    case 23:           // Synth Lead
      setAlgorithm(0); // Serial
      mCarrierMask = 1;
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.8f;
      mOpRatios[1] = 1.0f;
      mOpLevels[1] = 0.6f;
      mOpRatios[2] = 2.01f;
      mOpLevels[2] = 0.5f;
      mOpRatios[3] = 3.99f;
      mOpLevels[3] = 0.4f;
      mFeedback = 0.6f;
      mBrightness = 0.7f;
      break;

    case 24: // RECORDERS
      setAlgorithm(2);
      mCarrierMask = 63;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.81f;
      mOpAttack[0] = 0.040f;
      mOpDecay[0] = 0.520f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 0.380f;
      mOpRatios[1] = 4.04f;
      mOpLevels[1] = 0.17f;
      mOpAttack[1] = 0.580f;
      mOpDecay[1] = 0.500f;
      mOpSustain[1] = 0.87f;
      mOpRelease[1] = 0.860f;
      mOpRatios[2] = 1.00f;
      mOpLevels[2] = 0.57f;
      mOpAttack[2] = 0.560f;
      mOpDecay[2] = 1.620f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 1.060f;
      mOpRatios[3] = 4.04f;
      mOpLevels[3] = 0.34f;
      mOpAttack[3] = 0.540f;
      mOpDecay[3] = 1.580f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 1.480f;
      mOpRatios[4] = 1.01f;
      mOpLevels[4] = 0.84f;
      mOpAttack[4] = 0.080f;
      mOpDecay[4] = 0.520f;
      mOpSustain[4] = 1.00f;
      mOpRelease[4] = 0.380f;
      mOpRatios[5] = 4.04f;
      mOpLevels[5] = 0.22f;
      mOpAttack[5] = 0.540f;
      mOpDecay[5] = 1.580f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 1.480f;
      mBrightness = 0.7f;
      mFeedback = 0.0f;
      break;

    case 25: // SHIMMER
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.81f;
      mOpAttack[0] = 0.040f;
      mOpDecay[0] = 0.300f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 0.860f;
      mOpRatios[1] = 5.04f;
      mOpLevels[1] = 0.57f;
      mOpAttack[1] = 0.840f;
      mOpDecay[1] = 0.840f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 0.940f;
      mOpRatios[2] = 1.03f;
      mOpLevels[2] = 0.31f;
      mOpAttack[2] = 0.200f;
      mOpDecay[2] = 1.100f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 0.700f;
      mOpRatios[3] = 1.00f;
      mOpLevels[3] = 0.69f;
      mOpAttack[3] = 0.540f;
      mOpDecay[3] = 0.440f;
      mOpSustain[3] = 1.00f;
      mOpRelease[3] = 0.540f;
      mOpRatios[4] = 6.05f;
      mOpLevels[4] = 0.38f;
      mOpAttack[4] = 0.960f;
      mOpDecay[4] = 0.960f;
      mOpSustain[4] = 0.90f;
      mOpRelease[4] = 0.980f;
      mOpRatios[5] = 0.50f;
      mOpLevels[5] = 0.41f;
      mOpAttack[5] = 0.320f;
      mOpDecay[5] = 1.440f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.800f;
      mBrightness = 0.8f;
      mFeedback = 0.4f;
      break;

    case 26: // FILTER SWP
      setAlgorithm(0);
      mCarrierMask = 1;
      mOpRatios[0] = 0.50f;
      mOpLevels[0] = 0.96f;
      mOpAttack[0] = 0.220f;
      mOpDecay[0] = 0.300f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 1.340f;
      mOpRatios[1] = 1.00f;
      mOpLevels[1] = 0.44f;
      mOpAttack[1] = 0.320f;
      mOpDecay[1] = 0.600f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 1.020f;
      mOpRatios[2] = 0.50f;
      mOpLevels[2] = 0.52f;
      mOpAttack[2] = 0.460f;
      mOpDecay[2] = 0.600f;
      mOpSustain[2] = 0.96f;
      mOpRelease[2] = 1.000f;
      mOpRatios[3] = 1.00f;
      mOpLevels[3] = 0.79f;
      mOpAttack[3] = 0.560f;
      mOpDecay[3] = 0.600f;
      mOpSustain[3] = 0.98f;
      mOpRelease[3] = 1.000f;
      mOpRatios[4] = 1.00f;
      mOpLevels[4] = 0.47f;
      mOpAttack[4] = 0.540f;
      mOpDecay[4] = 1.000f;
      mOpSustain[4] = 0.74f;
      mOpRelease[4] = 1.020f;
      mOpRatios[5] = 0.50f;
      mOpLevels[5] = 0.93f;
      mOpAttack[5] = 0.440f;
      mOpDecay[5] = 1.000f;
      mOpSustain[5] = 0.61f;
      mOpRelease[5] = 1.020f;
      mBrightness = 0.6f;
      mFeedback = 0.5f;
      break;

    case 27: // FUNKY RISE
      setAlgorithm(0);
      mCarrierMask = 1;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.94f;
      mOpAttack[0] = 0.080f;
      mOpDecay[0] = 0.280f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 1.120f;
      mOpRatios[1] = 1.00f;
      mOpLevels[1] = 0.52f;
      mOpAttack[1] = 0.660f;
      mOpDecay[1] = 1.300f;
      mOpSustain[1] = 0.56f;
      mOpRelease[1] = 1.340f;
      mOpRatios[2] = 1.00f;
      mOpLevels[2] = 0.79f;
      mOpAttack[2] = 0.660f;
      mOpDecay[2] = 0.720f;
      mOpSustain[2] = 0.46f;
      mOpRelease[2] = 1.220f;
      mOpRatios[3] = 2.01f;
      mOpLevels[3] = 0.87f;
      mOpAttack[3] = 0.660f;
      mOpDecay[3] = 1.300f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 1.480f;
      mOpRatios[4] = 1.00f;
      mOpLevels[4] = 0.69f;
      mOpAttack[4] = 0.660f;
      mOpDecay[4] = 1.300f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 1.580f;
      mOpRatios[5] = 0.50f;
      mOpLevels[5] = 0.69f;
      mOpAttack[5] = 0.660f;
      mOpDecay[5] = 0.480f;
      mOpSustain[5] = 0.92f;
      mOpRelease[5] = 1.460f;
      mBrightness = 0.8f;
      mFeedback = 0.7f;
      break;

    case 28: // REFS WHISL
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 13.07f;
      mOpLevels[0] = 0.32f;
      mOpAttack[0] = 0.390f;
      mOpDecay[0] = 1.200f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 1.000f;
      mOpRatios[1] = 119.00f;
      mOpLevels[1] = 0.54f;
      mOpAttack[1] = 0.390f;
      mOpDecay[1] = 1.200f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 1.080f;
      mOpRatios[2] = 1.03f;
      mOpLevels[2] = 0.68f;
      mOpAttack[2] = 0.390f;
      mOpDecay[2] = 1.200f;
      mOpSustain[2] = 1.00f;
      mOpRelease[2] = 1.980f;
      mOpRatios[3] = 11.50f;
      mOpLevels[3] = 0.83f;
      mOpAttack[3] = 0.050f;
      mOpDecay[3] = 0.620f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 0.880f;
      mOpRatios[4] = 0.50f;
      mOpLevels[4] = 0.00f;
      mOpAttack[4] = 0.001f;
      mOpDecay[4] = 0.000f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 0.000f;
      mOpRatios[5] = 0.50f;
      mOpLevels[5] = 0.00f;
      mOpAttack[5] = 0.001f;
      mOpDecay[5] = 0.000f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.000f;
      mBrightness = 0.7f;
      mFeedback = 0.0f;
      break;

    case 29: // STEEL DRUM
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 10.20f;
      mOpLevels[0] = 0.00f;
      mOpAttack[0] = 0.001f;
      mOpDecay[0] = 1.180f;
      mOpSustain[0] = 0.00f;
      mOpRelease[0] = 1.220f;
      mOpRatios[1] = 0.50f;
      mOpLevels[1] = 0.71f;
      mOpAttack[1] = 0.001f;
      mOpDecay[1] = 1.600f;
      mOpSustain[1] = 0.00f;
      mOpRelease[1] = 1.800f;
      mOpRatios[2] = 10.20f;
      mOpLevels[2] = 0.00f;
      mOpAttack[2] = 0.001f;
      mOpDecay[2] = 1.380f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 1.140f;
      mOpRatios[3] = 12.04f;
      mOpLevels[3] = 0.00f;
      mOpAttack[3] = 0.001f;
      mOpDecay[3] = 1.100f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 1.560f;
      mOpRatios[4] = 0.50f;
      mOpLevels[4] = 0.33f;
      mOpAttack[4] = 0.001f;
      mOpDecay[4] = 1.180f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 1.980f;
      mOpRatios[5] = 1.05f;
      mOpLevels[5] = 0.61f;
      mOpAttack[5] = 0.001f;
      mOpDecay[5] = 1.000f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 1.740f;
      mBrightness = 0.9f;
      mFeedback = 0.0f;
      break;

    case 30: // HARMONICA1
      setAlgorithm(0);
      mCarrierMask = 1;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.98f;
      mOpAttack[0] = 0.380f;
      mOpDecay[0] = 0.300f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 0.440f;
      mOpRatios[1] = 2.01f;
      mOpLevels[1] = 0.59f;
      mOpAttack[1] = 0.420f;
      mOpDecay[1] = 0.300f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 0.460f;
      mOpRatios[2] = 3.00f;
      mOpLevels[2] = 0.65f;
      mOpAttack[2] = 0.160f;
      mOpDecay[2] = 1.800f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 0.460f;
      mOpRatios[3] = 2.01f;
      mOpLevels[3] = 0.81f;
      mOpAttack[3] = 0.160f;
      mOpDecay[3] = 0.700f;
      mOpSustain[3] = 0.53f;
      mOpRelease[3] = 0.460f;
      mOpRatios[4] = 3.00f;
      mOpLevels[4] = 0.46f;
      mOpAttack[4] = 0.060f;
      mOpDecay[4] = 0.640f;
      mOpSustain[4] = 0.61f;
      mOpRelease[4] = 0.640f;
      mOpRatios[5] = 2.00f;
      mOpLevels[5] = 0.74f;
      mOpAttack[5] = 0.001f;
      mOpDecay[5] = 0.820f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.500f;
      mBrightness = 0.8f;
      mFeedback = 0.5f;
      break;

    case 31: // ACCORDION
      setAlgorithm(2);
      mCarrierMask = 63;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.79f;
      mOpAttack[0] = 0.440f;
      mOpDecay[0] = 0.320f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 0.660f;
      mOpRatios[1] = 1.02f;
      mOpLevels[1] = 0.79f;
      mOpAttack[1] = 0.340f;
      mOpDecay[1] = 0.320f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 0.580f;
      mOpRatios[2] = 1.00f;
      mOpLevels[2] = 0.79f;
      mOpAttack[2] = 0.400f;
      mOpDecay[2] = 0.360f;
      mOpSustain[2] = 1.00f;
      mOpRelease[2] = 0.740f;
      mOpRatios[3] = 2.01f;
      mOpLevels[3] = 0.78f;
      mOpAttack[3] = 0.440f;
      mOpDecay[3] = 0.400f;
      mOpSustain[3] = 1.00f;
      mOpRelease[3] = 0.500f;
      mOpRatios[4] = 1.00f;
      mOpLevels[4] = 0.83f;
      mOpAttack[4] = 0.300f;
      mOpDecay[4] = 0.500f;
      mOpSustain[4] = 1.00f;
      mOpRelease[4] = 0.560f;
      mOpRatios[5] = 1.00f;
      mOpLevels[5] = 0.71f;
      mOpAttack[5] = 0.320f;
      mOpDecay[5] = 0.840f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.600f;
      mBrightness = 0.8f;
      mFeedback = 0.7f;
      break;

    case 32: // SITAR
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.99f;
      mOpAttack[0] = 0.020f;
      mOpDecay[0] = 1.980f;
      mOpSustain[0] = 0.00f;
      mOpRelease[0] = 0.520f;
      mOpRatios[1] = 2.00f;
      mOpLevels[1] = 0.86f;
      mOpAttack[1] = 0.080f;
      mOpDecay[1] = 1.580f;
      mOpSustain[1] = 0.00f;
      mOpRelease[1] = 1.620f;
      mOpRatios[2] = 0.50f;
      mOpLevels[2] = 0.73f;
      mOpAttack[2] = 0.040f;
      mOpDecay[2] = 0.520f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 1.580f;
      mOpRatios[3] = 0.50f;
      mOpLevels[3] = 0.86f;
      mOpAttack[3] = 0.040f;
      mOpDecay[3] = 1.600f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 0.540f;
      mOpRatios[4] = 2.00f;
      mOpLevels[4] = 0.69f;
      mOpAttack[4] = 0.080f;
      mOpDecay[4] = 1.480f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 1.480f;
      mOpRatios[5] = 0.50f;
      mOpLevels[5] = 0.84f;
      mOpAttack[5] = 0.001f;
      mOpDecay[5] = 0.520f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.700f;
      mBrightness = 0.9f;
      mFeedback = 0.6f;
      break;

    case 33: // LUTE
      setAlgorithm(0);
      mCarrierMask = 1;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.99f;
      mOpAttack[0] = 0.020f;
      mOpDecay[0] = 1.040f;
      mOpSustain[0] = 0.00f;
      mOpRelease[0] = 1.100f;
      mOpRatios[1] = 1.00f;
      mOpLevels[1] = 0.77f;
      mOpAttack[1] = 0.200f;
      mOpDecay[1] = 1.500f;
      mOpSustain[1] = 0.00f;
      mOpRelease[1] = 0.760f;
      mOpRatios[2] = 1.00f;
      mOpLevels[2] = 0.58f;
      mOpAttack[2] = 0.360f;
      mOpDecay[2] = 0.740f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 0.760f;
      mOpRatios[3] = 0.50f;
      mOpLevels[3] = 0.61f;
      mOpAttack[3] = 0.440f;
      mOpDecay[3] = 0.880f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 0.760f;
      mOpRatios[4] = 14.14f;
      mOpLevels[4] = 0.00f;
      mOpAttack[4] = 0.001f;
      mOpDecay[4] = 0.000f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 0.000f;
      mOpRatios[5] = 14.14f;
      mOpLevels[5] = 0.00f;
      mOpAttack[5] = 0.001f;
      mOpDecay[5] = 0.000f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.000f;
      mBrightness = 0.8f;
      mFeedback = 0.8f;
      break;

    case 34: // BANJO
      setAlgorithm(0);
      mCarrierMask = 1;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.99f;
      mOpAttack[0] = 0.020f;
      mOpDecay[0] = 0.980f;
      mOpSustain[0] = 0.00f;
      mOpRelease[0] = 1.100f;
      mOpRatios[1] = 1.00f;
      mOpLevels[1] = 0.76f;
      mOpAttack[1] = 0.001f;
      mOpDecay[1] = 1.340f;
      mOpSustain[1] = 0.00f;
      mOpRelease[1] = 0.800f;
      mOpRatios[2] = 2.01f;
      mOpLevels[2] = 0.76f;
      mOpAttack[2] = 0.001f;
      mOpDecay[2] = 0.660f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 1.200f;
      mOpRatios[3] = 0.50f;
      mOpLevels[3] = 0.80f;
      mOpAttack[3] = 0.120f;
      mOpDecay[3] = 1.160f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 1.000f;
      mOpRatios[4] = 14.14f;
      mOpLevels[4] = 0.10f;
      mOpAttack[4] = 0.040f;
      mOpDecay[4] = 0.460f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 0.360f;
      mOpRatios[5] = 1.00f;
      mOpLevels[5] = 0.83f;
      mOpAttack[5] = 0.040f;
      mOpDecay[5] = 0.920f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.420f;
      mBrightness = 0.9f;
      mFeedback = 0.7f;
      break;

    case 35: // HARP 1
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.99f;
      mOpAttack[0] = 0.020f;
      mOpDecay[0] = 1.980f;
      mOpSustain[0] = 0.00f;
      mOpRelease[0] = 0.900f;
      mOpRatios[1] = 1.00f;
      mOpLevels[1] = 0.76f;
      mOpAttack[1] = 0.001f;
      mOpDecay[1] = 1.480f;
      mOpSustain[1] = 0.00f;
      mOpRelease[1] = 1.200f;
      mOpRatios[2] = 2.01f;
      mOpLevels[2] = 0.68f;
      mOpAttack[2] = 0.080f;
      mOpDecay[2] = 0.680f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 1.220f;
      mOpRatios[3] = 0.50f;
      mOpLevels[3] = 0.86f;
      mOpAttack[3] = 0.040f;
      mOpDecay[3] = 1.600f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 0.600f;
      mOpRatios[4] = 6.09f;
      mOpLevels[4] = 0.00f;
      mOpAttack[4] = 0.001f;
      mOpDecay[4] = 0.000f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 0.000f;
      mOpRatios[5] = 3.01f;
      mOpLevels[5] = 0.00f;
      mOpAttack[5] = 0.001f;
      mOpDecay[5] = 0.000f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.000f;
      mBrightness = 0.8f;
      mFeedback = 0.7f;
      break;

    case 36: // HARP 2
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.99f;
      mOpAttack[0] = 0.001f;
      mOpDecay[0] = 1.980f;
      mOpSustain[0] = 0.00f;
      mOpRelease[0] = 0.900f;
      mOpRatios[1] = 0.50f;
      mOpLevels[1] = 0.84f;
      mOpAttack[1] = 0.020f;
      mOpDecay[1] = 1.320f;
      mOpSustain[1] = 0.00f;
      mOpRelease[1] = 0.860f;
      mOpRatios[2] = 1.00f;
      mOpLevels[2] = 0.75f;
      mOpAttack[2] = 0.001f;
      mOpDecay[2] = 1.080f;
      mOpSustain[2] = 0.00f;
      mOpRelease[2] = 0.380f;
      mOpRatios[3] = 0.50f;
      mOpLevels[3] = 0.92f;
      mOpAttack[3] = 0.001f;
      mOpDecay[3] = 1.980f;
      mOpSustain[3] = 0.00f;
      mOpRelease[3] = 0.900f;
      mOpRatios[4] = 2.01f;
      mOpLevels[4] = 0.59f;
      mOpAttack[4] = 0.040f;
      mOpDecay[4] = 0.840f;
      mOpSustain[4] = 0.00f;
      mOpRelease[4] = 0.840f;
      mOpRatios[5] = 0.50f;
      mOpLevels[5] = 0.90f;
      mOpAttack[5] = 0.020f;
      mOpDecay[5] = 0.900f;
      mOpSustain[5] = 0.00f;
      mOpRelease[5] = 0.700f;
      mBrightness = 0.8f;
      mFeedback = 0.5f;
      break;

    case 37: // SYN-VOX
      setAlgorithm(0);
      mCarrierMask = 1;
      mOpRatios[0] = 1.00f;
      mOpLevels[0] = 0.89f;
      mOpAttack[0] = 0.400f;
      mOpDecay[0] = 1.100f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 0.940f;
      mOpRatios[1] = 1.01f;
      mOpLevels[1] = 0.61f;
      mOpAttack[1] = 0.320f;
      mOpDecay[1] = 1.980f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 0.860f;
      mOpRatios[2] = 2.00f;
      mOpLevels[2] = 0.57f;
      mOpAttack[2] = 0.320f;
      mOpDecay[2] = 1.980f;
      mOpSustain[2] = 1.00f;
      mOpRelease[2] = 0.800f;
      mOpRatios[3] = 3.01f;
      mOpLevels[3] = 0.52f;
      mOpAttack[3] = 0.280f;
      mOpDecay[3] = 1.980f;
      mOpSustain[3] = 1.00f;
      mOpRelease[3] = 0.740f;
      mOpRatios[4] = 4.00f;
      mOpLevels[4] = 0.46f;
      mOpAttack[4] = 0.260f;
      mOpDecay[4] = 1.980f;
      mOpSustain[4] = 1.00f;
      mOpRelease[4] = 0.700f;
      mOpRatios[5] = 5.01f;
      mOpLevels[5] = 0.46f;
      mOpAttack[5] = 0.260f;
      mOpDecay[5] = 1.140f;
      mOpSustain[5] = 1.00f;
      mOpRelease[5] = 0.600f;
      mBrightness = 0.7f;
      mFeedback = 0.5f;
      break;

    case 38: // SYN-ORCH
      setAlgorithm(1);
      mCarrierMask = 9;
      mOpRatios[0] = 0.50f;
      mOpLevels[0] = 0.99f;
      mOpAttack[0] = 0.160f;
      mOpDecay[0] = 0.540f;
      mOpSustain[0] = 1.00f;
      mOpRelease[0] = 0.980f;
      mOpRatios[1] = 0.50f;
      mOpLevels[1] = 0.60f;
      mOpAttack[1] = 0.520f;
      mOpDecay[1] = 0.520f;
      mOpSustain[1] = 1.00f;
      mOpRelease[1] = 1.080f;
      mOpRatios[2] = 0.50f;
      mOpLevels[2] = 0.64f;
      mOpAttack[2] = 0.160f;
      mOpDecay[2] = 0.600f;
      mOpSustain[2] = 1.00f;
      mOpRelease[2] = 1.100f;
      mOpRatios[3] = 1.00f;
      mOpLevels[3] = 0.65f;
      mOpAttack[3] = 0.460f;
      mOpDecay[3] = 0.440f;
      mOpSustain[3] = 1.00f;
      mOpRelease[3] = 0.880f;
      mOpRatios[4] = 1.01f;
      mOpLevels[4] = 0.70f;
      mOpAttack[4] = 0.820f;
      mOpDecay[4] = 0.800f;
      mOpSustain[4] = 1.00f;
      mOpRelease[4] = 1.000f;
      mOpRatios[5] = 1.00f;
      mOpLevels[5] = 0.81f;
      mOpAttack[5] = 0.040f;
      mOpDecay[5] = 0.420f;
      mOpSustain[5] = 1.00f;
      mOpRelease[5] = 0.740f;
      mBrightness = 0.8f;
      mFeedback = 0.6f;
      break;

    default:
      // Generic Sine / FM Start
      setAlgorithm(1);
      mCarrierMask = 1;
      mOpRatios[0] = 1.0f;
      mOpLevels[0] = 0.8f;
      mOpRatios[1] = 1.0f;
      mOpLevels[1] = 0.2f;
      break;
    }
  }

  bool importPreset(const std::string& path) {
    // Open in binary mode to read the full buffer for analysis
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0) return false;

    std::vector<uint8_t> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
      return false;
    }
    file.close();

    // Differentiate between text-based .fmp format and binary DX7 Sysex formats
    bool isBinary = false;
    std::string ext = "";
    size_t dot = path.find_last_of(".");
    if (dot != std::string::npos) {
      ext = path.substr(dot);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    if (ext == ".syx" || ext == ".sysex" || ext == ".sys" || ext == ".bin") {
      isBinary = true;
    } else {
      // Content analysis to detect binary signatures
      int nonPrintable = 0;
      int checkLimit = std::min((int)size, 100);
      for (int i = 0; i < checkLimit; ++i) {
        char c = buffer[i];
        if ((c < 32 || c > 126) && c != '\r' && c != '\n' && c != '\t') {
          nonPrintable++;
        }
      }
      if (nonPrintable > 5) {
        isBinary = true;
      }
    }

    if (isBinary) {
      // Static DX7 algorithm to Loom Pi carrier mask mapping table
      auto getDx7CarrierMask = [](int algo) -> int {
        static const int masks[32] = {
          1, 1, 1, 1, 21, 5, 5, 5, 5, 9, 9, 17, 17, 1, 1, 1,
          1, 3, 15, 25, 29, 31, 25, 13, 15, 13, 9, 11, 21, 23, 31, 63
        };
        if (algo >= 0 && algo < 32) return masks[algo];
        return 1;
      };

      // Converts DX7 rate (0-99) exponentially to Loom Pi ADSR time (seconds)
      auto rateToSeconds = [](uint8_t rate) -> float {
        float r = std::min((float)rate, 99.0f);
        return std::max(0.001f, 8.0f * powf(1.0f - r / 99.0f, 3.0f));
      };

      // Parses a 128-byte packed DX7 voice from a buffer
      auto parsePackedVoice = [&](const uint8_t* data, CustomPreset& p) {
        for (int op = 0; op < 6; ++op) {
          int opOffset = (5 - op) * 17;
          uint8_t r1 = data[opOffset + 0] & 0x7F;
          uint8_t r2 = data[opOffset + 1] & 0x7F;
          uint8_t r3 = data[opOffset + 2] & 0x7F;
          uint8_t r4 = data[opOffset + 3] & 0x7F;
          uint8_t l1 = data[opOffset + 4] & 0x7F;
          uint8_t l2 = data[opOffset + 5] & 0x7F;
          uint8_t l3 = data[opOffset + 6] & 0x7F;
          uint8_t l4 = data[opOffset + 7] & 0x7F;

          p.opAttack[op] = rateToSeconds(r1);
          p.opDecay[op] = rateToSeconds(r2);
          p.opSustain[op] = std::min(1.0f, (float)l3 / 99.0f);
          p.opRelease[op] = rateToSeconds(r4);

          uint8_t level = data[opOffset + 14] & 0x7F;
          p.opLevels[op] = (float)level / 99.0f;

          uint8_t modeCoarse = data[opOffset + 15];
          uint8_t mode = modeCoarse & 0x01;
          uint8_t coarse = (modeCoarse >> 1) & 0x1F;
          uint8_t fine = data[opOffset + 16] & 0x7F;

          if (mode == 0) {
            float base = (coarse == 0) ? 0.5f : (float)coarse;
            p.opRatios[op] = base * (1.0f + (float)fine / 100.0f);
          } else {
            float hz = powf(10.0f, (float)(coarse % 4) + (float)fine / 100.0f);
            p.opRatios[op] = hz / 261.63f;
          }
        }

        p.algorithm = data[110] & 0x1F; // DX7 Alg 0-31
        p.carrierMask = getDx7CarrierMask(p.algorithm);
        p.activeMask = 63; // All 6 operators are active

        uint8_t fbVal = data[111] & 0x07;
        p.feedback = (float)fbVal / 7.0f;
        p.feedbackDrive = 0.5f;
        p.brightness = 0.8f;
        p.glide = 0.0f;
        p.detune = 0.0f;

        p.filterMode = 3; // DX7-perfect SVF filter bypass
        p.cutoff = 1.0f;
        p.resonance = 0.0f;
        p.filterEnvAmount = 0.0f;

        // Open master ADSR envelope
        p.attack = 0.01f;
        p.decay = 0.5f;
        p.sustain = 1.0f;
        p.release = 1.0f;

        // Parse Name (10 characters)
        p.name = "";
        for (int i = 0; i < 10; ++i) {
          char c = data[118 + i] & 0x7F;
          if (c >= 32 && c <= 126) p.name += c;
          else p.name += ' ';
        }
        size_t last = p.name.find_last_not_of(" ");
        if (last != std::string::npos) p.name = p.name.substr(0, last + 1);
        if (p.name.empty()) p.name = "DX7 Patch";
      };

      // Parses a 155-byte unpacked DX7 voice from a buffer
      auto parseUnpackedVoice = [&](const uint8_t* data, CustomPreset& p) {
        for (int op = 0; op < 6; ++op) {
          int opOffset = (5 - op) * 21;
          uint8_t r1 = data[opOffset + 0] & 0x7F;
          uint8_t r2 = data[opOffset + 1] & 0x7F;
          uint8_t r3 = data[opOffset + 2] & 0x7F;
          uint8_t r4 = data[opOffset + 3] & 0x7F;
          uint8_t l1 = data[opOffset + 4] & 0x7F;
          uint8_t l2 = data[opOffset + 5] & 0x7F;
          uint8_t l3 = data[opOffset + 6] & 0x7F;
          uint8_t l4 = data[opOffset + 7] & 0x7F;

          p.opAttack[op] = rateToSeconds(r1);
          p.opDecay[op] = rateToSeconds(r2);
          p.opSustain[op] = std::min(1.0f, (float)l3 / 99.0f);
          p.opRelease[op] = rateToSeconds(r4);

          uint8_t level = data[opOffset + 16] & 0x7F;
          p.opLevels[op] = (float)level / 99.0f;

          uint8_t mode = data[opOffset + 17] & 0x01;
          uint8_t coarse = data[opOffset + 18] & 0x1F;
          uint8_t fine = data[opOffset + 19] & 0x7F;

          if (mode == 0) {
            float base = (coarse == 0) ? 0.5f : (float)coarse;
            p.opRatios[op] = base * (1.0f + (float)fine / 100.0f);
          } else {
            float hz = powf(10.0f, (float)(coarse % 4) + (float)fine / 100.0f);
            p.opRatios[op] = hz / 261.63f;
          }
        }

        p.algorithm = data[134] & 0x1F;
        p.carrierMask = getDx7CarrierMask(p.algorithm);
        p.activeMask = 63;

        uint8_t fbVal = data[135] & 0x07;
        p.feedback = (float)fbVal / 7.0f;
        p.feedbackDrive = 0.5f;
        p.brightness = 0.8f;
        p.glide = 0.0f;
        p.detune = 0.0f;

        p.filterMode = 3; // DX7-perfect SVF filter bypass
        p.cutoff = 1.0f;
        p.resonance = 0.0f;
        p.filterEnvAmount = 0.0f;

        p.attack = 0.01f;
        p.decay = 0.5f;
        p.sustain = 1.0f;
        p.release = 1.0f;

        p.name = "";
        for (int i = 0; i < 10; ++i) {
          char c = data[145 + i] & 0x7F;
          if (c >= 32 && c <= 126) p.name += c;
          else p.name += ' ';
        }
        size_t last = p.name.find_last_not_of(" ");
        if (last != std::string::npos) p.name = p.name.substr(0, last + 1);
        if (p.name.empty()) p.name = "DX7 Patch";
      };

      // Search for Sysex wrappers inside buffer
      int sysexOffset = -1;
      for (int i = 0; i < (int)size - 4; ++i) {
        if (buffer[i] == 0xF0 && buffer[i+1] == 0x43) {
          sysexOffset = i;
          break;
        }
      }

      if (sysexOffset >= 0) {
        uint8_t format = buffer[sysexOffset + 3];
        if (format == 0x09) {
          // 32-voice packed bulk dump
          int dataStart = sysexOffset + 6;
          if (dataStart + 4096 <= (int)size) {
            for (int v = 0; v < 32; ++v) {
              CustomPreset p;
              parsePackedVoice(buffer.data() + dataStart + v * 128, p);
              mCustomPresets.push_back(p);
            }
            return true;
          }
        } else if (format == 0x00) {
          // 1-voice unpacked single dump
          int dataStart = sysexOffset + 6;
          if (dataStart + 155 <= (int)size) {
            CustomPreset p;
            parseUnpackedVoice(buffer.data() + dataStart, p);
            mCustomPresets.push_back(p);
            return true;
          }
        }
      }

      // Raw structures (fallback if no Sysex wrapper was found)
      if (size >= 4096) {
        // Bulk raw 32 packed voices
        for (int v = 0; v < 32; ++v) {
          if (v * 128 + 128 <= (int)size) {
            CustomPreset p;
            parsePackedVoice(buffer.data() + v * 128, p);
            mCustomPresets.push_back(p);
          }
        }
        return true;
      } else if (size >= 155 && size < 163) {
        // Single raw 155-byte unpacked voice
        CustomPreset p;
        parseUnpackedVoice(buffer.data(), p);
        mCustomPresets.push_back(p);
        return true;
      } else if (size >= 128 && size < 155) {
        // Single raw 128-byte packed voice
        CustomPreset p;
        parsePackedVoice(buffer.data(), p);
        mCustomPresets.push_back(p);
        return true;
      }
      return false; // unrecognized structure
    }

    // --- TEXT PRESET FILE (.fmp) PARSING ---
    std::ifstream fileTxt(path);
    if (!fileTxt.is_open()) {
      return false;
    }
    CustomPreset p;
    p.name = "Imported Patch";

    std::string line;
    while (std::getline(fileTxt, line)) {
      if (line.empty()) continue;
      size_t first = line.find_first_not_of(" \t\r\n");
      if (first == std::string::npos) continue;
      size_t last = line.find_last_not_of(" \t\r\n");
      line = line.substr(first, (last - first + 1));
      if (line.empty() || line[0] == '#') continue;

      size_t pos = line.find(':');
      if (pos == std::string::npos) continue;

      std::string key = line.substr(0, pos);
      std::string val = line.substr(pos + 1);

      // Trim key and val
      size_t k_first = key.find_first_not_of(" \t\r\n");
      size_t k_last = key.find_last_not_of(" \t\r\n");
      if (k_first != std::string::npos && k_last != std::string::npos) {
        key = key.substr(k_first, (k_last - k_first + 1));
      }
      size_t v_first = val.find_first_not_of(" \t\r\n");
      size_t v_last = val.find_last_not_of(" \t\r\n");
      if (v_first != std::string::npos && v_last != std::string::npos) {
        val = val.substr(v_first, (v_last - v_first + 1));
      }

      try {
        if (key == "NAME") p.name = val;
        else if (key == "ALGORITHM") p.algorithm = std::stoi(val);
        else if (key == "CUTOFF") p.cutoff = std::stof(val);
        else if (key == "RESONANCE") p.resonance = std::stof(val);
        else if (key == "CARRIER_MASK") p.carrierMask = std::stoi(val);
        else if (key == "ACTIVE_MASK") p.activeMask = std::stoi(val);
        else if (key == "FEEDBACK") p.feedback = std::stof(val);
        else if (key == "FEEDBACK_DRIVE") p.feedbackDrive = std::stof(val);
        else if (key == "BRIGHTNESS") p.brightness = std::stof(val);
        else if (key == "GLIDE") p.glide = std::stof(val);
        else if (key == "DETUNE") p.detune = std::stof(val);
        else if (key == "FILTER_MODE") p.filterMode = std::stoi(val);
        else if (key == "ATTACK") p.attack = std::stof(val);
        else if (key == "DECAY") p.decay = std::stof(val);
        else if (key == "SUSTAIN") p.sustain = std::stof(val);
        else if (key == "RELEASE") p.release = std::stof(val);
        else if (key == "FILTER_ATTACK") p.filterAttack = std::stof(val);
        else if (key == "FILTER_DECAY") p.filterDecay = std::stof(val);
        else if (key == "FILTER_SUSTAIN") p.filterSustain = std::stof(val);
        else if (key == "FILTER_RELEASE") p.filterRelease = std::stof(val);
        else if (key == "FILTER_ENV_AMOUNT") p.filterEnvAmount = std::stof(val);
        else {
          if (key.size() > 3 && key.substr(0, 2) == "OP") {
            int opIdx = key[2] - '1';
            if (opIdx >= 0 && opIdx < 6) {
              std::string subKey = key.substr(4);
              if (subKey == "LEVEL") p.opLevels[opIdx] = std::stof(val);
              else if (subKey == "RATIO") p.opRatios[opIdx] = std::stof(val);
              else if (subKey == "ATTACK") p.opAttack[opIdx] = std::stof(val);
              else if (subKey == "DECAY") p.opDecay[opIdx] = std::stof(val);
              else if (subKey == "SUSTAIN") p.opSustain[opIdx] = std::stof(val);
              else if (subKey == "RELEASE") p.opRelease[opIdx] = std::stof(val);
            }
          }
        }
      } catch (...) {
        // Safe catch-all
      }
    }
    mCustomPresets.push_back(p);
    return true;
  }

  bool savePresetToFile(const CustomPreset& p, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << "NAME:" << p.name << "\n";
    file << "ALGORITHM:" << p.algorithm << "\n";
    file << "CUTOFF:" << p.cutoff << "\n";
    file << "RESONANCE:" << p.resonance << "\n";
    file << "CARRIER_MASK:" << p.carrierMask << "\n";
    file << "ACTIVE_MASK:" << p.activeMask << "\n";
    file << "FEEDBACK:" << p.feedback << "\n";
    file << "FEEDBACK_DRIVE:" << p.feedbackDrive << "\n";
    file << "BRIGHTNESS:" << p.brightness << "\n";
    file << "GLIDE:" << p.glide << "\n";
    file << "DETUNE:" << p.detune << "\n";
    file << "FILTER_MODE:" << p.filterMode << "\n";
    file << "ATTACK:" << p.attack << "\n";
    file << "DECAY:" << p.decay << "\n";
    file << "SUSTAIN:" << p.sustain << "\n";
    file << "RELEASE:" << p.release << "\n";
    file << "FILTER_ATTACK:" << p.filterAttack << "\n";
    file << "FILTER_DECAY:" << p.filterDecay << "\n";
    file << "FILTER_SUSTAIN:" << p.filterSustain << "\n";
    file << "FILTER_RELEASE:" << p.filterRelease << "\n";
    file << "FILTER_ENV_AMOUNT:" << p.filterEnvAmount << "\n";
    for (int op = 0; op < 6; ++op) {
      file << "OP" << (op + 1) << "_LEVEL:" << p.opLevels[op] << "\n";
      file << "OP" << (op + 1) << "_RATIO:" << p.opRatios[op] << "\n";
      file << "OP" << (op + 1) << "_ATTACK:" << p.opAttack[op] << "\n";
      file << "OP" << (op + 1) << "_DECAY:" << p.opDecay[op] << "\n";
      file << "OP" << (op + 1) << "_SUSTAIN:" << p.opSustain[op] << "\n";
      file << "OP" << (op + 1) << "_RELEASE:" << p.opRelease[op] << "\n";
    }
    file.close();
    return true;
  }

  void saveAllCustomPresets(const std::string& dirPath) {
    DIR* dir = opendir(dirPath.c_str());
    if (dir) {
      struct dirent* entry;
      std::vector<std::string> toDelete;
      while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.rfind("custom_", 0) == 0) {
          toDelete.push_back(dirPath + "/" + name);
        }
      }
      closedir(dir);
      for (const auto& path : toDelete) {
        std::remove(path.c_str());
      }
    }

    for (size_t i = 0; i < mCustomPresets.size(); ++i) {
      std::string filename = "custom_" + std::to_string(i) + "_" + mCustomPresets[i].name + ".fmp";
      for (char &c : filename) {
        if (c == '/' || c == '\\' || c == ' ' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
          c = '_';
        }
      }
      savePresetToFile(mCustomPresets[i], dirPath + "/" + filename);
    }
  }

  bool deleteCustomPreset(int idx, const std::string& dirPath) {
    if (idx < 0 || idx >= (int)mCustomPresets.size()) return false;
    mCustomPresets.erase(mCustomPresets.begin() + idx);

    DIR* dir = opendir(dirPath.c_str());
    if (dir) {
      struct dirent* entry;
      std::vector<std::string> toDelete;
      while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.rfind("custom_", 0) == 0) {
          toDelete.push_back(dirPath + "/" + name);
        }
      }
      closedir(dir);
      for (const auto& path : toDelete) {
        std::remove(path.c_str());
      }
    }

    saveAllCustomPresets(dirPath);
    return true;
  }

  void renderBlock(float* outL, float* outR, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
      float s = render();
      outL[i] = s;
      outR[i] = s;
    }
  }

  float render() {
    float mixedOutput = 0.0f;
    int activeCount = 0;

    for (auto &v : mVoices) {
      if (!v.active)
        continue;
      // 1. Control Rate Phase (16-sample block)
      if (v.controlCounter % 16 == 0) {
        if (mGlide > 0.001f) {
           float glideTimeSamples = mGlide * mSampleRate * 0.5f;
           float glideAlpha = 16.0f / (glideTimeSamples + 16.0f);
           v.frequency += (v.targetFrequency - v.frequency) * glideAlpha;
        } else {
           v.frequency = v.targetFrequency;
        }
        
        // Pitch Sweep / Envelope logic
        v.pitchEnv *= (1.0f - v.pitchEnvDecay * 16.0f);
        if (v.pitchEnv < 0.0001f) v.pitchEnv = 0.0f;

        float pitchMod = 1.0f + (v.pitchEnv * mPitchSweepAmount);
        
        // Pitch Bend (Powf is expensive, do it at control rate)
        float bendFactor = powf(2.0f, mPitchBend / 12.0f);
        float finalFreq = v.frequency * bendFactor * pitchMod;

        for (int i = 0; i < 6; ++i) {
          v.operators[i].setFrequency(finalFreq, mOpRatios[i], mSampleRate);
          v.operators[i].processBlock();
        }
        v.masterEnv.processBlock(16, v.masterEnvStart, v.masterEnvDelta);
        v.filterEnv.processBlock(16, v.filterEnvStart, v.filterEnvDelta);
      }

      int blockPhase = v.controlCounter % 16;
      float mEnv = v.masterEnvStart + v.masterEnvDelta * blockPhase;
      if (mEnv < 0.0001f && !v.masterEnv.isActive()) {
        v.active = false;
        continue;
      }

      v.controlCounter++;
      activeCount++;
      
      float velModScale = v.amplitude * mBrightness;
      float modScale = mBrightness;
      float pitchMod = 1.0f; // Already baked into operator frequencies

      // Calculate op5 feedback
      float fbIn = (v.lastOp5Out + v.op5FeedbackHistory) * 0.5f * mFeedback;
      fbIn = fast_tanh(fbIn * (1.0f + mFeedbackDrive * 10.0f));

      float o[6] = {0.0f};
      // DX7 Algorithms 1-32 (0-indexed as 0-31)
      // Operators numbered 1-6 in DX7 docs, indexed 0-5 here
      // Op 6 (index 5) receives feedback. Carriers go to output via
      // mCarrierMask.
      switch (mAlgorithm) {
      case 0: // DX7 Alg 1: [6→5→4→3→2→1]
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 1: // DX7 Alg 2: [2→1], 6→5→4→3→(1) — op2 and stack feed op1
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample((o[2] + o[3]) * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 2: // DX7 Alg 3: [6→5→4→(1)], [3→2→1]
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] = v.operators[0].nextSample((o[1] + o[4]) * modScale, pitchMod, blockPhase) *
               mOpLevels[0];
        break;
      case 3: // DX7 Alg 4: [6→5→4→(1)], [3→2→(1)] — both stacks feed 1
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample((o[2] + o[4]) * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 4: // DX7 Alg 5: [6→5→(1)], [4→3→(1)], [2→1] — paired stacks
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 5: // DX7 Alg 6: [6→5→(1)], [4→3], [2→1] — 3 pairs, 3 carriers
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 6: // DX7 Alg 7: [6→5→4→3], [2→1] — carrier 1,3
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 7: // DX7 Alg 8: [6→5→4→3], [2→1] — 3 has feedback from 6, carrier
              // 1,3
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 8: // DX7 Alg 9: [6→5→4→3], [2→1] — carrier 1,3 (op2 has vibrato-like
              // routing)
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 9: // DX7 Alg 10: [3→2→1], [6→5→4] — carrier 1,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 10: // DX7 Alg 11: [6→5→4], [3→2→1] — carrier 1,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 11: // DX7 Alg 12: [4→3→2→1], [6→5] — carrier 1,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 12: // DX7 Alg 13: [4→3→2→1], [6→5] — carrier 1,5 (variant)
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 13: // DX7 Alg 14: [6→5→4→3→(1)], [2→1] — 5 feeds into 1
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] = v.operators[0].nextSample((o[1] + o[2]) * modScale, pitchMod, blockPhase) *
               mOpLevels[0];
        break;
      case 14: // DX7 Alg 15: [6→5→4→3→2→1] with branch — carrier 1
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] = v.operators[0].nextSample((o[1] + o[2]) * modScale, pitchMod, blockPhase) *
               mOpLevels[0];
        break;
      case 15: // DX7 Alg 16: [6→5→4→3→2→1] + 5→(1) — multi-mod carrier 1
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] = v.operators[0].nextSample((o[1] + o[4]) * modScale, pitchMod, blockPhase) *
               mOpLevels[0];
        break;
      case 16: // DX7 Alg 17: [6→5→4→3→(1)], [2→1] — 3+2 both mod 1
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] = v.operators[0].nextSample((o[1] + o[3]) * modScale, pitchMod, blockPhase) *
               mOpLevels[0];
        break;
      case 17: // DX7 Alg 18: [6→5→4→3], [3→2], [2→1] — chain with branch at 3
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] = v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[3];
        o[2] = v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[2];
        o[1] =
            v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) * mOpLevels[1];
        o[0] = v.operators[0].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 18: // DX7 Alg 19: [6→5→(4,3,2)], carrier 2,3,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] =
            v.operators[1].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[1];
        o[0] = v.operators[0].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 19: // DX7 Alg 20: [3→2→1], [6→(4,5)] — carrier 1,4,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 20: // DX7 Alg 21: [6→(5,4,3)], [2→1] — carrier 1,3,4,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 21: // DX7 Alg 22: [6→(5,4,3,2,1)] — all modulated by 6, 5 carriers
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] =
            v.operators[1].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 22: // DX7 Alg 23: [6→(5,4)], [3→2→1] — carrier 1,4,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 23: // DX7 Alg 24: [6→5→(4,3)], carrier 1,3,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 24: // DX7 Alg 25: [6→5→(4,3,2)], carrier 1,2,3,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] =
            v.operators[1].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[1];
        o[0] = v.operators[0].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 25: // DX7 Alg 26: [6→5→(4,3)], [2→1] — carrier 1,3,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 26: // DX7 Alg 27: [3→2→1], [6→5→4] — carrier 1,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] = v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 27: // DX7 Alg 28: [6→5→4], [3→2], [1] — carrier 1,2,4
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] = v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) *
               velModScale * mOpLevels[4];
        o[3] =
            v.operators[3].nextSample(o[4] * modScale, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[2];
        o[1] =
            v.operators[1].nextSample(o[2] * modScale, pitchMod, blockPhase) * mOpLevels[1];
        o[0] = v.operators[0].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 28: // DX7 Alg 29: [6→5], [4→3], [2→1] — 3 pairs, carrier 1,3,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[1];
        o[0] =
            v.operators[0].nextSample(o[1] * modScale, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 29: // DX7 Alg 30: [6→5], [4→3], [2], [1] — carrier 1,2,3,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * velModScale *
               mOpLevels[3];
        o[2] =
            v.operators[2].nextSample(o[3] * modScale, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[1];
        o[0] = v.operators[0].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[0];
        break;
      case 30: // DX7 Alg 31: [6→5], [4], [3], [2], [1] — carrier 1,2,3,4,5
        o[5] = v.operators[5].nextSample(fbIn, pitchMod, blockPhase) * velModScale *
               mOpLevels[5];
        o[4] =
            v.operators[4].nextSample(o[5] * modScale, pitchMod, blockPhase) * mOpLevels[4];
        o[3] = v.operators[3].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[3];
        o[2] = v.operators[2].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[2];
        o[1] = v.operators[1].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[1];
        o[0] = v.operators[0].nextSample(0.0f, pitchMod, blockPhase) * mOpLevels[0];
        break;
      default: // DX7 Alg 32: All parallel, all carriers (additive synthesis)
        for (int i = 0; i < 6; ++i)
          o[i] = v.operators[i].nextSample(i == 5 ? fbIn : 0.0f, pitchMod, blockPhase) *
                 mOpLevels[i];
        break;
      }

      float out = 0.0f;
      for (int i = 0; i < 6; ++i)
        if (mCarrierMask & (1 << i))
          out += o[i];

      v.op5FeedbackHistory = v.lastOp5Out;
      v.lastOp5Out = o[5];

      v.currentFilterEnvVal = v.filterEnvStart + v.filterEnvDelta * blockPhase;
      float envCutoffMod = v.currentFilterEnvVal * mFilterEnvAmount;
      float cutoffNormalized = std::max(0.001f, std::min(0.999f, mCutoff + envCutoffMod));
      if (blockPhase == 0) {
        float freq = 20.0f * powf(900.0f, cutoffNormalized);
        v.svf.setParams(freq, 0.7f + mResonance * 4.0f, mSampleRate);
      }
      float filtered =
          v.svf.process(out * v.amplitude * mEnv, (TSvf::Type)mFilterMode);
      // v3.1.40: Remove engine-level fast_tanh and add 6dB headroom to avoid double-saturation.
      mixedOutput += filtered;
    }
    
    // Proper normalization table usage for FM
    static const float normTable[17] = {0.0f, 1.0f, 0.7071f, 0.5773f, 0.5f, 0.4472f, 0.4082f, 0.3779f, 0.3535f, 0.3333f, 0.3162f, 0.3015f, 0.2886f, 0.2773f, 0.2672f, 0.2581f, 0.25f};
    float norm = (activeCount >= 0 && activeCount <= 16) ? normTable[activeCount] : 0.25f;
    return mixedOutput * norm * 0.5f;
  }

  bool isActive() const {
    for (const auto &v : mVoices)
      if (v.active)
        return true;
    return false;
  }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (const auto &v : mVoices) {
      if (v.active) {
        maxEnv = std::max(maxEnv, v.masterEnv.getValue());
      }
    }
    return maxEnv;
  }

private:
  std::vector<Voice> mVoices;
  std::vector<float> mOpLevels, mOpRatios, mOpAttack, mOpDecay, mOpSustain,
      mOpRelease;
  float mCutoff = 1.0f, mResonance = 0.0f, mBrightness = 1.0f, mDetune = 0.0f,
        mFeedback = 0.0f, mFeedbackDrive = 0.0f;
  float mAttack = 0.01f, mDecay = 0.1f, mSustain = 1.0f, mRelease = 0.2f;
  float mFilterAttack = 0.01f, mFilterDecay = 0.1f, mFilterSustain = 1.0f, mFilterRelease = 0.2f;
  float mFilterEnvAmount = 0.0f;
  int mAlgorithm = 0, mCarrierMask = 1, mActiveMask = 63, mFilterMode = 0;
  float mSampleRate = 48000.0f, mFrequency = 440.0f, mLastFrequency = 440.0f,
        mGlide = 0.0f;
  float mPitchSweepAmount = 0.0f;
  float mPitchBend = 0.0f;
  bool mUseEnvelope = true, mIgnoreNoteFrequency = false;
};

#endif
