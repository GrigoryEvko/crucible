#include <crucible/cipher/FederationProtocol.h>

#include <cstddef>
#include <cstdint>

int main() {
    const ::foundation::core::FixedArray<std::byte, 32> buf{};
    auto view = ::crucible::cipher::federation::deserialize_federation_entry(buf.view(), std::uint16_t{0});
    (void)view;
    return 0;
}
