# C++ Best Practices and Pitfalls

## Scope

Verified best practices, common bugs, and anti-patterns for modern C++
(C++17/20) as used in AIO Server. Covers memory management, undefined
behavior, performance, and debugging. Applies to all `src/` and `include/`
code.

## The Golden Rules

1. **RAII everywhere.** Every resource (memory, file handle, socket, mutex)
   must be owned by an object whose destructor releases it. No exceptions.
2. **No raw `new`/`delete`.** Use `std::unique_ptr` for sole ownership,
   `std::shared_ptr` only when shared ownership is genuinely needed.
3. **Const by default.** Variables, parameters, member functions — mark
   everything `const` unless it must be mutated.
4. **Initialize everything.** Uninitialized variables are the #1 source of
   non-deterministic bugs. Use member initializer lists, default member
   initializers, and `{}` initialization.
5. **Prefer value semantics.** Pass by value or const-reference. Reserve
   pointers for polymorphism and optional ownership transfer.

## Undefined Behavior (UB) — The Silent Killer

UB doesn't crash — it makes your program do _anything_, including appearing
to work correctly until it doesn't. The compiler is allowed to assume UB
never happens and optimize accordingly.

### Most Common UB in Practice

| UB Category                    | What Happens                                        |
| ------------------------------ | --------------------------------------------------- |
| **Null pointer dereference**   | Compiler may eliminate null checks upstream         |
| **Signed integer overflow**    | Compiler assumes it won't happen, removes branches  |
| **Use-after-free / dangling**  | Memory reused silently; corruption surfaces later   |
| **Buffer overflow**            | Stack smashing, RCE, silent data corruption         |
| **Iterator invalidation**      | Modifying container while iterating → dangling refs |
| **Data races**                 | Two threads access same memory, at least one writes |
| **Uninitialized read**         | Value is garbage; optimizer may propagate it        |
| **Double free**                | Heap corruption, usually delayed crash              |
| **Strict aliasing violation**  | Type-punning through incompatible pointer types     |
| **Shift by negative/overflow** | `x << 33` on 32-bit int is UB                       |

### The UB Trap: "It Works on My Machine"

- UB may work in debug builds and fail in release (or vice versa).
- Adding a print statement can _hide_ a bug (changes stack layout).
- Different compilers and optimization levels produce different UB outcomes.
- **Sanitizers catch what tests miss**: always build with `-fsanitize=address,undefined` in test configurations.

## Memory Management Hierarchy

```
Best  → worst:

1. Stack allocation (automatic lifetime, zero overhead)
2. std::unique_ptr (sole ownership, zero overhead at runtime)
3. std::shared_ptr (shared ownership, atomic ref-count overhead)
4. Raw pointer (non-owning observer only — NEVER for ownership)
5. Raw new/delete (forbidden in modern C++)
```

### Smart Pointer Rules

- **`unique_ptr` is the default.** Use it for factory returns, member
  variables, and any heap allocation.
- **`shared_ptr` costs**: construction/copy/destruction involve atomic ops.
  On hot paths, this matters. Use `make_shared` (single allocation vs two).
- **`weak_ptr`** breaks circular references. If two objects hold `shared_ptr`
  to each other, neither ever gets freed.
- **Never** pass `shared_ptr` by value unless you need to extend lifetime.
  Pass by `const&` or pass the raw `T*`/`T&` if the callee doesn't need
  ownership.

## Performance Pitfalls

### Hidden Copies

- **Range-for without `&`**: `for (auto item : vec)` copies every element.
  Use `for (const auto& item : vec)`.
- **Lambda capture by value**: `[=]` copies everything. Be explicit:
  `[&localRef, capturedValue]`.
- **Structured bindings**: `auto [x, y] = pair;` copies. Use
  `auto& [x, y] = pair;`.
- **Implicit conversions**: passing a `std::string` where `string_view` is
  expected triggers a temporary allocation.

### Move Semantics Traps

- **Don't `std::move` return values**: defeats NRVO. `return localObj;` is
  correct; `return std::move(localObj);` is pessimization.
- **Moved-from objects**: valid but unspecified state. Don't read from them
  without reassigning first.
- **`std::move` doesn't move**: it casts to rvalue reference. The actual move
  happens in the receiving constructor/assignment.

### Virtual Function Overhead

- Vtable lookup: ~2ns per indirect call (negligible in UI, significant in
  inner loops).
- Prevents inlining — meaningful in tight emulator loops.
- Alternative: CRTP (Curiously Recurring Template Pattern) for static
  polymorphism when the type set is known at compile time.

### `std::string_view` Lifetime Trap

`string_view` does not own its data. If the underlying string is destroyed
or reallocated, the view dangles. Never store a `string_view` beyond the
scope of the string it references.

### `std::async` Gotcha

If you discard the returned `std::future`, its destructor blocks until the
async task completes — turning your "async" call into a synchronous one.
Always capture the future.

## Iterator Invalidation Quick Reference

| Container     | Insert          | Erase                |
| ------------- | --------------- | -------------------- |
| `vector`      | Invalidates all | After erase point    |
| `deque`       | Invalidates all | All (front/back OK)  |
| `list`        | None            | Only erased iterator |
| `map`/`set`   | None            | Only erased iterator |
| `unordered_*` | May rehash=all  | Only erased iterator |

**Critical pattern**: never `erase()` inside a range-for. Use the
erase-remove idiom or `std::erase_if` (C++20).

## Large Application Architecture

### Subsystem Modularization

Structure a large C++ app as a graph of subsystems, each compiled as a
static library or object library in CMake. Each subsystem owns:

- A public header directory (`include/<subsystem>/`)
- A private source directory (`src/<subsystem>/`)
- Its own unit test target

Dependencies between subsystems are expressed via CMake `target_link_libraries`
with `PRIVATE` or `PUBLIC` as appropriate. This enforces compile-time
dependency boundaries — if subsystem A doesn't link B, it can't include B's
headers.

**AIO Server example**: `emulator/gba`, `emulator/ps1`, `gui/` are separate
subsystems. The GUI depends on emulator interfaces but not on emulator
internals.

### Service Layer Pattern (Qt)

For non-trivial features that span UI and backend logic, introduce a service
class:

```cpp
class SteamService : public QObject {
    Q_OBJECT
public:
    void fetchCatalog(const QString& category, int start, int count);
signals:
    void gamesPageReady(const QVector<GameEntry>& entries);
    void errorOccurred(const QString& message);
};
```

The service owns I/O, caching, and data transformation. The widget page
connects to its signals and only handles presentation. This keeps widget
code small and testable.

**Rules**:

- Services are `QObject` subclasses (for signals/slots + thread affinity).
- One service per domain (Steam, YouTube, NAS, etc.).
- Services never touch widgets. Widgets never do I/O.
- Services emit data via signals; widgets connect and render.

### Dependency Injection (Practical C++)

Full DI frameworks are rare in C++. Instead, use constructor injection:

```cpp
class GameStorePage : public QWidget {
public:
    explicit GameStorePage(SteamService* steam, QWidget* parent = nullptr);
};
```

The caller (usually `MainWindow`) creates the service and passes it in.
This makes the dependency explicit and testable (pass a mock in tests).

**Don't**: use singletons for services. They hide dependencies and make
testing impossible. If you need global-ish access, create the service in
`main()` and pass it down.

### Module Boundary Discipline

- **Headers expose interfaces, not implementation.** Use forward declarations
  in headers; include full definitions only in `.cpp` files.
- **Pimpl for ABI stability** in shared libraries. Not needed for
  application-internal code where recompilation is cheap.
- **No circular dependencies** between subsystems. If A needs B and B needs
  A, extract the shared interface into a third target.

## Debugging War Stories (Real Patterns)

These are the bugs professionals report most often:

1. **Dangling pointer after container resize** — vector reallocates, pointer
   into old buffer is now garbage.
2. **Stack variable used as parent** — Qt child outlives stack-allocated
   parent → double-free.
3. **DLL boundary STL export** — `std::vector` layout differs between DLLs
   compiled with different settings (MSVC). Never export STL types.
4. **Adding print statement hides bug** — changes stack layout, moves
   uninitialized memory just enough to avoid the crash.
5. **Heisenbug in optimized builds** — UB that only manifests with `-O2`
   because optimizer assumes no UB and removes "defensive" code.
6. **Memory ordering in multithreaded code** — the hardest bugs: data
   appears correct on x86 but fails on ARM due to weaker memory model.

## The Meme Wisdom (Real Lessons Behind the Jokes)

| Meme / Saying                                                                | Actual Lesson                                                               |
| ---------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| "It works on my machine"                                                     | Environment-specific bugs are real. Test in CI, not just locally.           |
| "It compiles, ship it"                                                       | Compilation ≠ correctness. C++ compiles plenty of UB.                       |
| "rm -rf /"                                                                   | Destructive operations need safeguards. Never run untested scripts as root. |
| "I don't always test, but when I do, I test in production"                   | Testing in production is not testing — it's praying.                        |
| "99 bugs on the wall, fix one, 127 bugs on the wall"                         | Fixing one bug can expose others. Regression tests matter.                  |
| "// TODO: fix later"                                                         | Technical debt compounds. Fix it now or document why not.                   |
| "Segfault is my middle name"                                                 | Use sanitizers, smart pointers, and bounds checking.                        |
| "There are only 2 hard problems: cache invalidation, naming, and off-by-one" | All three are real and common.                                              |

## Design Patterns Philosophy

The most common mistake with design patterns is applying them
**prescriptively** — picking a pattern before writing code and forcing the
implementation to fit it. The opposite is better: write the code that solves
the actual problem, then recognize and _name_ the pattern that emerged.

Key principles, sourced from r/cpp professional practitioners:

- **Patterns are discovered, not invented.** Extract them after the code
  exists, not before. When you look at code you've already written and say
  "this is a strategy pattern" — that is the right direction. Writing
  "I will use a strategy pattern here" first usually leads to over-engineered
  code that solves a problem you don't have yet.
- **For data-heavy or simulation code, question OOP first.** Class
  hierarchies and virtual dispatch are a poor fit for dense numerical data or
  loop-heavy simulation (emulators, physics, audio). Structs + free functions
  are often cleaner, faster, and easier to reason about. This applies
  directly to GBA and PS1 emulator code.
- **"If it feels amazing when a pattern fits, you forced it in."**
  Professionally: practitioners report refactoring forced patterns out of
  codebases more often than adding new ones. A pattern that fits feels
  _obvious_ in retrospect, not clever.
- **Most useful GoF patterns in practice**: factory, builder, decorator,
  strategy, observer. These are the ones that appear naturally in real
  codebases. The full GoF catalog exists as reference, not as a to-do list.
- **Patterns skew OOP/Java-origin.** Many were conceived around virtual
  dispatch, deep inheritance, and language constraints that modern C++ doesn't
  share. Evaluate each pattern against the C++ idioms available (CRTP, policy
  classes, lambdas, concepts) rather than applying the Java-flavored version.

### Design Pattern Anti-Patterns

| Anti-Pattern                                 | Why It Fails                        |
| -------------------------------------------- | ----------------------------------- |
| Factory for every object type                | Adds indirection without decoupling |
| Deep inheritance hierarchies                 | Tight coupling, hard to test, slow  |
| God object (one class for everything)        | No boundary → no modularity         |
| Template Method with many overrideable steps | Subclasses must know too much       |
| Forced visitor over simple `switch`          | Complexity without type-safety gain |

## Architecture Philosophy

### Design Document Before Code

Large subsystems benefit from a design document written **before** any C++ is
written. The key insight (from mredding, r/cpp): the document should describe
the system using mathematical primitives and logical relationships, **not C++
constructs**. If the document would look the same in any language, it's
describing the real system. If it's full of `std::vector` and `class Foo`, it
has pre-decided the implementation.

Rules for design documents:

1. **Describe modules and their interactions.** You don't need to understand
   the whole system — you need to understand the boundary of each piece.
2. **Write down every open question.** Never start coding an open question.
   If you don't know whether state goes here or there, that is the first thing
   to resolve, not the first thing to prototype around.
3. **The design is finished when there are no open questions.** Then and only
   then does the shape of the code become clear.

### Conway's Law

> Organizations which design systems are constrained to produce designs which
> are copies of the communication structures of those organizations.

In practice: the way your team is organized tends to show up in the software
structure. A single developer builds a monolith naturally. Subsystem
ownership boundaries in a team map to module/library boundaries in the code.
This is not a law to fight — it's a constraint to work _with_. If the desired
module boundaries don't match how the team is organized, expect constant
friction.

For AIO Server: single-developer project, so monolithic structure under
`src/` is natural. The subsystem boundaries (emulator, gui, server) are
maintained by CMake target discipline, not by team ownership.

### Scalability Means Modifiability

The word "scalable" is often misused to mean "pre-built to handle N times
the load." That is premature optimization.

Actual scalability in software design = **code that can be modified to scale
when the need arises**. This means:

- Low coupling between subsystems
- Clear, stable interfaces
- Tested behavior at boundaries
- No premature abstraction that locks in wrong assumptions

Plazmatic's principle (r/cpp): avoid premature optimization AND premature
architecture. Over-designed code is as dangerous as under-designed code,
because the wrong abstractions are harder to remove than no abstractions.

### Evolution Over Revolution

Most large codebases are not greenfield. They are accumulated evolution:

- **Don't redesign the whole thing.** Refactor the specific parts that are
  causing real problems.
- **Small, test-backed refactors.** Good tests make large structural changes
  safe. Without them, rearchitecting is risky.
- **Growing toward a design is normal.** Clean code usually arrives through
  iteration. The messy working version often contains implicit knowledge that
  a clean-room rewrite loses.

## Data Oriented Design (DOD)

DOD is a design approach that prioritizes **data layout and access patterns**
over object identity and encapsulation. It emerged from game development and
high-performance simulation, both of which have the same profile as emulator
code.

### Why DOD Matters for This Project

- GBA and PS1 emulators run tight inner loops over large blocks of memory
  (CPU fetch-decode-execute, PPU scanline rendering, DMA transfers).
- CPU caches care about access patterns, not object identity. Iterating over
  an `std::vector<Entity>` where each `Entity` has many rarely-used fields
  destroys cache efficiency.
- Virtual dispatch (vtable) prevents inlining and adds branching — both
  harmful in hot paths.

### DOD Principles

1. **Group data by how it is accessed, not by what it conceptually "belongs
   to".** If the PPU reads R, G, B components of all sprites in one pass,
   store `R[]`, `G[]`, `B[]` separately rather than `Sprite{R, G, B, ...}[]`.
2. **Structs of arrays (SoA) over arrays of structs (AoS)** in hot paths
   where partial fields are accessed in tight loops.
3. **Free functions over member functions** when the function transforms data
   without needing to maintain invariants. `applyDMA(DMAChannel& ch, Memory&
mem)` is often cleaner than `DMAChannel::apply(Memory& mem)`.
4. **Value types by default.** Passing `struct RGBA { uint8_t r,g,b,a; }` by
   value costs 4 bytes vs an object pointer (8 bytes), and avoids indirection.
5. **Batch operations over per-object operations.** A function that updates
   all particles at once often outperforms calling `particle.update()` for
   each one, because the batch version can be SIMD-vectorized.

### When OOP Is Still Correct

DOD and OOP are not opposites. Use the object model where it genuinely helps:

- **Subsystem encapsulation at coarse grain** (CPUCore, GPU, APU as classes
  is fine — they are long-lived, complex state machines).
- **Interfaces and polymorphism** when real runtime type selection is needed
  (e.g., different cartridge mappers).
- **RAII** resource management is inherently OOP and always correct.

The pattern is: OOP at the subsystem boundary level, DOD at the data
processing level inside hot paths.

## Testing Philosophy

### Math Code Is Uniquely Testable

Emulator and simulation code has a major advantage over most software: the
expected outputs are **mathematically defined**. The PS1 GTE should produce
exactly these transform results. The GBA timer should tick exactly N cycles
after M writes. There are no "acceptable ranges" — correctness is binary.

Exploit this aggressively:

- Every new CPU instruction implementation should have at least one test
  verifying the exact numerical output for a known input state.
- GTE operations have reference outputs computable from the spec. Use them.
- Timer tests can run cycle-exact because cycle counts are deterministic.

### Design for Testability

If code is **hard to test**, that is usually a design signal. The difficulty
often comes from:

- Hidden state dependencies (global singletons, mutable statics)
- Too many responsibilities in one class (does the thing AND logs AND caches)
- Constructor side effects (spawns a thread, opens a file, registers globally)

The fix is not to write more complex tests. The fix is to make the code
easier to test:

- Inject dependencies through constructor parameters
- Separate pure computation from I/O and state mutation
- Keep functions small enough to have obvious correct behavior

Hedshodd (r/cpp): "if hard to test → make it easier to test." This is
different from "write harder tests."

### Test Suite Organization

For a project like AIO Server:

- **Unit tests per subsystem**: CPUTests, PPUTests, GTETests, DMATests, etc.
- **Integration tests** verify subsystem interactions (PS1IntegrationTests)
- **Regression tests** lock in known-correct behavior before refactoring
- Keep test target names in sync with subsystem names for ctest `-R` filtering

## Strong Typing and Pure Functions

### Strong Typedefs

When two values have different semantic meaning, they should be different
types — even if both are `int`, `uint32_t`, or `float`.

```cpp
// Bad: both are uint32_t, easy to mix up
void writeGPU(uint32_t address, uint32_t command);

// Better: distinct types prevent passing command where address expected
struct GPUAddress { uint32_t value; };
struct GPUCommand { uint32_t value; };
void writeGPU(GPUAddress addr, GPUCommand cmd);
```

Where this matters in AIO Server:

- PS1 physical vs virtual (kseg0/kseg1/kseg2) addresses
- GBA ROM vs VRAM vs IWRAM vs EWRAM addresses
- Cycle counts vs frame counts vs scanline counts

Strong typedefs catch entire categories of bugs at compile time with zero
runtime cost.

### Pure Functions

A pure function has:

1. **No side effects** — it doesn't modify global state, emit signals, write
   to memory outside its scope, or cause I/O.
2. **Deterministic output** — same inputs always produce same output.

Benefits:

- Trivially unit-testable: no setup, no mocks, no state teardown
- Composable: `f(g(x))` just works
- Parallelizable: no shared mutable state
- Cacheable: output is a function of input only

For emulator code, pure functions are natural for:

- Instruction decode: `Instruction decode(uint32_t opcode)` — pure
- Address translation: `MemoryRegion resolve(uint32_t address)` — pure
- ALU operations: `uint32_t add(uint32_t a, uint32_t b, Flags& flags)` — nearly pure

**Rule of thumb**: start pure, add state only when necessary. Mutable state
should justify its existence.

### Namespace Discipline

Namespace pollution is the C++ equivalent of variable shadowing at global
scope. Rules:

- `using namespace std;` in headers is forbidden.
- Project code goes in a project-specific namespace or subdirectory scope.
- Anonymous namespaces (`namespace { ... }`) in `.cpp` files replace
  `static` for internal linkage — prefer them.
- Keep public API in the narrowest reasonable namespace.

## Recommended References

Books and resources that r/cpp practitioners cite as genuinely improving
C++ skill (not just syntax knowledge):

| Resource                                            | Why It Matters                                                                                                                    |
| --------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| _Effective Modern C++_ — Scott Meyers               | Maintainable modern C++. Move semantics, lambdas, smart pointers, `auto` idioms. Read before writing C++11/14/17 code.            |
| _Design Patterns_ — GoF (Gang of Four)              | The canonical pattern catalog. Important as vocabulary and reference, not as a prescription. Skews OOP/Java — read it critically. |
| _Pragmatic Programmer_ — Hunt & Thomas              | Craft over syntax. How to _think_ about software engineering, not just how to write code.                                         |
| _Code Complete 2nd_ — McConnell                     | Large-scale software construction. Still the best single book on software construction as a discipline.                           |
| _A Philosophy of Software Design_ — Ousterhout      | How to think about complexity. Modules, deep vs shallow interfaces, naming. Shorter and more opinionated than Code Complete.      |
| _C++ Coding Standards_ — Sutter & Alexandrescu      | 101 rules with rationale. "Consider/prefer" framing, not dogma. Companion to the Core Guidelines.                                 |
| _Designing Data-Intensive Applications_ — Kleppmann | For distributed/backend work. Less applicable to emulators but excellent on scaling, storage, and reliability tradeoffs.          |
| CppCon "Back to Basics" track                       | Video series. Patterns, RAII, templates, move semantics — high-quality peer-reviewed talks. Start here over random CppCon talks.  |

### Study the Source

Reading open-source codebases with documented design rationale teaches things
books don't. Practitioner recommendations:

- **Qt source** — well-structured, idiomatic C++. Signal/slot, event handling,
  and widget hierarchies are reference implementations.
- **Google/Facebook/Amazon open source** — large-scale C++ at production
  quality. Learn from the patterns, test infrastructure, and build systems.
- **MFEM** (finite element library) — numerical C++ with explicit design
  rationale. Note: intentionally avoids some modern C++ for HPC cluster
  portability — this is a deliberate design decision, not ignorance of
  modern idioms.

## Retrieval Hints

- Load when writing or reviewing C++ code, debugging crashes, investigating
  UB, optimizing performance, or designing new subsystems.
- Load also when evaluating or writing instruction file content for C++
  subsystems — this file provides the domain baseline for what good C++
  guidance covers.
- Key terms: RAII, smart pointer, undefined behavior, use-after-free,
  dangling, iterator invalidation, hidden copy, move semantics, data oriented
  design, pure function, strong typedef, design patterns, architecture.

## Verification Basis

Synthesized from: C++ Core Guidelines (Stroustrup/Sutter), cppreference.com
UB documentation, "100 C++ Mistakes" (Manning), Medium C++ performance
article, Stack Overflow UB thread, Reddit r/cpp professional experience
threads (r/cpp/comments/uu3vn4/ design patterns thread — 83 upvotes,
r/cpp/comments/15awwlz/ software architecture thread — 88 upvotes), and
project-specific debugging experience.
