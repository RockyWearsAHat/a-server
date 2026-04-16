# Emulator core and main app build

# ─── Emulator Common (shared infrastructure) ─────────────────────────────────
add_library(EmulatorCommon STATIC
    ${PROJECT_ROOT}/src/emulator/common/Logger.cpp
    ${PROJECT_ROOT}/src/emulator/common/Fuzzer.cpp
    ${PROJECT_ROOT}/src/emulator/common/Scheduler.cpp
    ${PROJECT_ROOT}/src/emulator/common/SaveState.cpp
    ${PROJECT_ROOT}/src/emulator/common/TraceRecorder.cpp
)

target_include_directories(EmulatorCommon PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

set_target_properties(EmulatorCommon PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/EmulatorCommon"
)

# ─── PS1 Emulator ────────────────────────────────────────────────────────
add_library(PS1Emulator STATIC
    ${PROJECT_ROOT}/src/emulator/ps1/PS1.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/R3000A.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1Memory.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1GPU.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1SPU.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1DMA.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1MDEC.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/InterruptController.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1Timer.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/CDROM.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/GTE.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1Controller.cpp
    ${PROJECT_ROOT}/src/emulator/ps1/PS1HleBios.cpp
)

target_include_directories(PS1Emulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(PS1Emulator PUBLIC EmulatorCommon)

set_target_properties(PS1Emulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1Emulator"
)

# ─── GBA Emulator ────────────────────────────────────────────────────────
add_library(GBAEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/gba/GBA.cpp
    ${PROJECT_ROOT}/src/emulator/gba/GBAMemory.cpp
    ${PROJECT_ROOT}/src/emulator/gba/ARM7TDMI.cpp
    ${PROJECT_ROOT}/src/emulator/gba/PPU.cpp
    ${PROJECT_ROOT}/src/emulator/gba/APU.cpp
    ${PROJECT_ROOT}/src/emulator/gba/ROMMetadataAnalyzer.cpp
)

target_include_directories(GBAEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(GBAEmulator PUBLIC EmulatorCommon)

# Set autogen directory for GBAEmulator
set_target_properties(GBAEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBAEmulator"
)

# ─── NES Emulator ────────────────────────────────────────────────────────
add_library(NESEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/nes/NESCartridge.cpp
    ${PROJECT_ROOT}/src/emulator/nes/NESMemory.cpp
    ${PROJECT_ROOT}/src/emulator/nes/RP2A03.cpp
    ${PROJECT_ROOT}/src/emulator/nes/PPU2C02.cpp
    ${PROJECT_ROOT}/src/emulator/nes/APU2A03.cpp
    ${PROJECT_ROOT}/src/emulator/nes/NES.cpp
)

target_include_directories(NESEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(NESEmulator PUBLIC EmulatorCommon)

set_target_properties(NESEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/NESEmulator"
)

# ─── Genesis Emulator ────────────────────────────────────────────────────
add_library(GenesisEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/genesis/GenesisCartridge.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/GenesisMemory.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/M68000.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/Z80.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/GenesisVDP.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/YM2612.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/SN76489.cpp
    ${PROJECT_ROOT}/src/emulator/genesis/Genesis.cpp
)

target_include_directories(GenesisEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(GenesisEmulator PUBLIC EmulatorCommon)

set_target_properties(GenesisEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GenesisEmulator"
)

# ─── SNES Emulator ───────────────────────────────────────────────────────
add_library(SNESEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/snes/SNESCartridge.cpp
    ${PROJECT_ROOT}/src/emulator/snes/SNESMemory.cpp
    ${PROJECT_ROOT}/src/emulator/snes/W65C816.cpp
    ${PROJECT_ROOT}/src/emulator/snes/SPC700.cpp
    ${PROJECT_ROOT}/src/emulator/snes/SNESPPU.cpp
    ${PROJECT_ROOT}/src/emulator/snes/SNES.cpp
)

target_include_directories(SNESEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(SNESEmulator PUBLIC EmulatorCommon)

set_target_properties(SNESEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SNESEmulator"
)

# ─── GB/GBC Emulator ─────────────────────────────────────────────────────
add_library(GBEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/gb/GBCartridge.cpp
    ${PROJECT_ROOT}/src/emulator/gb/GBMemory.cpp
    ${PROJECT_ROOT}/src/emulator/gb/LR35902.cpp
    ${PROJECT_ROOT}/src/emulator/gb/GBPPU.cpp
    ${PROJECT_ROOT}/src/emulator/gb/GBAPU.cpp
    ${PROJECT_ROOT}/src/emulator/gb/GB.cpp
)

target_include_directories(GBEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(GBEmulator PUBLIC EmulatorCommon)

set_target_properties(GBEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBEmulator"
)

# ─── N64 Emulator ────────────────────────────────────────────────────────
add_library(N64Emulator STATIC
    ${PROJECT_ROOT}/src/emulator/n64/R4300i.cpp
    ${PROJECT_ROOT}/src/emulator/n64/RSP.cpp
    ${PROJECT_ROOT}/src/emulator/n64/RDP.cpp
    ${PROJECT_ROOT}/src/emulator/n64/N64Memory.cpp
    ${PROJECT_ROOT}/src/emulator/n64/N64Cartridge.cpp
    ${PROJECT_ROOT}/src/emulator/n64/N64.cpp
)

target_include_directories(N64Emulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(N64Emulator PUBLIC EmulatorCommon)

set_target_properties(N64Emulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/N64Emulator"
)

# ─── Saturn Emulator ─────────────────────────────────────────────────────
add_library(SaturnEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/saturn/SH2.cpp
    ${PROJECT_ROOT}/src/emulator/saturn/VDP1.cpp
    ${PROJECT_ROOT}/src/emulator/saturn/VDP2.cpp
    ${PROJECT_ROOT}/src/emulator/saturn/SaturnMemory.cpp
    ${PROJECT_ROOT}/src/emulator/saturn/Saturn.cpp
)

target_include_directories(SaturnEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(SaturnEmulator PUBLIC EmulatorCommon)

set_target_properties(SaturnEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SaturnEmulator"
)

add_library(DreamcastEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/dreamcast/SH4.cpp
    ${PROJECT_ROOT}/src/emulator/dreamcast/PowerVR2.cpp
    ${PROJECT_ROOT}/src/emulator/dreamcast/DreamcastMemory.cpp
    ${PROJECT_ROOT}/src/emulator/dreamcast/Dreamcast.cpp
)

target_include_directories(DreamcastEmulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(DreamcastEmulator PUBLIC EmulatorCommon)

set_target_properties(DreamcastEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DreamcastEmulator"
)

add_library(PS2Emulator STATIC
    ${PROJECT_ROOT}/src/emulator/ps2/R5900.cpp
    ${PROJECT_ROOT}/src/emulator/ps2/R3000A.cpp
    ${PROJECT_ROOT}/src/emulator/ps2/GS.cpp
    ${PROJECT_ROOT}/src/emulator/ps2/PS2Memory.cpp
    ${PROJECT_ROOT}/src/emulator/ps2/PS2.cpp
)

target_include_directories(PS2Emulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(PS2Emulator PUBLIC EmulatorCommon)

set_target_properties(PS2Emulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS2Emulator"
)

add_library(SwitchEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/switch/SwitchEmulator.cpp
    ${PROJECT_ROOT}/src/emulator/switch/MemoryManager.cpp
    ${PROJECT_ROOT}/src/emulator/switch/CpuCore.cpp
    ${PROJECT_ROOT}/src/emulator/switch/GpuCore.cpp
    ${PROJECT_ROOT}/src/emulator/switch/ServiceManager.cpp
)

target_include_directories(SwitchEmulator PUBLIC 
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

# Set autogen directory for SwitchEmulator
set_target_properties(SwitchEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SwitchEmulator"
)

# ─── Windows Compat Layer ─────────────────────────────────────────────────
add_library(WindowsCompatLayer STATIC
    ${PROJECT_ROOT}/src/emulator/windows/WindowsEmulator.cpp
    ${PROJECT_ROOT}/src/emulator/windows/X86_64Core.cpp
    ${PROJECT_ROOT}/src/emulator/windows/WinMemory.cpp
    ${PROJECT_ROOT}/src/emulator/windows/WinAPILayer.cpp
    ${PROJECT_ROOT}/src/emulator/windows/WinProcess.cpp
)

target_include_directories(WindowsCompatLayer PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

target_link_libraries(WindowsCompatLayer PUBLIC Qt6::Gui)

set_target_properties(WindowsCompatLayer PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/WindowsCompatLayer"
)

add_executable(AIOServer 
    ${PROJECT_ROOT}/src/main.cpp
    ${PROJECT_ROOT}/assets/fonts.qrc
    ${PROJECT_ROOT}/assets/store.qrc
    ${PROJECT_ROOT}/src/common/Dotenv.cpp
    ${PROJECT_ROOT}/src/common/Logging.cpp
    ${PROJECT_ROOT}/src/common/CssVars.cpp
    ${PROJECT_ROOT}/src/common/PixelScaler.cpp
    ${PROJECT_ROOT}/src/common/AssetPaths.cpp
    ${PROJECT_ROOT}/src/common/AudioRecorder.cpp
    ${PROJECT_ROOT}/src/common/VideoRecorder.cpp
    ${PROJECT_ROOT}/src/nas/NASServer.cpp
    ${PROJECT_ROOT}/src/nas/server/NASServer_Http.cpp
    ${PROJECT_ROOT}/src/nas/server/NASServer_Routing.cpp
    ${PROJECT_ROOT}/src/gui/MainWindow.cpp
    ${PROJECT_ROOT}/src/gui/mainwindow/MainWindow_InputAudio.cpp
    ${PROJECT_ROOT}/src/gui/mainwindow/MainWindow_Emulation.cpp
    ${PROJECT_ROOT}/src/gui/mainwindow/MainWindow_Navigation.cpp
    ${PROJECT_ROOT}/src/gui/mainwindow/MainWindow_Pages.cpp
    ${PROJECT_ROOT}/src/gui/LogViewerDialog.cpp
    ${PROJECT_ROOT}/src/gui/HomeScreen.cpp
    ${PROJECT_ROOT}/src/gui/StreamingWebViewPage.cpp
    ${PROJECT_ROOT}/src/gui/NASPage.cpp
    ${PROJECT_ROOT}/src/gui/NASAdapter.cpp
    ${PROJECT_ROOT}/src/gui/ScreenMirrorPage.cpp
    ${PROJECT_ROOT}/src/gui/GameStorePage.cpp
    ${PROJECT_ROOT}/src/gui/SteamAuthDialog.cpp
    ${PROJECT_ROOT}/src/gui/SteamService.cpp
    ${PROJECT_ROOT}/src/gui/GamesLibraryPage.cpp
    ${PROJECT_ROOT}/src/screenmirror/AirPlayReceiver.cpp
    ${PROJECT_ROOT}/src/screenmirror/AirPlayPairing.cpp
    ${PROJECT_ROOT}/src/screenmirror/MirrorSessionManager.cpp
    ${PROJECT_ROOT}/src/gui/YouTubeBrowsePage.cpp
    ${PROJECT_ROOT}/src/gui/YouTubePlayerPage.cpp
    ${PROJECT_ROOT}/src/gui/youtube/YouTubePlayerOverlay.cpp
    ${PROJECT_ROOT}/src/gui/ThumbnailCache.cpp
    ${PROJECT_ROOT}/src/gui/NavigationController.cpp
    ${PROJECT_ROOT}/src/gui/UIActionMapper.cpp
    ${PROJECT_ROOT}/src/gui/MainMenuAdapter.cpp
    ${PROJECT_ROOT}/src/gui/ButtonListAdapter.cpp
    ${PROJECT_ROOT}/src/gui/EmulatorSelectAdapter.cpp
    ${PROJECT_ROOT}/src/gui/GameSelectAdapter.cpp
    ${PROJECT_ROOT}/src/gui/EmulatorSettingsAdapter.cpp
    ${PROJECT_ROOT}/src/gui/SettingsMenuAdapter.cpp
    ${PROJECT_ROOT}/src/gui/RemoteControlServer.cpp
    ${PROJECT_ROOT}/src/input/InputManager.cpp
    ${PROJECT_ROOT}/src/input/manager/InputManager_SDL.cpp
    ${PROJECT_ROOT}/src/input/InputBindings_Default.cpp
    ${PROJECT_ROOT}/src/streaming/StreamingManager.cpp
    ${PROJECT_ROOT}/src/streaming/YouTubeService.cpp
    ${PROJECT_ROOT}/src/streaming/NetflixService.cpp
    ${PROJECT_ROOT}/src/streaming/DisneyPlusService.cpp
    ${PROJECT_ROOT}/src/streaming/HuluService.cpp
    # Headers with Q_OBJECT for MOC processing
    ${PROJECT_ROOT}/include/gui/MainWindow.h
    ${PROJECT_ROOT}/include/nas/NASServer.h
    ${PROJECT_ROOT}/include/gui/LogViewerDialog.h
    ${PROJECT_ROOT}/include/gui/HomeScreen.h
    ${PROJECT_ROOT}/include/gui/StreamingWebViewPage.h
    ${PROJECT_ROOT}/include/gui/NASPage.h
    ${PROJECT_ROOT}/include/gui/NASAdapter.h
    ${PROJECT_ROOT}/include/gui/ScreenMirrorPage.h
    ${PROJECT_ROOT}/include/gui/GameStorePage.h
    ${PROJECT_ROOT}/include/gui/SteamAuthDialog.h
    ${PROJECT_ROOT}/include/gui/SteamService.h
    ${PROJECT_ROOT}/include/gui/GamesLibraryPage.h
    ${PROJECT_ROOT}/include/screenmirror/AirPlayReceiver.h
    ${PROJECT_ROOT}/include/screenmirror/MirrorSessionManager.h
    ${PROJECT_ROOT}/include/gui/YouTubeBrowsePage.h
    ${PROJECT_ROOT}/include/gui/YouTubePlayerPage.h
    ${PROJECT_ROOT}/include/gui/youtube/YouTubePlayerOverlay.h
    ${PROJECT_ROOT}/include/gui/ThumbnailCache.h
    ${PROJECT_ROOT}/include/common/Dotenv.h
    ${PROJECT_ROOT}/include/common/Logging.h
    ${PROJECT_ROOT}/include/common/CssVars.h
    ${PROJECT_ROOT}/include/common/AssetPaths.h
    ${PROJECT_ROOT}/include/gui/RemoteControlServer.h
    ${PROJECT_ROOT}/include/input/InputManager.h
    ${PROJECT_ROOT}/include/streaming/StreamingManager.h
    ${PROJECT_ROOT}/include/streaming/StreamingService.h
    ${PROJECT_ROOT}/include/streaming/YouTubeService.h
    ${PROJECT_ROOT}/include/streaming/NetflixService.h
    ${PROJECT_ROOT}/include/streaming/DisneyPlusService.h
    ${PROJECT_ROOT}/include/streaming/HuluService.h
)

target_include_directories(AIOServer PRIVATE 
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/gui
    ${SDL2_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    $<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(AIOServer PRIVATE
    Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Multimedia
    GBAEmulator GenesisEmulator SNESEmulator GBEmulator N64Emulator SaturnEmulator DreamcastEmulator PS2Emulator SwitchEmulator PS1Emulator WindowsCompatLayer SDL2::SDL2
    CURL::libcurl OpenSSL::SSL OpenSSL::Crypto
    $<$<BOOL:${APPLE}>:${VIDEOTOOLBOX_FRAMEWORK}>
    $<$<BOOL:${APPLE}>:${COREVIDEO_FRAMEWORK}>
    $<$<BOOL:${APPLE}>:${COREMEDIA_FRAMEWORK}>
)

# Precompiled headers — compile heavy Qt + stdlib headers once, reuse across all TUs
target_precompile_headers(AIOServer PRIVATE
    <QCheckBox>
    <QElapsedTimer>
    <QImage>
    <QLabel>
    <QListWidget>
    <QMainWindow>
    <QPushButton>
    <QSettings>
    <QStackedWidget>
    <QTimer>
    <array>
    <atomic>
    <cstdint>
    <memory>
    <mutex>
    <string>
    <thread>
    <vector>
)

target_precompile_headers(GBAEmulator PRIVATE
    <array>
    <atomic>
    <cstdint>
    <memory>
    <mutex>
    <string>
    <vector>
)

target_precompile_headers(PS1Emulator PRIVATE
    <array>
    <cstdint>
    <memory>
    <string>
    <vector>
)

# Set autogen directory for AIOServer
set_target_properties(AIOServer PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/AIOServer"
)

add_dependencies(AIOServer QssValidator)
add_custom_command(TARGET AIOServer POST_BUILD
    COMMAND "$<TARGET_FILE:QssValidator>" "${PROJECT_ROOT}/assets/qss"
    COMMENT "Validating QSS stylesheets"
)

# Sign with get-task-allow so AMFI permits kernel core dumps on macOS Sequoia+
if(APPLE)
    add_custom_command(TARGET AIOServer POST_BUILD
        COMMAND codesign --force --sign - --entitlements
            "${PROJECT_ROOT}/cmake/debuggable.entitlements.plist"
            "$<TARGET_FILE:AIOServer>"
        COMMENT "Re-signing AIOServer with get-task-allow for core dump support"
    )
endif()
