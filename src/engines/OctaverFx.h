#ifndef OCTAVER_FX_H
#define OCTAVER_FX_H

#include <algorithm>
#include <cmath>
#include <vector>

// Pitch-shifting Octaver & Harmony effect
// Uses delay-line granular pitch shifting with overlapping triangular windows.
// Supports octave shifts and chord harmonies with downward inversions.
class OctaverFx {
public:
  void updateSampleRate(float sr) {}
  OctaverFx() {
    mBuffer.resize(8192, 0.0f); // ~180ms circular buffer
  }

  float process(float input, float sampleRate) {
    if (!std::isfinite(input))
      input = 0.0f;
    if (mMix <= 0.001f)
      return input;

    // Write to circular buffer
    mBuffer[mWritePos] = input;

    // Render voices based on Mode
    float wet = 0.0f;

    // 10 modes (0-9):
    // 0: Oct Up       (+1 oct)
    // 1: 2 Oct Up     (+2 oct)
    // 2: Oct Down     (-1 oct)
    // 3: 2 Oct Down   (-2 oct)
    // 4: Up/Down      (+1 oct and -1 oct)
    // 5: Major        (5th down inversion + Major 3rd up)
    // 6: Dom7         (5th down + Major 3rd up + minor 7th up)
    // 7: Maj7         (5th down + Major 3rd up + Major 7th up)
    // 8: Min7         (5th down + minor 3rd up + minor 7th up)
    // 9: Dim          (tritone down + minor 3rd up)

    int mode = (int)(mMode * 9.99f);
    if (mode < 0)
      mode = 0;
    if (mode > 9)
      mode = 9;

    auto processVoice = [&](float ratio, float &phase,
                            float &windowPhase) -> float {
      float drift = 1.0f - ratio;
      phase += drift;

      const float windowSize = 2048.0f; // ~46ms

      auto getGrain = [&](float p) -> float {
        while (p < 0.0f)
          p += windowSize;
        while (p >= windowSize)
          p -= windowSize;

        float delay = p;
        float rPos = (float)mWritePos - delay;
        while (rPos < 0.0f)
          rPos += mBuffer.size();
        while (rPos >= mBuffer.size())
          rPos -= mBuffer.size();

        int i0 = (int)rPos;
        int i1 = (i0 + 1) % mBuffer.size();
        float f = rPos - i0;
        float samp = mBuffer[i0] * (1.0f - f) + mBuffer[i1] * f;

        // Triangular Window
        float win = 1.0f - std::abs(2.0f * (p / windowSize) - 1.0f);
        return samp * win;
      };

      float v1 = getGrain(phase);
      float v2 = getGrain(phase + windowSize * 0.5f); // Offset 50%

      return v1 + v2;
    };

    // Semitone ratio helper: 2^(semitones/12)
    // Pre-computed constants for chord intervals:
    static const float kP5Down = 0.74915f; // 2^(-5/12)  Perfect 5th down
    static const float kTTDown = 0.70711f; // 2^(-6/12)  Tritone down
    static const float kMin3Up = 1.18921f; // 2^(3/12)   Minor 3rd up
    static const float kMaj3Up = 1.25992f; // 2^(4/12)   Major 3rd up
    static const float kMin7Up = 1.78180f; // 2^(10/12)  Minor 7th up
    static const float kMaj7Up = 1.88775f; // 2^(11/12)  Major 7th up

    switch (mode) {
    case 0: // Oct Up
      wet += processVoice(2.0f, mPhase1, mWin1);
      if (mUnison > 0.3f)
        wet += processVoice(2.01f, mPhase2, mWin2);
      break;

    case 1: // 2 Oct Up
      wet += processVoice(4.0f, mPhase1, mWin1);
      if (mUnison > 0.3f)
        wet += processVoice(4.02f, mPhase2, mWin2);
      break;

    case 2: // Oct Down
      wet += processVoice(0.5f, mPhase1, mWin1);
      if (mUnison > 0.3f)
        wet += processVoice(0.505f, mPhase2, mWin2);
      break;

    case 3: // 2 Oct Down
      wet += processVoice(0.25f, mPhase1, mWin1);
      if (mUnison > 0.3f)
        wet += processVoice(0.253f, mPhase2, mWin2);
      break;

    case 4: // Up/Down
      wet += processVoice(2.0f, mPhase1, mWin1);
      wet += processVoice(0.5f, mPhase2, mWin2);
      break;

    case 5: // Major chord (5th down inversion + Major 3rd up)
      wet += processVoice(kP5Down, mPhase1, mWin1);
      wet += processVoice(kMaj3Up, mPhase2, mWin2);
      break;

    case 6: // Dom7 (5th down + Major 3rd up + minor 7th up)
      wet += processVoice(kP5Down, mPhase1, mWin1);
      wet += processVoice(kMaj3Up, mPhase2, mWin2);
      wet += processVoice(kMin7Up, mPhase3, mWin3);
      break;

    case 7: // Maj7 (5th down + Major 3rd up + Major 7th up)
      wet += processVoice(kP5Down, mPhase1, mWin1);
      wet += processVoice(kMaj3Up, mPhase2, mWin2);
      wet += processVoice(kMaj7Up, mPhase3, mWin3);
      break;

    case 8: // Min7 (5th down + minor 3rd up + minor 7th up)
      wet += processVoice(kP5Down, mPhase1, mWin1);
      wet += processVoice(kMin3Up, mPhase2, mWin2);
      wet += processVoice(kMin7Up, mPhase3, mWin3);
      break;

    case 9: // Diminished (tritone down + minor 3rd up)
      wet += processVoice(kTTDown, mPhase1, mWin1);
      wet += processVoice(kMin3Up, mPhase2, mWin2);
      break;
    }

    // Advance Write
    mWritePos++;
    if (mWritePos >= mBuffer.size())
      mWritePos = 0;

    float out = std::tanh(wet);
    if (!std::isfinite(out))
      out = 0.0f;
    return input * (1.0f - mMix) + out * mMix;
  }

  void setParameters(float mix, float detune, float unison, float mode) {
    mMix = mix;
    mDetune = detune;
    mUnison = unison;
    mMode = mode;
  }

  void setMix(float v) { mMix = v; }
  void setDetune(float v) { mDetune = v; }
  void setUnison(float v) { mUnison = v; }
  void setMode(float v) { mMode = v; }

private:
  std::vector<float> mBuffer;
  int mWritePos = 0;

  // Voices state (6 phase/window pairs for up to 3 harmony voices + unison)
  float mPhase1 = 0.0f, mPhase2 = 0.0f, mPhase3 = 0.0f;
  float mWin1 = 0.0f, mWin2 = 0.0f, mWin3 = 0.0f;

  float mMix = 0.0f;
  float mDetune = 0.0f;
  float mUnison = 0.0f;
  float mMode = 0.0f;
};

#endif
