#include <crucible/safety/IsMemOrder.h>

#include <cstdio>
#include <cstdlib>

int main() {
    if (!::crucible::safety::extract::is_mem_order_smoke_test()) {
        std::fprintf(stderr, "test_is_mem_order: FAIL\n");
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_is_mem_order: PASS\n");
    return EXIT_SUCCESS;
}
