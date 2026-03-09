# Emulator core and main app build

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
    ${PROJECT_ROOT}/src/emulator/common/Logger.cpp
)

target_include_directories(PS1Emulator PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

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
    ${PROJECT_ROOT}/src/emulator/common/Logger.cpp
    ${PROJECT_ROOT}/src/emulator/common/Fuzzer.cpp
)

target_include_directories(GBAEmulator PUBLIC 
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
)

# Set autogen directory for GBAEmulator
set_target_properties(GBAEmulator PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBAEmulator"
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

add_executable(AIOServer 
    ${PROJECT_ROOT}/src/main.cpp
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
    ${PROJECT_ROOT}/src/gui/StreamingHubWidget.cpp
    ${PROJECT_ROOT}/src/gui/StreamingWebViewPage.cpp
    ${PROJECT_ROOT}/src/gui/NASPage.cpp
    ${PROJECT_ROOT}/src/gui/NASAdapter.cpp
    ${PROJECT_ROOT}/src/gui/YouTubeBrowsePage.cpp
    ${PROJECT_ROOT}/src/gui/YouTubePlayerPage.cpp
    ${PROJECT_ROOT}/src/gui/ThumbnailCache.cpp
    ${PROJECT_ROOT}/src/gui/NavigationController.cpp
    ${PROJECT_ROOT}/src/gui/UIActionMapper.cpp
    ${PROJECT_ROOT}/src/gui/MainMenuAdapter.cpp
    ${PROJECT_ROOT}/src/gui/ButtonListAdapter.cpp
    ${PROJECT_ROOT}/src/gui/EmulatorSelectAdapter.cpp
    ${PROJECT_ROOT}/src/gui/GameSelectAdapter.cpp
    ${PROJECT_ROOT}/src/gui/EmulatorSettingsAdapter.cpp
    ${PROJECT_ROOT}/src/gui/SettingsMenuAdapter.cpp
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
    ${PROJECT_ROOT}/include/gui/StreamingHubWidget.h
    ${PROJECT_ROOT}/include/gui/StreamingWebViewPage.h
    ${PROJECT_ROOT}/include/gui/NASPage.h
    ${PROJECT_ROOT}/include/gui/NASAdapter.h
    ${PROJECT_ROOT}/include/gui/YouTubeBrowsePage.h
    ${PROJECT_ROOT}/include/gui/YouTubePlayerPage.h
    ${PROJECT_ROOT}/include/gui/ThumbnailCache.h
    ${PROJECT_ROOT}/include/common/Dotenv.h
    ${PROJECT_ROOT}/include/common/Logging.h
    ${PROJECT_ROOT}/include/common/CssVars.h
    ${PROJECT_ROOT}/include/common/AssetPaths.h
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
    $<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(AIOServer PRIVATE Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets GBAEmulator SwitchEmulator PS1Emulator SDL2::SDL2 CURL::libcurl)

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

# Sign with get-task-allow so AMFI permits kernel core dumps on macOS Sequoia+
if(APPLE)
    add_custom_command(TARGET AIOServer POST_BUILD
        COMMAND codesign --force --sign - --entitlements
            "${PROJECT_ROOT}/cmake/debuggable.entitlements.plist"
            "$<TARGET_FILE:AIOServer>"
        COMMENT "Re-signing AIOServer with get-task-allow for core dump support"
    )
endif()
