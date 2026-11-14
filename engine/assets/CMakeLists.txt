# rolo_assets — the files a project's scenes use (images, tilesets, sounds,
# fonts): stable asset IDs in .meta files, import, checks, tilesets.
#
# No backend and no UI; always built and unit-tested (also in `core`).

add_library(rolo_assets STATIC
    src/Animation.cpp
    src/AssetDatabase.cpp
    src/GamePack.cpp
    src/Tileset.cpp)
add_library(rolo::assets ALIAS rolo_assets)

target_include_directories(rolo_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(rolo_assets SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/stb")
target_link_libraries(rolo_assets
    PUBLIC rolo::scene
    PRIVATE nlohmann_json::nlohmann_json)
rolo_set_warnings(rolo_assets)

# See engine/scene/CMakeLists.txt: nlohmann/json needs char8_t in C++20 mode.
if(MSVC)
    target_compile_options(rolo_assets PRIVATE /Zc:char8_t)
else()
    target_compile_options(rolo_assets PRIVATE -fchar8_t)
endif()
