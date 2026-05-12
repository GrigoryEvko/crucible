#include <crucible/cntp/Backpressure.h>

// GAPS-137 fixture #2: resource pressure mints are gated by the
// canonical effects::ResourceKind catalog. Numeric casts outside the
// catalog cannot form a typed pressure sample.

int main() {
    namespace cntp = crucible::cntp;
    namespace effects = crucible::effects;
    constexpr auto bogus =
        static_cast<effects::ResourceKind>(static_cast<unsigned char>(0xff));
    auto pressure = cntp::mint_resource_pressure<bogus>(1);
    (void)pressure;
    return 0;
}
