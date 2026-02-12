# Azahar (3DS Emulator)

Azahar is an open-source 3DS emulator project based on Citra. It was created from the merging of PabloMK7's Citra fork and the Lime3DS project.

## Project Overview

- **Main Technologies:** C++20, CMake, Qt6 (Frontend), SDL2 (Frontend/Input), Vulkan/OpenGL (Rendering), Boost.
- **Architecture:** 
  - `src/common`: Common utility functions, logging, and data structures.
  - `src/core`: Core emulation logic (HLE, kernel, memory management).
  - `src/video_core`: Graphics emulation (OpenGL/Vulkan/Software renderers).
  - `src/audio_core`: Audio emulation (HLE/LLE).
  - `src/citra_qt`: Main Qt-based graphical user interface.
  - `src/citra_sdl`: SDL2-based command-line/simple interface.
  - `src/android`: Android-specific frontend and JNI bindings.
  - `externals`: Third-party dependencies managed via git submodules.

## Building and Running

### Prerequisites

- **CMake** (>= 3.25)
- **Ninja** (recommended) or Make
- **C++20 Compiler** (Clang 15+, GCC 12+, or MSVC 2022+)
- **Qt6** (Widgets, Multimedia, Concurrent)
- **SDL2**
- **Vulkan SDK** (for Vulkan support)

### Initial Setup

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/azahar-emu/azahar.git
cd azahar

# If already cloned without submodules:
git submodule update --init --recursive
```

### Build Commands

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

### Running Tests

```bash
cd build
ctest -VV -C Release
# Or run the tests executable directly:
./bin/Release/tests
```

## Development Conventions

- **Language Standard:** C++20 is strictly required.
- **Code Style:** Uses `clang-format` (version 15). A custom target `ninja clang-format` is available.
- **Pre-commit Hooks:** Automatically installed to `.git/hooks/pre-commit` during CMake configuration. These hooks run formatting checks.
- **Warnings:** Strict warning levels are enabled. `CITRA_WARNINGS_AS_ERRORS` is ON by default.
- **Git:** Never merge `master` into your feature branch repeatedly; maintainers will handle updates when appropriate.
- **Testing:** New features and bug fixes should include tests using the **Catch2** framework in `src/tests`.
- **Precompiled Headers:** Used by default (`CITRA_USE_PRECOMPILED_HEADERS`) to speed up build times.
- **LTO:** Link Time Optimization is enabled by default for Release builds on non-MSVC compilers.

## Key Files

- `CMakeLists.txt`: Root build configuration.
- `src/CMakeLists.txt`: Source directory organization and compiler flags.
- `CONTRIBUTING.md`: Links to the wiki for contribution guidelines.
- `README.md`: General project information and installation instructions.
- `.ci/`: Scripts used for Continuous Integration on various platforms.
