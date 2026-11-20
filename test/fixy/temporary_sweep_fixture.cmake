# Makes the directory that test_os_temporary_sweep takes as its TMPDIR, in the
# working directory of the test.  The id 4194305 is above PID_MAX_LIMIT of a
# 64-bit kernel, so no process holds it.  NS is the id of the pid namespace of
# this script, the inode number of the link /proc/self/ns/pid.  CTest runs the
# script and the test in one pid namespace, so NS is the namespace of the test
# too.  OTHER is NS + 1, the id of another namespace.  The directory holds:
#   crucible-NS-4194305-1            a tree of an ended process, which goes
#   crucible-OTHER-4194305-1         a tree of another namespace, which stays
#   crucible-4194305-1               a tree of a name of two numbers, which stays
#   crucible-NS-4194305-2            a link of the form to the directory target
#   target                           a directory with one file, which stays
#   crucible-NS-4194305-6            a file of the form
#   crucible-NS-4194305-007          a zero before a digit
#   crucible-0NS-4194305-1           a zero before the digits of the namespace
#   crucible-NS-0x400001-1           a prefix 0x
#   crucible-NS-4194305              no count
#   crucible-NS-4194305-3-4          a fourth part
#   crucible-NS-4294967296-1         an id out of the range of 32 bits
#   crucible-18446744073709551616-4194305-1
#                                    a namespace out of the range of 64 bits
#   scratch-NS-4194305-5             another prefix
#   crucible-NS-0-1                  the id zero, which the probe cannot judge
#   crucible-NS-2147483648-1         an id that the kernel reads as a negative word
# The directory comes again before each run, because the test changes it.
#
# Run as: cmake -P temporary_sweep_fixture.cmake

file(READ_SYMLINK /proc/self/ns/pid link)
if(NOT link MATCHES "^pid:\\[([0-9]+)\\]$")
  message(FATAL_ERROR "temporary_sweep_fixture: /proc/self/ns/pid gives '${link}', not pid:[NUMBER]")
endif()
set(space ${CMAKE_MATCH_1})
math(EXPR other "${space} + 1")

set(fixture temporary_sweep_fixture)
file(REMOVE_RECURSE ${fixture})
file(MAKE_DIRECTORY
  ${fixture}/crucible-${space}-4194305-1/inner
  ${fixture}/crucible-${other}-4194305-1/inner
  ${fixture}/crucible-4194305-1/inner
  ${fixture}/target
  ${fixture}/crucible-${space}-4194305-007
  ${fixture}/crucible-0${space}-4194305-1
  ${fixture}/crucible-${space}-0x400001-1
  ${fixture}/crucible-${space}-4194305
  ${fixture}/crucible-${space}-4194305-3-4
  ${fixture}/crucible-${space}-4294967296-1
  ${fixture}/crucible-18446744073709551616-4194305-1
  ${fixture}/scratch-${space}-4194305-5
  ${fixture}/crucible-${space}-0-1
  ${fixture}/crucible-${space}-2147483648-1)
file(WRITE ${fixture}/crucible-${space}-4194305-1/inner/file "gone\n")
file(WRITE ${fixture}/crucible-${other}-4194305-1/inner/file "other\n")
file(WRITE ${fixture}/crucible-4194305-1/inner/file "two numbers\n")
file(WRITE ${fixture}/target/keep "keep\n")
file(WRITE ${fixture}/crucible-${space}-4194305-6 "file\n")
file(CREATE_LINK target ${fixture}/crucible-${space}-4194305-2 SYMBOLIC)
