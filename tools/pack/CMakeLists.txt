# rolo_pack: packs a project folder into a game pack (docs/formats/game-pack.md)
# from the command line. The editor packs games itself when it exports them;
# this is for build scripts, such as the Android build (docs/building.md,
# "For Android"), which puts one pack into the app.

add_executable(rolo_pack main.cpp)
target_link_libraries(rolo_pack PRIVATE rolo::assets rolo::scene)
rolo_set_warnings(rolo_pack)
set_target_properties(rolo_pack PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
