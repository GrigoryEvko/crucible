// The compile-time checks of foundation/algebra/lattices/MemoryScopeLattice.h.

#include <foundation/algebra/lattices/MemoryScopeLattice.h>

namespace foundation::algebra::lattices {

namespace detail::memory_scope_lattice_self_test {

static_assert(::foundation::reflect::enum_count<MemoryScope> == 8,
              "The MemoryScope catalog changed size.  Confirm the intent: a new scope takes the next free value "
              "inside the high nibble of its chain, and the chain predicates read that nibble.");

static_assert(Lattice<MemoryScopeLattice>);
static_assert(BoundedLattice<MemoryScopeLattice>);
static_assert(!UnboundedLattice<MemoryScopeLattice>);
static_assert(!Semiring<MemoryScopeLattice>);

static_assert(MemoryScopeLattice::bottom() == MemoryScope::Thread);
static_assert(MemoryScopeLattice::top() == MemoryScope::System);

static_assert(mem_scope_is_accel(MemoryScope::Warp));
static_assert(mem_scope_is_accel(MemoryScope::Gpu));
static_assert(!mem_scope_is_accel(MemoryScope::Inner));
static_assert(!mem_scope_is_accel(MemoryScope::Thread));
static_assert(!mem_scope_is_accel(MemoryScope::System));
static_assert(mem_scope_is_arm(MemoryScope::Inner));
static_assert(mem_scope_is_arm(MemoryScope::Outer));
static_assert(!mem_scope_is_arm(MemoryScope::Cta));
static_assert(!mem_scope_is_arm(MemoryScope::Thread));
static_assert(!mem_scope_is_arm(MemoryScope::System));
static_assert(mem_scope_same_trunk(MemoryScope::Warp, MemoryScope::Gpu));
static_assert(mem_scope_same_trunk(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!mem_scope_same_trunk(MemoryScope::Cta, MemoryScope::Inner));
static_assert(!mem_scope_same_trunk(MemoryScope::Thread, MemoryScope::Inner));
static_assert(!mem_scope_same_trunk(MemoryScope::System, MemoryScope::Cta));
// A value outside the enum holds no chain, although its high nibble is the
// nibble of the accelerator chain.
static_assert(!mem_scope_is_accel(static_cast<MemoryScope>(0x14)) && !mem_scope_is_arm(static_cast<MemoryScope>(0x22)));

// The partial-order axioms at every triple, and leq, join and meet in
// agreement at every pair, walked by reflection over the enumerators.
static_assert(verify_enum_lattice_exhaustive<MemoryScopeLattice>(),
              "The partial-order axioms must hold at every triple of the eight "
              "scopes.  A failure means leq, join or meet is wrong for some pair, or "
              "the routing between Thread, System, same-chain rank and cross-chain "
              "is wrong.");

// Within one chain the rank is the low nibble, so the order there is the
// declaration order of the chain's members.  Every accelerator scope
// must stay incomparable to every host domain in both directions, with
// System as the join and Thread as the meet of a cross-chain pair.
// Admitting one chain for the other would let a fence that orders
// nothing on the requesting side stand in for one that does.
[[nodiscard]] consteval bool chains_order_within_and_not_across() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^MemoryScope));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            constexpr MemoryScope a = [:ea:];
            constexpr MemoryScope b = [:eb:];
            if constexpr (mem_scope_same_trunk(a, b)) {
                const bool ranked = std::to_underlying(a) <= std::to_underlying(b);
                if (MemoryScopeLattice::leq(a, b) != ranked) return false;
            } else if constexpr ((mem_scope_is_accel(a) && mem_scope_is_arm(b))
                                 || (mem_scope_is_arm(a) && mem_scope_is_accel(b))) {
                if (MemoryScopeLattice::leq(a, b)) return false;
                if (MemoryScopeLattice::join(a, b) != MemoryScope::System) return false;
                if (MemoryScopeLattice::meet(a, b) != MemoryScope::Thread) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(chains_order_within_and_not_across(),
              "The accelerator chain and the host chain must each order by rank and "
              "must stay incomparable to each other, joined only at System and met "
              "only at Thread.");

static_assert(MemoryScopeLattice::leq(MemoryScope::Cta, MemoryScope::Gpu),
              "A device-wide fence must satisfy a block-scope requirement.");
static_assert(!MemoryScopeLattice::leq(MemoryScope::Gpu, MemoryScope::Cta),
              "A block-scope fence is too narrow for a device-wide requirement.");
static_assert(!MemoryScopeLattice::leq(MemoryScope::Outer, MemoryScope::Inner),
              "An outer-shareable fence is wider than an inner-shareable "
              "requirement, not narrower, so the descending direction is false.");

// A bounded order with a single top and bottom and with two unordered internal
// chains cannot be distributive, so the failure below is structural rather than
// a defect.  It is pinned so that anyone merging the two chains has to confront
// the assertion first.
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = MemoryScopeLattice;
    auto lhs = L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer);
    auto rhs = L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer), L::meet(MemoryScope::Inner, MemoryScope::Outer));
    return lhs == MemoryScope::Outer && rhs == MemoryScope::Inner && lhs != rhs;
}
static_assert(non_distributive_witness(), "MemoryScopeLattice must stay non-distributive.  A failure means either "
                                          "the accelerator and host chains were collapsed into one, which "
                                          "destroys their incomparability, or an intermediate element was added "
                                          "that closed the distributivity gap.  Audit before resolving.");

// The shape of every At<scope>, walked by reflection.
static_assert(verify_pinned_at<MemoryScopeLattice>(),
              "MemoryScopeLattice::At<S>: a pinned grade lost its emptiness, its "
              "conversion back to S, or its reflected name.");

static_assert(MemoryScopeLattice::name() == "MemoryScopeLattice");
static_assert(memory_scope::ThreadScope::name() == "MemoryScopeLattice::At<Thread>");
static_assert(memory_scope::SystemScope::name() == "MemoryScopeLattice::At<System>");
static_assert(MemoryScopeLattice::At<static_cast<MemoryScope>(0x30)>::name() == "MemoryScopeLattice::At<?>");

static_assert(memory_scope::CtaScope::scope == MemoryScope::Cta);
static_assert(memory_scope::SystemScope::scope == MemoryScope::System);

}  // namespace detail::memory_scope_lattice_self_test

}  // namespace foundation::algebra::lattices
