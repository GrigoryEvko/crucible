#include <crucible/cipher/ComputationCacheFederation.h>
#include <foundation/effects/Row.h>

#include <cstddef>

namespace eff = ::foundation::effects;

inline void neg_payload(int) noexcept {}

int main() {
    constexpr ::foundation::core::FixedArray<std::byte, 32> key_bytes{};
    const ::crucible::cipher::federation::FederationHashKey hash_key{key_bytes.view()};
    ::foundation::core::FixedArray<std::byte, 64> buf{};
    auto written =
        ::crucible::cipher::federation::serialize_computation_cache_federation_entry<&neg_payload, eff::Row<>, int>(
            hash_key, buf.view(), ::fixy::View<std::byte const>{});
    (void)written;
    return 0;
}
