#ifndef GALACTIC_REVERB_H
#define GALACTIC_REVERB_H

#include <algorithm>
#include <cmath>
#include <vector>

// Helper Classes for Dattorro Reverb
namespace Galactic {

class DelayLine {
public:
  void setBufferSize(int size) {
    mBuffer.assign(size, 0.0f);
    mSize = size;
    mWritePos = 0;
  }

  void clear() {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWritePos = 0;
  }

  inline void write(float input) {
    mBuffer[mWritePos] = input;
    if (++mWritePos >= mSize)
      mWritePos = 0;
  }

  inline float read(int delaySamps) const {
    // mWritePos points to the index for the NEXT write.
    // The most recent sample is at mWritePos - 1.
    int readPos = (mWritePos - 1) - delaySamps;
    while (readPos < 0)
      readPos += mSize;
    return mBuffer[readPos];
  }

  // Linear Interpolation for Modulation
  inline float readMod(float delaySamps) const {
    int i1 = (int)delaySamps;
    int i2 = i1 + 1;
    float frac = delaySamps - i1;

    int p1 = mWritePos - i1;
    while (p1 < 0)
      p1 += mSize;
    int p2 = mWritePos - i2;
    while (p2 < 0)
      p2 += mSize;

    return mBuffer[p1] * (1.0f - frac) + mBuffer[p2] * frac;
  }

private:
  std::vector<float> mBuffer;
  int mSize = 0;
  int mWritePos = 0;
};

class AllPass {
public:
  void setBufferSize(int size) { mDelay.setBufferSize(size); }
  void clear() { mDelay.clear(); }
  inline float process(float input, float feedback) {
    float delayed = mDelay.read(mDelaySize - 1);
    float output = -input + delayed;
    // float feedIn = input + output * someFeedback... simpler structure:
    // Standard Schroeder Allpass: out(n) = -g*in(n) + x(n-D); x(n) = in(n) +
    // g*x(n-D) Dattorro uses: input + delayed * 0.5 for stability? Let's stick
    // to standard:
    float bufOut = mDelay.read(mDelaySize);
    float inToDelay = input + bufOut * feedback;

    // Safety Clamp for internal node
    if (!std::isfinite(inToDelay))
      inToDelay = 0.0f;
    inToDelay = std::max(-2.0f, std::min(2.0f, inToDelay));

    mDelay.write(inToDelay);
    return bufOut - input; // or bufOut - inToDelay*feedback
  }

  // Actually Dattorro flow is often:
  // y = x - k*w
  // w = z^-1 ( x + k*w )
  // We'll implementation "input + feedback * delayed" style for diffusion
  float processDiffusion(float input, float feedback) {
    // Based on Jon Dattorro's paper "Effect Design Part 1"
    float bufOut = mDelay.read(mDelaySize);
    float inVal = input + bufOut * feedback;
    mDelay.write(inVal);
    return bufOut - inVal * feedback;
  }

  inline float read(int delaySamps) const { return mDelay.read(delaySamps); }

  void setSize(int s) { mDelaySize = s; }

private:
  DelayLine mDelay;
  int mDelaySize = 0;
};

} // namespace Galactic

class GalacticReverb {
public:
  GalacticReverb() {
    mPreDelay.setBufferSize(9600); // 200ms max

    // Dattorro fixed sizes (approximate for 48kHz)
    mInputAP[0].setBufferSize(1000);
    mInputAP[0].setSize(142);
    mInputAP[1].setBufferSize(1000);
    mInputAP[1].setSize(107);
    mInputAP[2].setBufferSize(1000);
    mInputAP[2].setSize(379);
    mInputAP[3].setBufferSize(1000);
    mInputAP[3].setSize(277);

    // Tank sizes
    mDelayL.setBufferSize(8000);
    mLoopAPL.setBufferSize(4000);
    mLoopAPL.setSize(672);
    mDelayAfterAPL.setBufferSize(6000);

    mDelayR.setBufferSize(8000);
    mLoopAPR.setBufferSize(4000);
    mLoopAPR.setSize(908);
    mDelayAfterAPR.setBufferSize(6000);
  }

  void clear() {
    mPreDelay.clear();
    for (int i = 0; i < 4; i++)
      mInputAP[i].clear();
    mLoopAPL.clear();
    mLoopAPR.clear();
    mDelayL.clear();
    mDelayR.clear();
    mDelayAfterAPL.clear();
    mDelayAfterAPR.clear();
    mFilterL = mFilterR = 0.0f;
    mToneFilterL = mToneFilterR = 0.0f;
    mModPhase = 0.0f;
  }

  void processStereoWet(float inL, float inR, float &outL, float &outR) {
    if (!std::isfinite(inL) || !std::isfinite(inR)) {
      // If input is bad, we MUST clear the tank, otherwise it stays broken
      // forever.
      clear();
      outL = outR = 0.0f;
      return;
    }

    float mono = (inL + inR) * 0.5f;

    // Anti-denormal injection (DC offset to prevent CPU spikes at silence)
    mono += 1.0e-18f;

    // Pre-Delay
    mPreDelay.write(mono);
    // Ensure pre-delay value is safe
    float readPoint = mPreDelayMilli * 0.001f * mSampleRate;
    if (readPoint < 0.0f)
      readPoint = 0.0f;
    if (readPoint > 9590.0f)
      readPoint = 9590.0f;

    float input = mPreDelay.read((int)readPoint);

    // Diffusion
    input = mInputAP[0].processDiffusion(input, 0.5f);
    input = mInputAP[1].processDiffusion(input, 0.5f);
    input = mInputAP[2].processDiffusion(input, 0.5f);
    input = mInputAP[3].processDiffusion(input, 0.5f);

    // Modulation
    mModPhase += 0.0001f + mModDepth * 0.001f;
    if (mModPhase > 1.0f)
      mModPhase -= 1.0f;
    float mod = 15.0f * (0.5f + 0.5f * sinf(mModPhase * 2.0f * 3.14159f));

    // Right branch feedback into Left loop
    float readR = mDelayAfterAPR.read(
        3000); // 4200 sample delay point? Dattorro uses taps.
    // Let's use simplified taps for standard tank
    float sideR = readR * mFeedback;

    // Safety Clamping
    if (!std::isfinite(sideR))
      sideR = 0.0f;
    sideR = std::max(-2.0f, std::min(2.0f, sideR));

    float leftIn = input + sideR;

    float apLOut = mLoopAPL.processDiffusion(
        leftIn, 0.5f); // 0.7 used in some, 0.5 in Dattorro
    mDelayL.write(apLOut);
    float delayLOut = mDelayL.readMod(4000 + mod); // Modulated 4453

    // Damping L
    mFilterL += mDamp * (delayLOut - mFilterL);
    if (!std::isfinite(mFilterL))
      mFilterL = 0.0f;
    float dampenedL = std::max(
        -2.0f,
        std::min(
            2.0f,
            mFilterL *
                mDecay)); // Decay separate from feedback? Dattorro combines

    // Tone L (New)
    mToneFilterL += mTone * (dampenedL - mToneFilterL) + 1.0e-18f;
    if (!std::isfinite(mToneFilterL) || std::abs(mToneFilterL) < 1.0e-18f)
      mToneFilterL = 0.0f;
    float tonedL = mToneFilterL;

    mDelayAfterAPL.write(tonedL);

    // Left branch feedback into Right loop
    float readL = mDelayAfterAPL.read(3000);
    float sideL = readL * mFeedback;

    // Safety Clamping
    if (!std::isfinite(sideL))
      sideL = 0.0f;
    sideL = std::max(-2.0f, std::min(2.0f, sideL));

    float rightIn = input + sideL;

    float apROut = mLoopAPR.processDiffusion(rightIn, 0.5f);
    mDelayR.write(apROut);
    float delayROut = mDelayR.readMod(4200 - mod); // Inverse mod

    // Damping R
    mFilterR += mDamp * (delayROut - mFilterR);
    if (!std::isfinite(mFilterR))
      mFilterR = 0.0f;
    float dampenedR = std::max(-2.0f, std::min(2.0f, mFilterR * mDecay));

    // Tone R (New)
    mToneFilterR += mTone * (dampenedR - mToneFilterR) + 1.0e-18f;
    if (!std::isfinite(mToneFilterR) || std::abs(mToneFilterR) < 1.0e-18f)
      mToneFilterR = 0.0f;
    float tonedR = mToneFilterR;

    mDelayAfterAPR.write(tonedR);

    // Dattorro extraction taps for "Lush" sound (Plate extraction style)
    // Left: Taps from Right Tank half-delays and Allpasses
    // Right: Taps from Left Tank half-delays and Allpasses

    float wetL = (mDelayAfterAPR.read(266) + mDelayAfterAPR.read(2974) -
                  mLoopAPR.read(1913) + mDelayAfterAPR.read(1996) -
                  mDelayAfterAPL.read(1990) - mLoopAPL.read(187) -
                  mDelayAfterAPL.read(1066)) *
                 mMix * 0.8f; // Reduced from 1.5f to fix loudness

    float wetR = (mDelayAfterAPL.read(353) + mDelayAfterAPL.read(3627) -
                  mLoopAPL.read(1228) + mDelayAfterAPL.read(2673) -
                  mDelayAfterAPR.read(2111) - mLoopAPR.read(335) -
                  mDelayAfterAPR.read(121)) *
                 mMix * 0.8f;

    // Global Panic Check: Reset if audio becomes non-finite
    if (!std::isfinite(wetL) || !std::isfinite(wetR)) {
      clear();
      wetL = wetR = 0.0f;
    }

    outL = wetL;
    outR = wetR;

    // Silence tracking
    if (std::abs(wetL) < 1e-9f && std::abs(wetR) < 1e-9f) {
      if (mSilentCounter < 48000)
        mSilentCounter++;
    } else {
      mSilentCounter = 0;
    }
  }

  bool isSilent() const { return mSilentCounter >= 48000; }

  // Parameter Setters
  void setSize(float v) {
    // Modify delays or decay time
    mDecay = 0.3f + v * 0.69f;
    // If Space (type 3), reduce max feedback to 70%
    if (mType == 3)
      mDecay = 0.3f + v * 0.4f; // Max 0.7
  }

  void setSampleRate(float sr) { mSampleRate = sr; }
  void setDamp(float v) { mDamp = 0.05f + (1.0f - v) * 0.8f; } // Lowpass coeff
  void setDamping(float v) { setDamp(v); }
  void setModDepth(float v) { mModDepth = v; }
  void setMix(float v) { mMix = v; }
  float getMix() const { return mMix; }
  void setPreDelay(float v) { mPreDelayMilli = v * 200.0f; }
  void setTone(float v) { mTone = 0.1f + v * 0.8f; } // LPF on output

  void setType(int t) {
    mType = t;
    if (mType == 3) {
      mFeedback = 0.85f; // Space (more lush)
    } else {
      mFeedback = 0.6f; // Others (was 0.3)
    }
    // Re-trigger size update to clamp decay
    setSize(mDecay);
  }

  // Standard interface
  void setParameters(float size, float damp, float width, float mix,
                     float preDelay, float sr) {
    setSize(size);
    setDamp(damp);
    setModDepth(width);
    setMix(mix);
    setPreDelay(preDelay);
  }

  void setToneParam(float v) { setTone(v); }

private:
  float mSampleRate = 48000.0f;
  float mFeedback = 0.6f; // Cross-feedback gain, bumped for lushness
  float mDecay = 0.5f;    // Recirculation gain
  float mDamp = 0.5f;
  float mModDepth = 0.1f;
  float mMix = 0.5f;
  float mPreDelayMilli = 0.0f;
  float mTone = 0.8f;
  int mType = 0;

  float mModPhase = 0.0f;

  // State
  float mFilterL = 0.0f, mFilterR = 0.0f;
  float mToneFilterL = 0.0f, mToneFilterR = 0.0f;
  uint32_t mSilentCounter = 48000; // Start silent

  Galactic::DelayLine mPreDelay;
  Galactic::AllPass mInputAP[4];

  Galactic::AllPass mLoopAPL, mLoopAPR;
  Galactic::DelayLine mDelayL, mDelayR;
  Galactic::DelayLine mDelayAfterAPL, mDelayAfterAPR;
};

#endif // GALACTIC_REVERB_H
