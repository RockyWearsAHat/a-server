This project is built with cmake. Ensure working directory is `/Users/alexwaldmann/Desktop/AIO Server` before running build, always use cmake. The root directory of this project is `/Users/alexwaldmann/Desktop/AIO Server` ensure that this is always used. Terminals should always stem from the root directory, please be careful to ensure this. Check PWD if things aren't working as expected.

In all emulators, region locks should be bypassed.

Always use cmake to build this project, do not use clang++, do not use g++, do not use any other build system other than cmake.

I am creating a full server setup. This is a comprehensive application that is designed to store files on a network server, have an interface/GUI, preferably controllable with mouse and keyboard, but also controllers that are paired up to the machine running this server. This server, along with being an encrypted NAS server, should also be able to display media on connected displays, and be able to run applications/games that are stored on the server itself (with the accompanying emulator). Each emulator should be a direct recreation of the original hardware, with test cases written to ensure 100% accuracy. In addition, these emulators should be able to run whatever game in whatever resolution (maintaining aspect ratio) at whatever framerate the user desires, with options for graphical enhancement. The server should also have the ability to stream media and games to connected clients over the network with minimal latency. The server should support multiple users with individual profiles, allowing them to customize their experience and access their own files and settings. Security is paramount, so the server must implement robust encryption methods for data storage and transmission. Additionally, the server should have a modular architecture, allowing for easy updates and the addition of new features or emulators in the future. The user interface should be intuitive and user-friendly, providing easy access to all functionalities of the server.

This server will be running on a Windows operating system, but should also be able to be compiled and tested on a MacOS and absolutley be able to be compiled and ran on Linux (this is likely where the server will live in it's final state). Essentially I want this to be almost like an operating system for a home entertainment setup with some NAS capabilities built in.

Use C++ for efficiency and performance, along with appropriate libraries and frameworks for GUI development, networking, encryption, and emulation. Consider using Qt or wxWidgets for the GUI, OpenSSL for encryption, and heavily consulting documentation for emulator development.

Ensure good documentation throughout the development process including in code documentation and comments, good reuse of code when necessary, etc.

Use the space below this statement to break down the project into smaller, manageable components and outline the steps needed to achieve the final goal. Think of this as a notecard for current development

## Project Roadmap & Breakdown

### Phase 1: Foundation & Architecture

- [ ] **Project Scaffolding**: Set up directory structure (`src`, `include`, `tests`, `docs`, `libs`).
- [ ] **Build System**: Configure `CMake` for cross-platform support (Windows, macOS, Linux).
- [ ] **Dependency Management**: Set up `vcpkg` or `Conan` for managing libraries (Qt, OpenSSL, Boost, etc.).
- [ ] **Core Utilities**: Implement logging, configuration loading (JSON/YAML), and error handling.
- [ ] **Modular Plugin System**: Design the interface for loadable modules (Emulators, Media Players).

### Phase 2: User Interface (GUI) & Input

- [ ] **Qt Integration**: Initialize the main Qt application window.
- [ ] **Dashboard Design**: Create the "10-foot user interface" suitable for TV displays.
- [ ] **Input Manager**: Abstract input handling for Mouse/Keyboard and Game Controllers (SDL2 or Qt Gamepad).
- [ ] **User Profiles**: Login screen and profile switching mechanism.

### Phase 3: NAS & Security (The "Server" Core)

- [ ] **Encryption Layer**: Implement AES-256 encryption for file storage using OpenSSL.
- [ ] **File System Manager**: Virtual file system for managing user data and media libraries.
- [ ] **Network Server**: Set up a secure TCP/UDP server for client connections.
- [ ] **Authentication**: Secure handshake and session management.

### Phase 4: Emulation Engine (Focus: Game Boy Advance)

- [ ] **Emulator Interface**: Define the abstract base class for emulators (`IEmulator`).
- [ ] **GBA Core Architecture**:
  - [ ] **CPU**: ARM7TDMI implementation (ARM & Thumb instruction sets).
  - [ ] **Memory**: Memory Map implementation (WRAM, IWRAM, VRAM, ROM, IO).
  - [ ] **PPU**: Pixel Processing Unit (Modes 0-5, Sprites, Affine Backgrounds).
  - [ ] **APU**: Audio Processing Unit (DirectSound + GBC legacy channels).
  - [ ] **DMA & Interrupts**: Direct Memory Access and Interrupt Controller.
- [ ] **Accuracy Verification**: Test against "Super Mario Advance" and test suites (e.g., armwrestler).
- [ ] **Rendering Pipeline**: OpenGL/Vulkan integration for rendering game output.
- [ ] **Save States & Rewind**: Implement state serialization.

### Phase 5: Media & Streaming

- [ ] **Streaming Protocol**: Implement WebRTC or low-latency UDP for universal, real-time streaming.
- [ ] **Media Player**: Integrate `QtMultimedia` or `FFmpeg` for video/audio playback.
- [ ] **Streaming Server**: Implement low-latency video encoding for streaming to clients.
- [ ] **Library Management**: Metadata scraping and organization for games and movies.

### Phase 6: Testing & Polish

- [ ] **Unit Testing**: Comprehensive tests for crypto, emulation cores, and logic (Google Test).
- [ ] **CI/CD**: Set up GitHub Actions for cross-platform builds.
- [ ] **Documentation**: Generate Doxygen documentation.
