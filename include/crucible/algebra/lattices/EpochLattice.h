#pragma once

// Bounded chain over the fleet epoch: the cluster-wide, consensus-
// committed membership generation that advances whenever a peer joins,
// a peer is evicted, or the fleet reshards.
//
// The order is numeric, so an older view sits below a newer one and the
// join of two views is the more recent.  A gate asking for at least
// epoch N then admits exactly the values at or above it.
//
// The order knows nothing about history.  Nothing here stops a caller
// constructing an older epoch after a newer one, because the lattice
// sees two numbers and not a sequence of events.  Forward progress is a
// property of where the number comes from, and every construction site
// must derive it from the commit log rather than from an argument it was
// handed.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

// A distinct type, not a bare integer.  Several unrelated axes are also
// 64-bit unsigned counters, and separate types are what keep one from
// being assigned into another.
struct Epoch {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(Epoch const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(Epoch const&) const noexcept = default;

    [[nodiscard]] constexpr operator std::uint64_t() const noexcept { return value; }
};

struct EpochLattice {
    using element_type = Epoch;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{0}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        return element_type{std::numeric_limits<std::uint64_t>::max()};
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a.value <= b.value; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return element_type{a.value >= b.value ? a.value : b.value};
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return element_type{a.value <= b.value ? a.value : b.value};
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "EpochLattice"; }
};

namespace detail::epoch_lattice_self_test {

static_assert(Lattice<EpochLattice>);
static_assert(BoundedLattice<EpochLattice>);
static_assert(!UnboundedLattice<EpochLattice>);
static_assert(!Semiring<EpochLattice>);

static_assert(sizeof(Epoch) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Epoch>);
static_assert(std::is_standard_layout_v<Epoch>);

static_assert(!std::is_same_v<Epoch, std::uint64_t>);

// There is no assertion here that this counter differs from the sibling
// axis, because the sibling type is not in scope in this header.  That
// assertion belongs where both types are guaranteed present.

static_assert(EpochLattice::leq(Epoch{0}, Epoch{1}));
static_assert(EpochLattice::leq(Epoch{42}, Epoch{42}));
static_assert(!EpochLattice::leq(Epoch{99}, Epoch{42}));
static_assert(EpochLattice::leq(EpochLattice::bottom(), EpochLattice::top()));

static_assert(EpochLattice::bottom().value == 0);
static_assert(EpochLattice::top().value == std::numeric_limits<std::uint64_t>::max());

static_assert(EpochLattice::join(Epoch{3}, Epoch{7}).value == 7);
static_assert(EpochLattice::join(Epoch{7}, Epoch{3}).value == 7);
static_assert(EpochLattice::meet(Epoch{3}, Epoch{7}).value == 3);

static_assert(EpochLattice::join(Epoch{42}, EpochLattice::bottom()) == Epoch{42});
static_assert(EpochLattice::meet(Epoch{42}, EpochLattice::top()) == Epoch{42});

static_assert(EpochLattice::join(Epoch{99}, Epoch{99}).value == 99);
static_assert(EpochLattice::meet(Epoch{99}, Epoch{99}).value == 99);

// The interior witnesses matter: bottom and top satisfy distributivity
// for reasons that have nothing to do with the order between them.
[[nodiscard]] consteval bool distributive_witness() noexcept {
    Epoch a{2};
    Epoch b{5};
    Epoch c{8};
    auto lhs = EpochLattice::meet(a, EpochLattice::join(b, c));
    auto rhs = EpochLattice::join(EpochLattice::meet(a, b), EpochLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

static_assert(verify_bounded_lattice_axioms_at<EpochLattice>(EpochLattice::bottom(), Epoch{1024}, EpochLattice::top()));
static_assert(verify_bounded_lattice_axioms_at<EpochLattice>(Epoch{0}, Epoch{42}, Epoch{99}));
static_assert(verify_bounded_lattice_axioms_at<EpochLattice>(Epoch{1}, Epoch{2}, Epoch{3}));
static_assert(verify_distributive_lattice<EpochLattice>(EpochLattice::bottom(), Epoch{1024}, EpochLattice::top()));
static_assert(verify_distributive_lattice<EpochLattice>(Epoch{2}, Epoch{5}, Epoch{8}));
static_assert(verify_distributive_lattice<EpochLattice>(Epoch{7}, Epoch{7}, Epoch{42}));

static_assert([] consteval {
    Epoch e{42};
    std::uint64_t n = e;
    return n == 42;
}());

inline void runtime_smoke_test() {
    Epoch bot = EpochLattice::bottom();
    Epoch topv = EpochLattice::top();
    Epoch mid{1024};
    [[maybe_unused]] bool l = EpochLattice::leq(bot, topv);
    [[maybe_unused]] Epoch j = EpochLattice::join(mid, topv);
    [[maybe_unused]] Epoch m = EpochLattice::meet(mid, bot);

    Epoch e_at_genesis{0};
    Epoch e_after_join{1};
    Epoch e_after_reshard{2};
    Epoch most_recent = EpochLattice::join(EpochLattice::join(e_at_genesis, e_after_join), e_after_reshard);
    if (most_recent.value != 2u) std::abort();

    std::uint64_t total = mid;
    if (total != 1024u) std::abort();

    using EpochGraded = Graded<ModalityKind::Absolute, EpochLattice, int>;
    EpochGraded v{42, Epoch{16}};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::epoch_lattice_self_test

}  // namespace crucible::algebra::lattices
