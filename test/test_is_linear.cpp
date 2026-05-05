#include <crucible/safety/IsLinear.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::detail::is_linear_self_test::runtime_smoke_test()) {
        std::fprintf(stderr, "test_is_linear: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_linear: PASS\n");
    return EXIT_SUCCESS;
}
