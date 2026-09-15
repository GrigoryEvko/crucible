add_library(crucible_vessel SHARED vessel_api.cpp)
target_link_libraries(crucible_vessel PRIVATE crucible)

# ── Stdlib ABI ───────────────────────────────────────────────────────
#
# This file used to select libstdc++ over libc++ per target, because
# crucible_vessel and crucible_dispatch both compile the header-only
# Vigil into their own .so and a layout mismatch between them segfaults
# on the first cross-.so access.  The selection is gone: Crucible builds
# with GCC 16 only (see the Code Guide, "Toolchain"), which has one
# standard library and rejects -stdlib= outright.  Both .so files get
# libstdc++ 16 from the toolchain file, so they agree by construction.

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
    #
    # SYSTEM is load-bearing, not tidiness.  crucible propagates the full
    # PUBLIC warning set (-Werror -Wconversion -Wold-style-cast and ~190
    # more) onto every target that links it.  PyTorch's own headers do not
    # compile under it: with plain -I this target produces ~1300 errors
    # from inside c10 and ATen before reaching a line of Crucible code.
    # SYSTEM suppresses warnings from those headers while leaving every
    # warning armed for crucible_fallback.cpp itself.
    target_include_directories(crucible_dispatch SYSTEM PRIVATE
        "${TORCH_DIR}/torch/include"
        "${TORCH_DIR}/torch/include/torch/csrc/api/include"
    )

    # ── Stdlib ABI ───────────────────────────────────────────────────
    #
    # crucible_dispatch links libtorch/libc10 and calls APIs carrying
    # std::string, std::vector and std::optional in their signatures, so
    # the mangled names must match PyTorch's standard library.  This used
    # to need -stdlib=libstdc++ to override a libc++ Clang preset.  GCC 16
    # is now the only supported compiler, it has no -stdlib= flag and
    # rejects it as an unrecognized option, and it links libstdc++ 16
    # unconditionally — which is what PyTorch is built against.  Passing
    # the flag broke the target outright; there is nothing to select.

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
