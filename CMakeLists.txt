cmake_minimum_required(VERSION 3.28)

# The release workflow bumps this version through scripts/version_updater.py. 0.0.0 marks an unreleased tree.
project(AcerFan VERSION 0.0.0 DESCRIPTION "Temperature-aware fan control tray app for Acer laptops" LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "AcerFan builds with MSVC: it uses the WMI and Task Scheduler COM headers of the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the released exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(ACERFAN_BUILD_TESTS "Build the unit tests" ON)

# The logic without fan hardware access, shared by the app and the tests.
add_library(acerfan_core STATIC
    src/config.cpp
    src/curve.cpp
    src/fan_controller.cpp
    src/text.cpp
)
target_include_directories(acerfan_core PUBLIC src)
target_compile_options(acerfan_core PUBLIC /W4 /permissive- /utf-8 /EHsc)
target_compile_definitions(acerfan_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

configure_file(res/AcerFan.rc.in ${CMAKE_CURRENT_BINARY_DIR}/AcerFan.rc @ONLY)

add_executable(AcerFan WIN32
    src/main.cpp
    src/app.cpp
    src/acer_wmi.cpp
    src/autostart.cpp
    src/fan_service.cpp
    src/logger.cpp
    src/tray_icon.cpp
    res/AcerFan.manifest
    ${CMAKE_CURRENT_BINARY_DIR}/AcerFan.rc
)
# res/AcerFan.manifest carries the UAC level. The default asInvoker entry of the linker conflicts with it.
target_link_options(AcerFan PRIVATE /MANIFESTUAC:NO)
target_compile_definitions(AcerFan PRIVATE ACERFAN_VERSION="${PROJECT_VERSION}")
target_link_libraries(AcerFan PRIVATE
    acerfan_core
    wbemuuid
    taskschd
    secur32
    ole32
    oleaut32
    shell32
    user32
    gdi32
)

if(ACERFAN_BUILD_TESTS)
    enable_testing()
    foreach(suite IN ITEMS controller config)
        add_executable(acerfan_test_${suite} tests/test_${suite}.cpp)
        target_link_libraries(acerfan_test_${suite} PRIVATE acerfan_core)
        add_test(NAME ${suite} COMMAND acerfan_test_${suite})
    endforeach()
endif()
