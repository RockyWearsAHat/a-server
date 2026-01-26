# Plan: Fix SMA2 Performance Regression (EnvFlagCached Mutex Overhead)

**Status:** 🔴 NOT STARTED
**Goal:** Restore SMA2 boot performance to match commit 31b507b (~2s to title screen vs current ~10s)

---

## Context

### Root Cause Analysis

The `EnvFlagCached()` function was changed from a **static-local one-time evaluation** to a **mutex-protected hash map lookup** on every call:

**OLD (31b507b - fast):**
```cpp
template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));  // Computed ONCE per template instantiation
  return enabled;
}
```

**NEW (HEAD - slow):**
```cpp
inline bool EnvFlagCached(const char *name) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);  // LOCK EVERY CALL
  auto it = cache.find(name);  // HASH MAP LOOKUP EVERY CALL
  // ...
}
```

This is called on **hot paths** (every instruction cycle in some cases), causing a ~5x performance regression.

### Evidence

| Commit    | SMA2 at 2s dump | SMA2 at 10s dump |
|-----------|-----------------|------------------|
| 31b507b   | 14 colors, 96%  | N/A              |
| HEAD      | 2 colors, 0.78% | 20 colors        |

The game eventually renders correctly, but takes ~5x longer to reach the same state.

---

## Steps

### Step 1: Revert EnvFlagCached in ARM7TDMI.cpp — `src/emulator/gba/ARM7TDMI.cpp`

**Operation:** `REPLACE`
**Anchor:**
```cpp
inline bool EnvFlagCached(const char *name) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(name);
  if (it != cache.end()) {
    return it->second;
  }
  const bool enabled = EnvTruthy(std::getenv(name));
  cache.emplace(name, enabled);
  return enabled;
}
```

```cpp
template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}
```

**Verify:** `make build && timeout 10s ./build/bin/AIOServer --headless --rom SMA2.gba --headless-max-ms 4000 --headless-dump-ppm /tmp/test.ppm --headless-dump-ms 2000 && python3 -c "...count colors..."`

---

### Step 2: Revert EnvFlagCached in GBAMemory.cpp — `src/emulator/gba/GBAMemory.cpp`

**Operation:** `REPLACE`
**Anchor:**
```cpp
inline bool EnvFlagCached(const char *name) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(name);
  if (it != cache.end()) {
    return it->second;
  }
  const bool enabled = EnvTruthy(std::getenv(name));
  cache.emplace(name, enabled);
  return enabled;
}
```

```cpp
template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}
```

**Verify:** Build succeeds

---

### Step 3: Revert EnvFlagCached in PPU.cpp — `src/emulator/gba/PPU.cpp`

**Operation:** `REPLACE`
**Anchor:**
```cpp
inline bool EnvFlagCached(const char *name) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(name);
  if (it != cache.end()) {
    return it->second;
  }
  const bool enabled = EnvTruthy(std::getenv(name));
  cache.emplace(name, enabled);
  return enabled;
}
```

```cpp
template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}
```

**Verify:** Build succeeds

---

### Step 4: Revert EnvFlagCached in GBA.cpp — `src/emulator/gba/GBA.cpp`

**Operation:** `REPLACE`
**Anchor:**
```cpp
inline bool EnvFlagCached(const char *name) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(name);
  if (it != cache.end()) {
    return it->second;
  }
  const bool enabled = EnvTruthy(std::getenv(name));
  cache.emplace(name, enabled);
  return enabled;
}
```

```cpp
template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}
```

**Verify:** Build succeeds

---

### Step 5: Remove unused mutex/unordered_map includes (if safe)

Review each file after reverting. The `<mutex>`, `<unordered_map>`, and `<string>` includes may have been added solely for the new EnvFlagCached implementation. If no other code in the file uses these headers, they can be removed to clean up.

**Files to check:**
- `src/emulator/gba/ARM7TDMI.cpp`
- `src/emulator/gba/GBAMemory.cpp`
- `src/emulator/gba/PPU.cpp`
- `src/emulator/gba/GBA.cpp`

**Verify:** Build succeeds

---

## Test Strategy

1. `make build` — compiles without errors
2. `timeout 10s ./build/bin/AIOServer --headless --rom SMA2.gba --headless-max-ms 4000 --headless-dump-ppm /tmp/sma2_test.ppm --headless-dump-ms 2000` — SMA2 should show 13+ colors within 2 seconds
3. `./build/bin/SMA2Harness SMA2.gba` — harness passes
4. `cd build/generated/cmake && ctest --output-on-failure` — all tests pass
5. Verify other games still work: DKC, MMBN, MZM

---

## Documentation Updates

No memory.md changes needed - this is a performance fix, not an architecture change.

---

## Handoff

Run `@Implement` to execute all steps.

**Operation:** `REPLACE`
**Anchor:**
```cpp
inline bool EnvFlagCached(const char *name) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(name);
  if (it != cache.end()) {
    return it->second;
  }
  const bool enabled = EnvTruthy(std::getenv(name));
  cache.emplace(name, enabled);
  return enabled;
}
```

```cpp
template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}
```

**Verify:** Build succeeds

---

### Step 5: Remove unused includes — All affected files

**Operation:** `DELETE` (after reverting)

Remove the now-unused includes that were added for the mutex/unordered_map approach:
- `#include <mutex>`
- `#include <unordered_map>`
- `#include <string>` (if only used for this)

**Note:** Only remove if no other code in the file uses these headers.

**Verify:** Build succeeds

---

## Test Strategy

1. `make build` — compiles without errors
2. `timeout 10s ./build/bin/AIOServer --headless --rom SMA2.gba --headless-max-ms 4000 --headless-dump-ppm /tmp/sma2_test.ppm --headless-dump-ms 2000` — SMA2 should show 13+ colors within 2 seconds
3. `./build/bin/SMA2Harness SMA2.gba` — harness passes
4. `cd build/generated/cmake && ctest --output-on-failure` — all tests pass

---

## Documentation Updates

No memory.md changes needed - this is a performance fix, not an architecture change.

---

## Handoff

Run `@Implement` to execute all steps.

### Frame Dump Analysis

- Frame dumped at 6.7 seconds (headless-dump-ms 6700)
- 240x160 PPM image written successfully
- Green pixels confirmed at bird sprite locations (x=216-239, y=7-16)

### Trace Output

420 `[SKY_SPR]` traces showing sprites processed at frames 400-700.
100 `[BIRD_DRAW]` traces showing pixel writes to backBuffer.

**Status: Closed - Rendering Verified**
memory.Write16(0x06010000u + 2u, 0x1111u);

// Setup sprite 0: semi-transparent OBJ at (0,0), 8x8, 4bpp
// attr0: Y=0, objMode=1 (semi-transparent), bits 10-11 = 01
const uint16_t spr0_attr0 = (uint16_t)(0u | (1u << 10));
const uint16_t spr0_attr1 = 0u;
const uint16_t spr0_attr2 = 0u;
TestUtil::WriteOam16(memory, 0, spr0_attr0);
TestUtil::WriteOam16(memory, 2, spr0_attr1);
TestUtil::WriteOam16(memory, 4, spr0_attr2);

// Exit Forced Blank before rendering
memory.Write16(0x04000000u, 0x1000u);

ppu.Update(960);
ppu.SwapBuffers();

const auto fb = ppu.GetFramebuffer();
ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);

uint32_t pixel = fb[0];
uint8_t r = (pixel >> 16) & 0xFF;
uint8_t g = (pixel >> 8) & 0xFF;
uint8_t b = pixel & 0xFF;

// Expected: blended color from red OBJ + green backdrop
// With EVA=8, EVB=8: out = (OBJ*8 + backdrop*8) / 16
// Red OBJ: R=31, G=0, B=0 (BGR555: 0x001F -> RGB888: 0xF8,0,0)
// Green BG: R=0, G=31, B=0 (BGR555: 0x03E0 -> RGB888: 0,0xF8,0)
// Blended 5-bit: R=15, G=15, B=0 -> RGB888: 0x78, 0x78, 0
EXPECT_GT(r, 0u) << "Red component should be present (from OBJ)";
EXPECT_GT(g, 0u) << "Green component should be present (from backdrop blend)";
EXPECT_EQ(b, 0u) << "Blue component should be zero";
}

```

**Verify:** `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'`

---

## Test Strategy

1. `make build` — compiles without errors
2. `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'` — test passes
3. `./build/bin/PPUTests --gtest_filter='*SemiTransparent*'` — all semi-transparent tests still pass

---

## Documentation Updates

No memory.md updates needed — this was a test setup bug, not a code bug.

---

## Notes

The PPU code change from the previous plan (removing `topIsFirstTarget` check for semi-transparent OBJs) was already applied and is correct. The test was failing due to improper OAM initialization, not a PPU logic bug.

---

## Handoff

Run `@Implement` to execute Step 1.
```
