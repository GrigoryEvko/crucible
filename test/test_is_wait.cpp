#include <crucible/safety/IsWait.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::is_wait_smoke_test()) {
        std::fprintf(stderr, "test_is_wait: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_wait: PASS\n");
    return EXIT_SUCCESS;
}
