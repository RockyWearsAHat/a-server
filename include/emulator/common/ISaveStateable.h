#pragma once

namespace AIO::Emulator::Common {

/// Interface for subsystems that must participate in save-state serialization.
///
/// Every stateful component of a console emulator — CPU registers, memory
/// contents, I/O peripheral state — implements this interface so that the
/// global save-state machinery can gather and restore the full snapshot.
///
/// Design contract:
///   - Save() writes ALL mutable state that affects future Step() outcomes.
///   - Load() restores exactly what Save() wrote.
///   - A Save() immediately followed by a Load() on a fresh instance must
///     produce bit-identical Step() output (determinism test).
///   - Implementors do not write format headers or version tags; the outer
///     SaveStateWriter handles framing.
class ISaveStateable {
public:
    virtual ~ISaveStateable() = default;

    /// Serialize the component's state into @p writer.
    /// Called when the user requests a save slot.
    virtual void SaveState(class SaveStateWriter& writer) const = 0;

    /// Restore the component's state from @p reader.
    /// Called when the user loads a save slot.
    /// Throws std::runtime_error if the data is malformed or truncated.
    virtual void LoadState(class SaveStateReader& reader) = 0;
};

} // namespace AIO::Emulator::Common
