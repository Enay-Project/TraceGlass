cmake_minimum_required(VERSION 3.20)

project(TraceGlass VERSION 0.1.0 LANGUAGES C RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "TraceGlass supports x64 Windows toolchains only.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

configure_file(
    include/version.h.in
    generated/version.h
    @ONLY
)
configure_file(
    resources/traceglass.manifest.in
    generated/traceglass.manifest
    @ONLY
)
configure_file(
    resources/resource.rc.in
    generated/resource.rc
    @ONLY
)

add_executable(TraceGlass WIN32
    src/main.c
    src/gui.c
    src/telemetry.c
    src/etw_monitor.c
    src/process_monitor.c
    src/network_monitor.c
    src/event_queue.c
    src/event_store.c
    src/events.c
    src/detection.c
    src/export.c
    src/logger.c
    src/utils.c
    "${CMAKE_CURRENT_BINARY_DIR}/generated/resource.rc"
)

target_include_directories(TraceGlass PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_definitions(TraceGlass PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(TraceGlass PRIVATE /W4 /utf-8)
    target_link_options(TraceGlass PRIVATE /MANIFEST:NO)
else()
    target_compile_options(TraceGlass PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(TraceGlass PRIVATE -municode)
endif()

target_link_libraries(TraceGlass PRIVATE
    advapi32
    comctl32
    comdlg32
    iphlpapi
    shell32
    tdh
    uxtheme
    ws2_32
)

set_target_properties(TraceGlass PROPERTIES
    OUTPUT_NAME "TraceGlass"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:TraceGlass>"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(TraceGlassExportSmoke
        tests/export_smoke.c
        src/export.c
        src/event_store.c
        src/events.c
        src/utils.c
    )
    target_include_directories(TraceGlassExportSmoke PRIVATE
        include
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_compile_definitions(TraceGlassExportSmoke PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(TraceGlassExportSmoke PRIVATE /W4 /utf-8)
    else()
        target_compile_options(TraceGlassExportSmoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(TraceGlassExportSmoke PRIVATE comdlg32)
    add_test(NAME export_formats COMMAND TraceGlassExportSmoke)
endif()
