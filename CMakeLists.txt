cmake_minimum_required(VERSION 3.27)

# ── ccache: default + always-used layer ─────────────────────────────
#
# Set BEFORE project() so the compiler-probe pass also runs through
# ccache.  The reflection-and-contracts build pulls cc1plus per heavy
# TU through 800 MB - 1 GB and several CPU-seconds.  ccache works
# across build directories (hash_dir=false) and uses a tuned
# sloppiness set in ~/.config/ccache/ccache.conf.  Hit rate on the
# dev box: 78%+ over 648K calls at this commit.
#
# The compiler identity that ccache hashes is set after project(), in
# the block that follows it.
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

# utils/scripts/check-no-ffast-math.py reads the link line of each target from the
# codemodel reply of the CMake file API, because the compile database holds
# no link flag.  The command is the reason for the CMake 3.27 minimum.
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

# The compiler must have each fix in utils/toolchain/gcc/patches.  A configure
# without cmake/Toolchain-gcc16.cmake takes the compiler that CMake finds,
# for example the stock Fedora GCC 16.2.1 behind /usr/lib64/ccache/c++,
# which lacks the fixes.  So every configure probes the compiler here, and a
# ccache link is probed through the compiler that ccache runs.  The two
# probes take about 35 ms.
include(cmake/PatchedGccProbe.cmake)
crucible_real_compiler("${CMAKE_CXX_COMPILER}" CRUCIBLE_REAL_CXX)
crucible_patched_gcc_problem("${CRUCIBLE_REAL_CXX}" CRUCIBLE_GCC_PROBLEM)
if(NOT CRUCIBLE_GCC_PROBLEM STREQUAL "")
  message(FATAL_ERROR "${CRUCIBLE_GCC_PROBLEM}")
endif()

if(CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
  # ccache with compiler_check=content hashes the driver binary only.
  # A compiler fix that changes cc1plus and not the driver, as the
  # fixes in utils/toolchain/gcc do, then returns objects that the unfixed
  # cc1plus made.  So the launcher passes ccache a compiler identity:
  # the SHA-256 of the driver and of the cc1plus that it runs.  The
  # two binaries are configure dependencies.  A reinstall of the
  # compiler makes the next build configure again, and the new
  # identity makes every object compile again.
  # The driver is the compiler that ccache runs, so a ccache link does not
  # put the ccache binary in the identity.
  execute_process(
      COMMAND "${CRUCIBLE_REAL_CXX}" -print-prog-name=cc1plus
      OUTPUT_VARIABLE CRUCIBLE_CC1PLUS
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE CRUCIBLE_CC1PLUS_STATUS)
  if(NOT CRUCIBLE_CC1PLUS_STATUS EQUAL 0 OR NOT IS_ABSOLUTE "${CRUCIBLE_CC1PLUS}"
     OR NOT EXISTS "${CRUCIBLE_CC1PLUS}")
    message(FATAL_ERROR
        "ccache: '${CRUCIBLE_REAL_CXX} -print-prog-name=cc1plus' did not "
        "name an existing file (it gave '${CRUCIBLE_CC1PLUS}').  Without "
        "that file ccache cannot tell one compiler build from another.  "
        "Fix the compiler installation, or configure with "
        "-DCRUCIBLE_USE_CCACHE=OFF.")
  endif()
  file(SHA256 "${CRUCIBLE_REAL_CXX}" CRUCIBLE_DRIVER_SHA256)
  file(SHA256 "${CRUCIBLE_CC1PLUS}" CRUCIBLE_CC1PLUS_SHA256)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${CRUCIBLE_REAL_CXX}" "${CRUCIBLE_CC1PLUS}")
  list(GET CMAKE_CXX_COMPILER_LAUNCHER 0 CRUCIBLE_CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CRUCIBLE_CCACHE}"
      "compiler_check=string:${CRUCIBLE_DRIVER_SHA256}-${CRUCIBLE_CC1PLUS_SHA256}")
  message(STATUS "ccache: ${CRUCIBLE_CCACHE} (compiler launcher enabled, cc1plus ${CRUCIBLE_CC1PLUS})")
elseif(CRUCIBLE_USE_CCACHE)
  message(STATUS "ccache: not found on PATH — proceeding without launcher")
endif()

# ═══════════════════════════════════════════════════════════════
# Profile-guided optimization: CRUCIBLE_PGO = off | generate | use
#
# One tree, one profile for each hardware tier, and no flag that names
# the hardware.  The tier is the name the compiler resolves native to
# (x86: znver5, sapphirerapids; aarch64: the -mcpu=native name).  A
# Zen 5 host and a Sapphire Rapids host collect and consume their own
# profile from the same commands.  The profile directory is
#
#     ${CRUCIBLE_PGO_DIR}/gcc-<major>.<minor>/<tier>/
#
# generate  Instruments every target that links crucible_dialect.  A
#           bench writes its counters into the profile directory when
#           it exits.  utils/scripts/pgo-bootstrap.sh drives this mode from
#           configure to the COLLECTION record.
#
# use       Reads the counters.  The profile must exist for this tier,
#           and its TARGET record must be equal to what this
#           configuration would write, or configuration stops.  A
#           function whose control flow changed after collection stops
#           the build with -Wcoverage-mismatch.  That is the GCC
#           default and it is correct: a build that claims a profile it
#           cannot apply is not a PGO build.  Collect again.
#
#           Every TU carries CRUCIBLE_PGO_PROFILE, the tier, time,
#           commit and tree state of the profile it was built with, and
#           the benches print it.  A binary can say which profile made
#           it.  A new collection changes the stamp, so every TU
#           recompiles against the new counters instead of only the
#           objects something else made stale.
#
#           CRUCIBLE_PGO_STRICT=ON adds the two release conditions: the
#           profile was collected at HEAD, from a clean tree.  A release
#           artifact is built with it, and the profile directory is
#           archived next to the artifact, because the counters are the
#           input that makes the binary what it is.
#
# This block sits at the head of the file because it clears the
# compiler launcher, and a target reads that variable when it is
# created.  ccache with base_dir rewrites the source path to a
# relative form, GCC folds the source path into every function's
# lineno checksum, and the rewrite is not the same when -fprofile-use
# is on the line.  The use build then reports "source locations have
# changed" for functions nobody touched.  Measured on this tree: the
# same compile without the launcher reports none.  A PGO build runs
# the compiler directly.  The flags this block computes are applied
# to crucible_dialect next to its other C++26 options.
#
# The generate build and the use build have different build
# directories.  GCC keys each counter file by the object path, so
# -fprofile-prefix-path strips each build's own root from that path.
# Without it the use build looks for keys the generate build never
# wrote.
#
# -fprofile-update=atomic, because the benches are threaded and a torn
# counter is an incorrect branch probability.  -fprofile-partial-
# training, because the profile comes from the bench suite: a function
# the suite did not run is optimized as usual, not shrunk as cold.
# -Wno-missing-profile for the same reason, because CRUCIBLE_WERROR
# would stop every test TU that has no counters.  -Wno-suggest-
# attribute=cold, because the profile is what makes the compiler sure
# a function is cold, and an attribute that repeats the profile adds
# nothing.
#
# crucible_perf links crucible_dialect through foundation, so the profile
# covers it as it covers every other target.
# ═══════════════════════════════════════════════════════════════
set(CRUCIBLE_PGO "off" CACHE STRING "Profile-guided optimization: off, generate or use")
set_property(CACHE CRUCIBLE_PGO PROPERTY STRINGS off generate use)
set(CRUCIBLE_PGO_DIR "${CMAKE_SOURCE_DIR}/pgo" CACHE PATH
  "Root of the profile directories, one for each compiler version and hardware tier")
option(CRUCIBLE_PGO_STRICT
  "With CRUCIBLE_PGO=use: the profile must be collected at HEAD from a clean tree" OFF)
set(_CRUCIBLE_PGO_COMPILE_FLAGS)
set(_CRUCIBLE_PGO_LINK_FLAGS)
set(_CRUCIBLE_PGO_COMPILE_DEFINITIONS)

if(NOT CRUCIBLE_PGO STREQUAL "off")
  if(NOT CRUCIBLE_PGO MATCHES "^(generate|use)$")
    message(FATAL_ERROR "CRUCIBLE_PGO is '${CRUCIBLE_PGO}'. The values are off, generate and use.")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "PGO is wired for GCC. The compiler is ${CMAKE_CXX_COMPILER_ID}.")
  endif()
  if(CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "PGO: the compiler launcher (${CMAKE_CXX_COMPILER_LAUNCHER}) is not used for this build")
  endif()
  set(CMAKE_CXX_COMPILER_LAUNCHER "")
  set(CMAKE_C_COMPILER_LAUNCHER "")

  # The tier is the name the compiler gives the silicon of this host.
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_crucible_pgo_native_flag -march=native)
    set(_crucible_pgo_tier_option "-march=")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_crucible_pgo_native_flag -mcpu=native)
    set(_crucible_pgo_tier_option "-mcpu=")
  else()
    message(FATAL_ERROR "PGO tier detection covers x86_64 and aarch64. "
      "CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}'.")
  endif()
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" ${_crucible_pgo_native_flag} -Q --help=target
    OUTPUT_VARIABLE _crucible_pgo_target_help
    ERROR_VARIABLE _crucible_pgo_target_error
    RESULT_VARIABLE _crucible_pgo_target_result)
  if(NOT _crucible_pgo_target_result EQUAL 0)
    message(FATAL_ERROR "PGO tier detection: "
      "'${CMAKE_CXX_COMPILER} ${_crucible_pgo_native_flag} -Q --help=target' failed:\n"
      "${_crucible_pgo_target_error}")
  endif()
  if(NOT _crucible_pgo_target_help MATCHES "\n[ \t]*${_crucible_pgo_tier_option}[ \t]+([^ \t\r\n]+)")
    message(FATAL_ERROR "PGO tier detection: the --help=target output has no "
      "'${_crucible_pgo_tier_option}' line with a value. The compiler did not resolve native to a name.")
  endif()
  set(_crucible_pgo_tier "${CMAKE_MATCH_1}")
  if(_crucible_pgo_tier STREQUAL "native")
    message(FATAL_ERROR "PGO tier detection: the compiler reports '${_crucible_pgo_tier_option}native' "
      "unresolved, so the tier has no name.")
  endif()
  # The tier name is for the directory and for the reader.  The pin is
  # the full target option set, which two hosts with the same name but
  # a different feature set do not share.
  string(SHA256 _crucible_pgo_fingerprint "${_crucible_pgo_target_help}")
  string(REGEX MATCH "^[0-9]+\\.[0-9]+" _crucible_pgo_gcc_version "${CMAKE_CXX_COMPILER_VERSION}")
  set(CRUCIBLE_PGO_TIER "${_crucible_pgo_tier}" CACHE INTERNAL "The hardware tier of this host")
  set(CRUCIBLE_PGO_PROFILE_DIR "${CRUCIBLE_PGO_DIR}/gcc-${_crucible_pgo_gcc_version}/${_crucible_pgo_tier}"
    CACHE INTERNAL "The profile directory this configuration writes or reads")

  # TARGET is what the generate configuration writes and what the use
  # configuration must find, byte for byte.
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _crucible_pgo_config)
  string(JOIN "\n" _crucible_pgo_target_record
    "compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    "processor=${CMAKE_SYSTEM_PROCESSOR}"
    "tier=${_crucible_pgo_tier}"
    "fingerprint=${_crucible_pgo_fingerprint}"
    "build_type=${CMAKE_BUILD_TYPE}"
    "cxx_flags=${CMAKE_CXX_FLAGS}"
    "cxx_flags_config=${CMAKE_CXX_FLAGS_${_crucible_pgo_config}}")
  string(APPEND _crucible_pgo_target_record "\n")

  if(CRUCIBLE_PGO STREQUAL "generate")
    file(MAKE_DIRECTORY "${CRUCIBLE_PGO_PROFILE_DIR}")
    file(WRITE "${CRUCIBLE_PGO_PROFILE_DIR}/TARGET" "${_crucible_pgo_target_record}")
    set(_CRUCIBLE_PGO_COMPILE_FLAGS
      -fprofile-generate=${CRUCIBLE_PGO_PROFILE_DIR}
      -fprofile-update=atomic
      -fprofile-prefix-path=${CMAKE_BINARY_DIR})
    set(_CRUCIBLE_PGO_LINK_FLAGS
      -fprofile-generate=${CRUCIBLE_PGO_PROFILE_DIR})
    message(STATUS "PGO: generate, tier ${_crucible_pgo_tier}, gcc ${_crucible_pgo_gcc_version}, "
      "profile directory ${CRUCIBLE_PGO_PROFILE_DIR}")
  else()
    if(NOT EXISTS "${CRUCIBLE_PGO_PROFILE_DIR}/TARGET")
      message(FATAL_ERROR "PGO: no profile for tier ${_crucible_pgo_tier} at ${CRUCIBLE_PGO_PROFILE_DIR}. "
        "Run utils/scripts/pgo-bootstrap.sh on a host of this tier, or set CRUCIBLE_PGO=off.")
    endif()
    file(READ "${CRUCIBLE_PGO_PROFILE_DIR}/TARGET" _crucible_pgo_recorded_target)
    if(NOT _crucible_pgo_recorded_target STREQUAL _crucible_pgo_target_record)
      message(FATAL_ERROR "PGO: the profile at ${CRUCIBLE_PGO_PROFILE_DIR} was collected for a different "
        "target. Recorded:\n${_crucible_pgo_recorded_target}\nThis configuration:\n${_crucible_pgo_target_record}"
        "Run utils/scripts/pgo-bootstrap.sh again with the flags you build with.")
    endif()
    file(GLOB _crucible_pgo_counter_files "${CRUCIBLE_PGO_PROFILE_DIR}/*.gcda")
    list(LENGTH _crucible_pgo_counter_files _crucible_pgo_counter_count)
    if(_crucible_pgo_counter_count EQUAL 0)
      message(FATAL_ERROR "PGO: ${CRUCIBLE_PGO_PROFILE_DIR} has a TARGET record and no counter files. "
        "The instrumented benches were configured and not run. Run utils/scripts/pgo-bootstrap.sh.")
    endif()
    # utils/scripts/pgo-bootstrap.sh rewrites COLLECTION on every run, so a
    # build after a collection reconfigures and the stamp below changes.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${CRUCIBLE_PGO_PROFILE_DIR}/TARGET"
      "${CRUCIBLE_PGO_PROFILE_DIR}/COLLECTION")
    set(_crucible_pgo_collected "unrecorded")
    set(_crucible_pgo_commit "unrecorded")
    set(_crucible_pgo_tree "unrecorded")
    if(EXISTS "${CRUCIBLE_PGO_PROFILE_DIR}/COLLECTION")
      file(STRINGS "${CRUCIBLE_PGO_PROFILE_DIR}/COLLECTION" _crucible_pgo_collected REGEX "^collected=")
      file(STRINGS "${CRUCIBLE_PGO_PROFILE_DIR}/COLLECTION" _crucible_pgo_commit REGEX "^commit=")
      file(STRINGS "${CRUCIBLE_PGO_PROFILE_DIR}/COLLECTION" _crucible_pgo_tree REGEX "^tree=")
      string(REGEX REPLACE "^collected=" "" _crucible_pgo_collected "${_crucible_pgo_collected}")
      string(REGEX REPLACE "^commit=" "" _crucible_pgo_commit "${_crucible_pgo_commit}")
      string(REGEX REPLACE "^tree=" "" _crucible_pgo_tree "${_crucible_pgo_tree}")
    endif()
    execute_process(
      COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
      OUTPUT_VARIABLE _crucible_pgo_head
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _crucible_pgo_head_result
      ERROR_QUIET)
    if(NOT _crucible_pgo_head_result EQUAL 0)
      set(_crucible_pgo_head "unknown")
    endif()
    if(CRUCIBLE_PGO_STRICT)
      if(NOT _crucible_pgo_commit STREQUAL _crucible_pgo_head)
        message(FATAL_ERROR "PGO strict: the profile was collected at ${_crucible_pgo_commit} and HEAD "
          "is ${_crucible_pgo_head}. A release profile is collected at the commit it ships with. "
          "Run utils/scripts/pgo-bootstrap.sh at HEAD, or configure without CRUCIBLE_PGO_STRICT.")
      endif()
      if(NOT _crucible_pgo_tree STREQUAL "clean")
        message(FATAL_ERROR "PGO strict: the profile was collected from a tree recorded as "
          "'${_crucible_pgo_tree}'. A release profile is collected from a clean tree, so that the "
          "counters describe the commit and nothing else. Commit or discard the changes, then run "
          "utils/scripts/pgo-bootstrap.sh again.")
      endif()
    elseif(NOT _crucible_pgo_commit STREQUAL _crucible_pgo_head)
      message(WARNING "PGO: the profile was collected at ${_crucible_pgo_commit} and HEAD is "
        "${_crucible_pgo_head}. A function whose control flow changed between the two stops the "
        "build with -Wcoverage-mismatch. Run utils/scripts/pgo-bootstrap.sh again to collect at HEAD.")
    endif()
    set(_CRUCIBLE_PGO_COMPILE_FLAGS
      -fprofile-use=${CRUCIBLE_PGO_PROFILE_DIR}
      -fprofile-prefix-path=${CMAKE_BINARY_DIR}
      -fprofile-partial-training
      -Wno-missing-profile
      -Wno-suggest-attribute=cold)
    # The same flags at link time, because LTO applies the profile there.
    # Without LTO the driver ignores them, so the day -flto lands in the
    # release preset, PGO stays applied.
    set(_CRUCIBLE_PGO_LINK_FLAGS
      -fprofile-use=${CRUCIBLE_PGO_PROFILE_DIR}
      -fprofile-prefix-path=${CMAKE_BINARY_DIR}
      -fprofile-partial-training)
    set(_CRUCIBLE_PGO_COMPILE_DEFINITIONS
      "CRUCIBLE_PGO_PROFILE=\"${_crucible_pgo_tier} ${_crucible_pgo_collected} ${_crucible_pgo_commit} ${_crucible_pgo_tree}\"")
    message(STATUS "PGO: use, tier ${_crucible_pgo_tier}, ${_crucible_pgo_counter_count} counter files "
      "collected ${_crucible_pgo_collected} at ${_crucible_pgo_commit} (${_crucible_pgo_tree} tree)")
  endif()
endif()

# The quarantine plugin of GCC: CRUCIBLE_QUARANTINE = OFF | REPORT | ERROR.
# It puts its flags on the directory, so it comes before the first target.
# It can clear the compiler launcher, as the PGO block does.
include(utils/tools/quarantine/Quarantine.cmake)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── The dialect: one INTERFACE target that carries the compiler ────
#
# Every flag that makes a TU "a Crucible TU" — the C++26 feature flags, the
# warning set, -Werror, hardening, FORTIFY, the release contract policy, the
# linker hardening, the FP floor and libstdc++exp — lives on this target and
# nowhere else.  `crucible` links it PUBLIC, so every consumer of `crucible`
# inherits it exactly as before.  `foundation` links it too, which is how the
# sibling substrate compiles under the same rules without depending on
# `crucible`.  The flag blocks below are unchanged in content; only the
# target they attach to moved.
add_library(crucible_dialect INTERFACE)

# The weak contract-violation handler and the failure arm of CRUCIBLE_PRE come
# from `foundation`, which crucible links below.  Downstream tests and benches
# need `handle_contract_violation` at link time, because GCC 16 libstdc++
# ships no default.  The weak symbol lets a production binary override it.
add_library(crucible STATIC
  src/canopy/Lifeguard.cpp
  src/cog/NumaNic.cpp
  src/cog/NicConfig.cpp
  src/cog/NicOffloadAudit.cpp
  src/cog/SrIov.cpp
  src/cntp/CongestionControl.cpp
  src/cntp/_wip/GpuDirect.cpp
  src/cntp/IncastControl.cpp
  src/cntp/MtlsTransport.cpp
  src/cntp/Pacing.cpp
  src/cntp/_wip/Sharp.cpp
  src/forge/recipes/Network.cpp
  src/observe/HdrHistogram.cpp
  src/observe/SdcDetect.cpp
  src/observe/SyntheticProbe.cpp
  src/cntp/dataplane/TcEbpf.cpp
  src/topology/CongestionTelemetry.cpp
  src/topology/Discovery.cpp
  src/topology/Pingmesh.cpp
  src/topology/Ptp.cpp
  src/topology/Telemetry.cpp
)
target_include_directories(crucible PUBLIC include/)
set_target_properties(crucible PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(crucible PUBLIC crucible_dialect)

# ── The sibling substrate: foundation/ and fixy/ ───────────────────
#
# include/foundation/ (namespace ::foundation) is the algebra and the
# discharge mechanisms; include/fixy/ (namespace ::fixy) is the axis table,
# the atoms, the gate and the OS leaves.  Neither names crucible:: —
# utils/scripts/check-layer-boundary.py enforces the order foundation < fixy <
# crucible.
#
# `foundation` is a static library for one reason: it owns the weak contract
# handler and the CRUCIBLE_PRE / CRUCIBLE_POST failure arm for every binary
# that links `foundation` or `fixy` without `crucible`.  `fixy` is header-only.
add_library(foundation STATIC
  src/foundation/ContractHandler.cpp
  src/foundation/DiagRuntime.cpp
)
target_include_directories(foundation PUBLIC include/)
target_link_libraries(foundation PUBLIC crucible_dialect)
set_target_properties(foundation PROPERTIES POSITION_INDEPENDENT_CODE ON)

# crucible owns no contract handler of its own.  Every crucible header that
# uses CRUCIBLE_PRE expands to the foundation failure arm, so crucible links
# foundation for it and for `handle_contract_violation`.  crucible_perf links
# foundation as well, so libfoundation.a also follows libcrucible_perf.a on a
# consumer's link line.
target_link_libraries(crucible PUBLIC foundation)

add_library(fixy INTERFACE)
target_link_libraries(fixy INTERFACE foundation)

# -Walloc-size-larger-than and libstdc++: the list that could not hold.
#
# Several libstdc++ helpers carry a sentinel branch that allocates
# max_size() bytes and throws.  `std::string::_M_create` is the one this
# tree meets most: `if (capacity > max_size()) __throw_length_error()`.
# At -O1 and above the optimizer inlines the helper, constant-propagates
# PTRDIFF_MAX into the allocator call, cannot prove the branch dead, and
# `-Walloc-size-larger-than=1073741824` fires on a path that never
# executes.  `std::stacktrace_entry::_Info::_S_set` does the same.
#
# This was a list of the three translation units that happened to inline
# it.  Which TU inlines a library helper is an optimizer decision, so the
# list was keyed on the optimizer: -O level, -march, and now the profile.
# Building with -fprofile-use added two entries it did not have
# (utils/tools/crucible_hwprobe.cpp and vessel/torch/vessel_api.cpp, through
# read_small_file, read_trimmed_ and select_hot_cpu), and the next
# collection would move them again.  A list that has to be re-derived
# whenever the optimizer changes its mind is not a list, so it is gone.
#
# The rule that replaced it is in the CRUCIBLE_WERROR block below:
# the warning stays on and stays visible, and it is not an error.

# Control-flow integrity is ISA-specific: x86 = CET (-fcf-protection),
# aarch64 = BTI+PAC (-mbranch-protection). -fcf-protection=full is a hard
# error on aarch64 ("not supported for this target"), so select per-arch.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(_CRUCIBLE_CFI_FLAGS -fcf-protection=full)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(_CRUCIBLE_CFI_FLAGS -mbranch-protection=standard)
else()
  set(_CRUCIBLE_CFI_FLAGS)
endif()

# cntp/Fec.h (off-limits to edit here) instantiates std::start_lifetime_as<const
# __m256i> inside its `#if defined(__AVX2__)` arm (lines ~292-355). libstdc++ 16
# implements start_lifetime_as with a writable inline-asm barrier (`"=m"(*p)`), which
# is a hard "read-only location used as 'asm' output" error whenever the target type
# is const-qualified — on BOTH stock and patched GCC 16, at every -O level. It only
# bites when __AVX2__ is defined: release/bench pass -march=native (CMAKE_CXX_FLAGS);
# default/tsan/analyzer omit it, so the AVX2 arm is #if'd out and the FEC tests compile
# against the scalar path. Probe the exact idiom under the active flags so the two tests
# that instantiate the AVX2 kernels are skipped ONLY where they genuinely cannot compile,
# and auto-re-enable if cntp/Fec.h ever adopts a const-safe load.
include(CheckCXXSourceCompiles)
set(_crucible_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++26")
check_cxx_source_compiles("
#include <immintrin.h>
#if !defined(__AVX2__)
int main() {}
#else
#include <memory>
int main() { const __m256i* p = nullptr; (void) std::start_lifetime_as<const __m256i>(p); }
#endif
" CRUCIBLE_FEC_CONST_SIMD_OK)
set(CMAKE_REQUIRED_FLAGS "${_crucible_saved_required_flags}")

# canopy/Hlc.h (off-limits to edit here) static_asserts
# std::atomic<unsigned __int128>::is_always_lock_free on every non-x86
# target and refuses to build otherwise — a hidden libstdc++ mutex
# fallback would tank now() latency on the canopy hot path.
#
# EMPIRICAL FINDING (CI run 26714227806, stock GCC 16 / ubuntu-24.04-arm):
# the property is FALSE for EVERY safe -march, including armv8.4-a (LSE2).
# Stock GCC 16 only reports a lock-free 128-bit atomic with FEAT_LSE128
# (armv9.4-a+lse128) — exactly what Hlc.h's own comment names ("AArch64
# without LSE-128"). The GitHub arm runner's CPU lacks LSE128 (else the
# -march=native candidate below would have passed), and forcing +lse128
# on non-LSE128 silicon would compile but SIGILL at runtime on the
# 128-bit RMW path (LSE128-only SWPP/LDCLRP). So no runtime-safe flag
# exists on this runner — and this probe is now a pure PERFORMANCE
# optimization, not a build gate: canopy/Hlc.h carries a spinlock-guarded
# fallback for exactly the is_always_lock_free == false case, so the
# build SUCCEEDS regardless. The probe only buys the faster lock-free
# now() path when a flag genuinely flips is_always_lock_free true (e.g.
# future LSE128 runners via -march=native, or toolchains where
# -mno-outline-atomics suffices). No-op on x86; strictly cannot regress.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(_crucible_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(_crucible_lockfree128_flags "")
  set(_crucible_lockfree128_idx 0)
  # Safest-first: (1) base-ISA inline LL/SC, runs on every aarch64;
  # (2) exact-runner match, guaranteed executable since build==run host;
  # (3) forced armv8.4-a LSE2 baseline as a last resort.
  foreach(_crucible_lf128_cand
            "-mno-outline-atomics"
            "-mno-outline-atomics -march=native"
            "-mno-outline-atomics -march=armv8.4-a")
    set(CMAKE_REQUIRED_FLAGS "-std=c++26 ${_crucible_lf128_cand}")
    check_cxx_source_compiles("
#include <atomic>
#include <cstdint>
static_assert(std::atomic<unsigned __int128>::is_always_lock_free,
              \"need a lock-free 128-bit atomic\");
int main() {}
" CRUCIBLE_AARCH64_LOCKFREE128_${_crucible_lockfree128_idx})
    if(CRUCIBLE_AARCH64_LOCKFREE128_${_crucible_lockfree128_idx})
      set(_crucible_lockfree128_flags "${_crucible_lf128_cand}")
      break()
    endif()
    math(EXPR _crucible_lockfree128_idx "${_crucible_lockfree128_idx} + 1")
  endforeach()
  set(CMAKE_REQUIRED_FLAGS "${_crucible_saved_required_flags}")
  if(_crucible_lockfree128_flags STREQUAL "")
    message(STATUS
      "aarch64: no safe flag yields a lock-free 128-bit atomic (runner "
      "lacks FEAT_LSE128); canopy/Hlc.h uses its spinlock-guarded "
      "fallback — build succeeds, now() takes the locked path here.")
  else()
    # Plain codegen flags (no -std dependency) → safe in CMAKE_CXX_FLAGS;
    # global so every TU that transitively includes Hlc.h is covered,
    # including the standalone neg-compile fixtures that don't link crucible.
    string(APPEND CMAKE_CXX_FLAGS " ${_crucible_lockfree128_flags}")
    message(STATUS
      "aarch64 lock-free 128-bit atomic enabled via: "
      "${_crucible_lockfree128_flags}")
  endif()
endif()

# C++26-only flags — applied at target level where -std=c++26 is active
# (applying them in CMAKE_CXX_FLAGS would poison CMake's compiler-probe).
target_compile_options(crucible_dialect INTERFACE
  -freflection
  -fcontracts
)

# CRUCIBLE_CONTRACT_IGNORE_OPTIONS: the one list that takes a translation unit
# off contract checks.  It sets the flag for the language clauses and the
# define for CRUCIBLE_PRE and CRUCIBLE_POST.  The check at the foot of this
# file rejects a scope that has one of the two items without the other.
include(cmake/ContractSemantic.cmake)

# The crucible_fp_strict INTERFACE library defines THE FLOOR
# for FP discipline across every consumer of `crucible`.  PUBLIC link
# propagates the strict-IEEE-754 flag pack to every test / bench /
# vessel / tool / example TU.  See cmake/FpStrict.cmake for the
# per-flag rationale.  Deliberate opt-out (bench micro-benchmarks
# that want fast-math for measurement) is review-discoverable: the
# target deliberately does NOT link `crucible_fp_strict` and the
# author documents the engagement.
include(cmake/FpStrict.cmake)
target_link_libraries(crucible_dialect INTERFACE crucible_fp_strict)

add_executable(crucible_diag_lsp
  utils/tools/lsp_code_action_plugin/crucible_diag_lsp.cpp
)
target_link_libraries(crucible_diag_lsp PRIVATE crucible)

# The cross-build witness for the row-hash fold of foundation/diag/RowHash.h.
# The fold is one fold over the graded shape.  The tool prints each row hash
# as a labeled hex line, and a test diffs the output against
# utils/tools/row_hash_golden.txt.  A regression in display_string_of (the
# upstream of stable_type_id) that depends on the TU context cannot move a
# hash unseen.  Every graded wrapper folds a reflected lattice name, so
# every wrapper is exposed.
#
# It links fixy rather than crucible.  A witness against TU-context
# fragility must not carry a TU context that no production build runs.
add_executable(dump_row_hashes
  utils/tools/dump_row_hashes.cpp
)
target_link_libraries(dump_row_hashes PRIVATE fixy)

# The one-shot writer for the hardware-capability ledger.
#
# Fingerprints the host, judges whether the host is fit to measure
# anything, measures whatever the ledger is missing or has let go stale,
# and commits to $XDG_CACHE_HOME/crucible/hwledger.  The runtime reads
# that file through crucible::ledger::mint_ledger_view and never
# measures on its own.
#
# It reaches into bench/ for bench_harness.h rather than carrying its
# own timing rig: the harness already reports the quantiles, the
# within-run coefficient of variation and the drift check that a ledger
# evidence record wants.  That is an include path only — the tool is not
# a bench target, is built in every configuration, and does not take the
# bench tree's -O3 -march=native.
add_executable(crucible_hwprobe
  utils/tools/crucible_hwprobe.cpp
)
target_link_libraries(crucible_hwprobe PRIVATE crucible)
target_include_directories(crucible_hwprobe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bench)
set_target_properties(crucible_hwprobe PROPERTIES OUTPUT_NAME crucible-hwprobe)

# ── eBPF observability substrate (crucible::perf::SenseHub etc.) ────
#
# Promoted from bench/ on 2026-05-03.  When libbpf + a
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

# ─── BTF-typed tracepoint variants (shipped 2026-05-04) ───────────────
#
# tp_btf/sched_switch + tp_btf/sys_enter + tp_btf/sys_exit programs.
# Wire-equivalent to their legacy `tracepoint/...` siblings: same
# `TimelineSchedEvent` / `TimelineSyscallEvent` 32-byte struct layouts,
# same `BPF_F_MMAPABLE` ring-buffer publishing discipline, same
# `our_tids` / `target_tgid` filter shape.  Differences:
#
#   • SEC("tp_btf/...") instead of SEC("tracepoint/...").  ~30% lower
#     per-event cost (skip tracepoint format-string parse).  Requires
#     CONFIG_DEBUG_INFO_BTF=y AND kernel ≥ 5.5.
#
#   • The cost-discipline fixes of the first audit carry forward
#     (2026-05-04): BPF_MAP_TYPE_LRU_HASH (not HASH) for in-flight
#     maps, single bpf_ktime_get_ns() per event, compiler barrier
#     before the ts_ns completion-marker store.  Same as
#     SchedSwitch and SyscallLatency.
#
# Userspace facades: include/crucible/perf/SchedTpBtf.h and
# SyscallTpBtf.h (mirror SchedSwitch.h and SyscallLatency.h surfaces).
crucible_bpf_program(sched_tp_btf     include/crucible/perf/bpf/sched_tp_btf.bpf.c)
crucible_bpf_program(syscall_tp_btf   include/crucible/perf/bpf/syscall_tp_btf.bpf.c)

# ─── Subsystem-owned BPF stubs ─────────────────────────────────────
#
# These are intentionally rooted in the owning subsystem rather than
# perf/: CNT-P dataplane owns TC/XDP contracts; Warden owns enforcement
# lifecycle hooks; canopy/topology/observe/cog own their mesh and NIC evidence.
# Current programs are verifier-loadable no-op/pass-through stubs that pin
# section names, map wire contracts, and embedded symbol names before policy
# grows around them.
crucible_bpf_program(cntp_dp_tc_ingress_classify
  include/crucible/cntp/dataplane/bpf/dp_tc_ingress_classify.bpf.c)
crucible_bpf_program(cntp_dp_tc_egress_classify
  include/crucible/cntp/dataplane/bpf/dp_tc_egress_classify.bpf.c)
crucible_bpf_program(cntp_dp_pacing_budget
  include/crucible/cntp/dataplane/bpf/dp_pacing_budget.bpf.c)
crucible_bpf_program(cntp_dp_queue_backlog
  include/crucible/cntp/dataplane/bpf/dp_queue_backlog.bpf.c)
crucible_bpf_program(warden_socket_lifecycle
  include/crucible/warden/bpf/wd_socket_lifecycle.bpf.c)
crucible_bpf_program(cntp_dp_fq_budget
  include/crucible/cntp/dataplane/bpf/dp_fq_budget.bpf.c)

crucible_bpf_program(cntp_xdp_flow_filter
  include/crucible/cntp/bpf/xdp_flow_filter.bpf.c)
crucible_bpf_program(cntp_af_xdp_redirect
  include/crucible/cntp/bpf/af_xdp_redirect.bpf.c)
crucible_bpf_program(cntp_tc_egress_mark
  include/crucible/cntp/bpf/tc_egress_mark.bpf.c)
crucible_bpf_program(cntp_sk_msg_credit_gate
  include/crucible/cntp/bpf/sk_msg_credit_gate.bpf.c)
crucible_bpf_program(cntp_sock_ops_telemetry
  include/crucible/cntp/bpf/sock_ops_telemetry.bpf.c)
crucible_bpf_program(cntp_wip_fq_pacing_sample
  include/crucible/cntp/_wip/bpf/fq_pacing_sample.bpf.c)
crucible_bpf_program(cntp_ecn_ce_mark_sample
  include/crucible/cntp/bpf/ecn_ce_mark_sample.bpf.c)
crucible_bpf_program(cntp_wip_bbr_model_sample
  include/crucible/cntp/_wip/bpf/bbr_model_sample.bpf.c)
crucible_bpf_program(cntp_wip_ack_clock_sample
  include/crucible/cntp/_wip/bpf/ack_clock_sample.bpf.c)
crucible_bpf_program(cntp_wip_loss_recovery_sample
  include/crucible/cntp/_wip/bpf/loss_recovery_sample.bpf.c)

crucible_bpf_program(canopy_gossip_xdp_tx
  include/crucible/canopy/bpf/gossip_xdp_tx.bpf.c)
crucible_bpf_program(canopy_peer_liveness_probe
  include/crucible/canopy/bpf/peer_liveness_probe.bpf.c)
crucible_bpf_program(canopy_gossip_fanout_sample
  include/crucible/canopy/bpf/gossip_fanout_sample.bpf.c)

crucible_bpf_program(topology_xdp_rx_timestamp_probe
  include/crucible/topology/bpf/xdp_rx_timestamp_probe.bpf.c)
crucible_bpf_program(topology_napi_pressure_probe
  include/crucible/topology/bpf/napi_pressure_probe.bpf.c)
crucible_bpf_program(topology_qdisc_backlog_probe
  include/crucible/topology/bpf/qdisc_backlog_probe.bpf.c)
crucible_bpf_program(topology_lldp_rx_sample
  include/crucible/topology/bpf/lldp_rx_sample.bpf.c)

crucible_bpf_program(observe_tcp_flow_sample
  include/crucible/observe/bpf/tcp_flow_sample.bpf.c)
crucible_bpf_program(observe_packet_loss_sample
  include/crucible/observe/bpf/packet_loss_sample.bpf.c)
crucible_bpf_program(observe_ecn_sample
  include/crucible/observe/bpf/ecn_sample.bpf.c)

crucible_bpf_program(cog_nic_cog_probe
  include/crucible/cog/bpf/nic_cog_probe.bpf.c)
crucible_bpf_program(cog_offload_cap_probe
  include/crucible/cog/bpf/offload_cap_probe.bpf.c)

# ─── SenseHub v2 — opt-in dual-tier hub (basic 128 + extended 256) ───
#
# v2 ships the architectural surface (counters/gauges segregation,
# meta-versioned wire contract, /proc gauge poller) alongside v1.  v1
# remains the active production loader during the transition; v2's
# load() impl will land in subsequent PRs as the v1-handler-carry-
# forward + extended-handler bodies fill in.  See:
#   include/crucible/perf/bpf/sense_hub_v2.bpf.c
#   include/crucible/perf/SenseHubV2.h
#   include/crucible/perf/ProcGauges.h
#
# CRUCIBLE_SENSE_HUB_V2  (default OFF)   — compile v2 BPF + userspace
#                                          stub .cpp; doesn't replace v1
# CRUCIBLE_SENSE_HUB_EXTENDED (default OFF) — flip v2 to 256/64 debug
#                                             hub at compile time.
#                                             Has no effect unless
#                                             CRUCIBLE_SENSE_HUB_V2=ON.
option(CRUCIBLE_SENSE_HUB_V2
  "Compile SenseHub v2 (counters+gauges segregated, meta-versioned).
   Default OFF — v1 remains the active production loader." OFF)
option(CRUCIBLE_SENSE_HUB_EXTENDED
  "Use 256-counter / 64-gauge SenseHub v2 'debug hub' instead of
   128/32 default.  Requires CRUCIBLE_SENSE_HUB_V2=ON." OFF)

if (CRUCIBLE_SENSE_HUB_V2)
  if (CRUCIBLE_SENSE_HUB_EXTENDED)
    set(_CRUCIBLE_SENSE_HUB_V2_FLAGS "-DCRUCIBLE_SENSE_HUB_EXTENDED=1")
    target_compile_definitions(crucible PUBLIC CRUCIBLE_SENSE_HUB_EXTENDED=1)
  else()
    set(_CRUCIBLE_SENSE_HUB_V2_FLAGS "")
  endif()
  crucible_bpf_program(sense_hub_v2
    include/crucible/perf/bpf/sense_hub_v2.bpf.c
    EXTRA_FLAGS ${_CRUCIBLE_SENSE_HUB_V2_FLAGS})
endif()

if (CRUCIBLE_HAVE_BPF)
  get_property(_CRUCIBLE_BPF_EMBED_SOURCES
    GLOBAL PROPERTY CRUCIBLE_BPF_EMBED_SOURCES)
  set(_CRUCIBLE_PERF_SOURCES
    src/perf/SenseHub.cpp
    src/perf/SchedSwitch.cpp
    src/perf/PmuSample.cpp
    src/perf/LockContention.cpp
    src/perf/SyscallLatency.cpp
    src/perf/SchedTpBtf.cpp
    src/perf/SyscallTpBtf.cpp
    src/perf/Senses.cpp)
  if (CRUCIBLE_SENSE_HUB_V2)
    list(APPEND _CRUCIBLE_PERF_SOURCES
      src/perf/SenseHubV2.cpp
      src/perf/ProcGauges.cpp)
  endif()
  add_library(crucible_perf STATIC
    ${_CRUCIBLE_PERF_SOURCES}
    ${_CRUCIBLE_BPF_EMBED_SOURCES})
  target_include_directories(crucible_perf PUBLIC include/)
  # crucible_perf links foundation, and foundation links crucible_dialect,
  # so the perf sources compile under the dialect of every other target:
  # the warning set, -Werror, the hardening flags, the FP floor and the
  # contract semantic of the build.  The link also puts libfoundation.a
  # after libcrucible_perf.a on every consumer's link line, so the
  # contract handler that the perf contract clauses call resolves.  The
  # generated BPF byte arrays compile with -w (cmake/CrucibleBpf.cmake).
  target_link_libraries(crucible_perf PUBLIC PkgConfig::LIBBPF foundation)
  if (CRUCIBLE_SENSE_HUB_V2 AND CRUCIBLE_SENSE_HUB_EXTENDED)
    target_compile_definitions(crucible_perf PRIVATE CRUCIBLE_SENSE_HUB_EXTENDED=1)
  endif()
  set_target_properties(crucible_perf PROPERTIES POSITION_INDEPENDENT_CODE ON)
  # PUBLIC link so symbols transit to the final binary; consumers
  # that include <crucible/perf/SenseHub.h> get the loader symbols
  # at link time without an extra `target_link_libraries` call.
  target_link_libraries(crucible PUBLIC crucible_perf)
  target_compile_definitions(crucible PUBLIC CRUCIBLE_HAVE_BPF=1)
else()
  target_compile_definitions(crucible PUBLIC CRUCIBLE_HAVE_BPF=0)
endif()

# ── The Vigil runtime: src/Vigil.cpp and src/BackgroundThread.cpp ──
#
# The cold bodies of crucible/Vigil.h and crucible/BackgroundThread.h: the
# construction of a Vigil, the background pipeline, the trace graph build
# and the memory plan.  A translation unit that includes the two headers
# then compiles no part of the pipeline.
#
# These bodies are a library of their own, and libcrucible.a does not hold
# them.  The pipeline starts its threads through std::jthread, and the
# constructor opens the store through std::string and std::filesystem.
# Those instantiations define the virtual table of std::thread::_State_impl,
# and the std::set of fixy/concurrent/Topology.h references __cxa_throw and
# __cxa_rethrow.  The check of utils/scripts/check-no-throw-no-rtti.sh reads
# this library too.  It admits the throw of a failed allocation, and
# utils/scripts/no-throw-no-rtti-allowlist.txt admits the rethrow and the
# virtual table, each with its reason.
#
# The bodies compile with the definitions that crucible gives its
# consumers, so each header reads the same macros in both places.
add_library(crucible_vigil STATIC
  src/BackgroundThread.cpp
  src/Vigil.cpp)
target_include_directories(crucible_vigil PUBLIC include/)
target_compile_definitions(crucible_vigil PRIVATE $<TARGET_PROPERTY:crucible,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(crucible_vigil PUBLIC foundation)
if (CRUCIBLE_HAVE_BPF)
  target_link_libraries(crucible_vigil PUBLIC crucible_perf)
endif()
set_target_properties(crucible_vigil PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(crucible PUBLIC crucible_vigil)

# ── The Cipher store: src/Cipher.cpp ──
#
# The members of crucible/Cipher.h that read or write files, and the members
# that encode a region or a session-event batch.  A translation unit that
# includes Cipher.h then parses no stream, no region serializer and no
# federation protocol.
#
# The store is a library of its own, as the Vigil runtime above is.  Its
# streams, std::filesystem and its resident cache hold the throws of a failed
# allocation, which utils/scripts/check-no-throw-no-rtti.sh reads in each
# static library and admits.  The Vigil runtime opens the store, so it links
# this library.
add_library(crucible_cipher STATIC src/Cipher.cpp)
target_include_directories(crucible_cipher PUBLIC include/)
target_compile_definitions(crucible_cipher PRIVATE $<TARGET_PROPERTY:crucible,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(crucible_cipher PUBLIC foundation)
set_target_properties(crucible_cipher PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(crucible_vigil PUBLIC crucible_cipher)
target_link_libraries(crucible PUBLIC crucible_cipher)

# ── The trace visualizer: src/vis/ ──
#
# The block detector, the graph layout and the SVG renderer of
# include/crucible/vis/.  A translation unit that includes a vis header then
# compiles no layout and no renderer.  The bodies build std::string,
# std::vector and std::unordered_map values, so they hold the throws of a
# failed allocation, which utils/scripts/check-no-throw-no-rtti.sh admits.
add_library(crucible_vis STATIC
  src/vis/BlockDetector.cpp
  src/vis/NetworkSimplex.cpp
  src/vis/SugiyamaLayout.cpp
  src/vis/SvgRenderer.cpp
  src/vis/TraceVisualizer.cpp)
target_include_directories(crucible_vis PUBLIC include/)
target_compile_definitions(crucible_vis PRIVATE $<TARGET_PROPERTY:crucible,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(crucible_vis PUBLIC foundation)
set_target_properties(crucible_vis PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(crucible PUBLIC crucible_vis)

# `libstdc++exp.a` holds the parts of libstdc++ 16 that libstdc++.so does
# not have: the contract runtime, and parts of <print>, <debugging>,
# <stacktrace> and <text_encoding>.  Each consumer of the dialect links it,
# so a test binary, a vessel adapter or a bench TU that names one of them
# links without a target_link_libraries call of its own.
#
# The contract runtime includes a weak default handle_contract_violation.
# The weak handler of src/foundation/ContractHandler.cpp wins over it,
# because an object file with a contract check names handle_contract_violation
# under each semantic that does a check.  So the linker takes the foundation
# handler from libfoundation.a before it reads this archive.
target_link_libraries(crucible_dialect INTERFACE stdc++exp)

# No target sets -fno-ipa-modref.  A benchmark got wrong code from the
# inline asm `"+m,r"` of its do_not_optimize helper.  The flag removed
# the symptom from a small reproducer, but not from the benchmark.  GCC
# PR124958 found the defect in that asm, not in GCC.  do_not_optimize in
# bench/bench_harness.h is an empty [[gnu::noipa]] function with no
# inline asm, so no flag is necessary.

# ╔═══════════════════════════════════════════════════════════════════╗
# ║  CRUCIBLE HARDENING — 300+ compiler/linker/sanitizer flags       ║
# ║                                                                   ║
# ║  GCC 16:   200+ individual warnings + contracts + reflection     ║
# ║            (only supported compiler — see CLAUDE.md, §I)          ║
# ║  +         sanitizers, linker hardening, FORTIFY, library checks ║
# ║                                                                   ║
# ║  "Every nanosecond matters. Every byte is accounted for.         ║
# ║   Every type is strong. Every pointer is checked."               ║
# ╚═══════════════════════════════════════════════════════════════════╝

option(CRUCIBLE_WERROR "Treat warnings as errors" OFF)
option(CRUCIBLE_ANALYZER
  "Enable GCC -fanalyzer for crucible and all targets that consume it (slow, opt-in)" OFF)

if(CRUCIBLE_ANALYZER)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "CRUCIBLE_ANALYZER requires GCC because -fanalyzer is GCC-only")
  endif()
  message(STATUS "CRUCIBLE_ANALYZER=ON — GCC -fanalyzer enabled for crucible consumers")
endif()

# ── Sanitizer and verify builds (test presets only) ────────────────
#
# CRUCIBLE_SANITIZE (the default preset) puts ASan on every target of a
# Debug build, and UBSan on the test executables.  CRUCIBLE_TSAN (the tsan
# preset) puts ThreadSanitizer on every target of a Debug build.  A shared
# library built under ASan or ThreadSanitizer loads only after the runtime,
# so the Python vessel loads the libraries of the release preset.
#
# CRUCIBLE_UBSAN_STRICT removes -fno-strict-overflow and puts the full
# UBSan set on every target.  -fno-strict-overflow defines signed overflow
# as wrapping.  In the other presets, the signed-integer-overflow check of
# UBSan cannot find an overflow.  This preset finds each wrapping signed
# add, and each report from it is a real defect.
#
# CRUCIBLE_VERIFY builds Release code with the contract families armed.
# The P2900 clauses, CRUCIBLE_PRE and CRUCIBLE_POST operate under
# `enforce`.  The preset also removes NDEBUG, which arms
# CRUCIBLE_INVARIANT and CRUCIBLE_DEBUG_ASSERT.  The build shows how the
# optimizer changes a checked program.
option(CRUCIBLE_SANITIZE
  "Put ASan on every target and UBSan on the tests in a Debug build" ON)
option(CRUCIBLE_TSAN
  "Put ThreadSanitizer on every target in a Debug build (not with CRUCIBLE_SANITIZE)" OFF)
option(CRUCIBLE_UBSAN_STRICT
  "Put the full UBSan set on every target, with signed overflow undefined (test preset)" OFF)
option(CRUCIBLE_VERIFY
  "Release code generation with the contract families armed (test preset)" OFF)
if(CRUCIBLE_TSAN AND CRUCIBLE_SANITIZE)
  message(FATAL_ERROR "CRUCIBLE_TSAN and CRUCIBLE_SANITIZE are separate presets: ThreadSanitizer cannot share a process with ASan")
endif()
if(CRUCIBLE_UBSAN_STRICT AND (CRUCIBLE_SANITIZE OR CRUCIBLE_TSAN))
  message(FATAL_ERROR "CRUCIBLE_UBSAN_STRICT is its own preset: set CRUCIBLE_SANITIZE and CRUCIBLE_TSAN to OFF")
endif()
if(CRUCIBLE_UBSAN_STRICT AND CRUCIBLE_ANALYZER)
  message(FATAL_ERROR "CRUCIBLE_UBSAN_STRICT and CRUCIBLE_ANALYZER are separate presets")
endif()

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
# linked binary, with all kept symbols.  No build links with LTO, so
# the code of each function is the code of its object file.
# Default OFF — every objdump invocation adds 100s of ms; opt-in for
# asm-comparison.
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
# -O1 over -Og or -O0:
#   * inlining is enabled (`-finline-functions`).  This is load-
#     bearing for the safety-wrapper substrate — Linear / Refined /
#     Tagged / Stale / Graded only collapse to zero cost when their
#     accessor templates inline.  At -Og only `-finline-functions-
#     called-once` is on, leaving the wrappers as real function
#     calls; runtime tests that exercise wrapper-heavy paths run
#     2-5× slower for the same compile cost.
#   * compile time is within a few percent of -Og — the project's
#     bottleneck is template instantiation in the C++ frontend, not
#     codegen, so the optimization-level dial barely moves it.
#   * sanitizers (ASan, UBSan, TSan) and contracts (P2900R14) are
#     unaffected by -O level.
#   * _FORTIFY_SOURCE=3 still fires at -O1 (it requires any -O>=1).
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_FLAGS_DEBUG "-g -O1" CACHE STRING "" FORCE)
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 1: Common flags — GCC 16
#            52 warning flags + 13 hardening flags = 65
# ═══════════════════════════════════════════════════════════════
target_compile_options(crucible_dialect INTERFACE
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
  $<$<NOT:$<BOOL:${CRUCIBLE_UBSAN_STRICT}>>:-fno-strict-overflow>  #  56  signed overflow is UB→defined, but not under strict UBSan
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
  #
  # -Wno-error=alloc-size-larger-than: every instance this tree has ever
  # seen is a libstdc++ sentinel branch — `_M_create`'s length-error
  # path, `_S_set`'s stacktrace string — inlined at -O1+ with PTRDIFF_MAX
  # constant-propagated into the allocator call, on a path that never
  # runs.  The diagnostic points into bits/new_allocator.h, not into our
  # source.  The block near the top of this file says why the per-TU
  # suppression list this replaces could not be kept correct.
  #
  # What this gives up, stated: an allocation of more than 1 GiB in our
  # own code warns and no longer stops the build.  It is still reported,
  # at our own source location and with our own value rather than
  # PTRDIFF_MAX, so it stays greppable in a build log.  The tree's real
  # allocation discipline is elsewhere — the arena bump allocator, HS10's
  # no-allocation-on-the-hot-path rule, and std::*_sat at every size-math
  # site — and none of it depends on this warning being fatal.
  target_compile_options(crucible_dialect INTERFACE
    $<$<NOT:$<BOOL:$<TARGET_PROPERTY:CRUCIBLE_NO_WERROR>>>:-Werror
      -Wno-error=tsan
      -Wno-error=suggest-attribute=cold
      # The trailing `=` is part of the option name.  GCC rejects
      # `-Wno-error=alloc-size-larger-than` outright.
      -Wno-error=alloc-size-larger-than=>)
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 2: GCC 16 — MAXIMUM PARANOIA
#            220+ individual warning / hardening flags
# ═══════════════════════════════════════════════════════════════
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(crucible_dialect INTERFACE
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
    # foundation::simd uses over-wide [[gnu::vector_size]] vectors (e.g. the
    # logical 64-byte i64x8) that GCC lowers to the target's native width.
    # Returning/passing such a vector by value when the TU's -march is
    # narrower than the vector triggers -Wpsabi ("vector ABI changed").
    # This is benign: every TU in one binary is compiled with the same
    # flags, so the ABI is internally consistent (libstdc++'s own <simd>
    # relies on the identical suppression).  Without this, the no-march
    # default/tsan/analyzer presets fail on the 64-byte select() return.
    -Wno-psabi                          # 193b portable wide-vector facade

    # ── 3.13  GCC hardening flags (11) ───────────────────────
    # A Debug build does not take 196 and 197.  The two flags defend a
    # binary against fault injection, and they find no defect in a test.
    # On a heavy translation unit they cost 20% to 46% of the back end.
    ${_CRUCIBLE_CFI_FLAGS}              # 194  control-flow integrity (x86 CET / arm BTI+PAC)
    -fstack-clash-protection            # 195  stack clash guard pages
    $<$<NOT:$<CONFIG:Debug>>:-fharden-compares>              # 196  redundant compare checks
    $<$<NOT:$<CONFIG:Debug>>:-fharden-conditional-branches>  # 197  redundant branch checks
    -fno-delete-null-pointer-checks     # 198  keep null checks
    -fdiagnostics-color=always          # 199  colored output
    -ftemplate-backtrace-limit=0        # 200  full template errors
    -fconstexpr-ops-limit=100000000     # 201  generous constexpr
    -fstrict-enums                      # 203  assume valid enum values
    -Wvolatile                          # 204  deprecated volatile use
    -Wvla-larger-than=0                 # 205  reject ALL VLAs

    # ── 3.14  Source integrity / C++ template tripwires (20) ─────
    -Wbidi-chars=any                    # 206  reject bidi-control source spoofing
    -Wnormalized=nfkc                   # 207  Unicode identifiers normalize safely
    -Winvalid-utf8                      # 208  malformed UTF-8 in source
    -Wdate-time                         # 209  nondeterministic __DATE__/__TIME__
    -Wmissing-requires                  # 210  likely missing requires keyword
    -Wmissing-template-keyword          # 211  dependent template call ambiguity
    -Wcomma-subscript                   # 212  deprecated comma in subscript
    -Wdeprecated-enum-float-conversion  # 213  enum/float arithmetic drift
    -Wconversion-null                   # 214  NULL converted to non-pointer
    -Wdefaulted-function-deleted        # 215  defaulted function is deleted
    -Wdelete-incomplete                 # 216  delete incomplete type
    -Winit-list-lifetime                # 217  initializer_list lifetime hazards
    -Winfinite-recursion                # 218  provable infinite recursion
    -Winvalid-constexpr                 # 219  invalid constexpr path
    -Wreorder                           # 220  ctor init order differs from members
    -Wdangling-pointer=2                # 221  dangling pointer dataflow
    -Waligned-new=all                   # 222  overaligned allocation mismatch
    -Wmismatched-dealloc                # 223  malloc/free family mismatch
    -Wmaybe-musttail-local-addr         # 224  musttail with local address hazard
    -Wconditionally-supported           # 225  conditionally-supported C++ construct
  )

  # ── 3.15  Library hardening — libstdc++ ────────────────────
  target_compile_definitions(crucible_dialect INTERFACE
    $<$<CONFIG:Debug>:
      _GLIBCXX_ASSERTIONS=1             # 226  vector[]/string[] bounds
      _GLIBCXX_SANITIZE_VECTOR=1        # 227  vector realloc poison
    >
  )

  # ── 3.16  GCC static analyzer (opt-in, slow) ─────────────────
  #
  # Applied PUBLICly to `crucible` so the header-heavy runtime is
  # analyzed through real consumers: tests, Vessel, examples, fuzzers,
  # tools, and benches when those trees are enabled.  The analyzer
  # preset disables sanitizers and benches by default; use it as a
  # diagnostic pass, not as the normal fast edit/build loop.
  if(CRUCIBLE_ANALYZER)
    set(CRUCIBLE_ANALYZER_FLAGS
      -fanalyzer
      -Wanalyzer-double-free
      -Wanalyzer-double-fclose
      -Wanalyzer-free-of-non-heap
      -Wanalyzer-null-dereference
      -Wanalyzer-possible-null-dereference
      -Wanalyzer-null-argument
      -Wanalyzer-possible-null-argument
      -Wanalyzer-use-after-free
      -Wanalyzer-malloc-leak
      -Wanalyzer-file-leak
      -Wanalyzer-fd-leak
      -Wanalyzer-fd-use-after-close
      -Wanalyzer-fd-double-close
      -Wanalyzer-fd-access-mode-mismatch
      -Wanalyzer-fd-type-mismatch
      -Wanalyzer-fd-phase-mismatch
      -Wanalyzer-fd-use-without-check
      -Wanalyzer-tainted-array-index
      -Wanalyzer-tainted-allocation-size
      -Wanalyzer-tainted-divisor
      -Wanalyzer-tainted-offset
      -Wanalyzer-tainted-size
      -Wanalyzer-tainted-assertion
      -Wanalyzer-shift-count-negative
      -Wanalyzer-shift-count-overflow
      -Wanalyzer-deref-before-check
      -Wanalyzer-undefined-behavior-ptrdiff
      -Wanalyzer-infinite-loop
      -Wanalyzer-infinite-recursion
      -Wanalyzer-va-arg-type-mismatch
      -Wanalyzer-va-list-exhausted
      -Wanalyzer-va-list-leak
      -Wanalyzer-va-list-use-after-va-end
      -Wanalyzer-out-of-bounds
      -Wanalyzer-overlapping-buffers
      -Wanalyzer-exposure-through-output-file
      -Wanalyzer-exposure-through-uninit-copy
      -Wanalyzer-write-to-const
      -Wanalyzer-write-to-string-literal
      -Wanalyzer-use-of-uninitialized-value
      -Wanalyzer-use-of-pointer-in-stale-stack-frame
      -Wanalyzer-mismatching-deallocation
      -Wanalyzer-allocation-size
      -Wanalyzer-stale-setjmp-buffer
      -Wanalyzer-jump-through-null
      -Wanalyzer-putenv-of-auto-var
      -Wanalyzer-throw-of-unexpected-type
      -Wanalyzer-unsafe-call-within-signal-handler
      -Wanalyzer-imprecise-fp-arithmetic
      -Wanalyzer-undefined-behavior-strtok
    )
    target_compile_options(crucible_dialect INTERFACE ${CRUCIBLE_ANALYZER_FLAGS})
  endif()
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 4: FORTIFY_SOURCE + defines — both compilers
# ═══════════════════════════════════════════════════════════════
target_compile_definitions(crucible_dialect INTERFACE
  _FORTIFY_SOURCE=3                     # 228  max glibc buffer checks
)

# The CRUCIBLE_PGO block at the head of this file computes these.  Both
# lists are empty when CRUCIBLE_PGO is off.  They come after the warning
# set above, because GCC takes the last of -Wfoo and -Wno-foo, and the
# use flags switch one of those warnings off.
if(_CRUCIBLE_PGO_COMPILE_FLAGS)
  target_compile_options(crucible_dialect INTERFACE ${_CRUCIBLE_PGO_COMPILE_FLAGS})
endif()
if(_CRUCIBLE_PGO_LINK_FLAGS)
  target_link_options(crucible_dialect INTERFACE ${_CRUCIBLE_PGO_LINK_FLAGS})
endif()
if(_CRUCIBLE_PGO_COMPILE_DEFINITIONS)
  target_compile_definitions(crucible_dialect INTERFACE ${_CRUCIBLE_PGO_COMPILE_DEFINITIONS})
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 5: Debug-only — auto-init
#
# No build takes -fharden-control-flow-redundancy.  It defends a binary
# against fault injection, it finds no defect in a test, and it cannot
# instrument a function that calls setjmp.
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(crucible_dialect INTERFACE
    -ftrivial-auto-var-init=zero        # 229  zero-fill uninit stack (P2795R5)
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 6: Release-only — cheaper auto-init + contract policy
#
# Release evaluates every P2900 clause and reports a violation through
# `handle_contract_violation`.  Read the "READ THIS" paragraph below
# for what happens next, which is not what the flag's name implies.  A
# translation unit leaves this policy only by appearing in
# CRUCIBLE_CONTRACT_IGNORE_TUS, applied in SECTION 6b at the foot of
# this file.
#
# This inverts the previous default.  Until 2026-09-14 this block set
# `=ignore` PUBLIC for the whole of Release, under a comment that read
# "Boundary TUs that want enforcement flip this."  No translation unit
# ever flipped it, and no `#pragma GCC contract_evaluation_semantic`
# exists anywhere in include/ or src/, so every P2900 clause in the
# library was dead in the build we ship — on 2026-09-14 that was 179
# of them, 145 `pre` + 5 `post` + 29 `contract_assert`, counted over
# include/ and src/ with comment lines dropped.  Re-derive rather than
# trust that figure; it moves with every commit.  What does not move
# is the shape of the problem: an opt-in list that nobody opts into is
# not a policy.  An opt-out list is auditable, because it is short and
# each entry has to carry its justification at the point of exemption.
#
# `observe` costs one predicate evaluation and one not-taken branch per
# guarded call.  Every source file in the `crucible` static library is
# cold — cntp, canopy, topology, observe, cog — and none of them
# instantiate the recording chain, so that branch lands where nobody is
# counting nanoseconds.
#
# READ THIS BEFORE ASSUMING "log and continue".  P2900 says `observe`
# evaluates, reports, and resumes.  This project's handler does not
# resume: `handle_contract_violation` in src/foundation/ContractHandler.cpp is
# `[[gnu::weak, noreturn]]` and ends in `std::abort()`.  So a Release
# binary under this flag CHECKS AND DIES, which is stronger than the
# word "observe" suggests and is the behaviour this build now ships.
# Making Release genuinely continue means giving that handler a
# returning definition — it is weak, so a program can override it —
# and that is a deliberate decision about production failure policy,
# not a side effect to make here.
#
# The body-level CRUCIBLE_PRE / CRUCIBLE_POST family
# (include/foundation/contracts/{Pre,Post}.h) obeys this flag too.  Its
# runtime arm is a language contract clause, so a precondition does its
# check in a Release library as it does in Debug.  NDEBUG has no effect
# on that family.  A translation unit leaves it only through
# CRUCIBLE_CONTRACT_IGNORE_OPTIONS, which sets the flag and the define
# that the family reads.
#
# Two caveats this flag does NOT cover, recorded here so the next
# reader does not mistake the scope:
#
#   1. CRUCIBLE_INVARIANT and CRUCIBLE_DEBUG_ASSERT are NDEBUG-keyed by
#      design and stay compiled out.  CRUCIBLE_FATAL_INVARIANT (around
#      73 call sites) checks in every mode and is unaffected either way.
#   2. Test executables compile with `-UNDEBUG` (see test/CMakeLists.txt),
#      so the NDEBUG-keyed families above ARE armed in a Release test
#      binary and are NOT armed in a Release library or vessel binary.
#      A green Release test run is therefore weaker evidence about
#      production than it looks.
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE MATCHES "Release")
  if(CRUCIBLE_VERIFY)
    set(_crucible_release_contract_semantic enforce)
  else()
    set(_crucible_release_contract_semantic observe)
  endif()
  target_compile_options(crucible_dialect INTERFACE
    -ftrivial-auto-var-init=zero        # 231  zero-fill (cheaper)
    -fcontract-evaluation-semantic=${_crucible_release_contract_semantic}  # 231a  see SECTION 6
  )
endif()

# ── Sanitizers on every target that links crucible_dialect ───────
#
# This block puts the flags on crucible_dialect.  The checks then apply to
# the static libraries as well as to the tests.  A library that is not
# instrumented hides its overflows from ASan and its races from
# ThreadSanitizer.
#
# The default preset puts UBSan on the test executables only (the
# crucible_test factory).  UBSan tests the dynamic type of each polymorphic
# object, so an instrumented library defines the typeinfo of library classes
# such as std::_Sp_counted_base, and the no-throw gate refuses a defined
# typeinfo.  The strict preset puts the full UBSan set on the libraries, and
# that one preset disables the gate (test/CMakeLists.txt).  The -fsanitize=undefined
# set of GCC does not include float-cast-overflow, float-divide-by-zero and
# bounds-strict, and the strict preset names them.
set(_crucible_sanitizers "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CRUCIBLE_SANITIZE)
  set(_crucible_sanitizers address)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CRUCIBLE_TSAN)
  set(_crucible_sanitizers thread)
elseif(CRUCIBLE_UBSAN_STRICT)
  set(_crucible_sanitizers
    undefined,float-cast-overflow,float-divide-by-zero,bounds-strict)
endif()
if(_crucible_sanitizers)
  target_compile_options(crucible_dialect INTERFACE
    -fsanitize=${_crucible_sanitizers}
    $<$<NOT:$<BOOL:${CRUCIBLE_TSAN}>>:-fno-sanitize-recover=all>
    -fno-omit-frame-pointer)
  target_link_options(crucible_dialect INTERFACE
    -fsanitize=${_crucible_sanitizers})
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 7: Linker hardening — both compilers
#
# Full RELRO, immediate binding, no-exec stack, separate code,
# build-id for debuginfo, and more.
# ═══════════════════════════════════════════════════════════════
target_link_options(crucible_dialect INTERFACE
  "LINKER:-z,relro"                     # 232  read-only relocations
  "LINKER:-z,now"                       # 233  immediate symbol binding
  "LINKER:-z,noexecstack"              # 234  non-executable stack
  "LINKER:-z,separate-code"            # 235  separate code from data
  "LINKER:--as-needed"                 # 236  only link needed libs
  "LINKER:--no-copy-dt-needed-entries" # 237  no transitive deps
  "LINKER:--hash-style=gnu"            # 238  GNU hash (faster lookup)
  "LINKER:--build-id=sha1"             # 239  SHA-1 build ID
  "LINKER:-z,pack-relative-relocs"     # 240  compact relocations
  "LINKER:-z,nodump"                   # 241  prevent core dump of lib
  "LINKER:--gc-sections"               # 242  garbage-collect sections
  "LINKER:-z,defs"                     # 243  no undefined symbols
)

# Vessel adapter (shared library for Python/ctypes)
add_subdirectory(vessel/torch)

# Tests — built for Debug and Release. Release catches codegen,
# optimizer, and NDEBUG-only drift; tests that need assert() must use
# explicit checks instead.
enable_testing()
add_subdirectory(test)

# Examples — worked production-shape demonstrations of substrate
# concepts (Phase 0 P0-5 ships examples/fn/* for the Fn<...>
# aggregator).  Built alongside tests because they double as
# regression smoke tests for the substrate's public surface; opt-out
# via -DCRUCIBLE_EXAMPLES=OFF for minimal builds.
option(CRUCIBLE_EXAMPLES "Build worked examples of substrate concepts" ON)
if(CRUCIBLE_EXAMPLES)
  add_subdirectory(examples)
endif()

# Benchmarks (opt-in: only built if -DCRUCIBLE_BENCH=ON)
option(CRUCIBLE_BENCH "Build benchmark executables" OFF)
if(CRUCIBLE_BENCH)
  add_subdirectory(bench)
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 6b: Release contract policy — the opt-out list
#
# SECTION 6 put Release on `observe`.  This block is the complete set
# of translation units that opt back out to `ignore`, and it is the
# only place in the tree that may do so.  Adding a line here means
# claiming that a measured cost justifies giving up enforcement in
# that file; the claim belongs in a comment beside the line.
#
# Mechanism: source-file COMPILE_OPTIONS, set to the whole of
# CRUCIBLE_CONTRACT_IGNORE_OPTIONS.  The list also sets the define that
# CRUCIBLE_PRE and CRUCIBLE_POST read, because GCC gives no macro for
# the semantic.  CMake emits the compile line as
#
#   CMAKE_CXX_FLAGS  CMAKE_CXX_FLAGS_<CONFIG>  <target options>  <source options>
#
# so a source-level flag is last on the line, and GCC 16.2.1 takes the
# last `-fcontract-evaluation-semantic` it sees (verified: `ignore`
# then `observe` fires the handler, `observe` then `ignore` does not).
#
# The alternatives were rejected.  A per-file `#pragma GCC
# contract_evaluation_semantic` (what CLAUDE.md used to describe) is
# useless here because the recording chain is header-only: the pragma
# would have to be replicated into every TU that includes the header,
# and there is no header to put it in that would not also silence the
# cold callers.  An INTERFACE target carrying the flag is fragile,
# because INTERFACE_COMPILE_OPTIONS from several linked libraries are
# ordered by link order rather than by intent, and the winner of a
# last-wins flag would then depend on an ordering nobody states.
#
# There is deliberately no `src/` entry.  Not one file in the
# `crucible` static library instantiates TraceRing, MetaLog, Vigil,
# Arena, PoolAllocator, or ReplayEngine — checked by grep over src/,
# which returns zero hits for all six.  The recording chain lives in
# headers whose only consumers today are bench/ and test/, plus the
# vessel adapter.  When a production hot TU does land (vessel dispatch
# is the expected first one), it goes in the list below with its
# measurement.
#
# The entries are the six bench TUs that time the recording chain, one
# per named component.  A benchmark exists to report the per-op cost of
# the primitive it names, so it has to compile the primitive the way the
# hot path would compile it; an enforcement branch inside the timed loop
# would make the number describe the benchmark instead of the primitive.
# Benches that merely include these headers without timing them
# (bench_phases, bench_merkle_dag, bench_cipher, bench_serialize,
# bench_content_hash, bench_region_cache, bench_concurrent_queues,
# bench_metalog_session) are NOT listed, and stay on `observe`.
#
# PROVISIONAL, and the measurement says to keep it short.
#
# Measured 2026-09-14 on Arena::alloc_array_nonzero, which carries
# `pre(decide::positive(n))` and is the bump path bench_arena times.
# 15 trials x 2e6 allocations, pinned to isolated core 88/89, the two
# variants interleaved:
#
#   n a compile-time constant: p50 1.737 ns (ignore) vs 1.733 ns
#     (observe).  No cost at all — GCC folds the predicate away.
#   n opaque to the optimizer: p50 2.089 ns (ignore) vs 3.212 ns
#     (observe), so roughly +1.1 ns, but min-to-min is only +0.08 ns.
#
# The box was shared at the time (load average ~89) and the `ignore`
# samples spread 47% p10-to-p90, well past the 5% bar the code guide
# sets for a usable number, so treat the magnitude as unknown between
# about +0.1 and +1.1 ns per guarded call.  The ordering held at every
# quantile, which is the only part of this worth relying on.
#
# ── The quiet-box number, and the two entries it retires ───────────
#
# The paragraph above asked for a real number before shrinking this
# list.  Measured on an isolated core with the bench partition active,
# 10 runs per variant interleaved, one object per semantic linked
# against the same library, pinned g++-16p:
#
#   dispatch_op [COMPILED, cyclic]      3.94 -> 4.64 ns  (+0.70, +17.8%)
#   dispatch_op [COMPILED, MATCH only]  4.33 -> 5.30 ns  (+0.97, +22.4%)
#   CrucibleContext::advance (8 ops)    3.40 -> 5.30 ns  (+1.90, +55.9%)
#
# Run-to-run p50 spread stayed at or below 3.8% on all six arms, inside
# the 5% bar.  Two arms of the same run are void and are not quoted:
# dispatch_op [RECORDING] spread 8.2% and 13.5%, and is_compiled()
# under observe at 5.5%.
#
# So a guarded dispatch_op call costs +0.70 to +0.97 ns, which narrows
# the band above rather than contradicting it.  The two entries that
# timed those paths are therefore REMOVED: a benchmark whose figure sits
# 15-22% below what production runs is not reporting the primitive, and
# the exemption was the only thing making the two disagree.  The vessel
# adapter compiles with `observe` — verified from its own compile line in
# a Release build with CRUCIBLE_BENCH=OFF, where this whole block is
# inert — so the bench and the adapter now share one semantic.
#
# The four that remain are unmeasured on a quiet box.  Each leaves this
# list when it has its own number, not before.
#
# bench/ declares zero `add_test`, so there is no committed per-op
# latency in the tree to compare against.  bench/baselines/record_leaf.json
# predates this change and its dispatch figures were taken under
# `ignore`; they are not comparable to a production path and re-taking
# them waits on the recording-path rewrite now in flight.  Shrink this
# list, do not grow it.
# ═══════════════════════════════════════════════════════════════
# The CRUCIBLE_BENCH guard is not a policy choice.  `DIRECTORY bench`
# is an error when bench/ was never added, and bench/ is added only
# under that option.  One consequence is worth stating plainly: with
# the plain `release` preset (CRUCIBLE_BENCH=OFF) this block does
# nothing, so every TU in that build evaluates its contracts.  The two
# exceptions are the test_pre_post_cost sentinel, which measures the
# ignore arm, and test_swiss_table_buffer_ignore, which shows that the
# plain checks still abort (test/foundation/CMakeLists.txt).
if(CMAKE_BUILD_TYPE MATCHES "Release" AND CRUCIBLE_BENCH)
  set(CRUCIBLE_CONTRACT_IGNORE_TUS
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_trace_ring.cpp     # TraceRing push/pop
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_meta_log.cpp       # MetaLog append
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_arena.cpp          # Arena bump allocation
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_pool_allocator.cpp # PoolAllocator slot lookup
  )
  set_source_files_properties(
    ${CRUCIBLE_CONTRACT_IGNORE_TUS}
    DIRECTORY bench
    PROPERTIES COMPILE_OPTIONS "${CRUCIBLE_CONTRACT_IGNORE_OPTIONS}")
endif()

# The flag and the define of CRUCIBLE_CONTRACT_IGNORE_OPTIONS must go
# together in each scope.  This call comes after the last target, so it
# sees each one.  The self-test proves that the rule rejects each split
# form.
crucible_check_contract_ignore_pairs()
add_test(NAME contract_ignore_pairs_self_test
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/ContractSemanticSelfTest.cmake)
set_tests_properties(contract_ignore_pairs_self_test PROPERTIES LABELS "ci_guard")
