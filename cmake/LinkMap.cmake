# The map of each link.
#
# GNU ld writes the map of each link of an executable, a shared library and a
# module beside the output of the link: <output>.map.  The rule artifact-links
# (`guard check --rule artifact-links`) reads each map through
# utils/assay/readers/linkmap.py, for the archive members that a link takes
# from a library that is not a library of the tree.  The rule placeholder
# <TARGET> is the output of the link, relative to the build directory.
#
# On 2026-10-09, one map held 21 KB to 24 MB.  The 364 maps of the Debug
# build held 86 MB, and those of the Release build held 31 MB.  The relink of
# each artifact of the Debug build took 2.3 s of wall time.  The map keeps the
# names of the demangler of ld, because a link with --no-demangle also gives
# its errors with mangled names.
foreach(_crucible_link_rule CMAKE_CXX_LINK_EXECUTABLE CMAKE_CXX_CREATE_SHARED_LIBRARY CMAKE_CXX_CREATE_SHARED_MODULE)
  string(APPEND ${_crucible_link_rule} " -Wl,-Map=<TARGET>.map")
endforeach()
