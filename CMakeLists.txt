cmake_minimum_required(VERSION 3.13)

project(RP6502-SHELL C ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(tools)

add_executable(shell)
rp6502_executable(shell)
target_sources(shell PRIVATE
    src/main.c
)


