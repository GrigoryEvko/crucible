# CMakePin.cmake: reject a CMake of another version than the pin.
#
# utils/toolchain/cmake/requirements.txt pins one version of CMake and ctest.
# Each line `cmake==X.Y.Z ; platform_machine == "MACHINE"` gives that version
# for one machine.  The root CMakeLists.txt calls crucible_cmake_pin_problem()
# with CMAKE_VERSION before project(), so a configure with a CMake of another
# version stops before CMake tests the compiler.  The test cmake_pin rejects a
# ctest of another version (utils/scripts/cmake_pin.py).
# cmake/CMakePinSelfTest.cmake is the test of this file.

# Set OUT_VAR to the pinned version of PIN_FILE, or to an empty string when the
# file gives no version.  Set PROBLEM_VAR to an empty string, or to a message
# that tells what is wrong with the file.
function(crucible_cmake_pin_version pin_file out_var problem_var)
  set(${out_var} "" PARENT_SCOPE)
  if(NOT EXISTS "${pin_file}")
    set(${problem_var} "Crucible CMake pin: the pin file ${pin_file} does not exist." PARENT_SCOPE)
    return()
  endif()
  # A line of the pin ends in a backslash and holds a semicolon, so a CMake
  # list of the lines would join two lines.  Each match stops before the
  # first blank, semicolon or backslash.
  file(READ "${pin_file}" text)
  string(REGEX MATCHALL "\ncmake==[^ ;\n\\\\]*" requirements "\n${text}")
  set(pinned "")
  foreach(requirement IN LISTS requirements)
    string(STRIP "${requirement}" requirement)
    if(NOT requirement MATCHES "^cmake==([0-9]+\\.[0-9]+\\.[0-9]+)$")
      string(CONCAT problem "Crucible CMake pin: the requirement '${requirement}' of ${pin_file} does not give "
                            "a version X.Y.Z.")
      set(${problem_var} "${problem}" PARENT_SCOPE)
      return()
    endif()
    if(pinned STREQUAL "")
      set(pinned "${CMAKE_MATCH_1}")
    elseif(NOT pinned STREQUAL CMAKE_MATCH_1)
      string(CONCAT problem "Crucible CMake pin: ${pin_file} gives the two versions ${pinned} and "
                            "${CMAKE_MATCH_1}.  Give one version on each line.")
      set(${problem_var} "${problem}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  if(pinned STREQUAL "")
    set(${problem_var} "Crucible CMake pin: ${pin_file} has no line 'cmake==X.Y.Z'." PARENT_SCOPE)
    return()
  endif()
  set(${out_var} "${pinned}" PARENT_SCOPE)
  set(${problem_var} "" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to an empty string when VERSION is the version that PIN_FILE
# pins.  Otherwise set it to a message that gives the pinned version and the
# commands that install it.
function(crucible_cmake_pin_problem pin_file version out_var)
  crucible_cmake_pin_version("${pin_file}" pinned problem)
  if(NOT problem STREQUAL "")
    set(${out_var} "${problem}" PARENT_SCOPE)
    return()
  endif()
  if(version STREQUAL pinned)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  string(CONCAT report
    "Crucible CMake pin: CMake ${version} runs this configure, but ${pin_file} pins CMake ${pinned}.  "
    "The tree configures, builds and tests only with the pinned CMake and ctest.  "
    "Install them with one of these two commands:\n"
    "  bash ${root}/utils/scripts/install-cmake.sh\n"
    "  python3 -m pip install --require-hashes -r ${pin_file}\n"
    "Then put the bin directory of that install first in PATH.  The first command prints that directory.  "
    "Then configure again.")
  set(${out_var} "${report}" PARENT_SCOPE)
endfunction()
