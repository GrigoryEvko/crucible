#include <crucible/cntp/_wip/Wireguard.h>

int main() {
    auto key = crucible::cntp::_wip::admit_wireguard_secret_key_b64(
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=").value();
    auto copy = key;
    (void)copy;
}
