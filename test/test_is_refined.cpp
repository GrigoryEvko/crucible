#include <crucible/safety/IsRefined.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::detail::is_refined_self_test::runtime_smoke_test()) {
        std::fprintf(stderr, "test_is_refined: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_refined: PASS\n");
    return EXIT_SUCCESS;
}
