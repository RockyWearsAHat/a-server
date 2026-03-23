#pragma once

#include <QImage>
#include <memory>
#include <string>

namespace AIO::Emulator::Windows {

class WinMemory;
class X86_64Core;
class WinAPILayer;
class WinProcess;

enum class WinEmulatorState { Idle, Running, Suspended, Crashed, Exited };

class WindowsEmulator {
public:
  WindowsEmulator();
  ~WindowsEmulator();

  // Load a Windows PE executable (.exe) — mirrors SwitchEmulator::LoadROM
  bool LoadROM(const std::string &exePath);

  // Convenience entry for Steam games
  bool LaunchSteamApp(const std::string &installPath,
                      const std::string &exePath);

  // Run ~one video frame of CPU execution
  void RunFrame();

  // Tear down and re-initialize
  void Reset();

  // Current framebuffer (black until a DirectX surface hooks in)
  const QImage &GetFramebuffer() const { return framebuffer_; }

  WinEmulatorState GetState() const { return state_; }
  std::string GetDebugInfo() const;
  std::string GetLastError() const { return lastError_; }

private:
  std::unique_ptr<WinMemory> memory_;
  std::unique_ptr<WinAPILayer> winapi_;
  std::unique_ptr<WinProcess> process_;
  std::unique_ptr<X86_64Core> cpu_;

  WinEmulatorState state_ = WinEmulatorState::Idle;
  QImage framebuffer_;
  std::string lastError_;

  // ~3 GHz @ 60 fps ≈ 50M cycles per frame; use 1M for a safe initial value
  static constexpr int kCyclesPerFrame = 1'000'000;

  bool SetupProcess(const std::string &exePath, uint64_t entryPoint);
};

} // namespace AIO::Emulator::Windows
