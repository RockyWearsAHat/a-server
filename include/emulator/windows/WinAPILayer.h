#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace AIO::Emulator::Windows {

class WinMemory;
class X86_64Core;

// Windows API translation layer.
//
// All imported functions from kernel32.dll, ntdll.dll, msvcrt.dll, user32.dll,
// etc. are resolved to stub virtual addresses in a dedicated trampoline page.
// When the CPU executes CALL <stub>, we intercept, run the host implementation,
// put the return value in RAX, and let the CPU's CALL-handler do the RET.
class WinAPILayer {
public:
  explicit WinAPILayer(WinMemory &mem);
  ~WinAPILayer() = default;

  // Wire back-pointer after construction (avoids circular dependency).
  void SetCPU(X86_64Core *cpu) { cpu_ = cpu; }

  // Register all stub addresses.  Call once before loading the PE.
  void Initialize();

  // Called by WinMemory's import resolver.
  // Returns the stub VA for the given import, or 0 if not in our table.
  uint64_t Resolve(const std::string &dll, const std::string &func);

  // Called by the CPU whenever RIP lands on a stub address.
  // Executes the host implementation and returns true; returns false if
  // the address is unknown (CPU should fault).
  bool Dispatch(uint64_t stubAddr);

  // ── HANDLE table ────────────────────────────────────────────────────
  enum HandleType {
    HT_INVALID = 0,
    HT_FILE = 1,
    HT_MUTEX = 2,
    HT_EVENT = 3,
    HT_THREAD = 4,
    HT_PROCESS = 5
  };
  struct HandleEntry {
    HandleType type = HT_INVALID;
    uint64_t data = 0; // type-specific payload (e.g. FILE* cast to uint64)
    bool valid = false;
  };
  uint64_t AllocHandle(HandleType type, uint64_t data);
  bool FreeHandle(uint64_t h);
  HandleEntry *GetHandle(uint64_t h);

  // Win32 error code storage (GetLastError / SetLastError)
  uint32_t lastError = 0;

  // Address the CPU should CALL to trigger ExitProcess (planted on EXE entry)
  uint64_t GetExitStubAddr() const { return exitStubAddr_; }

  // SetCommandLine — call before Initialize so GetCommandLine stubs work
  void SetCommandLine(const std::string &path);

private:
  WinMemory &mem_;
  X86_64Core *cpu_ = nullptr;

  using APIFunc = std::function<void()>;
  std::unordered_map<uint64_t, APIFunc> stubDispatch_;
  std::unordered_map<std::string, uint64_t> nameToStub_; // "kernel32!Foo"

  uint64_t exitStubAddr_ = 0;

  // ── Heap allocator (simple arena backed by a dedicated region) ───────
  uint64_t heapBase_ = 0;
  uint64_t heapNext_ = 0;
  static constexpr uint64_t kHeapRegionSize = 128ULL * 1024 * 1024; // 128 MB

  uint64_t HeapAlloc_(uint64_t size);
  void HeapFree_(uint64_t addr);

  // Heap block tracking for reuse
  struct HeapBlock {
    uint64_t addr;
    uint64_t size;
    bool free;
  };
  std::vector<HeapBlock> heapBlocks_;

  // ── File handles ─────────────────────────────────────────────────────
  std::vector<HandleEntry> handles_;
  std::vector<FILE *>
      fileSlots_; // index = internal slot id, stored in HandleEntry.data

  FILE *GetFile(uint64_t h);

  // ── Persistent string buffers ────────────────────────────────────────
  uint64_t cmdLineAddrA_ = 0;
  uint64_t cmdLineAddrW_ = 0;
  uint64_t processHeapHandle_ =
      0x10001; // fake HANDLE value for the default heap

  // ── Stub registration ────────────────────────────────────────────────
  uint64_t AddStub(const std::string &dll, const std::string &func, APIFunc fn);

  // ── Argument helpers (Microsoft x64 ABI) ────────────────────────────
  // Args 1-4: RCX, RDX, R8, R9.  Args 5+: [RSP+0x28], [RSP+0x30], …
  uint64_t Arg(int n) const; // 1-based
  void Ret(uint64_t v);      // set RAX

  // ── All stub implementations ─────────────────────────────────────────
  // kernel32
  void I_ExitProcess();
  void I_GetModuleHandleA();
  void I_GetModuleHandleW();
  void I_GetProcAddress();
  void I_LoadLibraryA();
  void I_LoadLibraryW();
  void I_FreeLibrary();
  void I_VirtualAlloc();
  void I_VirtualFree();
  void I_VirtualProtect();
  void I_CreateFileA();
  void I_CreateFileW();
  void I_ReadFile();
  void I_WriteFile();
  void I_CloseHandle();
  void I_GetSystemInfo();
  void I_GetLastError();
  void I_SetLastError();
  void I_InitializeCriticalSection();
  void I_InitializeCriticalSectionEx();
  void I_EnterCriticalSection();
  void I_LeaveCriticalSection();
  void I_DeleteCriticalSection();
  void I_TryEnterCriticalSection();
  void I_GetTickCount();
  void I_GetTickCount64();
  void I_Sleep();
  void I_SleepEx();
  void I_QueryPerformanceCounter();
  void I_QueryPerformanceFrequency();
  void I_HeapAlloc();
  void I_HeapFree();
  void I_HeapCreate();
  void I_HeapReAlloc();
  void I_HeapSize();
  void I_HeapDestroy();
  void I_LocalAlloc();
  void I_LocalFree();
  void I_LocalReAlloc();
  void I_GlobalAlloc();
  void I_GlobalFree();
  void I_GetProcessHeap();
  void I_GetCurrentProcessId();
  void I_GetCurrentThreadId();
  void I_GetCurrentProcess();
  void I_GetCurrentThread();
  void I_IsDebuggerPresent();
  void I_CheckRemoteDebuggerPresent();
  void I_OutputDebugStringA();
  void I_OutputDebugStringW();
  void I_GetCommandLineA();
  void I_GetCommandLineW();
  void I_GetEnvironmentVariableA();
  void I_GetEnvironmentVariableW();
  void I_GetStdHandle();
  void I_SetConsoleMode();
  void I_GetConsoleMode();
  void I_SetConsoleTitleA();
  void I_SetConsoleTitleW();
  void I_AllocConsole();
  void I_FlushFileBuffers();
  void I_SetFilePointer();
  void I_SetFilePointerEx();
  void I_GetFileSize();
  void I_GetFileSizeEx();
  void I_CreateMutexA();
  void I_CreateMutexW();
  void I_ReleaseMutex();
  void I_CreateEventA();
  void I_CreateEventW();
  void I_SetEvent();
  void I_ResetEvent();
  void I_WaitForSingleObject();
  void I_WaitForSingleObjectEx();
  void I_WaitForMultipleObjects();
  void I_CreateThread();
  void I_ExitThread();
  void I_GetThreadId();
  void I_SetThreadPriority();
  void I_GetThreadPriority();
  void I_ResumeThread();
  void I_SuspendThread();
  void I_TerminateThread();
  void I_GetSystemTimeAsFileTime();
  void I_GetLocalTime();
  void I_GetSystemTime();
  void I_FileTimeToSystemTime();
  void I_SystemTimeToFileTime();
  void I_GetFileAttributesA();
  void I_GetFileAttributesW();
  void I_SetFileAttributesA();
  void I_CreateDirectoryA();
  void I_CreateDirectoryW();
  void I_DeleteFileA();
  void I_DeleteFileW();
  void I_MoveFileA();
  void I_CopyFileA();
  void I_FindFirstFileA();
  void I_FindFirstFileW();
  void I_FindNextFileA();
  void I_FindNextFileW();
  void I_FindClose();
  void I_GetFullPathNameA();
  void I_GetFullPathNameW();
  void I_GetTempPathA();
  void I_GetTempPathW();
  void I_GetModuleFileNameA();
  void I_GetModuleFileNameW();
  void I_SetUnhandledExceptionFilter();
  void I_UnhandledExceptionFilter();
  void I_RaiseException();
  void I_TerminateProcess();
  void I_GetSystemDirectoryA();
  void I_GetSystemDirectoryW();
  void I_GetWindowsDirectoryA();
  void I_GetWindowsDirectoryW();
  void I_ExpandEnvironmentStringsA();
  void I_MultiByteToWideChar();
  void I_WideCharToMultiByte();
  void I_FormatMessageA();
  void I_FormatMessageW();
  void I_InterlockedIncrement();
  void I_InterlockedDecrement();
  void I_InterlockedExchange();
  void I_InterlockedCompareExchange();
  void I_InterlockedAdd();
  void I_CreateFileMappingA();
  void I_CreateFileMappingW();
  void I_MapViewOfFile();
  void I_UnmapViewOfFile();
  void I_GetNativeSystemInfo();
  void I_IsWow64Process();
  void I_InitOnceExecuteOnce();
  // ntdll
  void I_NtAllocateVirtualMemory();
  void I_NtFreeVirtualMemory();
  void I_NtProtectVirtualMemory();
  void I_NtCreateFile();
  void I_NtReadFile();
  void I_NtWriteFile();
  void I_NtClose();
  void I_NtQueryInformationProcess();
  void I_NtQuerySystemInformation();
  void I_NtTerminateProcess();
  void I_NtTerminateThread();
  void I_RtlAllocateHeap();
  void I_RtlFreeHeap();
  void I_RtlReAllocateHeap();
  void I_RtlSizeHeap();
  void I_RtlInitializeCriticalSection();
  void I_RtlInitializeCriticalSectionEx();
  void I_RtlEnterCriticalSection();
  void I_RtlLeaveCriticalSection();
  void I_RtlDeleteCriticalSection();
  void I_RtlTryCriticalSection();
  void I_LdrLoadDll();
  void I_LdrGetDllHandleByName();
  void I_LdrGetProcedureAddress();
  void I_RtlExitUserProcess();
  void I_RtlGetVersion();
  void I_NtQueryVirtualMemory();
  void I_RtlCaptureStackBackTrace();
  void I_RtlRandom();
  void I_RtlUniform();
  // msvcrt / ucrtbase / vcruntime
  void I_malloc();
  void I_free();
  void I_realloc();
  void I_calloc();
  void I_printf();
  void I_fprintf();
  void I_sprintf();
  void I_snprintf();
  void I_sscanf();
  void I_vprintf();
  void I_vsprintf();
  void I_vsnprintf();
  void I_strlen();
  void I_wcslen();
  void I_strcpy();
  void I_strncpy();
  void I_strcmp();
  void I_strncmp();
  void I_strcat();
  void I_strncat();
  void I_strdup();
  void I_strchr();
  void I_strstr();
  void I_strtod();
  void I_strtol();
  void I_strtoul();
  void I_memcpy();
  void I_memmove();
  void I_memset();
  void I_memcmp();
  void I_memchr();
  void I_atoi();
  void I_atol();
  void I_atof();
  void I_itoa();
  void I_exit();
  void I_abort();
  void I__exit();
  void I_rand();
  void I_srand();
  void I_time();
  void I_clock();
  void I_difftime();
  void I_fopen();
  void I_fclose();
  void I_fread();
  void I_fwrite();
  void I_fseek();
  void I_ftell();
  void I_feof();
  void I_fflush();
  void I_fgets();
  void I_fputs();
  void I_fputc();
  void I_fgetc();
  void I_getc();
  void I_putc();
  void I_puts();
  void I_gets();
  void I_clearerr();
  void I_ferror();
  void I_rewind();
  void I__fileno();
  void I__CRT_INIT();
  void I___CxxFrameHandler3();
  void I___C_specific_handler();
  void I___chkstk();
  void I___security_cookie();
  void I___security_check_cookie();
  void I__get_initial_narrow_environment();
  void I__configure_narrow_argv();
  void I__initialize_narrow_environment();
  void I__cexit();
  void I__conexit();
  void I__initterm();
  void I__initterm_e();
  void I__register_onexit_function();
  void I__execute_onexit_table();
  void I__crt_atexit();
  void I_atexit();
  void I_fesetenv();
  void I_fegetenv();
  void I__controlfp_s();
  void I__set_app_type();
  // user32
  void I_MessageBoxA();
  void I_MessageBoxW();
  void I_CreateWindowExA();
  void I_CreateWindowExW();
  void I_RegisterClassExA();
  void I_RegisterClassExW();
  void I_ShowWindow();
  void I_UpdateWindow();
  void I_DefWindowProcA();
  void I_DefWindowProcW();
  void I_GetMessageA();
  void I_GetMessageW();
  void I_PeekMessageA();
  void I_PeekMessageW();
  void I_TranslateMessage();
  void I_DispatchMessageA();
  void I_DispatchMessageW();
  void I_PostQuitMessage();
  void I_DestroyWindow();
  void I_SetWindowTextA();
  void I_SetWindowTextW();
  void I_GetClientRect();
  void I_GetWindowRect();
  void I_AdjustWindowRect();
  void I_AdjustWindowRectEx();
  void I_ClientToScreen();
  void I_ScreenToClient();
  void I_SetCursor();
  void I_ShowCursor();
  void I_SetCapture();
  void I_ReleaseCapture();
  void I_GetCursorPos();
  void I_SetCursorPos();
  void I_SendMessageA();
  void I_SendMessageW();
  void I_PostMessageA();
  void I_PostMessageW();
  void I_LoadCursorA();
  void I_LoadCursorW();
  void I_LoadIconA();
  void I_LoadIconW();
  void I_FindWindowA();
  void I_FindWindowW();
  void I_SetForegroundWindow();
  void I_GetForegroundWindow();
  void I_IsWindowVisible();
  void I_EnableWindow();
  void I_SetWindowLongPtrA();
  void I_SetWindowLongPtrW();
  void I_GetWindowLongPtrA();
  void I_GetWindowLongPtrW();
  // d3d9 / d3d11 stubs (error path — signals DirectX support needed)
  void I_Direct3DCreate9();
  void I_Direct3DCreate9Ex();
  void I_D3D11CreateDevice();
  void I_D3D11CreateDeviceAndSwapChain();
  // xinput
  void I_XInputGetState();
  void I_XInputSetState();
  void I_XInputGetCapabilities();
  void I_XInputEnable();
  // ole32
  void I_CoInitialize();
  void I_CoInitializeEx();
  void I_CoUninitialize();
  void I_CoCreateInstance();
};

} // namespace AIO::Emulator::Windows
