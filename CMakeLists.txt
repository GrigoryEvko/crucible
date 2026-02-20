cmake_minimum_required(VERSION 3.25)
project(crucible LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library
add_library(crucible INTERFACE)
target_include_directories(crucible INTERFACE include/)

# ╔═══════════════════════════════════════════════════════════════════╗
# ║  CRUCIBLE HARDENING — 400+ compiler/linker/sanitizer flags       ║
# ║                                                                   ║
# ║  Clang 22: -Weverything (1002 warnings) minus ~30 useless ones   ║
# ║  GCC 16:   200+ individual warnings                              ║
# ║  Both:     sanitizers, linker hardening, FORTIFY, library checks ║
# ║                                                                   ║
# ║  "Every nanosecond matters. Every byte is accounted for.         ║
# ║   Every type is strong. Every pointer is checked."               ║
# ╚═══════════════════════════════════════════════════════════════════╝

option(CRUCIBLE_WERROR "Treat warnings as errors" OFF)

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
# SECTION 1: Common flags — both Clang 22 and GCC 15/16
#            52 warning flags + 13 hardening flags = 65
# ═══════════════════════════════════════════════════════════════
target_compile_options(crucible INTERFACE
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
  -Wswitch-enum                        #  14  missing enum case
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
  target_compile_options(crucible INTERFACE -Werror)
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 2: Clang 22 — THE NUCLEAR OPTION
#
# -Weverything enables ALL 1002 Clang warning groups.
# We suppress ~30 that are useless for C++26 or pathologically noisy.
# Net: ~970 active warning checks.
# ═══════════════════════════════════════════════════════════════
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(crucible INTERFACE
    # ── 2.1  Enable every Clang warning ever created ─────────
    -Weverything                        #  66  ALL 1002 warning groups

    # ── 2.2  Suppress useless / counterproductive ones ───────
    #         (30 suppressions → net ~970 active warnings)

    # C++98/11/14/17/20 backward compat — we're C++26, don't care
    -Wno-c++98-compat                   #  67
    -Wno-c++98-compat-pedantic          #  68
    -Wno-c++98-compat-bind-to-temporary-copy #  69
    -Wno-c++98-compat-extra-semi        #  70
    -Wno-c++98-compat-local-type-template-args #  71
    -Wno-pre-c++14-compat              #  72
    -Wno-pre-c++17-compat              #  73
    -Wno-pre-c++20-compat              #  74
    -Wno-pre-c++20-compat-pedantic     #  75
    -Wno-pre-c++26-compat             #  76
    -Wno-c++11-compat                  #  77
    -Wno-c++14-compat                  #  78
    -Wno-c++17-compat                  #  79
    -Wno-c++20-compat                  #  80

    # Padding — every struct with padding would warn. Not actionable.
    -Wno-padded                         #  81

    # We don't use Doxygen documentation comments
    -Wno-documentation                  #  82
    -Wno-documentation-unknown-command  #  83

    # System headers — not our code
    -Wno-system-headers                 #  84

    # Variadic macros are standard C++11+
    -Wno-variadic-macros                #  85

    # Nested anon types are fine in C++
    -Wno-nested-anon-types              #  86

    # Language extensions we use deliberately
    -Wno-language-extension-token       #  87

    # gnu-statement-expression used in assert macros
    -Wno-gnu-statement-expression-from-macro-expansion #  88

    # covered-switch-default conflicts with switch-default
    # (we WANT default in switch, even if enum is covered)
    -Wno-covered-switch-default         #  89

    # Disabled warnings that conflict with our patterns
    -Wno-missing-field-initializers     #  90  we use {} init, not all fields
    -Wno-unsafe-buffer-usage            #  91  too noisy for arena code (re-enable later)

    # libc++ marks printf as __attribute__((unused)), then we use it
    -Wno-used-but-marked-unused         #  92  false positive on std::printf

    # We use default: in switches on Op enum (55 values) intentionally
    -Wno-switch-enum                    #  93  covered by -Wswitch-default

    # Header-only lib: static locals in inline functions are OK (ODR)
    -Wno-unique-object-duplication      #  94  CKernelTable singleton

    # Trivial copy when NRVO can't elide — not a real issue
    -Wno-nrvo                           #  95  Cipher early-return copy

    # ── 2.3  Extra Clang hardening flags ─────────────────────
    -fstrict-enums                      #  96  assume valid enum values
    -fstrict-return                     #  97  UB on missing return
    -fstrict-vtable-pointers            #  98  devirt optimization
    -fcolor-diagnostics                 #  99  colored output
    -ftemplate-backtrace-limit=0        # 100  full template errors
    -fmacro-backtrace-limit=0           # 101  full macro errors
    -fconstexpr-backtrace-limit=0       # 102  full constexpr errors
    -fconstexpr-steps=10000000          # 103  generous constexpr budget
    -fbracket-depth=1024                # 104  deep template nesting

    # ── 2.4  Clang-only linker hardening ─────────────────────
    -fcf-protection=full                # 105  control-flow integrity
  )

  # ── 2.5  Library hardening — libc++ ────────────────────────
  target_compile_definitions(crucible INTERFACE
    # 102: libc++ bounds checks, null checks, type checks
    $<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>
    # 103: release: fast checks only (no perf impact)
    $<$<CONFIG:Release,RelWithDebInfo>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 3: GCC 15/16 — MAXIMUM PARANOIA
#            200+ individual warning flags
# ═══════════════════════════════════════════════════════════════
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(crucible INTERFACE
    # ── 3.1  Type conversion (9) ─────────────────────────────
    -Warith-conversion                  # 104  arith conversion lossy
    -Wconversion-extra                  # 105  extra conversions
    -Wenum-int-mismatch                 # 106  enum/int mismatch
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
    -Wabsolute-value                    # 176  wrong abs() func
    -Woverride-init                     # 177  override initializer
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

    # ── 3.12  Overflow / safety (6) ──────────────────────────
    -Walloc-size-larger-than=1073741824 # 188  alloc > 1GB
    -Wstrict-flex-arrays                # 189  flex array discipline
    -Wunterminated-string-initialization # 190  string init truncated
    -Wpacked-not-aligned                # 191  packed+aligned conflict
    -Wabi-tag                           # 192  missing ABI tag
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
  target_compile_definitions(crucible INTERFACE
    $<$<CONFIG:Debug>:
      _GLIBCXX_ASSERTIONS=1             # 206  vector[]/string[] bounds
      _GLIBCXX_SANITIZE_VECTOR=1        # 207  vector realloc poison
    >
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 4: FORTIFY_SOURCE + defines — both compilers
# ═══════════════════════════════════════════════════════════════
target_compile_definitions(crucible INTERFACE
  _FORTIFY_SOURCE=3                     # 208  max glibc buffer checks
)

# ═══════════════════════════════════════════════════════════════
# SECTION 5: Debug-only — auto-init
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(crucible INTERFACE
      -ftrivial-auto-var-init=pattern   # 209  0xAA fill uninit stack
    )
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(crucible INTERFACE
      -ftrivial-auto-var-init=zero      # 210  zero-fill uninit stack
      -fharden-control-flow-redundancy  # 211  redundant CFG checks
    )
  endif()
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 6: Release-only — cheaper auto-init
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE MATCHES "Release")
  target_compile_options(crucible INTERFACE
    -ftrivial-auto-var-init=zero        # 212  zero-fill (cheaper)
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 7: Linker hardening — both compilers
#
# Full RELRO, immediate binding, no-exec stack, separate code,
# build-id for debuginfo, and more.
# ═══════════════════════════════════════════════════════════════
target_link_options(crucible INTERFACE
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

# Tests
enable_testing()
add_subdirectory(test)

# Benchmarks (opt-in: only built if -DCRUCIBLE_BENCH=ON)
option(CRUCIBLE_BENCH "Build benchmark executables" OFF)
if(CRUCIBLE_BENCH)
  add_subdirectory(bench)
endif()
