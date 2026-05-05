#include <crucible/safety/IsBits.h>

#include <cstdio>
#include <cstdlib>

int main() {
    ::crucible::safety::extract::detail::is_bits_self_test::runtime_smoke_test();
    std::fprintf(stderr, "test_is_bits: PASS\n");
    return EXIT_SUCCESS;
}
