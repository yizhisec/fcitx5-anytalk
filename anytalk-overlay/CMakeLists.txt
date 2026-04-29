cmake_minimum_required(VERSION 3.16)

if(NOT PROJECT_NAME)
    project(anytalk-overlay LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent WebSockets)
find_package(LayerShellQt QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)
# libpulse (async API) is needed for PulseSourceProbe — pa_threaded_mainloop /
# pa_context — to detect Bluetooth default sources before opening the capture stream.
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

add_executable(anytalk-overlay
    src/main.cpp
    src/Theme.h
    src/Config.h
    src/Config.cpp
    src/AsrController.h
    src/AsrController.cpp
    src/OverlayService.h
    src/OverlayService.cpp
    src/OverlayWindow.h
    src/OverlayWindow.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/AuroraBars.h
    src/AuroraBars.cpp
    src/StatusDot.h
    src/StatusDot.cpp
    src/audio/AudioCapture.h
    src/audio/AudioCapture.cpp
    src/audio/PulseSourceProbe.h
    src/audio/PulseSourceProbe.cpp
    src/asr/AsrBackend.h
    src/asr/AsrBackendFactory.h
    src/asr/AsrBackendFactory.cpp
    src/asr/VolcengineProtocol.h
    src/asr/VolcengineProtocol.cpp
    src/asr/VolcengineBackend.h
    src/asr/VolcengineBackend.cpp
)

target_include_directories(anytalk-overlay PRIVATE src)

target_link_libraries(anytalk-overlay PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Concurrent
    Qt6::WebSockets
    PkgConfig::PULSE_SIMPLE
    PkgConfig::PULSE
)

if(LayerShellQt_FOUND)
    target_link_libraries(anytalk-overlay PRIVATE LayerShellQtInterface)
    message(STATUS "anytalk-overlay: LayerShellQt found — Wayland native centering enabled")
else()
    message(STATUS "anytalk-overlay: LayerShellQt not found — Wayland will use compositor placement")
endif()

include(GNUInstallDirs)
install(TARGETS anytalk-overlay DESTINATION ${CMAKE_INSTALL_BINDIR})
