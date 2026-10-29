# amped3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(amped3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(AMPED3_SOURCES
    src/main.cpp
    src/scripted_input.cpp
    src/hang_watch.cpp
    src/render_shutdown_fix.cpp
    src/frame_stats.cpp
    src/clock_probe.cpp
    src/step_probe.cpp
    src/rider_probe.cpp
    src/cpu_profile.cpp
    src/voice_probe.cpp
    src/frame_rate.cpp
    src/frame_graph.cpp
    src/music_ramp_fix.cpp
    src/pix_capture.cpp
    src/watch_probe.cpp
    src/cloth_probe.cpp
)

if(WIN32)
    add_executable(amped3 WIN32 ${AMPED3_SOURCES})
else()
    add_executable(amped3 ${AMPED3_SOURCES})
endif()

rexglue_setup_target(amped3)
if(WIN32)
    target_link_libraries(amped3 PRIVATE d3d11 dxgi shell32 psapi)  # frame graph, window title, cpu profile
    # Function addresses for tools/cpu_profile.py (the exe has no PDB).
    target_link_options(amped3 PRIVATE "LINKER:/MAP:${CMAKE_BINARY_DIR}/amped3.map")
endif()

# Patched SDK runtime and GPU DLLs (built by tools\build_sdk_ffx.cmd into sdk-bin/).
# rexglue_setup_target's POST_BUILD step copies the stock prebuilt rexruntime.dll
# next to the exe on every link; this later step puts ours back over it.
set(AMPED3_SDK_BIN "${CMAKE_CURRENT_SOURCE_DIR}/sdk-bin")
if(WIN32 AND EXISTS "${AMPED3_SDK_BIN}/rexruntime.dll")
    add_custom_command(TARGET amped3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AMPED3_SDK_BIN}/rexruntime.dll" "${AMPED3_SDK_BIN}/rexgpu-xenos.dll"
            "${AMPED3_SDK_BIN}/amd_fidelityfx_dx12.dll" "$<TARGET_FILE_DIR:amped3>"
        VERBATIM)
endif()

# Keep x64 FP exceptions masked (stock SDK v0.10.0 can unmask them through MXCSR
# writes, crashing with STATUS_FLOAT_INEXACT_RESULT). Header overrides from
# Table-Tennis-Rockstar-PC (github.com/davidptrssvea/Table-Tennis-Rockstar-PC,
# MIT); the installed SDK stays unchanged.
target_include_directories(amped3 BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sdk-overrides")
if(TARGET amped3_recomp)
    target_include_directories(amped3_recomp BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sdk-overrides")
endif()

# Patched copy of the SDK application source (pauses game input while a system
# dialog is open); the installed SDK stays unchanged.
get_target_property(AMPED3_HOST_SOURCES amped3 SOURCES)
set(AMPED3_SDK_APP "${REXGLUE_SHARE_DIR}/rex_app.cpp")
if(NOT AMPED3_SDK_APP IN_LIST AMPED3_HOST_SOURCES)
    message(FATAL_ERROR "SDK application source not found; cannot apply rex_app.cpp override")
endif()
list(REMOVE_ITEM AMPED3_HOST_SOURCES "${AMPED3_SDK_APP}")
set_property(TARGET amped3 PROPERTY SOURCES "${AMPED3_HOST_SOURCES}")
target_sources(amped3 PRIVATE sdk-overrides/rex_app.cpp)

# Profile-guided optimization of the recompiled game code (tools\build_pgo.cmd):
# AMPED3_PGO=generate builds an instrumented exe (counters written by the
# scripted "pgodump" command); AMPED3_PGO=<file.profdata> uses a merged profile.
set(AMPED3_PGO "" CACHE STRING "generate, or a .profdata file to optimize with")
if(AMPED3_PGO STREQUAL "generate")
    foreach(target amped3 amped3_recomp)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -fprofile-instr-generate)
        endif()
    endforeach()
    target_compile_definitions(amped3 PRIVATE AMPED3_PGO_GEN)
    target_link_options(amped3 PRIVATE -fprofile-instr-generate)
elseif(AMPED3_PGO)
    foreach(target amped3 amped3_recomp)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE "-fprofile-instr-use=${AMPED3_PGO}"
                -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled
                -Wno-profile-instr-missing)
        endif()
    endforeach()
endif()
