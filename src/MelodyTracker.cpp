#include "MelodyTracker.h"
#include "WavFileUtils.h"
#include <cstring>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 40 Scale interval definitions matching Loom Pi's scale table
static const int kMelodyScaleIntervals[40][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},   // 0  Chromatic
    {0,2,4,5,7,9,11,-1,-1,-1,-1,-1}, // 1  Major
    {0,2,3,5,7,8,10,-1,-1,-1,-1,-1}, // 2  Natural Minor
    {0,2,3,5,7,8,11,-1,-1,-1,-1,-1}, // 3  Harmonic Minor
    {0,2,3,5,7,9,11,-1,-1,-1,-1,-1}, // 4  Melodic Minor
    {0,2,3,5,7,9,10,-1,-1,-1,-1,-1}, // 5  Dorian
    {0,1,3,5,7,8,10,-1,-1,-1,-1,-1}, // 6  Phrygian
    {0,2,4,6,7,9,11,-1,-1,-1,-1,-1}, // 7  Lydian
    {0,2,4,5,7,9,10,-1,-1,-1,-1,-1}, // 8  Mixolydian
    {0,1,3,5,6,8,10,-1,-1,-1,-1,-1}, // 9  Locrian
    {0,1,4,5,7,8,10,-1,-1,-1,-1,-1}, // 10 Phrygian Dom
    {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}, // 11 Lydian Dom
    {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1},// 12 Pentatonic Maj
    {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 13 Pentatonic Min
    {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1},// 14 Blues
    {0,2,3,4,7,9,-1,-1,-1,-1,-1,-1}, // 15 Blues Major
    {0,2,4,5,7,8,9,11,-1,-1,-1,-1},  // 16 Bebop Major
    {0,2,4,5,7,9,10,11,-1,-1,-1,-1}, // 17 Bebop Dominant
    {0,2,3,5,7,8,9,10,-1,-1,-1,-1},  // 18 Bebop Minor
    {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1},// 19 Whole Tone
    {0,1,3,4,6,7,9,10,-1,-1,-1,-1},  // 20 Diminished HW
    {0,2,3,5,6,8,9,11,-1,-1,-1,-1},  // 21 Diminished WH
    {0,3,4,7,8,11,-1,-1,-1,-1,-1,-1},// 22 Augmented
    {0,1,4,5,7,8,11,-1,-1,-1,-1,-1}, // 23 Double Harmonic
    {0,2,3,6,7,8,11,-1,-1,-1,-1,-1}, // 24 Hungarian Minor
    {0,1,3,5,7,9,11,-1,-1,-1,-1,-1}, // 25 Neapolitan Maj
    {0,1,3,5,7,8,11,-1,-1,-1,-1,-1}, // 26 Neapolitan Min
    {0,1,4,5,6,8,11,-1,-1,-1,-1,-1}, // 27 Persian
    {0,2,4,5,6,8,10,-1,-1,-1,-1,-1}, // 28 Arabian
    {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1},// 29 Hirajoshi
    {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 30 In-Sen
    {0,2,5,7,9,-1,-1,-1,-1,-1,-1,-1},// 31 Yo
    {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1},// 32 Iwato
    {0,4,6,7,11,-1,-1,-1,-1,-1,-1,-1},// 33 Chinese
    {0,2,5,7,10,-1,-1,-1,-1,-1,-1,-1},// 34 Egyptian
    {0,2,4,6,9,10,-1,-1,-1,-1,-1,-1},// 35 Prometheus
    {0,1,4,6,7,10,-1,-1,-1,-1,-1,-1},// 36 Tritone
    {0,1,4,6,8,10,11,-1,-1,-1,-1,-1},// 37 Enigmatic
    {0,1,3,4,6,8,10,-1,-1,-1,-1,-1}, // 38 Super Locrian
    {0,2,4,6,7,9,10,-1,-1,-1,-1,-1}  // 39 Acoustic
};

MelodyTracker::MelodyTracker() {
    mAudioRingBuffer.resize(RING_BUFFER_SIZE, 0.0f);
    mYinBuffer.resize(YIN_WINDOW_SIZE, 0.0f);
    mYinDiff.resize(YIN_WINDOW_SIZE / 2, 0.0f);
    setupBandpassFilters();
}

MelodyTracker::~MelodyTracker() {}

void MelodyTracker::setSampleRate(float sr) {
    if (sr > 8000.0f && sr < 192000.0f) {
        mSampleRate = sr;
        mMinNoteDurationSamples = mSampleRate * (mTimeSensitivityMs * 0.001);
        setupBandpassFilters();
    }
}

void MelodyTracker::setTempoAndSteps(float bpm, int totalSteps, float clockMultiplier) {
    mBpm = std::max(20.0f, std::min(300.0f, bpm));
    mTotalSteps = std::max(1, std::min(64, totalSteps));
    mClockMultiplier = std::max(0.01f, clockMultiplier);
}

void MelodyTracker::setNoiseGateDb(float dbThreshold) {
    mGateThresholdDb = std::max(-80.0f, std::min(-6.0f, dbThreshold));
}

void MelodyTracker::setInputGainDb(float gainDb) {
    mInputGainDb = std::max(0.0f, std::min(36.0f, gainDb));
    mInputGainLin = powf(10.0f, mInputGainDb / 20.0f);
}

void MelodyTracker::setHighPassCutoff(float hz) {
    mHpCutoffHz = std::max(20.0f, std::min(500.0f, hz));
    setupBandpassFilters();
}

void MelodyTracker::setLowPassCutoff(float hz) {
    mLpCutoffHz = std::max(500.0f, std::min(12000.0f, hz));
    setupBandpassFilters();
}

void MelodyTracker::setConfidenceThreshold(float thresh) {
    mConfidenceThreshold = std::max(0.10f, std::min(0.70f, thresh));
}

void MelodyTracker::setTimeSensitivityMs(float ms) {
    mTimeSensitivityMs = std::max(5.0f, std::min(150.0f, ms));
    mMinNoteDurationSamples = mSampleRate * (mTimeSensitivityMs * 0.001);
}

void MelodyTracker::setPitchTolerance(float semitones) {
    mPitchToleranceSemitones = std::max(0.15f, std::min(2.0f, semitones));
}

void MelodyTracker::setScaleFilter(int rootNote, int scaleIdx) {
    mRootNote = std::max(0, std::min(11, rootNote));
    mScaleIdx = std::max(0, std::min(39, scaleIdx));
}

void MelodyTracker::setupBandpassFilters() {
    // 2nd-order Butterworth High-Pass at mHpCutoffHz (filters out mains hum, rumble, low noise)
    // and 2nd-order Butterworth Low-Pass at mLpCutoffHz (filters out sibilance, hiss, harmonics)
    auto computeHighpass = [this](float fc, Biquad& bq) {
        float omega = 2.0f * (float)M_PI * fc / mSampleRate;
        float cosOmega = cosf(omega);
        float sinOmega = sinf(omega);
        float alpha = sinOmega / (2.0f * 0.7071f);

        float a0 = 1.0f + alpha;
        bq.b0 = ((1.0f + cosOmega) / 2.0f) / a0;
        bq.b1 = (-(1.0f + cosOmega)) / a0;
        bq.b2 = ((1.0f + cosOmega) / 2.0f) / a0;
        bq.a1 = (-2.0f * cosOmega) / a0;
        bq.a2 = (1.0f - alpha) / a0;
        bq.reset();
    };

    auto computeLowpass = [this](float fc, Biquad& bq) {
        float omega = 2.0f * (float)M_PI * fc / mSampleRate;
        float cosOmega = cosf(omega);
        float sinOmega = sinf(omega);
        float alpha = sinOmega / (2.0f * 0.7071f);

        float a0 = 1.0f + alpha;
        bq.b0 = ((1.0f - cosOmega) / 2.0f) / a0;
        bq.b1 = (1.0f - cosOmega) / a0;
        bq.b2 = ((1.0f - cosOmega) / 2.0f) / a0;
        bq.a1 = (-2.0f * cosOmega) / a0;
        bq.a2 = (1.0f - alpha) / a0;
        bq.reset();
    };

    float hp = std::max(20.0f, std::min(mSampleRate * 0.45f, mHpCutoffHz));
    float lp = std::max(hp + 50.0f, std::min(mSampleRate * 0.45f, mLpCutoffHz));

    computeHighpass(hp, mHpFilter1);
    computeHighpass(hp, mHpFilter2);
    computeLowpass(lp, mLpFilter1);
    computeLowpass(lp, mLpFilter2);
}

float MelodyTracker::processFiltering(float in) {
    float s = mHpFilter1.process(in);
    s = mHpFilter2.process(s);
    s = mLpFilter1.process(s);
    s = mLpFilter2.process(s);
    return s;
}

void MelodyTracker::reset() {
    mIsRecording = false;
    mIsCountIn = false;
    mIsFinished = false;
    mSamplesRecorded = 0.0;
    mTotalRecordSamples = 0.0;
    mCountInSamples = 0.0;
    mCountInElapsed = 0.0;
    mCountInBeatsLeft = 4;
    mRingWritePtr = 0;
    mRingReadPtr = 0;
    mNoteIsActive = false;
    mCurrentRmsDb = -96.0f;
    mCurrentPitchHz = 0.0f;
    mCurrentMidiNote = 0;
    {
        std::lock_guard<std::mutex> lock(mNotesMutex);
        mCompletedNotes.clear();
    }
    {
        std::lock_guard<std::mutex> lock(mSampleMutex);
        mSamplePlaybackPos = 0;
    }
}

void MelodyTracker::startRecording(int countInBars, int recordBars) {
    reset();
    mCountInBars = std::max(0, std::min(4, countInBars));
    mRecordBars = std::max(1, std::min(8, recordBars));

    // Samples per beat (quarter note) = (60 / BPM) * SampleRate
    double samplesPerBeat = (60.0 / (double)mBpm) * (double)mSampleRate;
    
    // Loom Pi sequences are 16 steps per bar (4 beats per bar)
    double samplesPerBar = samplesPerBeat * 4.0;

    mCountInSamples = (double)mCountInBars * samplesPerBar;
    mTotalRecordSamples = (double)mRecordBars * samplesPerBar;

    if (mCountInBars > 0) {
        mIsCountIn = true;
        mCountInBeatsLeft = mCountInBars * 4;
    } else {
        mIsCountIn = false;
    }
    mIsRecording = true;
}

void MelodyTracker::stopRecording() {
    if (mIsRecording.load()) {
        mIsRecording = false;
        mIsCountIn = false;
        mIsFinished = true;

        // End currently ringing note if any
        if (mNoteIsActive) {
            mCurrentCandidate.endSample = mSamplesRecorded;
            if ((mCurrentCandidate.endSample - mCurrentCandidate.startSample) >= mMinNoteDurationSamples) {
                std::lock_guard<std::mutex> lock(mNotesMutex);
                mCompletedNotes.push_back(mCurrentCandidate);
            }
            mNoteIsActive = false;
        }
    }
}

float MelodyTracker::getRecordingProgress() const {
    if (!mIsRecording.load() && !mIsFinished.load()) return 0.0f;
    if (mTotalRecordSamples <= 0.0) return 0.0f;
    if (mIsCountIn.load()) return 0.0f;
    float p = (float)(mSamplesRecorded / mTotalRecordSamples);
    return std::max(0.0f, std::min(1.0f, p));
}

int MelodyTracker::getCurrentRecordingStep() const {
    if (mTotalRecordSamples <= 0.0 || mTotalSteps <= 0) return 0;
    if (mIsCountIn.load()) return 0;
    int step = (int)((mSamplesRecorded / mTotalRecordSamples) * mTotalSteps);
    return std::max(0, std::min(mTotalSteps - 1, step));
}

void MelodyTracker::pushAudio(const float* buffer, int numFrames) {
    if (!buffer || numFrames <= 0) return;

    float gain = mInputGainLin;

    for (int i = 0; i < numFrames; ++i) {
        float raw = buffer[i] * gain;
        // Soft-clip to prevent runaway feedback if gain is cranked high
        if (raw > 2.0f) raw = 2.0f;
        else if (raw < -2.0f) raw = -2.0f;

        float filtered = processFiltering(raw);

        // Store filtered audio in ring buffer
        uint32_t w = mRingWritePtr.load(std::memory_order_relaxed);
        mAudioRingBuffer[w % RING_BUFFER_SIZE] = filtered;
        mRingWritePtr.store(w + 1, std::memory_order_release);

        // Transport handling
        if (mIsRecording.load()) {
            if (mIsCountIn.load()) {
                mCountInElapsed += 1.0;
                double samplesPerBeat = (60.0 / (double)mBpm) * (double)mSampleRate;
                int beatsLeft = (int)ceil((mCountInSamples - mCountInElapsed) / samplesPerBeat);
                mCountInBeatsLeft = std::max(0, beatsLeft);

                if (mCountInElapsed >= mCountInSamples) {
                    mIsCountIn = false;
                    mSamplesRecorded = 0.0;
                }
            } else {
                mSamplesRecorded += 1.0;
                if (mSamplesRecorded >= mTotalRecordSamples) {
                    stopRecording();
                }
            }
        }
    }

    // Process available analysis blocks
    processAudioBlock();
}

float MelodyTracker::frequencyToMidi(float freqHz) {
    if (freqHz <= 8.0f) return 0.0f;
    return 69.0f + 12.0f * log2f(freqHz / 440.0f);
}

float MelodyTracker::midiToFrequency(int midiNote) {
    return 440.0f * powf(2.0f, (float)(midiNote - 69) / 12.0f);
}

int MelodyTracker::quantizeToScale(int rawMidi, int rootNote, int scaleIdx) {
    if (scaleIdx == 0) { // Chromatic
        return rawMidi;
    }
    if (scaleIdx < 0 || scaleIdx >= 40) return rawMidi;

    int noteInOctave = (rawMidi % 12 + 12) % 12;
    int octave = rawMidi / 12;

    int root = (rootNote % 12 + 12) % 12;
    int relativePitch = (noteInOctave - root + 12) % 12;

    const int* intervals = kMelodyScaleIntervals[scaleIdx];
    int bestInterval = intervals[0];
    int minDistance = 999;

    for (int i = 0; i < 12; ++i) {
        int interval = intervals[i];
        if (interval == -1) break;
        int dist = std::abs(relativePitch - interval);
        if (dist < minDistance) {
            minDistance = dist;
            bestInterval = interval;
        }
    }

    int quantizedPitchInOctave = (root + bestInterval) % 12;
    int result = octave * 12 + quantizedPitchInOctave;
    return std::max(0, std::min(127, result));
}

// YIN Pitch Detection Algorithm
float MelodyTracker::detectPitchYin(const float* buffer, int windowSize) {
    int halfWindow = windowSize / 2;
    if (halfWindow <= 0) return 0.0f;

    // Step 1: Difference Function
    mYinDiff[0] = 1.0f;
    for (int tau = 1; tau < halfWindow; ++tau) {
        float diffSum = 0.0f;
        for (int j = 0; j < halfWindow; ++j) {
            float delta = buffer[j] - buffer[j + tau];
            diffSum += delta * delta;
        }
        mYinDiff[tau] = diffSum;
    }

    // Step 2: Cumulative Mean Normalized Difference Function
    float runningSum = 0.0f;
    mYinDiff[0] = 1.0f;
    for (int tau = 1; tau < halfWindow; ++tau) {
        runningSum += mYinDiff[tau];
        if (runningSum > 0.00001f) {
            mYinDiff[tau] *= (float)tau / runningSum;
        } else {
            mYinDiff[tau] = 1.0f;
        }
    }

    // Step 3: Absolute Thresholding
    // YIN threshold is dynamically governed by mConfidenceThreshold (default ~0.35f)
    float yinThreshold = mConfidenceThreshold;
    int tauEstimate = -1;
    for (int tau = 2; tau < halfWindow; ++tau) {
        if (mYinDiff[tau] < yinThreshold) {
            while (tau + 1 < halfWindow && mYinDiff[tau + 1] < mYinDiff[tau]) {
                tau++;
            }
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == -1) {
        // Fallback: global minimum
        float minVal = 999.0f;
        for (int tau = 2; tau < halfWindow; ++tau) {
            if (mYinDiff[tau] < minVal) {
                minVal = mYinDiff[tau];
                tauEstimate = tau;
            }
        }
        // Fallback tolerance scales with confidence threshold
        float maxAllowedMin = std::min(0.65f, yinThreshold * 1.6f);
        if (minVal > maxAllowedMin) {
            return 0.0f; // Not voiced / unpitched noise
        }
    }

    // Step 4: Parabolic Interpolation for fine sub-sample accuracy
    float betterTau = (float)tauEstimate;
    if (tauEstimate > 0 && tauEstimate < halfWindow - 1) {
        float s0 = mYinDiff[tauEstimate - 1];
        float s1 = mYinDiff[tauEstimate];
        float s2 = mYinDiff[tauEstimate + 1];
        float denom = (s0 - 2.0f * s1 + s2);
        if (std::abs(denom) > 0.00001f) {
            betterTau += (s0 - s2) / (2.0f * denom);
        }
    }

    if (betterTau <= 0.0f) return 0.0f;
    float detectedFreq = mSampleRate / betterTau;

    // Vocal/Melody sanity filter: clamp to [mHpCutoffHz * 0.85, mLpCutoffHz * 1.15]
    float minHz = std::max(20.0f, mHpCutoffHz * 0.85f);
    float maxHz = std::min(mSampleRate * 0.45f, mLpCutoffHz * 1.15f);
    if (detectedFreq < minHz || detectedFreq > maxHz) {
        return 0.0f;
    }

    return detectedFreq;
}

void MelodyTracker::processAudioBlock() {
    uint32_t w = mRingWritePtr.load(std::memory_order_acquire);

    // Process in hops of YIN_HOP_SIZE
    while ((int32_t)(w - mRingReadPtr) >= YIN_WINDOW_SIZE) {
        // Extract window into YIN buffer and compute RMS
        float sumSquare = 0.0f;
        for (int i = 0; i < YIN_WINDOW_SIZE; ++i) {
            float s = mAudioRingBuffer[(mRingReadPtr + i) % RING_BUFFER_SIZE];
            mYinBuffer[i] = s;
            sumSquare += s * s;
        }

        float rms = sqrtf(sumSquare / (float)YIN_WINDOW_SIZE);
        float rmsDb = (rms > 0.00001f) ? (20.0f * log10f(rms)) : -96.0f;
        mCurrentRmsDb = rmsDb;

        // Pitch detection if energy exceeds noise gate (with 3dB hysteresis)
        float gateOn = mGateThresholdDb;
        float gateOff = mGateThresholdDb - 4.0f;

        bool hasSignal = mNoteIsActive ? (rmsDb > gateOff) : (rmsDb > gateOn);
        float pitchHz = 0.0f;
        int midiNote = 0;
        float rawMidiPitch = 0.0f;

        if (hasSignal) {
            pitchHz = detectPitchYin(mYinBuffer.data(), YIN_WINDOW_SIZE);
            if (pitchHz > 0.0f) {
                rawMidiPitch = frequencyToMidi(pitchHz);
                int roundedMidi = (int)roundf(rawMidiPitch);
                if (mScaleFilterEnabled) {
                    roundedMidi = quantizeToScale(roundedMidi, mRootNote, mScaleIdx);
                }
                midiNote = roundedMidi;
            }
        }

        mCurrentPitchHz = pitchHz;
        mCurrentMidiNote = midiNote;

        // Note Segmentation state machine
        if (mIsRecording.load() && !mIsCountIn.load()) {
            double currentSample = mSamplesRecorded;

            if (midiNote > 0 && pitchHz > 0.0f) {
                if (!mNoteIsActive) {
                    // Start new note candidate
                    mNoteIsActive = true;
                    mCurrentCandidate.startSample = currentSample;
                    mCurrentCandidate.endSample = currentSample + YIN_HOP_SIZE;
                    mCurrentCandidate.pitchAccum = (float)midiNote;
                    mCurrentCandidate.pitchCount = 1;
                    mCurrentCandidate.peakRms = rms;
                    mCurrentCandidate.stableNote = midiNote;
                } else {
                    // Check if pitch shifted beyond pitch tolerance (default ~0.70 semitones)
                    // or if an energy attack occurred (re-triggering the same note)
                    float currentAvgNote = mCurrentCandidate.pitchAccum / (float)mCurrentCandidate.pitchCount;
                    bool pitchChanged = (std::abs(rawMidiPitch - currentAvgNote) >= mPitchToleranceSemitones);
                    bool reAttack = (!pitchChanged && rms > mCurrentCandidate.peakRms * 1.8f && 
                                     (currentSample - mCurrentCandidate.startSample) >= mMinNoteDurationSamples);

                    if (pitchChanged || reAttack) {
                        // Conclude previous note and start new note
                        mCurrentCandidate.endSample = currentSample;
                        if ((mCurrentCandidate.endSample - mCurrentCandidate.startSample) >= mMinNoteDurationSamples) {
                            std::lock_guard<std::mutex> lock(mNotesMutex);
                            mCompletedNotes.push_back(mCurrentCandidate);
                        }

                        mCurrentCandidate.startSample = currentSample;
                        mCurrentCandidate.endSample = currentSample + YIN_HOP_SIZE;
                        mCurrentCandidate.pitchAccum = (float)midiNote;
                        mCurrentCandidate.pitchCount = 1;
                        mCurrentCandidate.peakRms = rms;
                        mCurrentCandidate.stableNote = midiNote;
                    } else {
                        // Extend existing note
                        mCurrentCandidate.endSample = currentSample + YIN_HOP_SIZE;
                        mCurrentCandidate.pitchAccum += (float)midiNote;
                        mCurrentCandidate.pitchCount++;
                        if (rms > mCurrentCandidate.peakRms) {
                            mCurrentCandidate.peakRms = rms;
                        }
                    }
                }
            } else {
                // Silence or unpitched: conclude active note
                if (mNoteIsActive) {
                    mCurrentCandidate.endSample = currentSample;
                    if ((mCurrentCandidate.endSample - mCurrentCandidate.startSample) >= mMinNoteDurationSamples) {
                        std::lock_guard<std::mutex> lock(mNotesMutex);
                        mCompletedNotes.push_back(mCurrentCandidate);
                    }
                    mNoteIsActive = false;
                }
            }
        }

        mRingReadPtr += YIN_HOP_SIZE;
    }
}

std::vector<TranscribedNote> MelodyTracker::getTranscribedNotes() {
    std::lock_guard<std::mutex> lock(mNotesMutex);
    std::vector<TranscribedNote> results;

    if (mTotalRecordSamples <= 0.0 || mTotalSteps <= 0) {
        return results;
    }

    double samplesPerStep = mTotalRecordSamples / (double)mTotalSteps;
    if (samplesPerStep <= 0.0) return results;

    for (const auto& raw : mCompletedNotes) {
        double durationSamples = raw.endSample - raw.startSample;
        if (durationSamples < mMinNoteDurationSamples) continue;

        double stepExact = raw.startSample / samplesPerStep;
        int stepIdx = (int)floor(stepExact);
        if (stepIdx < 0 || stepIdx >= mTotalSteps) continue;

        float subStep = (float)(stepExact - (double)stepIdx);
        float gate = (float)(durationSamples / samplesPerStep);
        if (gate < 0.2f) gate = 0.2f;

        // Velocity mapping from peak RMS (linear to 0.4 - 1.0)
        float vel = std::max(0.4f, std::min(1.0f, raw.peakRms * 3.5f + 0.3f));

        int finalNote = raw.stableNote;
        if (raw.pitchCount > 0) {
            finalNote = (int)roundf(raw.pitchAccum / (float)raw.pitchCount);
        }
        if (mScaleFilterEnabled) {
            finalNote = quantizeToScale(finalNote, mRootNote, mScaleIdx);
        }

        TranscribedNote tn;
        tn.startStep = stepIdx;
        tn.durationSteps = gate;
        tn.midiNote = finalNote;
        tn.velocity = vel;
        tn.subStepOffset = subStep;
        results.push_back(tn);
    }

    // Include the currently ringing note during live recording
    if (mNoteIsActive && (mSamplesRecorded - mCurrentCandidate.startSample) >= mMinNoteDurationSamples) {
        double durationSamples = mSamplesRecorded - mCurrentCandidate.startSample;
        double stepExact = mCurrentCandidate.startSample / samplesPerStep;
        int stepIdx = (int)floor(stepExact);
        if (stepIdx >= 0 && stepIdx < mTotalSteps) {
            float subStep = (float)(stepExact - (double)stepIdx);
            float gate = (float)(durationSamples / samplesPerStep);
            if (gate < 0.2f) gate = 0.2f;

            float vel = std::max(0.4f, std::min(1.0f, mCurrentCandidate.peakRms * 3.5f + 0.3f));
            int finalNote = mCurrentCandidate.stableNote;
            if (mCurrentCandidate.pitchCount > 0) {
                finalNote = (int)roundf(mCurrentCandidate.pitchAccum / (float)mCurrentCandidate.pitchCount);
            }
            if (mScaleFilterEnabled) {
                finalNote = quantizeToScale(finalNote, mRootNote, mScaleIdx);
            }

            TranscribedNote tn;
            tn.startStep = stepIdx;
            tn.durationSteps = gate;
            tn.midiNote = finalNote;
            tn.velocity = vel;
            tn.subStepOffset = subStep;
            results.push_back(tn);
        }
    }

    return results;
}

bool MelodyTracker::loadSample(const std::string& path) {
    std::vector<float> data;
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> slicePoints;

    if (!WavFileUtils::loadWav(path, data, sampleRate, channels, slicePoints)) {
        std::cerr << "[MelodyTracker] Failed to load WAV sample: " << path << std::endl;
        return false;
    }

    if (data.empty()) return false;

    // Convert to mono
    std::vector<float> mono;
    if (channels == 2) {
        int numFrames = (int)data.size() / 2;
        mono.resize(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            mono[i] = (data[i * 2] + data[i * 2 + 1]) * 0.5f;
        }
    } else {
        mono = std::move(data);
    }

    // Resample to mSampleRate if different
    if (sampleRate > 0 && std::abs((float)sampleRate - mSampleRate) > 10.0f) {
        float ratio = (float)sampleRate / mSampleRate;
        size_t newLength = (size_t)((float)mono.size() / ratio);
        std::vector<float> resampled(newLength);
        for (size_t i = 0; i < newLength; ++i) {
            float srcPos = (float)i * ratio;
            size_t idx0 = (size_t)srcPos;
            size_t idx1 = std::min(idx0 + 1, mono.size() - 1);
            float frac = srcPos - (float)idx0;
            resampled[i] = mono[idx0] * (1.0f - frac) + mono[idx1] * frac;
        }
        mono = std::move(resampled);
    }

    {
        std::lock_guard<std::mutex> lock(mSampleMutex);
        mSampleBuffer = std::move(mono);
        mSamplePlaybackPos = 0;
        mSampleFullPath = path;

        size_t slash = path.find_last_of("/\\");
        mSampleFilename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    }

    std::cout << "[MelodyTracker] Loaded sample: " << mSampleFilename 
              << " (" << mSampleBuffer.size() << " frames, " 
              << (float)mSampleBuffer.size() / mSampleRate << "s)" << std::endl;
    return true;
}

void MelodyTracker::clearSample() {
    std::lock_guard<std::mutex> lock(mSampleMutex);
    mSampleBuffer.clear();
    mSamplePlaybackPos = 0;
    mSampleFilename.clear();
    mSampleFullPath.clear();
}

bool MelodyTracker::hasSample() const {
    std::lock_guard<std::mutex> lock(mSampleMutex);
    return !mSampleBuffer.empty();
}

std::string MelodyTracker::getSampleFilename() const {
    std::lock_guard<std::mutex> lock(mSampleMutex);
    return mSampleFilename;
}

void MelodyTracker::processSamplePlayback(float* interleavedOut, int numFrames) {
    if (!mIsSampleMode || !mIsRecording.load() || mIsCountIn.load() || numFrames <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mSampleMutex);
    if (mSampleBuffer.empty()) return;

    constexpr int MAX_CHUNK = 1024;
    float chunk[MAX_CHUNK];
    int framesDone = 0;

    while (framesDone < numFrames) {
        int framesToDo = std::min(MAX_CHUNK, numFrames - framesDone);
        for (int i = 0; i < framesToDo; ++i) {
            float s = 0.0f;
            if (mSamplePlaybackPos < mSampleBuffer.size()) {
                s = mSampleBuffer[mSamplePlaybackPos++];
            }
            chunk[i] = s;

            // Audition / play sample through audio output
            if (interleavedOut) {
                int outIdx = (framesDone + i) * 2;
                interleavedOut[outIdx] += s * 0.85f;
                interleavedOut[outIdx + 1] += s * 0.85f;
            }
        }

        // Feed to pitch detection & note transcription
        pushAudio(chunk, framesToDo);
        framesDone += framesToDo;

        if (mSamplePlaybackPos >= mSampleBuffer.size()) {
            stopRecording();
            break;
        }
    }
}
