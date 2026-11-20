# Makes the directory that test_os_temporary_sweep takes as its TMPDIR, in the
# working directory of the test.  The id 4194305 is above PID_MAX_LIMIT of a
# 64-bit kernel, so no process holds it.  The directory holds:
#   crucible-4194305-1           a tree of an ended process, which goes
#   crucible-4194305-2           a link of the form to the directory target
#   target                       a directory with one file, which stays
#   crucible-4194305-6           a file of the form
#   crucible-4194305-007         a zero before a digit
#   crucible-0x400001-1          a prefix 0x
#   crucible-4194305             no count
#   crucible-4194305-3-4         a third part
#   crucible-4294967296-1        an id out of the range of 32 bits
#   scratch-4194305-5            another prefix
#   crucible-0-1                 the id zero, which the probe cannot judge
#   crucible-2147483648-1        an id that the kernel reads as a negative word
# The directory comes again before each run, because the test changes it.
#
# Run as: cmake -P temporary_sweep_fixture.cmake

set(fixture temporary_sweep_fixture)
file(REMOVE_RECURSE ${fixture})
file(MAKE_DIRECTORY
  ${fixture}/crucible-4194305-1/inner
  ${fixture}/target
  ${fixture}/crucible-4194305-007
  ${fixture}/crucible-0x400001-1
  ${fixture}/crucible-4194305
  ${fixture}/crucible-4194305-3-4
  ${fixture}/crucible-4294967296-1
  ${fixture}/scratch-4194305-5
  ${fixture}/crucible-0-1
  ${fixture}/crucible-2147483648-1)
file(WRITE ${fixture}/crucible-4194305-1/inner/file "gone\n")
file(WRITE ${fixture}/target/keep "keep\n")
file(WRITE ${fixture}/crucible-4194305-6 "file\n")
file(CREATE_LINK target ${fixture}/crucible-4194305-2 SYMBOLIC)
