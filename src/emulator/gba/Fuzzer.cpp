#include <emulator/gba/Fuzzer.h>
#include <emulator/gba/GBA.h>
#include <emulator/gba/IORegs.h>
#include <emulator/gba/Logger.h>
#include <iostream>
#include <fstream>
#include <random>
#include <vector>

namespace AIO::Emulator::GBA {

void Fuzzer::RunCPUFuzz(int iterations) {
    std::cout << "[Fuzzer] Starting CPU Fuzzing for " << iterations << " iterations..." << std::endl;
    
    GBA gba;
    // No ROM loaded, we will inject instructions into IWRAM/EWRAM
    gba.Reset();
    
    std::mt19937 rng(12345); // Fixed seed for reproducibility
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    
    // Set PC to IWRAM start
    uint32_t startPC = 0x03000000;
    
    for (int i = 0; i < iterations; ++i) {
        // Reset state every 1000 instructions to prevent total chaos
        if (i % 1000 == 0) {
            gba.Reset();
            // Manually set PC to IWRAM
            // We need access to CPU registers, but GBA class encapsulates it.
            // We can write a jump to 0x03000000 at 0x08000000 (ROM start) if we could write ROM.
            // Or we can just rely on the fact that we are fuzzing the decoder.
            // Let's just write random instructions to where PC is.
        }
        
        uint32_t pc = gba.GetPC();
        
        // If PC is in ROM (read-only), we can't inject there easily without hacking memory.
        // But GBA::Reset sets PC to 0x08000000.
        // Let's assume we can write to ROM via a backdoor or just use RAM.
        // Since we can't easily write to ROM via public API if it's read-only,
        // let's try to force PC to RAM.
        
        // Actually, for a decoder fuzzer, we just want to feed bits to the decoder.
        // We can use a mock memory or just write to the memory location pointed by PC.
        // GBAMemory::Write32 usually blocks ROM writes.
        
        // Alternative: Generate a random instruction and execute it directly?
        // The CPU executes from memory.
        
        // Let's try to fill IWRAM with random junk and jump there.
        // We need to set PC. GBA class doesn't expose SetPC directly?
        // It has GetPC.
        
        // Let's just fill IWRAM and try to execute it.
        // But we need to get the CPU to jump there.
        // We can write a branch instruction at 0x00000000 (BIOS) if we could write BIOS.
        
        // Okay, let's use the public API.
        // We can load a dummy ROM that jumps to IWRAM.
        // Or we can just fuzz the state by writing random values to random memory addresses (Mem Fuzz).
        
        // For CPU Fuzz specifically: "Feed random bit patterns into instruction decoder".
        // We can do this by writing random values to where the PC is pointing.
        // If PC is in ROM, we can't.
        // So we must ensure PC is in RAM.
        
        // Hack: We can use `gba.WriteMem32` to write to IWRAM.
        // But we need to start execution there.
        // If we can't set PC, we can't easily force it.
        
        // However, we can modify `GBA.h` to expose `SetPC` or `GetCPU`.
        // Or we can just rely on `HeadlessTest` having access if we make it friend or public.
        // `GBA` class has `cpu` as private.
        
        // Let's assume we can't easily change PC.
        // We can try to load a small "ROM" that is just a jump to 0x03000000.
        // Then fill 0x03000000 with random data.
        
        // Create a dummy ROM vector
        std::vector<uint8_t> dummyRom(0x1000000, 0); // 16MB
        // Write "B 0x03000000" at 0x08000000
        // ARM Branch: 0xEAxxxxxx. Offset = (Target - PC - 8) / 4
        // Target = 0x03000000. PC = 0x08000000.
        // Offset = (0x03000000 - 0x08000000 - 8) / 4 = (-0x05000008) / 4
        // This is a negative offset.
        // 0xEA000000 is B PC+8.
        // Let's just use a LDR PC, [PC, #...] to jump absolute.
        // LDR PC, [PC, #-4] -> 0xE51FF004. And put address at 0x08000000? No.
        // LDR PC, [PC, #0] -> Reads from PC+8.
        // 0x08000000: E59FF000 (LDR PC, [PC, #0]) -> Reads from 0x08000008
        // 0x08000004: E1A00000 (NOP)
        // 0x08000008: 03000000 (Address)
        
        // We can't easily inject a vector as ROM via `LoadROM` (it takes a path).
        // We can write a temp file.
        
        // Let's write a temp file "fuzz_stub.gba".
        {
            std::ofstream romFile("fuzz_stub.gba", std::ios::binary);
            uint32_t ldr_pc = 0xE59FF000;
            uint32_t nop = 0xE1A00000;
            uint32_t target = 0x03000000;
            romFile.write((char*)&ldr_pc, 4);
            romFile.write((char*)&nop, 4);
            romFile.write((char*)&target, 4);
            // Fill rest with 0
            for(int k=0; k<100; k++) romFile.write((char*)&nop, 4);
        }
        
        if (!gba.LoadROM("fuzz_stub.gba")) {
            std::cerr << "[Fuzzer] Failed to load stub ROM" << std::endl;
            return;
        }
        
        gba.Reset();
        
        // Run a few steps to get to IWRAM
        for(int k=0; k<10; k++) gba.Step();
        
        // Now PC should be 0x03000000 (or close)
        
        // Fuzz Loop
        for (int j = 0; j < iterations; ++j) {
            uint32_t opcode = dist(rng);
            uint32_t currentPC = gba.GetPC();
            
            // Ensure we are in IWRAM/EWRAM
            if ((currentPC & 0xFF000000) != 0x03000000 && (currentPC & 0xFF000000) != 0x02000000) {
                // We escaped! Reset.
                gba.Reset();
                for(int k=0; k<10; k++) gba.Step();
                currentPC = gba.GetPC();
            }
            
            // Write random opcode at PC
            gba.WriteMem(currentPC, opcode);
            
            // Step
            gba.Step();
            
            if (j % 10000 == 0) {
                std::cout << "[Fuzzer] Iteration " << j << " PC=0x" << std::hex << currentPC << std::dec << std::endl;
            }
        }
    }
    
    std::cout << "[Fuzzer] CPU Fuzzing Completed." << std::endl;
}

void Fuzzer::RunMemFuzz(const std::string& romPath, int iterations) {
    std::cout << "[Fuzzer] Starting Mem/Event Fuzzing for " << iterations << " iterations..." << std::endl;
    
    GBA gba;
    if (!gba.LoadROM(romPath)) {
        std::cerr << "[Fuzzer] Failed to load ROM: " << romPath << std::endl;
        return;
    }
    
    gba.Reset();
    
    std::mt19937 rng(54321);
    std::uniform_int_distribution<int> eventDist(0, 100);
    std::uniform_int_distribution<uint32_t> addrDist(0x03000000, 0x03007FFF); // IWRAM
    std::uniform_int_distribution<uint32_t> valDist(0, 0xFFFFFFFF);
    
    for (int i = 0; i < iterations; ++i) {
        gba.Step();
        
        int event = eventDist(rng);
        
        // 1% chance to fire random IRQ
        if (event == 0) {
            // We can't easily fire IRQ from outside without access to internal state.
            // But we can write to IF register.
                uint16_t if_val = gba.ReadMem16(IORegs::REG_IF);
            if_val |= (1 << (eventDist(rng) % 14));
                gba.WriteMem16(IORegs::REG_IF, if_val);
            // Logger::Log(LogCategory::IRQ, "[Fuzzer] Injected IRQ");
        }
        
        // 1% chance to corrupt random IWRAM
        if (event == 1) {
            uint32_t addr = addrDist(rng);
            uint32_t val = valDist(rng);
            gba.WriteMem(addr, val);
            // Logger::Log(LogCategory::Mem, "[Fuzzer] Corrupted IWRAM 0x%x", addr);
        }
        
        if (i % 10000 == 0) {
            std::cout << "[Fuzzer] Iteration " << i << std::endl;
        }
    }
    
    std::cout << "[Fuzzer] Mem Fuzzing Completed." << std::endl;
}

} // namespace AIO::Emulator::GBA
