#include "emulator/windows/WindowsEmulator.h"
#include "emulator/windows/WinAPILayer.h"
#include "emulator/windows/WinMemory.h"
#include "emulator/windows/WinProcess.h"
#include "emulator/windows/X86_64Core.h"

#include <iostream>

namespace AIO::Emulator::Windows {

WindowsEmulator::WindowsEmulator()
    : framebuffer_(1280, 720, QImage::Format_ARGB32) {
  framebuffer_.fill(Qt::black);
  Reset();
}

WindowsEmulator::~WindowsEmulator() = default;

void WindowsEmulator::Reset() {
  state_ = WinEmulatorState::Idle;
  lastError_.clear();

  memory_ = std::make_unique<WinMemory>();
  winapi_ = std::make_unique<WinAPILayer>(*memory_);
  cpu_ = std::make_unique<X86_64Core>(*memory_, *winapi_);
  process_ = std::make_unique<WinProcess>(*memory_);

  winapi_->SetCPU(cpu_.get());
  framebuffer_.fill(Qt::black);
}

bool WindowsEmulator::LoadROM(const std::string &exePath) {
  Reset();

  // Wire the import resolver: when the PE loader encounters an imported
  // function, ask WinAPILayer for a stub address.
  memory_->SetImportResolver(
      [this](const std::string &dll, const std::string &func) -> uint64_t {
        return winapi_->Resolve(dll, func);
      });

  // Set the command-line so GetCommandLineA/W return something sane.
  winapi_->SetCommandLine(exePath);

  // Register all Win32/CRT/DirectX stubs.
  winapi_->Initialize();

  // Load the PE into memory, patch imports, apply relocations.
  const uint64_t entry = memory_->LoadPE(exePath, 0);
  if (entry == 0) {
    lastError_ = "Failed to load PE: " + exePath;
    state_ = WinEmulatorState::Crashed;
    return false;
  }

  if (!SetupProcess(exePath, entry)) {
    state_ = WinEmulatorState::Crashed;
    return false;
  }

  state_ = WinEmulatorState::Running;
  std::cout << "[WindowsEmulator] Loaded " << exePath << "  entry=0x"
            << std::hex << entry << std::dec << "\n";
  return true;
}

bool WindowsEmulator::LaunchSteamApp(const std::string &installPath,
                                     const std::string &exePath) {
  (void)installPath;
  return LoadROM(exePath);
}

bool WindowsEmulator::SetupProcess(const std::string &exePath,
                                   uint64_t entryPoint) {
  // Allocate the stack (WinMemory already reserved the region).
  constexpr uint64_t kStackTop = 0x7FFE0000;
  constexpr uint64_t kStackSize = 4 * 1024 * 1024;       // 4 MB
  memory_->Allocate(kStackTop - kStackSize, kStackSize); // map if not yet

  // Build PEB, TEB, process-parameters, environment, LDR data.
  constexpr uint64_t kImageBase = 0x1'4000'0000;
  constexpr uint64_t kHeapBase = 0x1'0000'0000;
  process_->Initialize(kImageBase, kHeapBase, kStackTop, kStackSize, exePath);

  // Set registers: RSP below the top (room for a return sentinel), RIP = entry.
  constexpr uint64_t kExitSentinel = 0xDEAD'BEEF'DEAD'BEEFull;

  // Plant an exit-sentinel return address on the stack so that when
  // the program's main() returns, ExecuteOne() will halt cleanly.
  cpu_->SetGPR(4, kStackTop - 8);                 // RSP
  memory_->Write64(kStackTop - 8, kExitSentinel); // [RSP] = sentinel
  cpu_->SetRIP(entryPoint);

  // Microsoft x64 ABI: shadow space (32 bytes) already below RSP for the
  // callee to spill its register arguments.  We leave it alone — the called
  // code will allocate what it needs via SUB RSP, imm.

  return true;
}

void WindowsEmulator::RunFrame() {
  if (state_ != WinEmulatorState::Running)
    return;

  const int executed = cpu_->Execute(kCyclesPerFrame);
  (void)executed;

  if (cpu_->IsFaulted()) {
    lastError_ = cpu_->GetFaultMessage();
    state_ = WinEmulatorState::Crashed;
    std::cerr << "[WindowsEmulator] Crashed: " << lastError_ << "\n";
  } else if (cpu_->IsHalted()) {
    state_ = WinEmulatorState::Exited;
    std::cout << "[WindowsEmulator] Process exited normally.\n";
  }
}

std::string WindowsEmulator::GetDebugInfo() const {
  if (cpu_)
    return cpu_->GetStateString();
  return "(no CPU)";
}

} // namespace AIO::Emulator::Windows
