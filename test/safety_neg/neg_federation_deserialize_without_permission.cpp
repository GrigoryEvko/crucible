#include <crucible/cipher/FederationProtocol.h>

#include <array>
#include <cstdint>

int main() {
    std::array<std::uint8_t, 32> buf{};
    auto view = ::crucible::cipher::federation::deserialize_federation_entry(
        buf, std::uint16_t{0});
    (void)view;
    return 0;
}
