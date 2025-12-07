# Create a library for the emulator core
add_library(GBAEmulator STATIC
    emulator/gba/GBA.cpp
    emulator/gba/GBAMemory.cpp
    emulator/gba/ARM7TDMI.cpp
    emulator/gba/PPU.cpp
    emulator/gba/APU.cpp
    emulator/gba/GameDB.cpp
)

target_include_directories(GBAEmulator PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Create a library for the Switch emulator core
add_library(SwitchEmulator STATIC
    emulator/switch/SwitchEmulator.cpp
    emulator/switch/MemoryManager.cpp
    emulator/switch/CpuCore.cpp
    emulator/switch/GpuCore.cpp
    emulator/switch/ServiceManager.cpp
)

target_include_directories(SwitchEmulator PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

add_executable(AIOServer 
    main.cpp
    gui/MainWindow.cpp
    gui/InputConfigDialog.cpp
    input/InputManager.cpp
    ${CMAKE_SOURCE_DIR}/include/gui/MainWindow.h
    ${CMAKE_SOURCE_DIR}/include/gui/InputConfigDialog.h
    ${CMAKE_SOURCE_DIR}/include/input/InputManager.h
)

target_include_directories(AIOServer PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(AIOServer PRIVATE Qt6::Widgets GBAEmulator SwitchEmulator SDL2::SDL2)

# We will add Qt and other dependencies here later
