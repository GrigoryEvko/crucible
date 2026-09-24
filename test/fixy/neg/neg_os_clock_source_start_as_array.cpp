// The checked lifetime start refuses a clock reading.  A reading keeps a
// trivial copy constructor, so the ABI passes it in a register, and that
// makes it an implicit-lifetime type.  The class carries the annotation
// no_start_over_bytes, and start_as_array refuses it at its constraint.  A
// reading started over bytes would state a value that no clock returned.

#include <fixy/os/ClockSource.h>
#include <foundation/Lifetime.h>

#include <cstdint>

namespace ml = foundation::algebra::lattices;

int main() {
    using Reading = fixy::ClockSource<ml::ClockSource::Boot, std::uint64_t>;
    alignas(Reading) unsigned char bytes[sizeof(Reading)]{};
    auto const forged = ::foundation::lifetime::start_as_array<Reading>(bytes, 1);
    return static_cast<int>(forged.size());
}
