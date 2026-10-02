# The test of cmake/CMakePin.cmake.  ctest runs it as:
#   cmake -DWORK=<directory> -DMAKE_PROGRAM=<ninja> -P cmake/CMakePinSelfTest.cmake
#
# The check must accept the pinned version, and it must reject each other
# version with a message that gives the pin and the two install commands.  It
# must reject a pin file that is absent, that has no requirement, that gives
# no version X.Y.Z or that gives two versions.  The pin of the tree must give
# the version of the CMake that runs this test.  Two configures of a scratch
# project that makes the call of the root CMakeLists.txt show the whole path.
# A pin of another version stops the configure with the message, and a pin of
# the running version lets the configure complete.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/CMakePin.cmake")

if(NOT DEFINED WORK OR NOT DEFINED MAKE_PROGRAM)
  message(FATAL_ERROR "CMakePinSelfTest.cmake: give -DWORK=<directory> and -DMAKE_PROGRAM=<ninja>")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(failures "")
set(digest "27b024e903ef985b37183d754a5c61230b56b41fe0971cd44b71b80c787ec594")
if(NOT CMAKE_VERSION MATCHES "^([0-9]+)\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "CMakePinSelfTest.cmake: CMake ${CMAKE_VERSION} runs this test, and its version is "
                      "not X.Y.Z.  Run the test with the pinned CMake.")
endif()
math(EXPR other_major "${CMAKE_MATCH_1} + 1")
set(other "${other_major}.0.0")

# Write the pin file WORK/NAME with one requirement for each version.
function(write_pin name)
  set(text "# A pin of the test.\n")
  foreach(version IN LISTS ARGN)
    string(APPEND text "cmake==${version} ; platform_machine == \"x86_64\" \\\n    --hash=sha256:${digest}\n")
  endforeach()
  file(WRITE "${WORK}/${name}" "${text}")
endfunction()

# Record a failure when the check accepts VERSION against PIN_FILE, or rejects
# it for a reason that does not match each pattern of ARGN.
function(expect_refusal label pin_file version)
  crucible_cmake_pin_problem("${pin_file}" "${version}" problem)
  if(problem STREQUAL "")
    set(failures "${failures}\n  ${label}: the check accepts it" PARENT_SCOPE)
    return()
  endif()
  foreach(pattern IN LISTS ARGN)
    if(NOT problem MATCHES "${pattern}")
      set(failures "${failures}\n  ${label}: the message does not match '${pattern}':\n${problem}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# Record a failure when the check rejects VERSION against PIN_FILE.
function(expect_pass label pin_file version)
  crucible_cmake_pin_problem("${pin_file}" "${version}" problem)
  if(NOT problem STREQUAL "")
    set(failures "${failures}\n  ${label}: the check rejects it:\n${problem}" PARENT_SCOPE)
  endif()
endfunction()

write_pin(running.txt "${CMAKE_VERSION}" "${CMAKE_VERSION}")
write_pin(other.txt "${other}")
write_pin(empty.txt)
write_pin(two_versions.txt "${CMAKE_VERSION}" "${other}")
write_pin(short_version.txt "4.4")

expect_pass("the pin of the tree" "${CMAKE_CURRENT_LIST_DIR}/../utils/toolchain/cmake/requirements.txt"
            "${CMAKE_VERSION}")
expect_pass("a pin of the running version on two lines" "${WORK}/running.txt" "${CMAKE_VERSION}")
string(REPLACE "." "\\." escaped_running "${CMAKE_VERSION}")
string(REPLACE "." "\\." escaped_other "${other}")
expect_refusal("a pin of another version" "${WORK}/other.txt" "${CMAKE_VERSION}"
               "CMake ${escaped_running} runs this configure, but .* pins CMake ${escaped_other}\\."
               "bash .*/utils/scripts/install-cmake\\.sh"
               "python3 -m pip install --require-hashes -r .*/other\\.txt")
expect_refusal("a version with a suffix" "${WORK}/running.txt" "${CMAKE_VERSION}-rc1" "pins CMake")
expect_refusal("a version with one more digit" "${WORK}/running.txt" "${CMAKE_VERSION}0" "pins CMake")
expect_refusal("an absent pin file" "${WORK}/absent.txt" "${CMAKE_VERSION}" "does not exist")
expect_refusal("a pin file with no requirement" "${WORK}/empty.txt" "${CMAKE_VERSION}" "has no line 'cmake==X\\.Y\\.Z'")
expect_refusal("a pin file with two versions" "${WORK}/two_versions.txt" "${CMAKE_VERSION}" "gives the two versions")
expect_refusal("a version that is not X.Y.Z" "${WORK}/short_version.txt" "${CMAKE_VERSION}"
               "does not give a version X\\.Y\\.Z")

# A scratch project that makes the call of the root CMakeLists.txt.
file(MAKE_DIRECTORY "${WORK}/project")
file(WRITE "${WORK}/project/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.25)\n"
  "include(\"${CMAKE_CURRENT_LIST_DIR}/CMakePin.cmake\")\n"
  "crucible_cmake_pin_problem(\"\${PIN}\" \"\${CMAKE_VERSION}\" problem)\n"
  "if(NOT problem STREQUAL \"\")\n  message(FATAL_ERROR \"\${problem}\")\nendif()\n"
  "project(cmake_pin_probe NONE)\n")

# Configure the scratch project with the pin WORK/PIN_NAME, and set CODE_VAR and
# OUTPUT_VAR to the exit status and the output of the configure.
function(configure_with pin_name code_var output_var)
  file(REMOVE_RECURSE "${WORK}/build-${pin_name}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${WORK}/project" -B "${WORK}/build-${pin_name}" -G Ninja
            "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" "-DPIN=${WORK}/${pin_name}"
    RESULT_VARIABLE code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  set(${code_var} "${code}" PARENT_SCOPE)
  set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

configure_with(other.txt code output)
if(code EQUAL 0 OR NOT output MATCHES "pins CMake ${escaped_other}")
  string(APPEND failures "\n  a configure with a pin of another version must stop with the message, but it gave "
                         "the exit status '${code}' and:\n${output}")
endif()
configure_with(running.txt code output)
if(NOT code EQUAL 0)
  string(APPEND failures "\n  a configure with a pin of the running version must complete, but it gave the exit "
                         "status '${code}' and:\n${output}")
endif()

file(REMOVE_RECURSE "${WORK}")
if(NOT failures STREQUAL "")
  message(FATAL_ERROR "CMakePinSelfTest:${failures}")
endif()
message(STATUS "CMakePinSelfTest: the pin check gives the correct verdict for each case")
