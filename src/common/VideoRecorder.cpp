#include "common/VideoRecorder.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace AIO::Common {

// ============================================================================
// VideoRecorder
// ============================================================================

VideoRecorder::VideoRecorder(const Config &config) : config_(config) {}

VideoRecorder::~VideoRecorder() {
  if (recording_.load(std::memory_order_acquire)) {
    StopRecording();
  }
}

void VideoRecorder::Configure(const Config &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

void VideoRecorder::SetDimensions(int width, int height) {
  if (width > 0 && height > 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.width = width;
    config_.height = height;
  }
}

bool VideoRecorder::StartRecording(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (recording_.load(std::memory_order_acquire)) {
    std::cerr << "[VideoRecorder] Already recording to " << outputPath_
              << std::endl;
    return false;
  }

  outputPath_ = path;
  outputFile_.open(path, std::ios::binary);
  if (!outputFile_.is_open()) {
    std::cerr << "[VideoRecorder] Failed to open " << path << std::endl;
    return false;
  }

  totalFrames_.store(0, std::memory_order_release);
  recording_.store(true, std::memory_order_release);

  std::cout << "[VideoRecorder] Started recording to " << path << " ("
            << config_.width << "x" << config_.height << " @ " << config_.fps
            << " fps)" << std::endl;

  return true;
}

bool VideoRecorder::StopRecording() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!recording_.load(std::memory_order_acquire)) {
    return false;
  }

  recording_.store(false, std::memory_order_release);
  outputFile_.close();

  std::cout << "[VideoRecorder] Stopped recording. Total frames: "
            << totalFrames_.load() << " (" << GetRecordingDuration() << " s)"
            << std::endl;

  return true;
}

bool VideoRecorder::IsRecording() const {
  return recording_.load(std::memory_order_acquire);
}

void VideoRecorder::RecordFrame(const uint32_t *framebuffer) {
  if (!recording_.load(std::memory_order_acquire) || !framebuffer) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  const size_t frameSize = config_.width * config_.height * sizeof(uint32_t);
  outputFile_.write(reinterpret_cast<const char *>(framebuffer), frameSize);
  totalFrames_.fetch_add(1, std::memory_order_relaxed);
}

void VideoRecorder::RecordFrame(const std::vector<uint32_t> &framebuffer) {
  if (framebuffer.size() >=
      static_cast<size_t>(config_.width * config_.height)) {
    RecordFrame(framebuffer.data());
  }
}

double VideoRecorder::GetRecordingDuration() const {
  if (config_.fps <= 0)
    return 0.0;
  return static_cast<double>(totalFrames_.load(std::memory_order_acquire)) /
         config_.fps;
}

uint64_t VideoRecorder::GetTotalFramesRecorded() const {
  return totalFrames_.load(std::memory_order_acquire);
}

const std::string &VideoRecorder::GetOutputPath() const { return outputPath_; }

// ============================================================================
// AVRecorder
// ============================================================================

AVRecorder::AVRecorder(const Config &config) : config_(config) {}

AVRecorder::~AVRecorder() {
  if (recording_.load(std::memory_order_acquire)) {
    StopRecording();
  }
}

void AVRecorder::Configure(const Config &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

bool AVRecorder::StartRecording(const std::string &outputPath) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (recording_.load(std::memory_order_acquire)) {
    std::cerr << "[AVRecorder] Already recording" << std::endl;
    return false;
  }

  outputPath_ = outputPath;

  // Create temp files for raw video and audio
  std::filesystem::path outPath(outputPath);
  auto parentDir = outPath.parent_path();
  auto stem = outPath.stem().string();

  tempVideoPath_ = (parentDir / (stem + "_temp.raw")).string();
  tempAudioPath_ = (parentDir / (stem + "_temp.wav")).string();

  videoFile_.open(tempVideoPath_, std::ios::binary);
  if (!videoFile_.is_open()) {
    std::cerr << "[AVRecorder] Failed to open temp video file" << std::endl;
    return false;
  }

  audioFile_.open(tempAudioPath_, std::ios::binary);
  if (!audioFile_.is_open()) {
    std::cerr << "[AVRecorder] Failed to open temp audio file" << std::endl;
    videoFile_.close();
    return false;
  }

  // Write WAV header placeholder (we'll update it on stop)
  std::array<uint8_t, 44> wavHeader{};
  audioFile_.write(reinterpret_cast<const char *>(wavHeader.data()), 44);

  totalFrames_.store(0, std::memory_order_release);
  totalAudioSamples_.store(0, std::memory_order_release);
  recording_.store(true, std::memory_order_release);

  std::cout << "[AVRecorder] Started recording to " << outputPath << std::endl;
  std::cout << "  Video: " << config_.videoWidth << "x" << config_.videoHeight
            << " @ " << config_.videoFps << " fps" << std::endl;
  std::cout << "  Audio: " << config_.audioSampleRate << " Hz, "
            << config_.audioChannels << " ch" << std::endl;

  return true;
}

bool AVRecorder::StopRecording() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!recording_.load(std::memory_order_acquire)) {
    return false;
  }

  recording_.store(false, std::memory_order_release);

  // Close video file
  videoFile_.close();

  // Finalize WAV header and close audio file
  audioFile_.seekp(0);
  uint32_t audioDataSize =
      totalAudioSamples_.load() * config_.audioChannels * 2;
  uint16_t audioFormat = 1; // PCM
  uint16_t numChannels = static_cast<uint16_t>(config_.audioChannels);
  uint32_t sampleRate = static_cast<uint32_t>(config_.audioSampleRate);
  uint16_t bitsPerSample = 16;
  uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
  uint16_t blockAlign = numChannels * bitsPerSample / 8;
  uint32_t chunkSize = 36 + audioDataSize;

  audioFile_.write("RIFF", 4);
  audioFile_.write(reinterpret_cast<const char *>(&chunkSize), 4);
  audioFile_.write("WAVE", 4);
  audioFile_.write("fmt ", 4);
  uint32_t fmtSize = 16;
  audioFile_.write(reinterpret_cast<const char *>(&fmtSize), 4);
  audioFile_.write(reinterpret_cast<const char *>(&audioFormat), 2);
  audioFile_.write(reinterpret_cast<const char *>(&numChannels), 2);
  audioFile_.write(reinterpret_cast<const char *>(&sampleRate), 4);
  audioFile_.write(reinterpret_cast<const char *>(&byteRate), 4);
  audioFile_.write(reinterpret_cast<const char *>(&blockAlign), 2);
  audioFile_.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
  audioFile_.write("data", 4);
  audioFile_.write(reinterpret_cast<const char *>(&audioDataSize), 4);

  audioFile_.close();

  std::cout << "[AVRecorder] Stopped recording." << std::endl;
  std::cout << "  Frames: " << totalFrames_.load() << std::endl;
  std::cout << "  Audio samples: " << totalAudioSamples_.load() << std::endl;

  // Encode to final output
  bool ok = EncodeOutput();

  // Cleanup temp files
  std::filesystem::remove(tempVideoPath_);
  std::filesystem::remove(tempAudioPath_);

  return ok;
}

bool AVRecorder::IsRecording() const {
  return recording_.load(std::memory_order_acquire);
}

void AVRecorder::RecordVideoFrame(const uint32_t *framebuffer) {
  if (!recording_.load(std::memory_order_acquire) || !framebuffer) {
    return;
  }

  // Note: We don't lock here for performance - single producer assumed
  const size_t frameSize =
      config_.videoWidth * config_.videoHeight * sizeof(uint32_t);
  videoFile_.write(reinterpret_cast<const char *>(framebuffer), frameSize);
  totalFrames_.fetch_add(1, std::memory_order_relaxed);
}

void AVRecorder::RecordVideoFrame(const std::vector<uint32_t> &framebuffer) {
  if (framebuffer.size() >=
      static_cast<size_t>(config_.videoWidth * config_.videoHeight)) {
    RecordVideoFrame(framebuffer.data());
  }
}

void AVRecorder::RecordAudioSamples(const int16_t *samples,
                                    int numStereoSamples) {
  if (!recording_.load(std::memory_order_acquire) || !samples ||
      numStereoSamples <= 0) {
    return;
  }

  // Note: We don't lock here for performance - single producer assumed
  const size_t dataSize =
      numStereoSamples * config_.audioChannels * sizeof(int16_t);
  audioFile_.write(reinterpret_cast<const char *>(samples), dataSize);
  totalAudioSamples_.fetch_add(numStereoSamples, std::memory_order_relaxed);
}

double AVRecorder::GetRecordingDuration() const {
  if (config_.videoFps <= 0)
    return 0.0;
  return static_cast<double>(totalFrames_.load(std::memory_order_acquire)) /
         config_.videoFps;
}

uint64_t AVRecorder::GetTotalFrames() const {
  return totalFrames_.load(std::memory_order_acquire);
}

uint64_t AVRecorder::GetTotalAudioSamples() const {
  return totalAudioSamples_.load(std::memory_order_acquire);
}

bool AVRecorder::EncodeOutput() {
  // Use ffmpeg to combine raw video + WAV audio into final output
  // The framebuffer stores pixels as 0xAARRGGBB (32-bit ARGB)
  // On little-endian systems, bytes are stored as BB GG RR AA = bgra

  std::ostringstream cmd;
  cmd << "ffmpeg -y ";

  // Video input: raw BGRA frames (ARGB stored little-endian)
  cmd << "-f rawvideo ";
  cmd << "-pixel_format bgra ";
  cmd << "-video_size " << config_.videoWidth << "x" << config_.videoHeight
      << " ";
  cmd << "-framerate " << config_.videoFps << " ";
  cmd << "-i \"" << tempVideoPath_ << "\" ";

  // Audio input: WAV file
  cmd << "-i \"" << tempAudioPath_ << "\" ";

  // Output encoding
  cmd << "-c:v libx264 ";
  cmd << "-preset fast ";
  cmd << "-crf 18 ";
  cmd << "-pix_fmt yuv420p ";
  cmd << "-c:a aac ";
  cmd << "-b:a 192k ";
  cmd << "-shortest ";
  cmd << "\"" << outputPath_ << "\" ";
  cmd << "2>/dev/null";

  std::cout << "[AVRecorder] Encoding output with ffmpeg..." << std::endl;

  int result = std::system(cmd.str().c_str());

  if (result != 0) {
    std::cerr << "[AVRecorder] ffmpeg encoding failed (exit " << result << ")"
              << std::endl;
    std::cerr << "Command: " << cmd.str() << std::endl;
    return false;
  }

  if (std::filesystem::exists(outputPath_)) {
    auto size = std::filesystem::file_size(outputPath_);
    std::cout << "[AVRecorder] Created: " << outputPath_ << " (" << size
              << " bytes)" << std::endl;
    return true;
  }

  return false;
}

} // namespace AIO::Common
