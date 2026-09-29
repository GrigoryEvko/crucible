# PatchedGccProbe.cmake: identify the patched GCC by what it does.
#
# Crucible needs each fix in utils/toolchain/gcc/patches.  A version string cannot
# show a fix, because the patched compiler and the Fedora compiler both say
# 16.2.1.  So each fix has a probe in cmake/probes/, and the probe compiles
# differently with and without the fix.  The root CMakeLists.txt refuses a
# compiler that fails a probe after project(), so every configure is checked,
# with or without cmake/Toolchain-gcc16.cmake.  The toolchain file makes the
# same call before project(), so a bad compiler fails before CMake tests it.
# cmake/probes/self_test.cmake is the test of the probes.

# Set OUT_VAR to the compiler that CXX runs.  A ccache masquerade link, such as
# /usr/lib64/ccache/c++, resolves to the ccache binary, and ccache then runs the
# first program of the same name on PATH outside the directory of the link.
# That program is the compiler whose cc1plus the build uses, so the probes and
# the ccache key read it.  Any other CXX is its own compiler.
function(crucible_real_compiler cxx out_var)
  get_filename_component(resolved "${cxx}" REALPATH)
  get_filename_component(resolved_name "${resolved}" NAME)
  if(NOT resolved_name STREQUAL "ccache")
    set(${out_var} "${cxx}" PARENT_SCOPE)
    return()
  endif()
  get_filename_component(link_dir "${cxx}" DIRECTORY)
  get_filename_component(link_dir "${link_dir}" REALPATH)
  get_filename_component(name "${cxx}" NAME)
  string(REPLACE ":" ";" search "$ENV{PATH}")
  foreach(entry IN LISTS search)
    get_filename_component(entry_real "${entry}" REALPATH)
    if(entry STREQUAL "" OR entry_real STREQUAL link_dir OR NOT EXISTS "${entry}/${name}")
      continue()
    endif()
    get_filename_component(candidate "${entry}/${name}" REALPATH)
    get_filename_component(candidate_name "${candidate}" NAME)
    if(NOT candidate_name STREQUAL "ccache")
      set(${out_var} "${entry}/${name}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "Crucible toolchain: '${cxx}' is a ccache link, and PATH has no '${name}' outside "
                      "'${link_dir}' for ccache to run.  Configure with the full path of the compiler.")
endfunction()

# Compile PROBE (a file in cmake/probes/) with CXX, -fsyntax-only.  Set
# EXIT_VAR to the exit status and OUTPUT_VAR to the diagnostics.
function(crucible_run_gcc_probe cxx probe exit_var output_var)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C
            "${cxx}" -std=c++26 -fcontracts -fsyntax-only -fdiagnostics-color=never
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/probes/${probe}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_output)
  set(${exit_var} "${exit_code}" PARENT_SCOPE)
  set(${output_var} "${probe_output}" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to an empty string if CXX passes every probe.  Otherwise set it to
# a message that names each missing fix and tells how to build the compiler.
function(crucible_patched_gcc_problem cxx out_var)
  set(problems "")

  # The patched compiler refuses the second call of a consteval function
  # whose precondition fails.  The stock compiler accepts the file.
  crucible_run_gcc_probe("${cxx}" contract_cache.cpp exit_code probe_output)
  set(expected "call to consteval function .held_value\\(42\\). is not a constant expression")
  if(exit_code EQUAL 0 OR NOT probe_output MATCHES "${expected}")
    string(APPEND problems "\n- The fix for the constexpr cache of contracts")
    string(APPEND problems " (utils/toolchain/gcc/patches/0001-*.patch) is missing.")
    string(APPEND problems "  That compiler can keep the result of a constexpr call that")
    string(APPEND problems " violates a contract, and the pre() and post() clauses of the call")
    string(APPEND problems " then have no effect in a constant evaluation.")
    string(APPEND problems "  The probe cmake/probes/contract_cache.cpp gave the exit status")
    string(APPEND problems " '${exit_code}' and this output:\n${probe_output}")
  endif()

  # The patched compiler accepts four static_asserts on a string search from
  # a pointer with an offset.  The stock compiler refuses each of them.
  crucible_run_gcc_probe("${cxx}" memchr_offset.cpp exit_code probe_output)
  if(NOT exit_code EQUAL 0)
    string(APPEND problems "\n- The fix for the constant evaluation of memchr, strchr, strrchr")
    string(APPEND problems " and strstr (utils/toolchain/gcc/patches/0002-*.patch) is missing.")
    string(APPEND problems "  That compiler counts the offset of a pointer into a string two")
    string(APPEND problems " times, and std::string_view::find can then give a wrong value")
    string(APPEND problems " at run time at -O1 and above.")
    string(APPEND problems "  The probe cmake/probes/memchr_offset.cpp gave the exit status")
    string(APPEND problems " '${exit_code}' and this output:\n${probe_output}")
  endif()

  if(problems STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  set(report "Crucible toolchain: '${cxx}' does not have each fix in utils/toolchain/gcc/patches.")
  string(APPEND report "${problems}\n")
  string(APPEND report "Build the patched compiler with utils/toolchain/gcc/build.sh PREFIX, then")
  string(APPEND report " configure again with CRUCIBLE_GCC16_PREFIX=PREFIX.  utils/toolchain/gcc/BASE")
  string(APPEND report " and utils/toolchain/gcc/patches/ give the source.")
  set(${out_var} "${report}" PARENT_SCOPE)
endfunction()
