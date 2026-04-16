# Source Map

Technical file-to-subsystem map for implementation agents. Read this when you need to locate code.

## Build

- Entry: `make build` → `Makefile` → `cmake/Makefile` → Ninja
- Test: `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`
- See `test-scoping.instructions.md` for the file-to-test scope map.

## GUI Shell

- `src/gui/` + `include/gui/`
- `HomeScreen.cpp/.h` — unified home dashboard, app tiles, organize mode. **Target redesign: symmetric grid, rich contextual info on focus, Xbox/Wii/FireStick-quality — current functional state is far below target.**
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
- GBA: `ARM7TDMI.cpp` (CPU), `PPU.cpp` (graphics), `APU.cpp` (audio), `GBAMemory.cpp`, `GBA.cpp` (system)
- PS1: `R3000A.cpp` (CPU), `PS1GPU.cpp`, `PS1SPU.cpp` (audio), `PS1DMA.cpp`, `GTE.cpp`, `PS1Memory.cpp`, `PS1.cpp` (system), `CDROM.cpp`, `PS1Timer.cpp`, `PS1Controller.cpp`, `InterruptController.cpp`
- Switch: `CpuCore.cpp`, `GpuCore.cpp`, `MemoryManager.cpp`, `SwitchEmulator.cpp` (early stage)

## Server

- `server/` — Node.js/TypeScript backend

## Tests

- `tests/` — GoogleTest, one binary per subsystem
- 26 binaries: `APUTests`, `AudioCorruptionTests`, `BIOSTests`, `CPUTests`, `DMATests`, `DMATimingTests`, `EEPROMTests`, `GBAIntegrationTests`, `GbaTests`, `GraphicsCorruptionTests`, `InputLogicTests`, `LoggerTests`, `MemoryMapTests`, `PPUTests`, `PS1ControllerTests`, `PS1CPUTests`, `PS1DMATests`, `PS1GPUTests`, `PS1GTETests`, `PS1IntegrationTests`, `PS1InterruptTests`, `PS1MemoryTests`, `PS1SPUTests`, `PS1TimerTests`, `ROMMetadataTests`, `ScreenMirrorTests`
- Plus `QssValidation` (build-time, not a binary)

## Scripts & Tools

- `scripts/` — `visual_dev_loop.py`, `test_suite.py`, `clean.sh`, plus debug/analysis helpers
- MCP tools (search, knowledge cache, vision) are provided globally by the gsh MCP server

## Build Output

- `build/bin/` (AIOServer + 25 test binaries), `build/lib/`, `build/generated/`

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
| Build system            | `build-infrastructure.md`               | Makefile chain, CMake, vcpkg, test binaries                     |
| Research policy         | `research-policy.md`                    | Source priority, spec-first rules, prohibited sources           |
| UI state control        | `ui-state-and-transition-bugs.md`       | Hidden state, invalid transitions, sequence bug prevention      |
| Copilot setup           | `copilot-customization-layout.md`       | Agent roles, tool namespaces, layout rules                      |
