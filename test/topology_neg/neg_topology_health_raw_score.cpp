#include <crucible/topology/Health.h>

static bool below_quarantine(crucible::topology::HealthScore score) {
    return score.raw() < 400;
}

int main() {
    return below_quarantine(0) ? 0 : 1;
}
