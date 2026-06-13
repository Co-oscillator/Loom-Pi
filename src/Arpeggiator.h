#ifndef ARPEGGIATOR_H
#define ARPEGGIATOR_H

#include "ChordProgressionEngine.h"
#ifdef __ANDROID__

#endif
#include <algorithm>
#include <chrono>
#include <random>
#include <vector>

enum class ArpMode {
  OFF = 0,
  UP = 1,
  DOWN = 2,
  UP_DOWN = 3,
  STAGGER_UP = 4,
  STAGGER_DOWN = 5,
  RANDOM = 6,
  BACH = 7,
  BROWNIAN = 8,
  CONVERGE = 9,
  DIVERGE = 10
};

class Arpeggiator {
public:
  Arpeggiator()
      : mMode(ArpMode::OFF), mStep(0), mNoteIndex(0), mOctaves(0),
        mInversion(0), mIsLatched(false), mIsWaitingForNewGesture(false),
        mUpperLane1Index(0), mUpperLane2Index(0),
        mRng(static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count() ^
            std::random_device{}())),
        mProbability(1.0f), mWeird(0.0f), mRateMultiplier(1.0f), mSpeedMultiplier(1.0f) {
    // Default: Lane 0 (Root) active, Lanes 1-3 inactive
    mRhythms.resize(4, std::vector<bool>(16, false));
    std::fill(mRhythms[0].begin(), mRhythms[0].end(), true);
    mScaleIntervals = {0, 2, 4, 5, 7, 9, 11}; // Default Major

    mHeldNotes.reserve(32);
    mSequence.reserve(128);
    mNotesToPlay.reserve(8);
    mDroppedNotes.reserve(128);
  }

  void setProbability(float prob) {
    mProbability = prob;
  }
  float getProbability() const { return mProbability; }

  void setWeird(float weird) {
    mWeird = weird;
    if (mWeird <= 0.001f) {
      mRateMultiplier = 1.0f;
      mSpeedMultiplier = 1.0f;
    }
  }
  float getWeird() const { return mWeird; }

  float getRateMultiplier() const { return mRateMultiplier; }
  float getSpeedMultiplier() const { return mSpeedMultiplier; }

  void setChordProgConfig(bool enabled, int mood, int complexity) {
    mIsChordProgEnabled = enabled;
    mChordProgMood = mood;
    mChordProgComplexity = complexity;
    generateChordProgression();
    updateSequence();
  }

  void setScaleConfig(int rootNote, const std::vector<int> &scaleIntervals) {
    mRootNote = rootNote;
    mScaleIntervals = scaleIntervals;
    generateChordProgression();
    updateSequence();
  }

  void setMode(ArpMode mode) {
    if (mode != mMode) {
      mMode = mode;
      mStep = 0;
      mNoteIndex = 0;
      updateSequence();
    }
  }
  ArpMode getMode() const { return mMode; }
  void setOctaves(int octaves) {
    mOctaves = octaves;
    updateSequence();
  }
  int getOctaves() const { return mOctaves; }
  void setInversion(int inversion) {
    mInversion = inversion;
    updateSequence();
  }
  int getInversion() const { return mInversion; }
  int getScaleRoot() const { return mRootNote; }
  void setRhythm(const std::vector<std::vector<bool>> &rhythms) {
    mRhythms = rhythms;
  }
  const std::vector<std::vector<bool>>& getRhythm() const { return mRhythms; }
  void setRandomSequence(const std::vector<int> &sequence) {
    mRandomSequence = sequence;
    updateSequence();
  }
  void setGateLengths(const std::vector<float> &gateLengths) {
    mGateLengths = gateLengths;
  }
  float getCurrentGate() const {
    if (mGateLengths.empty())
      return 0.5f;
    int maxSteps = mIsLatched ? 16 : 8;
    return mGateLengths[mStep % maxSteps];
  }
  void setIsMutated(bool mutated) { mIsMutated = mutated; }

  bool isLatched() const { return mIsLatched; }
  bool isChordProgEnabled() const { return mIsChordProgEnabled; }
  int getChordProgMood() const { return mChordProgMood; }
  int getChordProgComplexity() const { return mChordProgComplexity; }

  void setLatched(bool latched) {
    mIsLatched = latched;
    if (!latched) {
      mHeldNotes.clear();
      mSequence.clear();
      mIsWaitingForNewGesture = false;
    }
  }

  const std::vector<int> &getNotes() const { return mHeldNotes; }

  void addNote(int note) {
    if (mIsLatched && mIsWaitingForNewGesture) {
      mHeldNotes.clear();
      mIsWaitingForNewGesture = false;
    }

    bool wasEmpty = mHeldNotes.empty();

    if (std::find(mHeldNotes.begin(), mHeldNotes.end(), note) ==
        mHeldNotes.end()) {
      mHeldNotes.push_back(note);
      std::sort(mHeldNotes.begin(), mHeldNotes.end());

      generateChordProgression();
      updateSequence();

      if (wasEmpty) {
        mStep = 0; // Fresh start for new gesture
        mNoteIndex = 0;
        
        // Per-gesture randomization roll (Only for unlatched)
        if (!mIsLatched) {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            mHasDropsInCurrentGesture = (mProbability > 0.001f && dist(mRng) < mProbability);
            mIsWeirdInCurrentGesture = (mWeird > 0.001f && dist(mRng) < mWeird);

            if (mIsWeirdInCurrentGesture) {
                rollRandomVariations();
            } else {
                mRateMultiplier = 1.0f;
                mSpeedMultiplier = 1.0f;
            }
        } else {
            mHasDropsInCurrentGesture = false;
            mIsWeirdInCurrentGesture = false;
            mRateMultiplier = 1.0f;
            mSpeedMultiplier = 1.0f;
        }
      }
    }
  }

  void removeNote(int note) {
    if (mIsLatched) {
      // In latched mode, we don't remove notes until a new gesture starts
      return;
    }
    auto it = std::find(mHeldNotes.begin(), mHeldNotes.end(), note);
    if (it != mHeldNotes.end()) {
      mHeldNotes.erase(it);
      generateChordProgression();
      updateSequence();
    }
  }

  void onAllPhysicallyReleased() {
    if (mIsLatched) {
      mIsWaitingForNewGesture = true;
    } else {
      mHeldNotes.clear();
      mRateMultiplier = 1.0f;
      mSpeedMultiplier = 1.0f;
      mStep = 0;
      mNoteIndex = 0;
      mHasDropsInCurrentGesture = false;
      mIsWeirdInCurrentGesture = false;
      updateSequence();
    }
    // Safety: always reset multipliers if weird is zero, even in latch
    if (mWeird <= 0.001f) {
        mRateMultiplier = 1.0f;
        mSpeedMultiplier = 1.0f;
    }
  }

  void clear() {
    mHeldNotes.clear();
    mSequence.clear();
    mGeneratedChordProgression.clear();
    mStep = 0;
    mNoteIndex = 0;
    mLastHarmonicStep = -1;
    mIsWaitingForNewGesture = false;
  }

  const std::vector<int> &nextNotes() {
    mNotesToPlay.clear();
    if (mSequence.empty() || mMode == ArpMode::OFF || mRhythms.empty())
      return mNotesToPlay;

    // Check for harmonic step change
    const int stepsPerChord = mIsLatched ? 32 : 8;

    if (mIsChordProgEnabled && !mGeneratedChordProgression.empty()) {
      int harmonicStep = (mStep / stepsPerChord) % 16;
      if (harmonicStep != mLastHarmonicStep) {
        mLastHarmonicStep = harmonicStep;
        updateSequence(); // Refresh sequence with new chord notes merged
      }
    }

    int maxSteps = mIsLatched ? 16 : 8;
    int stepIndex = mStep % maxSteps; // Conditional step pattern

    int seqSize = (int)mSequence.size();
    bool playedAnyInStep = false;

    // Roll random number per step
    std::uniform_real_distribution<float> probDist(0.0f, 1.0f);
    bool stepAllowed = (probDist(mRng) <= mProbability); // probability of playing this step!

    // Helper to add note with probability check
    auto addNoteIfVisible = [&](int idx) {
        if (idx >= 0 && idx < seqSize) {
            if (stepAllowed) {
                int noteIdx = mSequence[idx];
                mNotesToPlay.push_back(noteIdx);
            }
            playedAnyInStep = true;
        }
    };

    // Lane 2: Root/Main Note (UI Row 2)
    if (mRhythms.size() > 2 && mRhythms[2][stepIndex]) {
      if (stepAllowed) {
          int idx = mNoteIndex % seqSize;
          int noteIdx = mSequence[idx];
          if (mInversion != 0 && (mNoteIndex % seqSize) == 0) {
            noteIdx += mInversion * 12; // Apply inversion to root of cycle
          }
          mNotesToPlay.push_back(noteIdx);
      }
      playedAnyInStep = true; 
    }

    // Lane 1: +1 Walk (UI Row 1)
    if (mRhythms.size() > 1 && mRhythms[1][stepIndex]) {
      addNoteIfVisible((mNoteIndex + 1) % seqSize);
    }

    // Lane 0: +2 Walk (UI Row 0)
    if (mRhythms.size() > 0 && mRhythms[0][stepIndex]) {
      addNoteIfVisible((mNoteIndex + 2) % seqSize);
    }

    // Lane 3: -1 Walk (UI Row 3)
    if (mRhythms.size() > 3 && mRhythms[3][stepIndex]) {
      addNoteIfVisible((mNoteIndex - 1 + seqSize) % seqSize);
    }

    if (playedAnyInStep) {
      mNoteIndex++;
    }

    mStep++;
    return mNotesToPlay;
  }

  void setStrum(float strum) { mStrum = strum; }
  float getStrum() const { return mStrum; }

  void reset() {
    mStep = 0;
    mNoteIndex = 0;
  }

private:
  ArpMode mMode;
  int mStep;
  int mNoteIndex;
  int mOctaves;
  int mInversion;
  float mStrum = 0.0f; // 0.0 to 1.0 (Spread over step duration)
  bool mIsLatched;
  bool mIsMutated = false;
  bool mIsWaitingForNewGesture;
  std::vector<int> mHeldNotes;
  std::vector<int> mSequence;
  std::vector<int> mNotesToPlay;
  std::vector<std::vector<bool>> mRhythms; // 4 lanes x 16 steps
  std::vector<float> mGateLengths;
  std::vector<int> mRandomSequence;
  std::mt19937 mRng;

  bool mIsChordProgEnabled = false;
  int mChordProgMood = 0;
  int mChordProgComplexity = 0;
  int mRootNote = 48; // C3
  std::vector<int> mScaleIntervals;
  std::vector<std::vector<int>> mGeneratedChordProgression;

  void generateChordProgression() {
    if (mIsChordProgEnabled && !mHeldNotes.empty()) {
      const int stepsPerChord = mIsLatched ? 32 : 8;
      mGeneratedChordProgression = ChordProgressionEngine::generateProgression(
          mRootNote, mScaleIntervals, mChordProgMood,
          static_cast<Complexity>(mChordProgComplexity), mHeldNotes);
    } else {
      mGeneratedChordProgression.clear();
    }
    mLastHarmonicStep = -1;
  }

  void updateSequence() {
    mSequence.clear();
    if (mHeldNotes.empty()) {
      mLastHarmonicStep = -1;
      return;
    }

    std::vector<int> baseNotes = mHeldNotes;

    // Merge Chord Progression Notes
    if (mIsChordProgEnabled && !mGeneratedChordProgression.empty()) {
      const int stepsPerChord = mIsLatched ? 32 : 8;
      int harmonicStep = (mStep / stepsPerChord) % 16;
      if (harmonicStep < (int)mGeneratedChordProgression.size()) {
        std::vector<int> chord = mGeneratedChordProgression[harmonicStep];
        for (int n : chord) {
          if (std::find(baseNotes.begin(), baseNotes.end(), n) ==
              baseNotes.end()) {
            baseNotes.push_back(n);
          }
        }
      }
    }

    // Expand octaves
    std::vector<int> expanded;
    expanded.reserve(32);
    int startOct = std::min(0, mOctaves);
    int endOct = std::max(0, mOctaves);
    for (int o = startOct; o <= endOct; ++o) {
      for (int n : baseNotes) {
        expanded.push_back(n + (o * 12));
      }
    }
    std::sort(expanded.begin(), expanded.end());
    expanded.erase(std::unique(expanded.begin(), expanded.end()),
                   expanded.end());

    switch (mMode) {
    case ArpMode::UP:
      mSequence = expanded;
      break;
    case ArpMode::DOWN:
      mSequence = expanded;
      std::reverse(mSequence.begin(), mSequence.end());
      break;
    case ArpMode::UP_DOWN:
      mSequence = expanded;
      for (int i = (int)expanded.size() - 2; i > 0; --i) {
        mSequence.push_back(expanded[i]);
      }
      break;
    case ArpMode::STAGGER_UP:
      // Example: 1-3-2-4-3-5...
      for (size_t i = 0; i < expanded.size(); ++i) {
        mSequence.push_back(expanded[i]);
        if (i + 2 < expanded.size()) {
          mSequence.push_back(expanded[i + 2]);
        }
      }
      break;
    case ArpMode::STAGGER_DOWN:
      mSequence = expanded;
      std::reverse(mSequence.begin(), mSequence.end());
      // similar stagger logic
      break;
    case ArpMode::RANDOM:
      if (!mRandomSequence.empty()) {
        for (int idx : mRandomSequence) {
          mSequence.push_back(expanded[idx % expanded.size()]);
        }
      } else {
        mSequence = expanded;
        std::shuffle(mSequence.begin(), mSequence.end(), mRng);
      }
      break;
    case ArpMode::BACH: {
      // 3 steps forward, 1 step back
      if (expanded.empty())
        break;
      int size = (int)expanded.size();
      for (int i = 0; i < size + 4; ++i) { // Safety margin
        int groupSize = 3;
        int baseShift = i / groupSize;
        int internalStep = i % groupSize;
        int idx = (baseShift + internalStep) % size;
        mSequence.push_back(expanded[idx]);
      }
      break;
    }
    case ArpMode::CONVERGE: {
      // 0, Max, 1, Max-1...
      if (expanded.empty())
        break;
      int size = (int)expanded.size();
      for (int i = 0; i < size; ++i) {
        int offset = i / 2;
        int idx = (i % 2 == 0) ? offset : (size - 1 - offset);
        if (idx >= 0 && idx < size) {
          mSequence.push_back(expanded[idx]);
        }
      }
      break;
    }
    case ArpMode::DIVERGE: {
      // Inner -> Outer
      if (expanded.empty())
        break;
      int size = (int)expanded.size();
      int center = size / 2;
      for (int i = 0; i < size; ++i) {
        int offset = (i + 1) / 2;
        int idx = center + (offset * ((i % 2 == 1) ? -1 : 1));
        if (idx >= 0 && idx < size) {
          mSequence.push_back(expanded[idx]);
        }
      }
      break;
    }
    case ArpMode::BROWNIAN: {
      // Random walk simulation (pre-calc for loop stability)
      if (expanded.empty())
        break;
      int size = (int)expanded.size();
      int current = 0;
      std::uniform_int_distribution<int> dist(-1, 1);

      // Generate a nice long walk
      for (int i = 0; i < 32; ++i) {
        mSequence.push_back(expanded[current]);
        int move = dist(mRng);
        current = (current + move < 0)
                      ? 0
                      : (current + move >= size ? size - 1 : current + move);
      }
      break;
    }
    default:
      break;
    }
  }

  void rollRandomVariations() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Rate Logic (Speed up)
    // Tiers: <0.3 (mild, no 4x/7x), 0.3-0.6 (up to 4x), >0.6 (up to 7x)
    if (mWeird > 0.6f) {
        float r = dist(mRng);
        if (r < 0.25f) mRateMultiplier = 7.0f;
        else if (r < 0.55f) mRateMultiplier = 4.0f;
        else if (r < 0.85f) mRateMultiplier = 2.0f;
        else mRateMultiplier = (mWeird > 0.99f) ? 4.0f : 1.0f; // Guaranteed change at 100%
    } else if (mWeird > 0.3f) {
        float r = dist(mRng);
        if (r < 0.45f) mRateMultiplier = 4.0f;
        else if (r < 0.85f) mRateMultiplier = 2.0f;
        else mRateMultiplier = 1.0f;
    } else {
        float r = dist(mRng);
        if (r < 0.70f) mRateMultiplier = 2.0f;
        else mRateMultiplier = 1.0f;
    }

    // Speed Logic (Timing variation)
    // Applying similar tiers for speed variations
    if (mWeird > 0.6f) {
        float r = dist(mRng);
        if (r < 0.25f) mSpeedMultiplier = 3.0f;    // Extreme
        else if (r < 0.50f) mSpeedMultiplier = 1.5f; // Fast
        else if (r < 0.70f) mSpeedMultiplier = 0.5f; // Slow
        else if (r < 0.90f) mSpeedMultiplier = 0.75f;// Mild slow
        else mSpeedMultiplier = (mWeird > 0.99f) ? 1.25f : 1.0f; // Guaranteed change at 100%
    } else if (mWeird > 0.3f) {
        float r = dist(mRng);
        if (r < 0.45f) mSpeedMultiplier = 1.5f;
        else if (r < 0.90f) mSpeedMultiplier = 0.75f;
        else mSpeedMultiplier = 1.0f;
    } else {
        float r = dist(mRng);
        if (r < 0.50f) mSpeedMultiplier = 1.25f; // Milder speed up
        else if (r < 0.90f) mSpeedMultiplier = 0.85f; // Milder slow down
        else mSpeedMultiplier = 1.0f;
    }
  }

  int mLastHarmonicStep = -1;
  int mUpperLane1Index = 0;
  int mUpperLane2Index = 0;

  float mProbability;
  float mWeird;
  float mRateMultiplier;
  float mSpeedMultiplier;
  bool mHasDropsInCurrentGesture = false;
  bool mIsWeirdInCurrentGesture = false;
  std::vector<bool> mDroppedNotes;
};

#endif
