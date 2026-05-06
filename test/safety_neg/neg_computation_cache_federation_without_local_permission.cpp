#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/effects/EffectRow.h>

#include <array>
#include <cstdint>
#include <span>

namespace eff = ::crucible::effects;

inline void neg_payload(int) noexcept {}

int main() {
    std::array<std::uint8_t, 32> buf{};
    auto written =
        ::crucible::cipher::federation::
            serialize_computation_cache_federation_entry<
                &neg_payload, eff::Row<>, int>(
                buf, std::span<const std::uint8_t>{});
    (void)written;
    return 0;
}
