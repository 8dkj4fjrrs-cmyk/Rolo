# rolo_runtime — window, main loop, rendering and the optional Rolo UI
# layer, built on Axmol.
#
# Axmol is linked PRIVATELY: code that uses rolo_runtime sees only
# include/rolo/runtime/*.hpp and Rolo's own libraries, never Axmol.

add_library(rolo_runtime STATIC
    src/Application.cpp
    src/AxmolLogBridge.cpp
    src/ButtonIconNodes.cpp
    src/RumbleTimer.cpp
    src/SceneRenderer.cpp
    src/TextSprites.cpp
    src/TextureStore.cpp)

add_library(rolo::runtime ALIAS rolo_runtime)

# Gamepads: through GLFW, or in a web browser the browser's own Gamepad API.
# On Android the app's Java side keeps them (RoloGamepads.java, ADR 0028).
if(EMSCRIPTEN)
    target_sources(rolo_runtime PRIVATE src/Gamepads_web.cpp)
elseif(ANDROID)
    target_sources(rolo_runtime PRIVATE src/Gamepads_android.cpp)
else()
    target_sources(rolo_runtime PRIVATE src/Gamepads.cpp)
endif()

# The UI layer's host needs a desktop window (GLFW). Android has none, and
# no editor: there src/UiHostNone.hpp stands in, and src/Android.cpp has the
# app's folder and its packed files (include/rolo/runtime/Android.hpp).
if(ANDROID)
    target_sources(rolo_runtime PRIVATE src/Android.cpp)
else()
    target_sources(rolo_runtime PRIVATE src/UiHost.cpp)
endif()

# Gamepad rumble: each system's own controller API (src/Rumble.hpp, ADR 0018).
if(WIN32)
    target_sources(rolo_runtime PRIVATE src/Rumble_windows.cpp)
elseif(APPLE)
    target_sources(rolo_runtime PRIVATE src/Rumble_apple.mm)
    set_source_files_properties(src/Rumble_apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(rolo_runtime PRIVATE "-framework GameController" "-weak_framework CoreHaptics")
elseif(ANDROID)
    target_sources(rolo_runtime PRIVATE src/Rumble_android.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(rolo_runtime PRIVATE src/Rumble_linux.cpp)
else()
    target_sources(rolo_runtime PRIVATE src/Rumble_none.cpp)
endif()

target_include_directories(rolo_runtime
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(rolo_runtime
    PUBLIC rolo::core rolo::scene rolo::assets rolo::gameplay rolo::ui
    PRIVATE ${_AX_CORE_LIB})

rolo_set_warnings(rolo_runtime)
