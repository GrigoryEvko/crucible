// Two fatal exits on one thread, each one caught by an abort probe.
//
// test/harness/check_output.py runs this program and reads its standard
// error.  The text of each fatal exit must be there.  A report that leaves
// the thread marked as one that reports would make the second fatal exit
// abort with no text.
//
// Then a third arm opens a second arm while it is open.  The probe has one
// jump buffer, so the second arm is a fatal exit, and the third arm catches
// it.

#include "../test_abort_probe.h"

#include <fixy/Core.h>

int main() {
    bool const first_aborts = crucible::test::aborts([] { ::fixy::fatal("the first fatal exit of the probe"); });
    bool const second_aborts = crucible::test::aborts([] { ::fixy::fatal("the second fatal exit of the probe"); });
    bool const second_open_arm_aborts =
        crucible::test::aborts([] { static_cast<void>(crucible::test::aborts([] {})); });
    return first_aborts && second_aborts && second_open_arm_aborts ? 0 : 1;
}
