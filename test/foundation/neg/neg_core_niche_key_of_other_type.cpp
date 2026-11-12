// The storage of an Option gives the niche of a payload the key of that
// payload.  A key opens the niche of its own type only, so the niche of one
// payload cannot read the empty value of the niche of a Ref.

#include <foundation/core/Ref.h>

#include <cstdint>

struct SlotNumber {
    std::uint32_t raw = 0;

    [[nodiscard]] static constexpr SlotNumber
    niche_empty(::foundation::core::niche_key<SlotNumber> const& key) noexcept {
        ::foundation::core::Ref<int> const forged = ::foundation::core::niche<::foundation::core::Ref<int>>::empty(key);
        return SlotNumber{static_cast<std::uint32_t>(*forged)};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<SlotNumber> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};

int main() { return 0; }
