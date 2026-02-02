# Contributing to AIO Server

Thank you for your interest in contributing to AIO Server! This document provides guidelines and best practices for contributing to the project.

## Code of Conduct

- Be respectful and constructive in all interactions
- Focus on technical merit and project goals
- Help maintain a welcoming environment for all contributors

## Getting Started

1. **Fork the repository** and clone your fork
2. **Create a feature branch**: `git checkout -b feature/your-feature-name`
3. **Set up your development environment** (see README.md)
4. **Build the project**: `make build`
5. **Run tests**: `make test`

## C++ Best Practices

### Code Style

#### Naming Conventions

- **Classes/Structs**: `PascalCase` (e.g., `ARM7TDMI`, `GBAEmulator`)
- **Functions/Methods**: `camelCase` or `PascalCase` (e.g., `loadROM()`, `LoadROM()`)
- **Variables**: `camelCase` (e.g., `romPath`, `currentState`)
- **Member Variables**: `camelCase_` with trailing underscore (e.g., `emulator_`, `isRunning_`)
- **Constants**: `UPPER_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)
- **Namespaces**: `PascalCase`, hierarchical (e.g., `AIO::Emulator::GBA`)

#### File Organization

```cpp
// Header file structure (.h)
#pragma once

// System includes
#include <cstdint>
#include <memory>
#include <vector>

// Third-party includes
#include <QWidget>
#include <SDL2/SDL.h>

// Project includes
#include "emulator/common/Types.h"
#include "emulator/gba/Memory.h"

namespace AIO::Emulator::GBA {

/**
 * @brief Brief description of the class.
 *
 * Detailed description with usage examples if needed.
 */
class MyClass {
public:
    MyClass();
    ~MyClass();

    void DoSomething();

private:
    int privateData_;
};

} // namespace AIO::Emulator::GBA
```

```cpp
// Implementation file structure (.cpp)
#include "path/to/MyClass.h"

// Additional includes

namespace AIO::Emulator::GBA {

MyClass::MyClass() : privateData_(0) {
    // Constructor implementation
}

void MyClass::DoSomething() {
    // Method implementation
}

} // namespace AIO::Emulator::GBA
```

### Modern C++ Features

#### Use C++17 Standards

- **Structured bindings**: `auto [key, value] = map.find(...);`
- **std::optional**: For nullable values instead of pointers
- **std::variant**: For type-safe unions
- **if constexpr**: For compile-time conditionals
- **std::string_view**: For non-owning string references

#### Smart Pointers

```cpp
// Prefer unique_ptr for exclusive ownership
std::unique_ptr<Emulator> emulator_ = std::make_unique<Emulator>();

// Use shared_ptr only when shared ownership is required
std::shared_ptr<Resource> resource = std::make_shared<Resource>();

// Avoid raw pointers for ownership
// ❌ Emulator* emulator = new Emulator();
// ✅ std::unique_ptr<Emulator> emulator = std::make_unique<Emulator>();
```

#### RAII (Resource Acquisition Is Initialization)

```cpp
// Resources should be tied to object lifetime
class FileHandler {
public:
    FileHandler(const std::string& path) {
        file_.open(path);
    }

    ~FileHandler() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Delete copy, allow move
    FileHandler(const FileHandler&) = delete;
    FileHandler& operator=(const FileHandler&) = delete;
    FileHandler(FileHandler&&) = default;
    FileHandler& operator=(FileHandler&&) = default;

private:
    std::fstream file_;
};
```

#### Const Correctness

```cpp
class Example {
public:
    // Mark methods const when they don't modify state
    int GetValue() const { return value_; }

    // Use const references for read-only parameters
    void ProcessData(const std::vector<uint8_t>& data);

private:
    int value_;
};
```

### Performance Guidelines

#### Avoid Unnecessary Copies

```cpp
// ❌ Expensive copy
void ProcessROM(std::vector<uint8_t> rom);

// ✅ Const reference for read-only
void ProcessROM(const std::vector<uint8_t>& rom);

// ✅ Move semantics for transfer
void LoadROM(std::vector<uint8_t>&& rom);
```

#### Inline Hot Paths

```cpp
// Use inline for performance-critical small functions
inline uint32_t ReadMemory32(uint32_t address) const {
    return *reinterpret_cast<const uint32_t*>(&memory_[address]);
}
```

#### Optimize Memory Access

```cpp
// Cache-friendly: structure-of-arrays
struct ParticleSystem {
    std::vector<float> positionsX;
    std::vector<float> positionsY;
    std::vector<float> velocitiesX;
    std::vector<float> velocitiesY;
};

// Less cache-friendly: array-of-structures (avoid for large datasets)
struct Particle {
    float posX, posY;
    float velX, velY;
};
std::vector<Particle> particles;
```

### Error Handling

```cpp
// Use exceptions for exceptional conditions
if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + path);
}

// Use std::optional for expected "no value" cases
std::optional<Config> LoadConfig(const std::string& path) {
    if (!FileExists(path)) {
        return std::nullopt;
    }
    return Config::Load(path);
}

// Use return codes for performance-critical paths
enum class Status { Success, Error };
Status FastOperation() {
    // No exceptions in hot loop
    return Status::Success;
}
```

### Thread Safety

```cpp
// Use std::atomic for lock-free operations
std::atomic<bool> isRunning_{false};

// Use std::mutex for critical sections
mutable std::mutex mutex_;
std::vector<int> data_;

void AddData(int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.push_back(value);
}

// Consider thread-local storage for thread-specific data
thread_local int frameCount = 0;
```

## Documentation

### Code Comments

```cpp
/**
 * @brief Brief one-line description.
 *
 * Detailed multi-line description explaining:
 * - What the function does
 * - Important behavior or side effects
 * - Thread safety guarantees
 *
 * @param address Memory address to read from (must be aligned)
 * @param size Number of bytes to read
 * @return std::vector<uint8_t> containing the read data
 * @throws std::out_of_range if address is out of bounds
 */
std::vector<uint8_t> ReadMemory(uint32_t address, size_t size);
```

### TODO Comments

```cpp
// TODO(username): Brief description of what needs to be done
// Context about why this is needed and any related issues

// FIXME(username): Brief description of the bug
// Steps to reproduce and expected vs actual behavior

// HACK: Explanation of why this hack is necessary
// and what the proper solution would be

// NOTE: Important information about this code
// that future developers should know
```

## Testing

### Unit Tests

```cpp
#include <gtest/gtest.h>
#include "emulator/gba/CPU.h"

TEST(CPUTests, InitialPCValue) {
    GBA::ARM7TDMI cpu;
    cpu.Reset();
    EXPECT_EQ(cpu.GetPC(), 0x08000000);
}

TEST(CPUTests, AddInstruction) {
    GBA::ARM7TDMI cpu;
    cpu.SetRegister(0, 5);
    cpu.SetRegister(1, 3);
    cpu.ExecuteADD(0, 1, 0); // R0 = R0 + R1
    EXPECT_EQ(cpu.GetRegister(0), 8);
}
```

### Test Organization

- One test file per source file: `CPU.cpp` → `CPUTests.cpp`
- Group related tests using `TEST()` with descriptive names
- Use `ASSERT_*` when failure should stop the test
- Use `EXPECT_*` when test should continue after failure

### Coverage Requirements

- Aim for >80% code coverage on new code
- All public APIs must have tests
- Critical paths (CPU, memory) require >95% coverage

Run coverage report:

```bash
make coverage
# View: build/coverage/index.html
```

## Git Workflow

### Commit Messages

Follow the conventional commit format:

```
type(scope): Brief description (50 chars or less)

More detailed explanation if needed. Wrap at 72 characters.
Explain what changed and why, not how.

- Bullet points for multiple changes
- Reference issues: Fixes #123, Relates to #456
```

**Types:**

- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code restructuring without behavior change
- `perf`: Performance improvement
- `test`: Adding or updating tests
- `docs`: Documentation changes
- `build`: Build system or dependency changes
- `ci`: CI/CD pipeline changes

**Examples:**

```
feat(gba): Add EEPROM save support

Implements 512-byte and 8KB EEPROM variants for GBA cartridge saves.
Includes auto-detection based on ROM metadata.

Fixes #42
```

```
fix(cpu): Correct ARM barrel shifter carry flag

The carry flag was not being set correctly for ROR operations
when the shift amount was 0. Now matches ARM7TDMI specification.
```

```
refactor(ppu): Split PPU into separate rendering stages

- Extract background rendering to BackgroundRenderer
- Move sprite rendering to SpriteRenderer
- Improve code organization and testability
```

### Branch Naming

- `feature/description` - New features
- `fix/description` - Bug fixes
- `refactor/description` - Code refactoring
- `docs/description` - Documentation updates

### Pull Request Process

1. **Update your branch** with latest main: `git pull origin main`
2. **Ensure tests pass**: `make test`
3. **Run code coverage**: `make coverage`
4. **Write clear PR description**:
   - What changed and why
   - Testing performed
   - Related issues
5. **Request review** from maintainers
6. **Address feedback** promptly
7. **Squash commits** if requested before merge

## Architecture Guidelines

### Separation of Concerns

- **Emulator core** (`src/emulator/`) should be independent of GUI
- **GUI** (`src/gui/`) orchestrates emulator and UI
- **Input** (`src/input/`) abstracts controller/keyboard handling
- **Common** (`src/common/`) provides shared utilities

### Dependency Management

- Avoid circular dependencies between components
- Use forward declarations in headers when possible
- Keep include dependencies minimal

### Qt Integration

- Use Qt for GUI and high-level I/O only
- Don't use Qt types in emulator core (use STL)
- Qt signals/slots for UI updates, not core emulation

## Performance Considerations

### Emulator Performance

- **Cycle accuracy** is important for compatibility
- Profile hot paths with tools like Instruments (macOS) or perf (Linux)
- Optimize memory access patterns
- Use lookup tables for expensive operations

### Build Performance

- Use `#pragma once` instead of header guards
- Minimize includes in headers (use forward declarations)
- Keep template implementations in headers when needed

## Questions?

If you have questions about contributing:

1. Check existing issues for similar discussions
2. Create a new issue with the `question` label
3. Reach out to maintainers

## License

By contributing to AIO Server, you agree that your contributions will be licensed under the project's license (see LICENSE file).
