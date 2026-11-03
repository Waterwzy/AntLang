cmake_minimum_required(VERSION 3.16)
project(antc VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(antc
    src/main.c
    src/parse.c
    src/sim.c
    src/codegen.c
    src/util.c
)

if(MSVC)
    target_compile_definitions(antc PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(antc PRIVATE /W4)
else()
    target_compile_options(antc PRIVATE -Wall -Wextra -Wpedantic)
endif()
