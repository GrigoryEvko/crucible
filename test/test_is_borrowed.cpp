#include <crucible/safety/IsBorrowed.h>

#include <cstdio>
#include <cstdlib>

int main() {
    ::crucible::safety::extract::detail::is_borrowed_self_test::runtime_smoke_test();
    std::fprintf(stderr, "test_is_borrowed: PASS\n");
    return EXIT_SUCCESS;
}
