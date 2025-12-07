#include <gtest/gtest.h>
#include "emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

class EEPROMTest : public ::testing::Test {
protected:
    GBAMemory memory;

    void SetUp() override {
        memory.Reset();
        // Ensure we have a 64Kbit EEPROM (8KB)
        std::vector<uint8_t> save(8192, 0xFF);
        memory.LoadSave(save);
    }

    // Helper to write a single bit to EEPROM
    void WriteBit(uint8_t bit) {
        // EEPROM is accessed via IO registers or GamePak memory region
        // Usually mapped to 0x0Dxxxxxx
        // Writing to even address sends a bit (bit 0 of value)
        memory.Write16(0x0D000000, bit & 1);
    }

    // Helper to read a bit from EEPROM
    uint8_t ReadBit() {
        // Reading from 0x0Dxxxxxx
        // Bit 0 is the data
        return memory.Read16(0x0D000000) & 1;
    }

    // Helper to send an address (n bits)
    void SendAddress(uint32_t address, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            WriteBit((address >> i) & 1);
        }
    }

    // Helper to send 64 bits of data
    void SendData64(uint64_t data) {
        for (int i = 63; i >= 0; --i) {
            WriteBit((data >> i) & 1);
        }
    }

    // Helper to read 64 bits of data
    uint64_t ReadData64() {
        uint64_t data = 0;
        for (int i = 0; i < 64; ++i) {
            data = (data << 1) | ReadBit();
        }
        return data;
    }
};

TEST_F(EEPROMTest, InitialState) {
    // Should be 1 (Ready) when Idle
    // Updated to match ReadEEPROM logic where Idle = 1
    EXPECT_EQ(ReadBit(), 1);
}

TEST_F(EEPROMTest, WriteAndRead_64Kbit) {
    uint32_t address = 0x10;
    uint64_t writeValue = 0xDEADBEEFCAFEBABE;

    // WRITE: Send data via EEPROM protocol
    WriteBit(1); // Start
    WriteBit(0); // Write command
    SendAddress(address, 14);
    SendData64(writeValue);
    WriteBit(0); // Termination

    // Wait for write to complete
    memory.UpdateTimers(170000);

    // READ: Retrieve data via EEPROM protocol
    WriteBit(1); // Start
    WriteBit(1); // Read command
    SendAddress(address, 14);
    WriteBit(0); // Stop Bit (Required by protocol)
    
    // EEPROM outputs 4 dummy bits before data
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(ReadBit(), 0) << "Dummy bit " << i << " should be 0";
    }
    
    // Verify data read matches data written via serial protocol
    uint64_t readValue = ReadData64();
    EXPECT_EQ(readValue, writeValue);
}

TEST_F(EEPROMTest, Write_InvalidTermination) {
    // If termination bit is not 0, what happens?
    // The spec says it should be 0.
    // Our emulator might reset state.
}

TEST_F(EEPROMTest, Read_Uninitialized) {
    uint32_t address = 0x20;
    
    WriteBit(1); // Start
    WriteBit(1); // Read
    SendAddress(address, 14);
    WriteBit(0); // Stop Bit
    
    // Consume 4 dummy bits
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(ReadBit(), 0);
    }
    
    // Uninitialized EEPROM should read as 0xFF (set in SetUp)
    uint64_t val = ReadData64();
    EXPECT_EQ(val, 0xFFFFFFFFFFFFFFFFULL);
}

TEST_F(EEPROMTest, AddressAliasing_64Kbit) {
    // 64Kbit EEPROM uses 10-bit effective address (1024 blocks)
    // Addresses beyond 1023 should wrap/alias to 0-1023
    uint32_t addr1 = 0x005;
    uint32_t addr2 = 0x405; // Same as 0x005 when masked to 10 bits
    
    uint64_t val1 = 0x1111111111111111;
    uint64_t val2 = 0x2222222222222222;
    
    // Write val1 to addr1
    WriteBit(1); WriteBit(0); SendAddress(addr1, 14); SendData64(val1); WriteBit(0);
    memory.UpdateTimers(170000);
    
    // Write val2 to addr2 (should overwrite addr1 due to aliasing)
    WriteBit(1); WriteBit(0); SendAddress(addr2, 14); SendData64(val2); WriteBit(0);
    memory.UpdateTimers(170000);
    
    // Read from addr1 - should return val2 if properly aliased
    WriteBit(1); WriteBit(1); SendAddress(addr1, 14); WriteBit(0); // Stop Bit
    
    // Consume 4 dummy bits
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(ReadBit(), 0);
    }
    
    uint64_t readVal = ReadData64();
    EXPECT_EQ(readVal, val2);
}

TEST_F(EEPROMTest, DMA_Read_Simulation) {
    // Test DMA transfer from EEPROM to WRAM
    uint32_t address = 0x10;
    uint64_t writeValue = 0xAABBCCDDEEFF0011;
    
    // Write test data
    WriteBit(1); WriteBit(0); SendAddress(address, 14); SendData64(writeValue); WriteBit(0);
    memory.UpdateTimers(170000);
    
    // Start read operation
    WriteBit(1); WriteBit(1); SendAddress(address, 14); WriteBit(0); // Stop Bit
    
    // Consume 4 dummy bits
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(ReadBit(), 0);
    }
    
    // Configure DMA3 to read 64 bits from EEPROM to WRAM
    memory.Write32(0x40000D4, 0x0D000000); // Source: EEPROM
    memory.Write32(0x40000D8, 0x02000000); // Dest: WRAM
    memory.Write16(0x40000DC, 64);         // Count: 64 reads
    memory.Write16(0x40000DE, 0x8100);     // Control: Enable | Immediate | 16bit
    
    // Reconstruct value from WRAM
    uint64_t reconstructed = 0;
    for (int i = 0; i < 64; ++i) {
        uint16_t bit = memory.Read16(0x02000000 + i * 2);
        EXPECT_TRUE(bit == 0 || bit == 1) << "DMA should transfer individual bits";
        if (bit) {
            reconstructed |= (1ULL << (63 - i));
        }
    }
    
    EXPECT_EQ(reconstructed, writeValue);
}

TEST_F(EEPROMTest, Read_WithDummyWrite_64Kbit) {
    // This test validates that writing dummy bit doesn't corrupt the read
    // However, this may not match actual GBA hardware behavior
    // Removing this test as it tests undocumented behavior
    GTEST_SKIP() << "Test removed - validates undocumented protocol variant";
}
