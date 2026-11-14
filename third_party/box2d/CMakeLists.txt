# rolo_box2d — Box2D 3.2.0: 2D rigid-body physics (engine/physics, ADR 0016).
#
# Box2D's own sources, unmodified, built with the settings its own CMake
# file uses: C17, no fused multiply-add (so a simulation gives the same
# result on every computer), and the 8-wide solver compiled for AVX2, which
# Box2D only uses when the processor has it (it checks at run time).

file(GLOB ROLO_BOX2D_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(rolo_box2d STATIC ${ROLO_BOX2D_SOURCES})
add_library(rolo::box2d ALIAS rolo_box2d)
# SYSTEM: not our headers, so our strict warnings do not apply to code including them.
target_include_directories(rolo_box2d SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(rolo_box2d PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(rolo_box2d PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(rolo_box2d PRIVATE /w)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X86)$")
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/contact_solver_w8.c"
            PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    endif()
    if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(rolo_box2d PRIVATE /clang:-ffp-contract=off)
    endif()
else()
    target_compile_options(rolo_box2d PRIVATE -w -ffp-contract=off)   # -w: its warnings are not ours to fix
endif()
if(EMSCRIPTEN)
    # WebAssembly's SIMD, through Emscripten's SSE2 headers (as Box2D's own
    # build does for the web; Axmol is built with -msimd128 too).
    target_compile_options(rolo_box2d PRIVATE -msimd128 -msse2)
endif()
if(NOT EMSCRIPTEN)   # (the web build has no threads)
    find_package(Threads REQUIRED)   # its optional task scheduler uses threads (Rolo runs it on one)
    target_link_libraries(rolo_box2d PUBLIC Threads::Threads)
endif()
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(rolo_box2d PUBLIC m)
endif()
