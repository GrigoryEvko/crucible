// Two fatal exits on one thread, each one caught by an abort probe.
//
// test/harness/check_output.py runs this program and reads its standard
// error.  The text of each fatal exit must be there.  A report that leaves
// the thread marked as one that reports would make the second fatal exit
// abort with no text.

#include "../test_abort_probe.h"

#include <fixy/Core.h>

int main() {
    bool const first_aborts = crucible::test::aborts([] { ::fixy::fatal("the first fatal exit of the probe"); });
    bool const second_aborts = crucible::test::aborts([] { ::fixy::fatal("the second fatal exit of the probe"); });
    return first_aborts && second_aborts ? 0 : 1;
}
