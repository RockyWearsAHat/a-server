# Emulator core and main app build
add_library(GBAEmulator STATIC
    ${PROJECT_ROOT}/src/emulator/gba/GBA.cpp
    ${PROJECT_ROOT}/src/emulator/gba/GBAMemory.cpp
    ${PROJECT_ROOT}/src/emulator/gba/ARM7TDMI.cpp
    ${PROJECT_ROOT}/src/emulator/gba/PPU.cpp
    ${PROJECT_ROOT}/src/emulator/gba/APU.cpp
    ${PROJECT_ROOT}/src/emulator/gba/GameDB.cpp
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
    ${PROJECT_ROOT}/src/gui/MainWindow.cpp
    ${PROJECT_ROOT}/src/gui/InputConfigDialog.cpp
    ${PROJECT_ROOT}/src/gui/LogViewerDialog.cpp
    ${PROJECT_ROOT}/src/input/InputManager.cpp
    # Headers with Q_OBJECT for MOC processing
    ${PROJECT_ROOT}/include/gui/MainWindow.h
    ${PROJECT_ROOT}/include/gui/InputConfigDialog.h
    ${PROJECT_ROOT}/include/gui/LogViewerDialog.h
    ${PROJECT_ROOT}/include/input/InputManager.h
)

target_include_directories(AIOServer PRIVATE 
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/gui
    ${SDL2_INCLUDE_DIRS}
    $<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(AIOServer PRIVATE Qt6::Widgets GBAEmulator SwitchEmulator SDL2::SDL2)

# Set autogen directory for AIOServer
set_target_properties(AIOServer PROPERTIES
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/AIOServer"
)
