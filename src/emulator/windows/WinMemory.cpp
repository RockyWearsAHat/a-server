#include "emulator/windows/WinMemory.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace AIO::Emulator::Windows {

// ─── PE structure constants ───────────────────────────────────────────────────

static constexpr uint16_t kMZMagic       = 0x5A4D;
static constexpr uint32_t kPESignature   = 0x00004550;
static constexpr uint16_t kMachineAmd64  = 0x8664;
static constexpr uint16_t kPE32PlusMagic = 0x020B;

// Data directory indices
static constexpr int kDirExport   = 0;
static constexpr int kDirImport   = 1;
static constexpr int kDirReloc    = 5;

// Base relocation types
static constexpr uint8_t kRelocAbsolute = 0;
static constexpr uint8_t kRelocDir64    = 10;

// ─────────────────────────────────────────────────────────────────────────────

WinMemory::WinMemory() {
    // Pre-allocate the stub trampoline region so stubs can be issued immediately
    MappedRegion stubRegion;
    stubRegion.base = kStubBase;
    stubRegion.size = 0x10000; // 64 KB
    stubRegion.prot = PAGE_EXECUTE_READ;
    stubRegion.data.assign(0x10000, 0xC3); // fill with RET (0xC3) as safety net
    regions_[kStubBase] = std::move(stubRegion);
}

// ─── Region management ───────────────────────────────────────────────────────

MappedRegion* WinMemory::FindRegion(uint64_t addr) {
    // Walk backwards from the first entry > addr
    auto it = regions_.upper_bound(addr);
    if (it == regions_.begin()) return nullptr;
    --it;
    if (addr >= it->second.base && addr < it->second.base + it->second.size)
        return &it->second;
    return nullptr;
}

const MappedRegion* WinMemory::FindRegion(uint64_t addr) const {
    auto it = regions_.upper_bound(addr);
    if (it == regions_.begin()) return nullptr;
    --it;
    if (addr >= it->second.base && addr < it->second.base + it->second.size)
        return &it->second;
    return nullptr;
}

uint64_t WinMemory::Allocate(uint64_t preferredBase, uint64_t size,
                              uint32_t prot) {
    // Align size to 4 KB pages
    size = (size + 0xFFF) & ~uint64_t(0xFFF);

    uint64_t base = preferredBase;
    if (base == 0) {
        base = nextFreeAddr_;
        nextFreeAddr_ = base + size + 0x10000; // leave a guard gap
    }

    MappedRegion r;
    r.base = base;
    r.size = size;
    r.prot = prot;
    r.data.assign(size, 0);
    regions_[base] = std::move(r);
    return base;
}

void WinMemory::Free(uint64_t base) {
    regions_.erase(base);
}

bool WinMemory::Protect(uint64_t base, uint64_t size, uint32_t prot) {
    MappedRegion* r = FindRegion(base);
    if (!r) return false;
    r->prot = prot;
    (void)size;
    return true;
}

uint64_t WinMemory::AllocStub() {
    uint64_t addr = nextStubAddr_;
    nextStubAddr_ += 8;
    return addr;
}

// ─── Raw R/W ─────────────────────────────────────────────────────────────────

bool WinMemory::Read(uint64_t addr, void* buf, size_t len) const {
    const MappedRegion* r = FindRegion(addr);
    if (!r || addr + len > r->base + r->size) return false;
    std::memcpy(buf, r->data.data() + (addr - r->base), len);
    return true;
}

bool WinMemory::Write(uint64_t addr, const void* buf, size_t len) {
    MappedRegion* r = FindRegion(addr);
    if (!r || addr + len > r->base + r->size) return false;
    std::memcpy(r->data.data() + (addr - r->base), buf, len);
    return true;
}

uint8_t  WinMemory::Read8 (uint64_t a) const { uint8_t  v=0; Read(a,&v,1); return v; }
uint16_t WinMemory::Read16(uint64_t a) const { uint16_t v=0; Read(a,&v,2); return v; }
uint32_t WinMemory::Read32(uint64_t a) const { uint32_t v=0; Read(a,&v,4); return v; }
uint64_t WinMemory::Read64(uint64_t a) const { uint64_t v=0; Read(a,&v,8); return v; }
void WinMemory::Write8 (uint64_t a, uint8_t  v) { Write(a,&v,1); }
void WinMemory::Write16(uint64_t a, uint16_t v) { Write(a,&v,2); }
void WinMemory::Write32(uint64_t a, uint32_t v) { Write(a,&v,4); }
void WinMemory::Write64(uint64_t a, uint64_t v) { Write(a,&v,8); }

uint64_t WinMemory::WriteStringA(uint64_t addr, const std::string& s) {
    Write(addr, s.c_str(), s.size() + 1);
    return addr + s.size() + 1;
}

uint64_t WinMemory::WriteStringW(uint64_t addr, const std::wstring& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        uint16_t ch = static_cast<uint16_t>(s[i]);
        Write16(addr + i * 2, ch);
    }
    Write16(addr + s.size() * 2, 0);
    return addr + (s.size() + 1) * 2;
}

std::string WinMemory::ReadStringA(uint64_t addr) const {
    std::string out;
    for (int i = 0; i < 4096; ++i) {
        const MappedRegion* r = FindRegion(addr + i);
        if (!r) break;
        uint8_t c = r->data[addr + i - r->base];
        if (c == 0) break;
        out += static_cast<char>(c);
    }
    return out;
}

uint64_t WinMemory::GetModuleBase(const std::string& name) const {
    for (const auto& m : modules_) {
        // case-insensitive basename compare
        std::string mn = m.name;
        std::string qn = name;
        for (auto& c : mn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : qn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (mn == qn) return m.base;
    }
    return 0;
}

// ─── PE loading ──────────────────────────────────────────────────────────────

uint64_t WinMemory::LoadPE(const std::string& path, uint64_t preferredBase) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "[WinMem] Cannot open: " << path << "\n";
        return 0;
    }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), sz);

    // Extract basename
    std::string name = path;
    const auto sep = name.find_last_of("/\\");
    if (sep != std::string::npos) name = name.substr(sep + 1);

    const uint64_t ep = DoParsePE(data, name, preferredBase);
    if (ep) {
        // Record full path in last module entry
        modules_.back().path = path;
    }
    return ep;
}

uint64_t WinMemory::LoadPEFromData(const std::vector<uint8_t>& data,
                                    const std::string& name,
                                    uint64_t preferredBase) {
    return DoParsePE(data, name, preferredBase);
}

uint64_t WinMemory::DoParsePE(const std::vector<uint8_t>& data,
                               const std::string& name,
                               uint64_t preferredBase) {
    if (data.size() < 0x40) { std::cerr << "[PE] Too small\n"; return 0; }

    // DOS header
    const uint16_t e_magic = *reinterpret_cast<const uint16_t*>(data.data());
    if (e_magic != kMZMagic) { std::cerr << "[PE] Bad MZ magic\n"; return 0; }

    const uint32_t e_lfanew = *reinterpret_cast<const uint32_t*>(data.data() + 0x3C);
    if (e_lfanew + 0x50 > data.size()) { std::cerr << "[PE] e_lfanew OOB\n"; return 0; }

    // PE signature
    const uint32_t peSig = *reinterpret_cast<const uint32_t*>(data.data() + e_lfanew);
    if (peSig != kPESignature) { std::cerr << "[PE] Bad PE signature\n"; return 0; }

    // IMAGE_FILE_HEADER  (offset e_lfanew+4)
    const uint8_t* fhdr = data.data() + e_lfanew + 4;
    const uint16_t machine        = *reinterpret_cast<const uint16_t*>(fhdr + 0);
    const uint16_t numSections    = *reinterpret_cast<const uint16_t*>(fhdr + 2);
    const uint16_t sizeOptHdr     = *reinterpret_cast<const uint16_t*>(fhdr + 16);

    if (machine != kMachineAmd64) {
        std::cerr << "[PE] Not x86-64 (machine=0x" << std::hex << machine << ")\n";
        return 0;
    }

    // IMAGE_OPTIONAL_HEADER64  (offset e_lfanew+4+20)
    const uint8_t* ohdr = fhdr + 20;
    if (ohdr - data.data() + 0x70 > (ptrdiff_t)data.size()) {
        std::cerr << "[PE] Optional header OOB\n"; return 0;
    }

    const uint16_t magic = *reinterpret_cast<const uint16_t*>(ohdr + 0);
    if (magic != kPE32PlusMagic) {
        std::cerr << "[PE] Not PE32+ (magic=0x" << std::hex << magic << ")\n";
        return 0;
    }

    const uint32_t epRVA        = *reinterpret_cast<const uint32_t*>(ohdr + 0x10);
    const uint64_t origImgBase  = *reinterpret_cast<const uint64_t*>(ohdr + 0x18);
    const uint32_t sectionAlign = *reinterpret_cast<const uint32_t*>(ohdr + 0x20);
    const uint32_t imageSize    = *reinterpret_cast<const uint32_t*>(ohdr + 0x38);
    const uint32_t numDataDirs  = *reinterpret_cast<const uint32_t*>(ohdr + 0x60);

    (void)sectionAlign;

    // Data directories (each is 8 bytes: [RVA:4][Size:4])
    const uint8_t* dataDirs = ohdr + 0x68;

    uint64_t importRVA  = 0;
    uint64_t relocRVA   = 0;
    uint32_t relocSize  = 0;

    if (numDataDirs > (uint32_t)kDirImport) {
        importRVA = *reinterpret_cast<const uint32_t*>(dataDirs + kDirImport * 8);
    }
    if (numDataDirs > (uint32_t)kDirReloc) {
        relocRVA  = *reinterpret_cast<const uint32_t*>(dataDirs + kDirReloc * 8);
        relocSize = *reinterpret_cast<const uint32_t*>(dataDirs + kDirReloc * 8 + 4);
    }

    // Choose load address
    uint64_t loadBase = preferredBase ? preferredBase
                      : (origImgBase  ? origImgBase : kDefaultImageBase);

    // If the preferred base is already occupied, pick the next free address
    if (FindRegion(loadBase)) {
        loadBase = nextFreeAddr_;
        nextFreeAddr_ = loadBase + imageSize + 0x10000;
    }

    // Allocate space for the whole image
    Allocate(loadBase, imageSize, PAGE_EXECUTE_READWRITE);

    // Map section headers start: e_lfanew + 4 (Sig) + 20 (FileHdr) + sizeOptHdr
    const uint64_t sectionStart = e_lfanew + 4 + 20 + sizeOptHdr;
    if (!MapSections(data, loadBase, numSections, sectionStart)) return 0;

    // Apply base relocations if load address differs from preferred
    if (relocRVA && relocSize && loadBase != origImgBase) {
        if (!ApplyRelocations(data, loadBase, origImgBase, relocRVA, relocSize))
            return 0;
    }

    // Resolve imports
    if (importRVA) {
        if (!ResolveImports(data, loadBase, importRVA)) return 0;
    }

    const uint64_t entryPointVA = loadBase + epRVA;

    LoadedModule mod;
    mod.base       = loadBase;
    mod.size       = imageSize;
    mod.entryPoint = entryPointVA;
    mod.name       = name;
    modules_.push_back(std::move(mod));

    std::cout << "[PE] Loaded '" << name << "' at 0x" << std::hex << loadBase
              << " EP=0x" << entryPointVA << std::dec << "\n";
    return entryPointVA;
}

bool WinMemory::MapSections(const std::vector<uint8_t>& data,
                             uint64_t loadBase,
                             uint32_t numSections,
                             uint64_t sectionHeaderOffset) {
    // IMAGE_SECTION_HEADER is 40 bytes each
    for (uint32_t i = 0; i < numSections; ++i) {
        const uint64_t off = sectionHeaderOffset + i * 40;
        if (off + 40 > data.size()) return false;

        const uint32_t virtualSize   = *reinterpret_cast<const uint32_t*>(data.data() + off + 8);
        const uint32_t virtualAddr   = *reinterpret_cast<const uint32_t*>(data.data() + off + 12);
        const uint32_t rawSize       = *reinterpret_cast<const uint32_t*>(data.data() + off + 16);
        const uint32_t rawOffset     = *reinterpret_cast<const uint32_t*>(data.data() + off + 20);

        const uint64_t destVA = loadBase + virtualAddr;
        const uint32_t copySize = std::min(rawSize, virtualSize);

        if (rawOffset + copySize <= data.size()) {
            Write(destVA, data.data() + rawOffset, copySize);
        }
        // Zero pad if virtualSize > rawSize (BSS etc.) — already zeroed by Allocate
    }
    return true;
}

bool WinMemory::ApplyRelocations(const std::vector<uint8_t>& data,
                                  uint64_t loadBase,
                                  uint64_t origBase,
                                  uint64_t relocRVA,
                                  uint32_t relocSize) {
    const int64_t delta = static_cast<int64_t>(loadBase) -
                          static_cast<int64_t>(origBase);
    if (delta == 0) return true;

    uint64_t off = relocRVA;
    const uint64_t end = relocRVA + relocSize;

    while (off < end) {
        if (off + 8 > data.size()) break;
        const uint32_t pageRVA   = *reinterpret_cast<const uint32_t*>(data.data() + off);
        const uint32_t blockSize = *reinterpret_cast<const uint32_t*>(data.data() + off + 4);
        if (blockSize < 8) break;

        const uint32_t numEntries = (blockSize - 8) / 2;
        for (uint32_t i = 0; i < numEntries; ++i) {
            if (off + 8 + i * 2 + 2 > data.size()) break;
            const uint16_t entry = *reinterpret_cast<const uint16_t*>(
                data.data() + off + 8 + i * 2);
            const uint8_t  type  = entry >> 12;
            const uint16_t pgOff = entry & 0x0FFF;

            if (type == kRelocAbsolute) continue;
            if (type == kRelocDir64) {
                const uint64_t va = loadBase + pageRVA + pgOff;
                uint64_t val = Read64(va);
                Write64(va, val + static_cast<uint64_t>(delta));
            }
        }
        off += blockSize;
    }
    return true;
}

bool WinMemory::ResolveImports(const std::vector<uint8_t>& data,
                                uint64_t loadBase,
                                uint64_t importRVA) {
    // IMAGE_IMPORT_DESCRIPTOR: 20 bytes each, ends with all-zero entry
    uint64_t off = importRVA;
    for (;;) {
        if (off + 20 > data.size()) break;
        const uint32_t origThunkRVA = *reinterpret_cast<const uint32_t*>(data.data() + off + 0);
        const uint32_t nameRVA      = *reinterpret_cast<const uint32_t*>(data.data() + off + 12);
        const uint32_t iatRVA       = *reinterpret_cast<const uint32_t*>(data.data() + off + 16);

        if (nameRVA == 0 && iatRVA == 0) break; // end sentinel

        // Read DLL name from the file image
        std::string dllName;
        if (nameRVA < data.size()) {
            const char* p = reinterpret_cast<const char*>(data.data() + nameRVA);
            while (p < reinterpret_cast<const char*>(data.data() + data.size()) && *p)
                dllName += static_cast<char>(std::tolower(static_cast<unsigned char>(*p++)));
        }

        // Walk the thunk array (use OriginalFirstThunk when available)
        uint64_t thunkRVA = origThunkRVA ? origThunkRVA : iatRVA;
        uint64_t iatEntry = iatRVA;

        for (uint32_t slot = 0; ; ++slot) {
            const uint64_t thunkOff = thunkRVA + slot * 8;
            const uint64_t iatOff   = iatEntry + slot * 8;
            if (thunkOff + 8 > data.size()) break;

            const uint64_t thunk = *reinterpret_cast<const uint64_t*>(
                data.data() + thunkOff);
            if (thunk == 0) break;

            std::string funcName;
            if (thunk & (1ULL << 63)) {
                // Import by ordinal
                funcName = "#" + std::to_string(thunk & 0xFFFF);
            } else {
                // IMAGE_IMPORT_BY_NAME: Hint (2 bytes) + Name
                const uint64_t ibnOff = thunk & 0x7FFFFFFF;
                if (ibnOff + 2 < data.size()) {
                    const char* p = reinterpret_cast<const char*>(data.data() + ibnOff + 2);
                    while (p < reinterpret_cast<const char*>(data.data() + data.size()) && *p)
                        funcName += *p++;
                }
            }

            uint64_t resolvedAddr = 0;
            if (importResolver_) {
                resolvedAddr = importResolver_(dllName, funcName);
            }

            if (resolvedAddr == 0) {
                // Unresolved import — plant a stub address that will fault
                // gracefully rather than crash the PE loader itself
                resolvedAddr = static_cast<uint64_t>(-1LL);
                std::cerr << "[PE] Unresolved import: " << dllName
                          << "!" << funcName << "\n";
            }

            // Patch the IAT in the loaded image
            Write64(loadBase + iatOff, resolvedAddr);
        }

        off += 20;
    }
    return true;
}

} // namespace AIO::Emulator::Windows
