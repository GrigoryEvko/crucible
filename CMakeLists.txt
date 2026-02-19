cmake_minimum_required(VERSION 3.16)
project(crucible LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library
add_library(crucible INTERFACE)
target_include_directories(crucible INTERFACE include/)

# Tests
enable_testing()
add_subdirectory(test)
