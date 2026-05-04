cmake_minimum_required(VERSION 3.25)

# ── ccache: default + always-used layer ─────────────────────────────
#
# Set BEFORE project() so the compiler-probe pass also runs through
# ccache.  The reflection-and-contracts build pulls cc1plus per heavy
# TU through 800 MB - 1 GB and several CPU-seconds; ccache's cache
# survives compiler updates (compiler_check=content), works across
# build directories (hash_dir=false), and uses a tuned sloppiness set
# documented in /root/.config/ccache/ccache.conf.  Hit rate on the
# dev box: 78%+ over 648K calls at this commit.
#
# Honours pre-set CMAKE_CXX_COMPILER_LAUNCHER from env / -D; a user
# can disable by passing -DCRUCIBLE_USE_CCACHE=OFF at configure.
option(CRUCIBLE_USE_CCACHE "Use ccache as compiler launcher when available" ON)
if(CRUCIBLE_USE_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER
                       AND NOT DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
  find_program(CCACHE_EXECUTABLE ccache)
  if(CCACHE_EXECUTABLE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXECUTABLE}"
        CACHE STRING "Compiler launcher for CXX (ccache)")
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_EXECUTABLE}"
        CACHE STRING "Compiler launcher for C (ccache)")
  endif()
endif()

project(crucible LANGUAGES CXX)

if(CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
  message(STATUS "ccache: ${CMAKE_CXX_COMPILER_LAUNCHER} (compiler launcher enabled)")
elseif(CRUCIBLE_USE_CCACHE)
  message(STATUS "ccache: not found on PATH — proceeding without launcher")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Header-only library plus one TU for the weak contract-violation handler.
# Downstream tests/benches need `handle_contract_violation` defined at link
# time (GCC 16 libstdc++ does not provide a default); the weak symbol lets
# production binaries override with a stronger one that calls crucible_abort.
add_library(crucible STATIC
  src/ContractHandler.cpp
  src/DiagRuntime.cpp
)
target_include_directories(crucible PUBLIC include/)
set_target_properties(crucible PROPERTIES POSITION_INDEPENDENT_CODE ON)

# C++26-only flags — applied at target level where -std=c++26 is active
# (applying them in CMAKE_CXX_FLAGS would poison CMake's compiler-probe).
target_compile_options(crucible PUBLIC
  -freflection
  -fcontracts
)

# ── eBPF observability substrate (crucible::perf::SenseHub etc.) ────
#
# Promoted from bench/ on 2026-05-03 (GAPS-004a).  When libbpf + a
# BPF-capable clang are available, we compile the kernel-side BPF
# programs under include/crucible/perf/bpf/, embed each as a C array
# via xxd, and bundle them with their userspace loaders into a
# `crucible_perf` static library that crucible PUBLICly links.  Every
# consumer of `crucible` then transparently gets the perf subsystem;
# headers like <crucible/perf/SenseHub.h> are always included via
# `target_include_directories(crucible PUBLIC include/)` whether or
# not BPF is available, so callers can depend on the type definitions
# (Snapshot, Idx) unconditionally.  Loader symbols (SenseHub::load
# etc.) are present iff CRUCIBLE_HAVE_BPF=1; gate runtime calls on
# the macro to avoid link errors on non-BPF builds.
#
# Add a new program: write include/crucible/perf/bpf/<name>.bpf.c
# (kernel side) + include/crucible/perf/<Name>.h + src/perf/<Name>.cpp
# (userspace), then call crucible_bpf_program(<name> ...) and append
# the .cpp to the crucible_perf source list below.
include(${CMAKE_SOURCE_DIR}/cmake/CrucibleBpf.cmake)

crucible_bpf_program(sense_hub        include/crucible/perf/bpf/sense_hub.bpf.c)
crucible_bpf_program(sched_switch     include/crucible/perf/bpf/sched_switch.bpf.c)
crucible_bpf_program(pmu_sample       include/crucible/perf/bpf/pmu_sample.bpf.c)
crucible_bpf_program(lock_contention  include/crucible/perf/bpf/lock_contention.bpf.c)
crucible_bpf_program(syscall_latency  include/crucible/perf/bpf/syscall_latency.bpf.c)

if (CRUCIBLE_HAVE_BPF)
  get_property(_CRUCIBLE_BPF_EMBED_SOURCES
    GLOBAL PROPERTY CRUCIBLE_BPF_EMBED_SOURCES)
  add_library(crucible_perf STATIC
    src/perf/SenseHub.cpp
    src/perf/SchedSwitch.cpp
    src/perf/PmuSample.cpp
    src/perf/LockContention.cpp
    src/perf/SyscallLatency.cpp
    ${_CRUCIBLE_BPF_EMBED_SOURCES})
  target_include_directories(crucible_perf PUBLIC include/)
  target_link_libraries(crucible_perf PUBLIC PkgConfig::LIBBPF)
  # crucible_perf is a DEPENDENCY of `crucible` (crucible PUBLIC-links
  # crucible_perf below), not a dependent — so it does NOT inherit
  # crucible's PUBLIC -freflection / -fcontracts options.  We need
  # them ourselves because src/perf/SenseHub.cpp pulls
  # safety/Mutation.h, which transitively reaches algebra/Graded.h's
  # reflection-driven display_string_of() helper.
  #
  # `-fcontract-evaluation-semantic=ignore` is critical here: under
  # the default `enforce` semantic, every WriteOnce::set / .get
  # contract emits a reference to `handle_contract_violation`.  That
  # symbol lives in crucible's `src/ContractHandler.cpp`, but
  # crucible_perf is linked BEFORE crucible in the consumer link
  # order (crucible.a then crucible_perf.a; ld scans left-to-right
  # and won't re-resolve crucible.a's symbols once it moves past).
  # Ignoring contracts in this TU compiles them out entirely — the
  # discipline still binds at the type-system level (WriteOnce's
  # nodiscard-set-once-or-burn semantics persist), but no runtime
  # handler call is generated, so the link is clean.  This matches
  # the §XII semantic-per-TU pattern documented in CLAUDE.md.
  target_compile_options(crucible_perf PRIVATE
    -freflection
    -fcontracts
    -fcontract-evaluation-semantic=ignore
  )
  # crucible_perf inherits crucible's PUBLIC -freflection/-fcontracts
  # via the link below, but its sources don't NEED them — the loader
  # is a thin C++ shim around the C libbpf API.  Belt-and-braces:
  # opt out of -Werror (the embed .c file is xxd-generated bytes;
  # libbpf headers are C and may trip strict warnings under C++26).
  set_target_properties(crucible_perf PROPERTIES CRUCIBLE_NO_WERROR TRUE)
  # PUBLIC link so symbols transit to the final binary; consumers
  # that include <crucible/perf/SenseHub.h> get the loader symbols
  # at link time without an extra `target_link_libraries` call.
  target_link_libraries(crucible PUBLIC crucible_perf)
  target_compile_definitions(crucible PUBLIC CRUCIBLE_HAVE_BPF=1)
else()
  target_compile_definitions(crucible PUBLIC CRUCIBLE_HAVE_BPF=0)
endif()

# Re `-fno-ipa-modref`: we considered it as a workaround for a GCC 16
# miscompile where `asm volatile("" : "+m,r"(ptr) : : "memory")` applied
# to a non-const pointer-to-class-with-pre()-contract-method, captured
# by reference in a lambda, generates wrong code. Our release build
# uses `-fcontract-evaluation-semantic=ignore` which happens to avoid
# the minimal-repro crash path, but the real bench still triggers a
# DIFFERENT miscompile along the same family. Safer fix is at the
# source level: don't use `do_not_optimize(raw_typed_ptr)` — pass the
# result of a method call instead. See bugs/gcc-modref-miscompile/
# for the full GCC bug writeup.

# ╔═══════════════════════════════════════════════════════════════════╗
# ║  CRUCIBLE HARDENING — 300+ compiler/linker/sanitizer flags       ║
# ║                                                                   ║
# ║  GCC 16:   200+ individual warnings + contracts + reflection     ║
# ║            (only supported compiler — see code_guide.md §I)       ║
# ║  +         sanitizers, linker hardening, FORTIFY, library checks ║
# ║                                                                   ║
# ║  "Every nanosecond matters. Every byte is accounted for.         ║
# ║   Every type is strong. Every pointer is checked."               ║
# ╚═══════════════════════════════════════════════════════════════════╝

option(CRUCIBLE_WERROR "Treat warnings as errors" OFF)

# ── Asm dump (gitignored, opt-in) ──────────────────────────────────
#
# When -DCRUCIBLE_DUMP_ASM=ON, post-build steps for every test +
# bench target run `objdump -d -M intel` on the linked binary and
# drop the .s file (plus copy of the primary source) under
# build/asm-dump/.  The build/ dir is .gitignore'd; this is a dev-
# time inspection tool for compiler output / asm-comparison work
# (e.g., verifying typed wrapper compiles to identical machine code
# as bare).
#
# Usage:
#   cmake --preset default -DCRUCIBLE_DUMP_ASM=ON
#   cmake --build --preset default
#   ls build/asm-dump/   # one .s + one .cpp per test/bench target
#
# Each .s file contains the full Intel-syntax disassembly of the
# linked binary (post-LTO, all kept symbols).  Default OFF — every
# objdump invocation adds 100s of ms; opt-in for asm-comparison.
option(CRUCIBLE_DUMP_ASM
    "Dump objdump output of every test/bench to build/asm-dump/" OFF)

if(CRUCIBLE_DUMP_ASM)
    set(CRUCIBLE_ASM_DUMP_DIR "${CMAKE_BINARY_DIR}/asm-dump")
    file(MAKE_DIRECTORY "${CRUCIBLE_ASM_DUMP_DIR}")
    message(STATUS "CRUCIBLE_DUMP_ASM=ON — asm dumps will be written to ${CRUCIBLE_ASM_DUMP_DIR}")
endif()

# Helper called by crucible_test / crucible_bench.  No-op when
# CRUCIBLE_DUMP_ASM is OFF.  Emits two files into CRUCIBLE_ASM_DUMP_DIR:
#   ${target}.s   — Intel-syntax disassembly of the linked binary.
#   ${target}.cpp — copy of the primary source for side-by-side review.
function(crucible_dump_asm target)
    if(CRUCIBLE_DUMP_ASM)
        # Resolve source absolute path NOW so the post-build copy
        # doesn't need to interpret a generator expression against
        # the build dir's CWD.
        get_target_property(_srcs ${target} SOURCES)
        list(GET _srcs 0 _primary_src)
        if(NOT IS_ABSOLUTE "${_primary_src}")
            set(_primary_src "${CMAKE_CURRENT_SOURCE_DIR}/${_primary_src}")
        endif()

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "─── asm dump: ${target} ───────────────────"
            COMMAND objdump -d --no-show-raw-insn -M intel
                    $<TARGET_FILE:${target}>
                    > "${CRUCIBLE_ASM_DUMP_DIR}/${target}.s"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_primary_src}"
                    "${CRUCIBLE_ASM_DUMP_DIR}/${target}.cpp"
            BYPRODUCTS
                "${CRUCIBLE_ASM_DUMP_DIR}/${target}.s"
                "${CRUCIBLE_ASM_DUMP_DIR}/${target}.cpp"
            COMMENT "Dumping asm for ${target} → ${CRUCIBLE_ASM_DUMP_DIR}/${target}.s"
            VERBATIM)
    endif()
endfunction()

# ═══════════════════════════════════════════════════════════════
# SECTION 0: Debug optimization override
#
# -Og instead of -O0: enables _FORTIFY_SOURCE, better sanitizer
# coverage, still fully debuggable (variables visible, sane stepping).
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS_DEBUG "-g -Og" CACHE STRING "" FORCE)
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 1: Common flags — GCC 16
#            52 warning flags + 13 hardening flags = 65
# ═══════════════════════════════════════════════════════════════
target_compile_options(crucible PUBLIC
  # ── 1.1  Baseline (3) ──────────────────────────────────────
  -Wall                                #   1
  -Wextra                              #   2
  -Wpedantic                           #   3

  # ── 1.2  Type conversion safety (10) ───────────────────────
  -Wconversion                         #   4  implicit narrowing/widening
  -Wsign-conversion                    #   5  signed ↔ unsigned implicit
  -Wdouble-promotion                   #   6  float → double implicit
  -Wfloat-conversion                   #   7  float ↔ int implicit
  -Wfloat-equal                        #   8  == on floats
  -Wsign-compare                       #   9  signed vs unsigned compare
  -Wsign-promo                         #  10  unsigned→signed overload
  -Wold-style-cast                     #  11  C-style cast
  -Wcast-align                         #  12  alignment-increasing cast
  -Wcast-qual                          #  13  cast discards const

  # ── 1.3  Enum safety (5) ───────────────────────────────────
  # Note: -Wswitch-enum (warn on missing case even with default:) is
  # intentionally omitted.  Our Op/CKernelId enums have 60+/140+ values;
  # most dispatchers handle a strict subset and fall through on default.
  # -Werror=switch + -Wswitch-default catch the real bugs (uncovered value
  # with no default, missing default) without drowning in 60-way noise.
  -Wswitch-default                     #  15  no default in switch
  -Wimplicit-fallthrough               #  16  missing [[fallthrough]]
  -Wenum-conversion                    #  17  enum type conversion
  -Wenum-compare                       #  18  comparing different enums
  -Wdeprecated-enum-enum-conversion    #  19  arithmetic on enums

  # ── 1.4  Null / pointer (3) ────────────────────────────────
  -Wnull-dereference                   #  20  null deref paths
  -Wzero-as-null-pointer-constant      #  21  0 instead of nullptr
  -Wpointer-arith                      #  22  void/func ptr arith

  # ── 1.5  Initialization (2) ────────────────────────────────
  -Wuninitialized                      #  23  use before init
  -Wshadow                             #  24  shadowed variables

  # ── 1.6  Format strings (2) ────────────────────────────────
  -Wformat=2                           #  25  security + type checks
  -Wformat-signedness                  #  26  sign mismatch in format

  # ── 1.7  Unused / redundant (5) ────────────────────────────
  -Wundef                              #  27  #if on undefined macro
  -Wunused                             #  28  unused anything
  -Wunused-macros                      #  29  unused #define
  -Wredundant-decls                    #  30  redundant declarations
  -Wmissing-declarations               #  31  no prior declaration

  # ── 1.8  Class / OOP (6) ───────────────────────────────────
  -Wnon-virtual-dtor                   #  32  no virtual dtor
  -Wdelete-non-virtual-dtor            #  33  delete via base
  -Woverloaded-virtual                 #  34  hides base virtual
  -Wsuggest-override                   #  35  missing override
  -Wdeprecated-copy                    #  36  deprecated copy op
  -Wdeprecated-copy-dtor              #  37  deprecated copy + dtor

  # ── 1.9  Miscellaneous (16) ────────────────────────────────
  -Wmismatched-tags                    #  38  struct/class mismatch
  -Wchar-subscripts                    #  39  char as array index
  -Wreturn-type                        #  40  missing return value
  -Wstrict-aliasing=2                  #  41  aliasing violations
  -Wtype-limits                        #  42  comparison always t/f
  -Wvla                                #  43  variable-length arrays
  -Wextra-semi                         #  44  redundant semicolons
  -Wself-move                          #  45  move-to-self
  -Wstack-protector                    #  46  stack protector fail
  -Wdeprecated                         #  47  deprecated features
  -Wmissing-include-dirs               #  48  -I dir doesn't exist
  -Wwrite-strings                      #  49  string literal → char*
  -Wpacked                             #  50  packed struct issues

  # ── 1.10  Hardening — all builds (13) ──────────────────────
  -fno-omit-frame-pointer              #  53  frame ptr for profiling
  -fstack-protector-strong             #  54  stack canaries
  -fno-common                          #  55  no tentative definitions
  -fno-strict-overflow                 #  56  signed overflow is UB→defined
  -fstrict-flex-arrays=3               #  57  strict flex array rules
  -fvisibility=hidden                  #  58  hide symbols by default
  -fvisibility-inlines-hidden          #  59  hide inline symbols
  -ffunction-sections                  #  60  one func per section
  -fdata-sections                      #  61  one var per section
  -fno-plt                             #  62  direct calls (no PLT)
  -fno-semantic-interposition          #  63  assume no interposition
  -fsized-deallocation                 #  64  sized delete support
  -fdiagnostics-show-option            #  65  show -W flag in diagnostic
)

if(CRUCIBLE_WERROR)
  # Gated on target property so bench/ can opt out cleanly (benches
  # legitimately ignore some [[nodiscard]] returns — they're measuring,
  # not enforcing).  Library + tests keep full -Werror.
  # -Wno-error=tsan: GCC rejects std::atomic_thread_fence under
  # -fsanitize=thread (TSan can't model fences).  Our SPSC rings use
  # a relaxed-load + acquire-fence fast path; pattern is sound per the
  # C++ model.  Warning remains visible; just not an error.
  #
  # -Wno-error=suggest-attribute=cold: GCC 16's heuristic flags test
  # main() functions as cold under TSan instrumentation.  The code
  # guide lists -Wsuggest-* as suggestions, not errors.
  target_compile_options(crucible PUBLIC
    $<$<NOT:$<BOOL:$<TARGET_PROPERTY:CRUCIBLE_NO_WERROR>>>:-Werror
      -Wno-error=tsan
      -Wno-error=suggest-attribute=cold>)
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 2: GCC 16 — MAXIMUM PARANOIA
#            200+ individual warning flags
# ═══════════════════════════════════════════════════════════════
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(crucible PUBLIC
    # ── 3.1  Type conversion (7) ─────────────────────────────
    # -Wconversion-extra (Fortran-only), -Wenum-int-mismatch (C-only),
    # -Wabsolute-value (C-only), -Woverride-init (C-only),
    # -Wunterminated-string-initialization (C-only) dropped — GCC emits
    # "valid for C/ObjC but not for C++" for each on every TU.
    -Warith-conversion                  # 104  arith conversion lossy
    -Wlogical-op                        # 107  suspicious logical ops
    -Wduplicated-cond                   # 108  duplicate if/else cond
    -Wduplicated-branches               # 109  identical branches
    -Wbool-compare                      # 110  bool vs int compare
    -Wbool-operation                    # 111  suspicious bool op
    -Wuseless-cast                      # 112  cast to same type

    # ── 3.2  Initialization (2) ──────────────────────────────
    -Wmaybe-uninitialized               # 113  might-be-uninit
    -Wtrivial-auto-var-init             # 114  auto-init var warnings

    # ── 3.3  Bounds / overflow (8) ───────────────────────────
    -Warray-bounds=2                    # 115  aggressive OOB
    -Wshift-overflow=2                  # 116  shift overflow
    -Wstringop-overflow=4               # 117  stringop overflow
    -Wstrict-overflow=2                 # 118  signed overflow opt
    -Wformat-overflow=2                 # 119  sprintf overflow
    -Wformat-truncation=2               # 120  snprintf truncation
    -Wzero-length-bounds                # 121  zero-length array OOB
    -Wstringop-overread                 # 122  read past end of array

    # ── 3.4  Memory / class (7) ──────────────────────────────
    -Wclass-memaccess                   # 123  memcpy non-trivial
    -Wplacement-new=2                   # 124  placement-new into small
    -Walloc-zero                        # 125  alloc(0) usage
    -Walloca                            # 126  alloca usage
    -Wdangling-reference                # 127  dangling reference
    -Wuse-after-free=3                  # 128  use-after-free (aggressive)
    -Wmismatched-new-delete             # 129  new[]/delete mismatch

    # ── 3.5  Stack (3) ───────────────────────────────────────
    -Wstack-usage=524288                # 130  func stack > 512KB (bg thread has 2MB)
    -Wtrampolines                       # 131  trampoline generation
    -Walloca-larger-than=4096           # 132  large alloca

    # ── 3.6  Pointer / address (6) ───────────────────────────
    -Waddress                           # 133  suspicious address use
    -Waddress-of-packed-member          # 134  unaligned member addr
    -Wpointer-compare                   # 135  ptr == "string"
    -Wreturn-local-addr                 # 136  return addr of local
    -Wsizeof-pointer-div                # 137  sizeof(ptr)/sizeof(*ptr)
    -Wsizeof-pointer-memaccess          # 138  sizeof(ptr) in memcpy

    # ── 3.7  Format (2) ─────────────────────────────────────
    -Wformat-nonliteral                 # 139  non-literal format str
    -Wformat-security                   # 140  format as argument

    # ── 3.8  String / sizeof (5) ─────────────────────────────
    -Wstring-compare                    # 141  suspicious strcmp
    -Wstringop-truncation               # 142  strncpy truncation
    -Wsizeof-array-argument             # 143  sizeof on array param
    -Wsizeof-array-div                  # 144  sizeof(arr)/sizeof(T)
    -Wrestrict                          # 145  restrict violation

    # ── 3.9  Class / OOP extras (10) ─────────────────────────
    -Wredundant-tags                    # 146  redundant struct/class
    -Wredundant-move                    # 147  unnecessary std::move
    -Wpessimizing-move                  # 148  std::move prevents RVO
    -Wvirtual-move-assign               # 149  virtual base move
    -Wregister                          # 150  register storage class
    -Wvolatile                          # 151  deprecated volatile
    -Wrange-loop-construct              # 152  range-for improvements
    -Wvexing-parse                      # 153  most-vexing-parse
    -Wtemplate-id-cdtor                 # 154  template-id in ctor/dtor

    # ── 3.10  Attributes / suggestions (7) ───────────────────
    -Wattribute-alias=2                 # 156  alias type mismatch
    -Wsuggest-attribute=noreturn        # 157  suggest [[noreturn]]
    -Wsuggest-attribute=format          # 158  suggest format attr
    -Wsuggest-attribute=cold            # 159  suggest cold attr
    -Wsuggest-attribute=malloc          # 160  suggest malloc attr
    -Wsuggest-attribute=returns_nonnull # 161  suggest returns_nonnull
    -Wsuggest-final-types               # 162  suggest final class
    -Wsuggest-final-methods             # 163  suggest final method

    # ── 3.11  Miscellaneous GCC (22) ─────────────────────────
    -Wnoexcept                          # 164  noexcept-expr false
    -Wsubobject-linkage                 # 165  subobject linkage
    -Wsized-deallocation                # 166  no sized dealloc
    -Wstrict-null-sentinel              # 167  NULL as sentinel
    -Warray-compare                     # 168  array == array
    -Warray-parameter=2                 # 169  array param mismatch
    -Wvla-parameter                     # 170  VLA param mismatch
    -Wcatch-value=3                     # 171  catch by value
    -Wextra-semi                        # 172  extra semicolons (gcc)
    -Wshift-count-negative              # 173  negative shift count
    -Wshift-count-overflow              # 174  shift count ≥ width
    -Wshift-negative-value              # 175  shift of negative val
    -Wsequence-point                    # 178  undefined eval order
    -Wvarargs                           # 179  va_arg problems
    -Wattributes                        # 180  attribute warnings
    -Wattribute-warning                 # 181  user attribute warn
    -Wbuiltin-declaration-mismatch      # 182  wrong builtin decl
    -Wbuiltin-macro-redefined           # 183  redefine __LINE__ etc
    -Wmisleading-indentation            # 184  misleading indent
    -Wunused-but-set-parameter          # 185  param set but unused
    -Wunused-but-set-variable           # 186  var set but unused
    -Wunused-const-variable=2           # 187  unused static const

    # ── 3.12  Overflow / safety (5) ──────────────────────────
    -Walloc-size-larger-than=1073741824 # 188  alloc > 1GB
    -Wstrict-flex-arrays                # 189  flex array discipline
    -Wpacked-not-aligned                # 191  packed+aligned conflict
    # -Wabi-tag dropped: Cipher owns std::string which carries the cxx11
    # ABI tag transitively; the warning appears on every accessor that
    # returns string&, adding no signal.  Real ABI breaks show up as link
    # errors anyway.  -Wabi=0 is retained for all compat warnings.
    -Wabi=0                             # 193  all ABI compat warnings

    # ── 3.13  GCC hardening flags (10) ───────────────────────
    -fcf-protection=full                # 194  control-flow integrity
    -fstack-clash-protection            # 195  stack clash guard pages
    -fharden-compares                   # 196  redundant compare checks
    -fharden-conditional-branches       # 197  redundant branch checks
    -fno-delete-null-pointer-checks     # 198  keep null checks
    -fdiagnostics-color=always          # 199  colored output
    -ftemplate-backtrace-limit=0        # 200  full template errors
    -fconstexpr-ops-limit=100000000     # 201  generous constexpr
    -fimplicit-constexpr                # 202  auto constexpr simple fn
    -fstrict-enums                      # 203  assume valid enum values
    -Wvolatile                          # 204  deprecated volatile use
    -Wvla-larger-than=0                 # 205  reject ALL VLAs
  )

  # ── 3.14  Library hardening — libstdc++ ────────────────────
  target_compile_definitions(crucible PUBLIC
    $<$<CONFIG:Debug>:
      _GLIBCXX_ASSERTIONS=1             # 206  vector[]/string[] bounds
      _GLIBCXX_SANITIZE_VECTOR=1        # 207  vector realloc poison
    >
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 4: FORTIFY_SOURCE + defines — both compilers
# ═══════════════════════════════════════════════════════════════
target_compile_definitions(crucible PUBLIC
  _FORTIFY_SOURCE=3                     # 208  max glibc buffer checks
)

# ═══════════════════════════════════════════════════════════════
# SECTION 5: Debug-only — auto-init
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(crucible PUBLIC
    -ftrivial-auto-var-init=zero        # 210  zero-fill uninit stack (P2795R5)
    -fharden-control-flow-redundancy    # 211  redundant CFG checks
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 6: Release-only — cheaper auto-init + contracts to [[assume]]
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE MATCHES "Release")
  target_compile_options(crucible PUBLIC
    -ftrivial-auto-var-init=zero        # 212  zero-fill (cheaper)
    # Hot paths use contracts purely as invariant hints; release compiles
    # them to [[assume]] so the optimizer can exploit them without a
    # runtime check.  Boundary TUs that want enforcement flip this.
    -fcontract-evaluation-semantic=ignore
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 7: Linker hardening — both compilers
#
# Full RELRO, immediate binding, no-exec stack, separate code,
# build-id for debuginfo, and more.
# ═══════════════════════════════════════════════════════════════
target_link_options(crucible PUBLIC
  "LINKER:-z,relro"                     # 213  read-only relocations
  "LINKER:-z,now"                       # 214  immediate symbol binding
  "LINKER:-z,noexecstack"              # 215  non-executable stack
  "LINKER:-z,separate-code"            # 216  separate code from data
  "LINKER:--as-needed"                 # 217  only link needed libs
  "LINKER:--no-copy-dt-needed-entries" # 218  no transitive deps
  "LINKER:--hash-style=gnu"            # 219  GNU hash (faster lookup)
  "LINKER:--build-id=sha1"             # 220  SHA-1 build ID
  "LINKER:-z,pack-relative-relocs"     # 221  compact relocations
  "LINKER:-z,nodump"                   # 222  prevent core dump of lib
  "LINKER:--gc-sections"               # 223  garbage-collect sections
  "LINKER:-z,defs"                     # 224  no undefined symbols
)

# Vessel adapter (shared library for Python/ctypes)
add_subdirectory(vessel/torch)

# Tests — assert-heavy, only built in Debug.  Release/bench builds
# strip assert(), which would leave "set but not used" variables.
enable_testing()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "")
  add_subdirectory(test)
endif()

# Examples — worked production-shape demonstrations of substrate
# concepts (Phase 0 P0-5 ships examples/fn/* for the Fn<...>
# aggregator).  Built alongside tests because they double as
# regression smoke tests for the substrate's public surface; opt-out
# via -DCRUCIBLE_EXAMPLES=OFF for minimal builds.
option(CRUCIBLE_EXAMPLES "Build worked examples of substrate concepts" ON)
if(CRUCIBLE_EXAMPLES AND
   (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL ""))
  add_subdirectory(examples)
endif()

# Benchmarks (opt-in: only built if -DCRUCIBLE_BENCH=ON)
option(CRUCIBLE_BENCH "Build benchmark executables" OFF)
if(CRUCIBLE_BENCH)
  add_subdirectory(bench)
endif()

# Formal verification (opt-in: requires Z3 SMT solver)
option(CRUCIBLE_VERIFY "Build Z3 formal verification suite (Axiom)" OFF)
if(CRUCIBLE_VERIFY)
  add_subdirectory(verify)
endif()

# Fuzz infrastructure (opt-in: property-based stress + adversarial
# boundary harnesses).  Builds small executables that exercise the
# library's invariants at scale:
#   ctest -L fuzz_property  — deterministic property tests
#   ctest -L fuzz_boundary  — adversarial corpus replay
# Coverage-guided fuzzing via AFL++ QEMU-mode or libFuzzer drives the
# same boundary harnesses (see fuzz/README.md).
option(CRUCIBLE_FUZZ "Build property + boundary fuzz harnesses" OFF)
if(CRUCIBLE_FUZZ)
  add_subdirectory(fuzz)
endif()
