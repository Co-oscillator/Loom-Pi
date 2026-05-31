#ifndef COMPRESSOR_FX_H
#define COMPRESSOR_FX_H

#include <algorithm>
#include <cmath>

class CompressorFx {
public:
  void setThreshold(float dB) { mThreshold = powf(10.0f, dB / 20.0f); }
  void setRatio(float ratio) { mRatio = std::max(1.0f, ratio); }
  void setAttack(float ms) {
    mAttackRate = ms > 0 ? 1.0f - expf(-1.0f / (ms * 0.001f * 44100.0f)) : 1.0f;
  }
  void setRelease(float ms) {
    mReleaseRate =
        ms > 0 ? 1.0f - expf(-1.0f / (ms * 0.001f * 44100.0f)) : 1.0f;
  }
  void setMakeup(float dB) { mMakeup = powf(10.0f, dB / 20.0f); }

  float process(float input, float sidechain) {
    // Level detection on sidechain signal
    float absSide = std::abs(sidechain);

    // Envelope detector
    if (absSide > mEnvelope) {
      mEnvelope += mAttackRate * (absSide - mEnvelope);
    } else {
      mEnvelope += mReleaseRate * (absSide - mEnvelope);
    }

    if (!std::isfinite(mEnvelope))
      mEnvelope = 0.0f;

    // Gain processing
    float gain = 1.0f;
    if (mEnvelope > 0.0001f && mThreshold > 0.0f) {
      float detDb = 20.0f * log10f(mEnvelope);
      float threshDb = 20.0f * log10f(mThreshold);
      float overDb = detDb - threshDb;

      // Soft Knee (3dB knee)
      float knee = 3.0f;
      if (overDb > -knee) {
        if (overDb < knee) {
          // Inside knee
          overDb = (overDb + knee) * (overDb + knee) / (4.0f * knee);
        }
        float reductionDb = overDb * (1.0f - 1.0f / mRatio);
        gain = powf(10.0f, -reductionDb / 20.0f);
      }
    }

    if (!std::isfinite(gain))
      gain = 1.0f;

    float out = input * gain * mMakeup;
    // Soft clip at 0dB to prevent harsh digital distortion
    return std::tanh(out);
  }

private:
  float mThreshold = 0.5f;
  float mRatio = 4.0f;
  float mAttackRate = 0.01f;
  float mReleaseRate = 0.001f;
  float mMakeup = 1.0f;
  float mEnvelope = 0.0f;
};

#endif // COMPRESSOR_FX_H
