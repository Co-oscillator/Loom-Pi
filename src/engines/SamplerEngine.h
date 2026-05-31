
#ifndef SAMPLER_ENGINE_H
#define SAMPLER_ENGINE_H

#include "../Utils.h"
#include "Adsr.h"
#include <algorithm>

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

#include <vector>

class SamplerEngine {
public:
  enum PlayMode {
    OneShot,
    Sustain,
    Loop,
    Chops,
    OneShotChops,
    LoopChops,
    Scrub
  };

  struct Slice {
    size_t start;
    size_t end;
  };

  struct SliceParams {
    float pitch = 0.0f;
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.8f;
    float release = 0.2f;
    float cutoff = 1.0f;
    float resonance = 0.0f;
    float reverse = 0.0f;       // 0 or 1
    float fxSends[18] = {0.0f}; // Per-slice FX sends
    bool active = false;        // Whether this slice has overridden params
  };

  struct Voice {
    bool active = false;
    int note = -1;
    int sliceIdx = -1;
    double position = 0.0;      // Traversal position
    double grainPosition = 0.0; // Phase within grain (or offset)
    size_t start = 0;
    size_t end = 0;
    float baseVelocity = 1.0f;
    float pitchRatio = 1.0f;
    float targetPitchRatio = 1.0f;
    double noteOnTime = 0.0;

    // Captured Params for Slice Locking
    float slicePitch = 0.0f;
    float sliceReverse = 0.0f;
    float sliceCutoff = 1.0f;
    float sliceResonance = 0.0f;
    float fxSends[18] = {0.0f};

    Adsr envelope;
    TSvf filter;

    // Simple Granular state
    uint32_t grainTimer = 0;
    static const int GRAIN_SIZE = 1024; // Samples

    uint32_t controlCounter = 0;

    void reset() {
      active = false;
      note = -1;
      sliceIdx = -1;
      position = 0.0;
      grainPosition = 0.0;
      grainTimer = 0;
      envelope.reset();
      filter.setParams(10000.0f, 0.7f, 48000.0f);
      pitchRatio = 1.0f;
      targetPitchRatio = 1.0f;
      slicePitch = 0.0f;
      sliceReverse = 0.0f;
      sliceCutoff = 1.0f;
      sliceResonance = 0.0f;
      std::fill(std::begin(fxSends), std::end(fxSends), 0.0f);
    }
  };

  SamplerEngine() {
    mVoices.resize(16);
    for (auto &v : mVoices)
      v.reset();
  }

  // Disable copy (atomic is not copyable)
  SamplerEngine(const SamplerEngine &) = delete;
  SamplerEngine &operator=(const SamplerEngine &) = delete;

  // Move constructor (atomics need explicit handling)
  SamplerEngine(SamplerEngine &&other) noexcept
      : mActiveBuffer(other.mActiveBuffer.load(std::memory_order_relaxed)),
        mBuffers{std::move(other.mBuffers[0]), std::move(other.mBuffers[1])},
        mRecordingBuffer(std::move(other.mRecordingBuffer)),
        mVoices(std::move(other.mVoices)), mTrimStart(other.mTrimStart),
        mTrimEnd(other.mTrimEnd), mPitch(other.mPitch),
        mStretch(other.mStretch), mSpeed(other.mSpeed), mAttack(other.mAttack),
        mDecay(other.mDecay), mSustain(other.mSustain),
        mRelease(other.mRelease), mFilterCutoff(other.mFilterCutoff),
        mFilterResonance(other.mFilterResonance),
        mFilterEnvAmount(other.mFilterEnvAmount), mFilterType(other.mFilterType), mGlide(other.mGlide),
        mLastPitchRatio(other.mLastPitchRatio), mPlayMode(other.mPlayMode),
        mScrubGate(other.mScrubGate), mUseEnvelope(other.mUseEnvelope),
        mSampleRate(other.mSampleRate), mSlices(std::move(other.mSlices)),
        mLastTargetPos(other.mLastTargetPos) {
    mReverse.store(other.mReverse.load());
    mCurrentTime = other.mCurrentTime;
  }

  SamplerEngine &operator=(SamplerEngine &&other) noexcept {
    if (this != &other) {
      mActiveBuffer.store(other.mActiveBuffer.load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
      mBuffers[0] = std::move(other.mBuffers[0]);
      mBuffers[1] = std::move(other.mBuffers[1]);
      mRecordingBuffer = std::move(other.mRecordingBuffer);
      mReverse.store(other.mReverse.load());
      mVoices = std::move(other.mVoices);
      mTrimStart = other.mTrimStart;
      mTrimEnd = other.mTrimEnd;
      mPitch = other.mPitch;
      mStretch = other.mStretch;
      mSpeed = other.mSpeed;
      mAttack = other.mAttack;
      mDecay = other.mDecay;
      mSustain = other.mSustain;
      mRelease = other.mRelease;
      mFilterCutoff = other.mFilterCutoff;
      mFilterResonance = other.mFilterResonance;
      mFilterEnvAmount = other.mFilterEnvAmount;
      mFilterType = other.mFilterType;
      mGlide = other.mGlide;
      mLastPitchRatio = other.mLastPitchRatio;
      mPlayMode = other.mPlayMode;
      mScrubGate = other.mScrubGate;
      mUseEnvelope = other.mUseEnvelope;
      mSampleRate = other.mSampleRate;
      mCurrentTime = other.mCurrentTime;
      mSlices = std::move(other.mSlices);
      mLastTargetPos = other.mLastTargetPos;
    }
    return *this;
  }

  void resetToDefaults() {
    mPitch = 0.0f;
    mSliceIndex = -1; // -1 means use note-based indexing
    mSliceLockEnabled = false;
    for (int i = 0; i < 16; i++) {
      mSliceParams[i] = SliceParams();
    }
    mStretch = 1.0f;
    mSpeed = 1.0f;
    mAttack = 0.002f; // Fast attack but no click
    mDecay = 0.5f;    // Longer decay
    mSustain = 1.0f;  // Full sustain
    mRelease = 0.5f;  // Smooth release
    mFilterCutoff = 0.5f; // Default to midpoint (~2.5kHz with cubic mapping), not 1.0 (20kHz = fully open)
    mFilterResonance = 0.0f;
    mFilterEnvAmount = 0.0f;
    mFilterType = TSvf::LowPass;
    mPlayMode = OneShot;
    mUseEnvelope = true;
    mReverse = false;
    for (auto &v : mVoices) {
      if (v.active)
        v.envelope.setParameters(mAttack, mDecay, mSustain, mRelease);
    }
  }

  // 4-point, 3rd-order Hermite (Cubic) Interpolation for "Analogue" quality
  inline float getCubicInterpolatedSample(const std::vector<float> &buf,
                                          double pos) {
    int size = (int)buf.size();
    if (size == 0)
      return 0.0f;

    int idx1 = static_cast<int>(pos);
    float frac = static_cast<float>(pos - idx1);

    if (idx1 < 0 || idx1 >= size)
      return 0.0f;

    // We need 4 points: idx0, idx1, idx2, idx3
    int idx0 = (idx1 > 0) ? idx1 - 1 : idx1;
    int idx2 = (idx1 + 1 < size) ? idx1 + 1 : idx1;
    int idx3 = (idx1 + 2 < size) ? idx1 + 2 : idx2;

    return cubicInterpolation(buf[idx0], buf[idx1], buf[idx2], buf[idx3], frac);
  }

  // Linear Interpolation Helper (Fallback/Granular)
  inline float getInterpolatedSample(const std::vector<float> &buf,
                                     double pos) {
    int idx = static_cast<int>(pos);
    float frac = static_cast<float>(pos - idx);
    if (idx < 0 || idx >= (int)buf.size())
      return 0.0f;

    float s0 = buf[idx];
    float s1 = (idx + 1 < (int)buf.size()) ? buf[idx + 1] : s0; // Clamp to end

    return s0 + frac * (s1 - s0);
  }

  void setSample(const std::vector<float> &data) {
    // Write to inactive buffer, then swap
    int inactive = 1 - mActiveBuffer.load(std::memory_order_acquire);
    mBuffers[inactive] = data;
    mActiveBuffer.store(inactive, std::memory_order_release);
  }
  void loadSample(const std::vector<float> &data) {
    allNotesOff();
    setSample(data);
  }
  const std::vector<float> &getSampleData() const {
    return mBuffers[mActiveBuffer.load(std::memory_order_acquire)];
  }

  // Helper for internal access to active buffer
  const std::vector<float> &getBuffer() const {
    return mBuffers[mActiveBuffer.load(std::memory_order_acquire)];
  }

  size_t getSampleLength() const {
    return mBuffers[mActiveBuffer.load(std::memory_order_acquire)].size();
  }
  const std::vector<Slice>& getSlices() const { return mSlices; }

  void setSlicePoints(const std::vector<float> &points) {
    std::lock_guard<std::mutex> lock(mSliceLock);
    mSlices.clear();
    int active = mActiveBuffer.load(std::memory_order_acquire);
    const auto &buf = mBuffers[active];
    if (buf.empty())
      return;

    for (size_t i = 0; i < points.size(); ++i) {
      size_t start = static_cast<size_t>(points[i] * buf.size());
      size_t end = (i + 1 < points.size())
                       ? static_cast<size_t>(points[i + 1] * buf.size())
                       : buf.size();
      if (start < end)
        mSlices.push_back({start, end});
    }
  }

  void setSlicePoints(const std::vector<int> &starts,
                      const std::vector<int> &ends) {
    std::lock_guard<std::mutex> lock(mSliceLock);
    mSlices.clear();
    for (size_t i = 0; i < starts.size() && i < ends.size(); ++i) {
      mSlices.push_back(
          {static_cast<size_t>(starts[i]), static_cast<size_t>(ends[i])});
    }
  }

  void setPlaybackSpeed(float speed);

  void setSlicePosition(int index, float position) {
    std::lock_guard<std::mutex> lock(mSliceLock);
    int active = mActiveBuffer.load(std::memory_order_acquire);
    const auto &buf = mBuffers[active];
    if (index >= 0 && index < (int)mSlices.size() && !buf.empty()) {
      size_t newStart = static_cast<size_t>(position * buf.size());
      if (newStart >= buf.size())
        newStart = buf.size() - 1;

      mSlices[index].start = newStart;
      if (index > 0) {
        mSlices[index - 1].end = newStart;
      }
    }
  }

  void clearBuffer() {
    int inactive = 1 - mActiveBuffer.load(std::memory_order_acquire);
    mBuffers[inactive].clear();
    mActiveBuffer.store(inactive, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mSliceLock);
    mSlices.clear();
    for (auto &v : mVoices)
      v.active = false;
  }

  void pushSample(float sample) {
    // During recording, accumulate in recording buffer
    std::lock_guard<std::mutex> lock(mRecordingLock);
    mRecordingBuffer.push_back(sample);
  }

  void commitRecording() {
    // Called when recording stops - swap to active
    std::lock_guard<std::mutex> lock(mRecordingLock);
    int inactive = 1 - mActiveBuffer.load(std::memory_order_acquire);
    mBuffers[inactive] = std::move(mRecordingBuffer);
    mRecordingBuffer.clear();
    mActiveBuffer.store(inactive, std::memory_order_release);
  }

  void normalize() {
    int active = mActiveBuffer.load(std::memory_order_acquire);
    if (mBuffers[active].empty())
      return;
    int inactive = 1 - active;
    mBuffers[inactive] = mBuffers[active];
    float maxVal = 0.0f;
    for (float s : mBuffers[inactive])
      maxVal = std::max(maxVal, std::abs(s));
    if (maxVal > 0.0001f) {
      float gain = 0.95f / maxVal;
      for (auto &s : mBuffers[inactive])
        s *= gain;
    }
    mActiveBuffer.store(inactive, std::memory_order_release);
  }

  void trim() {
    int active = mActiveBuffer.load(std::memory_order_acquire);
    const auto &buf = mBuffers[active];
    if (buf.empty())
      return;
    size_t start = static_cast<size_t>(mTrimStart * buf.size());
    size_t end = static_cast<size_t>(mTrimEnd * buf.size());
    if (end > buf.size())
      end = buf.size();
    if (start >= end) {
      if (end > 0)
        start = end - 1;
      else
        return;
    }
    int inactive = 1 - active;
    mBuffers[inactive] =
        std::vector<float>(buf.begin() + start, buf.begin() + end);
    mActiveBuffer.store(inactive, std::memory_order_release);
    mTrimStart = 0.0f;
    mTrimEnd = 1.0f;
    std::lock_guard<std::mutex> lock(mSliceLock);
    mSlices.clear();
    for (auto &v : mVoices)
      v.active = false;
  }

  void allNotesOff() {
    for (auto &v : mVoices) {
      v.active = false;
      v.envelope.reset();
      v.active = false; // "isPlaying" equivalent
    }
  }

  void setSampleRate(float sr) {
    mSampleRate = (int)sr;
    for (auto &v : mVoices) {
      v.envelope.setSampleRate(sr);
      v.filter.setParams(1000.0f, 0.7f, sr);
    }
  }

  void setGlide(float g) { mGlide = g; }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (const auto &v : mVoices) {
      if (v.active) {
        maxEnv = std::max(maxEnv, v.envelope.getValue());
      }
    }
    return maxEnv;
  }

  void triggerNote(int note, int velocity) { noteOn(note, velocity); }

  void noteOn(int note, int velocity) {
    if (mVoices.empty())
      return;

    // SCRUB MODE: Test Trigger starts the "motor" on Voice 0 (the scrub
    // head). This gives DJ-style behavior: trigger to play, grab playhead to
    // scrub, release to resume normal-speed playback.
    if (mPlayMode == Scrub) {
      Voice &v = mVoices[0];
      if (!v.active) {
        v.active = true;
        v.position = 0.0;
        v.envelope.forceSustain();
      }
      mMotorRunning = true;
      mSmoothSpeed = 1.0; // Normal forward playback speed
      return;             // Don't allocate a second voice
    }

    // In non-Scrub modes, Voice 0 is available for allocation.
    int active = mActiveBuffer.load(std::memory_order_acquire);
    const auto &buf = mBuffers[active];
    if (buf.empty())
      return;

    size_t startIndex = 0;

    // 1. Try to find an inactive voice
    int voiceIdx = -1;
    for (size_t i = startIndex; i < mVoices.size(); i++) {
      if (!mVoices[i].active) {
        voiceIdx = static_cast<int>(i);
        break;
      }
    }

    // 2. If no inactive voice, try to find an active voice playing the same
    // note
    if (voiceIdx == -1) {
      for (size_t i = startIndex; i < mVoices.size(); i++) {
        if (mVoices[i].active && mVoices[i].note == note) {
          voiceIdx = static_cast<int>(i);
          break;
        }
      }
    }

    // 3. If still no voice, steal the oldest voice (lowest noteOnTime),
    // skipping reserved voice
    if (voiceIdx == -1) {
      int oldest = -1;
      double minTime = 1e15; // Large number

      for (size_t i = startIndex; i < mVoices.size(); i++) {
        // Don't steal the voice if it's the one we just triggered (unlikely
        // but safe)
        if (mVoices[i].noteOnTime < minTime) {
          minTime = mVoices[i].noteOnTime;
          oldest = static_cast<int>(i);
        }
      }
      voiceIdx = oldest;
    }

    // Fallback: If for some reason we can't find one, return startIndex?
    // But forcing 0 in Scrub mode is bad.
    if (voiceIdx == -1) {
      voiceIdx = (mPlayMode == Scrub) ? 1 : 0;
    }

    Voice &v = mVoices[voiceIdx];
    v.reset();
    v.active = true;
    v.note = note;
    v.baseVelocity = velocity / 127.0f;
    v.noteOnTime = mCurrentTime;
    v.envelope.setSampleRate(48000.0f);
    v.envelope.setParameters(mAttack, mDecay, mSustain, mRelease);
    v.envelope.trigger();

    if ((mPlayMode == Chops || mPlayMode == OneShotChops ||
         mPlayMode == LoopChops) &&
        !mSlices.empty()) {
      // In CHOP modes, slice index is determined by the MIDI note (Note 60 =
      // Slice 0) mSliceIndex is now used ONLY for UI parameter focus in
      // "Slice Lock" mode.
      int sliceIdx = 0;
      if (note >= 60) {
        sliceIdx = (note - 60);
      }

      // Explicitly cycle through slices
      if (!mSlices.empty()) {
        sliceIdx = sliceIdx % (int)mSlices.size();
      } else {
        sliceIdx = 0;
      }

      v.start = mSlices[sliceIdx].start;
      v.end = mSlices[sliceIdx].end;
      v.sliceIdx = sliceIdx;
    } else {
      v.start = static_cast<size_t>(mTrimStart * buf.size());
      v.end = static_cast<size_t>(mTrimEnd * buf.size());
      if (v.end > buf.size())
        v.end = buf.size();
      if (v.start >= v.end && v.end > 0)
        v.start = v.end - 1;
      v.sliceIdx = -1;
    }

    // Capture Per-Slice Parameters if LOCK is enabled
    if (mSliceLockEnabled && v.sliceIdx >= 0 && v.sliceIdx < 16) {
      const auto &p = mSliceParams[v.sliceIdx];
      v.envelope.setParameters(p.attack, p.decay, p.sustain, p.release);
      float initCutoff = 20.0f + (p.cutoff * p.cutoff * p.cutoff * 19980.0f);
      v.filter.setParams(initCutoff, 0.7f + p.resonance * 5.0f, 48000.0f);
      v.controlCounter = 15; // Force filter param update on the first rendered sample
      v.slicePitch = p.pitch;
      v.sliceReverse = p.reverse;
      v.sliceCutoff = p.cutoff;
      v.sliceResonance = p.resonance;
      std::copy(std::begin(p.fxSends), std::end(p.fxSends),
                std::begin(v.fxSends));
    } else {
      v.envelope.setParameters(mAttack, mDecay, mSustain, mRelease);
      float initCutoff = 20.0f + (mFilterCutoff * mFilterCutoff * mFilterCutoff * 19980.0f);
      // Use the same resonance mapping as the render loop: k = 1/(0.7 + res*5.0)
      // Passing raw mFilterResonance (0.0) would cause k=1/0 (division by zero)
      v.filter.setParams(initCutoff, 0.7f + mFilterResonance * 5.0f, 48000.0f);
      v.controlCounter = 15; // Force filter param update on the first rendered sample
      v.slicePitch = 0.0f;
      v.sliceReverse = 0.0f;
      v.sliceCutoff = 0.0f;    // Default to 0 if not slice-locked
      v.sliceResonance = 0.0f; // Default to 0 if not slice-locked
    }
    v.envelope.trigger();

    float keyShift = (mPlayMode == Chops || mPlayMode == OneShotChops ||
                      mPlayMode == LoopChops)
                         ? 0.0f
                         : (float)(note - 60);

    float pShift = mSliceLockEnabled ? v.slicePitch : mPitch;
    float targetRatio = powf(2.0f, (pShift + keyShift) / 12.0f) * mSpeed;
    v.targetPitchRatio = targetRatio;
    v.pitchRatio = (mGlide > 0.001f) ? mLastPitchRatio : targetRatio;
    mLastPitchRatio = targetRatio;

    // Apply Slice-Specific Reverse
    if (mSliceLockEnabled && v.sliceReverse > 0.5f) {
      v.position = (double)v.end - 1.0;
    } else if (mReverse) {
      v.position = (double)v.end - 1.0;
    } else {
      v.position = (double)v.start;
    }
    v.grainPosition = v.position;
    v.grainTimer = 0;
  }

  int getPlayMode() const { return mPlayMode; } // Added Getter for AudioEngine
  bool isSliceLockEnabled() const { return mSliceLockEnabled; }

  void releaseNote(int note) {
    for (auto &v : mVoices) {
      if (v.active && v.note == note) {
        if (mPlayMode == Sustain || mPlayMode == Loop ||
            mPlayMode == LoopChops) {
          v.envelope.release();
        }
      }
    }
  }

  void setParameter(int id, float value) {
    switch (id) {
    case 1: // Cutoff
      setFilterCutoff(value);
      break;
    case 2: // Resonance
      setFilterResonance(value);
      break;
    case 300: // PITCH: changes the pitch but keeps playback time constant
      mPitch = (value - 0.5f) * 48.0f;
      for (auto &v : mVoices) {
        if (v.active) {
          float keyShift = (mPlayMode == Chops || mPlayMode == OneShotChops ||
                            mPlayMode == LoopChops)
                               ? 0.0f
                               : (float)(v.note - 60);
          v.targetPitchRatio = powf(2.0f, (mPitch + keyShift) / 12.0f) * mSpeed;
        }
      }
      break;
    case 301: // STRETCH: changes playback time but keeps pitch constant
      mStretch = value * 4.0f;
      break;
    case 302: // SPEED: changes both pitch and playback time together
      mSpeed = powf(value, 3.0f) * 9.99f + 0.01f;
      for (auto &v : mVoices) {
        if (v.active) {
          float keyShift = (mPlayMode == Chops || mPlayMode == OneShotChops ||
                            mPlayMode == LoopChops)
                               ? 0.0f
                               : (float)(v.note - 60);
          v.targetPitchRatio = powf(2.0f, (mPitch + keyShift) / 12.0f) * mSpeed;
        }
      }
      break;
    case 305: {
      int typeIdx = (int)(value + 0.5f);
      mFilterType = static_cast<TSvf::Type>(std::max(0, std::min(4, typeIdx)));
      break;
    }
    case 303: // Filter Cutoff
      setFilterCutoff(value);
      break;
    case 304: // Filter Resonance
      setFilterResonance(value);
      break;
    case 310:
      mAttack = value;
      break;
    case 311:
      mDecay = value;
      break;
    case 312:
      mSustain = value;
      break;
    case 313:
      mRelease = value;
      break;
    case 314: // Filter EG Intensity
      setFilterEnvAmount(value);
      break;
    case 355:
      setGlide(value);
      break;
    case 112: // Alternate Cutoff
      setFilterCutoff(value);
      break;
    case 113: // Alternate Resonance
      setFilterResonance(value);
      break;
    case 320: {
      int newMode = Scrub;
      if (value < 0.16f)
        newMode = OneShot;
      else if (value < 0.33f)
        newMode = Sustain;
      else if (value < 0.5f)
        newMode = Loop;
      else if (value < 0.66f)
        newMode = Chops;
      else if (value < 0.83f)
        newMode = OneShotChops;
      else if (value < 0.95f)
        newMode = LoopChops;

      if (newMode != mPlayMode) {
        mPlayMode = (PlayMode)newMode;
        // Silence all voices to prevent artifacts
        for (auto &v : mVoices)
          v.active = false;
      }
      break;
    }
    case 360: // Scrub Position
      mScrubPosition = value;
      break;
    case 361: // Scrub Gate
      mScrubGate = (value > 0.5f);
      break;
    case 330:
      mTrimStart = value;
      break;
    case 331:
      mTrimEnd = value;
      break;
    case 350:
      mUseEnvelope = value > 0.5f;
      break;
    case 351:
      mReverse = value > 0.5f;
      break;
    case 340: {
      int count = static_cast<int>(value * 15.0f) + 1; // 1 to 16
      findConstrainedSlices(count);
      break;
    }
    case 341: { // Slice Index Lock
      mSliceIndex = value;
      break;
    }
    case 342: { // Slice Lock ENABLED
      mSliceLockEnabled = value > 0.5f;
      break;
    }
    case 118: // Filter Env Amount
      setFilterEnvAmount(value);
      break;
    case 362: // SYNC ENABLED
      mSyncEnabled = (value > 0.5f);
      calculateStretch();
      break;
    case 363: // SOURCE BPM
      mSourceBpm = 60.0f + value * 140.0f;
      calculateStretch();
      break;
    case 364: // BEATS
      mBeats = floorf(value * 63.0f) + 1.0f;
      calculateStretch();
      break;
    case 365: // REPEATS
      mRepeats = static_cast<int>(value * 15.0f) + 1;
      break;
    }
    for (auto &v : mVoices) {
      if (v.active)
        v.envelope.setParameters(mAttack, mDecay, mSustain, mRelease);
    }
  }

  void setAttack(float v) { mAttack = v; }
  void setDecay(float v) { mDecay = v; }
  void setSustain(float v) { mSustain = v; }
  void setRelease(float v) { mRelease = v; }
  void setFilterCutoff(float v) { mFilterCutoff = v; }
  void setFilterResonance(float v) { mFilterResonance = v; }
  void setFilterEnvAmount(float v) { mFilterEnvAmount = v; }
  void setPitchBend(float v) { mPitchBend = v; }

  // BPM Sync (Public)
  bool mSyncEnabled = false;
  float mSourceBpm = 80.0f;
  float mBeats = 1.0f;
  float mProjectBpm = 80.0f;
  int mRepeats = 1;

  void calculateStretch() {
    if (mSyncEnabled) {
      if (mProjectBpm > 1.0f && mSourceBpm > 1.0f) {
        mStretch = mSourceBpm / mProjectBpm;
      }
    }
  }

  void setProjectBpm(float bpm) {
    mProjectBpm = bpm;
    calculateStretch();
  }

  void renderBlock(float* outL, float* outR, int numFrames, float* fxBusesL = nullptr, float* fxBusesR = nullptr) {
    for (int i = 0; i < numFrames; ++i) {
      float s = render(fxBusesL, fxBusesR);
      outL[i] = s;
      outR[i] = s;
    }
  }

  float render(float *fxBusesL = nullptr, float *fxBusesR = nullptr) {
    // Early exit before any per-sample work if no sample is loaded
    const auto &buffer = getBuffer();
    if (buffer.empty())
      return 0.0f;

    // Basic Time Tracking for Voice Stealing (audio-thread-only, plain double)
    mCurrentTime += (1.0 / mSampleRate);

    float mixedOutput = 0.0f;
    int activeCount = 0;

    // 1. Calculate Scrub Physics (Voice 0 only)
    Voice &scrubVoice = mVoices[0];

    // FIX: Only run this in Scrub Mode (prevents Auto-Play loop in other
    // Track target for per-sample interpolation
    if (mPlayMode == Scrub) {
      mLastScrubParam =
          mScrubPosition; // Will be used in next render() for range
    }

    // Hard Speed Limit
    if (mSmoothSpeed > 12.0)
      mSmoothSpeed = 12.0;
    if (mSmoothSpeed < -12.0)
      mSmoothSpeed = -12.0;

    // Apply to Voice 0 state
    if (mPlayMode == Scrub) {
      if (!scrubVoice.active) {
        scrubVoice.active = true;
        scrubVoice.pitchRatio = 0.0f;
        mSmoothSpeed = 0.0;
        scrubVoice.envelope.forceSustain();
      }
      mLastScrubGate = mScrubGate;
    }

    // Process ALL voices
    for (size_t i = 0; i < mVoices.size(); i++) {
      // Logic for Scrub Voice 0 is handled below by overriding rate

      auto &v = mVoices[i];
      if (!v.active)
        continue;

      // If Envelope is disabled, behave like a Gate (1.0 while held, 0.0 on
      // release)
      float env = 1.0f;
      if (mUseEnvelope) {
        env = v.envelope.nextValue();
      } else {
        // Gate behavior: If released, cut immediately (or fast fade?)
        // Simple Gate:
        if (v.envelope.getStage() == AdsrStage::Idle ||
            v.envelope.getStage() == AdsrStage::Release) {
          env = 0.0f;
        }
        // Need to advance envelope state even if not using value, to track
        // Release phase
        v.envelope.nextValue();
      }

      if (env < 0.0001f && (!mUseEnvelope || !v.envelope.isActive())) {
        v.active = false;
        continue;
      }
      activeCount++;

      if (mGlide > 0.001f) {
        float glideTimeSamples = mGlide * mSampleRate * 0.5f;
        float glideAlpha = 1.0f / (glideTimeSamples + 1.0f);
        v.pitchRatio += (v.targetPitchRatio - v.pitchRatio) * glideAlpha;
      } else {
        v.pitchRatio = v.targetPitchRatio;
      }

      float bendFactor = powf(2.0f, mPitchBend / 12.0f);
      float pitchFactor = v.pitchRatio * bendFactor;
      float direction = mReverse ? -1.0f : 1.0f;
      if (mSliceLockEnabled) {
        direction = (v.sliceReverse > 0.5f) ? -1.0f : 1.0f;
      }

      // Scrub Handling: Voice 0 should not advance automatically

      float baseResampleRate = mSpeed * pitchFactor * direction;

      // OVERRIDE for Scrub Voice 0
      // In Scrub Mode, we disable Granular synthesis to avoid "Grainy"
      // artifacts. We rely on 'baseResampleRate' being 0 (because we updated
      // position manually above). AND we force useGranular = false.

      float traverseRate = mSpeed * direction;
      float readRate = v.pitchRatio;
      bool useGranular = (std::abs(traverseRate - readRate) > 0.001f) ||
                         (std::abs(mStretch - 1.0f) > 0.02f);

      if (mPlayMode == Scrub) {
        if (i == 0) {
          // SCRUB MODE: Use mSmoothSpeed as the per-sample advance rate.
          // This ensures smooth, sample-accurate playback without stepping.
          // Apply pitch bend so X-axis pad gesture still affects pitch in Scrub
          baseResampleRate = (float)mSmoothSpeed * bendFactor;
          useGranular = false; // FORCE CLASSIC MODE (Fixes Grainy Sound)
          traverseRate = 0;
        }
      }

      if (std::abs(mStretch - 1.0f) > 0.02f)
        traverseRate /= std::max(0.01f, mStretch);

      // Update loop/trim points dynamically
      // Update loop/trim points dynamically
      if (mPlayMode != Chops && mPlayMode != OneShotChops &&
          mPlayMode != LoopChops && mPlayMode != Scrub) {
        v.start = static_cast<size_t>(mTrimStart * buffer.size());
        v.end = static_cast<size_t>(mTrimEnd * buffer.size());
      } else if (mPlayMode == Scrub) {
        // Scrub Mode: Ensure we scrub within the trimmed section
        v.start = static_cast<size_t>(mTrimStart * buffer.size());
        v.end = static_cast<size_t>(mTrimEnd * buffer.size());
        if (v.end > buffer.size())
          v.end = buffer.size();
        if (v.start >= v.end && v.end > 0)
          v.start = v.end - 1;
      }

      float voiceOutput = 0.0f;
      if (!useGranular) {
        // Classic mode: Resampling

        // ANALOGUE SCRUB: Update physics PER SAMPLE for Voice 0
        if (mPlayMode == Scrub && i == 0) {
          double trimStartSamples = mTrimStart * buffer.size();
          double trimEndSamples = mTrimEnd * buffer.size();

          // Slew-limit the target position to avoid parameter-rate snaps
          // (Increased to 0.02f for snappier feel)
          mSmoothedScrubPos += (mScrubPosition - mSmoothedScrubPos) * 0.02f;
          double currentTarget = mSmoothedScrubPos * buffer.size();

          // Clamp target
          currentTarget = std::max(trimStartSamples,
                                   std::min(trimEndSamples, currentTarget));

          // Interaction Detection: Detect if user is touching OR if the target
          // is still moving towards the smoothing goal
          double targetDelta = std::abs(mScrubPosition - mSmoothedScrubPos);
          bool isInteracting = mScrubGate || (targetDelta > 0.0001);

          // Mass-Spring-Damper Physics
          double dist = currentTarget - v.position;
          // Stiffness (k): Reverting to "Ultra Heavy Mass" v2.1 constants
          // (3e-7) This eliminates high-freq ringing (graininess) and provides
          // fluid movement.
          double k = 0.0000003;
          double springForce = dist * k;

          // Damping (drag): Higher damping to suppress any tiny oscillations
          double drag = 0.002;
          double dampingForce = -mSmoothSpeed * drag;

          if (isInteracting) {
            // Initial Snap logic on fresh touch
            if (mScrubGate && !mLastScrubGate) {
              v.position = currentTarget;
              mSmoothSpeed = 0.0;
              mSmoothedScrubPos = mScrubPosition;
            }

            mSmoothSpeed += (springForce + dampingForce);
          } else if (mMotorRunning) {
            // Playback motor (1.0x speed)
            mSmoothSpeed += (1.0 - mSmoothSpeed) * 0.0005;
          } else {
            // NATURAL DECAY: Reduced platter friction for longer "throw"
            // coasting
            mSmoothSpeed *= 0.99998;
            if (std::abs(mSmoothSpeed) < 0.015)
              mSmoothSpeed = 0.0;
          }

          mLastScrubParam = mScrubPosition;
          mLastScrubGate = mScrubGate;

          // Limit speed
          if (mSmoothSpeed > 12.0)
            mSmoothSpeed = 12.0;
          if (mSmoothSpeed < -12.0)
            mSmoothSpeed = -12.0;

          baseResampleRate = (float)mSmoothSpeed;
        }

        v.position += baseResampleRate;

        // Non-destructive trim boundary enforcement
        if (v.position >= (double)v.end) {
          if (mPlayMode == Sustain || mPlayMode == Loop ||
              mPlayMode == LoopChops) {
            v.position = (double)v.start;
          } else {
            v.position = (double)v.end - 0.001;
            v.envelope.release();
            if (mPlayMode == Scrub) {
              mMotorRunning = false;
              mSmoothSpeed = 0.0; // Stop velocity on wall
            }
          }
        } else if (v.position < (double)v.start) {
          if (mPlayMode == Sustain || mPlayMode == Loop ||
              mPlayMode == LoopChops) {
            v.position = (double)v.end - 1.0;
          } else {
            v.position = (double)v.start;
            v.envelope.release();
            if (mPlayMode == Scrub) {
              mMotorRunning = false;
              mSmoothSpeed = 0.0; // Stop velocity on wall
            }
          }
        }

        int idx = static_cast<int>(v.position);
        if (idx >= 0 && idx < (int)buffer.size()) {
          // Use High-Quality Cubic (Hermite) Interpolation specifically for
          // Scrubbing to provide a smooth, analogue quality and minimize
          // aliasing noise.
          if (mPlayMode == Scrub && i == 0) {
            voiceOutput = getCubicInterpolatedSample(buffer, v.position);
          } else {
            voiceOutput = getInterpolatedSample(buffer, v.position);
          }
        }
      } else {
        // Granular mode
        v.position += traverseRate;
        v.grainTimer++;

        // Grain 1
        double gp1 = v.position + (v.grainTimer * (readRate - traverseRate));
        int idx1 = static_cast<int>(gp1);

        // Grain 2
        uint32_t timer2 =
            (v.grainTimer + (Voice::GRAIN_SIZE / 2)) % Voice::GRAIN_SIZE;
        double gp2 = v.position + (timer2 * (readRate - traverseRate));
        int idx2 = static_cast<int>(gp2);

        float phase = (float)v.grainTimer / (float)Voice::GRAIN_SIZE;
        float w1 = 1.0f - std::abs(phase * 2.0f - 1.0f);

        // Grain 1 & 2 read with strict boundary enforcement
        auto getClampedSample = [&](double pos) -> float {
          int idx = static_cast<int>(pos);
          if (idx >= (int)v.start && idx < (int)v.end) {
            // Apply strict clamping to prevent small interpolation leaks from
            // neighboring slices
            double clampedPos = std::max((double)v.start,
                                         std::min((double)v.end - 1.0001, pos));
            return getInterpolatedSample(buffer, clampedPos);
          }
          return 0.0f;
        };

        float s1 = getClampedSample(gp1);
        float s2 = getClampedSample(gp2);

        voiceOutput = (s1 * w1) + (s2 * (1.0f - w1));

        if (v.grainTimer >= Voice::GRAIN_SIZE) {
          v.grainTimer = 0;
        }

        if (v.position >= v.end || v.position < v.start) {
          if (mPlayMode == Sustain || mPlayMode == Loop ||
              mPlayMode == LoopChops) {
            v.position = mReverse ? (double)v.end - 1.0 : (double)v.start;
          } else {
            v.envelope.release();
            if (mPlayMode == Scrub) {
              mMotorRunning = false;
            }
          }
        }
      }

      // Filter Processing
      if (v.controlCounter++ % 16 == 0) {
        float baseCutoff = mSliceLockEnabled ? v.sliceCutoff : mFilterCutoff;
        float baseReson =
            mSliceLockEnabled ? v.sliceResonance : mFilterResonance;

        float cutoff = 20.0f + (baseCutoff * baseCutoff * baseCutoff * 19980.0f);
        // Integrate envelope to filter cutoff
        cutoff += env * mFilterEnvAmount * 12000.0f;
        cutoff = std::max(20.0f, std::min(20000.0f, cutoff));

        v.filter.setParams(cutoff, 0.7f + baseReson * 5.0f, 48000.0f);
      }
      voiceOutput = v.filter.process(voiceOutput, mFilterType);

      float finalSample = voiceOutput * env * v.baseVelocity;
      mixedOutput += finalSample;

      // Accumulate into per-slice FX buses if enabled
      if (mSliceLockEnabled && fxBusesL && fxBusesR) {
        for (int b = 0; b < 17; ++b) {
          if (v.fxSends[b] > 0.0001f) {
            fxBusesL[b] += finalSample * v.fxSends[b];
            fxBusesR[b] += finalSample * v.fxSends[b];
          }
        }
      }
    }

    if (activeCount > 1)
      mixedOutput *= (1.0f / sqrtf((float)activeCount));

    return mixedOutput;
  }

  void findConstrainedSlices(int count) {
    const auto &buf = getBuffer();
    std::lock_guard<std::mutex> lock(mSliceLock);
    mSlices.clear();
    if (buf.empty() || count <= 0)
      return;

    size_t totalSamples = buf.size();
    size_t avgLength = totalSamples / count;
    size_t windowSize = avgLength; // +/- 50% search window centered at beat

    size_t currentStart = 0;
    for (int i = 1; i < count; ++i) {
      size_t idealEnd = i * avgLength;

      // Search for strongest transient in window [idealEnd - windowSize/2,
      // idealEnd + windowSize/2]
      size_t searchStart =
          (idealEnd > windowSize / 2) ? (idealEnd - windowSize / 2) : 0;
      size_t searchEnd =
          std::min(totalSamples - 256, idealEnd + windowSize / 2);

      size_t bestTransient = idealEnd;
      float maxEnergyJump = 0.0f;
      float prevEnergy = 0.0f;

      // Use smaller window for transient detection within the search window
      const int energyWindow = 256;
      for (size_t j = searchStart; j < searchEnd - energyWindow; j += 128) {
        float energy = 0.0f;
        for (int k = 0; k < energyWindow; ++k) {
          float s = buf[j + k];
          energy += s * s;
        }

        if (j > searchStart) {
          float jump = energy / (prevEnergy + 0.001f);
          if (jump > maxEnergyJump && energy > 0.01f) {
            maxEnergyJump = jump;
            bestTransient = j;
          }
        }
        prevEnergy = energy;
      }

      // Require a decent jump to snap, otherwise stay at ideal beat
      size_t sliceEnd = (maxEnergyJump > 1.4f) ? bestTransient : idealEnd;
      mSlices.push_back({currentStart, sliceEnd});
      currentStart = sliceEnd;
    }
    mSlices.push_back({currentStart, totalSamples});
  }

  void prepareSlices(int count) {
    const auto &buf = getBuffer();
    std::lock_guard<std::mutex> lock(mSliceLock);
    mSlices.clear();
    if (buf.empty() || count <= 0)
      return;
    size_t step = buf.size() / count;
    for (int i = 0; i < count; ++i) {
      mSlices.push_back({i * step, (i + 1) * step});
    }
  }

  std::vector<float> getSlicePoints() const {
    const auto &buf = getBuffer();
    std::vector<float> points;
    if (buf.empty())
      return points;
    for (const auto &s : mSlices) {
      points.push_back((float)s.start / (float)buf.size());
    }
    return points;
  }

  void pushSamples(const float *buffer, int count) {
    if (count <= 0)
      return;
    std::lock_guard<std::mutex> lock(mRecordingLock);
    mRecordingBuffer.insert(mRecordingBuffer.end(), buffer, buffer + count);
  }

  std::vector<float> getAmplitudeWaveform(int numPoints) const {
    const auto &buf = getBuffer();

    // Use recording buffer if active and non-empty
    // We hold the lock ONLY to determine source and size, or we must hold it
    // during read? If mRecordingBuffer is being written to, we MUST hold
    // mRecordingLock during read. Optimization: Don't copy. Iterate with lock
    // held but sparsely.

    std::unique_lock<std::mutex> lock(
        (const_cast<SamplerEngine *>(this))->mRecordingLock);

    const std::vector<float> *sourcePtr;
    if (!mRecordingBuffer.empty()) {
      sourcePtr = &mRecordingBuffer;
    } else {
      sourcePtr = &buf;
      // If not using recording buffer, we can release the recording lock?
      // But buf (mBuffers) is stable unless commit happen (on audio thread).
      // We are on UI thread. Audio thread might swap buffers.
      // So strictly we need safety. But swap is atomic index change.
      // Reading mBuffers[active] is safe if we loaded active index freshly.
      // But let's keep lock for simplicity and safety against race
      // conditions.
    }

    const auto &source = *sourcePtr;
    std::vector<float> result;
    if (source.empty())
      return result;

    int step = source.size() / numPoints;
    if (step < 1)
      step = 1;

    // OPTIMIZATION: Strided sampling
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

  // Add dummy PlayheadInfo for matching Granular signature
  struct PlayheadInfo {
    float pos;
    float vol;
    float start;
    float end;
  };

  void getPlayheads(PlayheadInfo *out, int maxCount) const {
    const auto &buf = getBuffer();
    size_t bufSize = buf.empty() ? 1 : buf.size();
    for (int i = 0; i < maxCount; ++i) {
      if (i < (int)mVoices.size()) {
        out[i].pos = mVoices[i].active
                         ? (float)(mVoices[i].position / (double)bufSize)
                         : -1.0f;
        out[i].vol = mVoices[i].active ? 1.0f : 0.0f;
        out[i].start = mVoices[i].active
                         ? (float)((double)mVoices[i].start / (double)bufSize)
                         : 0.0f;
        out[i].end = mVoices[i].active
                         ? (float)((double)mVoices[i].end / (double)bufSize)
                         : 1.0f;
      } else {
        out[i].pos = -1.0f;
        out[i].vol = 0.0f;
        out[i].start = 0.0f;
        out[i].end = 1.0f;
      }
    }
  }

  void setSliceParameter(int sliceIdx, int paramId, float value) {
    if (sliceIdx < 0 || sliceIdx >= 16)
      return;
    auto &p = mSliceParams[sliceIdx];
    p.active = true;
    switch (paramId) {
    case 0: // Pitch
      p.pitch = (value - 0.5f) * 48.0f;
      break;
    case 1: // Cutoff
      p.cutoff = value;
      break;
    case 2: // Resonance
      p.resonance = value;
      break;
    case 3: // Reverse
      p.reverse = value;
      break;
    case 4: // Attack
      p.attack = value;
      break;
    case 5: // Decay
      p.decay = value;
      break;
    case 6: // Sustain
      p.sustain = value;
      break;
    case 7: // Release
      p.release = value;
      break;
    default:
      if (paramId >= 8 && paramId < 8 + 17) {
        p.fxSends[paramId - 8] = value;
      }
      break;
    }

    // Apply to active voices playing this slice
    for (auto &v : mVoices) {
      if (v.active && v.sliceIdx == sliceIdx) {
        if (paramId == 0) { // Pitch
          float keyShift = (mPlayMode == Chops || mPlayMode == OneShotChops ||
                            mPlayMode == LoopChops)
                               ? 0.0f
                               : (float)(v.note - 60);
          v.targetPitchRatio =
              powf(2.0f, (p.pitch + keyShift) / 12.0f) * mSpeed;
        } else if (paramId == 3) { // Reverse
          v.sliceReverse = value;
        } else if (paramId == 1) { // Cutoff
          v.sliceCutoff = value;
        } else if (paramId == 2) { // Resonance
          v.sliceResonance = value;
        } else if (paramId >= 8 && paramId < 8 + 17) {
          v.fxSends[paramId - 8] = value;
        }
      }
    }
  }

  bool isActive() const {
    for (const auto &v : mVoices)
      if (v.active)
        return true;
    return false;
  }

  // Double-buffering for lock-free audio
  mutable std::atomic<int> mActiveBuffer{0};
  std::vector<float> mBuffers[2];
  std::mutex mRecordingLock;
  std::vector<float> mRecordingBuffer;
  std::mutex mSliceLock;

  std::atomic<bool> mReverse{false};

  // Time tracking
  double mCurrentTime = 0.0; // Audio-thread-only; no atomic needed

private:
  std::vector<Voice> mVoices;
  std::vector<Slice> mSlices;

  // Scrub State
  float mScrubPosition = 0.0f;
  double mLastTargetPos = 0.0;
  bool mScrubGate = false;
  bool mLastScrubGate = false;
  bool mMotorRunning = false; // "Motor" state for Scrub Playback
  double mSmoothSpeed = 0.0;
  double mSmoothedScrubPos = 0.0;
  double mLastScrubParam = 0.0; // Track previous target for velocity calc

  // Parameters
  float mTrimStart = 0.0f;
  float mTrimEnd = 1.0f;
  float mPitch = 0.0f;
  float mStretch = 1.0f;
  float mSpeed = 1.0f;
  float mAttack = 0.01f, mDecay = 0.1f, mSustain = 0.8f, mRelease = 0.2f;
  float mFilterCutoff = 0.5f, mFilterResonance = 0.0f, mFilterEnvAmount = 0.0f;
  TSvf::Type mFilterType = TSvf::LowPass;
  float mGlide = 0.0f, mLastPitchRatio = 1.0f;
  float mPitchBend = 0.0f;
  PlayMode mPlayMode = OneShot;
  float mSliceIndex = -1.0f; // Range 0.0 to 1.0, -1 means use note
  bool mUseEnvelope = true;
  int mSampleRate = 48000;
  SliceParams mSliceParams[16];
  bool mSliceLockEnabled = false;
};

#endif // SAMPLER_ENGINE_H
