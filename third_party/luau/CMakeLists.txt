# rolo_luau: Luau 0.744 (Roblox's Lua, MIT), the language of gameplay scripts
# (ADR 0019, ADR 0020). Unmodified; see README.md.
#
# Built: the parser (Ast), the compiler to bytecode (Compiler, Bytecode),
# the virtual machine and its libraries (VM), and what they share (Common).
# Left out: type checking (Analysis), native code (CodeGen), and Luau's
# programs and tests. Which libraries a script can use is chosen by
# ScriptHost: the io, os, debug and package style libraries are never
# opened (Luau has no io or package at all).
#
# Luau's errors unwind with C++ exceptions (its default when built as C++),
# so destructors in Rolo's bindings run when a script fails.

file(GLOB ROLO_LUAU_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Ast/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Bytecode/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Compiler/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/VM/src/*.cpp")

add_library(rolo_luau STATIC ${ROLO_LUAU_SOURCES})
add_library(rolo::luau ALIAS rolo_luau)
target_compile_features(rolo_luau PUBLIC cxx_std_17)
# SYSTEM: Luau's headers are not ours, so our strict warnings do not apply.
target_include_directories(rolo_luau SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Ast/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Bytecode/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Compiler/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/VM/include")
target_include_directories(rolo_luau PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/VM/src")
set_target_properties(rolo_luau PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # Luau's own flags (its CMakeLists.txt): the portable CRT functions, several cores.
    target_compile_definitions(rolo_luau PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rolo_luau PRIVATE /MP /w)
else()
    target_compile_options(rolo_luau PRIVATE -w)   # not our code: no warnings from it
endif()
