#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <algorithm>
#include <map>
#include <vector>

struct Step {
  struct NoteInfo {
    int note = 60;
    float velocity = 0.8f;
    float subStepOffset = 0.0f; // 0.0 to 1.0 within the step
  };

  bool active = false;
  bool isSkipped = false;
  std::vector<NoteInfo> notes;
  int ratchet = 1;    // 1 = regular, 2 = double, etc.
  bool punch = false; // 1.1x volume + overdrive
  float probability = 1.0f;
  float gate = 1.0f;                   // 1.0 = full step
  std::map<int, float> parameterLocks; // CC ID -> Locked Value

  void addNote(int n, float vel = 0.8f, float offset = 0.0f) {
    for (auto &existing : notes) {
      if (existing.note == n) {
        existing.velocity = vel;
        existing.subStepOffset = offset;
        return;
      }
    }
    notes.push_back({n, vel, offset});
    active = true;
  }

  void removeNote(int n) {
    for (auto it = notes.begin(); it != notes.end(); ++it) {
      if (it->note == n) {
        notes.erase(it);
        break;
      }
    }
    if (notes.empty())
      active = false;
  }
};

class Sequencer {
public:
  Sequencer() {
    mSteps.resize(64);
    reset();
  }

  void reset() {
    mCurrentStep = 0;
    mNextStep = 0;
    mNeedsInitialTrigger = true;
    mPingPongForward = true;
  }

  void setConfiguration(int numPages, int stepsPerPage) {
    mNumPages = numPages;
    mStepsPerPage = stepsPerPage;
    mLoopLength = numPages * stepsPerPage;
  }

  void setLoopLength(int length) { mLoopLength = length; }
  int getLoopLength() const { return mLoopLength; }

  void setStep(int index, const Step &step) {
    if (index >= 0 && index < 64)
      mSteps[index] = step;
  }

  bool advance() {
    int totalSteps = mLoopLength;
    if (totalSteps <= 0)
      return false;

    if (mNeedsInitialTrigger) {
      mNeedsInitialTrigger = false;
      mCurrentStep = 0;
      // Calculate next step for the NEXT advance call
      if (mDirection == 0) { // Forward
        mNextStep = (mCurrentStep + 1) % totalSteps;
      } else if (mDirection == 1) { // Backward
        mNextStep = (mCurrentStep - 1 + totalSteps) % totalSteps;
      } else {
        mNextStep = mCurrentStep; // Simplification for other modes on first hit
      }
      return false; // Not a loop on start
    }

    bool looped = false;
    mCurrentStep = mNextStep;

    if (mIsJumpMode) {
      mNextStep = mCurrentStep;
      return false;
    }

    int searchLimit = totalSteps;
    int searchCount = 0;

    do {
      if (mIsRandom) {
        mNextStep = rand() % totalSteps;
      } else {
        if (mDirection == 0) { // Forward
          mNextStep = mCurrentStep + 1;
          if (mNextStep >= totalSteps) {
            mNextStep = 0;
            looped = true;
          }
        } else if (mDirection == 1) { // Backward
          mNextStep = mCurrentStep - 1;
          if (mNextStep < 0) {
            mNextStep = totalSteps - 1;
            looped = true;
          }
        } else if (mDirection == 2) { // Ping-Pong
          if (mPingPongForward) {
            mNextStep = mCurrentStep + 1;
            if (mNextStep >= totalSteps) {
              mNextStep = std::max(0, totalSteps - 2);
              mPingPongForward = false;
              looped = true;
            }
          } else {
            mNextStep = mCurrentStep - 1;
            if (mNextStep < 0) {
              mNextStep = std::min(totalSteps - 1, 1);
              mPingPongForward = true;
              looped = true;
            }
          }
        }
      }
      mCurrentStep = mNextStep;
      searchCount++;
    } while (mSteps[mNextStep].isSkipped && searchCount < searchLimit);

    return looped;
  }

  const Step &getCurrentStep() const { return mSteps[mCurrentStep]; }
  int getCurrentStepIndex() const { return mCurrentStep; }
  int getCurrentPage() const { return mCurrentStep / mStepsPerPage; }

  float getSwing() const { return mSwing; }
  bool isEvenStep() const { return (mCurrentStep % 2) == 0; }
  const std::vector<Step> &getSteps() const { return mSteps; }
  std::vector<Step> &getStepsMutable() { return mSteps; }
  void setSteps(const std::vector<Step> &steps) { mSteps = steps; }

  void setSwing(float swing) { mSwing = swing; }
  void setPlaybackDirection(int direction) { mDirection = direction; }
  void setIsRandomOrder(bool isRandom) { mIsRandom = isRandom; }
  int getPlaybackDirection() const { return mDirection; }
  bool getIsRandomOrder() const { return mIsRandom; }
  void setIsJumpMode(bool isJump) { mIsJumpMode = isJump; }

  void setParameterLock(int stepIndex, int parameterId, float value) {
    if (stepIndex >= 0 && stepIndex < 64) {
      mSteps[stepIndex].parameterLocks[parameterId] = value;
    }
  }

  void clearParameterLocks(int stepIndex) {
    if (stepIndex >= 0 && stepIndex < 64) {
      mSteps[stepIndex].parameterLocks.clear();
    }
  }

  void jumpToStep(int stepIndex) {
    mNextStep = stepIndex % mLoopLength;
  }

  void clear() {
    for (auto &step : mSteps) {
      step.active = false;
      step.notes.clear();
      step.parameterLocks.clear();
      step.ratchet = 1;
      step.punch = false;
      step.isSkipped = false;
    }
  }

private:
  std::vector<Step> mSteps;
  int mCurrentStep = 0;
  int mNextStep = 0;
  int mNumPages = 1;
  int mStepsPerPage = 16;
  int mLoopLength = 16;

  float mSwing = 0.0f;
  int mDirection = 0; // 0: Forward, 1: Backward, 2: Ping-Pong
  bool mIsRandom = false;
  bool mIsJumpMode = false;
  bool mPingPongForward = true;
  bool mNeedsInitialTrigger = true;
};

#endif // SEQUENCER_H
