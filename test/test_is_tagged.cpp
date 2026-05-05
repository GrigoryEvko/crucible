#include <crucible/safety/IsTagged.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::detail::is_tagged_self_test::runtime_smoke_test()) {
        std::fprintf(stderr, "test_is_tagged: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_tagged: PASS\n");
    return EXIT_SUCCESS;
}
