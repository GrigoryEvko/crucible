// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags. The runtime section then drives the
// same surface through non-constant values, catching consteval and
// inline-body faults that a static_assert pass alone would mask.

#include <crucible/safety/source/Arch.h>

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/diag/_RowHashFold.h>

#include <type_traits>

namespace {

namespace ss = ::crucible::safety;
namespace src = ::crucible::safety::source;
namespace dg = ::crucible::safety::diag;

using X86 = src::X86Pinned;
using Arm = src::ArmPinned;
using Port = src::PortablePinned;

static_assert(!std::is_same_v<X86, Arm>);
static_assert(!std::is_same_v<X86, Port>);
static_assert(src::arch_compatible(src::ArchTag::X86, src::ArchTag::X86));
static_assert(!src::arch_compatible(src::ArchTag::X86, src::ArchTag::Arm));
static_assert(src::arch_compatible(src::ArchTag::Portable, src::ArchTag::Arm));

static_assert(ss::ArchComposable<X86, X86>);
static_assert(ss::ArchComposable<X86, Port>);
static_assert(ss::ArchComposable<X86, src::External>);
static_assert(!ss::ArchComposable<X86, Arm>);
static_assert(!ss::ArchComposable<Arm, X86>);

static_assert(ss::arch_pin_v<X86> == src::ArchTag::X86);
static_assert(ss::arch_pin_v<Port> == src::ArchTag::Portable);
static_assert(ss::arch_pin_v<src::External> == src::ArchTag::Portable);
static_assert(ss::is_arch_pinned_v<X86>);
static_assert(!ss::is_arch_pinned_v<src::External>);

static_assert(ss::RetagAllowed<Port, X86>);  // sound weakening
static_assert(ss::RetagAllowed<Port, Arm>);  // sound weakening
static_assert(ss::RetagAllowed<X86, X86>);  // identity
static_assert(!ss::RetagAllowed<X86, Port>);  // false widening
static_assert(!ss::RetagAllowed<X86, Arm>);  // cross-trunk
static_assert(!ss::RetagAllowed<Arm, X86>);  // cross-trunk

static_assert(sizeof(ss::Tagged<int, X86>) == sizeof(int),
              "ArchPinned<Arch> must EBO-collapse in Tagged. The tag is phantom and "
              "carries no storage.");

using Bs_t = ::crucible::algebra::lattices::BarrierStrength;
using AcqRelInt = ss::BarrierGuarded<Bs_t::AcqRel, int>;
using PinnedX86 = ss::Tagged<AcqRelInt, X86>;
using PinnedArm = ss::Tagged<AcqRelInt, Arm>;
static_assert(dg::row_hash_contribution_v<PinnedX86> != dg::row_hash_contribution_v<AcqRelInt>,
              "adding source::ArchPinned<X86> must change the federation-cache slot "
              "against the bare barrier-guarded value.");
static_assert(dg::row_hash_contribution_v<PinnedX86> != dg::row_hash_contribution_v<PinnedArm>,
              "x86-pinned and ARM-pinned barrier values must hash to distinct "
              "federation-cache slots.");

}  // namespace

int main() {
    int probe = 0;
    for (int loop = 0; loop < 3; ++loop)
        probe += loop;  // not foldable to a literal

    ss::Tagged<int, Port> portable{probe};
    if (portable.value() != probe) return 1;

    // Portable to x86 is the admitted weakening direction.
    auto x86 = std::move(portable).retag<X86>();
    if (x86.value() != probe) return 2;

    // The runtime helper must agree with the constexpr relation on a pair
    // the compiler cannot fold.
    const auto a = src::ArchTag::X86;
    const auto b = (probe % 2 == 0) ? src::ArchTag::Arm : src::ArchTag::X86;
    const bool compatible = src::arch_compatible(a, b);
    // probe == 0+1+2 == 3 is odd, so b == X86 → compatible must be true.
    if (!compatible) return 3;

    return 0;
}
