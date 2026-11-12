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
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# ── Native dispatcher fallback (requires PyTorch fork) ───────────────
#
# Builds libcrucible_dispatch.so: registers TORCH_LIBRARY_IMPL fallback
# for DispatchKey::Crucible.  Loaded via torch.ops.load_library().
#
# The build has this library only when TORCH_DIR names a directory with
# torch/include and torch/lib of the patched PyTorch, which has
# DispatchKey::Crucible and the CrucibleState TLS.  The preset vessel gives
# TORCH_DIR the site-packages directory of the virtual environment .venv-torch:
#   cmake --preset vessel && cmake --build --preset vessel
#
set(TORCH_DIR "" CACHE PATH "The directory with torch/include and torch/lib of the patched PyTorch (DispatchKey::Crucible)")

if(TORCH_DIR AND EXISTS "${TORCH_DIR}/torch/include")
    # register.cpp holds one unboxed recording kernel per ATen operator and the
    # boxed fallback keeps catching everything no kernel claims, so the two
    # belong in one library: they share the Vigil handle in PyTorch's TLS, the
    # backward-window thread_local, and the metadata bridge in record_kernel.h.
    # Splitting them would give each .so its own copy of the header-only
    # Crucible and its own backward-window counter.
    add_library(crucible_dispatch SHARED crucible_fallback.cpp)

    # PyTorch headers from the built fork.
    #
    # SYSTEM is load-bearing, not tidiness.  crucible propagates the full
    # PUBLIC warning set (-Werror -Wconversion -Wold-style-cast and ~190
    # more) onto every target that links it.  PyTorch's own headers do not
    # compile under it: with plain -I this target produces ~1300 errors
    # from inside c10 and ATen before reaching a line of Crucible code.
    # SYSTEM suppresses warnings from those headers while leaving every
    # warning armed for the vessel sources.
    add_library(crucible_torch_headers INTERFACE)
    target_include_directories(crucible_torch_headers SYSTEM INTERFACE
        "${TORCH_DIR}/torch/include"
        "${TORCH_DIR}/torch/include/torch/csrc/api/include"
    )

    # crucible is INTERFACE (header-only) — compiles into this target's ABI.
    # foundation is the sibling substrate's runtime: record_kernel.h binds the
    # recording step through a ::fixy role, and fixy is an INTERFACE target
    # over foundation, whose contract handler the binding's own checks reach.
    target_link_libraries(crucible_dispatch PRIVATE crucible fixy crucible_torch_headers)

    # register.cpp holds a kernel for each of the 3110 operators.  One compile
    # of all of them used more than the hard limit of 600 s CPU of a compile
    # job.  So each shard of register.cpp is an object library of its own, with
    # the index of its shard, and the dispatch library links the objects of each
    # shard.  register.cpp divides the operators equally between the shards.
    # A shard of one operator ran 62.9 G instructions on 2026-11-12, most of
    # them in the PyTorch headers of record_kernel.h.  Each shard used about
    # 1.6 GB, so the available memory lets approximately 80 shards compile at one
    # time.  On 2026-11-12, 144 shards built in 220.7 s at -j 48 and 96 shards
    # in 144.5 s, so more shards give a longer wall time.  The rows of
    # utils/ledgers/step-cost.txt keep the shards above the limit of the kind
    # compile, and each object stays below the limit of the rule object-size.
    set(_crucible_register_shards 96)
    math(EXPR _crucible_register_last "${_crucible_register_shards} - 1")
    foreach(_crucible_shard RANGE 0 ${_crucible_register_last})
        set(_crucible_shard_target crucible_dispatch_register_${_crucible_shard})
        add_library(${_crucible_shard_target} OBJECT register.cpp)
        target_compile_definitions(${_crucible_shard_target} PRIVATE
            CRUCIBLE_VESSEL_REGISTER_SHARD=${_crucible_shard}
            CRUCIBLE_VESSEL_REGISTER_SHARDS=${_crucible_register_shards})
        target_link_libraries(${_crucible_shard_target} PRIVATE crucible fixy crucible_torch_headers)
        target_link_libraries(crucible_dispatch PRIVATE ${_crucible_shard_target})
    endforeach()

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
