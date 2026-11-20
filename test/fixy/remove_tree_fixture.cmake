# Makes the tree that test_os_remove_tree removes, in the working directory of
# the test.  The directory outside holds one file.  The directory tree holds a
# link to the directory outside and a link to its file.  The links are
# relative, so the tree has the same meaning in each build directory.  The
# tree comes again before each run, because the test removes it.
#
# Run as: cmake -P remove_tree_fixture.cmake

file(REMOVE_RECURSE remove_tree_fixture)
file(MAKE_DIRECTORY remove_tree_fixture/outside remove_tree_fixture/tree)
file(WRITE remove_tree_fixture/outside/keep "keep\n")
file(CREATE_LINK ../outside remove_tree_fixture/tree/directory-link SYMBOLIC)
file(CREATE_LINK ../outside/keep remove_tree_fixture/tree/file-link SYMBOLIC)
