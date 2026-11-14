# Installing and packaging Rolo: what someone downloads.
#
#   cmake --build --preset release --target rolo_package
#
# makes dist/Rolo-<version>-<system>/ and a .zip of it. The layout is what
# the editor expects to find beside itself (editor/model/RoloResources.hpp):
#
#   Linux, Windows                         macOS
#   Rolo-<v>-<system>/                  Rolo-<v>-macOS/
#     rolo_editor(.exe), axslc/           rolo_editor.app/  (Contents/Resources/rolo/...)
#     runtime/rolo_player(.exe), axslc/     ... runtime/rolo_player.app
#     web-runtime/ (index.html, rolo_player.js, .wasm, .data: for web exports)
#     templates/ examples/ licenses/ docs/   README.md, LICENSE, ...
#     README.md LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md
#
# Every rule belongs to the "rolo" component: libraries bundled by Axmol
# have install rules of their own (headers, static libraries), and none of
# those belong in the download.

if(NOT TARGET rolo_editor OR NOT TARGET rolo_player)
    return()
endif()

set(_component COMPONENT rolo)
if(APPLE)
    set(_resources "rolo_editor.app/Contents/Resources/rolo")
    install(TARGETS rolo_editor BUNDLE DESTINATION . ${_component})
    install(TARGETS rolo_player BUNDLE DESTINATION "${_resources}/runtime" ${_component})
else()
    set(_resources ".")
    install(TARGETS rolo_editor RUNTIME DESTINATION . ${_component})
    install(TARGETS rolo_player RUNTIME DESTINATION runtime ${_component})
    # The compiled shaders both programs load from beside themselves.
    install(DIRECTORY "${AXSLCC_OUT_DIR}/" DESTINATION axslc ${_component})
    install(DIRECTORY "${AXSLCC_OUT_DIR}/" DESTINATION runtime/axslc ${_component})
    if(WIN32)
        # The DLLs the programs link (Axmol's prebuilt zlib1.dll, for one).
        # A Windows program only finds DLLs beside it, so both folders get them;
        # exported games copy runtime/ (tools/release/check_windows_dlls.py
        # checks the package).
        install(FILES $<TARGET_RUNTIME_DLLS:rolo_editor> DESTINATION . ${_component})
        install(FILES $<TARGET_RUNTIME_DLLS:rolo_player> DESTINATION runtime ${_component})
        # The Visual C++ runtime (msvcp140.dll, vcruntime140*.dll): not every
        # PC has it installed, and Microsoft allows shipping it beside a program.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)   # the Universal CRT is part of Windows 10 and later
        include(InstallRequiredSystemLibraries)
        if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION . ${_component})
            install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION runtime ${_component})
        endif()
        # DLLs Axmol puts beside its programs (e.g. ANGLE's OpenGL ES on Windows).
        # The build folder also has an "axslc" link to the shaders there;
        # it is left out, since the real shaders are installed above (CMake
        # refuses to install a link over that folder).
        install(DIRECTORY "$<TARGET_FILE_DIR:rolo_editor>/" DESTINATION . ${_component}
                FILES_MATCHING PATTERN "*.dll" PATTERN "axslc" EXCLUDE)
        install(DIRECTORY "$<TARGET_FILE_DIR:rolo_player>/" DESTINATION runtime ${_component}
                FILES_MATCHING PATTERN "*.dll" PATTERN "axslc" EXCLUDE)
    endif()
endif()

# Templates, examples (projects and their C++ modules, to learn from),
# licenses and documentation.
install(DIRECTORY templates/ DESTINATION "${_resources}/templates" ${_component}
        PATTERN "__pycache__" EXCLUDE)
install(DIRECTORY examples/ DESTINATION "${_resources}/examples" ${_component}
        PATTERN "__pycache__" EXCLUDE)
install(DIRECTORY licenses/ DESTINATION "${_resources}/licenses" ${_component})
install(DIRECTORY docs/ DESTINATION "${_resources}/docs" ${_component})
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md DESTINATION . ${_component})

# The web runtime, for exporting games for browsers. It is built separately,
# with Emscripten (the "web" preset; docs/building.md), and taken from where
# that build puts it. A package made without it cannot export for the web
# (the editor says so).
set(ROLO_WEB_RUNTIME_DIR "${PROJECT_SOURCE_DIR}/build/web/bin/rolo_player" CACHE PATH
    "The built web runtime (index.html, rolo_player.js, .wasm, .data) to put in the package")
if(EXISTS "${ROLO_WEB_RUNTIME_DIR}/rolo_player.wasm")
    install(FILES "${ROLO_WEB_RUNTIME_DIR}/index.html" "${ROLO_WEB_RUNTIME_DIR}/rolo_player.js"
                  "${ROLO_WEB_RUNTIME_DIR}/rolo_player.wasm" "${ROLO_WEB_RUNTIME_DIR}/rolo_player.data"
            DESTINATION "${_resources}/web-runtime" ${_component})
    message(STATUS "Package: with the web runtime from ${ROLO_WEB_RUNTIME_DIR}")
else()
    message(STATUS "Package: WITHOUT the web runtime (none at ${ROLO_WEB_RUNTIME_DIR}); it will not export for the web")
endif()

# macOS: sign the apps "ad hoc", after everything is inside them. Apple
# silicon Macs call a downloaded app with no valid signature "damaged" and
# refuse to open it. An ad-hoc signature is valid without an Apple developer
# account; users then right-click > Open once ("unidentified developer").
# The linker signs the programs, but that signature does not cover the
# bundle's resources added here, so it would not count. Inner app first.
if(APPLE)
    install(CODE [[
        foreach(_app "rolo_editor.app/Contents/Resources/rolo/runtime/rolo_player.app" "rolo_editor.app")
            execute_process(COMMAND /usr/bin/codesign --force --deep --sign - "${CMAKE_INSTALL_PREFIX}/${_app}"
                            RESULT_VARIABLE _signed)
            if(NOT _signed EQUAL 0)
                message(FATAL_ERROR "Could not sign ${CMAKE_INSTALL_PREFIX}/${_app}")
            endif()
        endforeach()
        execute_process(COMMAND /usr/bin/codesign --verify --deep --strict "${CMAKE_INSTALL_PREFIX}/rolo_editor.app"
                        RESULT_VARIABLE _valid)
        if(NOT _valid EQUAL 0)
            message(FATAL_ERROR "The signature of rolo_editor.app does not verify")
        endif()
        message(STATUS "Signed rolo_editor.app (ad hoc)")
    ]] COMPONENT rolo)
endif()

# The package: install into dist/<name>, then zip it.
if(APPLE)
    set(_system "macOS")
elseif(WIN32)
    set(_system "Windows")
else()
    set(_system "Linux")
endif()
set(ROLO_PACKAGE_NAME "Rolo-${PROJECT_VERSION}${ROLO_VERSION_SUFFIX}-${_system}-${CMAKE_SYSTEM_PROCESSOR}")
set(_dist "${PROJECT_SOURCE_DIR}/dist")
add_custom_target(rolo_package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_dist}/${ROLO_PACKAGE_NAME}" "${_dist}/${ROLO_PACKAGE_NAME}.zip"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --component rolo --config $<CONFIG>
            --prefix "${_dist}/${ROLO_PACKAGE_NAME}"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${_dist}" "${CMAKE_COMMAND}" -E tar cf "${ROLO_PACKAGE_NAME}.zip"
            --format=zip "${ROLO_PACKAGE_NAME}"
    COMMENT "Packaging ${ROLO_PACKAGE_NAME} into ${_dist}"
    VERBATIM)
add_dependencies(rolo_package rolo_editor rolo_player)
