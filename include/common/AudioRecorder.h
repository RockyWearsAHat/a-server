#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace AIO::Common {

/**
 * Generic audio recorder that captures SDL audio samples for WAV output.
 *
 * This class hooks into any SDL audio callback to duplicate samples for
 * recording. It's designed to be emulator-agnostic - just call RecordSamples()
 * from within your SDL audio callback.
 *
 * Usage:
 *   1. Create an AudioRecorder instance
 *   2. Call StartRecording(path) to begin capture
 *   3. In your SDL audio callback, call RecordSamples(buffer, numSamples)
 *   4. Call StopRecording() to finalize and write the WAV file
 *
 * The recorder handles sample rate, channels, and format automatically based
 * on what you configure or pass to RecordSamples().
 */
class AudioRecorder {
public:
  struct Config {
    int sampleRate = 32768;
    int channels = 2;
    int bitsPerSample = 16;
  };

  AudioRecorder() = default;
  explicit AudioRecorder(const Config &config);
  ~AudioRecorder();

  AudioRecorder(const AudioRecorder &) = delete;
  AudioRecorder &operator=(const AudioRecorder &) = delete;
  AudioRecorder(AudioRecorder &&) = delete;
  AudioRecorder &operator=(AudioRecorder &&) = delete;

  /**
   * Configure the recorder (sample rate, channels, bits per sample).
   * Can be called before StartRecording or updated dynamically.
   */
  void Configure(const Config &config);

  /**
   * Configure just the sample rate (commonly changes based on SDL device).
   */
  void SetSampleRate(int hz);

  /**
   * Start recording audio to the specified file path.
   * The file will be written as a WAV when StopRecording() is called.
   * Returns true if recording started successfully.
   */
  bool StartRecording(const std::string &path);

  /**
   * Stop recording and finalize the WAV file.
   * Returns true if the file was written successfully.
   */
  bool StopRecording();

  /**
   * Check if currently recording.
   */
  bool IsRecording() const;

  /**
   * Record audio samples from an SDL callback buffer.
   * Call this from within your SDL audio callback.
   *
   * @param samples Pointer to interleaved int16_t samples (stereo: L,R,L,R,...)
   * @param numStereoSamples Number of stereo sample pairs (not bytes!)
   */
  void RecordSamples(const int16_t *samples, int numStereoSamples);

  /**
   * Record raw bytes (for non-int16 formats or direct buffer copy).
   * @param data Raw audio data
   * @param bytes Number of bytes
   */
  void RecordRawBytes(const uint8_t *data, int bytes);

  /**
   * Get the current recording duration in seconds.
   */
  double GetRecordingDuration() const;

  /**
   * Get the total number of samples recorded.
   */
  uint64_t GetTotalSamplesRecorded() const;

  /**
   * Get the output file path (empty if not recording).
   */
  const std::string &GetOutputPath() const;

private:
  void WriteWAVHeader(std::ofstream &file, uint32_t dataSize);
  void FinalizeWAV();

  Config config_;
  std::string outputPath_;
  std::vector<int16_t> sampleBuffer_;
  mutable std::mutex mutex_;
  std::atomic<bool> recording_{false};
  std::atomic<uint64_t> totalSamples_{0};
};

/**
 * RAII helper for scoped recording sessions.
 * Automatically stops recording when destroyed.
 */
class ScopedAudioRecording {
public:
  ScopedAudioRecording(AudioRecorder &recorder, const std::string &path)
      : recorder_(recorder), started_(recorder.StartRecording(path)) {}

  ~ScopedAudioRecording() {
    if (started_ && recorder_.IsRecording()) {
      recorder_.StopRecording();
    }
  }

  bool Started() const { return started_; }

  ScopedAudioRecording(const ScopedAudioRecording &) = delete;
  ScopedAudioRecording &operator=(const ScopedAudioRecording &) = delete;

private:
  AudioRecorder &recorder_;
  bool started_;
};

} // namespace AIO::Common
