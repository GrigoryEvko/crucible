# The map of each link.
#
# mold writes the map of each link of an executable, a shared library and a
# module beside the output of the link: <output>.map.  Each link uses mold
# (cmake/Linker.cmake).  utils/assay/readers/linkmap.py reads each map.  The
# rule artifact-links (`guard check --rule artifact-links`) reads the archive
# members that a link takes from a library that is not a library of the tree.
# The rule artifact-hardening reads the input files of the link of an
# artifact with no control-flow protection.  The rule placeholder <TARGET> is
# the output of the link, relative to the build directory.
#
# On 2026-10-17, one map of the Debug build held 5.6 KB to 19.3 MB, and the
# 364 maps held 66 MB.  The reader read the 364 maps in 0.71 s.  The map keeps
# the names of the demangler of mold, because a link with --no-demangle also
# gives its errors with mangled names.
foreach(_crucible_link_rule CMAKE_CXX_LINK_EXECUTABLE CMAKE_CXX_CREATE_SHARED_LIBRARY CMAKE_CXX_CREATE_SHARED_MODULE)
  string(APPEND ${_crucible_link_rule} " -Wl,-Map=<TARGET>.map")
endforeach()
