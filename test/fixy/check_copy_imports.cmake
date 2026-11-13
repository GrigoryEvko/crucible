# The import check of fixy::copy.
#
# Usage: cmake -DNM=<nm> -DFIXED=<object> -DDYNAMIC=<object> -P check_copy_imports.cmake
#
# FIXED is the object of copy_imports_fixed.cpp.  Its fixed copies of at most
# 64 bytes compile to loads and stores, so it must import no symbol whose name
# holds memmove or memcpy.  GCC calls the library memmove for a memmove
# builtin of most of these sizes in a cold block and under AddressSanitizer.
# So the base copies with its own words (fixy::copy in
# include/foundation/core/Region.h).
# DYNAMIC is the object of copy_imports_dynamic.cpp.  Its copy has a count of
# run time, so it must import memmove.  This control shows that nm reads the
# objects and that the pattern finds an import.

foreach(_input NM FIXED DYNAMIC)
  if(NOT DEFINED ${_input} OR "${${_input}}" STREQUAL "")
    message(FATAL_ERROR "check_copy_imports.cmake needs -D${_input}=<value>")
  endif()
endforeach()

# Writes the undefined symbols of object into the variable out, one on each
# line.  A failure of nm stops the check.
function(read_imports object out)
  execute_process(COMMAND "${NM}" --undefined-only --format=just-symbols "${object}"
                  RESULT_VARIABLE _status OUTPUT_VARIABLE _symbols ERROR_VARIABLE _error)
  if(NOT _status EQUAL 0)
    message(FATAL_ERROR "${NM} cannot read the undefined symbols of ${object}: ${_error}")
  endif()
  set(${out} "${_symbols}" PARENT_SCOPE)
endfunction()

read_imports("${FIXED}" _fixed_imports)
string(REGEX MATCHALL "[^\n]*mem(move|cpy)[^\n]*" _copy_calls "${_fixed_imports}")
if(_copy_calls)
  message(FATAL_ERROR "The fixed copies of ${FIXED} import ${_copy_calls}.  A fixed copy of at most 64 bytes must "
                      "compile to loads and stores in each build.  Correct fixy::copy in "
                      "include/foundation/core/Region.h")
endif()

read_imports("${DYNAMIC}" _dynamic_imports)
if(NOT _dynamic_imports MATCHES "(^|\n)memmove(\n|$)")
  message(FATAL_ERROR "The control ${DYNAMIC} imports no memmove, so the check cannot see an import.  The "
                      "undefined symbols are: ${_dynamic_imports}")
endif()
