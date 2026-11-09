# rolo_physics — 2D rigid-body physics in Rolo's units (points, degrees),
# built on Box2D (ADR 0016). Box2D's headers stay inside src/: code using
# rolo_physics sees only PhysicsWorld.hpp.
#
# No backend and no window: built and tested in every preset.

add_library(rolo_physics STATIC src/PhysicsWorld.cpp)
add_library(rolo::physics ALIAS rolo_physics)

target_include_directories(rolo_physics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rolo_physics
    PUBLIC rolo::scene
    PRIVATE rolo::box2d)
rolo_set_warnings(rolo_physics)
