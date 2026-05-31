#ifndef ENVELOPE_FOLLOWER_H
#define ENVELOPE_FOLLOWER_H

#include <algorithm>
#include <cmath>

class EnvelopeFollower {
public:
  void setParameters(float attackMs, float releaseMs, float sampleRate) {
    mAttack = expf(-1.0f / (attackMs * 0.001f * sampleRate));
    mRelease = expf(-1.0f / (releaseMs * 0.001f * sampleRate));
  }

  float process(float input) {
    float absInput = fabsf(input);
    if (absInput > mEnvelope) {
      mEnvelope = mAttack * mEnvelope + (1.0f - mAttack) * absInput;
    } else {
      mEnvelope = mRelease * mEnvelope + (1.0f - mRelease) * absInput;
    }

    if (mEnvelope < 1e-9f)
      mEnvelope = 0.0f;
    return mEnvelope;
  }

  float getLevel() const { return mEnvelope; }

private:
  float mEnvelope = 0.0f;
  float mAttack = 0.99f;
  float mRelease = 0.999f;
};

#endif // ENVELOPE_FOLLOWER_H
