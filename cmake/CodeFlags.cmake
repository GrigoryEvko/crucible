# The flags of the dialect that change the machine code.
#
# Each list has one definition, here.  crucible_dialect (CMakeLists.txt)
# applies the lists, so each unit of the tree compiles with them.
# crucible_release_code_target() applies the same lists to a target that
# must compile the code of a Release build in each build type, with no
# warning flag and no sanitizer.  The constant-time taint tests and the
# timing harness of test/ct are such targets.  So a change of a list changes
# the library and the taint tests together.  crucible_code_target() applies
# the lists with no flag of a Release build, to the probe unit of test/shape.
#
#   CRUCIBLE_CODE_FLAGS          the code flags that each build takes
#   CRUCIBLE_RELEASE_CODE_FLAGS  the code flags that each build other than
#                                Debug takes
#   CRUCIBLE_CODE_DEFINITIONS    the definitions that each build takes
#
# The FP floor is the INTERFACE library crucible_fp_strict
# (cmake/FpStrict.cmake), and crucible_release_code_target() links it.
#
# A code flag that a unit of the tree gets from another place does not reach
# a taint test.  The test ct_taint_flags (cmake/CodeFlagsCheck.cmake) reads
# the flags of each compile from the CMake file API, and it fails when a
# taint unit and a unit of the foundation library do not have the same code
# flags.
#
# Include this file after the option CRUCIBLE_UBSAN_STRICT, because
# CRUCIBLE_CODE_FLAGS reads it.  The functions at the end of this file use
# only list operations, so a script in -P mode can include the file.

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

# The numbers are the numbers of the flag catalog of CMakeLists.txt.
set(CRUCIBLE_CODE_FLAGS
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
  ${_CRUCIBLE_CFI_FLAGS}               # 194  control-flow integrity (x86 CET / arm BTI+PAC)
  -fstack-clash-protection             # 195  stack clash guard pages
  -fno-delete-null-pointer-checks      # 198  keep null checks
  -fstrict-enums                       # 203  assume valid enum values
  -ftrivial-auto-var-init=zero         # 229  zero-fill uninit stack (P2795R5)
)

# A Debug build does not take 196 and 197.  The two flags defend a binary
# against fault injection, and they find no defect in a test.  On a heavy
# translation unit they cost 20% to 46% of the back end.  No build takes
# -fharden-control-flow-redundancy.  It defends a binary against fault
# injection, it finds no defect in a test, and it cannot instrument a
# function that calls setjmp.
set(CRUCIBLE_RELEASE_CODE_FLAGS
  -fharden-compares                    # 196  redundant compare checks
  -fharden-conditional-branches        # 197  redundant branch checks
)

set(CRUCIBLE_CODE_DEFINITIONS
  _FORTIFY_SOURCE=3                    # 228  max glibc buffer checks
)

# Gives TARGET the code flags, the definitions and the FP floor that each
# build takes, and no flag of a Release build.  The probe unit of test/shape
# takes these lists, and the rule codegen-shapes compares its flags with the
# flags of the units of the foundation library.  The target gets no warning
# flag, no sanitizer and no contract semantic from this call.
function(crucible_code_target target)
  target_compile_options(${target} PRIVATE ${CRUCIBLE_CODE_FLAGS})
  target_compile_definitions(${target} PRIVATE ${CRUCIBLE_CODE_DEFINITIONS})
  target_link_libraries(${target} PRIVATE crucible_fp_strict)
endfunction()

# Gives TARGET the code flags, the definitions and the FP floor of a Release
# unit of the tree, in each build type.  The rule codegen-shapes takes the
# flags of a Release build from the compile of such a target.
function(crucible_release_code_target target)
  crucible_code_target(${target})
  target_compile_options(${target} PRIVATE ${CRUCIBLE_RELEASE_CODE_FLAGS})
endfunction()

# The words of a compile that change with the kind of build, and the words
# that do not change the code.  The comparison of crucible_code_flag_problems()
# leaves them out.  A word of no class below is a code word, so a new flag of
# the tree counts until a class names it.
set(_CRUCIBLE_NOT_CODE_PREFIXES
  -W -pedantic -fdiagnostics- -fno-diagnostics- -ftemplate-backtrace-limit= -fconstexpr-ops-limit=
  -fplugin -fdebug-prefix-map= -ffile-prefix-map= -fmacro-prefix-map= -fmessage-length= -g -I -isystem -iquote)
set(_CRUCIBLE_PER_BUILD_PREFIXES
  -O -march= -mtune= -mcpu= -fPIC -fPIE -fpic -fpie -fsanitize -fno-sanitize -fprofile- -fno-profile-
  -fbranch-probabilities -fauto-profile -fanalyzer -fcontract-evaluation-semantic=)
set(_CRUCIBLE_PER_BUILD_DEFINITIONS
  NDEBUG _GLIBCXX_ASSERTIONS _GLIBCXX_SANITIZE_VECTOR CRUCIBLE_PGO_PROFILE CRUCIBLE_QUARANTINE_ACTIVE)

# Sets OUT to the code word of WORD, or to an empty string when WORD does not
# change the code or changes with the kind of build.  A definition becomes
# -DNAME or -DNAME=VALUE, so a -D word of a fragment and a define of the file
# API compare equal.  An -U word is a per-build word: the build removes NDEBUG
# with it.
function(crucible_code_word out word)
  set(code "${word}")
  if(word MATCHES "^-U")
    set(code "")
  elseif(word MATCHES "^-D(.*)$" OR NOT word MATCHES "^-")
    if(word MATCHES "^-D(.*)$")
      set(definition "${CMAKE_MATCH_1}")
    else()
      set(definition "${word}")
    endif()
    string(REGEX REPLACE "=.*$" "" name "${definition}")
    set(code "-D${definition}")
    if(name IN_LIST _CRUCIBLE_PER_BUILD_DEFINITIONS)
      set(code "")
    endif()
  else()
    foreach(prefix IN LISTS _CRUCIBLE_NOT_CODE_PREFIXES _CRUCIBLE_PER_BUILD_PREFIXES)
      string(FIND "${word}" "${prefix}" at)
      if(at EQUAL 0)
        set(code "")
        break()
      endif()
    endforeach()
  endif()
  set(${out} "${code}" PARENT_SCOPE)
endfunction()

# Sets OUT to the sorted code words of WORDS, each one time.
# Complexity: linear in the words, times the number of prefixes.
function(crucible_code_words out words)
  set(codes "")
  foreach(word IN LISTS words)
    crucible_code_word(code "${word}")
    if(NOT code STREQUAL "")
      list(APPEND codes "${code}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES codes)
  list(SORT codes)
  set(${out} "${codes}" PARENT_SCOPE)
endfunction()

# Sets OUT to at most three problems.  The first names the code words of
# BASE_WORDS that RELEASE_WORDS lacks.  The second names the flags of
# CRUCIBLE_RELEASE_CODE_FLAGS that RELEASE_WORDS lacks.  The third names the
# code words of RELEASE_WORDS that BASE_WORDS and CRUCIBLE_RELEASE_CODE_FLAGS
# lack.  BASE_WORDS are the words of a unit of the foundation library, and
# RELEASE_WORDS are the words of a unit that must compile the code of a
# Release build.  In a Debug build the base unit lacks
# CRUCIBLE_RELEASE_CODE_FLAGS, so the release unit must hold each flag of that
# list too.  NAME names the release unit in each problem.
# Complexity: quadratic in the code words, through IN_LIST.
function(crucible_code_flag_problems out name base_words release_words)
  crucible_code_words(base "${base_words}")
  crucible_code_words(release "${release_words}")
  set(missing_base "")
  foreach(code IN LISTS base)
    if(NOT code IN_LIST release)
      list(APPEND missing_base "${code}")
    endif()
  endforeach()
  set(missing_release "")
  foreach(code IN LISTS CRUCIBLE_RELEASE_CODE_FLAGS)
    if(NOT code IN_LIST release)
      list(APPEND missing_release "${code}")
    endif()
  endforeach()
  set(extra "")
  foreach(code IN LISTS release)
    if(NOT code IN_LIST base AND NOT code IN_LIST CRUCIBLE_RELEASE_CODE_FLAGS)
      list(APPEND extra "${code}")
    endif()
  endforeach()
  set(problems "")
  if(missing_base)
    list(JOIN missing_base " " words)
    string(CONCAT text "the unit of the foundation library compiles with ${words}, and ${name} does not.  Put each "
                       "flag in a list of cmake/CodeFlags.cmake, or add its class to that file when it does not "
                       "change the code")
    list(APPEND problems "${text}")
  endif()
  if(missing_release)
    list(JOIN missing_release " " words)
    string(CONCAT text "${name} compiles with no ${words} of CRUCIBLE_RELEASE_CODE_FLAGS.  Give the target the "
                       "flags through crucible_release_code_target()")
    list(APPEND problems "${text}")
  endif()
  if(extra)
    list(JOIN extra " " words)
    string(CONCAT text "${name} compiles with ${words}, and the unit of the foundation library does not.  Remove "
                       "each flag from the target, so that it compiles the code of a Release build")
    list(APPEND problems "${text}")
  endif()
  set(${out} "${problems}" PARENT_SCOPE)
endfunction()
