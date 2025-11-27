# Create a library for the emulator core
add_library(GBAEmulator STATIC
    emulator/gba/GBA.cpp
    emulator/gba/GBAMemory.cpp
    emulator/gba/ARM7TDMI.cpp
    emulator/gba/PPU.cpp
)

target_include_directories(GBAEmulator PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

add_executable(AIOServer 
    main.cpp
    gui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/include/gui/MainWindow.h
)

target_include_directories(AIOServer PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(AIOServer PRIVATE Qt6::Widgets GBAEmulator)

# We will add Qt and other dependencies here later
