#ifndef ROUTING_MATRIX_H
#define ROUTING_MATRIX_H

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

enum class ModSource {
  None,
  TrackOutput,
  LFO1,
  LFO2,
  LFO3,
  LFO4,
  LFO5,
  LFO6,
  Envelope,
  SidechainFollower,
  Macro1,
  Macro2,
  Macro3,
  Macro4,
  Macro5,
  Macro6,
  Macro7,
  Macro8,
  Aftertouch
};

enum class ModDestination {
  None,
  Volume,
  FilterCutoff,
  Pitch,
  WavetablePos,
  Parameter
};

struct RoutingEntry {
  int sourceTrack;
  ModSource source;
  ModDestination destination;
  int destParamId; // New field for Generic Parameter
  float amount;
};

// Max 8 tracks, Max 16 mods per track (plenty)
static const int MAX_TRACKS = 8;
static const int MAX_MODS = 16;

class RoutingMatrix {
public:
  void addConnection(int destTrack, RoutingEntry entry) {
    std::lock_guard<std::mutex> lock(mMatrixLock);
    if (destTrack < 0 || destTrack >= MAX_TRACKS)
      return;

    // Check for existing connection matching Source + Destination + ParamId
    int count = mCounts[destTrack];
    for (int i = 0; i < count; ++i) {
      if (mFastMatrix[destTrack][i].source == entry.source &&
          mFastMatrix[destTrack][i].destination == entry.destination &&
          mFastMatrix[destTrack][i].destParamId == entry.destParamId) {

        // Found match. Update or Remove?
        if (std::abs(entry.amount) < 0.001f) {
          // Remove by swapping with last element
          int last = count - 1;
          if (i != last) {
            mFastMatrix[destTrack][i] = mFastMatrix[destTrack][last];
          }
          mCounts[destTrack]--;
        } else {
          // Update amount
          mFastMatrix[destTrack][i].amount = entry.amount;
        }
        return;
      }
    }

    // No existing connection found. Used standard append if not removing.
    if (std::abs(entry.amount) < 0.001f) {
      return; // Don't add a new connection with 0 amount
    }

    if (count < MAX_MODS) {
      mFastMatrix[destTrack][count] = entry;
      mCounts[destTrack]++;
    }
  }

  void clearConnections(int destTrack) {
    std::lock_guard<std::mutex> lock(mMatrixLock);
    if (destTrack < 0 || destTrack >= MAX_TRACKS)
      return;
    mCounts[destTrack] = 0;
  }

  // Lock-free read for Audio Thread
  // Returns pair of pointer and size
  void getFastConnections(int destTrack, const RoutingEntry **outPtr,
                          int *outSize) {
    if (destTrack < 0 || destTrack >= MAX_TRACKS) {
      *outSize = 0;
      return;
    }
    *outPtr = mFastMatrix[destTrack];
    *outSize = mCounts[destTrack];
  }

  // Legacy / Safe helper for other threads if needed
  std::vector<RoutingEntry> getConnections(int destTrack) {
    std::lock_guard<std::mutex> lock(mMatrixLock);
    std::vector<RoutingEntry> vec;
    if (destTrack < 0 || destTrack >= MAX_TRACKS)
      return vec;
    int count = mCounts[destTrack];
    for (int i = 0; i < count; ++i) {
      vec.push_back(mFastMatrix[destTrack][i]);
    }
    return vec;
  }

private:
  std::mutex mMatrixLock;
  RoutingEntry mFastMatrix[MAX_TRACKS][MAX_MODS];
  std::atomic<int>
      mCounts[MAX_TRACKS]; // Default constructor is fine, but we should init

public:
  RoutingMatrix() {
    for (int i = 0; i < MAX_TRACKS; ++i)
      mCounts[i] = 0;
  }
};

#endif // ROUTING_MATRIX_H
