# HostTier.cmake: the hardware tier of the host of a configure.
#
# The tier is the name that the compiler resolves the native target of this
# host to: the -march=native name on x86_64, for example znver5 or
# sapphirerapids, and the -mcpu=native name on aarch64.  No source file names
# the hardware.  The profile of `loop pgo` and the bench baselines of
# bench/baselines/<tier>/ belong to one tier.  The root CMakeLists.txt calls
# crucible_host_tier() one time for each configure, and it keeps the tier in
# the cache entry CRUCIBLE_HOST_TIER.  vessel/torch/bench_e2e.py reads that
# entry from the cache of the build that it measures.

# Set TIER_VAR to the tier of the host, HELP_VAR to the output of
# `-Q --help=target` for the native flag, and PROBLEM_VAR to an empty string.
# When the compiler does not resolve the native target, as a cross compiler
# does, set TIER_VAR to an empty string and PROBLEM_VAR to the reason.  A
# caller that needs the tier stops with that reason.  One run of the
# compiler, approximately 10 ms.
function(crucible_host_tier tier_var help_var problem_var)
  set(${tier_var} "" PARENT_SCOPE)
  set(${help_var} "" PARENT_SCOPE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(native_flag -march=native)
    set(tier_option "-march=")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(native_flag -mcpu=native)
    set(tier_option "-mcpu=")
  else()
    set(${problem_var}
      "the tier detection covers x86_64 and aarch64. CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}'."
      PARENT_SCOPE)
    return()
  endif()
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" ${native_flag} -Q --help=target
    OUTPUT_VARIABLE target_help
    ERROR_VARIABLE target_error
    RESULT_VARIABLE target_result)
  if(NOT target_result EQUAL 0)
    set(${problem_var}
      "'${CMAKE_CXX_COMPILER} ${native_flag} -Q --help=target' failed:\n${target_error}" PARENT_SCOPE)
    return()
  endif()
  if(NOT target_help MATCHES "\n[ \t]*${tier_option}[ \t]+([^ \t\r\n]+)")
    set(${problem_var}
      "the --help=target output has no '${tier_option}' line with a value. The compiler did not resolve native to a name."
      PARENT_SCOPE)
    return()
  endif()
  set(tier "${CMAKE_MATCH_1}")
  if(tier STREQUAL "native")
    set(${problem_var}
      "the compiler reports '${tier_option}native' unresolved, so the tier has no name." PARENT_SCOPE)
    return()
  endif()
  set(${tier_var} "${tier}" PARENT_SCOPE)
  set(${help_var} "${target_help}" PARENT_SCOPE)
  set(${problem_var} "" PARENT_SCOPE)
endfunction()
