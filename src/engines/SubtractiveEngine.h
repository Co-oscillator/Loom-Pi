#ifndef SUBTRACTIVE_ENGINE_H
#define SUBTRACTIVE_ENGINE_H

#include "../Utils.h"
#include "Adsr.h"
#include "Oscillator.h"

#include <cmath>
#include <memory>

#include <vector>

class SubtractiveEngine {
public:
  struct Voice {
    bool active = false;
    bool isNoteHeld = false;
    int note = -1;
    float frequency = 440.0f;
    float targetFrequency = 440.0f;
    float amplitude = 1.0f;
    Adsr ampEnv;
    Adsr filterEnv;
    std::vector<Oscillator> oscillators;

    // SVF state
    TSvf svf;
    float currentFilterEnvVal = 0.0f;
    uint32_t controlCounter = 0;
    float ampEnvStart = 0.0f, ampEnvDelta = 0.0f;
    float filterEnvStart = 0.0f, filterEnvDelta = 0.0f;

    Voice() { oscillators.resize(4); }

    void reset() {
      active = false;
      isNoteHeld = false;
      note = -1;
      frequency = 440.0f;
      targetFrequency = 440.0f;
      ampEnv.reset();
      filterEnv.reset();
      svf.setParams(1000.0f, 0.7f, 44100.0f);
    }
  };

  SubtractiveEngine() {
    mVoices.resize(16);
    for (auto &v : mVoices)
      v.reset();
    mOscVolumes.assign(4, 0.0f);
    mOscVolumes[0] = 0.6f;
    mOscVolumes[1] = 0.4f;
    mOscWaveValues.assign(4, 0.0f);
    mOscWaveValues[0] = 0.666666f;
    mOscWaveValues[1] = 1.0f;
    resetToDefaults();
  }

  void resetToDefaults() {
    mCutoff = 1.0f;
    mResonance = 0.0f;
    mAttack = 0.01f;
    mDecay = 0.1f;
    mSustain = 1.0f;
    mRelease = 0.5f;
    mF_Atk = 0.01f;
    mF_Dcy = 0.1f;
    mF_Sus = 0.0f;
    mF_Rel = 0.5f;
    mF_Amt = 0.0f;
    mDetune = 0.0f;
    mNoiseLevel = 0.0f;
    mOscSync = false;
    mRingMod = false;
    mFilterMode = 0;
    mOscPitch[0] = 1.0f;
    mOscPitch[1] = 1.0f;
    mOscPitch[2] = 0.5f;
    mOscPitch[3] = 1.0f;
    mOscVolumes[0] = 0.6f;
    mOscVolumes[1] = 0.4f;
    mOscVolumes[2] = 0.4f; // Default Sub Volume
    mOscVolumes[3] = 0.0f;
    mOscDrive[0] = mOscDrive[1] = mOscDrive[2] = mOscDrive[3] = 1.0f;
    mOscFold[0] = mOscFold[1] = mOscFold[2] = mOscFold[3] = 0.0f;
    mOscPW[0] = mOscPW[1] = mOscPW[2] = mOscPW[3] = 0.5f;
    mOscWaveValues[0] = 0.666666f;
    mOscWaveValues[1] = 1.0f;
    mOscWaveValues[2] = 0.0f; // Explicitly set Sub to Sine
    mOscWaveValues[3] = 0.666666f;

    // Propagate waveforms to all voices immediately
    for (int i = 0; i < 4; ++i) {
      setOscWaveform(i, mOscWaveValues[i]);
    }

    updateLiveEnvelopes();
  }

  void setSampleRate(float sr) {
    mSampleRate = sr;
    for (auto &v : mVoices) {
      v.ampEnv.setSampleRate(sr);
      v.filterEnv.setSampleRate(sr);
    }
  }

  void setFrequency(float freq, float sampleRate) {
    mSampleRate = sampleRate;
    mFrequency = freq;
  }
  void setIgnoreNoteFrequency(bool ignore) { mIgnoreNoteFrequency = ignore; }

  void setGlide(float v) { mGlide = v; }
  void setPitchBend(float semitones) { mPitchBend = semitones; }

  void allNotesOff() {
    for (auto &v : mVoices) {
      v.active = false;
      v.isNoteHeld = false;
      v.ampEnv.reset();
      v.filterEnv.reset();
    }
  }

  void triggerNote(int note, int velocity) {
    int idx = -1;
    for (int i = 0; i < 16; ++i)
      if (!mVoices[i].active) {
        idx = i;
        break;
      }

    if (idx == -1) {
      // Favor stealing voices that are in RELEASE phase even if they have higher volume
      // than one in DECAY phase, to prevent cutting off new notes.
      float minScore = 10.0f;
      for (int i = 0; i < 16; ++i) {
        float vVol = mVoices[i].ampEnv.getValue();
        float score = vVol;
        if (!mVoices[i].isNoteHeld) score -= 1.0f; // Strongly favor stealing released notes
        
        if (score < minScore) {
          minScore = score;
          idx = i;
        }
      }
      if (idx == -1) idx = 0;
    }

    Voice &v = mVoices[idx];
    v.active = true;
    v.isNoteHeld = true;
    v.note = note;
    v.amplitude = velocity / 127.0f;
    v.controlCounter = 0;
    float baseFreq = mIgnoreNoteFrequency
                         ? mFrequency
                         : 440.0f * powf(2.0f, (note - 69) / 12.0f);

    v.targetFrequency = baseFreq;
    v.frequency = (mGlide > 0.001f) ? mLastFrequency : baseFreq;
    mLastFrequency = baseFreq;

    v.ampEnv.setSampleRate(mSampleRate);
    v.ampEnv.setParameters(mAttack, mDecay, mSustain, mRelease);
    v.filterEnv.setSampleRate(mSampleRate);
    v.filterEnv.setParameters(mF_Atk, mF_Dcy, mF_Sus, mF_Rel);

    v.ampEnv.trigger();
    v.filterEnv.trigger();
    v.svf.setParams(1000.0f, 0.7f, mSampleRate);

    for (int i = 0; i < 4; ++i) {
      v.oscillators[i].setFrequency(v.frequency, mSampleRate);
      v.oscillators[i].resetPhase();
    }
  }

  void releaseNote(int note) {
    for (auto &v : mVoices)
      if (v.active && v.note == note) {
        v.isNoteHeld = false;
        v.ampEnv.release();
        v.filterEnv.release();
      }
  }

  void setAttack(float v) {
    mAttack = v;
    updateLiveEnvelopes();
  }
  void setDecay(float v) {
    mDecay = v;
    updateLiveEnvelopes();
  }
  void setSustain(float v) {
    mSustain = v;
    updateLiveEnvelopes();
  }
  void setRelease(float v) {
    mRelease = v;
    updateLiveEnvelopes();
  }
  void setFilterAttack(float v) {
    mF_Atk = v;
    updateLiveEnvelopes();
  }
  void setFilterDecay(float v) {
    mF_Dcy = v;
    updateLiveEnvelopes();
  }
  void setFilterSustain(float v) {
    mF_Sus = v;
    updateLiveEnvelopes();
  }
  void setFilterRelease(float v) {
    mF_Rel = v;
    updateLiveEnvelopes();
  }

  void setCutoff(float cutoff) { mCutoff = cutoff; }
  void setResonance(float res) { mResonance = res; }
  void setFilterEnvAmount(float v) { mF_Amt = v * 2.0f - 1.0f; }
  void setDetune(float v) { mDetune = v; }
  void setNoiseLevel(float v) { mNoiseLevel = v; }
  void setOscVolume(int osc, float vol) {
    if (osc >= 0 && osc < 4)
      mOscVolumes[osc] = vol;
  }
  void setLfoRate(float rate) { mLfoRate = rate; }
  void setLfoDepth(float depth) { mLfoDepth = depth; }
  void setUseEnvelope(bool v) { mUseEnvelope = v; }

  void setParameter(int id, float value) {
    if (id == 112)
      setCutoff(value);
    else if (id == 113)
      setResonance(value);
    else if (id == 100)
      setAttack(value);
    else if (id == 101)
      setDecay(value);
    else if (id == 102)
      setSustain(value);
    else if (id == 103)
      setRelease(value);
    else if (id == 150)
      mOscSync = (value > 0.5f);
    else if (id == 151)
      mRingMod = (value > 0.5f);
    else if (id == 152)
      mFmAmt = value;
    else if (id == 153)
      mLfoDest = std::max(0, std::min((int)value, 7));
    else if (id == 154)
      mLfoShape = std::max(0, std::min((int)value, 4));
    else if (id == 157)
      setFilterMode((int)value);
    else if (id >= 160 && id <= 163) {
      if (id == 162)
        mOscPitch[2] = value * 2.0f; // Sub (1 octave lower than Osc 1/2 @ 0.5)
      else
        mOscPitch[id - 160] = value * 4.0f;
    } else if (id >= 170 && id <= 173)
      mOscDrive[id - 170] = 1.0f + value * 10.0f;
    else if (id >= 180 && id <= 183)
      mOscFold[id - 180] = value;
    else if (id >= 190 && id <= 193) {
      mOscPW[id - 190] = value;
      for (auto &v : mVoices)
        v.oscillators[id - 190].setWaveShape(value);
    } else if (id >= 107 && id <= 109) {
      setOscVolume(id - 107, value);
    } else if (id == 110) {
      setNoiseLevel(value);
    } else if (id == 355) {
      setGlide(value);
    } else if (id == 155) { // Sub Shape
      setOscWaveform(2, value);
    }
  }

  void setOscWaveform(int index, float value) {
    if (index >= 0 && index < 4) {
      mOscWaveValues[index] = value;
      for (auto &v : mVoices)
        v.oscillators[index].setMorphValue(value);
    }
  }

  void setFilterMode(int mode) { mFilterMode = mode; }
  int getFilterMode() const { return mFilterMode; }

  static float getNorm(int count) {
    static const float table[17] = {0.0f, 1.0f, 0.7071f, 0.5773f, 0.5f, 0.4472f, 0.4082f, 0.3779f, 0.3535f, 0.3333f, 0.3162f, 0.3015f, 0.2886f, 0.2773f, 0.2672f, 0.2581f, 0.25f};
    return (count >= 0 && count <= 16) ? table[count] : 0.25f;
  }

  void renderBlock(float* outL, float* outR, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
      outL[i] = 0.0f;
    }

    int activeCount = 0;
    float lfoOutBlock[256];
    float bendFactor = powf(2.0f, mPitchBend / 12.0f);

    for (int i = 0; i < numFrames; ++i) {
      mLfoPhase += mLfoRate / mSampleRate;
      if (mLfoPhase >= 1.0f) {
        mLfoPhase -= 1.0f;
        mLfoLastRandVal = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
      }
      float lfoVal = 0.0f;
      if (mLfoShape == 0) {
        lfoVal = sinf(mLfoPhase * 2.0f * (float)M_PI);
      } else if (mLfoShape == 1) {
        lfoVal = 2.0f * fabsf(2.0f * mLfoPhase - 1.0f) - 1.0f;
      } else if (mLfoShape == 2) {
        lfoVal = 2.0f * mLfoPhase - 1.0f;
      } else if (mLfoShape == 3) {
        lfoVal = mLfoPhase < 0.5f ? 1.0f : -1.0f;
      } else if (mLfoShape == 4) {
        lfoVal = mLfoLastRandVal;
      }
      lfoOutBlock[i] = lfoVal * mLfoDepth;
    }

    for (auto &v : mVoices) {
      if (!v.active)
        continue;

      activeCount++;
      float osc1Pitch = mOscPitch[0] * (1.0f - mDetune * 0.03f);
      float osc2Pitch = mOscPitch[1] * (1.0f + mDetune * 0.03f);
      float osc3Pitch = mOscPitch[2] * (1.0f - mDetune * 0.015f);
      float osc4Pitch = mOscPitch[3];
      TSvf::Type filterType = (TSvf::Type)mFilterMode;

      for (int i = 0; i < numFrames; ++i) {
        float lfoOut = lfoOutBlock[i];

        if (v.controlCounter % 16 == 0) {
          if (mGlide > 0.001f) {
            float glideTimeSamples = mGlide * mSampleRate * 0.5f;
            float glideAlpha = 16.0f / (glideTimeSamples + 16.0f);
            v.frequency += (v.targetFrequency - v.frequency) * glideAlpha;
          } else {
            v.frequency = v.targetFrequency;
          }

          float lfoPitchBend = (mLfoDest == 1 ? lfoOut * 6.0f : 0.0f);
          float pitchBendFactor = bendFactor * powf(2.0f, lfoPitchBend / 12.0f);

          for (int oscIdx = 0; oscIdx < 4; ++oscIdx) {
            v.oscillators[oscIdx].setFrequency(v.frequency * pitchBendFactor, mSampleRate);
          }

          float morph1 = mOscWaveValues[0];
          if (mLfoDest == 2) {
            morph1 = std::max(0.0f, std::min(1.0f, morph1 + lfoOut));
          }
          v.oscillators[0].setMorphValue(morph1);

          float morph2 = mOscWaveValues[1];
          if (mLfoDest == 3) {
            morph2 = std::max(0.0f, std::min(1.0f, morph2 + lfoOut));
          }
          v.oscillators[1].setMorphValue(morph2);

          float cutoffLfoVal = (mLfoDest == 0 ? lfoOut : 0.0f);
          float modCutoff = std::max(
              0.0f,
              std::min(0.999f, mCutoff + v.currentFilterEnvVal * mF_Amt + cutoffLfoVal));
          v.svf.setParams(20.0f + modCutoff * modCutoff * 14000.0f,
                          std::max(0.1f, mResonance * 5.0f), mSampleRate);

          v.ampEnv.processBlock(16, v.ampEnvStart, v.ampEnvDelta);
          v.filterEnv.processBlock(16, v.filterEnvStart, v.filterEnvDelta);
        }

        int blockPhase = v.controlCounter % 16;
        float envVal = mUseEnvelope ? (v.ampEnvStart + v.ampEnvDelta * blockPhase) : 1.0f;
        bool deactive = mUseEnvelope ? (envVal < 0.0001f && !v.ampEnv.isActive()) : !v.isNoteHeld;

        if (deactive) {
          v.active = false;
          break;
        }

        v.currentFilterEnvVal = v.filterEnvStart + v.filterEnvDelta * blockPhase;
        v.controlCounter++;

        if (mOscSync && v.oscillators[0].hasWrapped()) {
          v.oscillators[1].resetPhase();
        }

        float modFold1 = mOscFold[0];
        if (mLfoDest == 4) {
          modFold1 = std::max(0.0f, std::min(1.0f, modFold1 + lfoOut));
        }
        float modFold2 = mOscFold[1];
        if (mLfoDest == 5) {
          modFold2 = std::max(0.0f, std::min(1.0f, modFold2 + lfoOut));
        }

        float osc1Val = v.oscillators[0].nextSample(0, osc1Pitch, modFold1);
        float osc2Val = v.oscillators[1].nextSample(0, osc2Pitch, modFold2);
        float osc3Val = v.oscillators[2].nextSample(0, osc3Pitch, mOscFold[2]);
        float osc4Val = v.oscillators[3].nextSample(0, osc4Pitch, mOscFold[3]);

        float vol1 = mOscVolumes[0];
        if (mLfoDest == 6) {
          vol1 = std::max(0.0f, std::min(1.0f, vol1 + lfoOut * 0.5f));
        }
        float vol2 = mOscVolumes[1];
        if (mLfoDest == 7) {
          vol2 = std::max(0.0f, std::min(1.0f, vol2 + lfoOut * 0.5f));
        }

        float subOutput = 0.0f;
        if (mRingMod) {
          subOutput = (osc1Val * vol1 * mOscDrive[0]) *
                      (osc2Val * vol2 * mOscDrive[1]);
        } else {
          subOutput = (osc1Val * vol1 * mOscDrive[0]) +
                      (osc2Val * vol2 * mOscDrive[1]);
        }
        subOutput += (osc3Val * mOscVolumes[2] * mOscDrive[2]);
        subOutput += (osc4Val * mOscVolumes[3] * mOscDrive[3]);

        mNoiseSeed = mNoiseSeed * 1103515245 + 12345;
        subOutput +=
            (((float)(mNoiseSeed & 0x7fffffff) / (float)0x7fffffff) * 2.0f -
             1.0f) *
            mNoiseLevel;

        float output = subOutput * v.amplitude * envVal;
        outL[i] += v.svf.process(output, filterType);
      }
    }

    mControlCounter += numFrames;

    if (activeCount > 0) {
      float norm = getNorm(activeCount) * 0.5f;
      for (int i = 0; i < numFrames; ++i) {
        float monoVal = outL[i] * norm;
        outL[i] = monoVal;
        outR[i] = monoVal;
      }
    } else {
      for (int i = 0; i < numFrames; ++i) {
        outL[i] = 0.0f;
        outR[i] = 0.0f;
      }
    }
  }

  float render() {
    float mixedOutput = 0.0f;
    int activeCount = 0;
    
    // Update LFO Phase & Waveform
    mLfoPhase += mLfoRate / mSampleRate;
    if (mLfoPhase >= 1.0f) {
      mLfoPhase -= 1.0f;
      // Trigger new random value for S&H
      mLfoLastRandVal = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    }

    float lfoVal = 0.0f;
    if (mLfoShape == 0) { // Sine
      lfoVal = sinf(mLfoPhase * 2.0f * (float)M_PI);
    } else if (mLfoShape == 1) { // Triangle
      lfoVal = 2.0f * fabsf(2.0f * mLfoPhase - 1.0f) - 1.0f;
    } else if (mLfoShape == 2) { // Saw
      lfoVal = 2.0f * mLfoPhase - 1.0f;
    } else if (mLfoShape == 3) { // Square
      lfoVal = mLfoPhase < 0.5f ? 1.0f : -1.0f;
    } else if (mLfoShape == 4) { // Random (S&H)
      lfoVal = mLfoLastRandVal;
    }

    float lfoOut = lfoVal * mLfoDepth;
    float bendFactor = powf(2.0f, mPitchBend / 12.0f);

    for (auto &v : mVoices) {
      if (!v.active)
        continue;

      // 1. Control Rate Phase (Speed up Glide and Frequency updates)
      if (v.controlCounter % 16 == 0) {
        if (mGlide > 0.001f) {
           float glideTimeSamples = mGlide * mSampleRate * 0.5f;
           float glideAlpha = 16.0f / (glideTimeSamples + 16.0f);
           v.frequency += (v.targetFrequency - v.frequency) * glideAlpha;
        } else {
           v.frequency = v.targetFrequency;
        }

        // Apply pitch LFO modulation if active (dest == 1, maps to +/- 6 semitones)
        float lfoPitchBend = (mLfoDest == 1 ? lfoOut * 6.0f : 0.0f);
        float pitchBendFactor = bendFactor * powf(2.0f, lfoPitchBend / 12.0f);

        for (int i = 0; i < 4; ++i) {
          v.oscillators[i].setFrequency(v.frequency * pitchBendFactor, mSampleRate);
        }

        // Apply morph shape modulation
        float morph1 = mOscWaveValues[0];
        if (mLfoDest == 2) {
          morph1 = std::max(0.0f, std::min(1.0f, morph1 + lfoOut));
        }
        v.oscillators[0].setMorphValue(morph1);

        float morph2 = mOscWaveValues[1];
        if (mLfoDest == 3) {
          morph2 = std::max(0.0f, std::min(1.0f, morph2 + lfoOut));
        }
        v.oscillators[1].setMorphValue(morph2);
        
        float cutoffLfoVal = (mLfoDest == 0 ? lfoOut : 0.0f);
        float modCutoff = std::max(
            0.0f,
            std::min(0.999f, mCutoff + v.currentFilterEnvVal * mF_Amt + cutoffLfoVal));
        v.svf.setParams(20.0f + modCutoff * modCutoff * 14000.0f,
                        std::max(0.1f, mResonance * 5.0f), mSampleRate);
                        
        v.ampEnv.processBlock(16, v.ampEnvStart, v.ampEnvDelta);
        v.filterEnv.processBlock(16, v.filterEnvStart, v.filterEnvDelta);
      }

      int blockPhase = v.controlCounter % 16;
      float envVal = mUseEnvelope ? (v.ampEnvStart + v.ampEnvDelta * blockPhase) : 1.0f;
      bool deactive = mUseEnvelope ? (envVal < 0.0001f && !v.ampEnv.isActive()) : !v.isNoteHeld;
      
      if (deactive) {
        v.active = false;
        continue;
      }
      activeCount++;
      v.currentFilterEnvVal = v.filterEnvStart + v.filterEnvDelta * blockPhase;

      v.controlCounter++;

      // Relative detuning relationship between all 3 oscillators
      float osc1Pitch = mOscPitch[0] * (1.0f - mDetune * 0.03f);
      float osc2Pitch = mOscPitch[1] * (1.0f + mDetune * 0.03f);
      float osc3Pitch = mOscPitch[2] * (1.0f - mDetune * 0.015f);
      float osc4Pitch = mOscPitch[3];

      if (mOscSync && v.oscillators[0].hasWrapped()) {
        v.oscillators[1].resetPhase();
      }

      // Modulated Fold values (dest == 4 & 5)
      float modFold1 = mOscFold[0];
      if (mLfoDest == 4) {
        modFold1 = std::max(0.0f, std::min(1.0f, modFold1 + lfoOut));
      }
      float modFold2 = mOscFold[1];
      if (mLfoDest == 5) {
        modFold2 = std::max(0.0f, std::min(1.0f, modFold2 + lfoOut));
      }

      float osc1Val = v.oscillators[0].nextSample(0, osc1Pitch, modFold1);
      float osc2Val = v.oscillators[1].nextSample(0, osc2Pitch, modFold2);
      float osc3Val = v.oscillators[2].nextSample(0, osc3Pitch, mOscFold[2]);
      float osc4Val = v.oscillators[3].nextSample(0, osc4Pitch, mOscFold[3]);

      // Modulated Oscillator Volumes (dest == 6 & 7)
      float vol1 = mOscVolumes[0];
      if (mLfoDest == 6) {
        vol1 = std::max(0.0f, std::min(1.0f, vol1 + lfoOut * 0.5f));
      }
      float vol2 = mOscVolumes[1];
      if (mLfoDest == 7) {
        vol2 = std::max(0.0f, std::min(1.0f, vol2 + lfoOut * 0.5f));
      }

      float subOutput = 0.0f;
      if (mRingMod) {
        subOutput = (osc1Val * vol1 * mOscDrive[0]) *
                    (osc2Val * vol2 * mOscDrive[1]);
      } else {
        subOutput = (osc1Val * vol1 * mOscDrive[0]) +
                    (osc2Val * vol2 * mOscDrive[1]);
      }
      subOutput += (osc3Val * mOscVolumes[2] * mOscDrive[2]);
      subOutput += (osc4Val * mOscVolumes[3] * mOscDrive[3]);

      mNoiseSeed = mNoiseSeed * 1103515245 + 12345;
      subOutput +=
          (((float)(mNoiseSeed & 0x7fffffff) / (float)0x7fffffff) * 2.0f -
           1.0f) *
          mNoiseLevel;
      
      float output = subOutput * v.amplitude * envVal;

      TSvf::Type type = (TSvf::Type)mFilterMode;
      mixedOutput += v.svf.process(output, type);
    }
    mControlCounter++;
    
    if (activeCount == 0) return 0.0f;
    return mixedOutput * getNorm(activeCount) * 0.5f;
  }

  bool isActive() const {
    for (const auto &v : mVoices)
      if (v.active)
        return true;
    return false;
  }

  float getEnvelopeValue() const {
    float maxEnv = 0.0f;
    for (const auto &v : mVoices) {
      if (v.active) {
        maxEnv = std::max(maxEnv, v.currentFilterEnvVal);
      }
    }
    return maxEnv;
  }

private:
  void updateLiveEnvelopes() {
    for (auto &v : mVoices)
      if (v.active) {
        v.ampEnv.setParameters(mAttack, mDecay, mSustain, mRelease);
        v.filterEnv.setParameters(mF_Atk, mF_Dcy, mF_Sus, mF_Rel);
      }
  }
  std::vector<Voice> mVoices;
  std::vector<float> mOscVolumes;
  std::vector<float> mOscWaveValues;
  uint32_t mControlCounter = 0;
  float mCutoff = 0.45f, mResonance = 0.0f, mAttack = 0.01f, mDecay = 0.1f,
        mSustain = 0.8f, mRelease = 0.5f, mF_Atk = 0.01f, mF_Dcy = 0.1f,
        mF_Sus = 0.0f, mF_Rel = 0.5f, mF_Amt = 0.0f;
  float mDetune = 0.0f, mNoiseLevel = 0.0f, mLfoRate = 0.0f, mLfoDepth = 0.0f,
        mFrequency = 440.0f, mLastFrequency = 440.0f, mGlide = 0.0f;
  float mLfoPhase = 0.0f;
  float mLfoLastRandVal = 0.0f;
  int mLfoShape = 0;
  int mLfoDest = 0;
  unsigned int mNoiseSeed = 12345;
  float mSampleRate = 48000.0f;
  bool mUseEnvelope = true;
  bool mReverse = false;
  bool mOscSync = false, mRingMod = false, mIgnoreNoteFrequency = false;
  float mFmAmt = 0.0f;
  float mPitchBend = 0.0f;
  int mFilterMode = 0;
  float mOscPitch[4] = {1.0f, 1.0f, 0.5f, 1.0f},
        mOscDrive[4] = {1.0f, 1.0f, 1.0f, 1.0f},
        mOscFold[4] = {0.0f, 0.0f, 0.0f, 0.0f},
        mOscPW[4] = {0.5f, 0.5f, 0.5f, 0.5f};
};

#endif
