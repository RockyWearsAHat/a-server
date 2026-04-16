#pragma once

#include <QStringList>
#include <QVector>

namespace AIO::GUI {

/// One row in the emulator format registry.
struct EmulatorFormat {
  QString badge;        ///< Short identifier used as consoleBadge ("GBA", "PS1", "SNES", …)
  QString displayName;  ///< Human-readable platform name shown in detail panels
  QStringList extensions; ///< Lower-case file extensions without leading dot
  bool launchable;      ///< Whether the production shell can actually launch titles
};

/// Central registry of all supported platforms and their file formats.
///
/// Add a new platform here to make it visible in the Games Library scanner,
/// filter chips, badge renderer, and info panels — no other file changes needed.
///
/// Extension uniqueness: if an extension could belong to multiple platforms
/// (e.g. ".bin" for PS1 and Genesis), list it under the platform with higher
/// priority (first match wins during scanning).
inline const QVector<EmulatorFormat> &emulatorFormats() {
  static const QVector<EmulatorFormat> kFormats = {
      // ── Fully launchable ──────────────────────────────────────────────
      {"GBA", "Game Boy Advance", {"gba"}, true},
      {"PS1", "PlayStation", {"bin", "cue", "iso", "img", "chd", "pbp"}, true},
      {"Atari2600", "Atari 2600", {"a26"}, true},
      {"NES", "Nintendo", {"nes"}, true},
      {"Genesis", "Sega Genesis", {"md", "gen", "smd"}, true},
      {"SNES", "Super Nintendo", {"smc", "sfc", "fig", "swc"}, true},
      {"GameBoy", "Game Boy / Color", {"gb", "gbc"}, true},

      // ── Emulation cores present, GUI launch not yet wired ────────────
      {"N64", "Nintendo 64", {"z64", "v64", "n64"}, false},

      // ── Indexed only — intentionally unavailable in production ────────
      {"Switch", "Nintendo Switch", {"xci", "nsp", "nso", "nro"}, false},
  };
  return kFormats;
}

} // namespace AIO::GUI
