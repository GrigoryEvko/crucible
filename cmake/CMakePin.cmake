# CMakePin.cmake: reject a CMake of another version than the pin.
#
# The rows of the pin cmake of utils/pins.txt give one version of CMake and
# ctest.  Each row `pin cmake via=archive version=X.Y.Z platform=... | ...`
# gives that version for one platform.  The root CMakeLists.txt calls
# crucible_cmake_pin_problem() with CMAKE_VERSION before project(), so a
# configure with a CMake of another version stops before CMake tests the
# compiler.  The test cmake_pin rejects a ctest of another version (`setup
# check`, utils/assay/setup/pins.py).  cmake/CMakePinSelfTest.cmake is the test
# of this file.

# Set OUT_VAR to the pinned version of PIN_FILE, or to an empty string when the
# file gives no version.  Set PROBLEM_VAR to an empty string, or to a message
# that tells what is wrong with the file.
function(crucible_cmake_pin_version pin_file out_var problem_var)
  set(${out_var} "" PARENT_SCOPE)
  if(NOT EXISTS "${pin_file}")
    set(${problem_var} "Crucible CMake pin: the pin file ${pin_file} does not exist." PARENT_SCOPE)
    return()
  endif()
  # Each line is one element of the list.  A semicolon in the cell of a row
  # divides that row into two elements, and only the first starts with the
  # head of the row.
  file(STRINGS "${pin_file}" rows REGEX "^pin cmake ")
  set(pinned "")
  foreach(row IN LISTS rows)
    if(NOT row MATCHES "^pin cmake ")
      continue()
    endif()
    if(NOT row MATCHES "^pin cmake[^|]* version=([^ |]+)")
      set(${problem_var} "Crucible CMake pin: the row '${row}' of ${pin_file} gives no field version." PARENT_SCOPE)
      return()
    endif()
    set(version "${CMAKE_MATCH_1}")
    if(NOT version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
      string(CONCAT problem "Crucible CMake pin: the row of cmake with version=${version} of ${pin_file} does not "
                            "give a version X.Y.Z.")
      set(${problem_var} "${problem}" PARENT_SCOPE)
      return()
    endif()
    if(pinned STREQUAL "")
      set(pinned "${version}")
    elseif(NOT pinned STREQUAL version)
      string(CONCAT problem "Crucible CMake pin: ${pin_file} gives the two versions ${pinned} and ${version} of "
                            "cmake.  Give one version on each row.")
      set(${problem_var} "${problem}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  if(pinned STREQUAL "")
    set(${problem_var} "Crucible CMake pin: ${pin_file} has no row 'pin cmake ... version=X.Y.Z'." PARENT_SCOPE)
    return()
  endif()
  set(${out_var} "${pinned}" PARENT_SCOPE)
  set(${problem_var} "" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to an empty string when VERSION is the version that PIN_FILE
# pins.  Otherwise set it to a message that gives the pinned version and the
# command that installs it.
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
    "Install them with this command:\n"
    "  python3 ${root}/utils/bin/setup install cmake\n"
    "Then put the bin directory of that install first in PATH.  "
    "`python3 ${root}/utils/bin/setup install --print-bin cmake` prints that directory.  Then configure again.")
  set(${out_var} "${report}" PARENT_SCOPE)
endfunction()
