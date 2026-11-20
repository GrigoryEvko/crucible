// The main of each property test.
//
// CMake links this file into each property test.  The C runtime gives the
// arguments of main as a count and an array of texts, which is a foreign ABI.
// So main stands in one ABI region, and it gives the arguments to the body of
// the test as the Args of fixy/os/Args.h.

#include "property_runner.h"

#include <fixy/os/Args.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")
int main(int argc, char** argv) { return ::crucible::fuzz::prop::run_property(::fixy::os::args(argc, argv)); }
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
