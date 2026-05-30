# Toolchain-gcc16.cmake — portable resolution of the patched GCC 16.1.1 toolchain.
#
# Crucible builds REQUIRE the patched GCC 16.1.1 (PR c++/124241 contracts-cache
# bypass cherry-picked) plus its matching libstdc++ 16. The location of that
# toolchain is machine-specific, so this file resolves it in priority order
# instead of hardcoding a single path in the preset.
#
# Resolution order (highest priority first):
#   1. CRUCIBLE_CXX           — full path to the g++ binary. The sibling tools
#                               (gcc, gcc-ar, gcc-nm, gcc-ranlib) and the lib
#                               prefix are derived from it. Use this when the
#                               binaries do not follow the <prefix>/usr/bin
#                               layout, or carry non-default names.
#   2. CRUCIBLE_GCC16_PREFIX  — install prefix; binaries are expected at
#                               <prefix>/usr/bin/{gcc-16p,g++-16p,...} and libs
#                               at <prefix>/usr/lib64 (+ <prefix>/lib64).
#   3. $HOME/.local/gcc16-patched — the canonical dev-box default. Keeps
#                               `cmake --preset default` working verbatim with
#                               no environment setup on the reference machine.
#
# Override on another machine:
#   export CRUCIBLE_GCC16_PREFIX=/opt/gcc16-patched
#   # or, for a non-standard binary name / layout:
#   export CRUCIBLE_CXX=/opt/toolchains/gcc16/bin/g++
#
# A resolved-but-missing compiler is a hard configure error (so a bogus env var
# fails fast rather than silently falling back to a system compiler that cannot
# build the tree).

# --- 1. Resolve the C++ compiler --------------------------------------------
if(DEFINED ENV{CRUCIBLE_CXX} AND NOT "$ENV{CRUCIBLE_CXX}" STREQUAL "")
  set(_crucible_cxx "$ENV{CRUCIBLE_CXX}")
  # Derive prefix as the grandparent of the binary (<prefix>/usr/bin/g++ ->
  # <prefix>/usr -> ... but we only need the lib dir, computed below from the
  # binary's directory).
  get_filename_component(_crucible_bindir "${_crucible_cxx}" DIRECTORY)        # .../usr/bin
  get_filename_component(_crucible_usrdir "${_crucible_bindir}" DIRECTORY)     # .../usr
  get_filename_component(_crucible_prefix "${_crucible_usrdir}" DIRECTORY)     # ...
  # Sibling tool names follow the binary's own suffix convention. The canonical
  # patched build uses the -16p suffix; derive sibling tools from the bindir.
  set(_crucible_cc     "${_crucible_bindir}/gcc-16p")
  set(_crucible_ar     "${_crucible_bindir}/gcc-ar-16p")
  set(_crucible_nm     "${_crucible_bindir}/gcc-nm-16p")
  set(_crucible_ranlib "${_crucible_bindir}/gcc-ranlib-16p")
else()
  # Prefix-based resolution (env var or canonical default).
  if(DEFINED ENV{CRUCIBLE_GCC16_PREFIX} AND NOT "$ENV{CRUCIBLE_GCC16_PREFIX}" STREQUAL "")
    set(_crucible_prefix "$ENV{CRUCIBLE_GCC16_PREFIX}")
  else()
    set(_crucible_prefix "$ENV{HOME}/.local/gcc16-patched")
  endif()
  set(_crucible_bindir "${_crucible_prefix}/usr/bin")
  set(_crucible_cxx    "${_crucible_bindir}/g++-16p")
  set(_crucible_cc     "${_crucible_bindir}/gcc-16p")
  set(_crucible_ar     "${_crucible_bindir}/gcc-ar-16p")
  set(_crucible_nm     "${_crucible_bindir}/gcc-nm-16p")
  set(_crucible_ranlib "${_crucible_bindir}/gcc-ranlib-16p")
endif()

# --- 2. Hard-fail on a missing compiler -------------------------------------
if(NOT EXISTS "${_crucible_cxx}")
  message(FATAL_ERROR
    "Crucible toolchain: patched g++ not found at '${_crucible_cxx}'.\n"
    "Set CRUCIBLE_GCC16_PREFIX to the install prefix "
    "(<prefix>/usr/bin/g++-16p), or CRUCIBLE_CXX to the full g++ path. "
    "See cmake/Toolchain-gcc16.cmake for resolution order.")
endif()

set(CMAKE_C_COMPILER   "${_crucible_cc}"     CACHE FILEPATH "Crucible patched gcc-16p")
set(CMAKE_CXX_COMPILER "${_crucible_cxx}"    CACHE FILEPATH "Crucible patched g++-16p")
set(CMAKE_AR           "${_crucible_ar}"     CACHE FILEPATH "Crucible patched gcc-ar-16p")
set(CMAKE_NM           "${_crucible_nm}"     CACHE FILEPATH "Crucible patched gcc-nm-16p")
set(CMAKE_RANLIB       "${_crucible_ranlib}" CACHE FILEPATH "Crucible patched gcc-ranlib-16p")

# --- 3. Derive the rpath from the SAME prefix -------------------------------
# Binaries compiled with the patched tree link against its newer libstdc++.so.6,
# which is not on the system path. The rpath is derived from the resolved
# prefix so it always matches the compiler that produced the binary.
# CRUCIBLE_CXX-derived prefix and prefix-based resolution both expose
# <prefix>/usr/lib64 and (optionally) <prefix>/lib64.
set(_crucible_rpath "-Wl,-rpath,${_crucible_prefix}/usr/lib64:${_crucible_prefix}/lib64")

# Use the *_INIT variants: these seed the cache without clobbering preset- or
# command-line-supplied CMAKE_*_LINKER_FLAGS, and compose with per-preset flags.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_crucible_rpath}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_crucible_rpath}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_crucible_rpath}")
