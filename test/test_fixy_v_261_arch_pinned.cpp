// test_fixy_v_261_arch_pinned.cpp — positive sentinel for FIXY-V-261.
//
// safety/source/Arch.h ships only header-embedded static_asserts (the
// v261_self_test block).  Per the header-only static_assert blind spot
// discipline, that block is NOT compiled under the project warning flags
// unless a .cpp TU includes the header — this is that TU.  It re-asserts
// the load-bearing properties at this TU's instantiation context AND
// exercises the tag + composition gate + retag at RUNTIME (a non-constant
// Tagged<int, ArchPinned> value), catching consteval/inline-body bugs a
// pure static_assert pass would mask.
//
// Mirrors the V-255 BarrierGuarded sentinel: pin distinctness, the
// composition truth table, arch_pin extraction, the ArchComposable gate,
// the retag direction discipline, AND the Tagged<BarrierGuarded<...>,
// ArchPinned<...>> nest the V-255 sentinel reserved for this tag.

#include <crucible/safety/source/Arch.h>

#include <crucible/safety/BarrierGuarded.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/diag/RowHashFold.h>

#include <type_traits>

namespace {

namespace ss  = ::crucible::safety;
namespace src = ::crucible::safety::source;
namespace dg  = ::crucible::safety::diag;

using X86  = src::X86Pinned;
using Arm  = src::ArmPinned;
using Port = src::PortablePinned;

// ── Compile-time re-assertion (header-only blind-spot guard) ───────
static_assert(!std::is_same_v<X86, Arm>);
static_assert(!std::is_same_v<X86, Port>);
static_assert(src::arch_compatible(src::ArchTag::X86, src::ArchTag::X86));
static_assert(!src::arch_compatible(src::ArchTag::X86, src::ArchTag::Arm));
static_assert(src::arch_compatible(src::ArchTag::Portable, src::ArchTag::Arm));

static_assert(ss::ArchComposable<X86, X86>);
static_assert(ss::ArchComposable<X86, Port>);
static_assert(ss::ArchComposable<X86, src::External>);  // non-pin composes
static_assert(!ss::ArchComposable<X86, Arm>);            // the gate's reason to exist
static_assert(!ss::ArchComposable<Arm, X86>);            // symmetric

static_assert(ss::arch_pin_v<X86>  == src::ArchTag::X86);
static_assert(ss::arch_pin_v<Port> == src::ArchTag::Portable);
static_assert(ss::arch_pin_v<src::External> == src::ArchTag::Portable);
static_assert(ss::is_arch_pinned_v<X86>);
static_assert(!ss::is_arch_pinned_v<src::External>);

// ── retag direction discipline ─────────────────────────────────────
static_assert(ss::RetagAllowed<Port, X86>);   // sound weakening
static_assert(ss::RetagAllowed<Port, Arm>);   // sound weakening
static_assert(ss::RetagAllowed<X86, X86>);    // identity (V-022)
static_assert(!ss::RetagAllowed<X86, Port>);  // false widening
static_assert(!ss::RetagAllowed<X86, Arm>);   // cross-trunk
static_assert(!ss::RetagAllowed<Arm, X86>);   // cross-trunk

// ── EBO collapse — ArchPinned adds no storage to Tagged ────────────
static_assert(sizeof(ss::Tagged<int, X86>) == sizeof(int),
    "FIXY-V-261: ArchPinned<Arch> must EBO-collapse in Tagged — the "
    "tag is phantom and carries no storage.");

// ── V-255 nest: the slot ArchPinned was reserved for ───────────────
// Tagged<BarrierGuarded<AcqRel, int>, ArchPinned<X86>> is the canonical
// fence-then-pin composition.  It must (a) be a valid type, (b) hash
// distinctly from the bare barrier-guarded value (adding a source tag
// changes the federation-cache slot), and (c) hash distinctly from the
// other trunk's pin (X86 vs ARM are different cache keys).
using Bs_t       = ::crucible::algebra::lattices::BarrierStrength;
using AcqRelInt  = ss::BarrierGuarded<Bs_t::AcqRel, int>;
using PinnedX86  = ss::Tagged<AcqRelInt, X86>;
using PinnedArm  = ss::Tagged<AcqRelInt, Arm>;
static_assert(dg::row_hash_contribution_v<PinnedX86>
              != dg::row_hash_contribution_v<AcqRelInt>,
    "FIXY-V-261: adding source::ArchPinned<X86> MUST change the "
    "federation-cache slot vs the bare barrier-guarded value.");
static_assert(dg::row_hash_contribution_v<PinnedX86>
              != dg::row_hash_contribution_v<PinnedArm>,
    "FIXY-V-261: x86-pinned and ARM-pinned barrier values MUST hash "
    "to distinct federation-cache slots.");

}  // namespace

int main() {
    // ── Runtime smoke — non-constant value through the wrapper ─────
    int probe = 0;
    for (int loop = 0; loop < 3; ++loop) probe += loop;  // not foldable to a literal

    // Construct an arch-pinned value at runtime.
    ss::Tagged<int, Port> portable{probe};
    if (portable.value() != probe) return 1;

    // Sound weakening: Portable → x86 at runtime (the admitted direction).
    auto x86 = std::move(portable).retag<X86>();
    if (x86.value() != probe) return 2;

    // The composition gate is a compile-time fact; confirm the runtime
    // helper agrees with the constexpr relation for a non-constant pair.
    const auto a = src::ArchTag::X86;
    const auto b = (probe % 2 == 0) ? src::ArchTag::Arm : src::ArchTag::X86;
    const bool compatible = src::arch_compatible(a, b);
    // probe == 0+1+2 == 3 is odd, so b == X86 → compatible must be true.
    if (!compatible) return 3;

    return 0;
}
