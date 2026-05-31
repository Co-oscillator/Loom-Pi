#ifndef GRANULAR_ENGINE_H
#define GRANULAR_ENGINE_H

#include "../Utils.h"
#include "Adsr.h"
#include <algorithm>

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <random>
#include <vector>

class GranularEngine {
public:
  // ADSR Parameters
  float mAttack = 0.01f;
  float mDecay = 0.1f;
  float mSustain = 1.0f;
  float mRelease = 0.1f;
  bool mFastInterpolation = false;

  void setAttack(float v) { mAttack = v; }
  void setDecay(float v) { mDecay = v; }
  void setSustain(float v) { mSustain = v; }
  void setRelease(float v) { mRelease = v; }
  void setGlide(float g) { mGlide = g; }
  void setPitchBend(float v) { mPitchBend = v; }
  void setSampleRate(float sr) { mSampleRate = sr; }

  struct Grain {
    float position;
    float spawnPos;  // Position in buffer at grain birth (for UI playhead display)
    float speed;
    float lOffset; // Stereo left
    float rOffset; // Stereo right
    float envValue;
    float attackStep;
    float decayStep;
    int life;
    int initialLife;
    bool isReverse;
    bool isActive;
    int voiceIdx; // Link to voice for ADSR and mixing

    // 4-point, 3rd-order Hermite Interpolation
    inline float cubicInterp(float y0, float y1, float y2, float y3, float mu) {
      float mu2 = mu * mu;
      float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
      float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
      float a2 = -0.5f * y0 + 0.5f * y2;
      float a3 = y1;
      return (a0 * mu * mu2) + (a1 * mu2) + (a2 * mu) + a3;
    }

    float nextSample(const float *source, size_t sourceSize, bool fastInterp) {
      if (!isActive || sourceSize == 0)
        return 0.0f;

      // -- GRAIN ENVELOPE (Windowing) --
      if (life > initialLife * 0.9f) { // Attack 10%
        envValue += attackStep;
      } else { // Decay 90%
        envValue -= decayStep;
      }
      envValue = std::max(0.0f, std::min(1.0f, envValue));

      // Parameter Setters

      int size = static_cast<int>(sourceSize);
      int idx = static_cast<int>(position);
      float frac = position - static_cast<float>(idx);

      int i1 = idx;
      int i2 = (idx + 1) % size;

      float s = 0.0f;
      if (fastInterp) {
        s = source[i1] + frac * (source[i2] - source[i1]);
      } else {
        int i0 = (idx - 1 + size) % size;
        int i3 = (idx + 2) % size;
        s = cubicInterp(source[i0], source[i1], source[i2], source[i3], frac);
      }

      if (isReverse) {
        position -= speed;
        if (position < 0)
          position += static_cast<float>(size);
      } else {
        position += speed;
        if (position >= static_cast<float>(size))
          position -= static_cast<float>(size);
      }

      life--;
      if (life <= 0)
        isActive = false;

      return s * envValue;
    }
  };

  class LFO {
  public:
    float phase = 0.0f;
    float rate = 0.1f;
    float depth = 0.0f;
    float shape = 0.0f;
    int target = 0;

    float nextValue() {
      phase += rate * 0.01f;
      if (phase >= 1.0f)
        phase -= 1.0f;

      float val = 0.0f;
      if (shape < 1.0f) {
        val = sinf(phase * 2.0f * M_PI);
      } else if (shape < 2.0f) {
        val = phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
      } else if (shape < 3.0f) {
        val = phase * 2.0f - 1.0f;
      } else {
        val = phase < 0.5f ? 1.0f : -1.0f;
      }
      return val * depth;
    }
  };

  struct Voice {
    bool active = false;
    int note = -1;
    float amplitude = 1.0f;
    float basePitch = 1.0f;
    float targetBasePitch = 1.0f;
    Adsr envelope;
    float spawnCounter = 0.0f;
  };

  GranularEngine() {
    mLFOS.resize(3);
    mGrains.resize(100);
    mVoices.resize(16);
    for (auto &g : mGrains)
      g.isActive = false;
    for (auto &v : mVoices)
      v.active = false;

    mDensity = 0.5;
    mGrainSize = 0.2;
    resetToDefaults();
  }

  // Disable copy (atomic is not copyable)
  GranularEngine(const GranularEngine &) = delete;
  GranularEngine &operator=(const GranularEngine &) = delete;

  // Move constructor (atomics need explicit handling)
  GranularEngine(GranularEngine &&other) noexcept
      : mActiveBuffer(other.mActiveBuffer.load(std::memory_order_relaxed)),
        mSourceBuffers{std::move(other.mSourceBuffers[0]),
                       std::move(other.mSourceBuffers[1])},
        mRecordingBuffer(std::move(other.mRecordingBuffer)),
        mBasePitch(other.mBasePitch), mGrains(std::move(other.mGrains)),
        mLFOS(std::move(other.mLFOS)), mVoices(std::move(other.mVoices)),
        mPosition(other.mPosition), mSpeed(other.mSpeed),
        mGrainSize(other.mGrainSize), mDensity(other.mDensity),
        mPitch(other.mPitch), mSpray(other.mSpray), mDetune(other.mDetune),
        mRandomTiming(other.mRandomTiming), mMaxGrains(other.mMaxGrains),
        mWidth(other.mWidth), mReverseProb(other.mReverseProb),
        mSpawnProb(other.mSpawnProb),
        mGlide(other.mGlide), mLastBasePitch(other.mLastBasePitch),
        mSampleRate(other.mSampleRate), mMainAttack(other.mMainAttack),
        mMainDecay(other.mMainDecay), mMainSustain(other.mMainSustain),
        mMainRelease(other.mMainRelease), mGain(other.mGain) {}

  GranularEngine &operator=(GranularEngine &&other) noexcept {
    if (this != &other) {
      mActiveBuffer.store(other.mActiveBuffer.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
      mSourceBuffers[0] = std::move(other.mSourceBuffers[0]);
      mSourceBuffers[1] = std::move(other.mSourceBuffers[1]);
      mRecordingBuffer = std::move(other.mRecordingBuffer);
      mBasePitch = other.mBasePitch;
      mGrains = std::move(other.mGrains);
      mLFOS = std::move(other.mLFOS);
      mVoices = std::move(other.mVoices);
      mPosition = other.mPosition;
      mSpeed = other.mSpeed;
      mGrainSize = other.mGrainSize;
      mDensity = other.mDensity;
      mPitch = other.mPitch;
      mSpray = other.mSpray;
      mDetune = other.mDetune;
      mRandomTiming = other.mRandomTiming;
      mMaxGrains = other.mMaxGrains;
      mWidth = other.mWidth;
      mReverseProb = other.mReverseProb;
      mSpawnProb = other.mSpawnProb;
      mGlide = other.mGlide;
      mLastBasePitch = other.mLastBasePitch;
      mSampleRate = other.mSampleRate;
      mMainAttack = other.mMainAttack;
      mMainDecay = other.mMainDecay;
      mMainSustain = other.mMainSustain;
      mMainRelease = other.mMainRelease;
      mGain = other.mGain;
    }
    return *this;
  }

  void resetToDefaults() {
    mPosition = 0.5f;
    mSpeed = 1.0f;
    mGrainSize = 0.2f;
    mDensity = 0.5f;
    mPitch = 1.0f;
    mSpray = 0.0f;
    mDetune = 0.0f;
    mRandomTiming = 0.0f;
    mMaxGrains = 40;
    mWidth = 0.5f;
    mReverseProb = 0.0f;
    mSpawnProb = 1.0f;
    mMainAttack = 0.01f;
    mMainDecay = 0.1f;
    mMainSustain = 1.0f;
    mMainRelease = 0.2f;
    mGain = 1.0f;
    for (int i = 0; i < 3; ++i) {
      mLFOS[i].phase = 0.0f;
      mLFOS[i].rate = 0.1f;
      mLFOS[i].depth = 0.0f;
      mLFOS[i].shape = 0.0f;
      mLFOS[i].target = 0;
    }
    for (auto &v : mVoices) {
      if (v.active)
        v.envelope.setParameters(mMainAttack, mMainDecay, mMainSustain,
                                 mMainRelease);
    }
  }

  void setSource(const std::vector<float> &source) {
    // Write to inactive buffer, then swap
    int inactive = 1 - mActiveBuffer.load(std::memory_order_acquire);
    mSourceBuffers[inactive] = source;
    mActiveBuffer.store(inactive, std::memory_order_release);
  }
  const std::vector<float> &getSampleData() const {
    return mSourceBuffers[mActiveBuffer.load(std::memory_order_acquire)];
  }
  size_t getSampleLength() const {
    return mSourceBuffers[mActiveBuffer.load(std::memory_order_acquire)].size();
  }
  void clearSource() {
    int inactive = 1 - mActiveBuffer.load(std::memory_order_acquire);
    mSourceBuffers[inactive].clear();
    mActiveBuffer.store(inactive, std::memory_order_release);
  }
  void pushSamples(const float *buffer, int count) {
    if (count <= 0)
      return;
    std::lock_guard<std::mutex> lock(mRecordingLock);
    mRecordingBuffer.insert(mRecordingBuffer.end(), buffer, buffer + count);
  }

  // Deprecated/Removed single sample push to prevent usage
  // void pushSample(float sample) { ... }
  void commitRecording() {
    // Called when recording stops - swap to active
    std::lock_guard<std::mutex> lock(mRecordingLock);
    int inactive = 1 - mActiveBuffer.load(std::memory_order_acquire);
    mSourceBuffers[inactive] = std::move(mRecordingBuffer);
    mRecordingBuffer.clear();
    mActiveBuffer.store(inactive, std::memory_order_release);
  }

  void normalize() {
    int active = mActiveBuffer.load(std::memory_order_acquire);
    if (mSourceBuffers[active].empty())
      return;
    // Work on inactive buffer
    int inactive = 1 - active;
    mSourceBuffers[inactive] = mSourceBuffers[active];
    float maxVal = 0.0f;
    for (float s : mSourceBuffers[inactive])
      maxVal = std::max(maxVal, std::abs(s));
    if (maxVal > 0.0001f) {
      for (float &s : mSourceBuffers[inactive])
        s /= maxVal;
    }
    mActiveBuffer.store(inactive, std::memory_order_release);
  }

  void trim(float start, float end) {
    int active = mActiveBuffer.load(std::memory_order_acquire);
    const auto &src = mSourceBuffers[active];
    if (src.empty())
      return;
    int s = std::max(0, std::min((int)(start * src.size()), (int)src.size()));
    int e = std::max(0, std::min((int)(end * src.size()), (int)src.size()));
    if (e > s) {
      int inactive = 1 - active;
      mSourceBuffers[inactive] =
          std::vector<float>(src.begin() + s, src.begin() + e);
      mActiveBuffer.store(inactive, std::memory_order_release);
    }
  }

  void triggerNote(int note, int velocity) {
    // Alloc Voice
    int idx = -1;
    for (int i = 0; i < 16; ++i) {
      if (!mVoices[i].active) {
        idx = i;
        break;
      }
    }
    if (idx == -1)
      idx = 0; // Steal

    Voice &v = mVoices[idx];
    v.active = true;
    v.note = note;
    v.amplitude = velocity / 127.0f;
    float targetPitch = powf(2.0f, (note - 60) / 12.0f);
    v.targetBasePitch = targetPitch;
    v.basePitch = (mGlide > 0.001f) ? mLastBasePitch : targetPitch;
    mLastBasePitch = targetPitch;
    v.spawnCounter = 0.0f;

    // Make sure ADSR parameters are applied to THIS voice's ADSR
    v.envelope.setSampleRate(mSampleRate);
    v.envelope.setParameters(mMainAttack, mMainDecay, mMainSustain,
                             mMainRelease);
    v.envelope.trigger();
  }

  void releaseNote(int note) {
    for (auto &v : mVoices) {
      if (v.active && v.note == note) {
        v.envelope.release();
      }
    }
  }

  void allNotesOff() {
    for (auto &g : mGrains) {
      g.isActive = false;
      g.life = 0;
    }
    for (auto &v : mVoices) {
      v.active = false;
      v.envelope.reset();
      v.spawnCounter = 0.0f;
    }
  }

  void setParameter(int id, float value) {
    if (id == 400)
      mPosition = value;
    else if (id == 401)
      mSpeed = value;
    else if (id >= 402 && id <= 405) {
      if (id == 402)
        mLFOS[0].shape = value;
      else if (id == 403)
        mLFOS[0].rate = value;
      else if (id == 404)
        mLFOS[0].depth = value;
      else if (id == 405)
        mLFOS[0].target = static_cast<int>(value * 3.0f);
    } else if (id == 406)
      mGrainSize = value;
    else if (id == 407)
      mDensity = value;
    else if (id == 408)
      mAttack = value;
    else if (id == 409)
      mDecay = value;
    else if (id == 410)
      mPitch = value;
    else if (id >= 411 && id <= 414) {
      if (id == 411)
        mLFOS[1].shape = value;
      else if (id == 412)
        mLFOS[1].rate = value;
      else if (id == 413)
        mLFOS[1].depth = value;
      else if (id == 414)
        mLFOS[1].target = static_cast<int>(value * 5.0f);
    } else if (id == 415)
      mSpray = value;
    else if (id == 416)
      mDetune = value;
    else if (id == 417)
      mRandomTiming = value;
    else if (id == 418)
      mMaxGrains = static_cast<int>(value * 95.0f + 5.0f);
    else if (id == 419)
      mWidth = value;
    else if (id == 420)
      mReverseProb = value;
    else if (id >= 421 && id <= 424) {
      if (id == 421)
        mLFOS[2].shape = value;
      else if (id == 422)
        mLFOS[2].rate = value;
      else if (id == 423)
        mLFOS[2].depth = value;
      else if (id == 424)
        mLFOS[2].target = static_cast<int>(value * 5.0f);
    }
    // New ADSR Params
    else if (id == 425)
      mMainAttack = value;
    else if (id == 426)
      mMainDecay = value;
    else if (id == 427)
      mMainSustain = value;
    else if (id == 428)
      mMainRelease = value;
    else if (id == 429)
      mGain = value * 2.5f; // 0 to 250% Gain
    else if (id == 355)
      setGlide(value);
    else if (id == 430)
      mSpawnProb = value;

    // Apply to live voices
    for (auto &v : mVoices) {
      if (v.active)
        v.envelope.setParameters(mMainAttack, mMainDecay, mMainSustain,
                                 mMainRelease);
    }
  }

  bool isActive() const {
    for (const auto &v : mVoices)
      if (v.active)
        return true;
    for (const auto &g : mGrains)
      if (g.isActive)
        return true;
    return false;
  }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (const auto &v : mVoices) {
      if (v.active) {
        maxEnv = std::max(maxEnv, v.envelope.getValue());
      }
    }
    return maxEnv;
  }

  void renderBlock(float* outL, float* outR, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
      render(&outL[i], &outR[i]);
    }
  }

  void render(float *left, float *right) {
    // Lock-free: read from active buffer
    int activeIdx = mActiveBuffer.load(std::memory_order_acquire);
    const auto &source = mSourceBuffers[activeIdx];
    if (source.empty()) {
      *left = *right = 0.0f;
      return;
    }

    bool hasActive = isActive();
    if (!hasActive) {
      *left = *right = 0.0f;
      return;
    }

    // Process LFOs
    float lfoOffsets[3] = {mLFOS[0].nextValue(), mLFOS[1].nextValue(),
                           mLFOS[2].nextValue()};

    for (int i = 0; i < 16; ++i) {
      Voice &v = mVoices[i];
      if (!v.active)
        continue;

      if (mGlide > 0.001f) {
        float glideTimeSamples = mGlide * mSampleRate * 0.5f;
        float glideAlpha = 1.0f / (glideTimeSamples + 1.0f);
        v.basePitch += (v.targetBasePitch - v.basePitch) * glideAlpha;
      } else {
        v.basePitch = v.targetBasePitch;
      }

      float envVal = v.envelope.nextValue();
      if (envVal < 0.0001f && !v.envelope.isActive()) {
        v.active = false;
      }

      // Spawn logic
      float grainDuration = (mGrainSize * 48000.0f * 2.0f) + 100.0f;
      float overlap = 0.1f + (mDensity * 4.0f);
      float interval = grainDuration / overlap;
      if (interval < 1.0f)
        interval = 1.0f;

      v.spawnCounter += 1.0f;
      float jitterOffset = (fastRand() - 0.5f) * mRandomTiming * interval * 0.5f;
      if (v.spawnCounter >= (interval + jitterOffset)) {
        v.spawnCounter = 0.0f;
        float randVal = fastRand();
        if (randVal < mSpawnProb) {
          spawnGrain(lfoOffsets, i);
        }
      }
    }

    // 2. Render and Mix Grains
    float lMixed = 0.0f, rMixed = 0.0f;
    int activeCount = 0;
    const float *srcData = source.data();
    size_t srcSize = source.size();
    for (auto &g : mGrains) {
      if (g.isActive) {
        float sample = g.nextSample(srcData, srcSize, mFastInterpolation);

        // Multiplier from parent voice (ADSR + Velocity)
        float masterGain = 0.0f;
        if (g.voiceIdx >= 0 && g.voiceIdx < 16) {
          masterGain = mVoices[g.voiceIdx].envelope.getValue() *
                       mVoices[g.voiceIdx].amplitude;
        }

        float out = sample * masterGain;
        lMixed += out * (1.0f - g.lOffset);
        rMixed += out * (1.0f - g.rOffset);
        activeCount++;
      }
    }

    float norm = activeCount > 0 ? 1.0f / sqrtf(activeCount) : 0.0f;
    // Boost output gain (2.5x)
    float finalGain = norm * 2.5f * mGain; // Apply user gain

    *left = lMixed * finalGain;
    *right = rMixed * finalGain;
  }

  struct PlayheadInfo {
    float pos;
    float vol;
    float start;
    float end;
  };
  void getPlayheads(PlayheadInfo *out, int maxCount) {
    int activeIdx = mActiveBuffer.load(std::memory_order_acquire);
    const auto &source = mSourceBuffers[activeIdx];

    // STABILIZATION FIX: Map first 'maxCount' active grains found, regardless
    // of index. Previously we only checked the first 'maxCount' indices,
    // missing grains at higher indices.
    int count = 0;

    // DEBUG LOGGING: Inspect why grains aren't showing
    // Only log occasionally to avoid spam
    static int logTicker = 0;
    bool shouldLog = (++logTicker % 60 == 0); // ~1Hz at 60fps

    for (const auto &g : mGrains) {
      if (g.isActive) {
        if (count < maxCount) {
          float p = source.empty() ? 0.0f : g.position / source.size();
          float sp = source.empty() ? 0.0f : g.spawnPos / (float)source.size();
          out[count].pos = p;
          out[count].vol = g.envValue;
          out[count].start = sp;
          out[count].end = 1.0f;

          if (shouldLog) {
            LOGD("Grain[%d]: Pos=%.3f, Vol=%.3f, SrcSize=%zu",
                 count, p, g.envValue, source.size());
          }

          count++;
        } else {
          break; // Filled output buffer
        }
      }
    }

    if (shouldLog && count == 0) {
      LOGD("No active grains found. Total Grains=%zu", mGrains.size());
    }

    // Fill remaining slots with empty
    for (int i = count; i < maxCount; ++i) {
      out[i].pos = -1.0f;
      out[i].vol = 0.0f;
      out[i].start = 0.0f;
      out[i].end = 1.0f;
    }
  }

  std::vector<float> getAmplitudeWaveform(int numPoints) const {
    // Check recording buffer first
    const std::vector<float> *sourcePtr = nullptr;
    std::unique_lock<std::mutex> recLock(
        const_cast<GranularEngine *>(this)->mRecordingLock);

    if (!mRecordingBuffer.empty()) {
      sourcePtr = &mRecordingBuffer;
    } else {
      recLock.unlock(); // Unlock if not using recording buffer
      int activeIdx = mActiveBuffer.load(std::memory_order_acquire);
      sourcePtr = &mSourceBuffers[activeIdx];
    }

    const auto &source = *sourcePtr;
    std::vector<float> result;
    if (source.empty())
      return result;

    int step = source.size() / numPoints;
    if (step < 1)
      step = 1;

    // OPTIMIZATION: Strided sampling to prevent audio thread lock-up
    // Scan at most 64 points per bin, or skip if bin is huge
    int skip = 1;
    if (step > 64)
      skip = step / 64;

    for (int i = 0; i < numPoints; ++i) {
      float maxVal = 0.0f;
      int start = i * step;
      int end = std::min((int)source.size(), (i + 1) * step);

      for (int j = start; j < end; j += skip) {
        float v = std::abs(source[j]);
        if (v > maxVal)
          maxVal = v;
      }
      result.push_back(maxVal);
    }
    return result;
  }

private:
  // Double-buffering for lock-free audio
  mutable std::atomic<int> mActiveBuffer{0};
  std::vector<float> mSourceBuffers[2];
  std::mutex mRecordingLock;
  std::vector<float> mRecordingBuffer;

  float mBasePitch = 1.0f;
  std::vector<Grain> mGrains;
  std::vector<LFO> mLFOS;
  std::vector<Voice> mVoices;

  float mPosition = 0.0f;
  float mSpeed = 1.0f;
  float mGrainSize = 0.1f;
  float mDensity = 0.2f;
  // mAttack and mDecay already defined in public section
  float mPitch = 1.0f;
  float mSpray = 0.0f;
  float mDetune = 0.0f;
  float mRandomTiming = 0.0f;
  int mMaxGrains = 20;
  float mWidth = 0.5f;
  float mReverseProb = 0.0f;
  float mSpawnProb = 1.0f;
  float mGlide = 0.0f;
  float mLastBasePitch = 1.0f;
  float mSampleRate = 48000.0f;

  // Main Envelope Params
  float mMainAttack = 0.01f;
  float mMainDecay = 0.1f;
  float mMainSustain = 1.0f;
  float mMainRelease = 0.1f;
  float mGain = 1.0f;
  float mPitchBend = 0.0f;
  
  uint32_t mRandState = 123456789;
  inline float fastRand() {
    mRandState ^= mRandState << 13;
    mRandState ^= mRandState >> 17;
    mRandState ^= mRandState << 5;
    return static_cast<float>(mRandState) * (1.0f / 4294967296.0f);
  }

  void spawnGrain(float *lfoOffsets, int voiceIdx) {
    int activeIdx = mActiveBuffer.load(std::memory_order_acquire);
    const auto &source = mSourceBuffers[activeIdx];
    if (source.empty())
      return;
    Voice &v = mVoices[voiceIdx];
    // Find inactive grain
    for (auto &g : mGrains) {
      if (!g.isActive) {
        float p = mPosition;
        if (mLFOS[0].target == 1)
          p += lfoOffsets[0];
        p += (fastRand() - 0.5f) * mSpray;
        p = std::max(0.0f, std::min(1.0f, p));

        float sp = mSpeed;
        if (mLFOS[0].target == 2)
          sp *= (1.0f + lfoOffsets[0]);

        float grainPitch = mPitch;
        if (mLFOS[1].target == 5)
          grainPitch *= (1.0f + lfoOffsets[1]);
        grainPitch += (fastRand() - 0.5f) * mDetune;

        float bendFactor = powf(2.0f, mPitchBend / 12.0f);
        g.position = p * source.size();
        g.spawnPos = g.position; // Record birth position for UI playhead display
        g.speed = sp * v.basePitch * grainPitch * bendFactor;
        g.isReverse = fastRand() < mReverseProb;

        float length = mGrainSize;
        if (mLFOS[1].target == 1)
          length *= (1.0f + lfoOffsets[1]);
        g.initialLife = static_cast<int>(length * mSampleRate * 2.0f + 100);
        g.life = g.initialLife;
        g.envValue = 0.0f;
        g.voiceIdx = voiceIdx; // Link to voice

        g.attackStep = 1.0f / (g.initialLife * 0.1f);
        g.decayStep = 1.0f / (g.initialLife * 0.9f);

        float pan = (fastRand() - 0.5f) * mWidth;
        g.lOffset = 0.5f + pan;
        g.rOffset = 0.5f - pan;

        g.isActive = true;
        break;
      }
    }
  }
};

#endif
