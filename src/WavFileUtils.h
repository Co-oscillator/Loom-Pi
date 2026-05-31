#ifndef WAV_FILE_UTILS_H
#define WAV_FILE_UTILS_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace WavFileUtils {

struct WavHeader {
  char riff[4] = {'R', 'I', 'F', 'F'};
  uint32_t fileSize;
  char wave[4] = {'W', 'A', 'V', 'E'};
  char fmt[4] = {'f', 'm', 't', ' '};
  uint32_t fmtSize = 16;
  uint16_t audioFormat = 1; // PCM
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char data[4] = {'d', 'a', 't', 'a'};
  uint32_t dataSize;
};

struct SliceChunk {
  char id[4] = {'s', 'l', 'c', 'e'};
  uint32_t size;
  uint32_t numSlices;
  // Followed by float slicePoints[numSlices]
};

inline void writeWav(const std::string &path, const std::vector<float> &data,
                     int sampleRate, int numChannels,
                     const std::vector<float> &slices) {
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open())
    return;

  uint32_t dataSize = data.size() * sizeof(int16_t);
  uint32_t sliceDataSize = sizeof(uint32_t) + slices.size() * sizeof(float);
  uint32_t sliceChunkSize = 8 + sliceDataSize; // Header (8) + Content

  WavHeader header;
  header.numChannels = (uint16_t)numChannels;
  header.sampleRate = (uint32_t)sampleRate;
  header.bitsPerSample = 16;
  header.byteRate = sampleRate * numChannels * 2;
  header.blockAlign = numChannels * 2;
  header.dataSize = dataSize;
  header.fileSize = 36 + dataSize + sliceChunkSize;

  file.write(reinterpret_cast<char *>(&header), sizeof(WavHeader));

  // Write Audio Data (Convert Float to Int16)
  for (float sample : data) {
    float clamped = std::max(-1.0f, std::min(1.0f, sample));
    int16_t s = static_cast<int16_t>(clamped * 32767.0f);
    file.write(reinterpret_cast<char *>(&s), sizeof(int16_t));
  }

  // Write Slice Chunk
  if (!slices.empty()) {
    SliceChunk sc;
    sc.size = sliceDataSize;
    sc.numSlices = (uint32_t)slices.size();
    file.write(sc.id, 4);
    file.write(reinterpret_cast<char *>(&sc.size), 4);
    file.write(reinterpret_cast<char *>(&sc.numSlices), 4);
    file.write(reinterpret_cast<const char *>(slices.data()),
               slices.size() * sizeof(float));
  }

  file.close();
}

inline bool loadWav(const std::string &path, std::vector<float> &outData,
                    int &outSampleRate, int &outNumChannels,
                    std::vector<float> &outSlices) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return false;

  WavHeader header;
  file.read(reinterpret_cast<char *>(&header), sizeof(WavHeader));

  if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
      std::strncmp(header.wave, "WAVE", 4) != 0) {
    return false;
  }

  outSampleRate = header.sampleRate;
  outNumChannels = header.numChannels;

  // Skip chunks until 'data'
  // Simplified parser (assumes standard structure logic, but robust enough to
  // skip junk) Wait, the header struct assumes fixed layout, which is dangerous
  // if 'fmt ' is larger or extra chunks exist before 'data'. Let's rewrite
  // strictly to parse chunks.

  file.seekg(12, std::ios::beg); // Skip RIFF WAVE

  uint32_t chunkId, chunkSize;
  bool foundData = false;
  int audioFormat = 1;

  // Need to loop chunks
  while (file.read(reinterpret_cast<char *>(&chunkId), 4)) {
    file.read(reinterpret_cast<char *>(&chunkSize), 4);

    // Check chunk type (Need endianness handling? Android is Little Endian
    // usually. WAV is Little Endian.) 'fmt ' = 0x20746D66 'data' = 0x61746164
    // 'slce' = 0x65636C73

    char id[5];
    std::memcpy(id, &chunkId, 4);
    id[4] = '\0';

    if (std::strncmp(id, "fmt ", 4) == 0) {
      // Read format details if needed, but we rely on simple PCM
      if (chunkSize >= 16) {
        uint16_t fmtTag, channels, bits;
        uint32_t sRate;
        file.read(reinterpret_cast<char *>(&fmtTag), 2);
        file.read(reinterpret_cast<char *>(&channels), 2);
        file.read(reinterpret_cast<char *>(&sRate), 4);
        file.ignore(6); // ByteRate, BlockAlign
        file.read(reinterpret_cast<char *>(&bits), 2);
        file.ignore(chunkSize - 16);

        outNumChannels = channels;
        outSampleRate = sRate;
        if (fmtTag != 1 && fmtTag != 3)
          return false; // Only PCM or IEEE Float
        audioFormat = fmtTag;
      } else {
        file.ignore(chunkSize);
      }
    } else if (std::strncmp(id, "data", 4) == 0) {
      foundData = true;
      if (audioFormat == 1) { // PCM (Int16)
        int numSamples = chunkSize / 2;
        outData.resize(numSamples);
        for (int i = 0; i < numSamples; ++i) {
          int16_t s;
          file.read(reinterpret_cast<char *>(&s), 2);
          outData[i] = s / 32767.0f;
        }
      } else if (audioFormat == 3) { // IEEE Float
        int numSamples = chunkSize / 4;
        outData.resize(numSamples);
        file.read(reinterpret_cast<char *>(outData.data()), chunkSize);
      }
      // Handle odd padding byte if size is odd? WAV standard says alignment to
      // 2 bytes, but chunk size tells truth. data chunk logic usually end of
      // stream for simple files, but metadata can follow.
    } else if (std::strncmp(id, "slce", 4) == 0) {
      uint32_t numSlices;
      file.read(reinterpret_cast<char *>(&numSlices), 4);
      outSlices.resize(numSlices);
      file.read(reinterpret_cast<char *>(outSlices.data()),
                numSlices * sizeof(float));
    } else {
      file.ignore(chunkSize);
    }
  }
  return foundData;
}
} // namespace WavFileUtils

#endif // WAV_FILE_UTILS_H
