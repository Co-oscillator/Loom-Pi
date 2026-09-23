#ifndef MELODY_TRACKER_H
#define MELODY_TRACKER_H

#include <vector>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <string>

// Transcribed Note structure ready for Sequencer steps
struct TranscribedNote {
    int startStep = 0;       // Quantized step (0 to 63)
    float durationSteps = 1.0f; // Gate length in steps (e.g. 1.0 = full step)
    int midiNote = 60;       // 0 to 127
    float velocity = 0.8f;   // 0.0 to 1.0
    float subStepOffset = 0.0f; // Microtiming (0.0 to 1.0 within start step)
};

class MelodyTracker {
public:
    MelodyTracker();
    ~MelodyTracker();

    // Configuration
    void setSampleRate(float sr);
    void setTempoAndSteps(float bpm, int totalSteps, float clockMultiplier = 1.0f);
    void setNoiseGateDb(float dbThreshold); // e.g. -36.0 dB
    void setScaleFilter(int rootNote, int scaleIdx); // root: 0=C..11=B; scale: 0=Chromatic, 1=Major, 2=Minor, etc.
    void setScaleFilterEnabled(bool enabled) { mScaleFilterEnabled = enabled; }

    // Live State & Audio Ingestion (called continuously from audio thread or buffer feeder)
    void pushAudio(const float* buffer, int numFrames);
    void reset();

    // Recording transport control
    void startRecording(int countInBars = 1, int recordBars = 1);
    void stopRecording();
    bool isRecording() const { return mIsRecording.load(); }
    bool isCountIn() const { return mIsCountIn.load(); }
    bool isFinished() const { return mIsFinished.load(); }

    // Real-time querying for UI
    float getLiveVuLevel() const { return mCurrentRmsDb.load(); } // in dB
    float getLiveDetectedPitchHz() const { return mCurrentPitchHz.load(); }
    int   getLiveDetectedMidi() const { return mCurrentMidiNote.load(); }
    float getRecordingProgress() const; // 0.0 to 1.0
    int   getCurrentRecordingStep() const; // 0 to totalSteps - 1
    int   getCountInBeatsLeft() const { return mCountInBeatsLeft.load(); }

    // Results
    std::vector<TranscribedNote> getTranscribedNotes();

    // Static helper for pitch to MIDI
    static float frequencyToMidi(float freqHz);
    static float midiToFrequency(int midiNote);
    static int quantizeToScale(int rawMidi, int rootNote, int scaleIdx);

private:
    // Butterworth 2nd-order Biquad Bandpass filter (HP ~65Hz, LP ~2200Hz)
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        void reset() { x1 = x2 = y1 = y2 = 0.0f; }
        float process(float in) {
            float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in;
            y2 = y1; y1 = out;
            return out;
        }
    };

    void setupBandpassFilters();
    float processFiltering(float in);

    // YIN Pitch Detection on a window
    float detectPitchYin(const float* buffer, int windowSize);

    // Segmentation & Analysis step
    void processAudioBlock();

    float mSampleRate = 48000.0f;
    float mBpm = 120.0f;
    int mTotalSteps = 16;
    float mClockMultiplier = 1.0f;
    float mGateThresholdDb = -36.0f;
    bool mScaleFilterEnabled = true;
    int mRootNote = 0;
    int mScaleIdx = 0;

    // Filters
    Biquad mHpFilter1, mHpFilter2; // 65Hz High-Pass
    Biquad mLpFilter1, mLpFilter2; // 2200Hz Low-Pass

    // Ring Buffer for input frames
    static constexpr int RING_BUFFER_SIZE = 131072; // ~2.7s buffer at 48k
    std::vector<float> mAudioRingBuffer;
    std::atomic<uint32_t> mRingWritePtr{0};
    uint32_t mRingReadPtr = 0;

    // YIN Analysis parameters
    static constexpr int YIN_WINDOW_SIZE = 2048; // ~42ms at 48kHz
    static constexpr int YIN_HOP_SIZE = 256;      // ~5.3ms hop size
    std::vector<float> mYinBuffer;
    std::vector<float> mYinDiff;

    // Transport & Timing
    std::atomic<bool> mIsRecording{false};
    std::atomic<bool> mIsCountIn{false};
    std::atomic<bool> mIsFinished{false};
    std::atomic<int>  mCountInBeatsLeft{4};
    int mCountInBars = 1;
    int mRecordBars = 1;
    double mSamplesRecorded = 0.0;
    double mTotalRecordSamples = 0.0;
    double mCountInSamples = 0.0;
    double mCountInElapsed = 0.0;

    // Live display metrics
    std::atomic<float> mCurrentRmsDb{-96.0f};
    std::atomic<float> mCurrentPitchHz{0.0f};
    std::atomic<int>   mCurrentMidiNote{0};

    // Note segmentation state machine
    struct RawNoteCandidate {
        double startSample = 0.0;
        double endSample = 0.0;
        float pitchAccum = 0.0f;
        int pitchCount = 0;
        float peakRms = 0.0f;
        int stableNote = 60;
    };

    bool mNoteIsActive = false;
    RawNoteCandidate mCurrentCandidate;
    std::vector<RawNoteCandidate> mCompletedNotes;
    std::mutex mNotesMutex;

    // Minimum note duration to avoid transient noise clicks (~40ms)
    double mMinNoteDurationSamples = 1920.0; 
};

#endif // MELODY_TRACKER_H
