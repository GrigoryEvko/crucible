#pragma once

#include <crucible/safety/_Tagged.h>

#include <type_traits>

namespace crucible::safety::source {

// The CPU instruction-set trunk a value is pinned to. A value acquires a pin
// when it carries the result of a fence or a hand-vectorized computation, both
// of which are spelled per trunk.
enum class ArchTag : unsigned char {
    X86 = 0,  // mfence/lfence/sfence, SSE through AVX-512
    Arm = 1,  // DMB ISH, NEON and SVE
    Portable = 2,  // no trunk constraint
};

// Two pins compose when they are equal or either side is Portable. The one
// rejected pair is two distinct concrete trunks: a binary holding both x86 and
// ARM encodings faults with an illegal instruction on whichever host it lands.
[[nodiscard]] constexpr bool arch_compatible(ArchTag a, ArchTag b) noexcept {
    return a == b || a == ArchTag::Portable || b == ArchTag::Portable;
}

template <ArchTag Arch>
struct ArchPinned {
    static constexpr ArchTag arch = Arch;
};

using X86Pinned = ArchPinned<ArchTag::X86>;
using ArmPinned = ArchPinned<ArchTag::Arm>;
using PortablePinned = ArchPinned<ArchTag::Portable>;

}  // namespace crucible::safety::source

namespace crucible::safety {

// A source with no pin maps to Portable rather than being ill-formed, so the
// gate below stays vacuously true for every source outside this axis. The only
// way it can fail is two concrete trunks meeting.
template <typename Source>
inline constexpr source::ArchTag arch_pin_v = source::ArchTag::Portable;
template <source::ArchTag Arch>
inline constexpr source::ArchTag arch_pin_v<source::ArchPinned<Arch>> = Arch;

// ArchPinned<Portable> reports true here. It is an explicit claim of trunk
// independence, which arch_pin_v alone cannot tell apart from having no pin.
template <typename Source>
inline constexpr bool is_arch_pinned_v = false;
template <source::ArchTag Arch>
inline constexpr bool is_arch_pinned_v<source::ArchPinned<Arch>> = true;

template <typename SourceA, typename SourceB>
inline constexpr bool arch_composable_v = source::arch_compatible(arch_pin_v<SourceA>, arch_pin_v<SourceB>);

template <typename SourceA, typename SourceB>
concept ArchComposable = arch_composable_v<SourceA, SourceB>;

template <>
struct retag_policy<source::PortablePinned, source::X86Pinned> {
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::PortablePinned, source::ArmPinned> {
    static constexpr bool allowed = true;
};

}  // namespace crucible::safety

namespace crucible::safety::source::detail::v261_self_test {

namespace ss = ::crucible::safety;

static_assert(!std::is_same_v<X86Pinned, ArmPinned>, "X86Pinned and ArmPinned must be distinct types: an x86 binary "
                                                     "does not run on ARM and the reverse also holds.");
static_assert(!std::is_same_v<X86Pinned, PortablePinned>, "X86Pinned and PortablePinned must be distinct types.");
static_assert(!std::is_same_v<ArmPinned, PortablePinned>, "ArmPinned and PortablePinned must be distinct types.");

static_assert(X86Pinned::arch == ArchTag::X86);
static_assert(ArmPinned::arch == ArchTag::Arm);
static_assert(PortablePinned::arch == ArchTag::Portable);

static_assert(arch_compatible(ArchTag::X86, ArchTag::X86), "The same trunk must compose.");
static_assert(arch_compatible(ArchTag::Arm, ArchTag::Arm), "The same trunk must compose.");
static_assert(!arch_compatible(ArchTag::X86, ArchTag::Arm),
              "x86 and ARM must not compose: the binary would fault on an illegal instruction.");
static_assert(!arch_compatible(ArchTag::Arm, ArchTag::X86),
              "Cross-trunk rejection is symmetric, so ARM and x86 must not compose either.");
static_assert(arch_compatible(ArchTag::X86, ArchTag::Portable), "Portable must compose with x86.");
static_assert(arch_compatible(ArchTag::Portable, ArchTag::Arm), "Portable must compose with ARM.");
static_assert(arch_compatible(ArchTag::Portable, ArchTag::Portable), "Portable must compose with itself.");

static_assert(ss::arch_pin_v<X86Pinned> == ArchTag::X86);
static_assert(ss::arch_pin_v<ArmPinned> == ArchTag::Arm);
static_assert(ss::arch_pin_v<PortablePinned> == ArchTag::Portable);
static_assert(ss::arch_pin_v<External> == ArchTag::Portable,
              "A source outside the arch axis must default to Portable so it imposes no trunk "
              "constraint on composition.");
static_assert(ss::is_arch_pinned_v<X86Pinned>, "X86Pinned is an arch pin.");
static_assert(!ss::is_arch_pinned_v<External>, "A generic provenance tag is not an arch pin.");

static_assert(ss::ArchComposable<X86Pinned, X86Pinned>, "Same-trunk composition must be admitted.");
static_assert(ss::ArchComposable<X86Pinned, PortablePinned>, "A concrete trunk with Portable must be admitted.");
static_assert(ss::ArchComposable<X86Pinned, External>,
              "A concrete pin with a source outside the arch axis must be admitted, because that "
              "source imposes no trunk constraint.");
static_assert(!ss::ArchComposable<X86Pinned, ArmPinned>, "Composing x86 with ARM must be rejected by the gate.");
static_assert(!ss::ArchComposable<ArmPinned, X86Pinned>, "The gate is symmetric, so ARM with x86 must be rejected.");

static_assert(ss::retag_policy<PortablePinned, X86Pinned>::allowed,
              "Portable into x86 is a sound weakening and must be admitted.");
static_assert(ss::retag_policy<PortablePinned, ArmPinned>::allowed,
              "Portable into ARM is a sound weakening and must be admitted.");
static_assert(ss::RetagAllowed<PortablePinned, X86Pinned>,
              "The RetagAllowed concept must see the Portable into x86 admittance.");
static_assert(ss::retag_policy<X86Pinned, X86Pinned>::allowed, "Identity retag must be admitted.");
static_assert(!ss::retag_policy<X86Pinned, PortablePinned>::allowed,
              "x86 into Portable is a false widening, because x86 code faults on ARM. It must "
              "stay rejected by the fail-closed primary.");
static_assert(!ss::retag_policy<X86Pinned, ArmPinned>::allowed, "Relabelling x86 as ARM must stay rejected.");
static_assert(!ss::retag_policy<ArmPinned, X86Pinned>::allowed, "Relabelling ARM as x86 must stay rejected.");
static_assert(!ss::RetagAllowed<X86Pinned, ArmPinned>, "The RetagAllowed concept must reject a cross-trunk retag.");

}  // namespace crucible::safety::source::detail::v261_self_test
