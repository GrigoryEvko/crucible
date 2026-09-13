#pragma once

// Boolean lattice over a CPU affinity bitmask: the powerset of the core
// ids 0 through kMaxCore.  leq is set inclusion, join is union and meet
// is intersection.  bottom admits no core and top admits every core, so
// a larger set sits higher and is the more permissive claim.
//
// kWords fixes the width at 256 cores, which covers every shipped server
// part.  A fleet with a wider part bumps that one constant.
//
// The backing store is a plain array of four words rather than a
// std::bitset.  A bitset is functionally equivalent, but its
// implementation is not guaranteed trivially relocatable, and this type
// is the grade of a layout-critical carrier.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/safety/Decide.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

struct AffinityMask {
    static constexpr std::size_t kWords = 4;
    static constexpr std::size_t kBits = kWords * 64;
    static constexpr std::uint16_t kMaxCore = static_cast<std::uint16_t>(kBits - 1);

    std::array<std::uint64_t, kWords> words{};

    [[nodiscard]] constexpr bool operator==(AffinityMask const&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(AffinityMask const&) const noexcept = default;

    // A core index past the mask width would index outside the word
    // array.  The preconditions on this factory, on contains and on
    // range fence that.
    [[nodiscard]] static constexpr AffinityMask single(std::uint16_t core) noexcept
        pre(::crucible::decide::in_range<std::uint16_t>(core, 0, kMaxCore)) {
        AffinityMask m{};
        m.words[core / 64] = std::uint64_t{1} << (core % 64);
        return m;
    }

    [[nodiscard]] constexpr bool contains(std::uint16_t core) const noexcept
        pre(::crucible::decide::in_range<std::uint16_t>(core, 0, kMaxCore)) {
        return (words[core / 64] & (std::uint64_t{1} << (core % 64))) != 0;
    }

    [[nodiscard]] constexpr std::uint16_t popcount() const noexcept {
        std::uint16_t count = 0;
        for (auto w : words) {
            count = static_cast<std::uint16_t>(count + __builtin_popcountll(w));
        }
        return count;
    }

    [[nodiscard]] static constexpr AffinityMask range(std::uint16_t first_core, std::uint16_t last_core) noexcept
        pre(first_core <= last_core) pre(::crucible::decide::in_range<std::uint16_t>(last_core, 0, kMaxCore)) {
        AffinityMask m{};
        for (std::uint16_t c = first_core; c <= last_core; ++c) {
            m.words[c / 64] |= std::uint64_t{1} << (c % 64);
        }
        return m;
    }
};

struct AffinityLattice {
    using element_type = AffinityMask;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return element_type{}; }
    [[nodiscard]] static constexpr element_type top() noexcept {
        element_type m{};
        for (auto& w : m.words)
            w = std::numeric_limits<std::uint64_t>::max();
        return m;
    }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            if ((a.words[i] & b.words[i]) != a.words[i]) return false;
        }
        return true;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        element_type r{};
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            r.words[i] = a.words[i] | b.words[i];
        }
        return r;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        element_type r{};
        for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
            r.words[i] = a.words[i] & b.words[i];
        }
        return r;
    }

    // A timestamp-counter read is sound only when the thread cannot
    // migrate, so a pin proof tests the mask for exactly one core.
    [[nodiscard]] static constexpr bool is_singleton(element_type m) noexcept { return m.popcount() == 1; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "AffinityLattice"; }
};

namespace detail::affinity_lattice_self_test {

static_assert(Lattice<AffinityLattice>);
static_assert(BoundedLattice<AffinityLattice>);
static_assert(!UnboundedLattice<AffinityLattice>);
static_assert(!Semiring<AffinityLattice>);

static_assert(AffinityMask::kWords == 4);
static_assert(AffinityMask::kBits == 256);
static_assert(AffinityMask::kMaxCore == 255);
static_assert(sizeof(AffinityMask) == AffinityMask::kWords * sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<AffinityMask>);
static_assert(std::is_standard_layout_v<AffinityMask>);

static_assert(!std::is_same_v<AffinityMask, std::uint64_t>);
static_assert(!std::is_same_v<AffinityMask, std::array<std::uint64_t, 4>>);

static_assert(AffinityMask::single(0).words[0] == 0b1);
static_assert(AffinityMask::single(3).words[0] == 0b1000);
static_assert(AffinityMask::single(63).words[0] == (std::uint64_t{1} << 63));
static_assert(AffinityMask::single(64).words[1] == std::uint64_t{1});
static_assert(AffinityMask::single(127).words[1] == (std::uint64_t{1} << 63));
static_assert(AffinityMask::single(128).words[2] == std::uint64_t{1});
static_assert(AffinityMask::single(192).words[3] == std::uint64_t{1});
static_assert(AffinityMask::single(255).words[3] == (std::uint64_t{1} << 63));

static_assert(AffinityMask::single(0).contains(0));
static_assert(!AffinityMask::single(0).contains(1));
static_assert(AffinityMask::single(127).contains(127));
static_assert(!AffinityMask::single(127).contains(128));
static_assert(AffinityMask::single(192).contains(192));

static_assert(AffinityMask::single(AffinityMask::kMaxCore).contains(255));
static_assert(!AffinityMask::single(AffinityMask::kMaxCore).contains(0));

static_assert(AffinityMask::range(0, 3).contains(0));
static_assert(AffinityMask::range(0, 3).contains(3));
static_assert(!AffinityMask::range(0, 3).contains(4));
static_assert(AffinityMask::range(60, 70).contains(60));
static_assert(AffinityMask::range(60, 70).contains(63));
static_assert(AffinityMask::range(60, 70).contains(64));
static_assert(AffinityMask::range(60, 70).contains(70));
static_assert(!AffinityMask::range(60, 70).contains(71));
static_assert(AffinityMask::range(0, 191).popcount() == 192);
static_assert(AffinityMask::range(0, 255).popcount() == 256);

static_assert(AffinityMask::single(0).popcount() == 1);
static_assert(AffinityMask::single(127).popcount() == 1);
static_assert(AffinityMask::single(255).popcount() == 1);
static_assert(AffinityLattice::bottom().popcount() == 0);
static_assert(AffinityLattice::top().popcount() == AffinityMask::kBits);

static_assert(AffinityLattice::is_singleton(AffinityMask::single(0)));
static_assert(AffinityLattice::is_singleton(AffinityMask::single(255)));
static_assert(!AffinityLattice::is_singleton(AffinityLattice::bottom()),
              "The empty mask is not a singleton.  It pins no core at all.");
static_assert(!AffinityLattice::is_singleton(AffinityMask::range(0, 1)),
              "A two-core mask is not a singleton.  A timestamp read that can "
              "land on either core is unsound.");
static_assert(!AffinityLattice::is_singleton(AffinityLattice::top()));

static_assert(AffinityLattice::leq(AffinityMask{}, AffinityMask::range(0, 7)));
static_assert(AffinityLattice::leq(AffinityMask::single(127), AffinityMask::range(127, 192)));
static_assert(!AffinityLattice::leq(AffinityMask::single(127), AffinityMask::range(0, 63)));
static_assert(AffinityLattice::leq(AffinityLattice::bottom(), AffinityLattice::top()));

static_assert(AffinityLattice::bottom() == AffinityMask{});

[[nodiscard]] consteval bool top_has_all_bits() noexcept {
    auto t = AffinityLattice::top();
    for (auto w : t.words) {
        if (w != std::numeric_limits<std::uint64_t>::max()) return false;
    }
    return true;
}
static_assert(top_has_all_bits());

[[nodiscard]] consteval bool join_meet_witness() noexcept {
    AffinityMask a = AffinityMask::range(0, 63);
    AffinityMask b = AffinityMask::range(64, 127);
    AffinityMask jab = AffinityLattice::join(a, b);
    AffinityMask mab = AffinityLattice::meet(a, b);
    return jab.popcount() == 128 && mab.popcount() == 0 && jab.contains(0) && jab.contains(127) && !mab.contains(0);
}
static_assert(join_meet_witness());

[[nodiscard]] consteval bool bound_identities() noexcept {
    AffinityMask m = AffinityMask::single(192);
    return AffinityLattice::join(m, AffinityLattice::bottom()) == m
        && AffinityLattice::meet(m, AffinityLattice::top()) == m;
}
static_assert(bound_identities());

[[nodiscard]] consteval bool idempotence_witness() noexcept {
    AffinityMask m = AffinityMask::range(0, 191);
    return AffinityLattice::join(m, m) == m && AffinityLattice::meet(m, m) == m;
}
static_assert(idempotence_witness());

[[nodiscard]] consteval bool distributive_witness() noexcept {
    AffinityMask a = AffinityMask::range(0, 63);
    AffinityMask b = AffinityMask::range(32, 95);
    AffinityMask c = AffinityMask::range(64, 127);
    auto lhs = AffinityLattice::meet(a, AffinityLattice::join(b, c));
    auto rhs = AffinityLattice::join(AffinityLattice::meet(a, b), AffinityLattice::meet(a, c));
    return lhs == rhs;
}
static_assert(distributive_witness());

[[nodiscard]] consteval bool complement_witness() noexcept {
    AffinityMask a = AffinityMask::range(0, 31);
    AffinityMask cmp{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        cmp.words[i] = ~a.words[i];
    }
    return AffinityLattice::join(a, cmp) == AffinityLattice::top()
        && AffinityLattice::meet(a, cmp) == AffinityLattice::bottom();
}
static_assert(complement_witness());

[[nodiscard]] consteval bool transitivity_witness() noexcept {
    AffinityMask small = AffinityMask::single(7);
    AffinityMask medium = AffinityMask::range(0, 31);
    AffinityMask large = AffinityMask::range(0, 191);
    return AffinityLattice::leq(small, medium) && AffinityLattice::leq(medium, large)
        && AffinityLattice::leq(small, large) && AffinityLattice::leq(AffinityLattice::bottom(), small)
        && AffinityLattice::leq(large, AffinityLattice::top());
}
static_assert(transitivity_witness());

[[nodiscard]] consteval bool de_morgan_witness() noexcept {
    AffinityMask a = AffinityMask::range(0, 31);
    AffinityMask b = AffinityMask::range(16, 47);
    AffinityMask cmp_a{}, cmp_b{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        cmp_a.words[i] = ~a.words[i];
        cmp_b.words[i] = ~b.words[i];
    }

    AffinityMask not_join_ab{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        not_join_ab.words[i] = ~AffinityLattice::join(a, b).words[i];
    }
    AffinityMask meet_neg = AffinityLattice::meet(cmp_a, cmp_b);
    bool law1 = not_join_ab == meet_neg;

    AffinityMask not_meet_ab{};
    for (std::size_t i = 0; i < AffinityMask::kWords; ++i) {
        not_meet_ab.words[i] = ~AffinityLattice::meet(a, b).words[i];
    }
    AffinityMask join_neg = AffinityLattice::join(cmp_a, cmp_b);
    bool law2 = not_meet_ab == join_neg;

    return law1 && law2;
}
static_assert(de_morgan_witness());

static_assert(verify_bounded_lattice_axioms_at<AffinityLattice>(AffinityLattice::bottom(), AffinityMask::range(0, 31),
                                                                AffinityLattice::top()));
static_assert(verify_bounded_lattice_axioms_at<AffinityLattice>(AffinityMask::single(0), AffinityMask::single(127),
                                                                AffinityMask::single(255)));
static_assert(verify_bounded_lattice_axioms_at<AffinityLattice>(AffinityMask::range(0, 63), AffinityMask::range(32, 95),
                                                                AffinityMask::range(64, 127)));
static_assert(verify_distributive_lattice<AffinityLattice>(AffinityLattice::bottom(), AffinityMask::range(0, 31),
                                                           AffinityLattice::top()));
static_assert(verify_distributive_lattice<AffinityLattice>(AffinityMask::range(0, 63), AffinityMask::range(32, 95),
                                                           AffinityMask::range(64, 127)));
static_assert(verify_distributive_lattice<AffinityLattice>(AffinityMask::single(0), AffinityMask::single(127),
                                                           AffinityMask::single(255)));

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    AffinityMask bot = AffinityLattice::bottom();
    AffinityMask topv = AffinityLattice::top();
    AffinityMask bergamo_full = AffinityMask::range(0, 191);

    [[maybe_unused]] bool l1 = AffinityLattice::leq(bot, topv);
    [[maybe_unused]] bool l2 = AffinityLattice::leq(bergamo_full, topv);
    [[maybe_unused]] AffinityMask j = AffinityLattice::join(bergamo_full, bot);
    [[maybe_unused]] AffinityMask m = AffinityLattice::meet(bergamo_full, topv);

    if (bergamo_full.popcount() != 192) std::abort();

    AffinityMask core_0 = AffinityMask::single(0);
    AffinityMask core_191 = AffinityMask::single(191);
    AffinityMask core_255 = AffinityMask::single(255);
    if (!core_0.contains(0)) std::abort();
    if (!core_191.contains(191)) std::abort();
    if (!core_255.contains(255)) std::abort();
    if (core_0.contains(1)) std::abort();

    AffinityMask joined = AffinityLattice::join(core_0, core_191);
    if (!joined.contains(0)) std::abort();
    if (!joined.contains(191)) std::abort();

    AffinityMask intersected = AffinityLattice::meet(AffinityMask::range(0, 127), AffinityMask::range(64, 191));
    if (intersected.popcount() != 64) std::abort();

    using AffinityGraded = Graded<ModalityKind::Absolute, AffinityLattice, double>;
    AffinityGraded v{3.14, AffinityMask::range(0, 31)};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

}  // namespace detail::affinity_lattice_self_test

}  // namespace crucible::algebra::lattices
