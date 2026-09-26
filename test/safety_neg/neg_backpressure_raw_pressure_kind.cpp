// The constructor that sets the resource of a pressure sample is private,
// so a sample cannot name a resource around mint_resource_pressure and
// its catalog clause.

#include <crucible/cntp/Backpressure.h>

int main() {
    namespace cntp = crucible::cntp;
    constexpr auto bogus = static_cast<::foundation::effects::ResourceKind>(static_cast<unsigned char>(0xff));
    auto ppm = cntp::admit_resource_pressure_ppm(1).value();
    cntp::ResourcePressure const pressure{bogus, ppm};
    return static_cast<int>(pressure.used_ppm().value());
}
