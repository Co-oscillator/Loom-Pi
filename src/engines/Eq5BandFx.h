#ifndef EQ_5BAND_FX_H
#define EQ_5BAND_FX_H

#include <algorithm>
#include <cmath>
#include <vector>

/**
 * 5-Band Equalizer Effect
 * Bands: Low Shelf (80Hz), Low-Mid (250Hz), Mid (1k), High-Mid (4k), High Shelf
 * (10k)
 */
class Eq5BandFx {
public:
  struct Band {
    float frequency;
    float Q;
    float gain; // in dB
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float m1, m2; // state

    void reset() {
      m1 = 0;
      m2 = 0;
      b0 = 1.0f;
      b1 = 0.0f;
      b2 = 0.0f;
      a1 = 0.0f;
      a2 = 0.0f;
    }
  };

  Eq5BandFx() {
    // Low Shelf
    mBands[0] = {80.0f, 0.707f, 0.0f, 0, 0};
    // Low Mid
    mBands[1] = {250.0f, 1.0f, 0.0f, 0, 0};
    // Mid
    mBands[2] = {1000.0f, 1.0f, 0.0f, 0, 0};
    // High Mid
    mBands[3] = {4000.0f, 1.0f, 0.0f, 0, 0};
    // High Shelf
    mBands[4] = {10000.0f, 0.707f, 0.0f, 0, 0};
  }

  void setBandGain(int bandIdx, float gainDb) {
    if (bandIdx >= 0 && bandIdx < 5) {
      mBands[bandIdx].gain = gainDb;
    }
  }

  void reset() {
    for (auto &band : mBands) {
      band.reset();
    }
  }

  void setMix(float mix) { mMix = mix; }

  float process(float input, float sampleRate) {
    if (!std::isfinite(input))
      return 0.0f;
    if (sampleRate <= 0)
      return input;

    float out = input;
    for (int i = 0; i < 5; ++i) {
      out = processBand(i, out, sampleRate);
    }

    return input * (1.0f - mMix) + out * mMix;
  }

  void clear() {
    for (auto &b : mBands)
      b.reset();
  }

private:
  float processBand(int idx, float input, float sampleRate) {
    auto &b = mBands[idx];

    // Limit frequency to avoid tangent explosion
    float freq = std::max(10.0f, std::min(b.frequency, sampleRate * 0.45f));
    float g = std::tan((float)M_PI * freq / sampleRate);
    float k = 1.0f / std::max(0.1f, b.Q);
    float A = std::pow(10.0f, b.gain / 40.0f); // Square root of linear gain

    // TPT SVF structure (Stable)
    float d = 1.0f / (1.0f + g * (g + k));
    float hp = (input - (k + g) * b.m1 - b.m2) * d;
    float bp = g * hp + b.m1;
    float lp = g * bp + b.m2;

    // Update state
    b.m1 = g * hp + bp;
    b.m2 = g * bp + lp;

    float wet = input;

    if (idx == 0) { // Low Shelf
      wet = input + (A * A - 1.0f) * lp + k * (A - 1.0f) * bp;
    } else if (idx == 4) { // High Shelf
      wet = input + (A * A - 1.0f) * hp + k * (A - 1.0f) * bp;
    } else { // Peaking
      // Standard TPT peaking gain formula
      wet = input + k * (A * A - 1.0f) * bp;
    }

    if (!std::isfinite(wet) || std::isnan(wet)) {
      b.reset();
      wet = input;
    }
    return wet;
  }

  Band mBands[5];
  float mMix = 1.0f;
};

#endif // EQ_5BAND_FX_H
