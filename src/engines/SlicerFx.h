#ifndef SLICER_FX_H
#define SLICER_FX_H

#include <cmath>

class SlicerFx {
public:
  // Input v is typically the rate multiplier (e.g. 1.0, 2.0, 4.0) passed from
  // AudioEngine
  void setRate1(float v) { mRate1 = v; }
  void setRate2(float v) { mRate2 = v; }
  void setRate3(float v) { mRate3 = v; }

  void setActive1(bool v) { mActive1 = v; }
  void setActive2(bool v) { mActive2 = v; }
  void setActive3(bool v) { mActive3 = v; }
  void setDepth(float v) { mDepth = v; }

  void setParameters(float rate1, float rate2, float rate3, bool active1,
                     bool active2, bool active3, float depth) {
    mRate1 = rate1;
    mRate2 = rate2;
    mRate3 = rate3;
    mActive1 = active1;
    mActive2 = active2;
    mActive3 = active3;
    mDepth = depth;
  }

  float process(float input, double sampleCount, double samplesPerStep) {
    if (samplesPerStep <= 0)
      return input;

    float activeGain = 1.0f;
    applySlice(activeGain, mRate1, mActive1, sampleCount, samplesPerStep);
    applySlice(activeGain, mRate2, mActive2, sampleCount, samplesPerStep);
    applySlice(activeGain, mRate3, mActive3, sampleCount, samplesPerStep);

    return input * activeGain;
  }

private:
  void applySlice(float &gain, float rate, bool active, double sampleCount,
                  double samplesPerStep) {
    if (active && rate > 0) {
      double cycle = samplesPerStep / (double)rate;
      double pos = std::fmod(sampleCount, cycle) / cycle;
      if (pos > 0.5)
        gain *= (1.0f - mDepth);
    }
  }

  float mRate1 = 1.0f;
  float mRate2 = 1.0f;
  float mRate3 = 1.0f;
  bool mActive1 = true;
  bool mActive2 = false;
  bool mActive3 = false;
  float mDepth = 1.0f;
};

#endif // SLICER_FX_H
