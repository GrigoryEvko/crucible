// The compile-time checks of foundation/algebra/lattices/AffinityLattice.h.

#include <foundation/algebra/lattices/AffinityLattice.h>

namespace foundation::algebra::lattices {

namespace detail::affinity_lattice_self_test {

static_assert(Lattice<AffinityLattice>);
static_assert(BoundedLattice<AffinityLattice>);
static_assert(!UnboundedLattice<AffinityLattice>);
static_assert(!Semiring<AffinityLattice>);

static_assert(AffinityMask::kBits == 1024);
static_assert(AffinityMask::kMaxCore == 1023);
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

static_assert(AffinityMask::single(383).words[5] == (std::uint64_t{1} << 63));
static_assert(AffinityMask::single(AffinityMask::kMaxCore).contains(1023));
static_assert(AffinityMask::single(AffinityMask::kMaxCore).words[15] == (std::uint64_t{1} << 63));
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

}  // namespace detail::affinity_lattice_self_test

}  // namespace foundation::algebra::lattices
