# GUI Shell Architecture

> **Last audited**: 2025-07-18

## Overview

Qt 6 widget-based TV shell for 10-foot viewing. Tile-based home screen, QStackedWidget page navigation, D-pad-first input with mouse/keyboard fallback.

## Input Pipeline

```
SDL GameController / Qt Keyboard → InputManager (100Hz poll)
  → InputSnapshot {keyinput, logical, system}
  → UIActionMapper (edge detection, directional repeat)
  → UIActionFrame {pressed, justPressed, justReleased}
  → NavigationController (index tracking, adapter binding)
  → NavigationAdapter callbacks (setHoveredIndex, activateIndex, back)
```

## Page Stack (QStackedWidget)

```
MainWindow (QMainWindow)
└── QStackedWidget (centralWidget)
    ├── 0: mainMenuPage (MainMenuAdapter → QPushButton grid)
    ├── 1: homeScreenPage (HomeScreen → tile grid)
    ├── 2: emulatorSelectPage (EmulatorSelectAdapter)
    ├── 3: gameSelectPage (GameSelectAdapter + QListWidget)
    ├── 4: emulatorPage (display + statusLabel for live emulation)
    ├── 5: emulatorSettingsPage (rebinding UI)
    ├── 6: settingsPage (SettingsMenuAdapter)
    ├── 7: streamingHubPage (StreamingHubWidget)
    ├── 8: youTubeBrowsePage (YouTubeBrowsePage)
    ├── 9: youTubePlayerPage (YouTubePlayerPage)
    ├── 10: streamingWebPage (StreamingWebViewPage, QtWebEngine)
    ├── 11: nasPage (NASPage / NASAdapter)
    └── 12: screenMirrorPage (ScreenMirrorPage)
```

`onPageChanged()`: Resets nav hover, clears UIActionMapper edge state, releases emulator keys, rebinds adapter.

## NavigationAdapter Hierarchy

```
NavigationAdapter (abstract: itemCount, setHoveredIndex, activateIndex, back)
├── ButtonListAdapter (template for button-list menus)
│   ├── MainMenuAdapter
│   ├── EmulatorSelectAdapter
│   ├── GameSelectAdapter
│   ├── SettingsMenuAdapter
│   └── EmulatorSettingsAdapter
└── HomeScreen (unique 2D grid, no adapter — handles D-pad internally)
```

## File Map

| File                                | Purpose                                   |
| ----------------------------------- | ----------------------------------------- |
| `src/gui/MainWindow.cpp`            | Primary window, page stack, emulator host |
| `src/gui/MainWindow_Navigation.cpp` | Input polling, page transitions           |
| `src/gui/MainWindow_Pages.cpp`      | Page setup/teardown                       |
| `src/gui/MainWindow_Emulation.cpp`  | Frame capture, emulator thread            |
| `src/gui/MainWindow_InputAudio.cpp` | SDL audio, key translation                |
| `src/gui/HomeScreen.cpp`            | Tile launcher, organize mode, hero panel  |
| `src/gui/HomeTile.cpp`              | Individual tile widget                    |
| `src/gui/NavigationController.cpp`  | Stateful D-pad navigator                  |
| `src/gui/UIActionMapper.cpp`        | InputSnapshot → UIActionFrame             |
| `src/gui/ButtonListAdapter.cpp`     | Generic menu adapter                      |
| `src/gui/InputManager.cpp`          | Global SDL + keyboard state               |
| `src/gui/RemoteControlServer.cpp`   | HTTP server for dev loop automation       |
| `src/gui/CssVars.cpp`               | CSS custom property extraction            |
| `src/gui/PixelScaler.cpp`           | Emulator output scaling                   |
| `src/gui/ThumbnailCache.cpp`        | Async image caching                       |

## QSS Styling

- `assets/qss/tv.qss` — Main 10-foot TV theme
- `assets/qss/youtube.qss` — YouTube-specific overrides
- QssValidator runs post-build; build fails on QSS syntax errors

## Signal/Slot Key Connections

```
HomeScreen::gbaRequested    → MainWindow::goToGameSelect() [GBA]
HomeScreen::ps1Requested    → MainWindow::goToGameSelect() [PS1]
HomeScreen::switchRequested → MainWindow::goToGameSelect() [Switch]
HomeScreen::nasRequested    → MainWindow::goToNAS()
HomeScreen::settingsRequested → MainWindow::goToSettings()
HomeScreen::streamingAppRequested → MainWindow::launchStreamingApp()

YouTubeBrowsePage::videoRequested → launchStreamingApp() → YouTubePlayerPage::playVideoUrl()
YouTubePlayerPage::homeRequested → MainWindow::openStreaming()

startGame(path) → SetEmulatorType → LoadROM → StartEmulatorThread
displayTimer (100ms) → UpdateDisplay [UI refresh loop]
stopGame() → StopEmulatorThread → displayTimer→stop()
```

## Remote Control Server

- HTTP REST, port 9876 (fallback +1..5), port written to `test_output/visual_loop/remote_port`
- Used by `scripts/visual_dev_loop.py` and Copilot agents for dev automation

**Input injection**: `POST /input/key`, `POST /input/click`, `POST /input/key-sequence`, `POST /input/type`  
**State queries**: `GET /state` (full), `/state/navigation`, `/state/page` (+ per-page `gameStore`/`gamesLibrary`/`streamingHub` objects), `/state/widgets`, `/state/input`, `/state/emulator`, `/state/audio`  
**Event streaming**: `GET /events?since=<epochMs>&limit=<N>` — ring buffer of last 500 events (`navigate_requested`, `page_changed`, `emulator_started/stopped/paused/resumed/stalled`, `audio_silence_detected/resumed`, `key_injected`, `click_injected`, `launch_rom`, `game_stopped`)  
**Execute actions** (`POST /execute` with `{"action":"<name>","params":{...}}`):

- `navigate` — go to named page; stops any running emulator first
- `launch-rom` — load ROM, start emulation (resolves path: romDirectory → CWD → workspace root → `test_roms/`)
- `stop-game` — stop emulator, return to home
- `pause` / `resume` / `toggle-pause` — emulator pause control
- `step-frame` — advance one frame while paused
- `select` — move home screen tile focus
- `press` — inject key with optional count/delay
- `dump-frame` — write emulator frame to PPM
- `start-audio-recording` / `stop-audio-recording` — WAV capture
- `list-actions` — returns full action catalog at runtime

## Home Screen

- Tile-based unified launcher replacing traditional menu hierarchy
- Organize mode: reorder/customize tiles via D-pad (hold confirm to grab, move, release)
- Layout persisted via QSettings
- Hero panel for focused tile
- Tile animations on focus/unfocus
- Default gameplay entry now routes through a single Library tile instead of per-emulator wrappers; `MainWindow::launchInstalledGame()` centralizes installed-game launch dispatch so future backends can plug in without changing page wiring.

## Steam / Game Store Integration

### Architecture

- `SteamService` (`include/gui/SteamService.h`, `src/gui/SteamService.cpp`): fetches Steam app list via local Node.js proxy (`/api/steam/apps`), caches to `~/.local/share/AIOServer/steam_catalog.json` (24h TTL), and resolves installed Steam app manifests across macOS, Linux, and Windows library locations.
- `GameStorePage` (`include/gui/GameStorePage.h`, `src/gui/GameStorePage.cpp`): Qt widget page showing Steam catalog with tab-filtered 4-column card grid. Receives `SteamService::gamesReady` signal.
- `MainWindow_Pages.cpp`: wires `setupGameStorePage()` and `goToGameStore()`.
- `MainWindow::launchSteamGame()` now uses `steam://run/<appId>` for installed titles and `steam://install/<appId>` for titles that are not installed, so the store can initiate installs directly from the app.
- `server/src/index.ts`: `/api/steam/apps` route — proxies Steam GetAppList/v2, caches body, slices to 500 entries.

### Known bugs fixed (2026-03-23)

1. Server proxy returned full 100K app list — added `sliceSteamApps()` to limit to 500 entries both on fresh fetch and cache-serve.
2. `QLayoutItem*` from `tabLay->takeAt(0)` was discarded (leak) — changed to `delete tabLay->takeAt(0)`.
3. `showSteamError` was non-idempotent — added `errorShown_` bool flag guard; reset in `onSteamGamesReady`.
4. `launchSteamGame` passed appId=0 for invalid/empty steamAppId strings — added `if (appId <= 0) return;` guard.
