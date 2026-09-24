#include <crucible/safety/IsProgress.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::is_progress_smoke_test()) {
        std::fprintf(stderr, "test_is_progress: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_progress: PASS\n");
    return EXIT_SUCCESS;
}
