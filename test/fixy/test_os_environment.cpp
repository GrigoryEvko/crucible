// The environment door, run against the environment of the C library.
//
// The CMake list of the test sets CRUCIBLE_TEST_DOOR_VALUE to "door value"
// and CRUCIBLE_TEST_DOOR_EMPTY to the empty value.  The door gives each
// value as the C library holds it, an empty value as an empty text, and no
// value for a variable that the environment does not hold.

#include <fixy/Core.h>
#include <fixy/os/Environment.h>

#include "../test_assert.h"

namespace {

// True when the environment holds name with the characters expected.
[[nodiscard]] bool holds(fixy::CText name, fixy::TextView expected) noexcept {
    return fixy::env::read_variable(name).match([expected](fixy::TextView value) { return value == expected; },
                                                [] { return false; });
}

[[nodiscard]] int check_reads() noexcept {
    if (!holds("CRUCIBLE_TEST_DOOR_VALUE", "door value")) return 1;
    if (!holds("CRUCIBLE_TEST_DOOR_EMPTY", fixy::TextView{})) return 2;
    if (fixy::env::read_variable("CRUCIBLE_TEST_DOOR_ABSENT").is_some()) return 3;
    return 0;
}

}  // namespace

int main() {
    int const failed = check_reads();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_environment: check {} failed\n", failed);
        return failed;
    }
    crucible::test::pass("test_os_environment: passed\n");
    return 0;
}
