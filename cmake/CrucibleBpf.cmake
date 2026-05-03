# cmake/CrucibleBpf.cmake — eBPF program compile + embed for the
# crucible static library.  Provides one option, one cache var, and
# one function:
#
#   option       CRUCIBLE_HAVE_BPF       (ON by default)
#   cache var    CRUCIBLE_HAVE_BPF       (resolved after detection)
#   function     crucible_bpf_program(<name> <source>)
#
# Each call to crucible_bpf_program(NAME source.bpf.c):
#   1. Compiles `source.bpf.c` with `clang -target bpf -O2 -g`.
#   2. Embeds the resulting .o as a C array via `xxd -i`.  The embed
#      file declares two extern symbols:
#          extern const unsigned char  <name>_bpf_bytecode[];
#          extern const unsigned int   <name>_bpf_bytecode_len;
#      Userspace loader code references those externs from a
#      crucible::perf TU.
#   3. Appends the generated .c to the global property
#      CRUCIBLE_BPF_EMBED_SOURCES.  After all crucible_bpf_program()
#      calls finish, the parent CMakeLists pulls that property and
#      adds it to the crucible target's source list.
#
# Discovery (libbpf, BPF-capable clang, xxd, optional setcap) runs
# at module load.  If any required tool is missing, CRUCIBLE_HAVE_BPF
# is forced OFF and crucible_bpf_program() becomes a no-op (graceful
# degradation: every consumer that wraps SenseHub::load() in
# `if constexpr (CRUCIBLE_HAVE_BPF)` simply gets stub behaviour).
#
# Promoted from bench/CMakeLists.txt by GAPS-004a (2026-05-03).  The
# original bench-only stanza compiled exactly one program
# (sense_hub.bpf.c) and bundled it into a bench-only static lib;
# the promoted module compiles N programs into the production
# crucible library so Vigil/Cipher/Augur can consume them.

option(CRUCIBLE_HAVE_BPF
  "Compile + embed eBPF programs into the crucible static library" ON)

set(_CRUCIBLE_BPF_AVAILABLE FALSE)

if (CRUCIBLE_HAVE_BPF)
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(LIBBPF QUIET IMPORTED_TARGET libbpf)
  endif()

  # Prefer the system clang (/usr/bin, /usr/local/bin) for BPF because
  # the user's personal toolchain under ~/.local/llvm/bin is a newer
  # clang (22.x) that may reject the installed kernel's BTF or emit
  # relocations libbpf doesn't yet understand.  The system clang is
  # the version matched against the running kernel's BPF ABI.  Fall
  # back to whatever's on PATH if no system clang is installed.
  find_program(CLANG_BPF_COMPILER NAMES clang
    PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
  if (NOT CLANG_BPF_COMPILER)
    find_program(CLANG_BPF_COMPILER NAMES clang)
  endif()

  find_program(XXD_EXECUTABLE NAMES xxd)

  # setcap is consulted by bench/CMakeLists.txt for the bench-caps
  # convenience target; we discover it here so both consumers (root
  # CMakeLists and bench/) can reuse the path.
  find_program(SETCAP_EXECUTABLE NAMES setcap PATHS /usr/sbin /sbin
    NO_DEFAULT_PATH)
  if (NOT SETCAP_EXECUTABLE)
    find_program(SETCAP_EXECUTABLE NAMES setcap)
  endif()

  if (LIBBPF_FOUND AND CLANG_BPF_COMPILER AND XXD_EXECUTABLE)
    set(_CRUCIBLE_BPF_AVAILABLE TRUE)
    if (LIBBPF_VERSION)
      message(STATUS
        "crucible: eBPF substrate ENABLED (libbpf ${LIBBPF_VERSION}, ${CLANG_BPF_COMPILER})")
    else()
      message(STATUS
        "crucible: eBPF substrate ENABLED (libbpf, ${CLANG_BPF_COMPILER})")
    endif()
    if (SETCAP_EXECUTABLE)
      message(STATUS
        "crucible: `cmake --build --preset bench --target bench-caps` to apply BPF caps (sudo)")
    else()
      message(STATUS
        "crucible: setcap not found — benches/binaries will need sudo at invocation for BPF programs")
    endif()
  else()
    message(STATUS "crucible: eBPF substrate DISABLED — missing:"
      "$<$<NOT:$<BOOL:${LIBBPF_FOUND}>>: libbpf>"
      "$<$<NOT:$<BOOL:${CLANG_BPF_COMPILER}>>: clang>"
      "$<$<NOT:$<BOOL:${XXD_EXECUTABLE}>>: xxd>")
  endif()
endif()

# Push the resolved availability back into the cache so callers see
# the FINAL state (after find_package + library detection narrowed
# it).  CACHE BOOL ... FORCE re-publishes the value on every config
# pass so a previously-cached TRUE doesn't survive a missing-libbpf
# rebuild on a different host.
set(CRUCIBLE_HAVE_BPF "${_CRUCIBLE_BPF_AVAILABLE}"
  CACHE BOOL "eBPF substrate available" FORCE)

if (CRUCIBLE_HAVE_BPF)
  # BPF CO-RE headers (libbpf's bpf_helpers.h, vmlinux.h) use
  # __TARGET_ARCH_* to select the per-arch pt_regs layout and syscall
  # conventions.  Hardcoding x86 silently miscompiles on aarch64
  # (register layout differs; argument extraction from pt_regs reads
  # the wrong fields).  Disable BPF on architectures we haven't
  # vetted rather than generate broken bytecode.
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_CRUCIBLE_BPF_ARCH_DEFINE "-D__TARGET_ARCH_x86")
  elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_CRUCIBLE_BPF_ARCH_DEFINE "-D__TARGET_ARCH_arm64")
  else()
    message(WARNING
      "crucible: unsupported BPF arch ${CMAKE_SYSTEM_PROCESSOR}, disabling BPF")
    set(CRUCIBLE_HAVE_BPF OFF CACHE BOOL "" FORCE)
  endif()
endif()

# Initialise the global accumulator that each crucible_bpf_program()
# call appends to.  The parent CMakeLists.txt pulls this property
# after all crucible_bpf_program() invocations and feeds it into the
# crucible target's source list.
#
# ─── Ordering footgun (read this before adding a new program) ──────
#
# The accumulator is read ONCE — by the parent CMakeLists.txt at the
# `add_library(crucible_perf STATIC ${_CRUCIBLE_BPF_EMBED_SOURCES})`
# call site.  CMake captures the source list by value at that moment.
# If `crucible_bpf_program(<new_name> ...)` is called AFTER that
# add_library line, the new embed source will be silently dropped
# from the static library — the BPF program will compile cleanly,
# the .o will be embedded, but the userspace loader will get a link
# error for `<new_name>_bpf_bytecode`.
#
# Discipline: ALL `crucible_bpf_program(...)` calls MUST appear in
# the root CMakeLists.txt BEFORE the `add_library(crucible_perf ...)`
# block.  Adding a new program is a two-line edit at the existing
# call-site cluster, not a separate per-subdirectory wiring.
#
# (This is mitigated for a single-program file today; gates against
# accidents when GAPS-004b/c/d/e/f land more programs.)
set_property(GLOBAL PROPERTY CRUCIBLE_BPF_EMBED_SOURCES "")

# crucible_bpf_program(<name> <source>)
#
# Compiles <source> (a .bpf.c file path relative to CMAKE_SOURCE_DIR)
# to BPF bytecode and embeds it as a C array via xxd.
#
# The embedded array is named exactly `<name>_bpf_bytecode` (with a
# companion `<name>_bpf_bytecode_len`).  Userspace code uses:
#
#     extern "C" {
#         extern const unsigned char  <name>_bpf_bytecode[];
#         extern const unsigned int   <name>_bpf_bytecode_len;
#     }
#
# and passes that pair to `bpf_object__open_mem(...)`.
#
# xxd derives the C identifier from the input file's PATH AS GIVEN
# (not its full path).  We stage the .o into a file named exactly
# `<name>_bpf_bytecode` (no extension), `cd` into its directory, and
# run xxd with the bare basename — that's what makes the generated
# array's C identifier come out as `<name>_bpf_bytecode`.  The
# staging file lives in a per-program `bpf_embed/<name>/` subdir so
# parallel CMake jobs can't race on the same staged path.
function(crucible_bpf_program name source)
  if (NOT CRUCIBLE_HAVE_BPF)
    return()
  endif()

  set(_BPF_DIR          "${CMAKE_SOURCE_DIR}/include/crucible/perf/bpf")
  set(_BPF_SRC          "${CMAKE_SOURCE_DIR}/${source}")
  set(_BPF_OBJ          "${CMAKE_CURRENT_BINARY_DIR}/${name}.bpf.o")
  set(_BPF_EMBED_DIR    "${CMAKE_CURRENT_BINARY_DIR}/bpf_embed/${name}")
  set(_BPF_EMBED_STAGED "${_BPF_EMBED_DIR}/${name}_bpf_bytecode")
  set(_BPF_EMBED_C      "${CMAKE_CURRENT_BINARY_DIR}/${name}_bpf_bytecode.c")

  # Compile BPF program: clang -target bpf, with -O2 (verifier
  # requires optimised code for larger programs) and BTF debug info
  # (-g) for CO-RE field relocations.  -fdebug-prefix-map scrubs the
  # absolute build path from .debug_str so the generated .o (and the
  # embedded byte array) are reproducible across checkouts.
  # IMPLICIT_DEPENDS CXX walks #includes in the .bpf.c to track
  # every header (common.h, vmlinux.h, plus any new file added under
  # include/crucible/perf/bpf/) automatically — saves a brittle
  # manual list per program.
  add_custom_command(
    OUTPUT  ${_BPF_OBJ}
    COMMAND ${CLANG_BPF_COMPILER}
            -target bpf
            ${_CRUCIBLE_BPF_ARCH_DEFINE}
            -I${_BPF_DIR}
            -O2 -g
            -fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
            -Wall -Wno-unused-function -Wno-address-of-packed-member
            -c ${_BPF_SRC}
            -o ${_BPF_OBJ}
    DEPENDS ${_BPF_SRC}
    IMPLICIT_DEPENDS CXX ${_BPF_SRC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Compiling BPF ${source} → ${name}.bpf.o")

  # Convert .bpf.o → C array via xxd -i.  See the comment block at
  # the top of this function for why we stage to a path named
  # exactly `<name>_bpf_bytecode` and run xxd from that directory.
  add_custom_command(
    OUTPUT  ${_BPF_EMBED_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_BPF_EMBED_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${_BPF_OBJ} ${_BPF_EMBED_STAGED}
    COMMAND sh -c "cd ${_BPF_EMBED_DIR} && ${XXD_EXECUTABLE} -i ${name}_bpf_bytecode > ${_BPF_EMBED_C}.tmp"
    COMMAND ${CMAKE_COMMAND} -E rename ${_BPF_EMBED_C}.tmp ${_BPF_EMBED_C}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${_BPF_EMBED_STAGED}
    DEPENDS ${_BPF_OBJ}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Embedding BPF bytecode for ${name}")

  # The embed file is xxd-generated (not present at configure time).
  # Compile it as C++ rather than enabling C project-wide — the file
  # is pure data (`unsigned char ...[] = { ... };`) and parses
  # identically in both languages.  -w silences warnings that would
  # otherwise trip -Werror.
  set_source_files_properties(${_BPF_EMBED_C} PROPERTIES
    GENERATED TRUE
    LANGUAGE CXX
    COMPILE_OPTIONS "-w")

  set_property(GLOBAL APPEND PROPERTY CRUCIBLE_BPF_EMBED_SOURCES
    ${_BPF_EMBED_C})
endfunction()
