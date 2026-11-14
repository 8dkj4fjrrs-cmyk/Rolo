# rolo_core — the lowest engine layer.
#
# Rules for this module:
#   * No editor code, no UI code, no Cocos types in public headers.
#   * Everything else in the engine may depend on it; it depends on nothing
#     except the C++ standard library.

configure_file(
    include/rolo/core/Version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rolo/core/Version.hpp"
    @ONLY)

add_library(rolo_core STATIC
    src/Engine.cpp
    src/Files.cpp
    src/Names.cpp
    src/Numbers.cpp
    src/Log.cpp
    src/Version.cpp)

add_library(rolo::core ALIAS rolo_core)

target_include_directories(rolo_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_features(rolo_core PUBLIC cxx_std_20)
rolo_set_warnings(rolo_core)

# Android: the console sink writes to the system's log, "logcat" (src/Log.cpp).
if(ANDROID)
    target_link_libraries(rolo_core PUBLIC log)
endif()
