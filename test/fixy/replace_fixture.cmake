# Makes the directories that test_os_fs_replace replaces a file in, in the
# working directory of the test.  The directory target is empty, and link is a
# relative symbolic link to it.  So a path through link/ names a file in
# target, in each build directory.  The directories come again before each run.
#
# Run as: cmake -P replace_fixture.cmake

file(REMOVE_RECURSE replace_fixture)
file(MAKE_DIRECTORY replace_fixture/target)
file(CREATE_LINK target replace_fixture/link SYMBOLIC)
