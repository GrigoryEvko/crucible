# ── ccache: default + always-used layer ─────────────────────────────
#
# The root CMakeLists.txt calls crucible_ccache_before_project() before
# project(), so the compiler-probe pass also runs through ccache, and
# crucible_ccache_after_project() after it.  The reflection-and-contracts
# build pulls cc1plus per heavy TU through 800 MB - 1 GB and several
# CPU-seconds.  ccache works across build directories (hash_dir=false) and
# uses a tuned sloppiness set in ~/.config/ccache/ccache.conf.  Hit rate on
# the dev box: 78%+ over 648K calls.
#
# A launcher from the environment or from -D CMAKE_CXX_COMPILER_LAUNCHER is
# used as it is.  -DCRUCIBLE_USE_CCACHE=OFF turns ccache off, also when the
# environment or -D names ccache.  project() reads the environment variable
# CMAKE_CXX_COMPILER_LAUNCHER into the cache, after the first macro, so the
# second macro removes a ccache launcher.  Only a launcher whose program is
# named ccache is ccache.  sccache and other launchers get no ccache option.
#
# crucible_relocatable_objects() keeps the build root and the source root out
# of the paths in the debug information of each object.  ccache with base_dir
# passes each path under the source root as a path from the build root.  With
# hash_dir=false, a hit from a different build root gives an object that names
# that build root as its compile directory, so the objects of two build roots
# are not the same.  -fdebug-prefix-map changes the source root to its path
# from the build root, and the build root to ".".  GCC uses the last map that
# matches a path, so the build root, which can be inside the source root, comes
# last.  Then two build roots at the same place in one source tree give the
# same object, when the two compiles both use ccache or both do not.  A
# debugger that starts in the build root finds each source file.
#
# The maps do not use -ffile-prefix-map, which also sets -fmacro-prefix-map.
# That map changes __FILE__ and the file name of a source location, and a
# reflection check compares that name with the path of a header
# (utils/scripts/check-padded-lists.py).  GCC applies no map to the options in
# the producer string of the debug information, and the plugin options of the
# tree name the two roots (utils/tools/quarantine/Quarantine.cmake).

# Set OUT_VAR to TRUE when the first item of LAUNCHER is a program named ccache.
function(crucible_launcher_is_ccache launcher out_var)
  set(is_ccache FALSE)
  if(launcher)
    list(GET launcher 0 program)
    get_filename_component(program_name "${program}" NAME)
    if(program_name STREQUAL "ccache")
      set(is_ccache TRUE)
    endif()
  endif()
  set(${out_var} ${is_ccache} PARENT_SCOPE)
endfunction()

macro(crucible_ccache_before_project)
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
endmacro()

# CRUCIBLE_REAL_CXX must name the compiler that the launcher runs
# (cmake/PatchedGccProbe.cmake).
macro(crucible_ccache_after_project)
  if(NOT CRUCIBLE_USE_CCACHE)
    foreach(_crucible_ccache_language CXX C)
      crucible_launcher_is_ccache("${CMAKE_${_crucible_ccache_language}_COMPILER_LAUNCHER}" _crucible_ccache_named)
      if(_crucible_ccache_named)
        message(STATUS "ccache: CRUCIBLE_USE_CCACHE=OFF, so the ${_crucible_ccache_language} launcher "
                       "'${CMAKE_${_crucible_ccache_language}_COMPILER_LAUNCHER}' is not used")
        set(CMAKE_${_crucible_ccache_language}_COMPILER_LAUNCHER "")
      endif()
    endforeach()
  endif()

  crucible_launcher_is_ccache("${CMAKE_CXX_COMPILER_LAUNCHER}" _crucible_ccache_named)
  if(_crucible_ccache_named)
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
    #
    # The launcher also sets base_dir to the source tree.  ccache writes each
    # path under base_dir in a dependency file relative to the build
    # directory, and a cache hit gives the dependency file of the build that
    # stored the result.  A path under the source tree is then correct in each
    # build directory that gets a hit.  A base_dir above the compiler, such as
    # a home directory, also makes the headers of the compiler relative.  A
    # hit in a build directory at a different depth then records headers that
    # do not exist, and Ninja compiles the object again on each build.
    # utils/scripts/check-reconfigure-noop.py finds such a dependency.
    # ccache does not hash base_dir, so an entry that a launcher with another
    # base_dir stored can still give such a dependency file.  The namespace
    # keeps the entries of this launcher apart from those entries.
    #
    # The launcher also sets depend_mode to false, which is the ccache default
    # and the mode of CI.  When the hashes of the included files give no hit,
    # ccache then hashes the output of the preprocessor.  A change that keeps
    # that output, such as a change of a comment on its own line, then gets a
    # hit.  The output keeps the number of each line and the column of each
    # token, so a change that moves code gets no hit.  The hit needs an entry
    # for the same output, so the first change after a hit in the direct mode
    # gets no hit.  A change of a comment in foundation/Platform.h rebuilt 844
    # objects in 2.8 s and 171 CPU seconds in place of 29 s and 2,219 CPU
    # seconds.  A change of code pays one more run of the preprocessor: 2,448
    # CPU seconds in place of 2,387.
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
        "compiler_check=string:${CRUCIBLE_DRIVER_SHA256}-${CRUCIBLE_CC1PLUS_SHA256}"
        "base_dir=${CMAKE_SOURCE_DIR}" "namespace=source-base-dir" "depend_mode=false")
    message(STATUS "ccache: ${CRUCIBLE_CCACHE} (compiler launcher enabled, cc1plus ${CRUCIBLE_CC1PLUS})")
  elseif(CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "ccache: the compiler launcher '${CMAKE_CXX_COMPILER_LAUNCHER}' is not ccache, and it gets "
                   "no ccache option")
  elseif(CRUCIBLE_USE_CCACHE)
    message(STATUS "ccache: not found on PATH — proceeding without launcher")
  endif()
endmacro()

# Call this macro before the first target of the directory.
macro(crucible_relocatable_objects)
  # file(RELATIVE_PATH) gives "../" for the parent directory.  GCC keeps the
  # separator after the old prefix, so a new prefix with a separator at its
  # end gives "..//", which a compile through ccache does not give.
  file(RELATIVE_PATH _crucible_source_from_build "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}")
  string(REGEX REPLACE "/+$" "" _crucible_source_from_build "${_crucible_source_from_build}")
  if(_crucible_source_from_build STREQUAL "")
    set(_crucible_source_from_build ".")
  endif()
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=${_crucible_source_from_build}>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fdebug-prefix-map=${CMAKE_BINARY_DIR}=.>")
endmacro()
