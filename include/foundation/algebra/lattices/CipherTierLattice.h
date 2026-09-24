#pragma once

// Three-tier chain over where a persisted value lives, and so how fast
// it recovers.  Not how hot its access pattern is, and not what a
// function is permitted to do.
//
// The fastest-recovering tier sits at the top, so `leq(weak, strong)`
// reads "a weaker-durability consumer is satisfied by a
// stronger-durability provider".  A Hot value is admissible everywhere.
//
// A structurally identical chain grades a different axis and stays a
// separate type.  The axes are independent, so the grades must never
// collapse into one.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class CipherTierTag : std::uint8_t {
    Cold = 0,  // bottom: durable blob storage, slowest recovery
    Warm = 1,  // node-local disk, survives the process but not the disk
    Hot = 2,  // top: a peer node's RAM, fastest recovery
};

inline constexpr std::size_t cipher_tier_tag_count = ::foundation::reflect::enum_count<CipherTierTag>;

// The identifier of t, or "<unknown CipherTierTag>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view cipher_tier_tag_name(CipherTierTag t) noexcept {
    return ::foundation::reflect::enum_name(t);
}

struct CipherTierLattice : ChainLatticeOps<CipherTierTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return CipherTierTag::Cold; }
    [[nodiscard]] static constexpr element_type top() noexcept { return CipherTierTag::Hot; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "CipherTierLattice"; }

    template <CipherTierTag T>
    struct AtElement : PinnedElement<T> {
        using cipher_tier_tag_value_type = CipherTierTag;
    };

    template <CipherTierTag T>
    struct At : PinnedAt<CipherTierLattice, T, AtElement<T>> {
        static constexpr CipherTierTag tier = T;
    };
};

namespace cipher_tier_tag {
using ColdTier = CipherTierLattice::At<CipherTierTag::Cold>;
using WarmTier = CipherTierLattice::At<CipherTierTag::Warm>;
using HotTier = CipherTierLattice::At<CipherTierTag::Hot>;
}  // namespace cipher_tier_tag

namespace detail::cipher_tier_lattice_self_test {

static_assert(cipher_tier_tag_count == 3, "CipherTierTag must hold exactly the three tiers Cold, Warm and Hot.");

static_assert(verify_chain_lattice<CipherTierLattice>(),
              "CipherTierLattice: the chain order, the pinned grades or the "
              "reflected names diverged from the CipherTierTag enumerator list.");

static_assert(!UnboundedLattice<CipherTierLattice>);
static_assert(!Semiring<CipherTierLattice>);

static_assert(CipherTierLattice::bottom() == CipherTierTag::Cold);
static_assert(CipherTierLattice::top() == CipherTierTag::Hot);

static_assert(CipherTierLattice::name() == "CipherTierLattice");
static_assert(cipher_tier_tag::ColdTier::name() == "CipherTierLattice::At<Cold>");
static_assert(cipher_tier_tag::HotTier::name() == "CipherTierLattice::At<Hot>");
static_assert(CipherTierLattice::At<static_cast<CipherTierTag>(255)>::name() == "CipherTierLattice::At<?>");

static_assert(cipher_tier_tag_name(CipherTierTag::Warm) == "Warm");
static_assert(cipher_tier_tag_name(static_cast<CipherTierTag>(255)) == "<unknown CipherTierTag>");

static_assert(cipher_tier_tag::ColdTier::tier == CipherTierTag::Cold);
static_assert(cipher_tier_tag::HotTier::tier == CipherTierTag::Hot);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

// The top tier witnesses the collapse for both class and arithmetic
// values; the other two need only one witness each.
template <typename T_>
using HotGraded = Graded<ModalityKind::Absolute, cipher_tier_tag::HotTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotGraded, double);

template <typename T_>
using WarmGraded = Graded<ModalityKind::Absolute, cipher_tier_tag::WarmTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmGraded, EightByteValue);

template <typename T_>
using ColdGraded = Graded<ModalityKind::Absolute, cipher_tier_tag::ColdTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdGraded, EightByteValue);

}  // namespace detail::cipher_tier_lattice_self_test

}  // namespace foundation::algebra::lattices
