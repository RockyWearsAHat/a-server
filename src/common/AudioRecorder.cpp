#include "common/AudioRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace AIO::Common {

AudioRecorder::AudioRecorder(const Config &config) : config_(config) {}

AudioRecorder::~AudioRecorder() {
  if (recording_.load(std::memory_order_acquire)) {
    StopRecording();
  }
}

void AudioRecorder::Configure(const Config &config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

void AudioRecorder::SetSampleRate(int hz) {
  if (hz > 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = hz;
  }
}

bool AudioRecorder::StartRecording(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (recording_.load(std::memory_order_acquire)) {
    std::cerr << "[AudioRecorder] Already recording to " << outputPath_
              << std::endl;
    return false;
  }

  outputPath_ = path;
  sampleBuffer_.clear();
  sampleBuffer_.reserve(config_.sampleRate * config_.channels *
                        60); // Pre-alloc ~60 seconds
  totalSamples_.store(0, std::memory_order_release);
  recording_.store(true, std::memory_order_release);

  std::cout << "[AudioRecorder] Started recording to " << path << " ("
            << config_.sampleRate << " Hz, " << config_.channels << " ch)"
            << std::endl;

  return true;
}

bool AudioRecorder::StopRecording() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!recording_.load(std::memory_order_acquire)) {
    return false;
  }

  recording_.store(false, std::memory_order_release);

  FinalizeWAV();

  std::cout << "[AudioRecorder] Stopped recording. Total samples: "
            << totalSamples_.load() << " (" << GetRecordingDuration() << " s)"
            << std::endl;

  return true;
}

bool AudioRecorder::IsRecording() const {
  return recording_.load(std::memory_order_acquire);
}

void AudioRecorder::RecordSamples(const int16_t *samples,
                                  int numStereoSamples) {
  // Always update live metrics, regardless of recording state.
  FeedSamples(samples, numStereoSamples);

  if (!recording_.load(std::memory_order_acquire) || !samples ||
      numStereoSamples <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  int totalSamples = numStereoSamples * config_.channels;
  sampleBuffer_.insert(sampleBuffer_.end(), samples, samples + totalSamples);
  totalSamples_.fetch_add(numStereoSamples, std::memory_order_relaxed);
}

void AudioRecorder::FeedSamples(const int16_t *samples, int numStereoSamples) {
  if (!samples || numStereoSamples <= 0)
    return;

  // Window size = 1 second of stereo data at the configured rate.
  // Floor at 1024 to avoid divide-by-zero when sample rate is not yet set.
  const int windowSize = std::max(config_.sampleRate, 1024);

  // Accumulate into current window.
  // This runs on the SDL audio callback thread — no lock needed for writes;
  // only the snapshot publish below takes metrics_mutex_.
  for (int i = 0; i < numStereoSamples; ++i) {
    const int16_t L = samples[i * 2 + 0];
    const int16_t R = samples[i * 2 + 1];

    const double dL = static_cast<double>(L);
    const double dR = static_cast<double>(R);
    windSumSqLeft_ += dL * dL;
    windSumSqRight_ += dR * dR;

    const int absL = std::abs(static_cast<int>(L));
    const int absR = std::abs(static_cast<int>(R));
    if (absL > windPeakLeft_)
      windPeakLeft_ = absL;
    if (absR > windPeakRight_)
      windPeakRight_ = absR;

    // Silence: both channels below ~1.5% of full scale (512/32767)
    if (absL < 512 && absR < 512)
      windSilentCount_++;

    // Clipping: either channel within 7 of max (≥32760)
    if (absL >= 32760 || absR >= 32760)
      windClipCount_++;

    windSampleCount_++;
  }
  metricsActive_ = true;

  // Publish snapshot when a full window has been accumulated.
  if (windSampleCount_ >= windowSize) {
    AudioMetrics snap;
    snap.sampleRate = config_.sampleRate;
    snap.windowSamples = windSampleCount_;
    snap.active = true;

    const double rmsL = std::sqrt(windSumSqLeft_ / windSampleCount_) / 32767.0;
    const double rmsR = std::sqrt(windSumSqRight_ / windSampleCount_) / 32767.0;
    snap.rmsLeft = static_cast<float>(rmsL);
    snap.rmsRight = static_cast<float>(rmsR);

    const double rmsMax = std::max(rmsL, rmsR);
    snap.rmsDb =
        (rmsMax > 0.0) ? static_cast<float>(20.0 * std::log10(rmsMax)) : -96.0f;

    snap.silenceRatio = static_cast<float>(windSilentCount_) /
                        static_cast<float>(windSampleCount_);
    snap.clippingRatio = static_cast<float>(windClipCount_) /
                         static_cast<float>(windSampleCount_);
    snap.peakLeft = windPeakLeft_;
    snap.peakRight = windPeakRight_;

    {
      std::lock_guard<std::mutex> lk(metrics_mutex_);
      metrics_snapshot_ = snap;
    }

    // Reset window accumulators for the next window.
    windSumSqLeft_ = 0.0;
    windSumSqRight_ = 0.0;
    windPeakLeft_ = 0;
    windPeakRight_ = 0;
    windSilentCount_ = 0;
    windClipCount_ = 0;
    windSampleCount_ = 0;
  }
}

AudioRecorder::AudioMetrics AudioRecorder::GetMetrics() const {
  std::lock_guard<std::mutex> lk(metrics_mutex_);
  AudioMetrics snap = metrics_snapshot_;
  snap.sampleRate = config_.sampleRate;
  snap.active = metricsActive_;
  return snap;
}

void AudioRecorder::RecordRawBytes(const uint8_t *data, int bytes) {
  if (!recording_.load(std::memory_order_acquire) || !data || bytes <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  int numSamples = bytes / sizeof(int16_t);
  const int16_t *samples = reinterpret_cast<const int16_t *>(data);
  sampleBuffer_.insert(sampleBuffer_.end(), samples, samples + numSamples);
  totalSamples_.fetch_add(numSamples / config_.channels,
                          std::memory_order_relaxed);
}

double AudioRecorder::GetRecordingDuration() const {
  if (config_.sampleRate <= 0)
    return 0.0;
  return static_cast<double>(totalSamples_.load(std::memory_order_acquire)) /
         config_.sampleRate;
}

uint64_t AudioRecorder::GetTotalSamplesRecorded() const {
  return totalSamples_.load(std::memory_order_acquire);
}

const std::string &AudioRecorder::GetOutputPath() const { return outputPath_; }

void AudioRecorder::WriteWAVHeader(std::ofstream &file, uint32_t dataSize) {
  uint16_t audioFormat = 1; // PCM
  uint16_t numChannels = static_cast<uint16_t>(config_.channels);
  uint32_t sampleRate = static_cast<uint32_t>(config_.sampleRate);
  uint16_t bitsPerSample = static_cast<uint16_t>(config_.bitsPerSample);
  uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
  uint16_t blockAlign = numChannels * bitsPerSample / 8;
  uint32_t chunkSize = 36 + dataSize;

  // RIFF header
  file.write("RIFF", 4);
  file.write(reinterpret_cast<const char *>(&chunkSize), 4);
  file.write("WAVE", 4);

  // fmt subchunk
  file.write("fmt ", 4);
  uint32_t subchunk1Size = 16;
  file.write(reinterpret_cast<const char *>(&subchunk1Size), 4);
  file.write(reinterpret_cast<const char *>(&audioFormat), 2);
  file.write(reinterpret_cast<const char *>(&numChannels), 2);
  file.write(reinterpret_cast<const char *>(&sampleRate), 4);
  file.write(reinterpret_cast<const char *>(&byteRate), 4);
  file.write(reinterpret_cast<const char *>(&blockAlign), 2);
  file.write(reinterpret_cast<const char *>(&bitsPerSample), 2);

  // data subchunk
  file.write("data", 4);
  file.write(reinterpret_cast<const char *>(&dataSize), 4);
}

void AudioRecorder::FinalizeWAV() {
  if (outputPath_.empty() || sampleBuffer_.empty()) {
    std::cerr << "[AudioRecorder] No audio data to write" << std::endl;
    return;
  }

  std::ofstream file(outputPath_, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "[AudioRecorder] Failed to open " << outputPath_
              << " for writing" << std::endl;
    return;
  }

  uint32_t dataSize =
      static_cast<uint32_t>(sampleBuffer_.size() * sizeof(int16_t));

  WriteWAVHeader(file, dataSize);
  file.write(reinterpret_cast<const char *>(sampleBuffer_.data()), dataSize);
  file.close();

  std::cout << "[AudioRecorder] Wrote " << outputPath_ << " (" << dataSize
            << " bytes)" << std::endl;
}

} // namespace AIO::Common
