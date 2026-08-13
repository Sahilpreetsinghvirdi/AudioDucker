cmake_minimum_required(VERSION 3.20)
project(AudioDucker VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use a separate build directory, e.g. cmake -B build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AUDIO_DUCKER_BUILD_TESTS "Build the test suite" ON)

# Link the C runtime statically so AudioDucker.exe runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Platform / feature macros shared by every target
# ---------------------------------------------------------------------------
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

# ---------------------------------------------------------------------------
# Core library: pure logic + config + logging. No COM, no UI.
# Reused by both the GUI app, the native-messaging host and the tests.
# ---------------------------------------------------------------------------
add_library(audio_ducker_core STATIC
  src/common/Helpers.cpp
  src/common/IniFile.cpp
  src/common/JsonMini.cpp
  src/utils/Logger.cpp
  src/core/VolumeFader.cpp
  src/core/PlaybackDetector.cpp
  src/core/DuckingStateMachine.cpp
  src/config/ConfigManager.cpp
  src/detection/BrowserActivityDetector.cpp
)
target_include_directories(audio_ducker_core PUBLIC src)

# ---------------------------------------------------------------------------
# GUI tray application
# ---------------------------------------------------------------------------
# The icon and resource files are regenerated into the build directory: this
# keeps the source tree free of binary artifacts and works even when the
# sources live in a OneDrive-synced folder (which blocks new local files).
# The icon is a real build edge (OUTPUT + custom target + OBJECT_DEPENDS) so
# it is generated before the .rc is compiled under every generator (Ninja,
# Makefile and Visual Studio) - PRE_BUILD would only work on VS generators.
set(AUDIO_DUCKER_GEN "${CMAKE_BINARY_DIR}/generated")
configure_file(src/resources/app.rc ${AUDIO_DUCKER_GEN}/app.rc COPYONLY)
configure_file(src/resources/app.manifest ${AUDIO_DUCKER_GEN}/app.manifest COPYONLY)

add_custom_command(
  OUTPUT ${AUDIO_DUCKER_GEN}/app.ico
  COMMAND powershell -ExecutionPolicy Bypass -File
          ${CMAKE_SOURCE_DIR}/scripts/build_icon.ps1
          -OutFile ${AUDIO_DUCKER_GEN}/app.ico
  DEPENDS ${CMAKE_SOURCE_DIR}/scripts/build_icon.ps1
  COMMENT "Generating app.ico"
  VERBATIM)
add_custom_target(audio_ducker_icon DEPENDS ${AUDIO_DUCKER_GEN}/app.ico)

add_executable(AudioDucker WIN32
  src/main.cpp
  src/audio/AudioSession.cpp
  src/audio/AudioSessionManager.cpp
  src/audio/VolumeController.cpp
  src/detection/BrowserActivityDetector.cpp
  src/ducking/DuckingManager.cpp
  src/ui/TrayIcon.cpp
  src/ui/SettingsWindow.cpp
  ${AUDIO_DUCKER_GEN}/app.rc
)
# Make the .rc object depend on the generated icon so it exists before the
# resource compiler runs (OBJECT_DEPENDS is honored by Ninja/Makefile; the
# custom target dependency below covers the Visual Studio generator).
set_source_files_properties(${AUDIO_DUCKER_GEN}/app.rc PROPERTIES
  OBJECT_DEPENDS ${AUDIO_DUCKER_GEN}/app.ico)
add_dependencies(AudioDucker audio_ducker_icon)

target_link_libraries(AudioDucker PRIVATE audio_ducker_core
  ole32 oleaut32 gdiplus shell32 comctl32 advapi32 winmm)
# The .rc already embeds a full application manifest (common controls v6,
# PerMonitorV2 DPI); disable the linker's auto-generated one to avoid a
# duplicate MANIFEST resource.
target_link_options(AudioDucker PRIVATE /MANIFEST:NO)

# ---------------------------------------------------------------------------
# Native messaging host (helper process used by the browser extension)
# ---------------------------------------------------------------------------
add_executable(AudioDuckerHost WIN32
  src/nativehost_main.cpp
)
target_link_libraries(AudioDuckerHost PRIVATE audio_ducker_core
  user32 shell32)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AUDIO_DUCKER_BUILD_TESTS)
  enable_testing()
  add_executable(audio_ducker_tests
    tests/test_main.cpp
    tests/test_snapshot.cpp
    tests/test_fader.cpp
    tests/test_playback.cpp
    tests/test_detector.cpp
    tests/test_ducking.cpp
    tests/test_config.cpp
    tests/test_json.cpp
    src/audio/AudioSession.cpp
    src/audio/AudioSessionManager.cpp
    src/audio/VolumeController.cpp
    src/ducking/DuckingManager.cpp
  )
  target_link_libraries(audio_ducker_tests PRIVATE audio_ducker_core
    advapi32 shell32 user32 ole32 winmm)
  add_test(NAME audio_ducker_tests COMMAND audio_ducker_tests)
endif()

# Release build convenience: the output directories land next to the sources.
set_target_properties(AudioDucker AudioDuckerHost audio_ducker_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)
