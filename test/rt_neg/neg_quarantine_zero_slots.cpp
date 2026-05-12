#include <crucible/rt/Quarantine.h>

using BadPolicy = crucible::rt::QuarantinePolicy<0>;

int main() {
    (void)sizeof(BadPolicy);
    return 0;
}
