// An explicit specialization of niche_key with a public constructor would
// give each caller a key of the niche of its type, and niche<T>::empty would
// then give the empty value, which no valid value holds.  niche_key has no
// open mark, so the rule specialization-home refuses each specialization of
// it outside Choice.h, also for a type of the same file.

#include <foundation/core/Choice.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr SlotNumber niche_empty(::foundation::core::niche_key<SlotNumber> const&) noexcept {
        return SlotNumber{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

template <>
class foundation::core::niche_key<SlotNumber> {
public:
    constexpr niche_key() noexcept {}
};

int main() {
    SlotNumber const forged = ::foundation::core::niche<SlotNumber>::empty(::foundation::core::niche_key<SlotNumber>{});
    return forged.raw == 0 ? 0 : 1;
}
