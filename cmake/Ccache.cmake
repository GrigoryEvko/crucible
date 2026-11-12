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
# crucible_relocatable_objects() also gives the macro prefix maps.  They
# change __FILE__, __builtin_FILE(), std::source_location and
# std::meta::source_location_of.  So a Site of the Report family holds a path
# from the source root, and a file of the build directory starts with
# @build/, the name that the rule table gives it.  Two checkouts and two build
# directories then give the same text of a report, with and without ccache.
# GCC uses the last map that matches a path, and an empty old prefix matches
# each path.  The maps, from the first to the last:
#   ""                  a path of the build directory that ccache gives with
#                       no prefix, such as gen/x.h
#   "/"                 an absolute path outside the two roots stays as it is
#   source root         a path of the source root in a compile with no ccache
#   source from build   a path of the source root that ccache gives
#   source from the physical build root
#                       the same, when the build root is behind a symlink
#   build root          a path of the build directory with no ccache
#   build from the physical build root
#                       a path of the build directory that ccache gives, when
#                       the build root is behind a symlink
# ccache hashes only the name of a map option, and not its text.  The text
# changes only with the roots.  A compile with other roots reads other paths,
# so it gets a hit only when the remapped text is the same.
#
# The tree gives no -ffile-prefix-map.  That option also sets the debug map,
# and the debug maps above keep the objects of two build roots the same.  A
# gdb that starts in a different directory finds each source file after
# `set substitute-path . BUILD_DIR`, where BUILD_DIR is the build root.
#
# The producer string of the debug information records the options of the
# compile, and GCC applies no map to them.  The plugin options of the tree name
# the two roots (utils/tools/quarantine/Quarantine.cmake), and the PGO presets
# name a profile directory under the source root.  So the macro also gives
# -gno-record-gcc-switches, and the producer string holds only the language
# and the version of the compiler.  Each other option that names a path is
# then out of the object too.  An object then does not tell the options of its
# compile, for example the -march, the -O level and the hardening flags, to
# readelf, to gdb (info source) or to a tool that audits the hardening flags.
# compile_commands.json and the build log still hold each option.

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
    # The rule reconfigure-noop (utils/assay/guard/engines/buildsys/reconfigure_noop.py) finds such a dependency.
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

# Set OUT_VAR to the path of the directory TO from the directory FROM, with no
# separator at its end, or "." for the same directory.  file(RELATIVE_PATH)
# gives "../" for the parent directory.  GCC keeps the separator after the old
# prefix of a map, so a new prefix with a separator at its end gives "..//",
# which a compile through ccache does not give.
function(crucible_path_from from to out_var)
  file(RELATIVE_PATH path "${from}" "${to}")
  string(REGEX REPLACE "/+$" "" path "${path}")
  if(path STREQUAL "")
    set(path ".")
  endif()
  set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to the macro prefix maps of the source root SOURCE and the build
# root BUILD, from the first map to the last (the list at the head of this
# file).  ccache gives a path relative to the directory of the compile, and it
# reads that directory from the kernel, which resolves each symlink.  When the
# build root is behind a symlink, that path starts from the physical build root
# and goes to the logical roots.  So the list then has two more maps: the source
# root and the build root from the physical build root.
function(crucible_macro_prefix_maps source build out_var)
  crucible_path_from("${build}" "${source}" source_from_build)
  set(source_maps "-fmacro-prefix-map==@build/" "-fmacro-prefix-map=/=/" "-fmacro-prefix-map=${source}/="
                  "-fmacro-prefix-map=${source_from_build}/=")
  set(build_maps "-fmacro-prefix-map=${build}/=@build/")
  file(REAL_PATH "${build}" physical_build)
  if(NOT physical_build STREQUAL build)
    crucible_path_from("${physical_build}" "${source}" source_from_physical_build)
    crucible_path_from("${physical_build}" "${build}" build_from_physical_build)
    if(NOT source_from_physical_build STREQUAL source_from_build)
      list(APPEND source_maps "-fmacro-prefix-map=${source_from_physical_build}/=")
    endif()
    list(APPEND build_maps "-fmacro-prefix-map=${build_from_physical_build}/=@build/")
  endif()
  set(${out_var} ${source_maps} ${build_maps} PARENT_SCOPE)
endfunction()

# Call this macro before the first target of the directory.
macro(crucible_relocatable_objects)
  crucible_path_from("${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}" _crucible_source_from_build)
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=${_crucible_source_from_build}>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fdebug-prefix-map=${CMAKE_BINARY_DIR}=.>"
    "$<$<COMPILE_LANGUAGE:CXX>:-gno-record-gcc-switches>")
  crucible_macro_prefix_maps("${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" _crucible_macro_maps)
  foreach(_crucible_macro_map IN LISTS _crucible_macro_maps)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${_crucible_macro_map}>")
  endforeach()
endmacro()
