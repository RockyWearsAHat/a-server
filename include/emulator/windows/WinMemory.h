#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace AIO::Emulator::Windows {

struct MappedRegion {
    uint64_t             base = 0;
    uint64_t             size = 0;
    uint32_t             prot = 0;
    std::vector<uint8_t> data;
};

// Virtual address space manager + Windows PE loader.
class WinMemory {
public:
    // PAGE_* protection flags (subset that matters for us)
    static constexpr uint32_t PAGE_NOACCESS          = 0x01;
    static constexpr uint32_t PAGE_READONLY          = 0x02;
    static constexpr uint32_t PAGE_READWRITE         = 0x04;
    static constexpr uint32_t PAGE_EXECUTE           = 0x10;
    static constexpr uint32_t PAGE_EXECUTE_READ      = 0x20;
    static constexpr uint32_t PAGE_EXECUTE_READWRITE = 0x40;

    explicit WinMemory();
    ~WinMemory() = default;

    // Allocate a region.  If preferredBase==0 the allocator picks one.
    uint64_t Allocate(uint64_t preferredBase, uint64_t size,
                      uint32_t prot = PAGE_READWRITE);
    void     Free(uint64_t base);
    bool     Protect(uint64_t base, uint64_t size, uint32_t prot);

    // Bounds-checked raw access
    bool Read (uint64_t addr, void* buf,       size_t len) const;
    bool Write(uint64_t addr, const void* buf, size_t len);

    uint8_t  Read8 (uint64_t addr) const;
    uint16_t Read16(uint64_t addr) const;
    uint32_t Read32(uint64_t addr) const;
    uint64_t Read64(uint64_t addr) const;
    void Write8 (uint64_t addr, uint8_t  v);
    void Write16(uint64_t addr, uint16_t v);
    void Write32(uint64_t addr, uint32_t v);
    void Write64(uint64_t addr, uint64_t v);

    // String helpers
    uint64_t    WriteStringA(uint64_t addr, const std::string& s);
    uint64_t    WriteStringW(uint64_t addr, const std::wstring& s);
    std::string ReadStringA (uint64_t addr) const;

    // ── PE loading ───────────────────────────────────────────────────────
    struct LoadedModule {
        uint64_t    base       = 0;
        uint64_t    size       = 0;
        uint64_t    entryPoint = 0; // absolute VA
        std::string name;           // basename (e.g. "game.exe")
        std::string path;           // full path
    };

    // Returns absolute entry-point VA on success, 0 on failure.
    uint64_t LoadPE(const std::string& path, uint64_t preferredBase = 0);
    uint64_t LoadPEFromData(const std::vector<uint8_t>& data,
                            const std::string& name,
                            uint64_t preferredBase = 0);

    const std::vector<LoadedModule>& GetModules()  const { return modules_; }
    uint64_t GetModuleBase(const std::string& name) const;

    // Import resolution hook — WinAPILayer registers this before loading PE
    using ImportResolver =
        std::function<uint64_t(const std::string& dll,
                               const std::string& func)>;
    void SetImportResolver(ImportResolver r) { importResolver_ = std::move(r); }

    // Allocate an 8-byte slot in the API-stub trampoline region
    uint64_t AllocStub();

    // Well-known fixed virtual addresses used by WinProcess
    static constexpr uint64_t kPEBBase  = 0x7FFF'0000ULL;
    static constexpr uint64_t kTEBBase  = 0x7FFE'8000ULL;
    static constexpr uint64_t kStubBase = 0x7FF0'0000ULL; // 64 KB
    static constexpr uint64_t kHeapBase = 0x1'0000'0000ULL; // 64 MB
    static constexpr uint64_t kStackTop = 0x7FFE'0000ULL;
    static constexpr uint64_t kStackSize= 4 * 1024 * 1024; // 4 MB
    // EXEs load here if they have no preferred base / ASLR clears it
    static constexpr uint64_t kDefaultImageBase = 0x1'4000'0000ULL;

private:
    // Sorted by base address for fast lookup
    std::map<uint64_t, MappedRegion> regions_;

    uint64_t nextFreeAddr_  = 0x2'0000'0000ULL;  // auto-alloc cursor
    uint64_t nextStubAddr_  = kStubBase;

    std::vector<LoadedModule> modules_;
    ImportResolver             importResolver_;

    MappedRegion*       FindRegion(uint64_t addr);
    const MappedRegion* FindRegion(uint64_t addr) const;

    // PE parsing helpers
    uint64_t DoParsePE(const std::vector<uint8_t>& data,
                       const std::string& name,
                       uint64_t preferredBase);
    bool MapSections    (const std::vector<uint8_t>& data, uint64_t loadBase,
                         uint32_t numSections,
                         uint64_t sectionHeaderOffset);
    bool ApplyRelocations(const std::vector<uint8_t>& data, uint64_t loadBase,
                          uint64_t origBase,
                          uint64_t relocRVA, uint32_t relocSize);
    bool ResolveImports (const std::vector<uint8_t>& data, uint64_t loadBase,
                         uint64_t importRVA);
};

} // namespace AIO::Emulator::Windows
