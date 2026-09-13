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

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class CipherTierTag : std::uint8_t {
    Cold = 0,  // bottom: durable blob storage, slowest recovery
    Warm = 1,  // node-local disk, survives the process but not the disk
    Hot = 2,  // top: a peer node's RAM, fastest recovery
};

inline constexpr std::size_t cipher_tier_tag_count = std::meta::enumerators_of(^^CipherTierTag).size();

[[nodiscard]] consteval std::string_view cipher_tier_tag_name(CipherTierTag t) noexcept {
    switch (t) {
        case CipherTierTag::Cold:
            return "Cold";
        case CipherTierTag::Warm:
            return "Warm";
        case CipherTierTag::Hot:
            return "Hot";
        default:
            return std::string_view{"<unknown CipherTierTag>"};
    }
}

struct CipherTierLattice : ChainLatticeOps<CipherTierTag> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return CipherTierTag::Cold; }
    [[nodiscard]] static constexpr element_type top() noexcept { return CipherTierTag::Hot; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "CipherTierLattice"; }

    template <CipherTierTag T>
    struct At {
        struct element_type {
            using cipher_tier_tag_value_type = CipherTierTag;
            [[nodiscard]] constexpr operator cipher_tier_tag_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr CipherTierTag tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case CipherTierTag::Cold:
                    return "CipherTierLattice::At<Cold>";
                case CipherTierTag::Warm:
                    return "CipherTierLattice::At<Warm>";
                case CipherTierTag::Hot:
                    return "CipherTierLattice::At<Hot>";
                default:
                    return "CipherTierLattice::At<?>";
            }
        }
    };
};

namespace cipher_tier_tag {
using ColdTier = CipherTierLattice::At<CipherTierTag::Cold>;
using WarmTier = CipherTierLattice::At<CipherTierTag::Warm>;
using HotTier = CipherTierLattice::At<CipherTierTag::Hot>;
}  // namespace cipher_tier_tag

namespace detail::cipher_tier_lattice_self_test {

static_assert(cipher_tier_tag_count == 3, "CipherTierTag must hold exactly the three tiers Cold, Warm and Hot.");

[[nodiscard]] consteval bool every_cipher_tier_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CipherTierTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (cipher_tier_tag_name([:en:]) == std::string_view{"<unknown CipherTierTag>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_cipher_tier_tag_has_name(),
              "cipher_tier_tag_name() has no arm for at least one tier, so that tier reports the "
              "'<unknown CipherTierTag>' sentinel.");

static_assert(Lattice<CipherTierLattice>);
static_assert(BoundedLattice<CipherTierLattice>);
static_assert(Lattice<cipher_tier_tag::ColdTier>);
static_assert(Lattice<cipher_tier_tag::WarmTier>);
static_assert(Lattice<cipher_tier_tag::HotTier>);
static_assert(BoundedLattice<cipher_tier_tag::HotTier>);

static_assert(!UnboundedLattice<CipherTierLattice>);
static_assert(!Semiring<CipherTierLattice>);

// Emptiness is the precondition for the grade to collapse under EBO.
static_assert(std::is_empty_v<cipher_tier_tag::ColdTier::element_type>);
static_assert(std::is_empty_v<cipher_tier_tag::WarmTier::element_type>);
static_assert(std::is_empty_v<cipher_tier_tag::HotTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<CipherTierLattice>(),
              "CipherTierLattice's chain-order axioms must hold at every "
              "(CipherTierTag)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<CipherTierLattice>(),
              "CipherTierLattice's chain order must satisfy distributivity at "
              "every (CipherTierTag)³ triple.");

static_assert(CipherTierLattice::leq(CipherTierTag::Cold, CipherTierTag::Warm));
static_assert(CipherTierLattice::leq(CipherTierTag::Warm, CipherTierTag::Hot));
static_assert(CipherTierLattice::leq(CipherTierTag::Cold, CipherTierTag::Hot));
static_assert(!CipherTierLattice::leq(CipherTierTag::Hot, CipherTierTag::Cold));
static_assert(!CipherTierLattice::leq(CipherTierTag::Hot, CipherTierTag::Warm));
static_assert(!CipherTierLattice::leq(CipherTierTag::Warm, CipherTierTag::Cold));

static_assert(CipherTierLattice::bottom() == CipherTierTag::Cold);
static_assert(CipherTierLattice::top() == CipherTierTag::Hot);

static_assert(CipherTierLattice::join(CipherTierTag::Cold, CipherTierTag::Hot) == CipherTierTag::Hot);
static_assert(CipherTierLattice::join(CipherTierTag::Warm, CipherTierTag::Cold) == CipherTierTag::Warm);
static_assert(CipherTierLattice::meet(CipherTierTag::Cold, CipherTierTag::Hot) == CipherTierTag::Cold);
static_assert(CipherTierLattice::meet(CipherTierTag::Warm, CipherTierTag::Hot) == CipherTierTag::Warm);

static_assert(CipherTierLattice::name() == "CipherTierLattice");
static_assert(cipher_tier_tag::ColdTier::name() == "CipherTierLattice::At<Cold>");
static_assert(cipher_tier_tag::WarmTier::name() == "CipherTierLattice::At<Warm>");
static_assert(cipher_tier_tag::HotTier::name() == "CipherTierLattice::At<Hot>");

[[nodiscard]] consteval bool every_at_cipher_tier_tag_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CipherTierTag));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (CipherTierLattice::At<([:en:])>::name() == std::string_view{"CipherTierLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_cipher_tier_tag_has_name(),
              "CipherTierLattice::At<T>::name() has no arm for at least one tier, so that tier reports the "
              "'CipherTierLattice::At<?>' sentinel.");

static_assert(cipher_tier_tag::ColdTier::tier == CipherTierTag::Cold);
static_assert(cipher_tier_tag::WarmTier::tier == CipherTierTag::Warm);
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

inline void runtime_smoke_test() {
    CipherTierTag a = CipherTierTag::Cold;
    CipherTierTag b = CipherTierTag::Hot;
    [[maybe_unused]] bool l1 = CipherTierLattice::leq(a, b);
    [[maybe_unused]] CipherTierTag j1 = CipherTierLattice::join(a, b);
    [[maybe_unused]] CipherTierTag m1 = CipherTierLattice::meet(a, b);
    [[maybe_unused]] CipherTierTag bot = CipherTierLattice::bottom();
    [[maybe_unused]] CipherTierTag topv = CipherTierLattice::top();

    CipherTierTag warm = CipherTierTag::Warm;
    [[maybe_unused]] CipherTierTag j2 = CipherTierLattice::join(warm, a);
    [[maybe_unused]] CipherTierTag m2 = CipherTierLattice::meet(warm, b);

    OneByteValue v{42};
    HotGraded<OneByteValue> initial{v, cipher_tier_tag::HotTier::bottom()};
    auto widened = initial.weaken(cipher_tier_tag::HotTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(cipher_tier_tag::HotTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    cipher_tier_tag::HotTier::element_type e{};
    [[maybe_unused]] CipherTierTag rec = e;
}

}  // namespace detail::cipher_tier_lattice_self_test

}  // namespace crucible::algebra::lattices
