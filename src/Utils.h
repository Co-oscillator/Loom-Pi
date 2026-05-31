#ifndef UTILS_H
#define UTILS_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#define LOG_TAG "Groovebox"
#define LOGD(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

static inline float fast_tanh(float x) {
  if (x < -3.0f)
    return -1.0f;
  if (x > 3.0f)
    return 1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// 4-point, 3rd-order Hermite Interpolation (Cubic)
static inline float cubicInterpolation(float y0, float y1, float y2, float y3,
                                       float mu) {
  float mu2 = mu * mu;
  float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
  float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
  float a2 = -0.5f * y0 + 0.5f * y2;
  float a3 = y1;
  return (a0 * mu * mu2) + (a1 * mu2) + (a2 * mu) + a3;
}

// Fast Sine Approximation using Look-Up Table
struct FastSine {
  static const int TABLE_SIZE = 2048;
  static const int MASK = TABLE_SIZE - 1;
  std::vector<float> table;

  FastSine() {
    table.resize(TABLE_SIZE +
                 1); // +1 for guard point (no wrapping needed for linear interp
                     // of last segment if we handle it)
    for (int i = 0; i < TABLE_SIZE; ++i) {
      table[i] = sinf((float)i * 2.0f * (float)M_PI / (float)TABLE_SIZE);
    }
    table[TABLE_SIZE] = 0.0f; // Wrap around for 2PI
  }

  // Input: Phase in radians [0, 2PI]
  // Uses linear interpolation
  inline float sin(float radians) {
    // Normalize to 0..TABLE_SIZE
    // 2PI -> TABLE_SIZE
    // factor = TABLE_SIZE / 2PI
    static const float factor = (float)TABLE_SIZE / (2.0f * (float)M_PI);

    float indexFloat = radians * factor;
    int indexInt = (int)indexFloat;
    float frac = indexFloat - (float)indexInt;

    // Wrap index
    indexInt &= MASK;

    // Linear Interp
    // We use MASK for next index too to be safe, though +1 is usually enough if
    // table has it
    int nextIndex = (indexInt + 1) & MASK;

    return table[indexInt] + frac * (table[nextIndex] - table[indexInt]);
  }

  // Static instance access if preferred, but usually members have their own
  static FastSine &getInstance() {
    static FastSine instance;
    return instance;
  }

  // Static helper for 0..1 phase input
  static inline float get(float phase) {
    // Wrap phase to [0..1]
    if (phase < 0.0f)
      phase -= floorf(phase);
    else if (phase >= 1.0f)
      phase -= floorf(phase);

    return getInstance().sin(phase * 2.0f * (float)M_PI);
  }

  // Fast integer phase LUT (0 to 0xFFFFFFFF maps to 0 to 2PI)
  static inline float getInt(uint32_t phase) {
    // Top 11 bits for index (2048 size)
    uint32_t indexInt = phase >> 21;
    // Next 21 bits for interpolation (0 to 0x1FFFFF)
    float frac = (phase & 0x1FFFFF) * 4.768371582e-7f; // 1.0 / 2^21
    int nextIndex = (indexInt + 1) & MASK;
    const std::vector<float>& t = getInstance().table;
    return t[indexInt] + frac * (t[nextIndex] - t[indexInt]);
  }
};

static inline float fixDenormal(float value) {
  if (std::abs(value) < 1e-15f) return 0.0f;
  return value;
}

// T-SVF (Zero-Delay Feedback State Variable Filter)
// Based on Andrew Simper's Trapezoidal integration method.
// Extremely stable even at high frequencies and high resonance.
class TSvf {
public:
  enum Type { LowPass, HighPass, BandPass, Notch, Peak };

  void setParams(float cutoff, float resonance, float sampleRate) {
    float f = tanf(M_PI * cutoff / sampleRate);
    float k = 1.0f / std::max(0.1f, resonance);
    mA1 = 1.0f / (1.0f + f * (f + k));
    mA2 = f * mA1;
    mA3 = f * mA2;
    mF = f;
    mK = k;
  }

  float process(float input, Type type) {
    float v3 = input - mSvfZ2;
    float v1 = mA1 * mSvfZ1 + mA2 * v3;
    float v2 = mSvfZ2 + mA2 * mSvfZ1 + mA3 * v3;

    mSvfZ1 = fixDenormal(2.0f * v1 - mSvfZ1);
    mSvfZ2 = fixDenormal(2.0f * v2 - mSvfZ2);

    switch (type) {
    case LowPass:
      return v2;
    case HighPass:
      return input - mK * v1 - v2;
    case BandPass:
      return v1;
    case Notch:
      return input - mK * v1;
    case Peak:
      return input - mK * v1 - 2.0f * v2;
    default:
      return v2;
    }
  }

private:
  float mSvfZ1 = 0.0f;
  float mSvfZ2 = 0.0f;
  float mA1 = 0.0f, mA2 = 0.0f, mA3 = 0.0f;
  float mF = 0.0f, mK = 0.0f;
};

#endif // UTILS_H
