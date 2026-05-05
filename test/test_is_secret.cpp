#include <crucible/safety/IsSecret.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::detail::is_secret_self_test::runtime_smoke_test()) {
        std::fprintf(stderr, "test_is_secret: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_secret: PASS\n");
    return EXIT_SUCCESS;
}
