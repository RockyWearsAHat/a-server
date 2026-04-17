# Source Map

Technical file-to-subsystem map for implementation agents. Read this when you need to locate code.

## Build

- Entry: `make build` → `Makefile` → `cmake/Makefile` → Ninja
- Test: `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`
- See `test-scoping.instructions.md` for the file-to-test scope map.

## GUI Shell

- `src/gui/` + `include/gui/`
- `HomeScreen.cpp/.h` — unified home dashboard, app tiles, organize mode. Tiles: YouTube, Netflix, DisneyPlus, Hulu, MediaServer, ScreenMirror, Store, Library, Settings. GBA/PS1/Switch console tiles have been **removed** — games are accessed via the Library tile only.
- `GamesLibraryPage.cpp/.h` — unified games library: scans ROM dirs, filter chips per platform (driven by `EmulatorFormats.h`), detail panel, launch button. Emulators back the content silently; no console-specific UI apps.
- `EmulatorFormats.h` — **single source of truth** for all platform badges, display names, file extensions, and launchability. Drives scanner, filter chips, badge colours, and RemoteControlServer state reporting. Add a platform here; everything else picks it up automatically.
- `MainWindow.cpp/.h` + `mainwindow/` partials — top-level window, page stack, navigation, input, audio
- `YouTubeBrowsePage.cpp/.h` — YouTube browse/search with tile grid (native, not a webview)
- `YouTubePlayerPage.cpp/.h` + `youtube/YouTubePlayerOverlay.cpp/.h` — YouTube playback + overlay
- `GameStorePage.cpp/.h` — **Fully native** game store: category tabs, card grid, detail panel, install/play CTAs. Backed by `SteamService.cpp/.h` for local library detection + proxy API. NOT a Steam webview. Must stay data-driven and multi-company-ready.
- `StreamingHubWidget.cpp/.h` — **Native** branded tile launcher for streaming apps (Netflix, Disney+, Hulu, YouTube). No webview here — pure Qt paint-rendered tiles.
- `StreamingWebViewPage.cpp/.h` — embedded WebEngine for actual streaming content (Netflix/Disney+/Hulu). Hub is native; content delivery is web.
- `NASPage.cpp/.h` + `NASAdapter.cpp/.h` — NAS media browser
- `ScreenMirrorPage.cpp/.h` — Screen Mirror / AirPlay receiver UI page
- `NavigationController.cpp/.h` — D-pad/remote navigation state machine
- `RemoteControlServer.cpp/.h` — HTTP REST dev-automation server (port 9876). Endpoints for navigation, state polling (7 sub-endpoints), event streaming, input injection, emulator control (launch-rom, stop-game, pause, resume, step-frame), and per-page structured state.
- `ButtonListAdapter.cpp/.h` — reusable button list component
- `GameSelectAdapter.cpp/.h` + `EmulatorSelectAdapter.cpp/.h` — ROM/emulator pickers
- `SettingsMenuAdapter.cpp/.h` + `EmulatorSettingsAdapter.cpp/.h` — settings UI
- `ThumbnailCache.cpp/.h` — async thumbnail loader
- `UIActionMapper.cpp/.h` — input-to-action mapping

## Styling

- `assets/qss/` — `youtube.qss`, `tv.qss`
- `assets/fonts.qrc` → `assets/fonts/` (Noto Sans, Walter)

## Screen Mirror

- `src/screenmirror/` + `include/screenmirror/`
- `AirPlayReceiver.cpp/.h` — Bonjour/mDNS advertisement + AirPlay HTTP server
- `MirrorSessionManager.cpp/.h` — Session lifecycle and state management

## Emulator Cores

- `src/emulator/` + `include/emulator/`
- GBA: `ARM7TDMI.cpp` (CPU), `PPU.cpp` (graphics), `APU.cpp` (audio), `GBAMemory.cpp`, `GBA.cpp` (system) — **launchable**
- PS1: `R3000A.cpp` (CPU), `PS1GPU.cpp`, `PS1SPU.cpp` (audio), `PS1DMA.cpp`, `GTE.cpp`, `PS1Memory.cpp`, `PS1.cpp` (system), `CDROM.cpp`, `PS1Timer.cpp`, `PS1Controller.cpp`, `InterruptController.cpp` — **launchable**
- SNES: `W65C816.cpp` (CPU), `SNESPPU.cpp`, `SPC700.cpp`, `SNESMemory.cpp`, `SNESCartridge.cpp`, `SNES.cpp` — core present, GUI launch **not yet wired**
- NES: `RP2A03.cpp` (CPU), `PPU2C02.cpp`, `APU2A03.cpp`, `NESMemory.cpp`, `NESCartridge.cpp`, `NES.cpp` — core present, GUI launch **not yet wired**
- GB/GBC: `LR35902.cpp`, `GBPPU.cpp`, `GBAPU.cpp`, `GBMemory.cpp`, `GBCartridge.cpp`, `GB.cpp` — core present, GUI launch **not yet wired**
- Genesis: `M68000.cpp`, `Z80.cpp`, `GenesisVDP.cpp`, `YM2612.cpp`, `SN76489.cpp`, `GenesisMemory.cpp`, `GenesisCartridge.cpp`, `Genesis.cpp` — core present, GUI launch **not yet wired**
- N64: `R4300i.cpp`, `RSP.cpp`, `RDP.cpp`, `N64Memory.cpp`, `N64Cartridge.cpp`, `N64.cpp` — core present, GUI launch **not yet wired**
- Switch: `CpuCore.cpp`, `GpuCore.cpp`, `MemoryManager.cpp`, `SwitchEmulator.cpp` — **indexed only, intentionally unavailable in production**

## Server

- `server/` — Node.js/TypeScript backend

## Tests

- `tests/` — GoogleTest, one binary per subsystem
- 30 binaries: `APUTests`, `Atari2600Tests`, `AudioCorruptionTests`, `BIOSTests`, `CPUTests`, `DeterminismTests`, `DMATests`, `DMATimingTests`, `EEPROMTests`, `GBAIntegrationTests`, `GbaTests`, `GraphicsCorruptionTests`, `InputLogicTests`, `LoggerTests`, `MemoryMapTests`, `PPUTests`, `PS1ControllerTests`, `PS1CPUTests`, `PS1DMATests`, `PS1GPUTests`, `PS1GTETests`, `PS1IntegrationTests`, `PS1InterruptTests`, `PS1MemoryTests`, `PS1SPUTests`, `PS1TimerTests`, `ROMMetadataTests`, `ScreenMirrorTests`, `SwitchCoreTests`, `WindowsCompatTests`
- Plus `QssValidation` (build-time, not a binary)

## Scripts & Tools

- `scripts/` — `visual_dev_loop.py`, `tas_determinism_test.py`, `test_suite.py`, `clean.sh`, plus debug/analysis helpers
- `scripts/tas_determinism_test.py` — TAS-driven frame-hash determinism validation.
	- GBA: production coverage with 120s checkpoints and committed baselines
	- NES/SNES/GB/GBC: script adapters exist but coverage depends on ROM and baseline setup
	- PS1: not yet supported in the TAS pipeline
	- Baselines: `test_output/tas_baselines/<rom_stem>/<ms>ms.ppm`
- MCP tools (search, knowledge cache, vision) are provided globally by the gsh MCP server

## Build Output

- `build/bin/` (AIOServer + test binaries), `build/lib/`, `build/generated/`

## Architecture Knowledge Index

Detailed architecture notes live alongside this source map in `.github/knowledge/`. Check the relevant doc before researching a subsystem from scratch.

| Domain                  | Knowledge Doc                           | Covers                                                          |
| ----------------------- | --------------------------------------- | --------------------------------------------------------------- |
| GUI shell & navigation  | `gui-architecture.md`                   | Page stack, NavigationController, adapters, MainWindow partials |
| AirPlay / Screen Mirror | `airplay-nas-architecture.md`           | Bonjour, pairing protocol, HTTP endpoints, NAS browser          |
| YouTube                 | `youtube-streaming-architecture.md`     | Browse/player pages, server backend, yt-dlp pipeline            |
| Streaming services      | `qt-webengine-streaming-integration.md` | WebEngine wrappers, StreamingWebViewPage, DRM                   |
| GBA emulator            | `gba-emulator-architecture.md`          | CPU, PPU, APU, memory, BIOS, timing                             |
| PS1 emulator            | `ps1-emulator-architecture.md`          | R3000A, GPU, SPU, DMA, GTE, CDROM, timers                       |
| Emulator verification   | `emulator-verification-pipeline.md`     | Layered correctness gates, platform coverage, deterministic flow |
| Build system            | `build-infrastructure.md`               | Makefile chain, CMake, vcpkg, test binaries                     |
| Research policy         | `research-policy.md`                    | Source priority, spec-first rules, prohibited sources           |
| UI state control        | `ui-state-and-transition-bugs.md`       | Hidden state, invalid transitions, sequence bug prevention      |
| Copilot setup           | `copilot-customization-layout.md`       | Agent roles, tool namespaces, layout rules                      |
