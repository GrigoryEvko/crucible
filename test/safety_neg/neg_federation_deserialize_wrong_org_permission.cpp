#include <crucible/cipher/FederationProtocol.h>
#include <crucible/permissions/FederationPermission.h>

#include <array>
#include <cstdint>

struct OrgA {};
struct OrgB {};

int main() {
    std::array<std::uint8_t, 32> buf{};
    auto permission = ::crucible::safety::mint_permission_root<
        ::crucible::permissions::tag::FederatedPeer<OrgA>>();

    auto view =
        ::crucible::cipher::federation::deserialize_federation_entry<OrgB>(
            permission, buf, std::uint16_t{0});
    (void)view;
    return 0;
}
