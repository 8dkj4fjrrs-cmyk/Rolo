# rolo_audio — plays sounds: the AudioEngine that the player and the editor
# give to games as their SoundPlayer (rolo/gameplay/Sound.hpp), built on
# miniaudio (ADR 0015).
#
# No Axmol and no window, so it is built and tested in every preset; tests
# use it without a sound card and check the sound it mixes.

add_library(rolo_audio STATIC src/AudioEngine.cpp)
add_library(rolo::audio ALIAS rolo_audio)

target_include_directories(rolo_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rolo_audio
    PUBLIC rolo::core rolo::assets rolo::gameplay
    PRIVATE rolo::miniaudio)   # users see only AudioEngine.hpp, never miniaudio
rolo_set_warnings(rolo_audio)
