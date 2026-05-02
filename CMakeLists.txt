cmake_minimum_required(VERSION 3.16)

project(cpp_fantastic_website VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_fantastic_website src/main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(cpp_fantastic_website PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cpp_fantastic_website PRIVATE -Wall -Wextra -Wpedantic)
endif()

