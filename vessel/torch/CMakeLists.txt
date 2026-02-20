add_library(crucible_vessel SHARED vessel_api.cpp)
target_link_libraries(crucible_vessel PRIVATE crucible)

# For Clang + libc++: embed rpath so Python/ctypes finds libc++.so.1.
# Derive the lib directory from the compiler location.
if(CMAKE_CXX_COMPILER MATCHES "llvm")
    get_filename_component(_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_LLVM_ROOT "${_COMPILER_DIR}" DIRECTORY)
    set(_LLVM_LIB "${_LLVM_ROOT}/lib/x86_64-unknown-linux-gnu")
    if(EXISTS "${_LLVM_LIB}/libc++.so")
        target_link_directories(crucible_vessel PRIVATE "${_LLVM_LIB}")
        target_link_libraries(crucible_vessel PRIVATE c++abi)
        set_target_properties(crucible_vessel PROPERTIES
            BUILD_RPATH "${_LLVM_LIB}"
        )
    endif()
endif()

set_target_properties(crucible_vessel PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
