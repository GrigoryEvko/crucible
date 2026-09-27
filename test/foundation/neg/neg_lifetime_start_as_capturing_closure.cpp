// A closure that captures an epoch, started over bytes.  A closure with
// captures has a trivial copy constructor, so it is an implicit-lifetime
// type.  GCC 16 reflects no capture, so a walk over bases and members
// sees nothing in it.  The start would give an epoch that no successor
// step made.  The checked start refuses a class whose state the walk
// cannot read.

#include <foundation/Lifetime.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>

#include <cstdint>
#include <type_traits>

namespace fl = ::foundation::lifetime;
namespace lat = ::foundation::algebra::lattices;

namespace {
inline constexpr auto carry_epoch = [held = lat::EpochLattice::bottom()] { return held; };
using EpochCarrier = std::remove_const_t<decltype(carry_epoch)>;
}  // namespace

int main() {
    alignas(EpochCarrier) unsigned char storage[sizeof(EpochCarrier)]{};
    auto forged = fl::start_as_array<EpochCarrier>(storage, 1);
    return static_cast<int>(forged[0]().raw());
}
