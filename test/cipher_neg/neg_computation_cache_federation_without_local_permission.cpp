#include <crucible/cipher/ComputationCacheFederation.h>
#include <foundation/effects/Row.h>

#include <cstddef>

namespace eff = ::foundation::effects;

inline void neg_payload(int) noexcept {}

int main() {
    ::foundation::core::FixedArray<std::byte, 32> buf{};
    auto written =
        ::crucible::cipher::federation::serialize_computation_cache_federation_entry<&neg_payload, eff::Row<>, int>(
            buf.view(), ::fixy::View<std::byte const>{});
    (void)written;
    return 0;
}
