# rolo_gameplay — running a game: behaviours (C++ gameplay code attached
# to objects), input, collision queries, scene changes, and the RPG content
# in scenes (triggers, the player, the dialogue box, save slots).
#
# No backend and no UI; always built and unit-tested (also in `core`).
# Game modules (cmake/RoloGameModules.cmake) build on this library.

add_library(rolo_gameplay STATIC
    src/Collision.cpp
    src/BuiltinBehaviours.cpp
    src/Components.cpp
    src/SaveStore.cpp
    src/Game.cpp
    src/GameModule.cpp
    src/Input.cpp
    src/PhysicsSync.cpp
    src/ScriptApi.cpp
    src/ScriptHost.cpp)
add_library(rolo::gameplay ALIAS rolo_gameplay)

target_include_directories(rolo_gameplay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rolo_gameplay
    PUBLIC rolo::core rolo::scene rolo::assets rolo::content rolo::physics
    PRIVATE rolo::lua)   # Lua scripts (Script.hpp); scripts never see Lua's C API
rolo_set_warnings(rolo_gameplay)
