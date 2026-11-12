// A file outside Ref.h can specialize the niche of a Ref for a type of its
// own.  Ref grants no friend to a niche, so such a specialization cannot call
// the private constructor of Ref.  A null Ref comes only from a hidden friend
// of Ref, which takes the niche key.

#include <foundation/core/Ref.h>

struct Forged {
    int value = 0;
};

struct ForgeBrand {};

template <>
struct foundation::core::niche<foundation::core::Ref<Forged, ForgeBrand>> {
    [[nodiscard]] static foundation::core::Ref<Forged, ForgeBrand> forge() noexcept {
        return foundation::core::Ref<Forged, ForgeBrand>{static_cast<Forged*>(nullptr)};
    }
};

int main() {
    foundation::core::Ref<Forged, ForgeBrand> const forged =
        foundation::core::niche<foundation::core::Ref<Forged, ForgeBrand>>::forge();
    return forged->value;
}
