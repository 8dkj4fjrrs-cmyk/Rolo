# rolo_scene — scenes, entities, components and the files they are saved in.
#
# No backend and no UI: scenes are plain data that the runtime draws and the
# editor edits. Always built and unit-tested (also in the `core` preset).

include(FetchContent)

# nlohmann/json reads and writes scene and project files. Pinned release
# archive, checked by its SHA-256 (see THIRD_PARTY_NOTICES.md).
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_library(rolo_scene STATIC
    src/Component.cpp
    src/Project.cpp
    src/ScreenFit.cpp
    src/Scene.cpp
    src/Tilemap.cpp
    src/SceneFile.cpp
    src/Types.cpp
    src/Values.cpp)
add_library(rolo::scene ALIAS rolo_scene)

target_include_directories(rolo_scene PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rolo_scene
    PUBLIC rolo::core
    PRIVATE nlohmann_json::nlohmann_json)   # JSON stays an implementation detail
rolo_set_warnings(rolo_scene)

# Axmol turns off C++20's char8_t for the whole build (-fno-char8_t,
# /Zc:char8_t-), but nlohmann/json 3.12 relies on it in C++20 mode. JSON is
# private to this library and its public headers use no char8_t, so turning
# it back on here is safe.
if(MSVC)
    target_compile_options(rolo_scene PRIVATE /Zc:char8_t)
else()
    target_compile_options(rolo_scene PRIVATE -fchar8_t)
endif()
