# Benchmark targets — O3 + native, no sanitizers.
# Built separately from tests:
#   cmake --preset bench && cmake --build --preset bench
#   ./build-bench/bench/bench_arena
#
# The `bench-caps` target (declared below) applies three Linux
# capabilities to every bench binary so they can load the eBPF sense
# hub as an unprivileged user:
#
#   cap_bpf               — bpf_prog_load (tracepoint)
#   cap_perfmon           — perf_event_open on tracepoint IDs
#   cap_dac_read_search   — read /sys/kernel/tracing/events/*/id
#
# Usage: `cmake --build --preset bench --target bench-caps`
# See the target's own comment block for rationale + caveats.

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

  # Prefer the system clang (/usr/bin, /usr/local/bin) for BPF because
  # the user's personal toolchain under ~/.local/llvm/bin is a newer
  # clang (22.x) that may reject the installed kernel's BTF or emit
  # relocations libbpf doesn't yet understand. The system clang is the
  # version matched against the running kernel's BPF ABI. Fall back to
  # whatever's on PATH if no system clang is installed.
  find_program(CLANG_BPF_COMPILER NAMES clang
    PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
  if (NOT CLANG_BPF_COMPILER)
    find_program(CLANG_BPF_COMPILER NAMES clang)
  endif()

  find_program(XXD_EXECUTABLE NAMES xxd)

  # setcap for the bench-caps convenience target (kernel ≥ 5.8 requires
  # CAP_BPF + CAP_PERFMON for tracepoint programs; setcap bakes these
  # into the ELF file's security.capability xattr). Mirror the clang
  # lookup: prefer /usr/sbin /sbin (the canonical location on most
  # distros), fall back to default PATH in case it's packaged under
  # /usr/bin somewhere unusual.
  find_program(SETCAP_EXECUTABLE NAMES setcap PATHS /usr/sbin /sbin NO_DEFAULT_PATH)
  if (NOT SETCAP_EXECUTABLE)
    find_program(SETCAP_EXECUTABLE NAMES setcap)
  endif()

  if (LIBBPF_FOUND AND CLANG_BPF_COMPILER AND XXD_EXECUTABLE)
    set(CRUCIBLE_BENCH_BPF_AVAILABLE TRUE)
    if (LIBBPF_VERSION)
      message(STATUS "bench: eBPF sense hub ENABLED (libbpf ${LIBBPF_VERSION}, ${CLANG_BPF_COMPILER})")
    else()
      message(STATUS "bench: eBPF sense hub ENABLED (libbpf, ${CLANG_BPF_COMPILER})")
    endif()
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

# Gate the BPF __TARGET_ARCH_* define on the build host. BPF CO-RE
# headers (libbpf's bpf_helpers.h, vmlinux.h) use this to select the
# per-arch pt_regs layout and syscall conventions. Hardcoding x86
# silently miscompiles on aarch64 (register layout differs; argument
# extraction from pt_regs reads the wrong fields). Disable BPF on
# architectures we haven't vetted rather than generate broken bytecode.
if (CRUCIBLE_BENCH_BPF_AVAILABLE)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_crucible_bpf_arch_define "-D__TARGET_ARCH_x86")
  elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_crucible_bpf_arch_define "-D__TARGET_ARCH_arm64")
  else()
    message(WARNING "bench: unsupported BPF arch ${CMAKE_SYSTEM_PROCESSOR}, disabling BPF")
    set(CRUCIBLE_BENCH_BPF_AVAILABLE FALSE)
  endif()
endif()

if (CRUCIBLE_BENCH_BPF_AVAILABLE)
  set(BPF_DIR "${CMAKE_SOURCE_DIR}/include/crucible/perf/bpf")
  set(BPF_SRC "${BPF_DIR}/sense_hub.bpf.c")
  set(BPF_OBJ "${CMAKE_CURRENT_BINARY_DIR}/sense_hub.bpf.o")
  set(BPF_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/bpf_embed")
  set(BPF_EMBED_STAGED "${BPF_EMBED_DIR}/sense_hub_bpf_bytecode")
  set(BPF_EMBED_C "${CMAKE_CURRENT_BINARY_DIR}/sense_hub_bpf_bytecode.c")

  # Compile BPF program: clang -target bpf, with -O2 (verifier
  # requires optimized code for larger programs) and BTF debug info
  # (-g) for CO-RE field relocations. -fdebug-prefix-map scrubs the
  # absolute build path from .debug_str so the generated .o (and the
  # embedded byte array) are reproducible across checkouts.
  # IMPLICIT_DEPENDS CXX walks #includes in sense_hub.bpf.c to track
  # every header (common.h, vmlinux.h, plus any new file added under
  # bench/bpf/) automatically, instead of a brittle manual list.
  add_custom_command(
    OUTPUT  ${BPF_OBJ}
    COMMAND ${CLANG_BPF_COMPILER}
            -target bpf
            ${_crucible_bpf_arch_define}
            -I${BPF_DIR}
            -O2 -g
            -fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
            -Wall -Wno-unused-function -Wno-address-of-packed-member
            -c ${BPF_SRC}
            -o ${BPF_OBJ}
    DEPENDS ${BPF_SRC}
    IMPLICIT_DEPENDS CXX ${BPF_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Compiling BPF sense_hub.bpf.c → sense_hub.bpf.o")

  # Convert .bpf.o → C array via xxd -i. xxd derives the C identifier
  # from the input *path as given*, so we stage the .o into a file
  # named exactly `sense_hub_bpf_bytecode` (no extension) and run xxd
  # from that directory with the bare basename. The generated array is
  # `unsigned char sense_hub_bpf_bytecode[]`, matching the extern
  # declarations in bpf_senses.cpp.
  #
  # The staging file lives in a dedicated bpf_embed/ subdirectory so
  # it can't collide with any other build artifact in the bench
  # binary dir that happens to pick the same extensionless name
  # (e.g. a parallel jobs race if a future target stages something
  # similar). Removing the staged file at the end keeps the tree clean.
  add_custom_command(
    OUTPUT  ${BPF_EMBED_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BPF_EMBED_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${BPF_OBJ} ${BPF_EMBED_STAGED}
    COMMAND sh -c "cd ${BPF_EMBED_DIR} && ${XXD_EXECUTABLE} -i sense_hub_bpf_bytecode > ${CMAKE_CURRENT_BINARY_DIR}/sense_hub_bpf_bytecode.c.tmp"
    COMMAND ${CMAKE_COMMAND} -E rename sense_hub_bpf_bytecode.c.tmp ${BPF_EMBED_C}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${BPF_EMBED_STAGED}
    DEPENDS ${BPF_OBJ}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Embedding BPF bytecode into C array")

  # The embed file is xxd-generated (not present at configure time).
  # We compile it as C++ rather than enabling C project-wide — the
  # file is pure data (`unsigned char ...[] = { ... };`) and parses
  # identically in both languages. -w silences any warnings that
  # would otherwise trip -Werror. COMPILE_OPTIONS is the list-based
  # modern API; COMPILE_FLAGS is the deprecated string form. The
  # redundant -x c++ override is gone — LANGUAGE CXX already routes
  # the file through the C++ compiler.
  set_source_files_properties(${BPF_EMBED_C} PROPERTIES
    GENERATED TRUE
    LANGUAGE CXX
    COMPILE_OPTIONS "-w")

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
  # No CRUCIBLE_NO_WERROR needed here: this target doesn't link
  # crucible, so it inherits none of crucible's PUBLIC options (where
  # -Werror lives). The property would be a no-op.
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

  # Asm dump (gitignored, opt-in via -DCRUCIBLE_DUMP_ASM=ON).
  crucible_dump_asm(${name})
endfunction()

# ── Migrated to the new bench::Run / Report / Compare API ─────────────
crucible_bench(bench_arena)
crucible_bench(bench_cipher)
crucible_bench(bench_ckernel)
crucible_bench(bench_content_hash)
crucible_bench(bench_dispatch)
crucible_bench(bench_expr_pool)
crucible_bench(bench_graph)
crucible_bench(bench_iteration_detector)
crucible_bench(bench_merkle_dag)
crucible_bench(bench_meta_log)
crucible_bench(bench_phases)
# Default trace for bench_phases: SD1.5 U-Net (36499 ops) — real-world
# production-scale baseline. 10k+ DAG ops is the minimum meaningful
# signal for the build_trace pipeline; resnet18 (1386) shows <100 µs
# noise, vit_b (~15k) is borderline. SD1.5's 3 ms full pipeline gives
# the phase breakdown clean percentages without sub-phase quantization.
target_compile_definitions(bench_phases PRIVATE
  CRUCIBLE_BENCH_DEFAULT_TRACE="${CMAKE_SOURCE_DIR}/vessel/torch/examples/traces/sd15_unet.crtrace")
crucible_bench(bench_philox)
crucible_bench(bench_pool_allocator)
crucible_bench(bench_reflect)
crucible_bench(bench_region_cache)
crucible_bench(bench_replay_engine)
crucible_bench(bench_safety)
crucible_bench(bench_saturate)
crucible_bench(bench_schema_table)
crucible_bench(bench_serialize)
crucible_bench(bench_simd)
crucible_bench(bench_smoke)
crucible_bench(bench_swiss_table)
crucible_bench(bench_trace_ring)
crucible_bench(bench_concurrent_queues)
crucible_bench(bench_concurrent_saturation)
crucible_bench(bench_mpmc_saturation)
crucible_bench(bench_mpsc_contention)
crucible_bench(bench_permissioned_calendar_grid)
crucible_bench(bench_permissioned_zero_cost)
crucible_bench(bench_permissioned_session_handle)
crucible_bench(bench_spsc_session)
crucible_bench(bench_scheduler_policies)

# ── Visualization tool ────────────────────────────────────────────────
# Not a benchmark — a utility for rendering .crtrace traces as SVG.
# Usage: ./render_trace <trace.crtrace> [-o output.svg] [--blocks-only]
add_executable(render_trace render_trace.cpp)
target_link_libraries(render_trace PRIVATE crucible)
target_compile_options(render_trace PRIVATE -O2)
set_target_properties(render_trace PROPERTIES CRUCIBLE_NO_WERROR TRUE)

# ── bench-caps target ────────────────────────────────────────────────
#
# The bench binary needs three capabilities to load + attach the BPF
# sense hub as an unprivileged user:
#
#   CAP_BPF             — bpf_prog_load (tracepoint type, since 5.8)
#   CAP_PERFMON         — perf_event_open on tracepoint IDs
#   CAP_DAC_READ_SEARCH — read /sys/kernel/tracing/events/*/id (640 root:root).
#                         libbpf reads the tracepoint id file on every
#                         bpf_program__attach() and silently fails to
#                         attach without it; CAP_PERFMON alone does
#                         not override DAC on tracefs.
#
# This target batches `sudo setcap` over every bench binary in one
# command so you enter the password exactly once per rebuild:
#
#   cmake --build --preset bench --target bench-caps
#
# Caveat: the ELF xattr is destroyed on every re-link, so re-run this
# target after any rebuild. It's intentionally NOT a POST_BUILD step
# because that would demand sudo on every build.
#
# Security note: CAP_DAC_READ_SEARCH lets the binary read any file on
# the system, including otherwise-protected ones like /etc/shadow.
# Acceptable for dev binaries running your own bench code; don't
# setcap binaries you don't trust to the same extent.
# Fallback when setcap is absent (hardened containers, some CI images,
# cross-distro build hosts): invoke benches under `sudo ./bench_foo` so
# the eBPF loader runs as root and bypasses the capability check
# entirely. The SenseHub setup path accepts either route — runtime
# behaviour is identical either way.
get_property(_crucible_bench_cap_targets GLOBAL PROPERTY CRUCIBLE_BENCH_CAP_TARGETS)
if (CRUCIBLE_BENCH_BPF_AVAILABLE AND SETCAP_EXECUTABLE AND _crucible_bench_cap_targets)
  set(_crucible_bench_caps_commands
    COMMAND ${CMAKE_COMMAND} -E echo
      "Applying CAP_BPF+CAP_PERFMON to ${_crucible_bench_cap_targets}")
  foreach (_t IN LISTS _crucible_bench_cap_targets)
    list(APPEND _crucible_bench_caps_commands
      COMMAND sudo ${SETCAP_EXECUTABLE} cap_bpf,cap_perfmon,cap_dac_read_search=eip $<TARGET_FILE:${_t}>)
  endforeach()
  add_custom_target(bench-caps
    ${_crucible_bench_caps_commands}
    DEPENDS ${_crucible_bench_cap_targets}
    COMMENT "Applying BPF caps (sudo password required once, re-run after any rebuild)"
    VERBATIM)
endif()
