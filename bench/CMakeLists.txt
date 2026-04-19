# Benchmark targets — O3 + native, no sanitizers.
# Built separately from tests:
#   cmake --preset bench && cmake --build --preset bench
#   ./build-bench/bench/bench_arena

# ── eBPF sense hub ──────────────────────────────────────────────────
#
# When libbpf + a BPF-capable clang are available, compile the kernel
# sense_hub.bpf.c program (96 counters, 58 tracepoints) to BPF
# bytecode, embed it into a static library as a byte array via xxd,
# and link that library into every bench target. Benches that don't
# care about the senses simply don't call SenseHub::load().
#
# Opt out via: cmake --preset bench -DCRUCIBLE_BENCH_BPF=OFF
option(CRUCIBLE_BENCH_BPF "Compile + embed the eBPF sense hub" ON)

set(CRUCIBLE_BENCH_BPF_AVAILABLE FALSE)

if (CRUCIBLE_BENCH_BPF)
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(LIBBPF QUIET IMPORTED_TARGET libbpf)
  endif()

  find_program(CLANG_BPF_COMPILER NAMES clang
    PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
  if (NOT CLANG_BPF_COMPILER)
    find_program(CLANG_BPF_COMPILER NAMES clang)
  endif()

  find_program(XXD_EXECUTABLE NAMES xxd)

  # setcap for the bench-caps convenience target (kernel ≥ 5.8 requires
  # CAP_BPF + CAP_PERFMON for tracepoint programs; setcap bakes these
  # into the ELF file's security.capability xattr).
  find_program(SETCAP_EXECUTABLE NAMES setcap PATHS /usr/sbin /sbin)

  if (LIBBPF_FOUND AND CLANG_BPF_COMPILER AND XXD_EXECUTABLE)
    set(CRUCIBLE_BENCH_BPF_AVAILABLE TRUE)
    message(STATUS "bench: eBPF sense hub ENABLED (libbpf ${LIBBPF_VERSION}, ${CLANG_BPF_COMPILER})")
    if (SETCAP_EXECUTABLE)
      message(STATUS "bench: `cmake --build --preset bench --target bench-caps` to apply BPF caps (sudo)")
    else()
      message(STATUS "bench: setcap not found — benches will need sudo/run-as-root at invocation")
    endif()
  else()
    message(STATUS "bench: eBPF sense hub DISABLED — missing:"
      "$<$<NOT:$<BOOL:${LIBBPF_FOUND}>>: libbpf>"
      "$<$<NOT:$<BOOL:${CLANG_BPF_COMPILER}>>: clang>"
      "$<$<NOT:$<BOOL:${XXD_EXECUTABLE}>>: xxd>")
  endif()
endif()

if (CRUCIBLE_BENCH_BPF_AVAILABLE)
  set(BPF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/bpf/sense_hub.bpf.c")
  set(BPF_OBJ "${CMAKE_CURRENT_BINARY_DIR}/sense_hub.bpf.o")
  set(BPF_EMBED_C "${CMAKE_CURRENT_BINARY_DIR}/sense_hub_bpf_bytecode.c")

  # Compile BPF program: clang -target bpf, with -O2 (verifier
  # requires optimized code for larger programs) and BTF debug info
  # (-g) for CO-RE field relocations.
  add_custom_command(
    OUTPUT  ${BPF_OBJ}
    COMMAND ${CLANG_BPF_COMPILER}
            -target bpf
            -D__TARGET_ARCH_x86
            -I${CMAKE_CURRENT_SOURCE_DIR}/bpf
            -O2 -g
            -Wall -Wno-unused-function -Wno-address-of-packed-member
            -c ${BPF_SRC}
            -o ${BPF_OBJ}
    DEPENDS ${BPF_SRC}
            ${CMAKE_CURRENT_SOURCE_DIR}/bpf/common.h
            ${CMAKE_CURRENT_SOURCE_DIR}/bpf/vmlinux.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Compiling BPF sense_hub.bpf.c → sense_hub.bpf.o")

  # Convert .bpf.o → C array via xxd -i. The generated file declares
  # `sense_hub_bpf_o[]` + `sense_hub_bpf_o_len`; we rename them to
  # the canonical bench API names with sed-free in-place edits by
  # copying the input file into binary dir with the desired basename
  # first (xxd uses the input file name to derive the C identifier).
  # xxd derives the C identifier from the input *path as given*. We
  # stage the .o to a file named exactly `sense_hub_bpf_bytecode` in
  # the build dir, then run xxd with that bare basename from the same
  # directory — the generated array is `sense_hub_bpf_bytecode[]`,
  # matching the extern decls in bpf_senses.cpp.
  add_custom_command(
    OUTPUT  ${BPF_EMBED_C}
    COMMAND ${CMAKE_COMMAND} -E copy ${BPF_OBJ} sense_hub_bpf_bytecode
    COMMAND sh -c "${XXD_EXECUTABLE} -i sense_hub_bpf_bytecode > sense_hub_bpf_bytecode.c.tmp"
    COMMAND ${CMAKE_COMMAND} -E rename sense_hub_bpf_bytecode.c.tmp ${BPF_EMBED_C}
    COMMAND ${CMAKE_COMMAND} -E remove -f sense_hub_bpf_bytecode
    DEPENDS ${BPF_OBJ}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Embedding BPF bytecode into C array")

  # The generated array is at sense_hub_bpf_bytecode[] (xxd derives
  # the identifier from the input file basename, and we copied the
  # .o into a file named sense_hub_bpf_bytecode with no extension).
  # That lines up exactly with the extern declarations in bpf_senses.cpp.

  # The embed file is xxd-generated (not present at configure time).
  # We compile it as C++ rather than enabling C project-wide — the
  # file is pure data (`unsigned char ...[] = { ... };`) and parses
  # identically in both languages. -w silences any warnings that
  # would otherwise trip our project-wide -Werror.
  set_source_files_properties(${BPF_EMBED_C} PROPERTIES
    GENERATED TRUE
    LANGUAGE CXX
    COMPILE_FLAGS "-w -x c++")

  add_library(crucible_bench_bpf STATIC
    bpf_senses.cpp
    ${BPF_EMBED_C})
  target_include_directories(crucible_bench_bpf PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(crucible_bench_bpf PUBLIC
    PkgConfig::LIBBPF)
  # Benches compile with -O3 -march=native; match that for the BPF
  # loader so runtime overhead of SenseHub::read() is minimal.
  target_compile_options(crucible_bench_bpf PRIVATE
    -O3 -march=native -mtune=native -fno-omit-frame-pointer)
  set_target_properties(crucible_bench_bpf PROPERTIES CRUCIBLE_NO_WERROR TRUE)
endif()

function(crucible_bench name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_compile_options(${name} PRIVATE
    -O3 -march=native -mtune=native -fno-omit-frame-pointer)
  target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH)
  if (CRUCIBLE_BENCH_BPF_AVAILABLE)
    target_link_libraries(${name} PRIVATE crucible_bench_bpf)
    target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH_HAVE_BPF=1)
    # Register for the bench-caps target — applied after all benches
    # are declared so we can batch them into one sudo invocation.
    set_property(GLOBAL APPEND PROPERTY CRUCIBLE_BENCH_CAP_TARGETS ${name})
  else()
    target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH_HAVE_BPF=0)
  endif()
  # Opt out of -Werror: benches ignore nodiscard returns by design.
  set_target_properties(${name} PROPERTIES CRUCIBLE_NO_WERROR TRUE)
endfunction()

# ── Migrated to the new bench::Run / Report / Compare API ─────────────
crucible_bench(bench_arena)
crucible_bench(bench_trace_ring)

# ── TODO: migrate to the new API in subsequent commits. Each one is
#        independent. Until migrated they remain off so the bench preset
#        builds clean against the new harness. ────────────────────────
#
# crucible_bench(bench_smoke)
# crucible_bench(bench_meta_log)
# crucible_bench(bench_pool_allocator)
# crucible_bench(bench_region_cache)
# crucible_bench(bench_dispatch)
# crucible_bench(bench_swiss_table)
# crucible_bench(bench_expr_pool)
# crucible_bench(bench_iteration_detector)
# crucible_bench(bench_replay_engine)
# crucible_bench(bench_merkle_dag)
# crucible_bench(bench_phases)
# crucible_bench(bench_saturate)
# crucible_bench(bench_safety)
# crucible_bench(bench_philox)
# crucible_bench(bench_ckernel)
# crucible_bench(bench_schema_table)
# crucible_bench(bench_serialize)
# crucible_bench(bench_graph)
# crucible_bench(bench_reflect)
# crucible_bench(bench_cipher)
# crucible_bench(bench_content_hash)

# ── Visualization tool ────────────────────────────────────────────────
# Not a benchmark — a utility for rendering .crtrace traces as SVG.
# Usage: ./render_trace <trace.crtrace> [-o output.svg] [--blocks-only]
add_executable(render_trace render_trace.cpp)
target_link_libraries(render_trace PRIVATE crucible)
target_compile_options(render_trace PRIVATE -O2)
set_target_properties(render_trace PROPERTIES CRUCIBLE_NO_WERROR TRUE)

# ── bench-caps target ────────────────────────────────────────────────
#
# The kernel requires CAP_BPF + CAP_PERFMON for tracepoint-type BPF
# programs (Linux ≥ 5.8, hardcoded in bpf_prog_load — not gated by
# kernel.unprivileged_bpf_disabled). This target batches `sudo setcap`
# over every bench binary in a single command so you enter the
# password exactly once per rebuild:
#
#   cmake --build --preset bench --target bench-caps
#
# Caveat: the ELF xattr is destroyed on every re-link, so this target
# must be re-run after any rebuild of the benches. It's intentionally
# NOT a POST_BUILD step because that would demand sudo on every build.
get_property(_crucible_bench_cap_targets GLOBAL PROPERTY CRUCIBLE_BENCH_CAP_TARGETS)
if (CRUCIBLE_BENCH_BPF_AVAILABLE AND SETCAP_EXECUTABLE AND _crucible_bench_cap_targets)
  set(_crucible_bench_caps_commands
    COMMAND ${CMAKE_COMMAND} -E echo
      "Applying CAP_BPF+CAP_PERFMON to ${_crucible_bench_cap_targets}")
  foreach (_t IN LISTS _crucible_bench_cap_targets)
    list(APPEND _crucible_bench_caps_commands
      COMMAND sudo ${SETCAP_EXECUTABLE} cap_bpf,cap_perfmon=eip $<TARGET_FILE:${_t}>)
  endforeach()
  add_custom_target(bench-caps
    ${_crucible_bench_caps_commands}
    DEPENDS ${_crucible_bench_cap_targets}
    COMMENT "Applying BPF caps (sudo password required once, re-run after any rebuild)"
    VERBATIM)
endif()
