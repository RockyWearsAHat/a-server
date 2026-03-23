#include "emulator/windows/WinProcess.h"
#include "emulator/windows/WinMemory.h"

#include <algorithm>
#include <codecvt>
#include <cstring>
#include <locale>

namespace AIO::Emulator::Windows {

// ─── PEB offsets (64-bit)
// ───────────────────────────────────────────────────── Based on Windows 10+
// x64 Process Environment Block layout.
static constexpr uint64_t kPEB_InheritedAddressSpace = 0x000;    // UCHAR
static constexpr uint64_t kPEB_ReadImageFileExecOptions = 0x001; // UCHAR
static constexpr uint64_t kPEB_BeingDebugged = 0x002;            // UCHAR
static constexpr uint64_t kPEB_BitField = 0x003; // UCHAR (NtGlobalFlag etc.)
static constexpr uint64_t kPEB_Mutant = 0x008;   // HANDLE
static constexpr uint64_t kPEB_ImageBaseAddress = 0x010; // PVOID
static constexpr uint64_t kPEB_Ldr = 0x018;              // PPEB_LDR_DATA
static constexpr uint64_t kPEB_ProcessParameters =
    0x020; // PRTL_USER_PROCESS_PARAMETERS
static constexpr uint64_t kPEB_SubSystemData = 0x028;          // PVOID
static constexpr uint64_t kPEB_ProcessHeap = 0x030;            // PVOID
static constexpr uint64_t kPEB_FastPebLockRoutine = 0x038;     // PVOID
static constexpr uint64_t kPEB_AtlThunkSListPtr = 0x040;       // PVOID
static constexpr uint64_t kPEB_IFEOKey = 0x048;                // PVOID
static constexpr uint64_t kPEB_NumberOfProcessors = 0x0B8;     // ULONG
static constexpr uint64_t kPEB_NtGlobalFlag = 0x0BC;           // ULONG
static constexpr uint64_t kPEB_CriticalSectionTimeout = 0x0C0; // LARGE_INTEGER
static constexpr uint64_t kPEB_HeapSegmentReserve = 0x0C8;     // ULONG_PTR
static constexpr uint64_t kPEB_HeapSegmentCommit = 0x0D0;      // ULONG_PTR
static constexpr uint64_t kPEB_HeapDeCommitTotalFrees = 0x0D8; // ULONG_PTR
static constexpr uint64_t kPEB_HeapDeCommitFreeBlockThr = 0x0E0; // ULONG_PTR
static constexpr uint64_t kPEB_NumberOfHeaps = 0x0E8;            // ULONG
static constexpr uint64_t kPEB_MaximumNumberOfHeaps = 0x0EC;     // ULONG
static constexpr uint64_t kPEB_ProcessHeaps = 0x0F0;             // PVOID*
static constexpr uint64_t kPEB_OSMajorVersion = 0x118;           // ULONG
static constexpr uint64_t kPEB_OSMinorVersion = 0x11C;           // ULONG
static constexpr uint64_t kPEB_OSBuildNumber = 0x120;            // USHORT
static constexpr uint64_t kPEB_OSCSDVersion = 0x122;             // USHORT
static constexpr uint64_t kPEB_OSPlatformId = 0x124;             // ULONG

// ─── TEB offsets (64-bit)
// ─────────────────────────────────────────────────────
static constexpr uint64_t kTEB_ExceptionList = 0x000; // NT_TIB.ExceptionList
static constexpr uint64_t kTEB_StackBase = 0x008;  // NT_TIB.StackBase   (top)
static constexpr uint64_t kTEB_StackLimit = 0x010; // NT_TIB.StackLimit (bottom)
static constexpr uint64_t kTEB_SubSystemTib = 0x018; // NT_TIB.SubSystemTib
static constexpr uint64_t kTEB_FiberData = 0x020;    // NT_TIB.FiberData
static constexpr uint64_t kTEB_ArbitraryUserPointer =
    0x028;                                   // NT_TIB.ArbitraryUserPointer
static constexpr uint64_t kTEB_Self = 0x030; // NT_TIB.Self = &TEB
static constexpr uint64_t kTEB_EnvironmentPointer = 0x038; // PVOID
static constexpr uint64_t kTEB_ClientId_Process = 0x040;   // UniqueProcess
static constexpr uint64_t kTEB_ClientId_Thread = 0x048;    // UniqueThread
static constexpr uint64_t kTEB_ActiveRpcHandle = 0x050;    // PVOID
static constexpr uint64_t kTEB_ThreadLocalStoragePointer = 0x058; // PVOID
static constexpr uint64_t kTEB_ProcessEnvironmentBlock = 0x060;   // PPEB
static constexpr uint64_t kTEB_LastErrorValue = 0x068;            // ULONG
static constexpr uint64_t kTEB_CountOfOwnedCriticalSect = 0x06C;  // ULONG
static constexpr uint64_t kTEB_GdiClientPID = 0x078;              // PVOID

// ─── LDR_DATA_TABLE_ENTRY offsets
// ────────────────────────────────────────────── PEB_LDR_DATA layout
// (simplified):
//   0x00 Length ULONG
//   0x04 Initialized UCHAR
//   0x08 SsHandle PVOID
//   0x10 InLoadOrderModuleList LIST_ENTRY (Flink, Blink each 8 bytes)
//   0x20 InMemoryOrderModuleList LIST_ENTRY
//   0x30 InInitializationOrderModuleList LIST_ENTRY
static constexpr size_t kLdrDataSize = 0x60;
static constexpr uint64_t kLDR_Length = 0x00;
static constexpr uint64_t kLDR_Initialized = 0x04;
static constexpr uint64_t kLDR_InLoadOrder = 0x10; // LIST_ENTRY (Flink+Blink)

// RTL_USER_PROCESS_PARAMETERS offsets (64-bit)
static constexpr uint64_t kProcParam_MaxLength = 0x000;     // ULONG
static constexpr uint64_t kProcParam_Length = 0x004;        // ULONG
static constexpr uint64_t kProcParam_Flags = 0x008;         // ULONG
static constexpr uint64_t kProcParam_DebugFlags = 0x00C;    // ULONG
static constexpr uint64_t kProcParam_ConsoleHandle = 0x010; // HANDLE
static constexpr uint64_t kProcParam_ConsoleFlags = 0x018;  // ULONG
static constexpr uint64_t kProcParam_StdInput = 0x020;      // HANDLE (stdin)
static constexpr uint64_t kProcParam_StdOutput = 0x028;     // HANDLE (stdout)
static constexpr uint64_t kProcParam_StdError = 0x030;      // HANDLE (stderr)
static constexpr uint64_t kProcParam_CurrentDir = 0x038;    // CURDIR (24 bytes)
static constexpr uint64_t kProcParam_DllPath =
    0x050; // UNICODE_STRING (16 bytes)
static constexpr uint64_t kProcParam_ImagePathName = 0x060; // UNICODE_STRING
static constexpr uint64_t kProcParam_CommandLine = 0x070;   // UNICODE_STRING
static constexpr uint64_t kProcParam_Environment = 0x080;   // PVOID
static constexpr uint64_t kProcParamStructSize = 0x100;

// ─── UNICODE_STRING helper
// ────────────────────────────────────────────────────
static void WriteUnicodeString(WinMemory &mem, uint64_t addr, uint64_t bufAddr,
                               const std::wstring &s) {
  const uint16_t len = static_cast<uint16_t>(s.size() * 2);
  const uint16_t maxLen = len + 2;
  mem.Write16(addr + 0, len);
  mem.Write16(addr + 2, maxLen);
  mem.Write64(addr + 8, bufAddr);
  mem.WriteStringW(bufAddr, s);
}

// ─────────────────────────────────────────────────────────────────────────────

WinProcess::WinProcess(WinMemory &mem) : mem_(mem) {}

void WinProcess::SetEnvVar(const std::string &name, const std::string &value) {
  env_[name] = value;
}

bool WinProcess::Initialize(uint64_t imageBase, uint64_t heapBase,
                            uint64_t stackBase, uint64_t stackSize,
                            const std::string &imagePath) {
  stackBase_ = stackBase;
  stackSize_ = stackSize;
  imagePath_ = imagePath;
  pebAddr_ = WinMemory::kPEBBase;
  tebAddr_ = WinMemory::kTEBBase;

  // Allocate PEB and TEB pages if not already present
  mem_.Allocate(pebAddr_, 0x1000, WinMemory::PAGE_READWRITE);
  mem_.Allocate(tebAddr_, 0x1000, WinMemory::PAGE_READWRITE);

  // Build sub-structures, collecting their VAs
  const uint64_t envBlockAddr = WriteEnvironmentBlock();
  const uint64_t ldrAddr = WriteLdrData(imageBase, pebAddr_ + 0x200);
  const uint64_t paramAddr = WriteProcessParameters(imageBase, envBlockAddr);

  WritePEB(imageBase, heapBase, ldrAddr);
  // Write ProcessParameters pointer into PEB (done after param block is built)
  mem_.Write64(pebAddr_ + kPEB_ProcessParameters, paramAddr);
  WriteTEB(stackBase, stackBase + stackSize);

  return true;
}

// ─── PEB
// ──────────────────────────────────────────────────────────────────────

void WinProcess::WritePEB(uint64_t imageBase, uint64_t heapBase,
                          uint64_t ldrAddr) {
  const uint64_t base = pebAddr_;
  mem_.Write8(base + kPEB_BeingDebugged, 0);
  mem_.Write64(base + kPEB_Mutant, static_cast<uint64_t>(-1)); // INVALID_HANDLE
  mem_.Write64(base + kPEB_ImageBaseAddress, imageBase);
  mem_.Write64(base + kPEB_Ldr, ldrAddr);
  // ProcessParameters written separately
  mem_.Write64(base + kPEB_ProcessHeap, heapBase);
  // OS version: Windows 10
  mem_.Write32(base + kPEB_OSMajorVersion, 10);
  mem_.Write32(base + kPEB_OSMinorVersion, 0);
  mem_.Write16(base + kPEB_OSBuildNumber, 19045); // 22H2
  mem_.Write32(base + kPEB_OSPlatformId, 2);      // VER_PLATFORM_WIN32_NT
  mem_.Write32(base + kPEB_NumberOfProcessors, 4);
  mem_.Write32(base + kPEB_NtGlobalFlag, 0);
}

// ─── TEB
// ──────────────────────────────────────────────────────────────────────

void WinProcess::WriteTEB(uint64_t stackBase, uint64_t stackTop) {
  const uint64_t base = tebAddr_;
  mem_.Write64(base + kTEB_ExceptionList, static_cast<uint64_t>(-1));
  mem_.Write64(base + kTEB_StackBase, stackTop);   // top (high addr)
  mem_.Write64(base + kTEB_StackLimit, stackBase); // bottom (low addr)
  mem_.Write64(base + kTEB_Self, tebAddr_);
  mem_.Write64(base + kTEB_ProcessEnvironmentBlock, pebAddr_);
  mem_.Write64(base + kTEB_ClientId_Process, 1); // PID
  mem_.Write64(base + kTEB_ClientId_Thread, 1);  // TID
  mem_.Write32(base + kTEB_LastErrorValue, 0);
}

// ─── PEB_LDR_DATA
// ─────────────────────────────────────────────────────────────

uint64_t WinProcess::WriteLdrData(uint64_t imageBase, uint64_t ldrAddr) {
  // Allocate a small block inside the PEB page for the LDR data
  // We use a fixed offset inside the PEB page to avoid a separate alloc
  mem_.Write32(ldrAddr + kLDR_Length, static_cast<uint32_t>(kLdrDataSize));
  mem_.Write8(ldrAddr + kLDR_Initialized, 1);

  // Minimal circular doubly-linked list: point Flink and Blink to themselves
  // so ntdll's module walk terminates cleanly.
  const uint64_t loadOrderList = ldrAddr + kLDR_InLoadOrder;
  mem_.Write64(loadOrderList + 0, loadOrderList); // Flink
  mem_.Write64(loadOrderList + 8, loadOrderList); // Blink

  // InMemoryOrder and InInitializationOrder entries also need valid lists
  const uint64_t memOrderList = ldrAddr + kLDR_InLoadOrder + 0x10;
  const uint64_t initOrderList = ldrAddr + kLDR_InLoadOrder + 0x20;
  mem_.Write64(memOrderList + 0, memOrderList);
  mem_.Write64(memOrderList + 8, memOrderList);
  mem_.Write64(initOrderList + 0, initOrderList);
  mem_.Write64(initOrderList + 8, initOrderList);

  (void)imageBase; // reserved for future LDR_DATA_TABLE_ENTRY population
  return ldrAddr;
}

// ─── Environment block
// ────────────────────────────────────────────────────────

uint64_t WinProcess::WriteEnvironmentBlock() {
  // Build a UTF-16LE environment block: KEY=VALUE\0 … \0\0
  // Emit default vars first, then user overrides
  std::unordered_map<std::string, std::string> effective = {
      {"TEMP", "/tmp"},
      {"TMP", "/tmp"},
      {"SystemRoot", "C:\\Windows"},
      {"SystemDrive", "C:"},
      {"COMSPEC", "C:\\Windows\\System32\\cmd.exe"},
      {"PATHEXT", ".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC"},
      {"windir", "C:\\Windows"},
      {"NUMBER_OF_PROCESSORS", "4"},
      {"PROCESSOR_ARCHITECTURE", "AMD64"},
      {"OS", "Windows_NT"},
  };
  // Apply user overrides
  for (const auto &[k, v] : env_)
    effective[k] = v;

  // Measure total size in UTF-16 chars
  size_t totalChars = 0;
  for (const auto &[k, v] : effective)
    totalChars += k.size() + 1 + v.size() + 1; // key=value\0
  totalChars += 1;                             // final null terminator

  const uint64_t regionSize = (totalChars * 2 + 0xFFF) & ~uint64_t(0xFFF);
  const uint64_t envAddr =
      mem_.Allocate(0, regionSize, WinMemory::PAGE_READWRITE);
  uint64_t cursor = envAddr;

  for (const auto &[k, v] : effective) {
    const std::string pair = k + "=" + v;
    for (char c : pair) {
      mem_.Write16(cursor,
                   static_cast<uint16_t>(static_cast<unsigned char>(c)));
      cursor += 2;
    }
    mem_.Write16(cursor, 0); // null terminator for this entry
    cursor += 2;
  }
  mem_.Write16(cursor, 0); // double-null end of block

  return envAddr;
}

// ─── RTL_USER_PROCESS_PARAMETERS ─────────────────────────────────────────────

uint64_t WinProcess::WriteProcessParameters(uint64_t /*imageBase*/,
                                            uint64_t envBlockAddr) {
  const uint64_t paramAddr =
      mem_.Allocate(0, 0x1000, WinMemory::PAGE_READWRITE);

  // UNICODE_STRING buffers live immediately after the structure
  uint64_t strBuf = paramAddr + kProcParamStructSize;

  const std::wstring wImagePath(imagePath_.begin(), imagePath_.end());
  const std::wstring wCmdLine = wImagePath;
  const std::wstring wCurDir = L"C:\\";
  const std::wstring wDllPath = L"C:\\Windows\\System32;.";

  // DllPath
  WriteUnicodeString(mem_, paramAddr + kProcParam_DllPath, strBuf, wDllPath);
  strBuf += (wDllPath.size() + 2) * 2;

  // ImagePathName
  WriteUnicodeString(mem_, paramAddr + kProcParam_ImagePathName, strBuf,
                     wImagePath);
  strBuf += (wImagePath.size() + 2) * 2;

  // CommandLine
  WriteUnicodeString(mem_, paramAddr + kProcParam_CommandLine, strBuf,
                     wCmdLine);
  strBuf += (wCmdLine.size() + 2) * 2;

  // Environment pointer
  mem_.Write64(paramAddr + kProcParam_Environment, envBlockAddr);

  // Fake STD handles
  mem_.Write64(paramAddr + kProcParam_StdInput, 0xF0);
  mem_.Write64(paramAddr + kProcParam_StdOutput, 0xF4);
  mem_.Write64(paramAddr + kProcParam_StdError, 0xF8);

  // Length fields
  mem_.Write32(paramAddr + kProcParam_MaxLength, 0x1000);
  mem_.Write32(paramAddr + kProcParam_Length,
               static_cast<uint32_t>(kProcParamStructSize));
  mem_.Write32(paramAddr + kProcParam_Flags,
               0x4001); // RTL_USER_PROC_PARAMS_NORMALIZED

  return paramAddr;
}

} // namespace AIO::Emulator::Windows
