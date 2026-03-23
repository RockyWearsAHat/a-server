#include "emulator/windows/WinAPILayer.h"
#include "emulator/windows/WinMemory.h"
#include "emulator/windows/X86_64Core.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>

namespace AIO::Emulator::Windows {

// ─── Constants ───────────────────────────────────────────────────────────────

// Well-known fake HANDLE values (must be non-zero, odd numbers are
// recognisable)
static constexpr uint64_t kHandleProcessSelf = 0xFFFF'FFFF'FFFF'FFFFull;
static constexpr uint64_t kHandleThreadSelf = 0xFFFF'FFFF'FFFF'FFFEull;
static constexpr uint64_t kHandleStdIn = 0x0000'0000'0000'00F0ull;
static constexpr uint64_t kHandleStdOut = 0x0000'0000'0000'00F4ull;
static constexpr uint64_t kHandleStdErr = 0x0000'0000'0000'00F8ull;
static constexpr uint32_t kINFINITE = 0xFFFF'FFFFu;
static constexpr uint64_t kE_NOTIMPL = 0x8000'4001ull; // HRESULT E_NOTIMPL
static constexpr uint64_t kSTATUS_SUCCESS = 0;
static constexpr uint64_t kPerfFreq =
    10'000'000ull; // 10 MHz (matches Windows QPC default)

// ─── Construction
// ─────────────────────────────────────────────────────────────

WinAPILayer::WinAPILayer(WinMemory &mem) : mem_(mem) {
  // Reserve handle slot 0 as invalid
  handles_.push_back({HT_INVALID, 0, false});
}

// ─── Handle table
// ─────────────────────────────────────────────────────────────

uint64_t WinAPILayer::AllocHandle(HandleType type, uint64_t data) {
  // Find free slot
  for (size_t i = 1; i < handles_.size(); ++i) {
    if (!handles_[i].valid) {
      handles_[i] = {type, data, true};
      return static_cast<uint64_t>(i) * 4; // HANDLE values are multiples of 4
    }
  }
  handles_.push_back({type, data, true});
  return static_cast<uint64_t>(handles_.size() - 1) * 4;
}

bool WinAPILayer::FreeHandle(uint64_t h) {
  const size_t idx = static_cast<size_t>(h / 4);
  if (idx >= handles_.size() || !handles_[idx].valid)
    return false;
  handles_[idx].valid = false;
  return true;
}

WinAPILayer::HandleEntry *WinAPILayer::GetHandle(uint64_t h) {
  const size_t idx = static_cast<size_t>(h / 4);
  if (idx >= handles_.size() || !handles_[idx].valid)
    return nullptr;
  return &handles_[idx];
}

// ─── StdIO helper
// ─────────────────────────────────────────────────────────────

FILE *WinAPILayer::GetFile(uint64_t h) {
  auto *entry = GetHandle(h);
  if (!entry || entry->type != HT_FILE)
    return nullptr;
  const size_t slot = static_cast<size_t>(entry->data);
  if (slot >= fileSlots_.size())
    return nullptr;
  return fileSlots_[slot];
}

// ─── Arg / ret helpers (Microsoft x64 ABI)
// ────────────────────────────────────
//   Arg 1: RCX  Arg 2: RDX  Arg 3: R8  Arg 4: R9
//   Arg 5+: [RSP + 0x20 + (n-5)*8]  (home-space is 4*8 = 0x20 bytes)
uint64_t WinAPILayer::Arg(int n) const {
  // n is 1-based
  switch (n) {
  case 1:
    return cpu_->GetGPR(1); // RCX
  case 2:
    return cpu_->GetGPR(2); // RDX
  case 3:
    return cpu_->GetGPR(8); // R8
  case 4:
    return cpu_->GetGPR(9); // R9
  default: {
    const uint64_t rsp = cpu_->GetRSP();
    // [RSP+8] = return address, [RSP+0x10..0x28] = home space,
    // [RSP+0x28 + (n-5)*8] = arg 5+
    return mem_.Read64(rsp + 0x20 + static_cast<uint64_t>(n - 1) * 8);
  }
  }
}

void WinAPILayer::Ret(uint64_t v) {
  cpu_->SetGPR(0, v); // RAX
}

// ─── Heap allocator ──────────────────────────────────────────────────────────

uint64_t WinAPILayer::HeapAlloc_(uint64_t size) {
  if (size == 0)
    size = 8;
  // Align to 16 bytes
  size = (size + 15) & ~uint64_t(15);

  // Try to reuse a freed block
  for (auto &b : heapBlocks_) {
    if (b.free && b.size >= size) {
      b.free = false;
      mem_.Write64(b.addr - 8, size); // store size header
      return b.addr;
    }
  }

  if (heapBase_ == 0) {
    heapBase_ = mem_.Allocate(WinMemory::kHeapBase, kHeapRegionSize,
                              WinMemory::PAGE_READWRITE);
    heapNext_ = heapBase_ + 16; // leave a small header gap
    processHeapHandle_ = 0x10001;
  }

  if (heapNext_ + size + 8 > heapBase_ + kHeapRegionSize) {
    std::cerr << "[WinAPI] Heap exhausted!\n";
    return 0;
  }

  uint64_t addr = heapNext_ + 8; // 8-byte header before each block
  mem_.Write64(heapNext_, size); // store block size
  heapNext_ = addr + size;

  heapBlocks_.push_back({addr, size, false});
  return addr;
}

void WinAPILayer::HeapFree_(uint64_t addr) {
  for (auto &b : heapBlocks_) {
    if (b.addr == addr) {
      b.free = true;
      return;
    }
  }
}

// ─── Stub registration
// ────────────────────────────────────────────────────────

uint64_t WinAPILayer::AddStub(const std::string &dll, const std::string &func,
                              APIFunc fn) {
  const std::string key = dll + "!" + func;
  auto it = nameToStub_.find(key);
  if (it != nameToStub_.end())
    return it->second;

  uint64_t addr = mem_.AllocStub();
  stubDispatch_[addr] = std::move(fn);
  nameToStub_[key] = addr;
  return addr;
}

// ─── Resolve (called by PE loader) ───────────────────────────────────────────

uint64_t WinAPILayer::Resolve(const std::string &dll, const std::string &func) {
  // Normalise DLL name: strip path + lowercase
  std::string dllKey = dll;
  const auto sep = dllKey.find_last_of("/\\");
  if (sep != std::string::npos)
    dllKey = dllKey.substr(sep + 1);
  for (auto &c : dllKey)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // Strip version suffix: "vcruntime140.dll" → search as-is, plus no suffix
  const std::string key = dllKey + "!" + func;
  auto it = nameToStub_.find(key);
  if (it != nameToStub_.end())
    return it->second;
  return 0;
}

// ─── Dispatch (called by CPU) ────────────────────────────────────────────────

bool WinAPILayer::Dispatch(uint64_t stubAddr) {
  auto it = stubDispatch_.find(stubAddr);
  if (it == stubDispatch_.end())
    return false;
  it->second();
  return true;
}

// ─── SetCommandLine ──────────────────────────────────────────────────────────

void WinAPILayer::SetCommandLine(const std::string &path) {
  // Allocate two small buffers in a scratch region
  uint64_t scratch = mem_.Allocate(0, 4096, WinMemory::PAGE_READWRITE);
  cmdLineAddrA_ = scratch;
  mem_.WriteStringA(cmdLineAddrA_, path);

  cmdLineAddrW_ = scratch + 2048;
  std::wstring wpath(path.begin(), path.end());
  mem_.WriteStringW(cmdLineAddrW_, wpath);
}

// ─── Initialize (register all Win32 stubs) ───────────────────────────────────

void WinAPILayer::Initialize() {
  // Save the ExitProcess stub address so we can plant it in the startup code
  exitStubAddr_ =
      AddStub("kernel32.dll", "ExitProcess", [this] { I_ExitProcess(); });

// Helper macros to reduce boilerplate
#define REG(dll, fn) AddStub(dll, #fn, [this] { I_##fn(); })
#define ALIAS(dll, fn, impl) AddStub(dll, #fn, [this] { I_##impl(); })

  // ── kernel32 ─────────────────────────────────────────────────────────
  REG("kernel32.dll", GetModuleHandleA);
  REG("kernel32.dll", GetModuleHandleW);
  REG("kernel32.dll", GetProcAddress);
  REG("kernel32.dll", LoadLibraryA);
  REG("kernel32.dll", LoadLibraryW);
  ALIAS("kernel32.dll", LoadLibraryExA, LoadLibraryA);
  ALIAS("kernel32.dll", LoadLibraryExW, LoadLibraryW);
  REG("kernel32.dll", FreeLibrary);
  REG("kernel32.dll", VirtualAlloc);
  REG("kernel32.dll", VirtualFree);
  REG("kernel32.dll", VirtualProtect);
  REG("kernel32.dll", CreateFileA);
  REG("kernel32.dll", CreateFileW);
  REG("kernel32.dll", ReadFile);
  REG("kernel32.dll", WriteFile);
  REG("kernel32.dll", CloseHandle);
  REG("kernel32.dll", GetSystemInfo);
  REG("kernel32.dll", GetLastError);
  REG("kernel32.dll", SetLastError);
  REG("kernel32.dll", InitializeCriticalSection);
  REG("kernel32.dll", InitializeCriticalSectionEx);
  ALIAS("kernel32.dll", InitializeCriticalSectionAndSpinCount,
        InitializeCriticalSection);
  REG("kernel32.dll", EnterCriticalSection);
  REG("kernel32.dll", LeaveCriticalSection);
  REG("kernel32.dll", DeleteCriticalSection);
  REG("kernel32.dll", TryEnterCriticalSection);
  REG("kernel32.dll", GetTickCount);
  REG("kernel32.dll", GetTickCount64);
  REG("kernel32.dll", Sleep);
  REG("kernel32.dll", SleepEx);
  REG("kernel32.dll", QueryPerformanceCounter);
  REG("kernel32.dll", QueryPerformanceFrequency);
  REG("kernel32.dll", HeapAlloc);
  REG("kernel32.dll", HeapFree);
  REG("kernel32.dll", HeapCreate);
  REG("kernel32.dll", HeapReAlloc);
  REG("kernel32.dll", HeapSize);
  REG("kernel32.dll", HeapDestroy);
  REG("kernel32.dll", LocalAlloc);
  REG("kernel32.dll", LocalFree);
  REG("kernel32.dll", LocalReAlloc);
  REG("kernel32.dll", GlobalAlloc);
  REG("kernel32.dll", GlobalFree);
  REG("kernel32.dll", GetProcessHeap);
  REG("kernel32.dll", GetCurrentProcessId);
  REG("kernel32.dll", GetCurrentThreadId);
  REG("kernel32.dll", GetCurrentProcess);
  REG("kernel32.dll", GetCurrentThread);
  REG("kernel32.dll", IsDebuggerPresent);
  REG("kernel32.dll", CheckRemoteDebuggerPresent);
  REG("kernel32.dll", OutputDebugStringA);
  REG("kernel32.dll", OutputDebugStringW);
  REG("kernel32.dll", GetCommandLineA);
  REG("kernel32.dll", GetCommandLineW);
  REG("kernel32.dll", GetEnvironmentVariableA);
  REG("kernel32.dll", GetEnvironmentVariableW);
  REG("kernel32.dll", GetStdHandle);
  REG("kernel32.dll", SetConsoleMode);
  REG("kernel32.dll", GetConsoleMode);
  REG("kernel32.dll", SetConsoleTitleA);
  REG("kernel32.dll", SetConsoleTitleW);
  REG("kernel32.dll", AllocConsole);
  REG("kernel32.dll", FlushFileBuffers);
  REG("kernel32.dll", SetFilePointer);
  REG("kernel32.dll", SetFilePointerEx);
  REG("kernel32.dll", GetFileSize);
  REG("kernel32.dll", GetFileSizeEx);
  REG("kernel32.dll", CreateMutexA);
  REG("kernel32.dll", CreateMutexW);
  REG("kernel32.dll", ReleaseMutex);
  REG("kernel32.dll", CreateEventA);
  REG("kernel32.dll", CreateEventW);
  REG("kernel32.dll", SetEvent);
  REG("kernel32.dll", ResetEvent);
  REG("kernel32.dll", WaitForSingleObject);
  REG("kernel32.dll", WaitForSingleObjectEx);
  REG("kernel32.dll", WaitForMultipleObjects);
  REG("kernel32.dll", CreateThread);
  REG("kernel32.dll", ExitThread);
  REG("kernel32.dll", GetThreadId);
  REG("kernel32.dll", SetThreadPriority);
  REG("kernel32.dll", GetThreadPriority);
  REG("kernel32.dll", ResumeThread);
  REG("kernel32.dll", SuspendThread);
  REG("kernel32.dll", TerminateThread);
  REG("kernel32.dll", GetSystemTimeAsFileTime);
  REG("kernel32.dll", GetLocalTime);
  REG("kernel32.dll", GetSystemTime);
  REG("kernel32.dll", FileTimeToSystemTime);
  REG("kernel32.dll", SystemTimeToFileTime);
  REG("kernel32.dll", GetFileAttributesA);
  REG("kernel32.dll", GetFileAttributesW);
  REG("kernel32.dll", SetFileAttributesA);
  REG("kernel32.dll", CreateDirectoryA);
  REG("kernel32.dll", CreateDirectoryW);
  REG("kernel32.dll", DeleteFileA);
  REG("kernel32.dll", DeleteFileW);
  REG("kernel32.dll", MoveFileA);
  REG("kernel32.dll", CopyFileA);
  REG("kernel32.dll", FindFirstFileA);
  REG("kernel32.dll", FindFirstFileW);
  REG("kernel32.dll", FindNextFileA);
  REG("kernel32.dll", FindNextFileW);
  REG("kernel32.dll", FindClose);
  REG("kernel32.dll", GetFullPathNameA);
  REG("kernel32.dll", GetFullPathNameW);
  REG("kernel32.dll", GetTempPathA);
  REG("kernel32.dll", GetTempPathW);
  REG("kernel32.dll", GetModuleFileNameA);
  REG("kernel32.dll", GetModuleFileNameW);
  REG("kernel32.dll", SetUnhandledExceptionFilter);
  REG("kernel32.dll", UnhandledExceptionFilter);
  REG("kernel32.dll", RaiseException);
  REG("kernel32.dll", TerminateProcess);
  REG("kernel32.dll", GetSystemDirectoryA);
  REG("kernel32.dll", GetSystemDirectoryW);
  REG("kernel32.dll", GetWindowsDirectoryA);
  REG("kernel32.dll", GetWindowsDirectoryW);
  REG("kernel32.dll", ExpandEnvironmentStringsA);
  REG("kernel32.dll", MultiByteToWideChar);
  REG("kernel32.dll", WideCharToMultiByte);
  REG("kernel32.dll", FormatMessageA);
  REG("kernel32.dll", FormatMessageW);
  REG("kernel32.dll", InterlockedIncrement);
  REG("kernel32.dll", InterlockedDecrement);
  REG("kernel32.dll", InterlockedExchange);
  REG("kernel32.dll", InterlockedCompareExchange);
  REG("kernel32.dll", InterlockedAdd);
  REG("kernel32.dll", CreateFileMappingA);
  REG("kernel32.dll", CreateFileMappingW);
  REG("kernel32.dll", MapViewOfFile);
  REG("kernel32.dll", UnmapViewOfFile);
  REG("kernel32.dll", GetNativeSystemInfo);
  REG("kernel32.dll", IsWow64Process);
  REG("kernel32.dll", InitOnceExecuteOnce);

  // ── ntdll ─────────────────────────────────────────────────────────────
  REG("ntdll.dll", NtAllocateVirtualMemory);
  REG("ntdll.dll", NtFreeVirtualMemory);
  REG("ntdll.dll", NtProtectVirtualMemory);
  REG("ntdll.dll", NtCreateFile);
  REG("ntdll.dll", NtReadFile);
  REG("ntdll.dll", NtWriteFile);
  REG("ntdll.dll", NtClose);
  REG("ntdll.dll", NtQueryInformationProcess);
  REG("ntdll.dll", NtQuerySystemInformation);
  REG("ntdll.dll", NtTerminateProcess);
  REG("ntdll.dll", NtTerminateThread);
  ALIAS("ntdll.dll", NtSetInformationThread, SetThreadPriority);
  REG("ntdll.dll", RtlAllocateHeap);
  REG("ntdll.dll", RtlFreeHeap);
  REG("ntdll.dll", RtlReAllocateHeap);
  REG("ntdll.dll", RtlSizeHeap);
  REG("ntdll.dll", RtlInitializeCriticalSection);
  REG("ntdll.dll", RtlInitializeCriticalSectionEx);
  ALIAS("ntdll.dll", RtlInitializeCriticalSectionAndSpinCount,
        RtlInitializeCriticalSectionEx);
  REG("ntdll.dll", RtlEnterCriticalSection);
  REG("ntdll.dll", RtlLeaveCriticalSection);
  REG("ntdll.dll", RtlDeleteCriticalSection);
  REG("ntdll.dll", RtlTryCriticalSection);
  REG("ntdll.dll", LdrLoadDll);
  REG("ntdll.dll", LdrGetDllHandleByName);
  REG("ntdll.dll", LdrGetProcedureAddress);
  REG("ntdll.dll", RtlExitUserProcess);
  REG("ntdll.dll", RtlGetVersion);
  REG("ntdll.dll", NtQueryVirtualMemory);
  REG("ntdll.dll", RtlCaptureStackBackTrace);
  REG("ntdll.dll", RtlRandom);
  REG("ntdll.dll", RtlUniform);

  // ── msvcrt / ucrtbase / vcruntime ─────────────────────────────────────
  for (const char *lib :
       {"msvcrt.dll", "ucrtbase.dll", "vcruntime140.dll", "vcruntime140d.dll",
        "msvcr120.dll", "msvcr110.dll", "ucrtbased.dll",
        "api-ms-win-crt-heap-l1-1-0.dll", "api-ms-win-crt-runtime-l1-1-0.dll",
        "api-ms-win-crt-string-l1-1-0.dll", "api-ms-win-crt-stdio-l1-1-0.dll",
        "api-ms-win-crt-math-l1-1-0.dll", "api-ms-win-crt-time-l1-1-0.dll",
        "api-ms-win-crt-filesystem-l1-1-0.dll",
        "api-ms-win-crt-environment-l1-1-0.dll",
        "api-ms-win-crt-locale-l1-1-0.dll", "api-ms-win-crt-convert-l1-1-0.dll",
        "msvcp140.dll", "msvcp140d.dll"}) {
    std::string l = lib;
    REG(l, malloc);
    REG(l, free);
    REG(l, realloc);
    REG(l, calloc);
    REG(l, printf);
    REG(l, fprintf);
    REG(l, sprintf);
    REG(l, snprintf);
    REG(l, sscanf);
    REG(l, vprintf);
    REG(l, vsprintf);
    REG(l, vsnprintf);
    REG(l, strlen);
    REG(l, wcslen);
    REG(l, strcpy);
    REG(l, strncpy);
    REG(l, strcat);
    REG(l, strncat);
    REG(l, strcmp);
    REG(l, strncmp);
    REG(l, strdup);
    REG(l, strchr);
    REG(l, strstr);
    REG(l, strtod);
    REG(l, strtol);
    REG(l, strtoul);
    REG(l, memcpy);
    REG(l, memmove);
    REG(l, memset);
    REG(l, memcmp);
    REG(l, memchr);
    REG(l, atoi);
    REG(l, atol);
    REG(l, atof);
    REG(l, itoa);
    REG(l, exit);
    REG(l, abort);
    REG(l, _exit);
    REG(l, rand);
    REG(l, srand);
    REG(l, time);
    REG(l, clock);
    REG(l, difftime);
    REG(l, fopen);
    REG(l, fclose);
    REG(l, fread);
    REG(l, fwrite);
    REG(l, fseek);
    REG(l, ftell);
    REG(l, feof);
    REG(l, fflush);
    REG(l, fgets);
    REG(l, fputs);
    REG(l, fputc);
    REG(l, fgetc);
    REG(l, getc);
    REG(l, putc);
    REG(l, puts);
    REG(l, gets);
    REG(l, clearerr);
    REG(l, ferror);
    REG(l, rewind);
    REG(l, _fileno);
    REG(l, _CRT_INIT);
    REG(l, __CxxFrameHandler3);
    REG(l, __C_specific_handler);
    REG(l, __chkstk);
    REG(l, __security_cookie);
    REG(l, __security_check_cookie);
    REG(l, _get_initial_narrow_environment);
    REG(l, _configure_narrow_argv);
    REG(l, _initialize_narrow_environment);
    REG(l, _cexit);
    REG(l, _conexit);
    REG(l, _initterm);
    REG(l, _initterm_e);
    REG(l, _register_onexit_function);
    REG(l, _execute_onexit_table);
    REG(l, _crt_atexit);
    REG(l, atexit);
    REG(l, fesetenv);
    REG(l, fegetenv);
    REG(l, _controlfp_s);
    REG(l, _set_app_type);
  }

  // ── user32 ────────────────────────────────────────────────────────────
  REG("user32.dll", MessageBoxA);
  REG("user32.dll", MessageBoxW);
  REG("user32.dll", CreateWindowExA);
  REG("user32.dll", CreateWindowExW);
  REG("user32.dll", RegisterClassExA);
  REG("user32.dll", RegisterClassExW);
  REG("user32.dll", ShowWindow);
  REG("user32.dll", UpdateWindow);
  REG("user32.dll", DefWindowProcA);
  REG("user32.dll", DefWindowProcW);
  REG("user32.dll", GetMessageA);
  REG("user32.dll", GetMessageW);
  REG("user32.dll", PeekMessageA);
  REG("user32.dll", PeekMessageW);
  REG("user32.dll", TranslateMessage);
  REG("user32.dll", DispatchMessageA);
  REG("user32.dll", DispatchMessageW);
  REG("user32.dll", PostQuitMessage);
  REG("user32.dll", DestroyWindow);
  REG("user32.dll", SetWindowTextA);
  REG("user32.dll", SetWindowTextW);
  REG("user32.dll", GetClientRect);
  REG("user32.dll", GetWindowRect);
  REG("user32.dll", AdjustWindowRect);
  REG("user32.dll", AdjustWindowRectEx);
  REG("user32.dll", ClientToScreen);
  REG("user32.dll", ScreenToClient);
  REG("user32.dll", SetCursor);
  REG("user32.dll", ShowCursor);
  REG("user32.dll", SetCapture);
  REG("user32.dll", ReleaseCapture);
  REG("user32.dll", GetCursorPos);
  REG("user32.dll", SetCursorPos);
  REG("user32.dll", SendMessageA);
  REG("user32.dll", SendMessageW);
  REG("user32.dll", PostMessageA);
  REG("user32.dll", PostMessageW);
  REG("user32.dll", LoadCursorA);
  REG("user32.dll", LoadCursorW);
  REG("user32.dll", LoadIconA);
  REG("user32.dll", LoadIconW);
  REG("user32.dll", FindWindowA);
  REG("user32.dll", FindWindowW);
  REG("user32.dll", SetForegroundWindow);
  REG("user32.dll", GetForegroundWindow);
  REG("user32.dll", IsWindowVisible);
  REG("user32.dll", EnableWindow);
  REG("user32.dll", SetWindowLongPtrA);
  REG("user32.dll", SetWindowLongPtrW);
  REG("user32.dll", GetWindowLongPtrA);
  REG("user32.dll", GetWindowLongPtrW);

  // ── DirectX stubs ────────────────────────────────────────────────────
  REG("d3d9.dll", Direct3DCreate9);
  ALIAS("d3d9.dll", Direct3DCreate9Ex, Direct3DCreate9);
  REG("d3d11.dll", D3D11CreateDevice);
  REG("d3d11.dll", D3D11CreateDeviceAndSwapChain);

  // ── XInput ───────────────────────────────────────────────────────────
  for (const char *lib :
       {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
    std::string l = lib;
    REG(l, XInputGetState);
    REG(l, XInputSetState);
    REG(l, XInputGetCapabilities);
    REG(l, XInputEnable);
  }

  // ── ole32 ────────────────────────────────────────────────────────────
  REG("ole32.dll", CoInitialize);
  REG("ole32.dll", CoInitializeEx);
  REG("ole32.dll", CoUninitialize);
  REG("ole32.dll", CoCreateInstance);

#undef REG
#undef ALIAS
}

// ─────────────────────────────────────────────────────────────────────────────
// Implementations
// ─────────────────────────────────────────────────────────────────────────────

void WinAPILayer::I_ExitProcess() {
  const uint32_t code = static_cast<uint32_t>(Arg(1));
  std::cout << "[WinAPI] ExitProcess(" << code << ")\n";
  cpu_->SetRIP(0xDEAD'BEEF'DEAD'BEEFull); // signals halted
  Ret(0);
}
void WinAPILayer::I_GetModuleHandleA() {
  const uint64_t nameAddr = Arg(1);
  if (nameAddr == 0) {
    // Return handle to EXE itself
    const auto &mods = mem_.GetModules();
    Ret(mods.empty() ? 0 : mods.front().base);
    return;
  }
  const std::string name = mem_.ReadStringA(nameAddr);
  Ret(mem_.GetModuleBase(name));
}
void WinAPILayer::I_GetModuleHandleW() { I_GetModuleHandleA(); }
void WinAPILayer::I_GetProcAddress() {
  const uint64_t hmod = Arg(1);
  const uint64_t fnSym = Arg(2);
  const std::string func = mem_.ReadStringA(fnSym);
  // Search all registered DLL stubs
  for (auto &[key, addr] : nameToStub_) {
    const auto bang = key.find('!');
    if (bang != std::string::npos && key.substr(bang + 1) == func) {
      Ret(addr);
      return;
    }
  }
  (void)hmod;
  Ret(0);
}
void WinAPILayer::I_LoadLibraryA() {
  // We don't actually load anything — just acknowledge so games don't crash
  Ret(0x4000); // non-null fake HMODULE
}
void WinAPILayer::I_LoadLibraryW() { I_LoadLibraryA(); }
void WinAPILayer::I_FreeLibrary() { Ret(1); }

void WinAPILayer::I_VirtualAlloc() {
  const uint64_t base = Arg(1);
  const uint64_t size = Arg(2);
  // Arg(3) = flAllocationType, Arg(4) = flProtect — we store what we get
  const uint32_t prot = static_cast<uint32_t>(Arg(4));
  uint32_t mprot = WinMemory::PAGE_READWRITE;
  if (prot & 0x10 || prot & 0x20 || prot & 0x40)
    mprot = WinMemory::PAGE_EXECUTE_READWRITE;
  Ret(mem_.Allocate(base, size, mprot));
}
void WinAPILayer::I_VirtualFree() {
  mem_.Free(Arg(1));
  Ret(1);
}
void WinAPILayer::I_VirtualProtect() {
  mem_.Protect(Arg(1), Arg(2), static_cast<uint32_t>(Arg(3)));
  // Write old protect to *lpflOldProtect
  const uint64_t pOld = Arg(4);
  if (pOld)
    mem_.Write32(pOld, WinMemory::PAGE_READWRITE);
  Ret(1);
}

void WinAPILayer::I_CreateFileA() {
  const std::string path = mem_.ReadStringA(Arg(1));
  const uint32_t access = static_cast<uint32_t>(Arg(2));
  const uint32_t create = static_cast<uint32_t>(Arg(5));
  const char *mode = (access & 0x40000000) ? "wb+" : "rb";
  if (create == 1 /*CREATE_NEW*/ || create == 2 /*CREATE_ALWAYS*/)
    mode = "wb+";
  FILE *f = std::fopen(path.c_str(), mode);
  if (!f) {
    lastError = 2;
    Ret(static_cast<uint64_t>(-1));
    return;
  } // INVALID_HANDLE
  size_t slot = fileSlots_.size();
  for (size_t i = 0; i < fileSlots_.size(); ++i) {
    if (!fileSlots_[i]) {
      slot = i;
      fileSlots_[i] = f;
      goto found;
    }
  }
  fileSlots_.push_back(f);
found:
  Ret(AllocHandle(HT_FILE, static_cast<uint64_t>(slot)));
}
void WinAPILayer::I_CreateFileW() {
  I_CreateFileA();
} // simplified: name already copied

void WinAPILayer::I_ReadFile() {
  FILE *f = GetFile(Arg(1));
  const uint64_t buf = Arg(2);
  const uint32_t nReq = static_cast<uint32_t>(Arg(3));
  const uint64_t pRead = Arg(4);
  if (!f || !buf) {
    Ret(0);
    return;
  }
  std::vector<uint8_t> tmp(nReq);
  const size_t got = std::fread(tmp.data(), 1, nReq, f);
  mem_.Write(buf, tmp.data(), got);
  if (pRead)
    mem_.Write32(pRead, static_cast<uint32_t>(got));
  Ret(1);
}
void WinAPILayer::I_WriteFile() {
  FILE *f = GetFile(Arg(1));
  const uint64_t buf = Arg(2);
  const uint32_t nReq = static_cast<uint32_t>(Arg(3));
  const uint64_t pWrit = Arg(4);
  if (!buf || !nReq) {
    Ret(1);
    return;
  }
  std::vector<uint8_t> tmp(nReq);
  mem_.Read(buf, tmp.data(), nReq);
  size_t written = 1;
  if (f)
    written = std::fwrite(tmp.data(), 1, nReq, f);
  if (pWrit)
    mem_.Write32(pWrit, static_cast<uint32_t>(written));
  Ret(1);
}
void WinAPILayer::I_CloseHandle() {
  FreeHandle(Arg(1));
  Ret(1);
}

void WinAPILayer::I_GetSystemInfo() {
  // SYSTEM_INFO structure is 48 bytes; write key fields
  const uint64_t pSI = Arg(1);
  if (!pSI)
    return;
  mem_.Write32(pSI + 0,
               9); // wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64
  mem_.Write32(pSI + 4, 4096);               // dwPageSize
  mem_.Write64(pSI + 8, 0x10000ULL);         // lpMinimumApplicationAddress
  mem_.Write64(pSI + 16, 0x7FFEFFFF0000ULL); // lpMaximumApplicationAddress
  mem_.Write32(pSI + 24, 0);                 // dwActiveProcessorMask (lo)
  mem_.Write32(pSI + 28, 0);                 // dwActiveProcessorMask (hi)
  mem_.Write32(pSI + 32, 4);                 // dwNumberOfProcessors
  mem_.Write32(pSI + 36, 586);               // dwProcessorType
  mem_.Write32(pSI + 40, 0x10000);           // dwAllocationGranularity 64K
  mem_.Write16(pSI + 44, 0);                 // wProcessorLevel
  mem_.Write16(pSI + 46, 0);                 // wProcessorRevision
  Ret(0);
}
void WinAPILayer::I_GetNativeSystemInfo() { I_GetSystemInfo(); }

void WinAPILayer::I_GetLastError() { Ret(lastError); }
void WinAPILayer::I_SetLastError() {
  lastError = static_cast<uint32_t>(Arg(1));
  Ret(0);
}

// Critical sections: we don't actually synchronise (single-threaded emulation)
void WinAPILayer::I_InitializeCriticalSection() { Ret(0); }
void WinAPILayer::I_InitializeCriticalSectionEx() { Ret(1); }
void WinAPILayer::I_EnterCriticalSection() { Ret(0); }
void WinAPILayer::I_LeaveCriticalSection() { Ret(0); }
void WinAPILayer::I_DeleteCriticalSection() { Ret(0); }
void WinAPILayer::I_TryEnterCriticalSection() { Ret(1); }

static uint64_t GetHostMillis() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}
static uint64_t GetHostNanos() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
          .count());
}

void WinAPILayer::I_GetTickCount() { Ret(GetHostMillis() & 0xFFFFFFFFull); }
void WinAPILayer::I_GetTickCount64() { Ret(GetHostMillis()); }
void WinAPILayer::I_Sleep() { Ret(0); } // no-op in emulation
void WinAPILayer::I_SleepEx() { Ret(0); }

void WinAPILayer::I_QueryPerformanceCounter() {
  const uint64_t pQPC = Arg(1);
  if (pQPC)
    mem_.Write64(pQPC, GetHostNanos());
  Ret(1);
}
void WinAPILayer::I_QueryPerformanceFrequency() {
  const uint64_t pQPF = Arg(1);
  if (pQPF)
    mem_.Write64(pQPF, kPerfFreq);
  Ret(1);
}

void WinAPILayer::I_HeapAlloc() {
  // (Heap, Flags, Size)
  Ret(HeapAlloc_(Arg(3)));
}
void WinAPILayer::I_HeapFree() {
  HeapFree_(Arg(3));
  Ret(1);
}
void WinAPILayer::I_HeapCreate() {
  if (heapBase_ == 0)
    HeapAlloc_(0); // initialize
  Ret(processHeapHandle_);
}
void WinAPILayer::I_HeapReAlloc() {
  const uint64_t newSize = Arg(4);
  const uint64_t oldAddr = Arg(3);
  const uint64_t newAddr = HeapAlloc_(newSize);
  if (newAddr && oldAddr) {
    // Copy old content (best-effort, up to new size)
    for (uint64_t i = 0; i < newSize; ++i) {
      mem_.Write8(newAddr + i, mem_.Read8(oldAddr + i));
    }
    HeapFree_(oldAddr);
  }
  Ret(newAddr);
}
void WinAPILayer::I_HeapSize() { Ret(static_cast<uint64_t>(-1)); }
void WinAPILayer::I_HeapDestroy() { Ret(1); }

void WinAPILayer::I_LocalAlloc() { Ret(HeapAlloc_(Arg(2))); }
void WinAPILayer::I_LocalFree() {
  HeapFree_(Arg(1));
  Ret(0);
}
void WinAPILayer::I_LocalReAlloc() { Ret(HeapAlloc_(Arg(2))); }
void WinAPILayer::I_GlobalAlloc() { Ret(HeapAlloc_(Arg(2))); }
void WinAPILayer::I_GlobalFree() {
  HeapFree_(Arg(1));
  Ret(0);
}
void WinAPILayer::I_GetProcessHeap() { Ret(processHeapHandle_); }

void WinAPILayer::I_GetCurrentProcessId() {
  Ret(static_cast<uint32_t>(getpid()));
}
void WinAPILayer::I_GetCurrentThreadId() { Ret(1); }
void WinAPILayer::I_GetCurrentProcess() { Ret(kHandleProcessSelf); }
void WinAPILayer::I_GetCurrentThread() { Ret(kHandleThreadSelf); }
void WinAPILayer::I_IsDebuggerPresent() { Ret(0); }
void WinAPILayer::I_CheckRemoteDebuggerPresent() { Ret(0); }

void WinAPILayer::I_OutputDebugStringA() {
  std::cerr << "[OutputDebugString] " << mem_.ReadStringA(Arg(1)) << "\n";
  Ret(0);
}
void WinAPILayer::I_OutputDebugStringW() { I_OutputDebugStringA(); }

void WinAPILayer::I_GetCommandLineA() { Ret(cmdLineAddrA_); }
void WinAPILayer::I_GetCommandLineW() { Ret(cmdLineAddrW_); }

void WinAPILayer::I_GetEnvironmentVariableA() {
  const std::string name = mem_.ReadStringA(Arg(1));
  const uint64_t buf = Arg(2);
  const uint32_t size = static_cast<uint32_t>(Arg(3));
  const char *val = std::getenv(name.c_str());
  if (!val) {
    lastError = 203;
    Ret(0);
    return;
  }
  if (buf && size > 0)
    mem_.WriteStringA(buf, val);
  Ret(static_cast<uint32_t>(std::strlen(val)));
}
void WinAPILayer::I_GetEnvironmentVariableW() { I_GetEnvironmentVariableA(); }

void WinAPILayer::I_GetStdHandle() {
  const uint32_t which = static_cast<uint32_t>(Arg(1));
  if (which == static_cast<uint32_t>(-10))
    Ret(kHandleStdIn);
  else if (which == static_cast<uint32_t>(-11))
    Ret(kHandleStdOut);
  else
    Ret(kHandleStdErr);
}
void WinAPILayer::I_SetConsoleMode() { Ret(1); }
void WinAPILayer::I_GetConsoleMode() { Ret(1); }
void WinAPILayer::I_SetConsoleTitleA() { Ret(1); }
void WinAPILayer::I_SetConsoleTitleW() { Ret(1); }
void WinAPILayer::I_AllocConsole() { Ret(1); }
void WinAPILayer::I_FlushFileBuffers() { Ret(1); }

void WinAPILayer::I_SetFilePointer() {
  FILE *f = GetFile(Arg(1));
  const int32_t dist = static_cast<int32_t>(Arg(2));
  const uint32_t method = static_cast<uint32_t>(Arg(4));
  if (f)
    std::fseek(f, dist,
               method == 0   ? SEEK_SET
               : method == 1 ? SEEK_CUR
                             : SEEK_END);
  Ret(f ? static_cast<uint32_t>(std::ftell(f)) : static_cast<uint32_t>(-1));
}
void WinAPILayer::I_SetFilePointerEx() { I_SetFilePointer(); }

void WinAPILayer::I_GetFileSize() {
  FILE *f = GetFile(Arg(1));
  if (!f) {
    Ret(static_cast<uint64_t>(-1));
    return;
  }
  const long cur = std::ftell(f);
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, cur, SEEK_SET);
  Ret(static_cast<uint32_t>(sz));
}
void WinAPILayer::I_GetFileSizeEx() {
  FILE *f = GetFile(Arg(1));
  const uint64_t pSize = Arg(2);
  if (!f) {
    Ret(0);
    return;
  }
  const long cur = std::ftell(f);
  std::fseek(f, 0, SEEK_END);
  if (pSize)
    mem_.Write64(pSize, static_cast<uint64_t>(std::ftell(f)));
  std::fseek(f, cur, SEEK_SET);
  Ret(1);
}

void WinAPILayer::I_CreateMutexA() { Ret(AllocHandle(HT_MUTEX, 0)); }
void WinAPILayer::I_CreateMutexW() { Ret(AllocHandle(HT_MUTEX, 0)); }
void WinAPILayer::I_ReleaseMutex() { Ret(1); }
void WinAPILayer::I_CreateEventA() { Ret(AllocHandle(HT_EVENT, 0)); }
void WinAPILayer::I_CreateEventW() { Ret(AllocHandle(HT_EVENT, 0)); }
void WinAPILayer::I_SetEvent() { Ret(1); }
void WinAPILayer::I_ResetEvent() { Ret(1); }

void WinAPILayer::I_WaitForSingleObject() {
  // Single-threaded: all objects appear signalled immediately
  Ret(0); // WAIT_OBJECT_0
}
void WinAPILayer::I_WaitForSingleObjectEx() { Ret(0); }
void WinAPILayer::I_WaitForMultipleObjects() { Ret(0); }

void WinAPILayer::I_CreateThread() {
  // Can't truly multithread; return a fake handle. The thread function
  // will never be called in this single-threaded model.
  Ret(AllocHandle(HT_THREAD, 0));
}
void WinAPILayer::I_ExitThread() { Ret(0); }
void WinAPILayer::I_GetThreadId() { Ret(1); }
void WinAPILayer::I_SetThreadPriority() { Ret(1); }
void WinAPILayer::I_GetThreadPriority() { Ret(0); }
void WinAPILayer::I_ResumeThread() { Ret(0); }
void WinAPILayer::I_SuspendThread() { Ret(0); }
void WinAPILayer::I_TerminateThread() { Ret(1); }

void WinAPILayer::I_GetSystemTimeAsFileTime() {
  const uint64_t pFT = Arg(1);
  if (!pFT)
    return;
  // FILETIME: nanoseconds since 1601-01-01 / 100  (100-ns intervals)
  // We approximate with current time in nanoseconds converted
  uint64_t ns = GetHostNanos();
  // Add offset from 1601 to 1970 in 100-ns intervals: 116444736000000000
  uint64_t ft = ns / 100 + 116444736000000000ULL;
  mem_.Write64(pFT, ft);
  Ret(0);
}
void WinAPILayer::I_GetLocalTime() { Ret(0); }
void WinAPILayer::I_GetSystemTime() { Ret(0); }
void WinAPILayer::I_FileTimeToSystemTime() { Ret(1); }
void WinAPILayer::I_SystemTimeToFileTime() { Ret(1); }

void WinAPILayer::I_GetFileAttributesA() { Ret(0x80); } // FILE_ATTRIBUTE_NORMAL
void WinAPILayer::I_GetFileAttributesW() { Ret(0x80); }
void WinAPILayer::I_SetFileAttributesA() { Ret(1); }
void WinAPILayer::I_CreateDirectoryA() { Ret(1); }
void WinAPILayer::I_CreateDirectoryW() { Ret(1); }
void WinAPILayer::I_DeleteFileA() { Ret(1); }
void WinAPILayer::I_DeleteFileW() { Ret(1); }
void WinAPILayer::I_MoveFileA() { Ret(1); }
void WinAPILayer::I_CopyFileA() { Ret(1); }
void WinAPILayer::I_FindFirstFileA() {
  Ret(static_cast<uint64_t>(-1));
} // INVALID_HANDLE
void WinAPILayer::I_FindFirstFileW() { Ret(static_cast<uint64_t>(-1)); }
void WinAPILayer::I_FindNextFileA() {
  lastError = 18;
  Ret(0);
}
void WinAPILayer::I_FindNextFileW() {
  lastError = 18;
  Ret(0);
}
void WinAPILayer::I_FindClose() { Ret(1); }

void WinAPILayer::I_GetFullPathNameA() {
  const std::string path = mem_.ReadStringA(Arg(1));
  const uint64_t buf = Arg(3);
  if (buf)
    mem_.WriteStringA(buf, path);
  Ret(static_cast<uint32_t>(path.size()));
}
void WinAPILayer::I_GetFullPathNameW() { I_GetFullPathNameA(); }

void WinAPILayer::I_GetTempPathA() {
  const uint64_t buf = Arg(2);
  if (buf)
    mem_.WriteStringA(buf, "/tmp/");
  Ret(5);
}
void WinAPILayer::I_GetTempPathW() { I_GetTempPathA(); }

void WinAPILayer::I_GetModuleFileNameA() {
  const uint64_t buf = Arg(2);
  const uint32_t sz = static_cast<uint32_t>(Arg(3));
  const auto &mods = mem_.GetModules();
  const std::string path = mods.empty() ? "" : mods.front().path;
  if (buf && sz)
    mem_.WriteStringA(buf, path.substr(0, sz - 1));
  Ret(static_cast<uint32_t>(path.size()));
}
void WinAPILayer::I_GetModuleFileNameW() { I_GetModuleFileNameA(); }
void WinAPILayer::I_GetSystemDirectoryA() {
  const uint64_t b = Arg(1);
  if (b)
    mem_.WriteStringA(b, "C:\\Windows\\System32");
  Ret(20);
}
void WinAPILayer::I_GetSystemDirectoryW() { I_GetSystemDirectoryA(); }
void WinAPILayer::I_GetWindowsDirectoryA() {
  const uint64_t b = Arg(1);
  if (b)
    mem_.WriteStringA(b, "C:\\Windows");
  Ret(10);
}
void WinAPILayer::I_GetWindowsDirectoryW() { I_GetWindowsDirectoryA(); }
void WinAPILayer::I_ExpandEnvironmentStringsA() {
  const std::string src = mem_.ReadStringA(Arg(1));
  const uint64_t dst = Arg(2);
  if (dst)
    mem_.WriteStringA(dst, src);
  Ret(static_cast<uint32_t>(src.size() + 1));
}

void WinAPILayer::I_MultiByteToWideChar() {
  // (CodePage, Flags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar)
  const std::string src = mem_.ReadStringA(Arg(3));
  const uint64_t dst = Arg(5);
  const int dstLen = static_cast<int>(Arg(6));
  if (dst && dstLen > 0) {
    for (int i = 0; i < (int)src.size() && i < dstLen - 1; ++i)
      mem_.Write16(dst + i * 2, static_cast<uint16_t>(src[i]));
    mem_.Write16(dst + std::min((int)src.size(), dstLen - 1) * 2, 0);
  }
  Ret(static_cast<uint32_t>(src.size() + 1));
}
void WinAPILayer::I_WideCharToMultiByte() {
  // Minimal: just copy bytes
  const uint64_t src = Arg(3);
  const uint64_t dst = Arg(5);
  const int dstLen = static_cast<int>(Arg(6));
  if (src && dst && dstLen > 0) {
    int i = 0;
    while (i < dstLen - 1) {
      uint16_t ch = mem_.Read16(src + i * 2);
      if (!ch)
        break;
      mem_.Write8(dst + i, static_cast<uint8_t>(ch & 0xFF));
      ++i;
    }
    mem_.Write8(dst + i, 0);
    Ret(i);
    return;
  }
  Ret(0);
}
void WinAPILayer::I_FormatMessageA() {
  const uint64_t b = Arg(5);
  if (b)
    mem_.WriteStringA(b, "");
  Ret(0);
}
void WinAPILayer::I_FormatMessageW() { I_FormatMessageA(); }

// Interlocked
void WinAPILayer::I_InterlockedIncrement() {
  uint64_t a = Arg(1);
  uint32_t v = mem_.Read32(a) + 1;
  mem_.Write32(a, v);
  Ret(v);
}
void WinAPILayer::I_InterlockedDecrement() {
  uint64_t a = Arg(1);
  uint32_t v = mem_.Read32(a) - 1;
  mem_.Write32(a, v);
  Ret(v);
}
void WinAPILayer::I_InterlockedExchange() {
  uint64_t a = Arg(1);
  uint32_t newV = static_cast<uint32_t>(Arg(2));
  uint32_t old = mem_.Read32(a);
  mem_.Write32(a, newV);
  Ret(old);
}
void WinAPILayer::I_InterlockedCompareExchange() {
  uint64_t a = Arg(1);
  uint32_t exch = static_cast<uint32_t>(Arg(2));
  uint32_t cmp = static_cast<uint32_t>(Arg(3));
  uint32_t cur = mem_.Read32(a);
  if (cur == cmp)
    mem_.Write32(a, exch);
  Ret(cur);
}
void WinAPILayer::I_InterlockedAdd() {
  uint64_t a = Arg(1);
  uint32_t addend = static_cast<uint32_t>(Arg(2));
  uint32_t r = mem_.Read32(a) + addend;
  mem_.Write32(a, r);
  Ret(r);
}

void WinAPILayer::I_SetUnhandledExceptionFilter() { Ret(0); }
void WinAPILayer::I_UnhandledExceptionFilter() { Ret(1); }
void WinAPILayer::I_RaiseException() {
  cpu_->SetRIP(0xDEAD'BEEF'DEAD'BEEFull);
  Ret(0);
}
void WinAPILayer::I_TerminateProcess() { I_ExitProcess(); }
void WinAPILayer::I_IsWow64Process() {
  const uint64_t p = Arg(2);
  if (p)
    mem_.Write32(p, 0);
  Ret(1);
}
void WinAPILayer::I_InitOnceExecuteOnce() { Ret(1); }

// File mapping (minimal stubs)
void WinAPILayer::I_CreateFileMappingA() { Ret(0x5001); }
void WinAPILayer::I_CreateFileMappingW() { Ret(0x5001); }
void WinAPILayer::I_MapViewOfFile() {
  Ret(mem_.Allocate(0, Arg(5) ? Arg(5) : 0x10000, WinMemory::PAGE_READWRITE));
}
void WinAPILayer::I_UnmapViewOfFile() { Ret(1); }

// ── ntdll
// ─────────────────────────────────────────────────────────────────────
void WinAPILayer::I_NtAllocateVirtualMemory() {
  // (ProcessHandle, *BaseAddr, ZeroBits, *RegionSize, AllocType, Protect)
  const uint64_t pBase = Arg(2), pSize = Arg(4);
  uint64_t base = pBase ? mem_.Read64(pBase) : 0;
  uint64_t size = pSize ? mem_.Read64(pSize) : 0x1000;
  base = mem_.Allocate(base, size, WinMemory::PAGE_READWRITE);
  if (pBase)
    mem_.Write64(pBase, base);
  if (pSize)
    mem_.Write64(pSize, size);
  Ret(kSTATUS_SUCCESS);
}
void WinAPILayer::I_NtFreeVirtualMemory() {
  mem_.Free(Arg(2));
  Ret(kSTATUS_SUCCESS);
}
void WinAPILayer::I_NtProtectVirtualMemory() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_NtCreateFile() {
  Ret(0xC0000034ull);
} // STATUS_OBJECT_NAME_NOT_FOUND
void WinAPILayer::I_NtReadFile() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_NtWriteFile() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_NtClose() {
  FreeHandle(Arg(1));
  Ret(kSTATUS_SUCCESS);
}
void WinAPILayer::I_NtQueryInformationProcess() {
  // Return sensible defaults for common info classes
  const uint32_t infoClass = static_cast<uint32_t>(Arg(2));
  const uint64_t buf = Arg(3);
  if (infoClass == 0 && buf) {              // ProcessBasicInformation
    mem_.Write64(buf + 0, kSTATUS_SUCCESS); // ExitStatus
    mem_.Write64(buf + 8, WinMemory::kPEBBase);
    mem_.Write64(buf + 16, 0);
    mem_.Write64(buf + 24, 1); // UniqueProcessId
    mem_.Write64(buf + 32, 0); // InheritedFromUniqueProcessId
  }
  Ret(kSTATUS_SUCCESS);
}
void WinAPILayer::I_NtQuerySystemInformation() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_NtTerminateProcess() { I_ExitProcess(); }
void WinAPILayer::I_NtTerminateThread() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlAllocateHeap() { Ret(HeapAlloc_(Arg(3))); }
void WinAPILayer::I_RtlFreeHeap() {
  HeapFree_(Arg(3));
  Ret(1);
}
void WinAPILayer::I_RtlReAllocateHeap() {
  const uint64_t oldAddr = Arg(3);
  const uint64_t newSize = Arg(4);
  uint64_t newAddr = HeapAlloc_(newSize);
  if (newAddr && oldAddr) {
    for (uint64_t i = 0; i < newSize; ++i)
      mem_.Write8(newAddr + i, mem_.Read8(oldAddr + i));
    HeapFree_(oldAddr);
  }
  Ret(newAddr);
}
void WinAPILayer::I_RtlSizeHeap() { Ret(0); }
void WinAPILayer::I_RtlInitializeCriticalSection() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlInitializeCriticalSectionEx() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlEnterCriticalSection() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlLeaveCriticalSection() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlDeleteCriticalSection() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlTryCriticalSection() { Ret(1); }
void WinAPILayer::I_LdrLoadDll() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_LdrGetDllHandleByName() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_LdrGetProcedureAddress() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlExitUserProcess() { I_ExitProcess(); }
void WinAPILayer::I_RtlGetVersion() {
  const uint64_t pOS = Arg(1); // OSVERSIONINFOEXW
  if (!pOS) {
    Ret(kSTATUS_SUCCESS);
    return;
  }
  mem_.Write32(pOS + 0, 0x120);  // dwOSVersionInfoSize
  mem_.Write32(pOS + 4, 10);     // dwMajorVersion
  mem_.Write32(pOS + 8, 0);      // dwMinorVersion
  mem_.Write32(pOS + 12, 19045); // dwBuildNumber (Win10 22H2)
  mem_.Write32(pOS + 16, 2);     // dwPlatformId
  Ret(kSTATUS_SUCCESS);
}
void WinAPILayer::I_NtQueryVirtualMemory() { Ret(kSTATUS_SUCCESS); }
void WinAPILayer::I_RtlCaptureStackBackTrace() { Ret(0); }
void WinAPILayer::I_RtlRandom() { Ret(static_cast<uint32_t>(std::rand())); }
void WinAPILayer::I_RtlUniform() { Ret(static_cast<uint32_t>(std::rand())); }

// ── msvcrt / ucrtbase
// ─────────────────────────────────────────────────────────
void WinAPILayer::I_malloc() { Ret(HeapAlloc_(Arg(1))); }
void WinAPILayer::I_free() {
  HeapFree_(Arg(1));
  Ret(0);
}
void WinAPILayer::I_realloc() {
  uint64_t old = Arg(1);
  uint64_t sz = Arg(2);
  uint64_t n = HeapAlloc_(sz);
  if (n && old) {
    for (uint64_t i = 0; i < sz; ++i)
      mem_.Write8(n + i, mem_.Read8(old + i));
    HeapFree_(old);
  }
  Ret(n);
}
void WinAPILayer::I_calloc() {
  uint64_t n = Arg(1) * Arg(2);
  uint64_t a = HeapAlloc_(n);
  if (a)
    for (uint64_t i = 0; i < n; ++i)
      mem_.Write8(a + i, 0);
  Ret(a);
}
void WinAPILayer::I_printf() {
  Ret(0); /* no-op — games rarely need this to print */
}
void WinAPILayer::I_fprintf() { Ret(0); }
void WinAPILayer::I_sprintf() { Ret(0); }
void WinAPILayer::I_snprintf() { Ret(0); }
void WinAPILayer::I_sscanf() { Ret(0); }
void WinAPILayer::I_vprintf() { Ret(0); }
void WinAPILayer::I_vsprintf() { Ret(0); }
void WinAPILayer::I_vsnprintf() { Ret(0); }

void WinAPILayer::I_strlen() {
  const std::string s = mem_.ReadStringA(Arg(1));
  Ret(static_cast<uint64_t>(s.size()));
}
void WinAPILayer::I_wcslen() {
  uint64_t a = Arg(1);
  uint64_t len = 0;
  while (mem_.Read16(a + len * 2))
    ++len;
  Ret(len);
}
void WinAPILayer::I_strcpy() {
  const std::string s = mem_.ReadStringA(Arg(2));
  mem_.WriteStringA(Arg(1), s);
  Ret(Arg(1));
}
void WinAPILayer::I_strncpy() {
  const std::string s = mem_.ReadStringA(Arg(2));
  uint64_t n = Arg(3);
  std::string trunc = s.substr(0, n);
  mem_.WriteStringA(Arg(1), trunc);
  Ret(Arg(1));
}
void WinAPILayer::I_strcmp() {
  Ret(static_cast<uint64_t>(static_cast<int32_t>(
      mem_.ReadStringA(Arg(1)).compare(mem_.ReadStringA(Arg(2))))));
}
void WinAPILayer::I_strncmp() {
  std::string a = mem_.ReadStringA(Arg(1)).substr(0, Arg(3));
  std::string b = mem_.ReadStringA(Arg(2)).substr(0, Arg(3));
  Ret(static_cast<uint64_t>(static_cast<int32_t>(a.compare(b))));
}
void WinAPILayer::I_strcat() {
  std::string a = mem_.ReadStringA(Arg(1));
  std::string b = mem_.ReadStringA(Arg(2));
  mem_.WriteStringA(Arg(1), a + b);
  Ret(Arg(1));
}
void WinAPILayer::I_strncat() {
  std::string a = mem_.ReadStringA(Arg(1));
  std::string b = mem_.ReadStringA(Arg(2)).substr(0, Arg(3));
  mem_.WriteStringA(Arg(1), a + b);
  Ret(Arg(1));
}
void WinAPILayer::I_strdup() {
  const std::string s = mem_.ReadStringA(Arg(1));
  uint64_t a = HeapAlloc_(s.size() + 1);
  if (a)
    mem_.WriteStringA(a, s);
  Ret(a);
}
void WinAPILayer::I_strchr() {
  std::string s = mem_.ReadStringA(Arg(1));
  char c = static_cast<char>(Arg(2) & 0xFF);
  auto pos = s.find(c);
  Ret(pos == std::string::npos ? 0 : Arg(1) + pos);
}
void WinAPILayer::I_strstr() {
  std::string h = mem_.ReadStringA(Arg(1)), n = mem_.ReadStringA(Arg(2));
  auto pos = h.find(n);
  Ret(pos == std::string::npos ? 0 : Arg(1) + pos);
}
void WinAPILayer::I_strtod() { Ret(0); }
void WinAPILayer::I_strtol() {
  Ret(static_cast<int32_t>(std::strtol(mem_.ReadStringA(Arg(1)).c_str(),
                                       nullptr, static_cast<int>(Arg(3)))));
}
void WinAPILayer::I_strtoul() {
  Ret(std::strtoul(mem_.ReadStringA(Arg(1)).c_str(), nullptr,
                   static_cast<int>(Arg(3))));
}
void WinAPILayer::I_memcpy() {
  uint64_t d = Arg(1), s = Arg(2);
  uint64_t n = Arg(3);
  for (uint64_t i = 0; i < n; ++i)
    mem_.Write8(d + i, mem_.Read8(s + i));
  Ret(d);
}
void WinAPILayer::I_memmove() { I_memcpy(); }
void WinAPILayer::I_memset() {
  uint64_t d = Arg(1);
  uint8_t c = static_cast<uint8_t>(Arg(2));
  uint64_t n = Arg(3);
  for (uint64_t i = 0; i < n; ++i)
    mem_.Write8(d + i, c);
  Ret(d);
}
void WinAPILayer::I_memcmp() {
  uint64_t a = Arg(1), b = Arg(2), n = Arg(3);
  for (uint64_t i = 0; i < n; ++i) {
    int diff = static_cast<int>(mem_.Read8(a + i)) -
               static_cast<int>(mem_.Read8(b + i));
    if (diff) {
      Ret(static_cast<uint64_t>(static_cast<int32_t>(diff)));
      return;
    }
  }
  Ret(0);
}
void WinAPILayer::I_memchr() {
  uint64_t p = Arg(1);
  uint8_t c = static_cast<uint8_t>(Arg(2));
  uint64_t n = Arg(3);
  for (uint64_t i = 0; i < n; ++i)
    if (mem_.Read8(p + i) == c) {
      Ret(p + i);
      return;
    }
  Ret(0);
}
void WinAPILayer::I_atoi() {
  Ret(static_cast<int32_t>(std::atoi(mem_.ReadStringA(Arg(1)).c_str())));
}
void WinAPILayer::I_atol() {
  Ret(static_cast<int32_t>(std::atol(mem_.ReadStringA(Arg(1)).c_str())));
}
void WinAPILayer::I_atof() { Ret(0); }
void WinAPILayer::I_itoa() {
  const int v = static_cast<int>(Arg(1));
  mem_.WriteStringA(Arg(2), std::to_string(v));
  Ret(Arg(2));
}
void WinAPILayer::I_exit() { I_ExitProcess(); }
void WinAPILayer::I_abort() { I_ExitProcess(); }
void WinAPILayer::I__exit() { I_ExitProcess(); }
void WinAPILayer::I_rand() { Ret(static_cast<uint32_t>(std::rand() & 0x7FFF)); }
void WinAPILayer::I_srand() {
  std::srand(static_cast<unsigned>(Arg(1)));
  Ret(0);
}
void WinAPILayer::I_time() {
  std::time_t t = std::time(nullptr);
  const uint64_t p = Arg(1);
  if (p)
    mem_.Write64(p, static_cast<uint64_t>(t));
  Ret(static_cast<uint64_t>(t));
}
void WinAPILayer::I_clock() { Ret(static_cast<uint64_t>(std::clock())); }
void WinAPILayer::I_difftime() { Ret(0); }
void WinAPILayer::I_fopen() {
  const std::string path = mem_.ReadStringA(Arg(1));
  const std::string mode = mem_.ReadStringA(Arg(2));
  FILE *f = std::fopen(path.c_str(), mode.c_str());
  if (!f) {
    Ret(0);
    return;
  }
  size_t slot = fileSlots_.size();
  fileSlots_.push_back(f);
  Ret(AllocHandle(HT_FILE, static_cast<uint64_t>(slot)));
}
void WinAPILayer::I_fclose() {
  FILE *f = GetFile(Arg(1));
  if (f)
    std::fclose(f);
  FreeHandle(Arg(1));
  Ret(0);
}
void WinAPILayer::I_fread() {
  FILE *f = GetFile(Arg(4));
  uint64_t buf = Arg(1);
  uint64_t sz = Arg(2);
  uint64_t cnt = Arg(3);
  if (!f) {
    Ret(0);
    return;
  }
  std::vector<uint8_t> tmp(sz * cnt);
  size_t got = std::fread(tmp.data(), sz, cnt, f);
  mem_.Write(buf, tmp.data(), got * sz);
  Ret(got);
}
void WinAPILayer::I_fwrite() {
  FILE *f = GetFile(Arg(4));
  uint64_t buf = Arg(1);
  uint64_t sz = Arg(2);
  uint64_t cnt = Arg(3);
  if (!f || !buf) {
    Ret(0);
    return;
  }
  std::vector<uint8_t> tmp(sz * cnt);
  mem_.Read(buf, tmp.data(), sz * cnt);
  Ret(std::fwrite(tmp.data(), sz, cnt, f));
}
void WinAPILayer::I_fseek() {
  FILE *f = GetFile(Arg(1));
  if (f)
    std::fseek(f, static_cast<long>(Arg(2)), static_cast<int>(Arg(3)));
  Ret(0);
}
void WinAPILayer::I_ftell() {
  FILE *f = GetFile(Arg(1));
  Ret(f ? static_cast<uint64_t>(std::ftell(f)) : static_cast<uint64_t>(-1));
}
void WinAPILayer::I_feof() {
  FILE *f = GetFile(Arg(1));
  Ret(f ? static_cast<uint64_t>(std::feof(f)) : 1);
}
void WinAPILayer::I_fflush() {
  FILE *f = GetFile(Arg(1));
  if (f)
    std::fflush(f);
  Ret(0);
}
void WinAPILayer::I_fgets() { Ret(0); }
void WinAPILayer::I_fputs() { Ret(0); }
void WinAPILayer::I_fputc() { Ret(0); }
void WinAPILayer::I_fgetc() { Ret(-1); }
void WinAPILayer::I_getc() { Ret(-1); }
void WinAPILayer::I_putc() { Ret(0); }
void WinAPILayer::I_puts() { Ret(0); }
void WinAPILayer::I_gets() { Ret(0); }
void WinAPILayer::I_clearerr() { Ret(0); }
void WinAPILayer::I_ferror() { Ret(0); }
void WinAPILayer::I_rewind() {
  FILE *f = GetFile(Arg(1));
  if (f)
    std::rewind(f);
  Ret(0);
}
void WinAPILayer::I__fileno() { Ret(0); }

// CRT startup / lifecycle stubs — return success so CRT initializes normally
void WinAPILayer::I__CRT_INIT() { Ret(1); }
void WinAPILayer::I___CxxFrameHandler3() { Ret(0); }
void WinAPILayer::I___C_specific_handler() { Ret(0); }
void WinAPILayer::I___chkstk() { Ret(0); } // stack probe
void WinAPILayer::I___security_cookie() { Ret(0); }
void WinAPILayer::I___security_check_cookie() { Ret(0); }
void WinAPILayer::I__get_initial_narrow_environment() { Ret(0); }
void WinAPILayer::I__configure_narrow_argv() { Ret(0); }
void WinAPILayer::I__initialize_narrow_environment() { Ret(0); }
void WinAPILayer::I__cexit() { Ret(0); }
void WinAPILayer::I__conexit() { Ret(0); }
void WinAPILayer::I__initterm() { Ret(0); }
void WinAPILayer::I__initterm_e() { Ret(0); }
void WinAPILayer::I__register_onexit_function() { Ret(0); }
void WinAPILayer::I__execute_onexit_table() { Ret(0); }
void WinAPILayer::I__crt_atexit() { Ret(0); }
void WinAPILayer::I_atexit() { Ret(0); }
void WinAPILayer::I_fesetenv() { Ret(0); }
void WinAPILayer::I_fegetenv() { Ret(0); }
void WinAPILayer::I__controlfp_s() { Ret(0); }
void WinAPILayer::I__set_app_type() { Ret(0); }

// ── user32
// ────────────────────────────────────────────────────────────────────
void WinAPILayer::I_MessageBoxA() {
  const std::string msg = mem_.ReadStringA(Arg(2));
  const std::string cap = mem_.ReadStringA(Arg(3));
  std::cerr << "[MessageBox] " << cap << ": " << msg << "\n";
  Ret(1); // IDOK
}
void WinAPILayer::I_MessageBoxW() { I_MessageBoxA(); }
void WinAPILayer::I_CreateWindowExA() { Ret(0x1001); } // fake HWND
void WinAPILayer::I_CreateWindowExW() { Ret(0x1001); }
void WinAPILayer::I_RegisterClassExA() { Ret(1); }
void WinAPILayer::I_RegisterClassExW() { Ret(1); }
void WinAPILayer::I_ShowWindow() { Ret(1); }
void WinAPILayer::I_UpdateWindow() { Ret(1); }
void WinAPILayer::I_DefWindowProcA() { Ret(0); }
void WinAPILayer::I_DefWindowProcW() { Ret(0); }
void WinAPILayer::I_GetMessageA() {
  // Post WM_QUIT (0x0012) to terminate the game's message loop gracefully
  const uint64_t pMsg = Arg(1);
  if (pMsg) {
    mem_.Write32(pMsg + 4, 0x0012);
    mem_.Write32(pMsg + 8, 0);
  }
  Ret(0); // returns 0 on WM_QUIT
}
void WinAPILayer::I_GetMessageW() { I_GetMessageA(); }
void WinAPILayer::I_PeekMessageA() { Ret(0); }
void WinAPILayer::I_PeekMessageW() { Ret(0); }
void WinAPILayer::I_TranslateMessage() { Ret(0); }
void WinAPILayer::I_DispatchMessageA() { Ret(0); }
void WinAPILayer::I_DispatchMessageW() { Ret(0); }
void WinAPILayer::I_PostQuitMessage() { Ret(0); }
void WinAPILayer::I_DestroyWindow() { Ret(1); }
void WinAPILayer::I_SetWindowTextA() { Ret(1); }
void WinAPILayer::I_SetWindowTextW() { Ret(1); }
void WinAPILayer::I_GetClientRect() {
  const uint64_t p = Arg(2);
  if (p) {
    mem_.Write32(p, 0);
    mem_.Write32(p + 4, 0);
    mem_.Write32(p + 8, 1920);
    mem_.Write32(p + 12, 1080);
  }
  Ret(1);
}
void WinAPILayer::I_GetWindowRect() { I_GetClientRect(); }
void WinAPILayer::I_AdjustWindowRect() { Ret(1); }
void WinAPILayer::I_AdjustWindowRectEx() { Ret(1); }
void WinAPILayer::I_ClientToScreen() { Ret(1); }
void WinAPILayer::I_ScreenToClient() { Ret(1); }
void WinAPILayer::I_SetCursor() { Ret(0); }
void WinAPILayer::I_ShowCursor() { Ret(0); }
void WinAPILayer::I_SetCapture() { Ret(0); }
void WinAPILayer::I_ReleaseCapture() { Ret(1); }
void WinAPILayer::I_GetCursorPos() { Ret(1); }
void WinAPILayer::I_SetCursorPos() { Ret(1); }
void WinAPILayer::I_SendMessageA() { Ret(0); }
void WinAPILayer::I_SendMessageW() { Ret(0); }
void WinAPILayer::I_PostMessageA() { Ret(1); }
void WinAPILayer::I_PostMessageW() { Ret(1); }
void WinAPILayer::I_LoadCursorA() { Ret(1); }
void WinAPILayer::I_LoadCursorW() { Ret(1); }
void WinAPILayer::I_LoadIconA() { Ret(1); }
void WinAPILayer::I_LoadIconW() { Ret(1); }
void WinAPILayer::I_FindWindowA() { Ret(0); }
void WinAPILayer::I_FindWindowW() { Ret(0); }
void WinAPILayer::I_SetForegroundWindow() { Ret(1); }
void WinAPILayer::I_GetForegroundWindow() { Ret(0x1001); }
void WinAPILayer::I_IsWindowVisible() { Ret(1); }
void WinAPILayer::I_EnableWindow() { Ret(0); }
void WinAPILayer::I_SetWindowLongPtrA() { Ret(0); }
void WinAPILayer::I_SetWindowLongPtrW() { Ret(0); }
void WinAPILayer::I_GetWindowLongPtrA() { Ret(0); }
void WinAPILayer::I_GetWindowLongPtrW() { Ret(0); }

// ── DirectX stubs
// ─────────────────────────────────────────────────────────────
void WinAPILayer::I_Direct3DCreate9() {
  std::cerr << "[WinAPI] Direct3DCreate9 called — DirectX not yet supported. "
               "GPU output will be black.\n";
  Ret(0); // null IDirect3D9*
}
void WinAPILayer::I_Direct3DCreate9Ex() { I_Direct3DCreate9(); }
void WinAPILayer::I_D3D11CreateDevice() {
  std::cerr
      << "[WinAPI] D3D11CreateDevice called — DirectX not yet supported.\n";
  Ret(kE_NOTIMPL);
}
void WinAPILayer::I_D3D11CreateDeviceAndSwapChain() {
  std::cerr << "[WinAPI] D3D11CreateDeviceAndSwapChain called — DirectX not "
               "yet supported.\n";
  Ret(kE_NOTIMPL);
}

// ── XInput
// ────────────────────────────────────────────────────────────────────
void WinAPILayer::I_XInputGetState() {
  Ret(1167);
} // ERROR_DEVICE_NOT_CONNECTED
void WinAPILayer::I_XInputSetState() { Ret(1167); }
void WinAPILayer::I_XInputGetCapabilities() { Ret(1167); }
void WinAPILayer::I_XInputEnable() { Ret(0); }

// ── ole32 ────────────────────────────────────────────────────────────────────
void WinAPILayer::I_CoInitialize() { Ret(0); } // S_OK
void WinAPILayer::I_CoInitializeEx() { Ret(0); }
void WinAPILayer::I_CoUninitialize() { Ret(0); }
void WinAPILayer::I_CoCreateInstance() { Ret(kE_NOTIMPL); }

} // namespace AIO::Emulator::Windows
