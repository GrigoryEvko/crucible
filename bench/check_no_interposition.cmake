# The check after the link of bench_alloc_scaling: the executable defines no
# call of the allocator of the C library and no operator new or delete of C++.
#
#   cmake -DNM=PATH -DBINARY=PATH -P check_no_interposition.cmake
#
# The bench links three reference allocators as static libraries
# (bench/CMakeLists.txt).  A member of such a library that defines malloc or
# operator new would take the place of the allocator of the process for each
# library of the process.  The check reads the symbols that the executable
# defines with nm, and it ends the build with the names that it found.  An
# import of such a name, a line of the kind U, is no definition and passes.
# Complexity: linear in the symbols of the executable.

if(NOT NM OR NOT BINARY)
  message(FATAL_ERROR "check_no_interposition.cmake: give -DNM=PATH and -DBINARY=PATH")
endif()

execute_process(
  COMMAND "${NM}" --defined-only "${BINARY}"
  RESULT_VARIABLE status
  OUTPUT_VARIABLE symbols
  ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "check_no_interposition.cmake: ${NM} --defined-only ${BINARY} failed: ${errors}")
endif()

# The names of the allocator of the C library, and the operators new and
# delete in each form of the Itanium ABI: _Znwm, _Znam, _ZdlPv, _ZdaPv and
# their forms with an alignment, a size or a nothrow tag.
set(_allocator_names
  "malloc|free|calloc|realloc|reallocarray|aligned_alloc|posix_memalign|memalign|valloc|pvalloc|cfree"
  "|malloc_usable_size|free_sized|free_aligned_sized|__libc_malloc|__libc_free|__libc_calloc|__libc_realloc"
  "|__libc_memalign|__malloc_hook|__free_hook|__realloc_hook|__memalign_hook")
string(JOIN "" _allocator_names ${_allocator_names})
# Only a symbol that the dynamic linker can bind interposes: a global or weak
# kind (an upper-case letter), a unique global (u) or an indirect function
# (i).  A local symbol of the same name binds nothing outside its object.
set(_kind "([A-Z]|u|i)")
string(REGEX MATCHALL "[^\n]*\n" _lines "${symbols}")
set(_found "")
foreach(_line IN LISTS _lines)
  string(STRIP "${_line}" _line)
  if(_line MATCHES "^[0-9a-fA-F]* *${_kind} (${_allocator_names})$" OR _line MATCHES "^[0-9a-fA-F]* *${_kind} _Z(nw|na|dl|da)")
    list(APPEND _found "${_line}")
  endif()
endforeach()

if(_found)
  string(JOIN "\n  " _report ${_found})
  message(FATAL_ERROR "check_no_interposition.cmake: ${BINARY} defines a symbol of the allocator of the process, so "
                      "a reference allocator would interpose it:\n  ${_report}\nBuild each reference with its symbol "
                      "prefix, and leave out each unit that defines an operator new or delete.")
endif()
