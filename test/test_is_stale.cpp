#include <crucible/safety/IsStale.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::detail::is_stale_self_test::runtime_smoke_test()) {
        std::fprintf(stderr, "test_is_stale: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_stale: PASS\n");
    return EXIT_SUCCESS;
}
