# rolo_stress: plays the stress-test scenes (examples/stress) without a
# window and measures how long each frame of gameplay takes: scripts,
# physics and the rest. The baseline for changes to scripting (ADR 0019).

add_executable(rolo_stress main.cpp)
target_link_libraries(rolo_stress PRIVATE rolo::gameplay)
target_compile_definitions(rolo_stress PRIVATE
    ROLO_STRESS_PROJECT="${PROJECT_SOURCE_DIR}/examples/stress/project")
set_target_properties(rolo_stress PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
