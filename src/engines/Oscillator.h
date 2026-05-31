#ifndef OSCILLATOR_H
#define OSCILLATOR_H


#include <cmath>
#include <cstdint>
#include "../Utils.h"

enum class Waveform { Sine, Triangle, Square, Sawtooth };

class Oscillator {
public:
  void setFrequency(float frequency, float sampleRate) {
    // 2^32 = 4294967296.0
    mPhaseIncrement = (uint32_t)((frequency / sampleRate) * 4294967296.0);
  }

  void setWaveform(Waveform waveform) {
    mWaveform = waveform;
    mMorphActive = false;
  }

  void setMorphValue(float value) {
    mMorphValue = value;
    mMorphActive = true;
  }

  void setWaveShape(float shape) {
    mShape = shape; // 0.0 to 1.0, affects pulse width or morphing
    mShapeInt = (uint32_t)(mShape * 4294967296.0);
  }

  bool hasWrapped() const {
    return (mPhase + mPhaseIncrement) < mPhase; // Integer overflow
  }

  void resetPhase() { mPhase = 0; }

  float foldWave(float sample, float amount) {
    if (amount <= 0.0f)
      return sample;
    float threshold = 1.0f - (amount * 0.9f);
    if (threshold < 0.1f)
      threshold = 0.1f;

    // Recursive folding
    while (std::abs(sample) > threshold) {
      if (sample > threshold) {
        sample = threshold - (sample - threshold);
      } else if (sample < -threshold) {
        sample = -threshold - (sample + threshold);
      }
    }
    return sample / threshold;
  }

  float getShapeSample(Waveform wf, uint32_t phaseWithMod) const {
    switch (wf) {
    case Waveform::Sine:
      return FastSine::getInt(phaseWithMod);
    case Waveform::Triangle: {
      float phaseF = phaseWithMod * 2.3283064365386963e-10f; // / 2^32
      float tri;
      if (mShape == 0.5f) {
        // Fast path for 50% triangle
        tri = 2.0f * fabsf(2.0f * phaseF - 1.0f) - 1.0f;
      } else {
        if (phaseF < mShape) {
          tri = (phaseF / mShape) * 2.0f - 1.0f;
        } else {
          tri = 1.0f - ((phaseF - mShape) / (1.0f - mShape)) * 2.0f;
        }
      }
      return tri;
    }
    case Waveform::Square:
      return (phaseWithMod < mShapeInt) ? 1.0f : -1.0f;
    case Waveform::Sawtooth: {
      float phaseF = phaseWithMod * 2.3283064365386963e-10f; // / 2^32
      return 2.0f * phaseF - 1.0f;
    }
    }
    return 0.0f;
  }

  float nextSample(uint32_t modulation = 0, float fmFreqMult = 1.0f,
                   float waveFold = 0.0f) {
    uint32_t phaseWithMod = mPhase + modulation;

    float sample = 0.0f;

    if (!mMorphActive) {
      sample = getShapeSample(mWaveform, phaseWithMod);
    } else {
      float v = mMorphValue;
      float detent = 0.05f;
      if (v < detent)
        v = 0.0f;
      else if (v > 0.333333f - detent && v < 0.333333f + detent)
        v = 0.333333f;
      else if (v > 0.666666f - detent && v < 0.666666f + detent)
        v = 0.666666f;
      else if (v > 1.0f - detent)
        v = 1.0f;

      if (v <= 0.333333f) {
        float mix = v * 3.0f;
        sample = getShapeSample(Waveform::Sine, phaseWithMod) * (1.0f - mix) +
                 getShapeSample(Waveform::Triangle, phaseWithMod) * mix;
      } else if (v <= 0.666666f) {
        float mix = (v - 0.333333f) * 3.0f;
        sample =
            getShapeSample(Waveform::Triangle, phaseWithMod) * (1.0f - mix) +
            getShapeSample(Waveform::Sawtooth, phaseWithMod) * mix;
      } else {
        float mix = (v - 0.666666f) * 3.0f;
        sample =
            getShapeSample(Waveform::Sawtooth, phaseWithMod) * (1.0f - mix) +
            getShapeSample(Waveform::Square, phaseWithMod) * mix;
      }
    }

    if (waveFold > 0.01f) {
      sample = foldWave(sample, waveFold);
    }

    mPhase += (uint32_t)(mPhaseIncrement * fmFreqMult);

    return sample;
  }

private:
  uint32_t mPhase = 0;
  uint32_t mPhaseIncrement = 0;
  float mShape = 0.5f; // Default square pulse width
  uint32_t mShapeInt = 2147483648; // 0.5 * 2^32
  Waveform mWaveform = Waveform::Sine;
  float mMorphValue = 0.0f;
  bool mMorphActive = false;
};

#endif // OSCILLATOR_H
