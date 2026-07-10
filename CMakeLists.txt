cmake_minimum_required(VERSION 3.16)

project(cpp_fantastic_website VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_fantastic_website
    src/main.cpp
)

target_include_directories(cpp_fantastic_website PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(cpp_fantastic_website PRIVATE Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cpp_fantastic_website PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Lightweight unit tests for pure helpers (url_decode / json_escape).
add_executable(aster_unit_tests
    tests/unit_tests.cpp
)
target_include_directories(aster_unit_tests PRIVATE src)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME unit COMMAND aster_unit_tests)
