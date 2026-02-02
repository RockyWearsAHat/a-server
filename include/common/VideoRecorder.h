#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace AIO::Common {

/**
 * Generic video recorder that captures raw framebuffer data for video output.
 *
 * This class captures frames directly from the emulator's framebuffer,
 * providing frame-accurate recording without relying on system screen capture.
 * Designed to work with AudioRecorder for perfectly synchronized A/V output.
 *
 * Output format: Raw ARGB32 frames written sequentially, or can be piped
 * to ffmpeg for encoding to common video formats.
 *
 * Usage:
 *   1. Create a VideoRecorder instance with frame dimensions
 *   2. Call StartRecording(path) to begin capture
 *   3. In your display update, call RecordFrame(framebuffer)
 *   4. Call StopRecording() to finalize
 */
class VideoRecorder {
public:
  struct Config {
    int width = 240;
    int height = 160;
    int fps = 60;
  };

  VideoRecorder() = default;
  explicit VideoRecorder(const Config &config);
  ~VideoRecorder();

  VideoRecorder(const VideoRecorder &) = delete;
  VideoRecorder &operator=(const VideoRecorder &) = delete;
  VideoRecorder(VideoRecorder &&) = delete;
  VideoRecorder &operator=(VideoRecorder &&) = delete;

  /**
   * Configure the recorder (dimensions, fps).
   */
  void Configure(const Config &config);

  /**
   * Set frame dimensions (commonly changes per emulator).
   */
  void SetDimensions(int width, int height);

  /**
   * Start recording video to the specified file path.
   * The file will contain raw ARGB32 frames.
   * Returns true if recording started successfully.
   */
  bool StartRecording(const std::string &path);

  /**
   * Stop recording and finalize the file.
   * Returns true if the file was written successfully.
   */
  bool StopRecording();

  /**
   * Check if currently recording.
   */
  bool IsRecording() const;

  /**
   * Record a single frame from the framebuffer.
   * Call this once per frame from your display update loop.
   *
   * @param framebuffer Pointer to ARGB32 pixel data (width * height pixels)
   */
  void RecordFrame(const uint32_t *framebuffer);

  /**
   * Record a frame from a vector (common emulator format).
   */
  void RecordFrame(const std::vector<uint32_t> &framebuffer);

  /**
   * Get the current recording duration in seconds.
   */
  double GetRecordingDuration() const;

  /**
   * Get the total number of frames recorded.
   */
  uint64_t GetTotalFramesRecorded() const;

  /**
   * Get the output file path (empty if not recording).
   */
  const std::string &GetOutputPath() const;

  /**
   * Get current configuration.
   */
  const Config &GetConfig() const { return config_; }

private:
  Config config_;
  std::string outputPath_;
  std::ofstream outputFile_;
  mutable std::mutex mutex_;
  std::atomic<bool> recording_{false};
  std::atomic<uint64_t> totalFrames_{0};
};

/**
 * Combined A/V recorder that synchronizes audio and video capture.
 * Uses internal frame/sample buffers to produce a single output file.
 */
class AVRecorder {
public:
  struct Config {
    int videoWidth = 240;
    int videoHeight = 160;
    int videoFps = 60;
    int audioSampleRate = 32768;
    int audioChannels = 2;
  };

  AVRecorder() = default;
  explicit AVRecorder(const Config &config);
  ~AVRecorder();

  AVRecorder(const AVRecorder &) = delete;
  AVRecorder &operator=(const AVRecorder &) = delete;

  void Configure(const Config &config);

  /**
   * Start recording A/V to the specified output path.
   * Creates temporary files for raw video/audio, combines on stop.
   */
  bool StartRecording(const std::string &outputPath);

  /**
   * Stop recording and encode final output.
   * Uses ffmpeg to combine raw video + audio into final format.
   */
  bool StopRecording();

  bool IsRecording() const;

  /**
   * Record a video frame.
   */
  void RecordVideoFrame(const uint32_t *framebuffer);
  void RecordVideoFrame(const std::vector<uint32_t> &framebuffer);

  /**
   * Record audio samples.
   */
  void RecordAudioSamples(const int16_t *samples, int numStereoSamples);

  double GetRecordingDuration() const;
  uint64_t GetTotalFrames() const;
  uint64_t GetTotalAudioSamples() const;

private:
  bool EncodeOutput();

  Config config_;
  std::string outputPath_;
  std::string tempVideoPath_;
  std::string tempAudioPath_;
  std::ofstream videoFile_;
  std::ofstream audioFile_;
  mutable std::mutex mutex_;
  std::atomic<bool> recording_{false};
  std::atomic<uint64_t> totalFrames_{0};
  std::atomic<uint64_t> totalAudioSamples_{0};
};

} // namespace AIO::Common
