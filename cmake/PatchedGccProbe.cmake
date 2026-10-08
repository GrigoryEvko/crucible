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

# Compile PROBE (a file in cmake/probes/) with CXX, -fsyntax-only, and each
# argument after OUTPUT_VAR.  Set EXIT_VAR to the exit status and OUTPUT_VAR
# to the diagnostics.  The probe file is the last argument of the compiler.
function(crucible_run_gcc_probe cxx probe exit_var output_var)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C
            "${cxx}" -std=c++26 -fcontracts -fsyntax-only -fdiagnostics-color=never ${ARGN}
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/probes/${probe}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_output)
  set(${exit_var} "${exit_code}" PARENT_SCOPE)
  set(${output_var} "${probe_output}" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to TRUE if the file SARIF holds a SARIF log with a related
# location whose property "gcc/context" is "constraint" and whose message
# starts with "required for the satisfaction of".  Set it to FALSE if the
# file is missing, is not JSON, or has no such location.  The walk is
# O(results x related locations) of the log of one probe.
function(crucible_sarif_has_constraint_context sarif out_var)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT EXISTS "${sarif}")
    return()
  endif()
  file(READ "${sarif}" log)
  string(JSON result_count ERROR_VARIABLE problem LENGTH "${log}" runs 0 results)
  if(problem OR result_count EQUAL 0)
    return()
  endif()
  math(EXPR last_result "${result_count} - 1")
  foreach(result RANGE ${last_result})
    string(JSON location_count ERROR_VARIABLE problem LENGTH "${log}" runs 0 results ${result} relatedLocations)
    if(problem OR location_count EQUAL 0)
      continue()
    endif()
    math(EXPR last_location "${location_count} - 1")
    foreach(location RANGE ${last_location})
      string(JSON context ERROR_VARIABLE problem
             GET "${log}" runs 0 results ${result} relatedLocations ${location} properties gcc/context)
      if(problem OR NOT context STREQUAL "constraint")
        continue()
      endif()
      string(JSON text ERROR_VARIABLE problem
             GET "${log}" runs 0 results ${result} relatedLocations ${location} message text)
      if(NOT problem AND text MATCHES "^required for the satisfaction of ")
        set(${out_var} TRUE PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endforeach()
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

  # The patched compiler refuses the probe, and its SARIF log holds the
  # constraint chain of the error.  The stock compiler writes that chain only
  # to its text output.  The log goes in a directory of the build tree that
  # the function removes after the check.
  set(sarif_directory "${CMAKE_BINARY_DIR}/CMakeFiles/crucible-sarif-probe")
  file(REMOVE_RECURSE "${sarif_directory}")
  file(MAKE_DIRECTORY "${sarif_directory}")
  set(sarif "${sarif_directory}/sarif_context.sarif")
  crucible_run_gcc_probe("${cxx}" sarif_context.cpp exit_code probe_output
                         "-fdiagnostics-add-output=sarif:file=${sarif}")
  crucible_sarif_has_constraint_context("${sarif}" has_context)
  file(REMOVE_RECURSE "${sarif_directory}")
  if(exit_code EQUAL 0 OR NOT has_context)
    string(APPEND problems "\n- The fix that writes the C++ context of a diagnostic to the SARIF output")
    string(APPEND problems " (utils/toolchain/gcc/patches/0003-*.patch) is missing.  That compiler writes")
    string(APPEND problems " the function, instantiation, constexpr, constraint and macro lines of an")
    string(APPEND problems " error only to its text output.  The negative-compile fixtures read each")
    string(APPEND problems " diagnostic from the SARIF log, and without the fix they cannot see these lines.")
    string(APPEND problems "  The probe cmake/probes/sarif_context.cpp gave the exit status")
    string(APPEND problems " '${exit_code}', and its SARIF log has no constraint line.  The output:\n${probe_output}")
  endif()

  # The patched compiler gives a plugin six events of the C++ front end, and
  # the plugin.def of its plugin headers names each one.  Its plugin.h also
  # declares the slots of the two direct events.  The stock compiler has no
  # such event, and a compiler with the previous form of the patch has no slot.
  # The probe refuses each of them.
  execute_process(
    COMMAND "${cxx}" -print-file-name=plugin
    RESULT_VARIABLE plugin_query_code
    OUTPUT_VARIABLE plugin_directory
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  crucible_run_gcc_probe("${cxx}" plugin_events.cpp exit_code probe_output -isystem "${plugin_directory}/include")
  if(NOT plugin_query_code EQUAL 0 OR NOT exit_code EQUAL 0)
    string(APPEND problems "\n- The plugin events of the C++ front end (utils/toolchain/gcc/patches/0004-*.patch)")
    string(APPEND problems " are missing.  That compiler gives a plugin no event for an explicit instantiation, a")
    string(APPEND problems " sizeof, a name use, a using form, a member access or a reflection.  The rule")
    string(APPEND problems " proof-routes of the quarantine plugin then cannot read two of its shapes, and the")
    string(APPEND problems " plugin does not build.  Or the compiler has the previous form of the patch, which gives")
    string(APPEND problems " the name use and the member access through invoke_plugin_callbacks, at approximately")
    string(APPEND problems " 80 instructions for each name lookup.  A plugin whose callbacks have the types of")
    string(APPEND problems " these two direct events then does not build.")
    string(APPEND problems "  The plugin headers are in '${plugin_directory}/include', and the probe")
    string(APPEND problems " cmake/probes/plugin_events.cpp gave the exit status '${exit_code}' and this output:\n")
    string(APPEND problems "${probe_output}")
  endif()

  if(problems STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  set(report "Crucible toolchain: '${cxx}' does not have each fix in utils/toolchain/gcc/patches.")
  string(APPEND report "${problems}\n")
  string(APPEND report "Build the patched compiler with python3 utils/bin/setup gcc PREFIX, then")
  string(APPEND report " configure again with CRUCIBLE_GCC16_PREFIX=PREFIX.  The pin gcc of")
  string(APPEND report " utils/pins.txt and utils/toolchain/gcc/patches/ give the source.")
  set(${out_var} "${report}" PARENT_SCOPE)
endfunction()
