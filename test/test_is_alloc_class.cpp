#include <crucible/safety/IsAllocClass.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::is_alloc_class_smoke_test()) {
        std::fprintf(stderr, "test_is_alloc_class: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_alloc_class: PASS\n");
    return EXIT_SUCCESS;
}
