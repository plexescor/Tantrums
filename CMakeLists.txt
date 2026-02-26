cmake_minimum_required(VERSION 3.15)
project(Tantrums CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES "src/*.cpp")

add_executable(tantrums ${SOURCES})
target_include_directories(tantrums PRIVATE include)

if(MSVC)
    target_compile_definitions(tantrums PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(tantrums PRIVATE /W3 /EHsc)
else()
    target_compile_options(tantrums PRIVATE -Wall -Wextra)
endif()
