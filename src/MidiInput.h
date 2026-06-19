#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include "AudioEngine.h"
#include "ui/UIManager.h"
#include <iostream>

struct MidiCallbackData {
    AudioEngine* engine;
    UIManager* ui;
};

// ─── Unified Scale Table ───────────────────────────────────────────────────
// Index 0 = Chromatic (no snapping). All indices match between the scale
// picker modal, the arpeggiator, and this note-snap code.
// Intervals are semitone offsets from the root within one octave.
// -1 terminates the interval list for variable-length scales.
static const int kScaleCount = 40;
static const char* kScaleNames[40] = {
    "Chromatic",        //  0
    "Major",            //  1
    "Natural Minor",    //  2
    "Harmonic Minor",   //  3
    "Melodic Minor",    //  4
    "Dorian",           //  5
    "Phrygian",         //  6
    "Lydian",           //  7
    "Mixolydian",       //  8
    "Locrian",          //  9
    "Phrygian Dom.",    // 10
    "Lydian Dom.",      // 11
    "Pentatonic Maj",   // 12
    "Pentatonic Min",   // 13
    "Blues",            // 14
    "Blues Major",      // 15
    "Bebop Major",      // 16
    "Bebop Dominant",   // 17
    "Bebop Minor",      // 18
    "Whole Tone",       // 19
    "Diminished HW",    // 20
    "Diminished WH",    // 21
    "Augmented",        // 22
    "Double Harmonic",  // 23
    "Hungarian Minor",  // 24
    "Neapolitan Maj",   // 25
    "Neapolitan Min",   // 26
    "Persian",          // 27
    "Arabian",          // 28
    "Hirajoshi",        // 29
    "In-Sen",           // 30
    "Yo",               // 31
    "Iwato",            // 32
    "Chinese",          // 33
    "Egyptian",         // 34
    "Prometheus",       // 35
    "Tritone",          // 36
    "Enigmatic",        // 37
    "Super Locrian",    // 38
    "Acoustic",         // 39
};
static const int kScaleIntervals[40][12] = {
    // 0  Chromatic
    {0,1,2,3,4,5,6,7,8,9,10,11},
    // 1  Major
    {0,2,4,5,7,9,11,-1,-1,-1,-1,-1},
    // 2  Natural Minor
    {0,2,3,5,7,8,10,-1,-1,-1,-1,-1},
    // 3  Harmonic Minor
    {0,2,3,5,7,8,11,-1,-1,-1,-1,-1},
    // 4  Melodic Minor
    {0,2,3,5,7,9,11,-1,-1,-1,-1,-1},
    // 5  Dorian
    {0,2,3,5,7,9,10,-1,-1,-1,-1,-1},
    // 6  Phrygian
    {0,1,3,5,7,8,10,-1,-1,-1,-1,-1},
    // 7  Lydian
    {0,2,4,6,7,9,11,-1,-1,-1,-1,-1},
    // 8  Mixolydian
    {0,2,4,5,7,9,10,-1,-1,-1,-1,-1},
    // 9  Locrian
    {0,1,3,5,6,8,10,-1,-1,-1,-1,-1},
    // 10 Phrygian Dominant
    {0,1,4,5,7,8,10,-1,-1,-1,-1,-1},
    // 11 Lydian Dominant
    {0,2,4,6,7,9,10,-1,-1,-1,-1,-1},
    // 12 Pentatonic Major
    {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1},
    // 13 Pentatonic Minor
    {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1},
    // 14 Blues
    {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1},
    // 15 Blues Major
    {0,2,3,4,7,9,-1,-1,-1,-1,-1,-1},
    // 16 Bebop Major
    {0,2,4,5,7,8,9,11,-1,-1,-1,-1},
    // 17 Bebop Dominant
    {0,2,4,5,7,9,10,11,-1,-1,-1,-1},
    // 18 Bebop Minor
    {0,2,3,5,7,8,9,10,-1,-1,-1,-1},
    // 19 Whole Tone
    {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1},
    // 20 Diminished HW
    {0,1,3,4,6,7,9,10,-1,-1,-1,-1},
    // 21 Diminished WH
    {0,2,3,5,6,8,9,11,-1,-1,-1,-1},
    // 22 Augmented
    {0,3,4,7,8,11,-1,-1,-1,-1,-1,-1},
    // 23 Double Harmonic
    {0,1,4,5,7,8,11,-1,-1,-1,-1,-1},
    // 24 Hungarian Minor
    {0,2,3,6,7,8,11,-1,-1,-1,-1,-1},
    // 25 Neapolitan Major
    {0,1,3,5,7,9,11,-1,-1,-1,-1,-1},
    // 26 Neapolitan Minor
    {0,1,3,5,7,8,11,-1,-1,-1,-1,-1},
    // 27 Persian
    {0,1,4,5,6,8,11,-1,-1,-1,-1,-1},
    // 28 Arabian
    {0,2,4,5,6,8,10,-1,-1,-1,-1,-1},
    // 29 Hirajoshi
    {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1},
    // 30 In-Sen
    {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1},
    // 31 Yo
    {0,2,5,7,9,-1,-1,-1,-1,-1,-1,-1},
    // 32 Iwato
    {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1},
    // 33 Chinese
    {0,4,6,7,11,-1,-1,-1,-1,-1,-1,-1},
    // 34 Egyptian
    {0,2,5,7,10,-1,-1,-1,-1,-1,-1,-1},
    // 35 Prometheus
    {0,2,4,6,9,10,-1,-1,-1,-1,-1,-1},
    // 36 Tritone
    {0,1,4,6,7,10,-1,-1,-1,-1,-1,-1},
    // 37 Enigmatic
    {0,1,4,6,8,10,11,-1,-1,-1,-1,-1},
    // 38 Super Locrian
    {0,1,3,4,6,8,10,-1,-1,-1,-1,-1},
    // 39 Acoustic
    {0,2,4,6,7,9,10,-1,-1,-1,-1,-1},
};

// Snap a MIDI note to the given scale (scaleIdx from kScaleIntervals).
// scaleIdx == 0 = Chromatic (no snap). rootKey is the root semitone offset.
static inline int snapNoteToScale(int note, int scaleIdx, int rootKey) {
    if (scaleIdx <= 0 || scaleIdx >= kScaleCount) return note;
    // Build interval list
    const int* row = kScaleIntervals[scaleIdx];
    int intervals[12];
    int numNotes = 0;
    for (int i = 0; i < 12 && row[i] >= 0; ++i)
        intervals[numNotes++] = row[i];
    if (numNotes == 0) return note;
    // Map linear key index to (octave, degree)
    int noteIdx = note - 60;
    int octave = (noteIdx >= 0) ? (noteIdx / numNotes) : ((noteIdx - numNotes + 1) / numNotes);
    int degree = noteIdx - octave * numNotes;
    if (degree < 0) { degree += numNotes; octave--; }
    degree = degree % numNotes;
    return 60 + rootKey + octave * 12 + intervals[degree];
}
// ──────────────────────────────────────────────────────────────────────────


#ifdef __APPLE__
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

static MIDIClientRef gMidiClient = 0;
static MIDIPortRef gMidiInputPort = 0;
static MidiCallbackData gMidiCallbackData = {nullptr, nullptr};

static void reconnectMidiSources() {
    if (gMidiInputPort == 0) return;
    
    ItemCount numSources = MIDIGetNumberOfSources();
    std::cout << "CoreMIDI: " << numSources << " MIDI source(s) found." << std::endl;
    for (ItemCount i = 0; i < numSources; ++i) {
        MIDIEndpointRef source = MIDIGetSource(i);
        if (source != 0) {
            CFStringRef nameRef = NULL;
            MIDIObjectGetStringProperty(source, kMIDIPropertyName, &nameRef);
            if (nameRef) {
                char name[256];
                CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8);
                CFRelease(nameRef);
                std::cout << "  Source [" << i << "]: " << name << std::endl;
            } else {
                std::cout << "  Source [" << i << "]: (Unknown Name)" << std::endl;
            }
            OSStatus err = MIDIPortConnectSource(gMidiInputPort, source, NULL);
            if (err != noErr) {
                std::cerr << "  CoreMIDI: Failed to connect source, error: " << err << std::endl;
            } else {
                std::cout << "  CoreMIDI: Successfully connected source [" << i << "]" << std::endl;
            }
        }
    }
}

static void midiNotifyProc(const MIDINotification *notification, void *refCon) {
    if (notification->messageID == kMIDIMsgObjectAdded || notification->messageID == kMIDIMsgObjectRemoved) {
        reconnectMidiSources();
    }
}

static void midiInputCallback(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcRefCon) {
    MidiCallbackData* data = static_cast<MidiCallbackData*>(readProcRefCon);
    if (!data || !data->engine || !data->ui) return;
    
    const MIDIPacket *packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        for (int b = 0; b < packet->length; ) {
            uint8_t status = packet->data[b];
            
            // Check for MIDI Real-Time messages (single byte)
            if (status == 0xFA) { // Start / Play
                data->engine->setPlaying(true);
                b++;
                continue;
            } else if (status == 0xFC) { // Stop
                data->engine->setPlaying(false);
                data->engine->setIsRecording(false);
                b++;
                continue;
            } else if (status == 0xFB) { // Continue
                data->engine->setPlaying(true);
                b++;
                continue;
            }
            
            // Check for SysEx (MMC)
            if (status == 0xF0) {
                int sysExLen = 0;
                while (b + sysExLen < packet->length && packet->data[b + sysExLen] != 0xF7) {
                    sysExLen++;
                }
                if (b + sysExLen < packet->length) sysExLen++; // include 0xF7
                
                // MMC SysEx is typically: F0 7F [device_id] 06 [command] F7
                if (sysExLen >= 6 && packet->data[b + 1] == 0x7F && packet->data[b + 3] == 0x06) {
                    uint8_t cmd = packet->data[b + 4];
                    if (cmd == 0x02) { // Play
                        data->engine->setPlaying(true);
                    } else if (cmd == 0x01) { // Stop
                        data->engine->setPlaying(false);
                        data->engine->setIsRecording(false);
                    } else if (cmd == 0x06) { // Record Start
                        data->engine->setIsRecording(true);
                        data->engine->setPlaying(true);
                    } else if (cmd == 0x07) { // Record Stop
                        data->engine->setIsRecording(false);
                    }
                }
                b += sysExLen;
                continue;
            }
            
            uint8_t messageType = status & 0xF0;
            uint8_t channel = status & 0x0F;
            
            if (messageType == 0x90) { // Note On
                if (b + 2 < packet->length) {
                    uint8_t note = packet->data[b + 1];
                    uint8_t velocity = packet->data[b + 2];
                    int activeTrack = data->ui->getActiveTrack();
                    int targetTrack = activeTrack;

                    if (data->ui->mPadLearnActive && data->ui->mPadLearnTarget >= 0 && data->ui->mPadLearnTarget < 24) {
                        if (velocity > 0) {
                            data->ui->mSettingsPadNoteMap[data->ui->mPadLearnTarget] = note;
                            data->ui->mPadLearnActive = false;
                            data->ui->mNeedsScreenRebuild = true;
                        }
                        b += 3;
                        continue;
                    }

                    int padIdx = -1;
                    if (data->ui->mSettingsPadMode != 0) {
                        for (int i = 0; i < data->ui->mSettingsPadCount; ++i) {
                            if (note == data->ui->mSettingsPadNoteMap[i]) {
                                padIdx = i;
                                break;
                            }
                        }
                    }
                    
                    // Intercept pads as FX/Drum pad triggers ONLY if pad mode is not Keyboard (0)
                    if (padIdx >= 0) {
                        
                        // Keep track of the most recently played FX pad for channel pressure routing
                        if (data->ui->mSettingsPadMode == 1) {
                            data->ui->mLastActiveFxPadIdx = padIdx;
                        }
                        
                        if (data->ui->mSettingsPadMode == 3) { // FM Drum Mode
                            int foundTrack = -1;
                            for (int t = 0; t < 8; ++t) {
                                if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                            }
                            if (foundTrack == -1) {
                                for (int t = 0; t < 8; ++t) {
                                    if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                                }
                            }
                            if (foundTrack != -1) targetTrack = foundTrack;
                        } else if (data->ui->mSettingsPadMode == 4) { // Analogue Drum Mode
                            int foundTrack = -1;
                            for (int t = 0; t < 8; ++t) {
                                if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                            }
                            if (foundTrack == -1) {
                                for (int t = 0; t < 8; ++t) {
                                    if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                                }
                            }
                            if (foundTrack != -1) targetTrack = foundTrack;
                        }
                        
                        if (velocity > 0) {
                            if (data->ui->mSettingsPadMode == 1) { // FX
                                if (data->ui->mSettingsFxPadMomentary) {
                                    int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                                    data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), 1.0f);
                                } else {
                                    data->ui->mSettingsPadFxToggleState[padIdx] = !data->ui->mSettingsPadFxToggleState[padIdx];
                                    int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                                    data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), data->ui->mSettingsPadFxToggleState[padIdx] ? 1.0f : 0.0f);
                                    data->ui->mNeedsScreenRebuild = true;
                                }
                            } else if (data->ui->mSettingsPadMode == 2) { // Scales
                                int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                                int scaleIdx = data->ui->mSelectedScaleIdx;
                                // Build interval list from scale table
                                std::vector<int> intervals;
                                {
                                    const int* row = kScaleIntervals[scaleIdx < kScaleCount ? scaleIdx : 1];
                                    for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                                    if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                                }
                                int numNotes = (int)intervals.size();
                                int baseNote = 60 + rootKey + data->ui->mSettingsOctaveOffset * 12;
                                int octShift = padIdx / numNotes;
                                int degIdx = padIdx % numNotes;
                                int noteToPlay = baseNote + octShift * 12 + intervals[degIdx];
                                data->engine->triggerNote(activeTrack, noteToPlay, velocity);
                            } else if (data->ui->mSettingsPadMode == 3 || data->ui->mSettingsPadMode == 4) { // Drum
                                int drumIdx = data->ui->mSettingsPadDrumAssign[padIdx];
                                data->engine->triggerNote(targetTrack, 60 + drumIdx, velocity);
                            } else if (data->ui->mSettingsPadMode == 5) { // Slices
                                int col = padIdx % 4;
                                int row = padIdx / 4;
                                int revPadIdx = (3 - row) * 4 + col;
                                int numSlices = 1;
                                std::vector<float> slicePoints = data->engine->getSamplerSlicePoints(activeTrack);
                                if (!slicePoints.empty()) {
                                    numSlices = (int)slicePoints.size();
                                }
                                int drumIdx = revPadIdx % numSlices;
                                data->engine->triggerNote(activeTrack, 60 + drumIdx, velocity);
                            }
                        } else {
                            // Note Off via velocity 0
                            if (data->ui->mSettingsPadMode == 1) { // FX
                                if (data->ui->mSettingsFxPadMomentary) {
                                    int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                                    data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), 0.0f);
                                }
                            } else if (data->ui->mSettingsPadMode == 2) { // Scales
                                int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                                int scaleIdx = data->ui->mSelectedScaleIdx;
                                // Build interval list from scale table
                                std::vector<int> intervals;
                                {
                                    const int* row = kScaleIntervals[scaleIdx < kScaleCount ? scaleIdx : 1];
                                    for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                                    if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                                }
                                int numNotes = (int)intervals.size();
                                int baseNote = 60 + rootKey + data->ui->mSettingsOctaveOffset * 12;
                                int octShift = padIdx / numNotes;
                                int degIdx = padIdx % numNotes;
                                int noteToPlay = baseNote + octShift * 12 + intervals[degIdx];
                                data->engine->releaseNote(activeTrack, noteToPlay);
                            } else if (data->ui->mSettingsPadMode == 3 || data->ui->mSettingsPadMode == 4) { // Drum
                                int drumIdx = data->ui->mSettingsPadDrumAssign[padIdx];
                                data->engine->releaseNote(targetTrack, 60 + drumIdx);
                            } else if (data->ui->mSettingsPadMode == 5) { // Slices
                                int col = padIdx % 4;
                                int row = padIdx / 4;
                                int revPadIdx = (3 - row) * 4 + col;
                                int numSlices = 1;
                                std::vector<float> slicePoints = data->engine->getSamplerSlicePoints(activeTrack);
                                if (!slicePoints.empty()) {
                                    numSlices = (int)slicePoints.size();
                                }
                                int drumIdx = revPadIdx % numSlices;
                                data->engine->releaseNote(activeTrack, 60 + drumIdx);
                            }
                        }
                    } else {
                        // Standard note: plays in active track, snaps to active scale if selected!
                        if (velocity > 0) {
                            int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                            int scaleIdx = data->ui->mSelectedScaleIdx;
                            int finalNote = note;
                            if (scaleIdx > 0) {
                                finalNote = snapNoteToScale(note, scaleIdx, rootKey);
                            }
                            data->engine->triggerNote(activeTrack, finalNote, velocity);
                        } else {
                            // Note Off via velocity 0
                            int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                            int scaleIdx = data->ui->mSelectedScaleIdx;
                            int finalNote = note;
                            if (scaleIdx > 0) {
                                finalNote = snapNoteToScale(note, scaleIdx, rootKey);
                            }
                            data->engine->releaseNote(activeTrack, finalNote);
                        }
                    }
                    b += 3;
                } else {
                    break;
                }
            } else if (messageType == 0x80) { // Note Off
                if (b + 2 < packet->length) {
                    uint8_t note = packet->data[b + 1];
                    int activeTrack = data->ui->getActiveTrack();
                    int targetTrack = activeTrack;
                    
                    int padIdx = -1;
                    if (data->ui->mSettingsPadMode != 0) {
                        for (int i = 0; i < data->ui->mSettingsPadCount; ++i) {
                            if (note == data->ui->mSettingsPadNoteMap[i]) {
                                padIdx = i;
                                break;
                            }
                        }
                    }
                    
                    // Intercept pads as FX/Drum pad triggers ONLY if pad mode is not Keyboard (0)
                    if (padIdx >= 0) {
                        
                        // Clear the active FX pad index if it matches the one being released
                        if (data->ui->mSettingsPadMode == 1 && data->ui->mLastActiveFxPadIdx == padIdx) {
                            data->ui->mLastActiveFxPadIdx = -1;
                        }
                        
                        if (data->ui->mSettingsPadMode == 3) { // FM Drum Mode
                            int foundTrack = -1;
                            for (int t = 0; t < 8; ++t) {
                                if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                            }
                            if (foundTrack == -1) {
                                for (int t = 0; t < 8; ++t) {
                                    if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                                }
                            }
                            if (foundTrack != -1) targetTrack = foundTrack;
                        } else if (data->ui->mSettingsPadMode == 4) { // Analogue Drum Mode
                            int foundTrack = -1;
                            for (int t = 0; t < 8; ++t) {
                                if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                            }
                            if (foundTrack == -1) {
                                for (int t = 0; t < 8; ++t) {
                                    if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                                }
                            }
                            if (foundTrack != -1) targetTrack = foundTrack;
                        }
                        
                        if (data->ui->mSettingsPadMode == 1) { // FX
                            if (data->ui->mSettingsFxPadMomentary) {
                                int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                                data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), 0.0f);
                            }
                        } else if (data->ui->mSettingsPadMode == 2) { // Scales
                            int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                            int scaleIdx = data->ui->mSelectedScaleIdx;
                            // Build interval list from scale table
                            std::vector<int> intervals;
                            {
                                const int* row = kScaleIntervals[scaleIdx < kScaleCount ? scaleIdx : 1];
                                for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                                if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                            }
                            int numNotes = (int)intervals.size();
                            int baseNote = 60 + rootKey + data->ui->mSettingsOctaveOffset * 12;
                            int octShift = padIdx / numNotes;
                            int degIdx = padIdx % numNotes;
                            int noteToPlay = baseNote + octShift * 12 + intervals[degIdx];
                            data->engine->releaseNote(activeTrack, noteToPlay);
                        } else if (data->ui->mSettingsPadMode == 3 || data->ui->mSettingsPadMode == 4) { // Drum
                            int drumIdx = data->ui->mSettingsPadDrumAssign[padIdx];
                            data->engine->releaseNote(targetTrack, 60 + drumIdx);
                        } else if (data->ui->mSettingsPadMode == 5) { // Slices
                            int col = padIdx % 4;
                            int row = padIdx / 4;
                            int revPadIdx = (3 - row) * 4 + col;
                            int numSlices = 1;
                            std::vector<float> slicePoints = data->engine->getSamplerSlicePoints(activeTrack);
                            if (!slicePoints.empty()) {
                                numSlices = (int)slicePoints.size();
                            }
                            int drumIdx = revPadIdx % numSlices;
                            data->engine->releaseNote(activeTrack, 60 + drumIdx);
                        }
                    } else {
                        // Standard note: plays in active track, snaps to active scale if selected!
                        int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                        int scaleIdx = data->ui->mSelectedScaleIdx;
                        int finalNote = note;
                        if (scaleIdx > 0) {
                            finalNote = snapNoteToScale(note, scaleIdx, rootKey);
                        }
                        data->engine->releaseNote(activeTrack, finalNote);
                    }
                    b += 3;
                } else {
                    break;
                }
            } else if (messageType == 0xB0) { // CC (Control Change)
                if (b + 2 < packet->length) {
                    uint8_t cc = packet->data[b + 1];
                    uint8_t val = packet->data[b + 2];
                    
                    // Hardware transport & navigation CC buttons
                    if (cc == data->ui->mCcPlay && cc == data->ui->mCcStop) {
                        data->engine->setPlaying(val >= 64);
                    } else if (cc == data->ui->mCcPlay && val >= 64) {
                        data->engine->setPlaying(true);
                    } else if (cc == data->ui->mCcStop && val >= 64) {
                        data->engine->setPlaying(false);
                    } else if (cc == data->ui->mCcRecord) {
                        bool recState = (val >= 64);
                        data->engine->setIsRecording(recState);
                        if (recState && !data->engine->getIsPlaying()) {
                            data->engine->setPlaying(true);
                        }
                    } else if (cc == data->ui->mCcClear) {
                        if (val >= 64) {
                            int activeTrack = data->ui->getActiveTrack();
                            data->engine->clearSequencer(activeTrack);
                        }
                    } else if (cc == data->ui->mCcPrevTrack) {
                        if (val >= 64) {
                            int activeTrack = data->ui->getActiveTrack();
                            int prevTrk = (activeTrack - 1 + 8) % 8;
                            data->ui->setActiveTrack(prevTrk);
                            data->ui->mNeedsScreenRebuild = true;
                        }
                    } else if (cc == data->ui->mCcNextTrack) {
                        if (val >= 64) {
                            int activeTrack = data->ui->getActiveTrack();
                            int nextTrk = (activeTrack + 1) % 8;
                            data->ui->setActiveTrack(nextTrk);
                            data->ui->mNeedsScreenRebuild = true;
                        }
                    } else {
                        // MIDI Learn or Dynamic CC mapping update
                        int activeTrack = data->ui->getActiveTrack();
                        float floatVal = val / 127.0f;

                        if (data->ui->mMidiLearnActive && data->ui->mMidiLearnTargetParamId >= 0) {
                            int targetParamId = data->ui->mMidiLearnTargetParamId;
                            
                            // Find if this CC is already mapped on the active track
                            int foundIdx = -1;
                            bool isKnob = true;
                            for (int k = 0; k < data->ui->mSettingsKnobCount; ++k) {
                                if (data->ui->mSeqMidiKnobCC[activeTrack][k] == cc) {
                                    int mappedCh = data->ui->mSeqMidiKnobChannel[activeTrack][k];
                                    if (mappedCh == 0 || mappedCh == (channel + 1)) {
                                        foundIdx = k;
                                        isKnob = true;
                                        break;
                                    }
                                }
                            }
                            if (foundIdx == -1) {
                                for (int f = 0; f < data->ui->mSettingsSliderCount; ++f) {
                                    if (data->ui->mSeqMidiFaderCC[activeTrack][f] == cc) {
                                        int mappedCh = data->ui->mSeqMidiFaderChannel[activeTrack][f];
                                        if (mappedCh == 0 || mappedCh == (channel + 1)) {
                                            foundIdx = f;
                                            isKnob = false;
                                            break;
                                        }
                                    }
                                }
                            }

                            // If not found, assign it to the first unmapped knob or fader slot
                            if (foundIdx == -1) {
                                for (int k = 0; k < data->ui->mSettingsKnobCount; ++k) {
                                    if (data->ui->mSeqMidiKnobParam[activeTrack][k] == -1) {
                                        foundIdx = k;
                                        isKnob = true;
                                        for (int t = 0; t < 8; ++t) {
                                            data->ui->mSeqMidiKnobCC[t][k] = cc;
                                            data->ui->mSeqMidiKnobChannel[t][k] = channel + 1;
                                        }
                                        break;
                                    }
                                }
                            }
                            if (foundIdx == -1) {
                                for (int f = 0; f < data->ui->mSettingsSliderCount; ++f) {
                                    if (data->ui->mSeqMidiFaderParam[activeTrack][f] == -1) {
                                        foundIdx = f;
                                        isKnob = false;
                                        for (int t = 0; t < 8; ++t) {
                                            data->ui->mSeqMidiFaderCC[t][f] = cc;
                                            data->ui->mSeqMidiFaderChannel[t][f] = channel + 1;
                                        }
                                        break;
                                    }
                                }
                            }

                            // If still not found, overwrite the first knob slot
                            if (foundIdx == -1) {
                                foundIdx = 0;
                                isKnob = true;
                                for (int t = 0; t < 8; ++t) {
                                    data->ui->mSeqMidiKnobCC[t][0] = cc;
                                    data->ui->mSeqMidiKnobChannel[t][0] = channel + 1;
                                }
                            }

                            // Perform the assignment
                            if (isKnob) {
                                data->ui->mSeqMidiKnobParam[activeTrack][foundIdx] = targetParamId;
                                data->ui->mSeqMidiKnobValue[activeTrack][foundIdx] = floatVal;
                                float scaledVal = data->ui->scaleParamFromNormalized(targetParamId, floatVal);
                                data->engine->setParameter(activeTrack, targetParamId, scaledVal);
                            } else {
                                data->ui->mSeqMidiFaderParam[activeTrack][foundIdx] = targetParamId;
                                data->ui->mSeqMidiFaderValue[activeTrack][foundIdx] = floatVal;
                                float scaledVal = data->ui->scaleParamFromNormalized(targetParamId, floatVal);
                                data->engine->setParameter(activeTrack, targetParamId, scaledVal);
                            }
                            
                            // Exit learn mode thread-safely
                            data->ui->mMidiLearnActive = false;
                            data->ui->mMidiLearnTargetParamId = -1;
                            data->ui->mNeedsScreenRebuild = true;
                        } else {
                            bool ccMatched = false;
                            
                            // Check knobs CCs
                            for (int k = 0; k < data->ui->mSettingsKnobCount; ++k) {
                                if (data->ui->mSeqMidiKnobCC[activeTrack][k] == cc) {
                                    int mappedCh = data->ui->mSeqMidiKnobChannel[activeTrack][k];
                                    if (mappedCh == 0 || mappedCh == (channel + 1)) {
                                        int paramId = data->ui->mSeqMidiKnobParam[activeTrack][k];
                                        if (paramId >= 0) {
                                            uint32_t now = lv_tick_get();
                                            uint8_t lastCcVal = data->ui->mLastKnobCcVal[activeTrack][k];
                                            uint32_t lastTime = data->ui->mLastKnobMs[activeTrack][k];
                                            bool isInit = data->ui->mKnobInitialized[activeTrack][k];

                                            data->ui->mLastKnobCcVal[activeTrack][k] = val;
                                            data->ui->mLastKnobMs[activeTrack][k] = now;
                                            data->ui->mKnobInitialized[activeTrack][k] = true;

                                            float paramVal = data->engine->getTracks()[activeTrack].parameters[paramId];

                                            if (isInit) {
                                                float normVal = data->ui->normalizeParamValue(paramId, paramVal);

                                                // Calculate CC value delta
                                                int rawDelta = (int)val - (int)lastCcVal;
                                                
                                                // Handle wrap-arounds for endless encoders
                                                if (rawDelta > 64) rawDelta -= 128;
                                                else if (rawDelta < -64) rawDelta += 128;

                                                float delta = (float)rawDelta / 127.0f;

                                                // Calculate time delta and velocity-based acceleration
                                                uint32_t dt = now - lastTime;
                                                if (dt == 0) dt = 1; // prevent division by zero

                                                float multiplier = 1.0f;
                                                if (dt < 100) {
                                                    // High-speed quadratic spin acceleration (half as aggressive)
                                                    float speedFactor = 100.0f / (float)dt;
                                                    multiplier = 1.0f + 0.5f * (speedFactor * speedFactor - 1.0f);
                                                    if (multiplier > 4.0f) multiplier = 4.0f; // cap max acceleration at 4.0f (half of 8.0)
                                                } else if (dt > 300) {
                                                    // Slow turn fine resolution adjustment (0.8x for less sluggish feel)
                                                    multiplier = 0.8f;
                                                }

                                                float paramDelta = delta * multiplier;
                                                if (data->ui->mSeqMidiKnobInverted[activeTrack][k]) {
                                                    paramDelta = -paramDelta;
                                                }

                                                float newNormVal = normVal + paramDelta;
                                                if (newNormVal < 0.0f) newNormVal = 0.0f;
                                                if (newNormVal > 1.0f) newNormVal = 1.0f;

                                                float scaledVal = data->ui->scaleParamFromNormalized(paramId, newNormVal);

                                                data->engine->setParameter(activeTrack, paramId, scaledVal);
                                                data->ui->mSeqMidiKnobValue[activeTrack][k] = newNormVal;
                                            } else {
                                                // First knob move: synchronize visual state to current soft param
                                                data->ui->mSeqMidiKnobValue[activeTrack][k] = data->ui->normalizeParamValue(paramId, paramVal);
                                            }
                                        }
                                        ccMatched = true;
                                        break;
                                    }
                                }
                            }
                            
                            // Check sliders/faders CCs
                            if (!ccMatched) {
                                for (int f = 0; f < data->ui->mSettingsSliderCount; ++f) {
                                    if (data->ui->mSeqMidiFaderCC[activeTrack][f] == cc) {
                                        int mappedCh = data->ui->mSeqMidiFaderChannel[activeTrack][f];
                                        if (mappedCh == 0 || mappedCh == (channel + 1)) {
                                            data->ui->mSeqMidiFaderValue[activeTrack][f] = floatVal;
                                            int paramId = data->ui->mSeqMidiFaderParam[activeTrack][f];
                                            if (paramId >= 0) {
                                                float finalVal = data->ui->mSeqMidiFaderInverted[activeTrack][f] ? (1.0f - floatVal) : floatVal;
                                                float scaledVal = data->ui->scaleParamFromNormalized(paramId, finalVal);
                                                data->engine->setParameter(activeTrack, paramId, scaledVal);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    b += 3;
                } else {
                    break;
                }
            } else if (messageType == 0xD0) { // Channel Pressure (Aftertouch)
                if (b + 1 < packet->length) {
                    uint8_t pressure = packet->data[b + 1];
                    float floatVal = pressure / 127.0f;
                    int activeTrack = data->ui->getActiveTrack();
                    
                    if (data->ui->mSettingsPadMode == 1 && data->ui->mLastActiveFxPadIdx != -1) {
                        // Modulate Send and Mix levels for the active FX pedal
                        int padIdx = data->ui->mLastActiveFxPadIdx;
                        int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                        data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), floatVal);
                        data->engine->updateEngineParameter(-1, 3000 + pedalIdx, floatVal);
                    } else {
                        data->engine->setPadMod(activeTrack, floatVal);
                    }
                    b += 2;
                } else {
                    break;
                }
            } else if (messageType == 0xA0) { // Polyphonic Pressure
                if (b + 2 < packet->length) {
                    uint8_t note = packet->data[b + 1];
                    uint8_t pressure = packet->data[b + 2];
                    float floatVal = pressure / 127.0f;
                    int activeTrack = data->ui->getActiveTrack();
                    
                    int padIdx = -1;
                    if (data->ui->mSettingsPadMode == 1) {
                        for (int i = 0; i < data->ui->mSettingsPadCount; ++i) {
                            if (note == data->ui->mSettingsPadNoteMap[i]) {
                                padIdx = i;
                                break;
                            }
                        }
                    }
                    if (padIdx >= 0) {
                        // Modulate Send and Mix levels for the specific FX pedal
                        int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                        data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), floatVal);
                        data->engine->updateEngineParameter(-1, 3000 + pedalIdx, floatVal);
                    } else {
                        data->engine->setPadMod(activeTrack, floatVal);
                    }
                    b += 3;
                } else {
                    break;
                }
            } else {
                if ((status & 0xC0) == 0x80 || (status & 0xC0) == 0x90 || (status & 0xC0) == 0xA0 || (status & 0xC0) == 0xE0) {
                    b += 3;
                } else if ((status & 0xD0) == 0xC0 || (status & 0xD0) == 0xD0) {
                    b += 2;
                } else {
                    b++;
                }
            }
        }
        packet = MIDIPacketNext(packet);
    }
}

static inline void setupMidiInput(MidiCallbackData* data) {
    gMidiCallbackData = *data;
    
    OSStatus err;
    err = MIDIClientCreate(CFSTR("LoomPi Client"), midiNotifyProc, &gMidiCallbackData, &gMidiClient);
    if (err != noErr) {
        std::cerr << "CoreMIDI: Failed to create client" << std::endl;
        return;
    }
    
    err = MIDIInputPortCreate(gMidiClient, CFSTR("LoomPi Input Port"), midiInputCallback, &gMidiCallbackData, &gMidiInputPort);
    if (err != noErr) {
        std::cerr << "CoreMIDI: Failed to create input port" << std::endl;
        return;
    }
    
    reconnectMidiSources();
    std::cout << "CoreMIDI: MIDI Input successfully initialized." << std::endl;
}

#else
// Real-time Linux ALSA Sequencer MIDI Input implementation
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>

static snd_seq_t* gSeq = nullptr;
static int gInPort = -1;
static pthread_t gMidiThread;
static bool gMidiThreadRunning = false;
static MidiCallbackData gMidiCallbackData = {nullptr, nullptr};

static void processMidiMessage(uint8_t status, uint8_t d1, uint8_t d2, MidiCallbackData* data) {
    if (!data || !data->engine || !data->ui) return;
    
    // Check for MIDI Real-Time messages
    if (status == 0xFA) { // Start / Play
        data->engine->setPlaying(true);
        return;
    } else if (status == 0xFC) { // Stop
        data->engine->setPlaying(false);
        data->engine->setIsRecording(false);
        return;
    } else if (status == 0xFB) { // Continue
        data->engine->setPlaying(true);
        return;
    }
    
    uint8_t messageType = status & 0xF0;
    uint8_t channel = status & 0x0F;
    
    if (messageType == 0x90) { // Note On
        uint8_t note = d1;
        uint8_t velocity = d2;

        if (data->ui->mPadLearnActive && data->ui->mPadLearnTarget >= 0 && data->ui->mPadLearnTarget < 24) {
            if (velocity > 0) {
                data->ui->mSettingsPadNoteMap[data->ui->mPadLearnTarget] = note;
                data->ui->mPadLearnActive = false;
                data->ui->mNeedsScreenRebuild = true;
            }
            return;
        }

        if (velocity > 0 && (data->ui->mEditingStepIdx != -1 || data->ui->mHeldStepIdx != -1)) {
            int stepIdx = (data->ui->mEditingStepIdx != -1) ? data->ui->mEditingStepIdx : data->ui->mHeldStepIdx;
            int activeTrack = data->ui->getActiveTrack();
            auto& track = data->engine->getTracks()[activeTrack];
            
            bool isDrum = (track.engineType == 5 || track.engineType == 6 || 
                          (track.engineType == 2 && track.samplerEngine.getPlayMode() >= 3));
            
            auto& step = isDrum ? track.drumSequencers[data->ui->mActiveDrumIdx].getStepsMutable()[stepIdx]
                                : track.sequencer.getStepsMutable()[stepIdx];
            
            if (data->ui->mStepMidiEntryCount == 0) {
                step.notes.clear();
                step.active = true;
            }
            step.addNote(note, velocity / 127.0f);
            data->ui->mStepMidiEntryCount++;
            data->ui->mNeedsScreenRebuild = true;
            return;
        }

        data->ui->addMidiLog(velocity > 0 ? "Note On" : "Note Off", channel + 1, note, velocity);
        int activeTrack = data->ui->getActiveTrack();
        int targetTrack = activeTrack;
        
        int padIdx = -1;
        if (data->ui->mSettingsPadMode != 0) {
            for (int i = 0; i < data->ui->mSettingsPadCount; ++i) {
                if (note == data->ui->mSettingsPadNoteMap[i]) {
                    padIdx = i;
                    break;
                }
            }
        }

        // Intercept pads as FX/Drum pad triggers ONLY if pad mode is not Keyboard (0)
        if (padIdx >= 0) {
            
            // Keep track of the most recently played FX pad for channel pressure routing
            if (data->ui->mSettingsPadMode == 1) {
                data->ui->mLastActiveFxPadIdx = padIdx;
            }
            
            if (data->ui->mSettingsPadMode == 3) { // FM Drum Mode
                int foundTrack = -1;
                for (int t = 0; t < 8; ++t) {
                    if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                }
                if (foundTrack == -1) {
                    for (int t = 0; t < 8; ++t) {
                        if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                    }
                }
                if (foundTrack != -1) targetTrack = foundTrack;
            } else if (data->ui->mSettingsPadMode == 4) { // Analogue Drum Mode
                int foundTrack = -1;
                for (int t = 0; t < 8; ++t) {
                    if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                }
                if (foundTrack == -1) {
                    for (int t = 0; t < 8; ++t) {
                        if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                    }
                }
                if (foundTrack != -1) targetTrack = foundTrack;
            }
            
            if (velocity > 0) {
                if (data->ui->mSettingsPadMode == 1) { // FX
                    if (data->ui->mSettingsFxPadMomentary) {
                        int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                        data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), 1.0f);
                    } else {
                        data->ui->mSettingsPadFxToggleState[padIdx] = !data->ui->mSettingsPadFxToggleState[padIdx];
                        int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                        data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), data->ui->mSettingsPadFxToggleState[padIdx] ? 1.0f : 0.0f);
                        data->ui->mNeedsScreenRebuild = true;
                    }
                } else if (data->ui->mSettingsPadMode == 2) { // Scales
                    int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                    int scaleIdx = data->ui->mSelectedScaleIdx;
                    // Build interval list from scale table
                    std::vector<int> intervals;
                    {
                        const int* row = kScaleIntervals[scaleIdx < kScaleCount ? scaleIdx : 1];
                        for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                        if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                    }
                    int numNotes = (int)intervals.size();
                    int baseNote = 60 + rootKey + data->ui->mSettingsOctaveOffset * 12;
                    int octShift = padIdx / numNotes;
                    int degIdx = padIdx % numNotes;
                    int noteToPlay = baseNote + octShift * 12 + intervals[degIdx];
                    data->engine->triggerNote(activeTrack, noteToPlay, velocity);
                } else if (data->ui->mSettingsPadMode == 3 || data->ui->mSettingsPadMode == 4) { // Drum
                    int drumIdx = data->ui->mSettingsPadDrumAssign[padIdx];
                    data->engine->triggerNote(targetTrack, 60 + drumIdx, velocity);
                } else if (data->ui->mSettingsPadMode == 5) { // Slices
                    int col = padIdx % 4;
                    int row = padIdx / 4;
                    int revPadIdx = (3 - row) * 4 + col;
                    int numSlices = 1;
                    std::vector<float> slicePoints = data->engine->getSamplerSlicePoints(activeTrack);
                    if (!slicePoints.empty()) {
                        numSlices = (int)slicePoints.size();
                    }
                    int drumIdx = revPadIdx % numSlices;
                    data->engine->triggerNote(activeTrack, 60 + drumIdx, velocity);
                }
            } else {
                // Note Off via velocity 0
                if (data->ui->mSettingsPadMode == 1) { // FX
                    if (data->ui->mSettingsFxPadMomentary) {
                        int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                        data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), 0.0f);
                    }
                } else if (data->ui->mSettingsPadMode == 2) { // Scales
                    int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                    int scaleIdx = data->ui->mSelectedScaleIdx;
                    // Build interval list from scale table
                    std::vector<int> intervals;
                    {
                        const int* row = kScaleIntervals[scaleIdx < kScaleCount ? scaleIdx : 1];
                        for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                        if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                    }
                    int numNotes = (int)intervals.size();
                    int baseNote = 60 + rootKey + data->ui->mSettingsOctaveOffset * 12;
                    int octShift = padIdx / numNotes;
                    int degIdx = padIdx % numNotes;
                    int noteToPlay = baseNote + octShift * 12 + intervals[degIdx];
                    data->engine->releaseNote(activeTrack, noteToPlay);
                } else if (data->ui->mSettingsPadMode == 3 || data->ui->mSettingsPadMode == 4) { // Drum
                    int drumIdx = data->ui->mSettingsPadDrumAssign[padIdx];
                    data->engine->releaseNote(targetTrack, 60 + drumIdx);
                } else if (data->ui->mSettingsPadMode == 5) { // Slices
                    int col = padIdx % 4;
                    int row = padIdx / 4;
                    int revPadIdx = (3 - row) * 4 + col;
                    int numSlices = 1;
                    std::vector<float> slicePoints = data->engine->getSamplerSlicePoints(activeTrack);
                    if (!slicePoints.empty()) {
                        numSlices = (int)slicePoints.size();
                    }
                    int drumIdx = revPadIdx % numSlices;
                    data->engine->releaseNote(activeTrack, 60 + drumIdx);
                }
            }
        } else {
            // Standard note: plays in active track, snaps to active scale if selected!
            if (velocity > 0) {
                int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                int scaleIdx = data->ui->mSelectedScaleIdx;
                int finalNote = note;
                if (scaleIdx > 0) {
                    finalNote = snapNoteToScale(note, scaleIdx, rootKey);
                }
                data->engine->triggerNote(activeTrack, finalNote, velocity);
            } else {
                // Note Off via velocity 0
                int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                int scaleIdx = data->ui->mSelectedScaleIdx;
                int finalNote = note;
                if (scaleIdx > 0) {
                    finalNote = snapNoteToScale(note, scaleIdx, rootKey);
                }
                data->engine->releaseNote(activeTrack, finalNote);
            }
        }
    } else if (messageType == 0x80) { // Note Off
        uint8_t note = d1;
        data->ui->addMidiLog("Note Off", channel + 1, note, d2);
        int activeTrack = data->ui->getActiveTrack();
        int targetTrack = activeTrack;
        
        int padIdx = -1;
        if (data->ui->mSettingsPadMode != 0) {
            for (int i = 0; i < data->ui->mSettingsPadCount; ++i) {
                if (note == data->ui->mSettingsPadNoteMap[i]) {
                    padIdx = i;
                    break;
                }
            }
        }

        // Intercept pads as FX/Drum pad triggers ONLY if pad mode is not Keyboard (0)
        if (padIdx >= 0) {
            
            // Clear the active FX pad index if it matches the one being released
            if (data->ui->mSettingsPadMode == 1 && data->ui->mLastActiveFxPadIdx == padIdx) {
                data->ui->mLastActiveFxPadIdx = -1;
            }
            
            if (data->ui->mSettingsPadMode == 3) { // FM Drum Mode
                int foundTrack = -1;
                for (int t = 0; t < 8; ++t) {
                    if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                }
                if (foundTrack == -1) {
                    for (int t = 0; t < 8; ++t) {
                        if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                    }
                }
                if (foundTrack != -1) targetTrack = foundTrack;
            } else if (data->ui->mSettingsPadMode == 4) { // Analogue Drum Mode
                int foundTrack = -1;
                for (int t = 0; t < 8; ++t) {
                    if (data->engine->getTracks()[t].engineType == 6) { foundTrack = t; break; }
                }
                if (foundTrack == -1) {
                    for (int t = 0; t < 8; ++t) {
                        if (data->engine->getTracks()[t].engineType == 5) { foundTrack = t; break; }
                    }
                }
                if (foundTrack != -1) targetTrack = foundTrack;
            }
            
            if (data->ui->mSettingsPadMode == 1) { // FX
                if (data->ui->mSettingsFxPadMomentary) {
                    int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
                    data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), 0.0f);
                }
            } else if (data->ui->mSettingsPadMode == 2) { // Scales
                int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
                int scaleIdx = data->ui->mSelectedScaleIdx;
                // Build interval list from scale table
                std::vector<int> intervals;
                {
                    const int* row = kScaleIntervals[scaleIdx < kScaleCount ? scaleIdx : 1];
                    for (int _i = 0; _i < 12 && row[_i] >= 0; ++_i) intervals.push_back(row[_i]);
                    if (intervals.empty()) intervals = {0,2,4,5,7,9,11};
                }
                int numNotes = (int)intervals.size();
                int baseNote = 60 + rootKey + data->ui->mSettingsOctaveOffset * 12;
                int octShift = padIdx / numNotes;
                int degIdx = padIdx % numNotes;
                int noteToPlay = baseNote + octShift * 12 + intervals[degIdx];
                data->engine->releaseNote(activeTrack, noteToPlay);
            } else if (data->ui->mSettingsPadMode == 3 || data->ui->mSettingsPadMode == 4) { // Drum
                int drumIdx = data->ui->mSettingsPadDrumAssign[padIdx];
                data->engine->releaseNote(targetTrack, 60 + drumIdx);
            } else if (data->ui->mSettingsPadMode == 5) { // Slices
                int col = padIdx % 4;
                int row = padIdx / 4;
                int revPadIdx = (3 - row) * 4 + col;
                int numSlices = 1;
                std::vector<float> slicePoints = data->engine->getSamplerSlicePoints(activeTrack);
                if (!slicePoints.empty()) {
                    numSlices = (int)slicePoints.size();
                }
                int drumIdx = revPadIdx % numSlices;
                data->engine->releaseNote(activeTrack, 60 + drumIdx);
            }
        } else {
            // Standard note: plays in active track, snaps to active scale if selected!
            int rootKey = data->ui->mSettingsRootDd ? lv_dropdown_get_selected(data->ui->mSettingsRootDd) : 0;
            int scaleIdx = data->ui->mSelectedScaleIdx;
            int finalNote = note;
            if (scaleIdx > 0) {
                finalNote = snapNoteToScale(note, scaleIdx, rootKey);
            }
            data->engine->releaseNote(activeTrack, finalNote);
        }
    } else if (messageType == 0xB0) { // CC (Control Change)
        uint8_t cc = d1;
        uint8_t val = d2;
        data->ui->addMidiLog("CC", channel + 1, cc, val);
        
        // Hardware transport & navigation CC buttons
        if (cc == data->ui->mCcPlay && cc == data->ui->mCcStop) {
            data->engine->setPlaying(val >= 64);
        } else if (cc == data->ui->mCcPlay && val >= 64) {
            data->engine->setPlaying(true);
        } else if (cc == data->ui->mCcStop && val >= 64) {
            data->engine->setPlaying(false);
        } else if (cc == data->ui->mCcRecord) {
            bool recState = (val >= 64);
            data->engine->setIsRecording(recState);
            if (recState && !data->engine->getIsPlaying()) {
                data->engine->setPlaying(true);
            }
        } else if (cc == data->ui->mCcClear) {
            if (val >= 64) {
                int activeTrack = data->ui->getActiveTrack();
                data->engine->clearSequencer(activeTrack);
            }
        } else if (cc == data->ui->mCcPrevTrack) {
            if (val >= 64) {
                int activeTrack = data->ui->getActiveTrack();
                int prevTrk = (activeTrack - 1 + 8) % 8;
                data->ui->setActiveTrack(prevTrk);
                data->ui->mNeedsScreenRebuild = true;
            }
        } else if (cc == data->ui->mCcNextTrack) {
            if (val >= 64) {
                int activeTrack = data->ui->getActiveTrack();
                int nextTrk = (activeTrack + 1) % 8;
                data->ui->setActiveTrack(nextTrk);
                data->ui->mNeedsScreenRebuild = true;
            }
        } else {
            // MIDI Learn or Dynamic CC mapping update
            int activeTrack = data->ui->getActiveTrack();
            float floatVal = val / 127.0f;

            if (data->ui->mMidiLearnActive && data->ui->mMidiLearnTargetParamId >= 0) {
                int targetParamId = data->ui->mMidiLearnTargetParamId;
                
                // Find if this CC is already mapped on the active track
                int foundIdx = -1;
                bool isKnob = true;
                for (int k = 0; k < data->ui->mSettingsKnobCount; ++k) {
                    if (data->ui->mSeqMidiKnobCC[activeTrack][k] == cc) {
                        int mappedCh = data->ui->mSeqMidiKnobChannel[activeTrack][k];
                        if (mappedCh == 0 || mappedCh == (channel + 1)) {
                            foundIdx = k;
                            isKnob = true;
                            break;
                        }
                    }
                }
                if (foundIdx == -1) {
                    for (int f = 0; f < data->ui->mSettingsSliderCount; ++f) {
                        if (data->ui->mSeqMidiFaderCC[activeTrack][f] == cc) {
                            int mappedCh = data->ui->mSeqMidiFaderChannel[activeTrack][f];
                            if (mappedCh == 0 || mappedCh == (channel + 1)) {
                                foundIdx = f;
                                isKnob = false;
                                break;
                            }
                        }
                    }
                }

                // If not found, assign it to the first unmapped knob or fader slot
                if (foundIdx == -1) {
                    for (int k = 0; k < data->ui->mSettingsKnobCount; ++k) {
                        if (data->ui->mSeqMidiKnobParam[activeTrack][k] == -1) {
                            foundIdx = k;
                            isKnob = true;
                            for (int t = 0; t < 8; ++t) {
                                data->ui->mSeqMidiKnobCC[t][k] = cc;
                                data->ui->mSeqMidiKnobChannel[t][k] = channel + 1;
                            }
                            break;
                        }
                    }
                }
                if (foundIdx == -1) {
                    for (int f = 0; f < data->ui->mSettingsSliderCount; ++f) {
                        if (data->ui->mSeqMidiFaderParam[activeTrack][f] == -1) {
                            foundIdx = f;
                            isKnob = false;
                            for (int t = 0; t < 8; ++t) {
                                data->ui->mSeqMidiFaderCC[t][f] = cc;
                                data->ui->mSeqMidiFaderChannel[t][f] = channel + 1;
                            }
                            break;
                        }
                    }
                }

                // If still not found, overwrite the first knob slot
                if (foundIdx == -1) {
                    foundIdx = 0;
                    isKnob = true;
                    for (int t = 0; t < 8; ++t) {
                        data->ui->mSeqMidiKnobCC[t][0] = cc;
                        data->ui->mSeqMidiKnobChannel[t][0] = channel + 1;
                    }
                }

                // Perform the assignment
                if (isKnob) {
                    data->ui->mSeqMidiKnobParam[activeTrack][foundIdx] = targetParamId;
                    data->ui->mSeqMidiKnobValue[activeTrack][foundIdx] = floatVal;
                    float scaledVal = data->ui->scaleParamFromNormalized(targetParamId, floatVal);
                    data->engine->setParameter(activeTrack, targetParamId, scaledVal);
                } else {
                    data->ui->mSeqMidiFaderParam[activeTrack][foundIdx] = targetParamId;
                    data->ui->mSeqMidiFaderValue[activeTrack][foundIdx] = floatVal;
                    float scaledVal = data->ui->scaleParamFromNormalized(targetParamId, floatVal);
                    data->engine->setParameter(activeTrack, targetParamId, scaledVal);
                }
                
                // Exit learn mode thread-safely
                data->ui->mMidiLearnActive = false;
                data->ui->mMidiLearnTargetParamId = -1;
                data->ui->mNeedsScreenRebuild = true;
            } else {
                bool ccMatched = false;
                
                // Check knobs CCs
                for (int k = 0; k < data->ui->mSettingsKnobCount; ++k) {
                    if (data->ui->mSeqMidiKnobCC[activeTrack][k] == cc) {
                        int mappedCh = data->ui->mSeqMidiKnobChannel[activeTrack][k];
                        if (mappedCh == 0 || mappedCh == (channel + 1)) {
                            int paramId = data->ui->mSeqMidiKnobParam[activeTrack][k];
                            if (paramId >= 0) {
                                uint32_t now = lv_tick_get();
                                uint8_t lastCcVal = data->ui->mLastKnobCcVal[activeTrack][k];
                                uint32_t lastTime = data->ui->mLastKnobMs[activeTrack][k];
                                bool isInit = data->ui->mKnobInitialized[activeTrack][k];

                                data->ui->mLastKnobCcVal[activeTrack][k] = val;
                                data->ui->mLastKnobMs[activeTrack][k] = now;
                                data->ui->mKnobInitialized[activeTrack][k] = true;

                                float paramVal = data->engine->getTracks()[activeTrack].parameters[paramId];

                                if (isInit) {
                                    float normVal = data->ui->normalizeParamValue(paramId, paramVal);

                                    // Calculate CC value delta
                                    int rawDelta = (int)val - (int)lastCcVal;
                                    
                                    // Handle wrap-arounds for endless encoders
                                    if (rawDelta > 64) rawDelta -= 128;
                                    else if (rawDelta < -64) rawDelta += 128;

                                    float delta = (float)rawDelta / 127.0f;

                                    // Calculate time delta and velocity-based acceleration
                                    uint32_t dt = now - lastTime;
                                    if (dt == 0) dt = 1; // prevent division by zero

                                    float multiplier = 1.0f;
                                    if (dt < 100) {
                                        // High-speed quadratic spin acceleration (half as aggressive)
                                        float speedFactor = 100.0f / (float)dt;
                                        multiplier = 1.0f + 0.5f * (speedFactor * speedFactor - 1.0f);
                                        if (multiplier > 4.0f) multiplier = 4.0f; // cap max acceleration at 4.0f (half of 8.0)
                                    } else if (dt > 300) {
                                        // Slow turn fine resolution adjustment (0.8x for less sluggish feel)
                                        multiplier = 0.8f;
                                    }

                                    float paramDelta = delta * multiplier;
                                    if (data->ui->mSeqMidiKnobInverted[activeTrack][k]) {
                                        paramDelta = -paramDelta;
                                    }

                                    float newNormVal = normVal + paramDelta;
                                    if (newNormVal < 0.0f) newNormVal = 0.0f;
                                    if (newNormVal > 1.0f) newNormVal = 1.0f;

                                    float scaledVal = data->ui->scaleParamFromNormalized(paramId, newNormVal);

                                    data->engine->setParameter(activeTrack, paramId, scaledVal);
                                    data->ui->mSeqMidiKnobValue[activeTrack][k] = newNormVal;
                                } else {
                                    // First knob move: synchronize visual state to current soft param
                                    data->ui->mSeqMidiKnobValue[activeTrack][k] = data->ui->normalizeParamValue(paramId, paramVal);
                                }
                            }
                            ccMatched = true;
                            break;
                        }
                    }
                }
                
                // Check sliders/faders CCs
                if (!ccMatched) {
                    for (int f = 0; f < data->ui->mSettingsSliderCount; ++f) {
                        if (data->ui->mSeqMidiFaderCC[activeTrack][f] == cc) {
                            int mappedCh = data->ui->mSeqMidiFaderChannel[activeTrack][f];
                            if (mappedCh == 0 || mappedCh == (channel + 1)) {
                                data->ui->mSeqMidiFaderValue[activeTrack][f] = floatVal;
                                int paramId = data->ui->mSeqMidiFaderParam[activeTrack][f];
                                if (paramId >= 0) {
                                    float finalVal = data->ui->mSeqMidiFaderInverted[activeTrack][f] ? (1.0f - floatVal) : floatVal;
                                    float scaledVal = data->ui->scaleParamFromNormalized(paramId, finalVal);
                                    data->engine->setParameter(activeTrack, paramId, scaledVal);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    } else if (messageType == 0xD0) { // Channel Pressure (Aftertouch)
        uint8_t pressure = d1;
        float floatVal = pressure / 127.0f;
        int activeTrack = data->ui->getActiveTrack();
        
        if (data->ui->mSettingsPadMode == 1 && data->ui->mLastActiveFxPadIdx != -1) {
            // Modulate Send and Mix levels for the active FX pedal
            int padIdx = data->ui->mLastActiveFxPadIdx;
            int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
            data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), floatVal);
            data->engine->updateEngineParameter(-1, 3000 + pedalIdx, floatVal);
        } else {
            data->engine->setPadMod(activeTrack, floatVal);
        }
    } else if (messageType == 0xA0) { // Polyphonic Pressure
        uint8_t note = d1;
        uint8_t pressure = d2;
        float floatVal = pressure / 127.0f;
        int activeTrack = data->ui->getActiveTrack();
        
        int padIdx = -1;
        if (data->ui->mSettingsPadMode == 1) {
            for (int i = 0; i < data->ui->mSettingsPadCount; ++i) {
                if (note == data->ui->mSettingsPadNoteMap[i]) {
                    padIdx = i;
                    break;
                }
            }
        }
        if (padIdx >= 0) {
            // Modulate Send and Mix levels for the specific FX pedal
            int pedalIdx = data->ui->mSettingsPadFxAssign[padIdx];
            data->engine->setParameter(activeTrack, 2000 + (pedalIdx * 10), floatVal);
            data->engine->updateEngineParameter(-1, 3000 + pedalIdx, floatVal);
        } else {
            data->engine->setPadMod(activeTrack, floatVal);
        }
    }
}

static void* alsaMidiThreadProc(void* arg) {
    MidiCallbackData* data = &gMidiCallbackData;
    snd_seq_event_t *ev = nullptr;
    
    std::cout << "ALSA Sequencer: Background MIDI listening thread started." << std::endl;
    
    while (gMidiThreadRunning) {
        int err = snd_seq_event_input(gSeq, &ev);
        if (err < 0) {
            usleep(10000); // Prevent CPU spin on error/interrupt
            continue;
        }
        if (!ev) continue;
        
        uint8_t status = 0;
        uint8_t d1 = 0;
        uint8_t d2 = 0;
        bool valid = false;
        
        switch (ev->type) {
            case SND_SEQ_EVENT_NOTEON:
                status = 0x90 | (ev->data.note.channel & 0x0F);
                d1 = ev->data.note.note;
                d2 = ev->data.note.velocity;
                valid = true;
                break;
            case SND_SEQ_EVENT_NOTEOFF:
                status = 0x80 | (ev->data.note.channel & 0x0F);
                d1 = ev->data.note.note;
                d2 = ev->data.note.velocity;
                valid = true;
                break;
            case SND_SEQ_EVENT_CONTROLLER:
                status = 0xB0 | (ev->data.control.channel & 0x0F);
                d1 = ev->data.control.param;
                d2 = ev->data.control.value;
                valid = true;
                break;
            case SND_SEQ_EVENT_CHANPRESS:
                status = 0xD0 | (ev->data.control.channel & 0x0F);
                d1 = ev->data.control.value;
                d2 = 0;
                valid = true;
                break;
            case SND_SEQ_EVENT_KEYPRESS:
                status = 0xA0 | (ev->data.note.channel & 0x0F);
                d1 = ev->data.note.note;
                d2 = ev->data.note.velocity;
                valid = true;
                break;
            case SND_SEQ_EVENT_START:
                status = 0xFA;
                valid = true;
                break;
            case SND_SEQ_EVENT_STOP:
                status = 0xFC;
                valid = true;
                break;
            case SND_SEQ_EVENT_CONTINUE:
                status = 0xFB;
                valid = true;
                break;
            default:
                break;
        }
        
        if (valid) {
            processMidiMessage(status, d1, d2, data);
        }
        
        snd_seq_free_event(ev);
    }
    
    return nullptr;
}

static pthread_t gMidiAutoConnectThread;

static void autoConnectAlsaSources(snd_seq_t* seq, int ourPort) {
    if (!seq || ourPort < 0) return;
    
    snd_seq_client_info_t *cinfo = nullptr;
    snd_seq_port_info_t *pinfo = nullptr;
    
    if (snd_seq_client_info_malloc(&cinfo) < 0) return;
    if (snd_seq_port_info_malloc(&pinfo) < 0) {
        snd_seq_client_info_free(cinfo);
        return;
    }
    
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == snd_seq_client_id(seq)) {
            continue; // Skip ourselves
        }
        
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned int capability = snd_seq_port_info_get_capability(pinfo);
            // Check if the port supports read events (is an output/sender)
            if ((capability & SND_SEQ_PORT_CAP_READ) && (capability & SND_SEQ_PORT_CAP_SUBS_READ)) {
                snd_seq_port_subscribe_t *sub = nullptr;
                if (snd_seq_port_subscribe_malloc(&sub) >= 0) {
                    snd_seq_addr_t sender, dest;
                    sender.client = client;
                    sender.port = snd_seq_port_info_get_port(pinfo);
                    dest.client = snd_seq_client_id(seq);
                    dest.port = ourPort;
                    
                    snd_seq_port_subscribe_set_sender(sub, &sender);
                    snd_seq_port_subscribe_set_dest(sub, &dest);
                    
                    snd_seq_subscribe_port(seq, sub);
                    snd_seq_port_subscribe_free(sub);
                }
            }
        }
    }
    
    snd_seq_port_info_free(pinfo);
    snd_seq_client_info_free(cinfo);
}

static void* alsaAutoConnectThreadProc(void* arg) {
    std::cout << "ALSA Sequencer: Background auto-connection thread started." << std::endl;
    while (gMidiThreadRunning) {
        autoConnectAlsaSources(gSeq, gInPort);
        sleep(2);
    }
    return nullptr;
}

static inline void setupMidiInput(MidiCallbackData* data) {
    gMidiCallbackData = *data;
    
    int err = snd_seq_open(&gSeq, "default", SND_SEQ_OPEN_INPUT, 0);
    if (err < 0) {
        std::cerr << "ALSA Sequencer: Failed to open sequencer. MIDI input disabled." << std::endl;
        return;
    }
    
    snd_seq_set_client_name(gSeq, "LoomPi");
    
    gInPort = snd_seq_create_simple_port(gSeq, "LoomPi Input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        
    if (gInPort < 0) {
        std::cerr << "ALSA Sequencer: Failed to create port." << std::endl;
        snd_seq_close(gSeq);
        gSeq = nullptr;
        return;
    }
    
    gMidiThreadRunning = true;
    err = pthread_create(&gMidiThread, nullptr, alsaMidiThreadProc, nullptr);
    if (err != 0) {
        std::cerr << "ALSA Sequencer: Failed to create background thread." << std::endl;
        snd_seq_close(gSeq);
        gSeq = nullptr;
        gMidiThreadRunning = false;
        return;
    }
    
    // Start the background auto-connection thread
    pthread_create(&gMidiAutoConnectThread, nullptr, alsaAutoConnectThreadProc, nullptr);
    
    std::cout << "ALSA Sequencer: Virtual MIDI input port initialized ('LoomPi:LoomPi Input')." << std::endl;
}
#endif

#endif // MIDI_INPUT_H
