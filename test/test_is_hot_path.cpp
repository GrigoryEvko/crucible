#include <crucible/safety/IsHotPath.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::is_hot_path_smoke_test()) {
        std::fprintf(stderr, "test_is_hot_path: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_hot_path: PASS\n");
    return EXIT_SUCCESS;
}
