#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace AIO::Emulator::Windows {

class WinMemory;

// Sets up the Windows in-process data structures that games expect:
//   - PEB  (Process Environment Block) at kPEBBase
//   - TEB  (Thread Environment Block) for the main thread at kTEBBase
//   - Stack guard pages, environment block, process parameters
class WinProcess {
public:
    explicit WinProcess(WinMemory& mem);
    ~WinProcess() = default;

    // Call after WinMemory has the PEB/TEB regions allocated.
    // imageBase  : load address of the main EXE
    // heapBase   : address returned for GetProcessHeap / PEB.ProcessHeap
    // stackBase  : lowest address of the stack region
    // stackSize  : size in bytes
    // imagePath  : full path to the EXE being run
    bool Initialize(uint64_t imageBase,
                    uint64_t heapBase,
                    uint64_t stackBase,
                    uint64_t stackSize,
                    const std::string& imagePath);

    uint64_t GetPEBAddress()  const { return pebAddr_;   }
    uint64_t GetTEBAddress()  const { return tebAddr_;   }
    uint64_t GetStackBase()   const { return stackBase_; }
    uint64_t GetStackTop()    const { return stackBase_ + stackSize_; }

    // Set environment variables before calling Initialize.
    void SetEnvVar(const std::string& name, const std::string& value);

    const std::string& GetImagePath() const { return imagePath_; }

private:
    WinMemory& mem_;

    uint64_t pebAddr_   = 0;
    uint64_t tebAddr_   = 0;
    uint64_t stackBase_ = 0;
    uint64_t stackSize_ = 0;
    std::string imagePath_;

    std::unordered_map<std::string, std::string> env_;

    void WritePEB(uint64_t imageBase, uint64_t heapBase, uint64_t ldrAddr);
    void WriteTEB(uint64_t stackBase, uint64_t stackTop);
    uint64_t WriteProcessParameters(uint64_t imageBase,
                                    uint64_t envBlockAddr);
    uint64_t WriteEnvironmentBlock();
    uint64_t WriteLdrData(uint64_t imageBase, uint64_t ldrAddr);
};

} // namespace AIO::Emulator::Windows
