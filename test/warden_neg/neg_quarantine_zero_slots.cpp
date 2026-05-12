#include <crucible/warden/Quarantine.h>

using BadPolicy = crucible::warden::QuarantinePolicy<0>;

int main() {
    (void)sizeof(BadPolicy);
    return 0;
}
