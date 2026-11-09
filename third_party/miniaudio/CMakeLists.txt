# rolo_miniaudio — miniaudio 0.11.26 and stb_vorbis 1.22: playing sounds
# (engine/audio, ADR 0015).
#
# miniaudio talks to each system's sound output itself (Core Audio on macOS,
# WASAPI on Windows, PulseAudio or ALSA on Linux), loading it when the
# program starts rather than linking it, so nothing extra is needed to build
# or run. It decodes WAV, MP3 and FLAC; stb_vorbis adds OGG.

add_library(rolo_miniaudio STATIC miniaudio.c)
add_library(rolo::miniaudio ALIAS rolo_miniaudio)
# SYSTEM: not our headers, so our strict warnings do not apply.
target_include_directories(rolo_miniaudio SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(rolo_miniaudio PUBLIC
    MA_NO_ENCODING        # Rolo only plays sounds; it never writes sound files
    MA_NO_GENERATION)     # ...or makes tones and noise
set_target_properties(rolo_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(rolo_miniaudio PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rolo_miniaudio PRIVATE /w)
else()
    target_compile_options(rolo_miniaudio PRIVATE -w)   # third-party code: its warnings are not ours to fix
endif()
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(rolo_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
