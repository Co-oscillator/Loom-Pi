#ifndef CHORD_PROGRESSION_ENGINE_H
#define CHORD_PROGRESSION_ENGINE_H

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

enum class Complexity { SIMPLE = 0, COMPLEX = 1, COLTRANE = 2 };

class ChordProgressionEngine {
public:
  static std::vector<std::vector<int>>
  generateProgression(int rootNote, const std::vector<int> &scaleIntervals,
                      int mood, Complexity complexity,
                      const std::vector<int> &anchors) {
    // Use a fixed random seed based on settings to ensure consistency *until*
    // settings change, or just use time if we want it truly "different each
    // time" as requested.
    static std::mt19937 rng(std::random_device{}());

    int baselineRoot = rootNote;
    if (!anchors.empty()) {
      float avgAnchor = 0;
      for (int a : anchors) avgAnchor += a;
      avgAnchor /= anchors.size();
      int anchorOctave = (int)std::round(avgAnchor / 12.0f) * 12;
      if (anchorOctave < 24) anchorOctave = 24;
      if (anchorOctave > 96) anchorOctave = 96;
      baselineRoot = (rootNote % 12) + anchorOctave;
    }

    // 1. Get Roman Numeral sequence for mood with variation
    std::vector<int> degrees = getDegreesForMood(mood, rng);

    std::vector<std::vector<int>> progression;
    std::vector<int> lastChord;

    for (int i = 0; i < 8; ++i) {
      int degree = degrees[i];

      // Coltrane Tritone Substitution
      bool isTritoneSub = false;
      if (complexity == Complexity::COLTRANE && (i == 2 || i == 5 || i == 6)) {
        if (degree == 5) {
          isTritoneSub = true;
        }
      }

      // Major Third Rotation
      int coltraneShift = 0;
      if (complexity == Complexity::COLTRANE) {
        if (i >= 2 && i < 4)
          coltraneShift = 4;
        else if (i >= 4 && i < 6)
          coltraneShift = 8;
      }

      // Calculate Root for this step
      int scaleIdx = (degree - 1) % scaleIntervals.size();
      int baseRoot = baselineRoot + scaleIntervals[scaleIdx];
      if (isTritoneSub)
        baseRoot += 6;
      baseRoot += coltraneShift;

      // Generate Chord notes
      std::vector<int> chord =
          buildChord(baseRoot, scaleIntervals, complexity, mood);

      // Voice Leading
      if (!lastChord.empty()) {
        applyVoiceLeading(chord, lastChord);
      }

      // Multi-Anchor Logic
      if (!anchors.empty()) {
        applyMultiAnchor(chord, anchors);
      }

      progression.push_back(chord);
      lastChord = chord;
    }

    return progression;
  }

private:
  static std::vector<int> getDegreesForMood(int mood, std::mt19937 &rng) {
    // Mood Matrix with Variations
    auto pick = [&](const std::vector<std::vector<int>> &variations) {
      std::uniform_int_distribution<int> dist(0, variations.size() - 1);
      return variations[dist(rng)];
    };

    switch (mood) {
    case 0: // Calm
      return pick({{1, 4, 1, 6, 4, 2, 5, 1}, {1, 4, 1, 4, 6, 2, 4, 1}});
    case 1: // Happy
      return pick({{1, 5, 6, 4, 1, 2, 5, 1}, {1, 4, 5, 1, 6, 2, 5, 1}});
    case 2: // Sad
      return pick({{6, 3, 4, 1, 2, 6, 5, 6}, {6, 4, 1, 5, 6, 4, 2, 6}});
    case 3: // Spooky
      return pick({{1, 4, 2, 7, 1, 6, 5, 1}, {1, 2, 6, 7, 1, 4, 5, 1}});
    case 4: // Angry
      return pick({{1, 6, 7, 1, 2, 6, 5, 1}, {1, 2, 1, 6, 7, 6, 5, 1}});
    case 5: // Excited
      return pick({{1, 4, 5, 4, 6, 5, 1, 5}, {1, 6, 4, 5, 1, 4, 5, 1}});
    case 6: // Grandiose
      return pick({{1, 5, 6, 3, 4, 1, 4, 5}, {1, 6, 3, 4, 1, 5, 1, 5}});
    case 7: // Tense
      return pick({{7, 5, 2, 7, 5, 6, 7, 5}, {7, 2, 5, 7, 1, 2, 7, 5}});
    default:
      return {1, 4, 1, 4, 1, 4, 1, 4};
    }
  }

  static std::vector<int> buildChord(int root, const std::vector<int> &scale,
                                     Complexity complexity, int mood) {
    std::vector<int> notes;

    if (complexity == Complexity::COLTRANE) {
      std::vector<int> intervals;
      switch (mood) {
      case 0:
        intervals = {0, 5, 10, 14, 21};
        break;
      case 1:
        intervals = {0, 4, 6, 7, 14};
        break;
      case 3:
        intervals = {0, 3, 6, 11, 13};
        break;
      case 7:
        intervals = {0, 4, 10, 13, 15, 18};
        break;
      default:
        intervals = {0, 4, 7, 10, 14};
        break;
      }
      for (int interval : intervals)
        notes.push_back(root + interval);
      return notes;
    }

    int notesToStack = (complexity == Complexity::SIMPLE) ? 3 : 5;

    int rootIndexInScale = 0;
    int minDiff = 100;
    int octaveShift = (root / 12) * 12;

    for (int i = 0; i < scale.size(); ++i) {
      int d = std::abs((root % 12) - scale[i]);
      if (d < minDiff) {
        minDiff = d;
        rootIndexInScale = i;
      }
    }

    for (int i = 0; i < notesToStack; ++i) {
      int stepIdx = (rootIndexInScale + i * 2);
      int oct = (stepIdx / (int)scale.size()) * 12;
      int scaleNote = scale[stepIdx % scale.size()];
      notes.push_back(octaveShift + oct + scaleNote);
    }

    return notes;
  }

  static void applyVoiceLeading(std::vector<int> &chord,
                                const std::vector<int> &lastChord) {
    if (chord.empty() || lastChord.empty())
      return;
    float avgLast = 0;
    for (int n : lastChord)
      avgLast += n;
    avgLast /= lastChord.size();

    float currentAvg = 0;
    for (int n : chord)
      currentAvg += n;
    currentAvg /= chord.size();

    int shift = (int)std::round((avgLast - currentAvg) / 12.0f);
    for (int &n : chord)
      n += shift * 12;
  }

  static void applyMultiAnchor(std::vector<int> &chord,
                               const std::vector<int> &anchors) {
    // Incorporate all anchors into the chord
    // If an anchor (octave-agnostic) is not in the chord, replace a note with
    // it. We iterate through anchors and ensure their pitch classes are
    // represented.
    std::vector<int> chordPitchClasses;
    for (int n : chord)
      chordPitchClasses.push_back(n % 12);

    std::sort(chord.begin(), chord.end());

    int replacementIdx = chord.size() - 1; // Start replacing from top down
    for (int anchor : anchors) {
      int anchorPC = anchor % 12;
      bool found = false;
      for (int pc : chordPitchClasses) {
        if (pc == anchorPC) {
          found = true;
          break;
        }
      }

      if (!found && replacementIdx >= 0) {
        // Adjust anchor to be in the same octave range as the note it replaces
        int targetOctave = (chord[replacementIdx] / 12) * 12;
        chord[replacementIdx] = targetOctave + anchorPC;
        replacementIdx--;
      }
    }
    std::sort(chord.begin(), chord.end());
  }
};

#endif
