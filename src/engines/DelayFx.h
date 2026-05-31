#ifndef DELAY_FX_H
#define DELAY_FX_H

#include "../Utils.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace DelayDetails {
class TinyAllPass {
public:
  void setBufferSize(int size) {
    mBuffer.assign(size, 0.0f);
    mSize = size;
  }
  float process(float input, float feedback) {
    float delayed = mBuffer[mReadPos];
    float out = -input + delayed;
    // Anti-denormal injection for internal feedback loop
    float newVal = input + delayed * feedback;
    mBuffer[mReadPos] = newVal;
    if (std::abs(mBuffer[mReadPos]) < 1.0e-9f)
      mBuffer[mReadPos] = 0.0f;

    mReadPos = (mReadPos + 1) % mSize;
    return out;
  }

private:
  std::vector<float> mBuffer;
  int mSize = 0;
  int mReadPos = 0;
};
} // namespace DelayDetails

class DelayFx {
public:
  DelayFx(int maxDelayFrames = 192000) {
    mBufferL.assign(maxDelayFrames, 0.0f);
    mBufferR.assign(maxDelayFrames, 0.0f);
    for (int i = 0; i < 3; ++i) {
      mDiffL[i].setBufferSize(150 + i * 77);
      mDiffR[i].setBufferSize(163 + i * 81);
    }
  }

  void setSampleRate(float sr) { mSampleRate = sr; }

  void setDelay(float frames) {
    if (frames < mBufferL.size())
      mTargetDelayFrames = frames;
  }
  void setDelayTime(float value) {
    // User requested max 1500ms
    float maxFrames =
        1.5f * 48000.0f; // Keep max buffer reference const or dynamic?
    // Ideally we track sample rate in class. For now, assume 48k for buffer
    // calc BUT we must use current rate for delay time calc. Actually,
    // mTargetDelayFrames is absolute frames. If setDelayTime(1.0) means 1
    // second, it should require Sample Rate. We will update setDelayTime to
    // rely on a member mSampleRate.
    float maxFramesRef = 1.5f * mSampleRate;
    mTargetDelayFrames = value * maxFramesRef;
    if (mTargetDelayFrames < 1.0f)
      mTargetDelayFrames = 1.0f;
  }

  void setFeedback(float feedback) {
    mTargetFeedback = feedback;
    // TAPE mode usually needs a slight boost for self-oscillation but
    // 1.1f might be too hot with the 1.5f drive boost.
    // We'll scale it in the process loop instead.
  }
  void setMix(float mix) { mTargetMix = mix; }
  void setFilterMix(float mix) { mTargetFilterMix = mix; }
  void setFilterResonance(float res) { mTargetResonance = res; }

  void setFilterMode(int mode) {
    mFilterMode = (mode < 0) ? 0 : (mode > 2 ? 2 : mode);
  }

  void setFilter(float filterMix, float resonance) {
    mTargetFilterMix = filterMix;
    mTargetResonance = resonance;
  }

  void setType(int type) {
    mType = type;
  } // 0=Digital, 1=Tape, 2=PingPong, 3=Reverse

  void clear() {
    std::fill(mBufferL.begin(), mBufferL.end(), 0.0f);
    std::fill(mBufferR.begin(), mBufferR.end(), 0.0f);
    mWriteIndex = 0;
    mSvfZ1L = mSvfZ2L = mSvfZ1R = mSvfZ2R = 0.0f;
    mSmoothedDelay = mTargetDelayFrames;
    mFeedback = mTargetFeedback;
    mMix = mTargetMix;
    mFilterMix = mTargetFilterMix;
    mResonance = mTargetResonance;
  }

  void processStereo(float inL, float inR, float &outL, float &outR,
                     float sampleRate = 48000.0f) {
    if (!std::isfinite(inL))
      inL = 0.0f;
    if (!std::isfinite(inR))
      inR = 0.0f;

    // Smooth Transitions
    if (!std::isfinite(mTargetDelayFrames))
      mTargetDelayFrames = 11025.0f;
    if (!std::isfinite(mTargetFeedback))
      mTargetFeedback = 0.5f;
    if (!std::isfinite(mTargetMix))
      mTargetMix = 0.5f;
    if (!std::isfinite(mTargetFilterMix))
      mTargetFilterMix = 0.5f;
    if (!std::isfinite(mTargetResonance))
      mTargetResonance = 0.0f;

    mSmoothedDelay += 0.001f * (mTargetDelayFrames - mSmoothedDelay);
    if (!std::isfinite(mSmoothedDelay))
      mSmoothedDelay = mTargetDelayFrames;

    mFeedback += 0.001f * (mTargetFeedback - mFeedback);
    mMix += 0.001f * (mTargetMix - mMix);
    mFilterMix += 0.001f * (mTargetFilterMix - mFilterMix);
    mResonance += 0.001f * (mTargetResonance - mResonance);

    float delayedL = 0.0f, delayedR = 0.0f;

    // Read from Delay Lines
    float bufferSize = (float)mBufferL.size();
    if (bufferSize < 4.0f)
      return; // Extreme safety

    float safeDelay = mSmoothedDelay;
    if (!std::isfinite(safeDelay) || safeDelay < 0.0f)
      safeDelay = 1.0f;
    if (safeDelay > bufferSize - 2.0f)
      safeDelay = bufferSize - 2.0f;

    // mWriteIndex points to the NEXT write.
    // Recent sample is at mWriteIndex - 1.
    float rp = (float)mWriteIndex - 1.0f - safeDelay;
    while (rp < 0.0f)
      rp += bufferSize;

    int i0 = static_cast<int>(rp);
    if (i0 < 0)
      i0 = 0;
    if (i0 >= (int)mBufferL.size())
      i0 = (int)mBufferL.size() - 1;

    int i1 = (i0 + 1) % mBufferL.size();
    float frac = rp - static_cast<float>(i0);
    if (frac < 0.0f)
      frac = 0.0f;
    if (frac > 1.0f)
      frac = 1.0f;

    delayedL = mBufferL[i0] * (1.0f - frac) + mBufferL[i1] * frac;
    delayedR = mBufferR[i0] * (1.0f - frac) + mBufferR[i1] * frac;

    if (mType == 1) { // Tape
      delayedL = fast_tanh(delayedL * 1.2f);
      delayedR = fast_tanh(delayedR * 1.2f);
    }

    // Filter Logic (Per Channel SVF)
    auto processFilter = [&](float input, float &z1, float &z2, float g,
                             float k) {
      float a1 = 1.0f / (1.0f + g * (g + k));
      float a2 = g * a1;
      float a3 = g * a2;
      float v3 = input - z2;
      float v1 = a1 * z1 + a2 * v3;
      float v2 = z2 + a2 * z1 + a3 * v3;
      z1 = 2.0f * v1 - z1;
      z2 = 2.0f * v2 - z2;
      if (std::abs(z1) < 1.0e-9f)
        z1 = 0.0f;
      if (std::abs(z2) < 1.0e-9f)
        z2 = 0.0f;

      switch (mFilterMode) {
      case 0:
        return v2; // LP
      case 1:
        return input - k * v1 - v2; // HP
      case 2:
        return v1; // BP
      default:
        return v2;
      }
    };

    // Cutoff mapping: 20Hz to 20kHz
    float cutoff = 20.0f + (mFilterMix * mFilterMix * 19980.0f);
    cutoff = std::max(20.0f, std::min(sampleRate * 0.45f, cutoff));

    float g = tanf(M_PI * cutoff / sampleRate);
    float k = 2.0f - (mResonance * 1.95f);

    float filteredL = processFilter(delayedL, mSvfZ1L, mSvfZ2L, g, k);
    float filteredR = processFilter(delayedR, mSvfZ1R, mSvfZ2R, g, k);

    // NaN/Inf guard on filter state (can blow up with extreme resonance +
    // feedback)
    if (!std::isfinite(mSvfZ1L) || !std::isfinite(mSvfZ2L)) {
      mSvfZ1L = mSvfZ2L = 0.0f;
    }
    if (!std::isfinite(mSvfZ1R) || !std::isfinite(mSvfZ2R)) {
      mSvfZ1R = mSvfZ2R = 0.0f;
    }
    if (!std::isfinite(filteredL))
      filteredL = 0.0f;
    if (!std::isfinite(filteredR))
      filteredR = 0.0f;

    // Cross-Feedback / Ping-Pong
    float nextL = 0, nextR = 0;
    float currentFb = mFeedback;
    if (mType == 1)
      currentFb *=
          0.8f; // Reduced to 0.8f to compensate for 1.2f Drive (Net ~0.96)

    if (mType == 2) { // Ping-Pong
      float monoIn = (inL + inR) * 0.707f;
      nextL = monoIn + filteredR * currentFb;
      nextR = filteredL * currentFb;
    } else {
      nextL = inL + filteredL * currentFb;
      nextR = inR + filteredR * currentFb;
    }

    // Hard limiter on feedback to prevent runaway (clamp to ±4.0 before tanh)
    nextL = std::max(-4.0f, std::min(4.0f, nextL));
    nextR = std::max(-4.0f, std::min(4.0f, nextR));

    // Denormal prevention
    if (std::abs(nextL) < 1.0e-9f)
      nextL = 0.0f;
    if (std::abs(nextR) < 1.0e-9f)
      nextR = 0.0f;

    mBufferL[mWriteIndex] = fast_tanh(nextL);
    mBufferR[mWriteIndex] = fast_tanh(nextR);
    if (mBufferL.size() > 0)
      mWriteIndex = (mWriteIndex + 1) % mBufferL.size();
    else
      mWriteIndex = 0;

    // Diffusion Smear (Lushness)
    for (int i = 0; i < 3; i++) {
      filteredL = mDiffL[i].process(filteredL, 0.5f);
      filteredR = mDiffR[i].process(filteredR, 0.5f);
    }

    // Final NaN guard and hard clamp on output
    if (!std::isfinite(filteredL))
      filteredL = 0.0f;
    if (!std::isfinite(filteredR))
      filteredR = 0.0f;

    outL = std::max(-1.0f, std::min(1.0f, filteredL * mMix));
    outR = std::max(-1.0f, std::min(1.0f, filteredR * mMix));

    // Silence tracking
    if (std::abs(outL) < 1e-9f && std::abs(outR) < 1e-9f) {
      if (mSilentCounter < 48000)
        mSilentCounter++;
    } else {
      mSilentCounter = 0;
    }
  }

  bool isSilent() const { return mSilentCounter >= 48000; }

  float process(float input, float sampleRate = 48000.0f) {
    float l = 0, r = 0;
    processStereo(input, input, l, r, sampleRate);
    return (l + r) * 0.5f;
  }

private:
  std::vector<float> mBufferL;
  std::vector<float> mBufferR;
  int mWriteIndex = 0;
  float mTargetDelayFrames = 11025.0f;
  float mSmoothedDelay = 11025.0f;
  int mCounter = 1;
  int mPingPongCounter = 0;

  float mFeedback = 0.5f, mTargetFeedback = 0.5f;
  float mMix = 0.5f, mTargetMix = 0.5f;
  float mFilterMix = 0.5f, mTargetFilterMix = 0.5f;
  float mResonance = 0.0f, mTargetResonance = 0.0f;
  float mSvfZ1L = 0.0f, mSvfZ2L = 0.0f;
  float mSvfZ1R = 0.0f, mSvfZ2R = 0.0f;
  int mType = 0;
  int mFilterMode = 0; // 0=LP, 1=HP, 2=BP
  DelayDetails::TinyAllPass mDiffL[3], mDiffR[3];
  uint32_t mSilentCounter = 48000;
  float mSampleRate = 48000.0f;
};

#endif // DELAY_FX_H
