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

# ── Native dispatcher fallback (requires PyTorch fork) ───────────────
#
# Builds libcrucible_dispatch.so: registers TORCH_LIBRARY_IMPL fallback
# for DispatchKey::Crucible.  Loaded via torch.ops.load_library().
#
# Only built when TORCH_DIR is set (path to PyTorch source tree with
# DispatchKey::Crucible + CrucibleState TLS).  Example:
#   cmake -DTORCH_DIR=~/Downloads/pytorch ...
#
set(TORCH_DIR "" CACHE PATH "Path to PyTorch source tree (fork with DispatchKey::Crucible)")

if(TORCH_DIR AND EXISTS "${TORCH_DIR}/torch/include")
    add_library(crucible_dispatch SHARED crucible_fallback.cpp)

    # crucible is INTERFACE (header-only) — compiles into this target's ABI.
    target_link_libraries(crucible_dispatch PRIVATE crucible)

    # PyTorch headers from the built fork.
    target_include_directories(crucible_dispatch PRIVATE
        "${TORCH_DIR}/torch/include"
        "${TORCH_DIR}/torch/include/torch/csrc/api/include"
    )

    # ── ABI compatibility with PyTorch ───────────────────────────────
    #
    # PyTorch is typically built with libstdc++ (even when using Clang).
    # crucible_dispatch links against libtorch/libc10 and calls APIs with
    # std::string, std::vector, std::optional in their signatures — the
    # mangled names MUST match.
    #
    # Our preset sets CMAKE_CXX_FLAGS="-stdlib=libc++".  We can't remove
    # it (CMake applies it before target options), but Clang uses the LAST
    # -stdlib= flag on the command line.  So we append -stdlib=libstdc++
    # to override it for this target.
    #
    # This is safe because crucible is header-only: no pre-compiled object
    # with a fixed ABI.  The Crucible headers compile into libstdc++ here.
    target_compile_options(crucible_dispatch PRIVATE -stdlib=libstdc++)
    target_link_options(crucible_dispatch PRIVATE -stdlib=libstdc++)

    # Link against libtorch and libc10 from the fork build.
    find_library(TORCH_LIB torch HINTS "${TORCH_DIR}/torch/lib")
    find_library(C10_LIB c10 HINTS "${TORCH_DIR}/torch/lib")
    find_library(TORCH_CPU_LIB torch_cpu HINTS "${TORCH_DIR}/torch/lib")
    if(TORCH_LIB AND C10_LIB AND TORCH_CPU_LIB)
        target_link_libraries(crucible_dispatch PRIVATE
            ${TORCH_LIB} ${C10_LIB} ${TORCH_CPU_LIB})
    else()
        message(WARNING "Could not find libtorch/libc10 in ${TORCH_DIR}/torch/lib — "
                        "crucible_dispatch will not link correctly")
    endif()

    set_target_properties(crucible_dispatch PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )

    # Rpath to find libtorch.so and libc10.so at runtime.
    set_target_properties(crucible_dispatch PROPERTIES
        BUILD_RPATH "${TORCH_DIR}/torch/lib"
    )

    message(STATUS "Crucible: building native dispatch library (TORCH_DIR=${TORCH_DIR})")
elseif(TORCH_DIR)
    message(WARNING "TORCH_DIR set but ${TORCH_DIR}/torch/include not found — "
                    "skipping crucible_dispatch build")
endif()
