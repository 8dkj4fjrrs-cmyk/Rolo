# rolo_runtime — window, main loop, rendering and the optional Rolo UI
# layer, built on Axmol.
#
# Axmol is linked PRIVATELY: code that uses rolo_runtime sees only
# include/rolo/runtime/*.hpp and Rolo's own libraries, never Axmol.

add_library(rolo_runtime STATIC
    src/Application.cpp
    src/AxmolLogBridge.cpp
    src/Gamepads.cpp
    src/SceneRenderer.cpp
    src/TextureStore.cpp
    src/UiHost.cpp)

add_library(rolo::runtime ALIAS rolo_runtime)

target_include_directories(rolo_runtime
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(rolo_runtime
    PUBLIC rolo::core rolo::scene rolo::assets rolo::gameplay rolo::ui
    PRIVATE ${_AX_CORE_LIB})

rolo_set_warnings(rolo_runtime)
