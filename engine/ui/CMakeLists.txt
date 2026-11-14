# rolo_ui — Rolo's own user-interface toolkit.
#
# Pure C++ with no window, GPU or backend code: widgets produce a DrawList
# (triangles + textures) and consume Rolo input events. rolo_runtime
# connects it to the real window. Because of that, everything here is
# unit-tested without a display, in every build preset.
#
# Third-party pieces (kept separate, see THIRD_PARTY_NOTICES.md):
#   third_party/stb/stb_truetype.h  font rasteriser (public domain / MIT)
#   assets/fonts/Inter-*.ttf         UI font (SIL Open Font License 1.1)

include(RoloEmbed)

add_library(rolo_ui STATIC
    src/Context.cpp
    src/DockLayout.cpp
    src/DrawList.cpp
    src/EmbeddedFonts.cpp
    src/Font.cpp
    src/Icons.cpp
    src/Input.cpp
    src/StbTrueTypeImpl.cpp
    src/TextImage.cpp
    src/Utf8.cpp
    src/Widget.cpp
    src/widgets/Basic.cpp
    src/widgets/Box.cpp
    src/widgets/Dialog.cpp
    src/widgets/DockSpace.cpp
    src/widgets/Fields.cpp
    src/widgets/Inspector.cpp
    src/widgets/Menu.cpp
    src/widgets/Scroll.cpp
    src/widgets/TextField.cpp
    src/widgets/Tree.cpp)

add_library(rolo::ui ALIAS rolo_ui)

rolo_embed_files(rolo_ui
    NAMESPACE rolo::ui::embedded
    FILES
        inter_regular=${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        inter_semibold=${PROJECT_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf)

target_include_directories(rolo_ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(rolo_ui SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/stb")
target_compile_features(rolo_ui PUBLIC cxx_std_20)
target_link_libraries(rolo_ui PUBLIC rolo::core)   # text <-> number helpers
rolo_set_warnings(rolo_ui)

# Third-party and generated files are not held to our warning rules.
set_source_files_properties(src/StbTrueTypeImpl.cpp PROPERTIES COMPILE_OPTIONS "-w")
if(MSVC)
    set_source_files_properties(src/StbTrueTypeImpl.cpp PROPERTIES COMPILE_OPTIONS "/w")
endif()
