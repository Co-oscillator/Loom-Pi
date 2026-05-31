#ifndef FM_OPERATOR_H
#define FM_OPERATOR_H

#include "Adsr.h"
#include "Oscillator.h"

class FmOperator {
public:
  FmOperator() { mOscillator.setWaveform(Waveform::Sine); }

  void setFrequency(float baseFrequency, float ratio, float sampleRate) {
    mRatio = ratio;
    mEnv.setSampleRate(sampleRate);
    mOscillator.setFrequency(baseFrequency * mRatio, sampleRate);
  }

  void setADSR(float a, float d, float s, float r) {
    mEnv.setParameters(a, d, s, r);
  }

  void trigger() {
    mIsNoteHeld = true;
    mOscillator.resetPhase();
    mEnv.trigger();
  }

  void release() {
    mIsNoteHeld = false;
    mEnv.release();
  }

  void reset() {
    mIsNoteHeld = false;
    mEnv.reset();
  }

  float nextSample(float modulation = 0.0f, float fmFreqMult = 1.0f) {
    float envVal =
        mUseEnvelope ? mEnv.nextValue() : (mIsNoteHeld ? 1.0f : 0.0f);
    // Optimization: If envelope value is effectively zero, skip specific
    // processing
    if (envVal < 0.0001f)
      return 0.0f;

    return mOscillator.nextSample(modulation, fmFreqMult) * mLevel * envVal;
  }

  void setLevel(float level) { mLevel = level; }
  void setRatio(float ratio) { mRatio = ratio; }
  void setUseEnvelope(bool use) { mUseEnvelope = use; }
  bool isActive() const { return mIsNoteHeld || mEnv.isActive(); }
  bool isNoteHeld() const { return mIsNoteHeld; }

private:
  Oscillator mOscillator;
  float mLevel = 1.0f;
  float mRatio = 1.0f;
  Adsr mEnv;
  bool mIsNoteHeld = false;
  bool mUseEnvelope = true;
};

#endif // FM_OPERATOR_H
