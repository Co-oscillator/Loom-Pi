#ifndef LFO_ENGINE_H
#define LFO_ENGINE_H

#include "../Utils.h"
#include <cmath>
#include <cstdlib>

enum class LfoShape { Sine, Triangle, Square, Saw, Random };

class LfoEngine {
public:
  LfoEngine() {}

  void setParameters(float frequency, float depth, int shape, bool sync) {
    mFrequency = frequency;
    mDepth = depth;
    mShape = static_cast<LfoShape>(shape);
    mSync = sync;
  }

  void setFrequency(float f) { mFrequency = f; }
  void setUiRate(float r) { mUiRate = r; }
  void setDepth(float d) { mDepth = d; }
  void setShape(int s) { mShape = static_cast<LfoShape>(s); }
  void setSync(bool s) { mSync = s; }

  float getFrequency() const { return mFrequency; }
  float getUiRate() const { return mUiRate; }
  float getDepth() const { return mDepth; }
  int getShape() const { return static_cast<int>(mShape); }
  bool getSync() const { return mSync; }

  void reset(float sampleRate) { mPhase = 0.0f; }

  void setBpm(float bpm) { mBpm = bpm; }

  void advance(float sampleRate) {
    float effectiveFreq = mFrequency;
    if (mSync) {
      float beatFreq = mBpm / 60.0f;
      int syncIdx = (int)(mUiRate * 22.99f);
      switch (syncIdx) {
      case 0:
        effectiveFreq = beatFreq / 128.0f;
        break; // 32/1
      case 1:
        effectiveFreq = beatFreq / 96.0f;
        break; // 24/1
      case 2:
        effectiveFreq = beatFreq / 64.0f;
        break; // 16/1
      case 3:
        effectiveFreq = beatFreq / 48.0f;
        break; // 12/1
      case 4:
        effectiveFreq = beatFreq / 32.0f;
        break; // 8/1
      case 5:
        effectiveFreq = beatFreq / 24.0f;
        break; // 6/1
      case 6:
        effectiveFreq = beatFreq / 16.0f;
        break; // 4/1
      case 7:
        effectiveFreq = beatFreq / 12.0f;
        break; // 3/1
      case 8:
        effectiveFreq = beatFreq / 8.0f;
        break; // 2/1
      case 9:
        effectiveFreq = beatFreq / 4.0f;
        break; // 1/1
      case 10:
        effectiveFreq = beatFreq / 2.0f;
        break; // 1/2
      case 11:
        effectiveFreq = beatFreq * 0.75f;
        break; // 1/3
      case 12:
        effectiveFreq = beatFreq;
        break; // 1/4
      case 13:
        effectiveFreq = beatFreq * 1.5f;
        break; // 1/6
      case 14:
        effectiveFreq = beatFreq * 2.0f;
        break; // 1/8
      case 15:
        effectiveFreq = beatFreq * 3.0f;
        break; // 1/12
      case 16:
        effectiveFreq = beatFreq * 4.0f;
        break; // 1/16
      case 17:
        effectiveFreq = beatFreq * 6.0f;
        break; // 1/24
      case 18:
        effectiveFreq = beatFreq * 8.0f;
        break; // 1/32
      case 19:
        effectiveFreq = beatFreq * 12.0f;
        break; // 1/48
      case 20:
        effectiveFreq = beatFreq * 16.0f;
        break; // 1/64
      case 21:
        effectiveFreq = beatFreq * 18.0f;
        break; // 1/72
      case 22:
        effectiveFreq = beatFreq * 24.0f;
        break; // 1/96
      default:
        effectiveFreq = beatFreq;
      }
    }
    float phaseInc = effectiveFreq / sampleRate;
    mPhase += phaseInc;
    if (mPhase >= 1.0f) {
      mPhase = fmodf(mPhase, 1.0f);
      updateRandom();
    }

    float out = 0.0f;
    switch (mShape) {
    case LfoShape::Sine:
      out = FastSine::get(mPhase);
      break;
    case LfoShape::Triangle:
      out = (mPhase < 0.5f) ? (4.0f * mPhase - 1.0f) : (3.0f - 4.0f * mPhase);
      break;
    case LfoShape::Square:
      out = (mPhase < 0.5f) ? 1.0f : -1.0f;
      break;
    case LfoShape::Saw:
      out = 2.0f * mPhase - 1.0f;
      break;
    case LfoShape::Random:
      out = mRandomValue;
      break;
    }

    mLastOutput = out * mDepth;
  }

  float process(float sampleRate, int numFrames = 1) {
    float effectiveFreq = mFrequency;
    if (mSync) {
      float beatFreq = mBpm / 60.0f;
      int syncIdx = (int)(mUiRate * 22.99f);
      switch (syncIdx) {
      case 0:
        effectiveFreq = beatFreq / 128.0f;
        break; // 32/1
      case 1:
        effectiveFreq = beatFreq / 96.0f;
        break; // 24/1
      case 2:
        effectiveFreq = beatFreq / 64.0f;
        break; // 16/1
      case 3:
        effectiveFreq = beatFreq / 48.0f;
        break; // 12/1
      case 4:
        effectiveFreq = beatFreq / 32.0f;
        break; // 8/1
      case 5:
        effectiveFreq = beatFreq / 24.0f;
        break; // 6/1
      case 6:
        effectiveFreq = beatFreq / 16.0f;
        break; // 4/1
      case 7:
        effectiveFreq = beatFreq / 12.0f;
        break; // 3/1
      case 8:
        effectiveFreq = beatFreq / 8.0f;
        break; // 2/1
      case 9:
        effectiveFreq = beatFreq / 4.0f;
        break; // 1/1
      case 10:
        effectiveFreq = beatFreq / 2.0f;
        break; // 1/2
      case 11:
        effectiveFreq = beatFreq * 0.75f;
        break; // 1/3
      case 12:
        effectiveFreq = beatFreq;
        break; // 1/4
      case 13:
        effectiveFreq = beatFreq * 1.5f;
        break; // 1/6
      case 14:
        effectiveFreq = beatFreq * 2.0f;
        break; // 1/8
      case 15:
        effectiveFreq = beatFreq * 3.0f;
        break; // 1/12
      case 16:
        effectiveFreq = beatFreq * 4.0f;
        break; // 1/16
      case 17:
        effectiveFreq = beatFreq * 6.0f;
        break; // 1/24
      case 18:
        effectiveFreq = beatFreq * 8.0f;
        break; // 1/32
      case 19:
        effectiveFreq = beatFreq * 12.0f;
        break; // 1/48
      case 20:
        effectiveFreq = beatFreq * 16.0f;
        break; // 1/64
      case 21:
        effectiveFreq = beatFreq * 18.0f;
        break; // 1/72
      case 22:
        effectiveFreq = beatFreq * 24.0f;
        break; // 1/96
      default:
        effectiveFreq = beatFreq;
      }
    }

    float phaseInc = (effectiveFreq / sampleRate) * numFrames;
    mPhase += phaseInc;
    if (mPhase >= 1.0f) {
      mPhase = fmodf(mPhase, 1.0f);
      updateRandom();
    }

    float out = 0.0f;
    switch (mShape) {
    case LfoShape::Sine:
      out = FastSine::get(mPhase);
      break;
    case LfoShape::Triangle:
      out = (mPhase < 0.5f) ? (4.0f * mPhase - 1.0f) : (3.0f - 4.0f * mPhase);
      break;
    case LfoShape::Square:
      out = (mPhase < 0.5f) ? 1.0f : -1.0f;
      break;
    case LfoShape::Saw:
      out = 2.0f * mPhase - 1.0f;
      break;
    case LfoShape::Random:
      out = mRandomValue;
      break;
    }

    mLastOutput = out * mDepth;
    return mLastOutput;
  }

  float getCurrentValue() const { return mLastOutput; }

private:
  void updateRandom() {
    mRandomValue = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
  }

  float mPhase = 0.0f;
  float mLastOutput = 0.0f;
  float mFrequency = 1.0f; // Hz
  float mUiRate = 0.5f;    // Raw 0..1 Parameter
  float mDepth = 1.0f;
  LfoShape mShape = LfoShape::Sine;
  bool mSync = false;
  float mBpm = 80.0f;
  float mRandomValue = 0.0f;
};

#endif
