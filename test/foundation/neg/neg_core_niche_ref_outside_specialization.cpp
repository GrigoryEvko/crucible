// The rule specialization-home refuses a niche of a Ref outside Ref.h, and
// this probe forges one in an opt-out region.  Ref has no constructor from a
// pointer and grants a niche no friend, so such a specialization cannot build
// a Ref from the null pointer.  A null Ref comes only from a hidden friend of
// Ref, which takes the niche key.

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>

struct Forged {
    int value = 0;
};

struct ForgeBrand {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the probe forges a niche of a Ref to show that Ref gives a niche no friend")
template <>
struct foundation::core::niche<foundation::core::Ref<Forged, ForgeBrand>> {
    [[nodiscard]] static foundation::core::Ref<Forged, ForgeBrand> forge() noexcept {
        return foundation::core::Ref<Forged, ForgeBrand>{static_cast<Forged*>(nullptr)};
    }
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    foundation::core::Ref<Forged, ForgeBrand> const forged =
        foundation::core::niche<foundation::core::Ref<Forged, ForgeBrand>>::forge();
    return forged->value;
}
