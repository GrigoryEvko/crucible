#include <crucible/topology/AsymmetricFailure.h>

using BadDetector = crucible::topology::AsymmetricFailureDetector<2, 0, 2>;

int main() {
    (void)sizeof(BadDetector);
    return 0;
}
