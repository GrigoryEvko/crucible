# The test of cmake/PatchedGccProbe.cmake.  ctest runs it as:
#   cmake -DCXX=<compiler> -DWORK=<directory> -DSOURCE=<source tree> -P cmake/probes/self_test.cmake
#
# With SOURCE, it also configures the tree with a compiler that lacks a fix,
# and the configure must stop.  A ccache link must resolve to the compiler
# that ccache runs.
#
# The compiler of the build must pass every probe.  Five stub compilers in
# WORK must each get the result that their behaviour calls for:
#   - A stub that accepts every file stands for a compiler without the
#     contract fix.  The probes must refuse it.
#   - A stub that refuses every file with a different error stands for a
#     broken compiler.  The probes must refuse it.
#   - A stub that prints the contract diagnostic of the patched compiler but
#     gives the exit status 0 must be refused, because a warning is not a
#     refusal.
#   - A stub that has the contract fix but refuses the memchr probe stands
#     for a compiler without the memchr fix.  The probes must refuse it.
#   - A stub that does what the patched compiler does must pass.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/../PatchedGccProbe.cmake")

if(NOT DEFINED CXX OR NOT DEFINED WORK)
  message(FATAL_ERROR "self_test.cmake: give -DCXX=<compiler> and -DWORK=<directory>")
endif()
file(MAKE_DIRECTORY "${WORK}")

set(contract_error
  "contract_cache.cpp:6:22: error: call to consteval function 'held_value(42)' is not a constant expression")
set(memchr_error "memchr_offset.cpp:19:37: error: static assertion failed")

# Write the stub compiler WORK/NAME.  Its body runs with the name of the
# probe file in $probe.
function(write_stub name body)
  file(WRITE "${WORK}/${name}" "#!/bin/sh\nfor probe; do :; done\n${body}")
  file(CHMOD "${WORK}/${name}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endfunction()

write_stub(accepts_all "exit 0\n")
write_stub(refuses_all "echo 'probe.cpp:1:1: error: unrelated' >&2\nexit 1\n")
write_stub(warns_only
  "case \"$probe\" in\n*contract_cache.cpp) echo \"${contract_error}\" >&2 ;;\nesac\nexit 0\n")
write_stub(lacks_memchr_fix
  "case \"$probe\" in\n*contract_cache.cpp) echo \"${contract_error}\" >&2; exit 1 ;;\n*memchr_offset.cpp) echo \"${memchr_error}\" >&2; exit 1 ;;\nesac\nexit 0\n")
write_stub(acts_patched
  "case \"$probe\" in\n*contract_cache.cpp) echo \"${contract_error}\" >&2; exit 1 ;;\nesac\nexit 0\n")

function(expect_pass cxx label)
  crucible_patched_gcc_problem("${cxx}" problem)
  if(NOT problem STREQUAL "")
    message(SEND_ERROR "self_test: ${label} must pass the probes, but the probes gave:\n${problem}")
  endif()
endfunction()

function(expect_refusal cxx label reason)
  crucible_patched_gcc_problem("${cxx}" problem)
  if(problem STREQUAL "")
    message(SEND_ERROR "self_test: the probes must refuse ${label}, but they accept it")
  elseif(NOT problem MATCHES "${reason}")
    message(SEND_ERROR "self_test: the probes refuse ${label} for a different reason:\n${problem}")
  endif()
endfunction()

set(contract_reason "constexpr cache of contracts")
set(memchr_reason "constant evaluation of memchr")

# A ccache masquerade link resolves to the program of the same name that PATH
# holds outside the directory of the link.  Any other compiler is itself.
file(MAKE_DIRECTORY "${WORK}/masquerade" "${WORK}/real" "${WORK}/bin")
write_stub(bin/ccache "exit 1\n")
write_stub(real/c++ "exit 0\n")
file(REMOVE "${WORK}/masquerade/c++")
file(CREATE_LINK "${WORK}/bin/ccache" "${WORK}/masquerade/c++" SYMBOLIC)
set(saved_path "$ENV{PATH}")
set(ENV{PATH} "${WORK}/masquerade:${WORK}/real:${saved_path}")
crucible_real_compiler("${WORK}/masquerade/c++" resolved)
set(ENV{PATH} "${saved_path}")
if(NOT resolved STREQUAL "${WORK}/real/c++")
  message(SEND_ERROR "self_test: a ccache link must resolve to ${WORK}/real/c++, but it resolves to ${resolved}")
endif()
crucible_real_compiler("${WORK}/real/c++" itself)
if(NOT itself STREQUAL "${WORK}/real/c++")
  message(SEND_ERROR "self_test: a compiler that is not a ccache link must resolve to itself, not ${itself}")
endif()

expect_pass("${CXX}" "the compiler of the build (${CXX})")
expect_pass("${WORK}/acts_patched" "a stub that acts as the patched compiler")
expect_refusal("${WORK}/accepts_all" "a stub that accepts every file" "${contract_reason}")
expect_refusal("${WORK}/refuses_all" "a stub that refuses every file" "${contract_reason}")
expect_refusal("${WORK}/refuses_all" "a stub that refuses every file" "${memchr_reason}")
expect_refusal("${WORK}/warns_only" "a stub that only warns" "${contract_reason}")
expect_refusal("${WORK}/lacks_memchr_fix" "a stub without the memchr fix" "${memchr_reason}")

# A configure of the tree, without the toolchain file, with a compiler that
# lacks a fix, stops at the check after project().  The wrapper runs the
# compiler of the build for everything but the contract probe, which it
# accepts as the stock compiler does, so CMake's own compiler test passes.
if(DEFINED SOURCE)
  write_stub(unpatched_wrapper "case \"$probe\" in\n*contract_cache.cpp) exit 0 ;;\nesac\nexec \"${CXX}\" \"$@\"\n")
  file(REMOVE_RECURSE "${WORK}/refusal")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}" -B "${WORK}/refusal" -G Ninja
            "-DCMAKE_CXX_COMPILER=${WORK}/unpatched_wrapper" -DCRUCIBLE_USE_CCACHE=OFF
    RESULT_VARIABLE refusal_code
    OUTPUT_VARIABLE refusal_output
    ERROR_VARIABLE refusal_output)
  if(refusal_code EQUAL 0 OR NOT refusal_output MATCHES "${contract_reason}")
    message(SEND_ERROR "self_test: a configure with a compiler that lacks the contract fix must stop, but it gave "
                       "the exit status '${refusal_code}' and:\n${refusal_output}")
  endif()
  file(REMOVE_RECURSE "${WORK}/refusal")
endif()
