# Emulator core and main app build
add_library(GBAEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/gba/GBA.cpp
    ${PROJECT_ROOT}/src/emulator/gba/GBAMemory.cpp
    ${PROJECT_ROOT}/src/emulator/gba/ARM7TDMI.cpp
    ${PROJECT_ROOT}/src/emulator/gba/PPU.cpp
    ${PROJECT_ROOT}/src/emulator/gba/APU.cpp
    ${PROJECT_ROOT}/src/emulator/gba/GameDB.cpp
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
    ${PROJECT_ROOT}/src/gui/MainWindow.cpp
    ${PROJECT_ROOT}/src/gui/InputConfigDialog.cpp
    ${PROJECT_ROOT}/src/gui/LogViewerDialog.cpp
    ${PROJECT_ROOT}/src/gui/StreamingHubWidget.cpp
    ${PROJECT_ROOT}/src/gui/StreamingWebViewPage.cpp
    ${PROJECT_ROOT}/src/gui/YouTubeBrowsePage.cpp
    ${PROJECT_ROOT}/src/gui/YouTubePlayerPage.cpp
    ${PROJECT_ROOT}/src/gui/ThumbnailCache.cpp
    ${PROJECT_ROOT}/src/gui/NavigationController.cpp
    ${PROJECT_ROOT}/src/gui/UIActionMapper.cpp
    ${PROJECT_ROOT}/src/gui/MainMenuAdapter.cpp
    ${PROJECT_ROOT}/src/gui/ButtonListAdapter.cpp
    ${PROJECT_ROOT}/src/gui/EmulatorSelectAdapter.cpp
    ${PROJECT_ROOT}/src/gui/GameSelectAdapter.cpp
    ${PROJECT_ROOT}/src/gui/SettingsMenuAdapter.cpp
    ${PROJECT_ROOT}/src/gui/ActionBindingsDialog.cpp
    ${PROJECT_ROOT}/src/gui/ControllerDiagramWidget.cpp
    ${PROJECT_ROOT}/src/input/InputManager.cpp
    ${PROJECT_ROOT}/src/input/AppActions.cpp
    ${PROJECT_ROOT}/src/input/ActionBindings.cpp
    ${PROJECT_ROOT}/src/streaming/StreamingManager.cpp
    ${PROJECT_ROOT}/src/streaming/YouTubeService.cpp
    ${PROJECT_ROOT}/src/streaming/NetflixService.cpp
    ${PROJECT_ROOT}/src/streaming/DisneyPlusService.cpp
    ${PROJECT_ROOT}/src/streaming/HuluService.cpp
    # Headers with Q_OBJECT for MOC processing
    ${PROJECT_ROOT}/include/gui/MainWindow.h
    ${PROJECT_ROOT}/include/gui/InputConfigDialog.h
    ${PROJECT_ROOT}/include/gui/LogViewerDialog.h
    ${PROJECT_ROOT}/include/gui/StreamingHubWidget.h
    ${PROJECT_ROOT}/include/gui/StreamingWebViewPage.h
    ${PROJECT_ROOT}/include/gui/YouTubeBrowsePage.h
    ${PROJECT_ROOT}/include/gui/YouTubePlayerPage.h
    ${PROJECT_ROOT}/include/gui/ThumbnailCache.h
    ${PROJECT_ROOT}/include/common/Dotenv.h
    ${PROJECT_ROOT}/include/input/InputManager.h
    ${PROJECT_ROOT}/include/gui/ActionBindingsDialog.h
    ${PROJECT_ROOT}/include/gui/ControllerDiagramWidget.h
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

target_link_libraries(AIOServer PRIVATE Qt6::Widgets Qt6::WebEngineWidgets Qt6::Network GBAEmulator SwitchEmulator SDL2::SDL2 CURL::libcurl)

# Set autogen directory for AIOServer
set_target_properties(AIOServer PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/AIOServer"
)
