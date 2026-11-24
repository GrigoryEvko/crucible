add_library(crucible_vessel SHARED vessel_api.cpp level_note.cpp layout_note.cpp)
target_link_libraries(crucible_vessel PRIVATE crucible)

# The layout digest of layout_note.cpp folds the layout of each shared type in
# one constant evaluation, and the Vigil reaches many types.  The evaluation
# takes more than 33758572 and at most 33772290 operations, measured with a
# bisection of the limit, and CRUCIBLE_CONSTEXPR_OPS_LIMIT is 33554432.  So this
# unit, and no other, takes a higher limit (the owner decision of 2026-11-15: a
# correct change raises the limit).  Each target that compiles the unit is in
# this directory.
set_source_files_properties(layout_note.cpp PROPERTIES COMPILE_OPTIONS "-fconstexpr-ops-limit=40000000")

# ── The layout table of the vessel libraries ─────────────────────────
#
# Each vessel library compiles layout_note.cpp, so it carries an ELF note with
# the layout digest of the types and the C entries that the two libraries
# share (the note of layout_note.h).  The host tool crucible_layout_digest
# compiles the same unit, and writes the layout table of the build beside the
# libraries: the digest, and the size and the member offsets of each struct of
# the C ABI that ctypes mirrors (the table of layout_table.cpp).  The loader
# of the vessel refuses a library whose note differs from the table, and a
# ctypes Structure whose layout differs from it.  The link of each vessel
# library takes the table as an input (LINK_DEPENDS), so a build of one library
# writes the table of its build, and no compile of a library waits for the
# link of the tool.  The command reads only the tool, so it takes
# DEPENDS_EXPLICIT_ONLY: it then waits for no archive that the tool links.  A
# cross build cannot run the tool on the build host, and it has no table: no
# loader of the build host loads its libraries.
set(_crucible_layout_table "${CMAKE_BINARY_DIR}/lib/crucible_layout_digest.txt")
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(crucible_layout_digest layout_table.cpp layout_note.cpp)
    target_link_libraries(crucible_layout_digest PRIVATE crucible)
    add_custom_command(OUTPUT "${_crucible_layout_table}"
        COMMAND sh -c "$<TARGET_FILE:crucible_layout_digest> > ${_crucible_layout_table}.tmp"
        COMMAND ${CMAKE_COMMAND} -E rename "${_crucible_layout_table}.tmp" "${_crucible_layout_table}"
        DEPENDS "$<TARGET_FILE:crucible_layout_digest>"
        DEPENDS_EXPLICIT_ONLY
        COMMENT "Writing the layout table of the vessel libraries"
        VERBATIM)
    add_custom_target(crucible_layout_table DEPENDS "${_crucible_layout_table}")
    set_property(TARGET crucible_vessel APPEND PROPERTY LINK_DEPENDS "${_crucible_layout_table}")
endif()

# ── The x86-64 level of the vessel libraries ─────────────────────────
#
# Each vessel library compiles level_note.cpp, so it carries an ELF note
# with the x86-64 level of its flags (the note of level_note.h).  A build at
# a level gives that file CRUCIBLE_X86_LEVEL_EXACT, and the note then says
# exact.  The loader of the vessel reads the note before it loads a library.
#
# The level probe libcrucible_level.so tells the loader the level of the
# host.  It compiles at -march=x86-64 in each build, after the flags of the
# build, so it runs on each x86_64 host.  It compiles the handler sources of
# foundation itself, because libfoundation.a has the level of the build.  It
# carries no sanitizer, so a Python process with no sanitizer runtime loads
# it in each build: the loader of the vessel, and the test launcher, which
# reads the level of the host before a test starts (the level of
# utils/assay/loop/teststep.py).
#
# The level root
#   The install of a build at a level puts the two vessel libraries into
#   lib/crucible/x86-64-LEVEL, and the install of each build puts the probe
#   into lib/crucible.  Four builds at the four levels with one prefix give a
#   level root, prefix/lib/crucible, from which the loader picks the level of
#   the host (CRUCIBLE_LEVEL_ROOT of crucible_native.py):
#     cmake --install BUILD --component vessel --prefix PREFIX
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(CRUCIBLE_X86_LEVEL)
        string(SUBSTRING "${CRUCIBLE_X86_LEVEL}" 1 1 _crucible_level_number)
        set_source_files_properties(level_note.cpp PROPERTIES
            COMPILE_DEFINITIONS CRUCIBLE_X86_LEVEL_EXACT=${_crucible_level_number})
        install(TARGETS crucible_vessel LIBRARY
            DESTINATION lib/crucible/x86-64-${CRUCIBLE_X86_LEVEL} COMPONENT vessel)
        if(TARGET crucible_layout_table)
            install(FILES "${_crucible_layout_table}"
                DESTINATION lib/crucible/x86-64-${CRUCIBLE_X86_LEVEL} COMPONENT vessel)
        endif()
    endif()
    add_library(crucible_level SHARED level_probe.cpp ${CRUCIBLE_HANDLER_SOURCES})
    target_include_directories(crucible_level PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(crucible_level PRIVATE crucible_dialect)
    target_compile_options(crucible_level PRIVATE -march=x86-64 -mtune=generic)
    # The options of a source come after the options that crucible_dialect
    # gives, so -fno-sanitize=all ends each sanitizer of the build.  No other
    # target of this directory compiles these sources.  The link options of
    # crucible_dialect also name the sanitizer, and GCC then links its
    # runtime with --no-as-needed.  An item of the link libraries comes after
    # each link option, so -fno-sanitize=all there ends the sanitizer of the
    # link too.
    set_source_files_properties(level_probe.cpp ${CRUCIBLE_HANDLER_SOURCES} PROPERTIES
        COMPILE_OPTIONS -fno-sanitize=all)
    target_link_libraries(crucible_level PRIVATE -fno-sanitize=all)
    set_target_properties(crucible_level PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    install(TARGETS crucible_level LIBRARY DESTINATION lib/crucible COMPONENT vessel)
    # The loader opens the probe before each vessel library, so a build of a
    # vessel library builds the probe too, such as the vessel step of the
    # gate, which builds only the two vessel libraries.
    add_dependencies(crucible_vessel crucible_level)
endif()

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
    add_library(crucible_dispatch SHARED crucible_fallback.cpp storage_generation.cpp level_note.cpp layout_note.cpp)

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
    #
    # Each shard compiles with -g1: the debug information of the lines and of
    # the functions, and none of the types and the variables.  With -g, each
    # shard held the debug types of the PyTorch headers again, and the link of
    # the dispatch library read them 96 times.  On 2026-11-15 that link took
    # 3.02 GB of peak memory and wrote a library of 760 MB.  With -g1 it took
    # 0.92 GB and wrote 236 MB.  The shards ran 11.2 T instructions in place
    # of about 13.8 T, and the largest compile took 1.54 GB in place of 1.66.
    set(_crucible_register_shards 96)
    math(EXPR _crucible_register_last "${_crucible_register_shards} - 1")
    foreach(_crucible_shard RANGE 0 ${_crucible_register_last})
        set(_crucible_shard_target crucible_dispatch_register_${_crucible_shard})
        add_library(${_crucible_shard_target} OBJECT register.cpp)
        target_compile_definitions(${_crucible_shard_target} PRIVATE
            CRUCIBLE_VESSEL_REGISTER_SHARD=${_crucible_shard}
            CRUCIBLE_VESSEL_REGISTER_SHARDS=${_crucible_register_shards})
        target_compile_options(${_crucible_shard_target} PRIVATE -g1)
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

    # The dispatch library of a level goes beside the vessel library of the
    # same level (see the level root above).  The loader imports torch._C before it
    # loads the library, so libtorch and libc10 are in the process, and the
    # installed library needs no rpath.
    if(CRUCIBLE_X86_LEVEL)
        install(TARGETS crucible_dispatch LIBRARY
            DESTINATION lib/crucible/x86-64-${CRUCIBLE_X86_LEVEL} COMPONENT vessel)
    endif()
    if(TARGET crucible_level)
        add_dependencies(crucible_dispatch crucible_level)
    endif()
    if(TARGET crucible_layout_table)
        set_property(TARGET crucible_dispatch APPEND PROPERTY LINK_DEPENDS "${_crucible_layout_table}")
    endif()

    message(STATUS "Crucible: building native dispatch library (TORCH_DIR=${TORCH_DIR})")
elseif(TORCH_DIR)
    message(WARNING "TORCH_DIR set but ${TORCH_DIR}/torch/include not found — "
                    "skipping crucible_dispatch build")
endif()
